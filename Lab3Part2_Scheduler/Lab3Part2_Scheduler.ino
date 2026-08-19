/*
 * File: Lab3Part2_Scheduler.ino
 * Author(s): Jonathan Lu, Sparsh Dadhich
 * Date: 06-May-2026
 * ChatGPT : 846
 * Version: 1.0
 * Description: ECE 474 Lab 3 Part II. This sketch implements a synchronized,
 *              non-preemptive priority scheduler using Task Control Blocks
 *              (TCBs). It runs the LED blinker, LCD counter, music player,
 *              alphabet printer, and priority updater tasks required by the lab.
 *
 * External source acknowledgement: The assignment requirements, task names, and
 * priority schemes are from the ECE 474 Lab 3 handout/briefing. This code was
 * drafted with ChatGPT assistance and should be reviewed and understood by the
 * submitting team before turn-in, as required by the course code guidelines.
 */

// =============================== Includes ===============================
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_arduino_version.h>

// ================================ Macros =================================
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#define LED_PIN 5
#define BUZZER_PIN 6
#define SCHEDULER_MONITOR_PIN 7

#define BUZZER_CHANNEL 0
#define BUZZER_RESOLUTION_BITS 8
#define BUZZER_IDLE_FREQUENCY 1000

#define SCHEDULER_TIMER_HZ 1000000UL
#define SCHEDULER_TICK_US 1000UL
#define SCHEDULER_TICK_MS 1UL

#define LED_TOGGLE_MS 63UL
#define LCD_COUNTER_PERIOD_MS 2000UL
#define NOTE_DURATION_MS 500UL
#define ALPHABET_PERIOD_MS 500UL
#define UPDATER_FIRST_SLEEP_MS 30000UL
#define UPDATER_RECHECK_MS 500UL

#define MIN_PRIORITY 1
#define MAX_PRIORITY 7
#define PRIORITY_LEVEL_COUNT 7

// ================================ Types ==================================
typedef void (*TaskFunction)(void);

enum TaskState {
  TASK_RUNNING,
  TASK_READY,
  TASK_HALTED,
  TASK_SLEEPING
};

enum TaskIndex {
  TASK_A_LED = 0,
  TASK_B_COUNTER = 1,
  TASK_C_MUSIC = 2,
  TASK_D_ALPHABET = 3,
  TASK_E_UPDATER = 4,
  TASK_F_MONITOR = 5,
  TASK_COUNT = 6
};

struct TCB {
  TaskFunction function;
  const char *name;
  uint8_t pid;
  uint8_t priority;
  TaskState state;
  unsigned long sleepRemainingMs;
};

// =========================== Global Constants ============================
const uint8_t prioritySchemes[3][TASK_COUNT] = {
  {2, 3, 4, 5, 1, 7},
  {5, 4, 2, 3, 1, 7},
  {2, 2, 2, 2, 1, 7}
};

const uint16_t melodyFrequencies[] = {
  262, 294, 330, 349, 392, 440, 494, 523, 587, 659
};
const uint8_t melodyLength = sizeof(melodyFrequencies) / sizeof(melodyFrequencies[0]);

// ========================== Global Objects/State ==========================
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

hw_timer_t *schedulerTimer = NULL;
portMUX_TYPE schedulerTimerMux = portMUX_INITIALIZER_UNLOCKED;
volatile unsigned long pendingSchedulerTicks = 0;

TCB tasks[TASK_COUNT];
int8_t lastServedAtPriority[PRIORITY_LEVEL_COUNT + 1];
int8_t currentTaskIndex = -1;
uint8_t currentPriorityScheme = 0;

bool ledState = false;
uint8_t lcdCounterValue = 1;
uint8_t lcdCounterPasses = 0;
uint8_t melodyIndex = 0;
uint8_t melodyPasses = 0;
uint8_t alphabetIndex = 0;
uint8_t alphabetPasses = 0;
bool updaterWaitingForCompletion = false;

// ========================== Function Prototypes ===========================
void IRAM_ATTR schedulerTimerISR(void);
void setupSchedulerTimer(void);
void initializeTCBs(void);
void applyPriorityScheme(uint8_t schemeIndex);
void resetRunnableTasks(void);
void resetTaskLocalState(void);
void displayFixedLine(uint8_t row, const String &text);
void displayPriorityScheme(uint8_t schemeIndex);
void updateSleepingTasks(unsigned long elapsedMs);
int8_t chooseNextReadyTask(void);
void runSchedulerTick(unsigned long elapsedMs);
void sleep_me(unsigned long durationMs);
void halt_me(void);
const char *stateToString(TaskState state);

void taskLedBlinker(void);
void taskLcdCounter(void);
void taskMusicPlayer(void);
void taskAlphabetPrinter(void);
void taskPriorityUpdater(void);
void taskSchedulerMonitor(void);

void setupBuzzer(void);
void startBuzzer(uint16_t frequencyHz);
void stopBuzzer(void);

// ============================== Arduino Setup =============================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(SCHEDULER_MONITOR_PIN, OUTPUT);
  digitalWrite(SCHEDULER_MONITOR_PIN, LOW);

  setupBuzzer();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  initializeTCBs();
  applyPriorityScheme(currentPriorityScheme);

  displayPriorityScheme(currentPriorityScheme);
  Serial.println("\n\nStarting Priority Scheme 1\n");
  delay(2000);
  lcd.clear();

  setupSchedulerTimer();
}

// =============================== Arduino Loop =============================
void loop() {
  unsigned long localTicks = 0;

  portENTER_CRITICAL(&schedulerTimerMux);
  localTicks = pendingSchedulerTicks;
  pendingSchedulerTicks = 0;
  portEXIT_CRITICAL(&schedulerTimerMux);

  if (localTicks > 0) {
    runSchedulerTick(localTicks * SCHEDULER_TICK_MS);
  }
}

// ========================== Scheduler Functions ===========================
/*
 * Function: schedulerTimerISR
 * Use: Hardware timer ISR that records one scheduler tick. It intentionally
 *      only updates a volatile counter so no slow library code runs in the ISR.
 * Parameters: none.
 * Returns: none.
 */
void IRAM_ATTR schedulerTimerISR(void) {
  portENTER_CRITICAL_ISR(&schedulerTimerMux);
  pendingSchedulerTicks++;
  portEXIT_CRITICAL_ISR(&schedulerTimerMux);
}

/*
 * Function: setupSchedulerTimer
 * Use: Configures an ESP32 hardware timer to generate a 1 ms scheduling tick.
 * Parameters: none.
 * Returns: none.
 */
void setupSchedulerTimer(void) {
  schedulerTimer = timerBegin(SCHEDULER_TIMER_HZ);
  timerAttachInterrupt(schedulerTimer, &schedulerTimerISR);
  timerAlarm(schedulerTimer, SCHEDULER_TICK_US, true, 0);
}

/*
 * Function: initializeTCBs
 * Use: Initializes the task table with function pointers, names, process IDs,
 *      default states, and default round-robin bookkeeping.
 * Parameters: none.
 * Returns: none.
 */
void initializeTCBs(void) {
  tasks[TASK_A_LED] = {taskLedBlinker, "LED Blinker", TASK_A_LED, 2, TASK_READY, 0};
  tasks[TASK_B_COUNTER] = {taskLcdCounter, "LCD Counter", TASK_B_COUNTER, 3, TASK_READY, 0};
  tasks[TASK_C_MUSIC] = {taskMusicPlayer, "Music Player", TASK_C_MUSIC, 4, TASK_READY, 0};
  tasks[TASK_D_ALPHABET] = {taskAlphabetPrinter, "Alphabet Printer", TASK_D_ALPHABET, 5, TASK_READY, 0};
  tasks[TASK_E_UPDATER] = {taskPriorityUpdater, "Priority Updater", TASK_E_UPDATER, 1, TASK_READY, 0};
  tasks[TASK_F_MONITOR] = {taskSchedulerMonitor, "Scheduler Monitor", TASK_F_MONITOR, 7, TASK_READY, 0};
  
  for (uint8_t priority = 0; priority <= PRIORITY_LEVEL_COUNT; priority++) {
    lastServedAtPriority[priority] = -1;
  }
}

/*
 * Function: applyPriorityScheme
 * Use: Copies one priority scheme from the lab table into the TCBs. Priority 1
 *      is treated as the highest priority.
 * Parameters: schemeIndex - zero-based index for schemes 1, 2, or 3.
 * Returns: none.
 */
void applyPriorityScheme(uint8_t schemeIndex) {
  if (schemeIndex > 2) {
    schemeIndex = 0;
  }

  for (uint8_t task = 0; task < TASK_COUNT; task++) {
    tasks[task].priority = prioritySchemes[schemeIndex][task];
  }

  for (uint8_t priority = 0; priority <= PRIORITY_LEVEL_COUNT; priority++) {
    lastServedAtPriority[priority] = -1;
  }
}

/*
 * Function: resetRunnableTasks
 * Use: Restarts Tasks A-D when the priority updater moves to the next scheme.
 *      Task E keeps running so the priority cycle continues forever.
 * Parameters: none.
 * Returns: none.
 */
void resetRunnableTasks(void) {
  resetTaskLocalState();

  for (uint8_t task = TASK_A_LED; task <= TASK_D_ALPHABET; task++) {
    tasks[task].state = TASK_READY;
    tasks[task].sleepRemainingMs = 0;
  }
}

/*
 * Function: resetTaskLocalState
 * Use: Restores the local progress counters used by Tasks A-D so each priority
 *      scheme starts with the same observable behavior.
 * Parameters: none.
 * Returns: none.
 */
void resetTaskLocalState(void) {
  ledState = false;
  digitalWrite(LED_PIN, LOW);

  lcdCounterValue = 1;
  lcdCounterPasses = 0;

  melodyIndex = 0;
  melodyPasses = 0;
  stopBuzzer();

  alphabetIndex = 0;
  alphabetPasses = 0;

  lcd.clear();
}

/*
 * Function: displayFixedLine
 * Use: Writes a message to one LCD line and pads the remaining columns with
 *      spaces so old characters do not remain visible.
 * Parameters: row - LCD row number, 0 or 1.
 *             text - text to display on that row.
 * Returns: none.
 */
void displayFixedLine(uint8_t row, const String &text) {
  String padded = text.substring(0, LCD_COLUMNS);
  while (padded.length() < LCD_COLUMNS) {
    padded += ' ';
  }

  lcd.setCursor(0, row);
  lcd.print(padded);
}

/*
 * Function: displayPriorityScheme
 * Use: Shows the active priority scheme on the LCD for setup and scheme changes.
 * Parameters: schemeIndex - zero-based priority scheme number.
 * Returns: none.
 */
void displayPriorityScheme(uint8_t schemeIndex) {
  lcd.clear();
  displayFixedLine(0, "Priority Scheme");
  displayFixedLine(1, String(schemeIndex + 1));
}

/*
 * Function: updateSleepingTasks
 * Use: Decrements the remaining sleep time for all sleeping tasks and returns
 *      tasks to Ready when their software delay expires.
 * Parameters: elapsedMs - number of elapsed milliseconds since the last update.
 * Returns: none.
 */
void updateSleepingTasks(unsigned long elapsedMs) {
  for (uint8_t task = 0; task < TASK_COUNT; task++) {
    if (tasks[task].state == TASK_SLEEPING) {
      if (tasks[task].sleepRemainingMs <= elapsedMs) {
        tasks[task].sleepRemainingMs = 0;
        tasks[task].state = TASK_READY;
      } else {
        tasks[task].sleepRemainingMs -= elapsedMs;
      }
    }
  }
}

/*
 * Function: chooseNextReadyTask
 * Use: Selects the highest-priority ready task. Equal-priority tasks are served
 *      by round robin using the lastServedAtPriority table.
 * Parameters: none.
 * Returns: task index of the next task to run, or -1 if none is ready.
 */
int8_t chooseNextReadyTask(void) {
  for (uint8_t priority = MIN_PRIORITY; priority <= MAX_PRIORITY; priority++) {
    int8_t startIndex = lastServedAtPriority[priority];

    for (uint8_t offset = 1; offset <= TASK_COUNT; offset++) {
      uint8_t candidate = (startIndex + offset + TASK_COUNT) % TASK_COUNT;

      if (tasks[candidate].state == TASK_READY && tasks[candidate].priority == priority) {
        lastServedAtPriority[priority] = candidate;
        return candidate;
      }
    }
  }

  return -1;
}

/*
 * Function: runSchedulerTick
 * Use: Performs one scheduler pass after the timer ISR reports one or more
 *      elapsed ticks. The scheduler is non-preemptive because each selected
 *      task runs until it voluntarily calls sleep_me(), halt_me(), or returns.
 * Parameters: elapsedMs - elapsed time represented by pending timer ticks.
 * Returns: none.
 */
void runSchedulerTick(unsigned long elapsedMs) {
  updateSleepingTasks(elapsedMs);

  int8_t selectedTask = chooseNextReadyTask();
  if (selectedTask < 0) {
    return;
  }

  currentTaskIndex = selectedTask;
  tasks[selectedTask].state = TASK_RUNNING;
  tasks[selectedTask].function();

  if (tasks[selectedTask].state == TASK_RUNNING) {
    tasks[selectedTask].state = TASK_READY;
  }

  currentTaskIndex = -1;
}

/*
 * Function: sleep_me
 * Use: Cooperative sleep function called by a task to release the CPU until the
 *      scheduler has counted down the requested duration.
 * Parameters: durationMs - number of milliseconds to sleep.
 * Returns: none.
 */
void sleep_me(unsigned long durationMs) {
  if (currentTaskIndex >= 0) {
    tasks[currentTaskIndex].sleepRemainingMs = durationMs;
    tasks[currentTaskIndex].state = (durationMs == 0) ? TASK_READY : TASK_SLEEPING;
  }
}

/*
 * Function: halt_me
 * Use: Marks the currently running task as halted and reports completion to the
 *      serial monitor using the task name and priority from its TCB.
 * Parameters: none.
 * Returns: none.
 */
void halt_me(void) {
  if (currentTaskIndex >= 0) {
    tasks[currentTaskIndex].state = TASK_HALTED;
    tasks[currentTaskIndex].sleepRemainingMs = 0;

    Serial.print("Completed: ");
    Serial.print(tasks[currentTaskIndex].name);
    Serial.print(": ");
    Serial.println(tasks[currentTaskIndex].priority);
  }
}

/*
 * Function: stateToString
 * Use: Converts a TaskState enum value to a readable string for debugging.
 * Parameters: state - task state enum value.
 * Returns: string literal naming the state.
 */
const char *stateToString(TaskState state) {
  switch (state) {
    case TASK_RUNNING: return "Running";
    case TASK_READY: return "Ready";
    case TASK_HALTED: return "Halted";
    case TASK_SLEEPING: return "Sleeping";
    default: return "Unknown";
  }
}

// ============================== Task Functions ============================
/*
 * Function: taskLedBlinker
 * Use: Task A. Toggles an external LED every 63 ms, giving an approximately
 *      8 Hz square wave. This task runs forever and never calls halt_me().
 * Parameters: none.
 * Returns: none.
 */
void taskLedBlinker(void) {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  sleep_me(LED_TOGGLE_MS);
}

/*
 * Function: taskLcdCounter
 * Use: Task B. Displays counts 1-10 on LCD line 0 at one count per two seconds.
 *      After two passes through the count sequence, the task halts.
 * Parameters: none.
 * Returns: none.
 */
void taskLcdCounter(void) {
  displayFixedLine(0, "Count: " + String(lcdCounterValue));

  lcdCounterValue++;
  if (lcdCounterValue > 10) {
    lcdCounterValue = 1;
    lcdCounterPasses++;
  }

  if (lcdCounterPasses >= 2) {
    halt_me();
    return;
  }

  sleep_me(LCD_COUNTER_PERIOD_MS);
}

/*
 * Function: taskMusicPlayer
 * Use: Task C. Plays a ten-note melody on the buzzer using LEDC and displays
 *      the current frequency on LCD line 1. After two melody passes, it halts.
 * Parameters: none.
 * Returns: none.
 */
void taskMusicPlayer(void) {
  if (melodyIndex >= melodyLength) {
    melodyIndex = 0;
    melodyPasses++;

    if (melodyPasses >= 2) {
      stopBuzzer();
      displayFixedLine(1, "Music done");
      halt_me();
      return;
    }
  }

  uint16_t frequency = melodyFrequencies[melodyIndex];
  startBuzzer(frequency);
  displayFixedLine(1, "Note: " + String(frequency) + "Hz");

  melodyIndex++;
  sleep_me(NOTE_DURATION_MS);
}

/*
 * Function: taskAlphabetPrinter
 * Use: Task D. Prints A-Z to the serial monitor at two letters per second.
 *      After the alphabet has printed twice, the task halts.
 * Parameters: none.
 * Returns: none.
 */
void taskAlphabetPrinter(void) {
  if (alphabetIndex >= 26) {
    alphabetIndex = 0;
    alphabetPasses++;
    Serial.println();

    if (alphabetPasses >= 2) {
      halt_me();
      return;
    }
  }

  char letter = static_cast<char>('A' + alphabetIndex);
  Serial.print(letter);
  alphabetIndex++;
  sleep_me(ALPHABET_PERIOD_MS);
}

/*
 * Function: taskPriorityUpdater
 * Use: Task E. Sleeps for 30 seconds, waits until Tasks B-D are halted, then
 *      advances to the next priority scheme and restarts Tasks A-D. Waiting for
 *      B-D after the first 30 seconds resolves the timing conflict caused by
 *      the LCD counter needing about 40 seconds to complete two full passes.
 * Parameters: none.
 * Returns: none.
 */
void taskPriorityUpdater(void) {
  bool tasksBDone = (tasks[TASK_B_COUNTER].state == TASK_HALTED);
  bool tasksCDone = (tasks[TASK_C_MUSIC].state == TASK_HALTED);
  bool tasksDDone = (tasks[TASK_D_ALPHABET].state == TASK_HALTED);

  if (!updaterWaitingForCompletion) {
    updaterWaitingForCompletion = true;
    sleep_me(UPDATER_FIRST_SLEEP_MS);
    return;
  }

  if (!(tasksBDone && tasksCDone && tasksDDone)) {
    sleep_me(UPDATER_RECHECK_MS);
    return;
  }

  currentPriorityScheme = (currentPriorityScheme + 1) % 3;
  applyPriorityScheme(currentPriorityScheme);
  resetRunnableTasks();

  Serial.println();
  Serial.println();
  Serial.print("Starting Priority Scheme ");
  Serial.println(currentPriorityScheme + 1);
  Serial.println();

  displayPriorityScheme(currentPriorityScheme);
  updaterWaitingForCompletion = true;
  sleep_me(UPDATER_FIRST_SLEEP_MS);
}


// ============================= Schedule Monitor ===========================
/*
 * Function: taskSchedulerMonitor
 * Use: Extra credit lightweight monitoring task. Toggles a GPIO pin every
 *      scheduler tick so an oscilloscope can verify scheduler timing.
 * Parameters: none.
 * Returns: none.
 */
void taskSchedulerMonitor(void) {
  static bool state = false;

  state = !state;
  digitalWrite(SCHEDULER_MONITOR_PIN, state);

  sleep_me(1);
}

// ============================= Buzzer Functions ===========================
/*
 * Function: setupBuzzer
 * Use: Initializes the ESP32 LEDC peripheral for the buzzer. Conditional
 *      compilation keeps the code compatible with ESP32 Arduino core 2.x and
 *      3.x, whose LEDC APIs differ.
 * Parameters: none.
 * Returns: none.
 */
void setupBuzzer(void) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, BUZZER_IDLE_FREQUENCY, BUZZER_RESOLUTION_BITS);
#else
  ledcSetup(BUZZER_CHANNEL, BUZZER_IDLE_FREQUENCY, BUZZER_RESOLUTION_BITS);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
#endif
  stopBuzzer();
}

/*
 * Function: startBuzzer
 * Use: Starts or retunes the buzzer output to the requested note frequency.
 * Parameters: frequencyHz - frequency to generate in hertz.
 * Returns: none.
 */
void startBuzzer(uint16_t frequencyHz) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, frequencyHz);
#else
  ledcWriteTone(BUZZER_CHANNEL, frequencyHz);
#endif
}

/*
 * Function: stopBuzzer
 * Use: Stops the LEDC waveform so the buzzer is quiet between runs.
 * Parameters: none.
 * Returns: none.
 */
void stopBuzzer(void) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, 0);
#else
  ledcWriteTone(BUZZER_CHANNEL, 0);
#endif
}
