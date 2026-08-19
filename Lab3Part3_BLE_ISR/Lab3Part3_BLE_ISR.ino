/*
 * File: Lab3Part3_BLE_ISR.ino
 * Author(s): Jonathan Lu, Sparsh Dadhich
 * Date: 06-May-2026
 * ChatGPT : 752
 * Version: 1.0
 * Description: ECE 474 Lab 3 Part III. This sketch combines a timer interrupt,
 *              a button interrupt, and a BLE write callback. The interrupt or
 *              callback routines only set flags; the main loop owns all LCD
 *              updates so slow library functions do not run inside an ISR.
 *
 * External source acknowledgement: The BLE server structure follows the ECE 474
 * BLE setup skeleton. This code was drafted with ChatGPT assistance and should
 * be reviewed and understood by the submitting team before turn-in, as required
 * by the course code guidelines.
 */

// =============================== Includes ===============================
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// ================================ Macros =================================
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#define BUTTON_PIN 4
#define BUTTON_DEBOUNCE_MS 200UL
#define MESSAGE_HOLD_MS 2000UL

#define TIMER_HZ 1000000UL
#define ONE_SECOND_ALARM_TICKS 1000000UL

#define BLE_DEVICE_NAME "MyESP32-Lab3"
#define SERVICE_UUID "2d1cf4d8-3a4a-4d3e-b17b-421a1d9a4743"
#define CHARACTERISTIC_UUID "b9638f04-9d0a-4f86-9c88-2a147d9c4743"

// ================================ Types ==================================
enum DisplayMode {
  DISPLAY_COUNTER,
  DISPLAY_BUTTON_MESSAGE,
  DISPLAY_BLE_MESSAGE
};

// ========================== Global Objects/State ==========================
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

hw_timer_t *secondTimer = NULL;
portMUX_TYPE interruptMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool oneSecondFlag = false;
volatile bool buttonPressedFlag = false;
volatile bool bleMessageFlag = false;
volatile unsigned long secondsCounter = 0;

DisplayMode currentDisplayMode = DISPLAY_COUNTER;
unsigned long messageExpiresAtMs = 0;
unsigned long lastAcceptedButtonMs = 0;
unsigned long lastDisplayedCounter = 0;

// ========================== Function Prototypes ===========================
void IRAM_ATTR oneSecondTimerISR(void);
void IRAM_ATTR buttonISR(void);
void setupTimerInterrupt(void);
void setupButtonInterrupt(void);
void setupBleServer(void);
void setupLcd(void);
void displayFixedLine(uint8_t row, const String &text);
void displayCounter(unsigned long countValue);
void showTemporaryMessage(DisplayMode mode, const String &line0, const String &line1);
void handlePendingEvents(void);
void handleCounterDisplay(void);

// ============================= BLE Callback ===============================
class MyCallbacks : public BLECharacteristicCallbacks {
 public:
  /*
   * Function: onWrite
   * Use: BLE callback invoked when the central device writes to the lab
   *      characteristic. It only sets a flag; the LCD is updated in loop().
   * Parameters: pCharacteristic - characteristic that received the write.
   * Returns: none.
   */
  void onWrite(BLECharacteristic *pCharacteristic) override {
    (void)pCharacteristic;
    bleMessageFlag = true;
  }
};

// ============================== Arduino Setup =============================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  setupLcd();
  setupBleServer();
  setupButtonInterrupt();
  setupTimerInterrupt();

  displayFixedLine(0, "BLE ready");
  displayFixedLine(1, BLE_DEVICE_NAME);
  Serial.println("Lab 3 Part III ready. Connect with LightBlue and write any value.");
}

// =============================== Arduino Loop =============================
void loop() {
  handlePendingEvents();
  handleCounterDisplay();
}

// ============================== ISR Functions =============================
/*
 * Function: oneSecondTimerISR
 * Use: Timer ISR that counts seconds and signals the main loop. No LCD or serial
 *      library calls are used here so the ISR stays short.
 * Parameters: none.
 * Returns: none.
 */
void IRAM_ATTR oneSecondTimerISR(void) {
  portENTER_CRITICAL_ISR(&interruptMux);
  secondsCounter++;
  oneSecondFlag = true;
  portEXIT_CRITICAL_ISR(&interruptMux);
}

/*
 * Function: buttonISR
 * Use: Button ISR that flags a press event. Debouncing and display updates are
 *      handled in loop(), not inside the interrupt context.
 * Parameters: none.
 * Returns: none.
 */
void IRAM_ATTR buttonISR(void) {
  buttonPressedFlag = true;
}

// ========================== Setup Helper Functions ========================
/*
 * Function: setupTimerInterrupt
 * Use: Configures an ESP32 hardware timer to interrupt once per second.
 * Parameters: none.
 * Returns: none.
 */
void setupTimerInterrupt(void) {
  secondTimer = timerBegin(TIMER_HZ);
  timerAttachInterrupt(secondTimer, &oneSecondTimerISR);
  timerAlarm(secondTimer, ONE_SECOND_ALARM_TICKS, true, 0);
}

/*
 * Function: setupButtonInterrupt
 * Use: Enables the internal pull-up resistor and attaches a falling-edge
 *      interrupt, so pressing a button wired to GND triggers the ISR.
 * Parameters: none.
 * Returns: none.
 */
void setupButtonInterrupt(void) {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
}

/*
 * Function: setupBleServer
 * Use: Creates a BLE peripheral with one writable/readable characteristic.
 *      LightBlue can connect to this service and write any value to trigger the
 *      "New Message!" display behavior.
 * Parameters: none.
 * Returns: none.
 */
void setupBleServer(void) {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );

  pCharacteristic->setValue("0");
  pCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}

/*
 * Function: setupLcd
 * Use: Initializes the I2C LCD display and clears any startup characters.
 * Parameters: none.
 * Returns: none.
 */
void setupLcd(void) {
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

// ========================== Display/Event Functions =======================
/*
 * Function: displayFixedLine
 * Use: Writes a message to one LCD row and pads with spaces to erase leftovers
 *      from longer previous messages.
 * Parameters: row - LCD row number, 0 or 1.
 *             text - message to display.
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
 * Function: displayCounter
 * Use: Displays the current one-second counter value on the LCD.
 * Parameters: countValue - current value of the timer-driven seconds counter.
 * Returns: none.
 */
void displayCounter(unsigned long countValue) {
  displayFixedLine(0, "Seconds Count");
  displayFixedLine(1, String(countValue));
}

/*
 * Function: showTemporaryMessage
 * Use: Shows a two-line event message and records when the display should return
 *      to the one-second timer count.
 * Parameters: mode - event type currently blocking the counter display.
 *             line0 - first LCD line.
 *             line1 - second LCD line.
 * Returns: none.
 */
void showTemporaryMessage(DisplayMode mode, const String &line0, const String &line1) {
  currentDisplayMode = mode;
  messageExpiresAtMs = millis() + MESSAGE_HOLD_MS;
  lcd.clear();
  displayFixedLine(0, line0);
  displayFixedLine(1, line1);
}

/*
 * Function: handlePendingEvents
 * Use: Pulls ISR and BLE flags into local variables, then updates the LCD from
 *      normal loop context. Button events take priority over BLE events if both
 *      are pending in the same pass.
 * Parameters: none.
 * Returns: none.
 */
void handlePendingEvents(void) {
  bool localButtonFlag = false;
  bool localBleFlag = false;

  portENTER_CRITICAL(&interruptMux);
  localButtonFlag = buttonPressedFlag;
  localBleFlag = bleMessageFlag;
  buttonPressedFlag = false;
  bleMessageFlag = false;
  portEXIT_CRITICAL(&interruptMux);

  unsigned long nowMs = millis();

  if (localButtonFlag && (nowMs - lastAcceptedButtonMs >= BUTTON_DEBOUNCE_MS)) {
    lastAcceptedButtonMs = nowMs;
    showTemporaryMessage(DISPLAY_BUTTON_MESSAGE, "Button Pressed", "Counter paused");
    Serial.println("Button interrupt received.");
    return;
  }

  if (localBleFlag) {
    showTemporaryMessage(DISPLAY_BLE_MESSAGE, "New Message!", "Counter paused");
    Serial.println("BLE write received.");
  }
}

/*
 * Function: handleCounterDisplay
 * Use: Updates the LCD with the timer count when no temporary button or BLE
 *      message is active. Counter updates that happen during a temporary message
 *      are preserved and shown after the two-second message expires.
 * Parameters: none.
 * Returns: none.
 */
void handleCounterDisplay(void) {
  unsigned long localSeconds = 0;
  bool localOneSecondFlag = false;

  portENTER_CRITICAL(&interruptMux);
  localSeconds = secondsCounter;
  localOneSecondFlag = oneSecondFlag;
  oneSecondFlag = false;
  portEXIT_CRITICAL(&interruptMux);

  if (currentDisplayMode != DISPLAY_COUNTER) {
    if (millis() >= messageExpiresAtMs) {
      currentDisplayMode = DISPLAY_COUNTER;
      lcd.clear();
      displayCounter(localSeconds);
      lastDisplayedCounter = localSeconds;
    }
    return;
  }

  if (localOneSecondFlag || localSeconds != lastDisplayedCounter) {
    displayCounter(localSeconds);
    lastDisplayedCounter = localSeconds;
  }
}
