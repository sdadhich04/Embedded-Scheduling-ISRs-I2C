# Embedded Scheduling, ISRs, and I2C

Three Arduino sketches for ECE 474 Lab 3. They implement separate exercises in direct I2C LCD communication, cooperative priority scheduling, and interrupt/BLE event handling on an ESP32.

## What it does

### Part I: I2C LCD

`Lab3Part1_I2C_LCD/Lab3Part1_I2C_LCD.ino` reads a line from the serial monitor and displays up to 32 characters on a 16x2 LCD at I2C address `0x27`. It uses `LiquidCrystal_I2C` to initialize the display, then sends LCD commands and data with `Wire` transactions through the PCF8574 backpack.

### Part II: Cooperative scheduler

`Lab3Part2_Scheduler/Lab3Part2_Scheduler.ino` implements a Task Control Block table and a non-preemptive priority scheduler. A 1 ms hardware-timer ISR increments a pending-tick counter; `loop()` updates sleeping tasks and selects one ready task. Lower numeric priorities are selected first, and equal-priority tasks are selected round-robin.

The task set includes an LED blinker, LCD counter, buzzer melody player, serial alphabet printer, priority-scheme updater, and GPIO scheduler monitor. The sketch defines three priority schemes and uses ESP32 LEDC APIs for the buzzer.

### Part III: Timer, button, and BLE events

`Lab3Part3_BLE_ISR/Lab3Part3_BLE_ISR.ino` combines a one-second timer ISR, a falling-edge button ISR on GPIO 4, and a BLE characteristic write callback. The timer increments a seconds counter; the button ISR and BLE callback set event flags. `loop()` consumes those flags, debounces the button for 200 ms, and updates the LCD. Button and BLE messages are displayed for two seconds before the counter display resumes.

The BLE peripheral is named `MyESP32-Lab3` and exposes one readable/writable characteristic.

## Hardware and tools

- ESP32-compatible board
- 16x2 I2C LCD with a PCF8574 backpack (`0x27` in the sketches)
- External LED and buzzer for Part II
- Push button wired to GPIO 4 and ground for Part III
- Arduino IDE or Arduino CLI with an ESP32 board package
- `Wire`, `LiquidCrystal_I2C`, and the ESP32 BLE libraries

Part II assigns GPIO 5 to the LED, GPIO 6 to the buzzer, and GPIO 7 to the scheduler monitor.

## Run

1. Install an ESP32 board package and the `LiquidCrystal_I2C` library in the Arduino environment.
2. Open one sketch directory at a time, select a compatible ESP32 board and serial port, then upload it.
3. For Part I, open a serial monitor at 115200 baud and send a line of text. Part III starts BLE advertising after setup.

## Scope

This checkout contains these three standalone sketches; it does not contain `RTOS-Scheduling-Anomaly-Detection`. This README therefore does not claim an implementation comparison with that separate repository.

## Credits

Each sketch names Jonathan Lu and Sparsh Dadhich as authors. The source comments acknowledge the course handout for the I2C LCD setup/mapping and lab requirements, and the Part III source acknowledges a course BLE setup skeleton. The source headers also record ChatGPT assistance.

## License

[MIT](LICENSE)
