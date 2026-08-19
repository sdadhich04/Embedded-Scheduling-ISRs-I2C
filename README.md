# Embedded-Scheduling-ISRs-I2C

**Origin:** Lab 3 from ECE/CSE 474 (Introduction to Embedded Systems), University of Washington, Spring 2026 — "Serial Communication, Priority Scheduling, and ISR Integration."

Three ESP32-S3 sketches built by **Sparsh Dadhich** and **Jonathan Lu**, each covering a different embedded systems concept.

## Part I — Raw I2C LCD driving

`Lab3Part1_I2C_LCD/Lab3Part1_I2C_LCD.ino`

Reads a line of text from the Serial Monitor and writes it to a 16×2 LCD through a PCF8574 I2C backpack. The `LiquidCrystal_I2C` library is used only once, for initial 4-bit-mode setup — every character and command after that is sent with raw `Wire.beginTransmission()/write()/endTransmission()` calls, splitting each byte into nibbles and mapping them onto the PCF8574's output bits (RS/RW/EN/backlight) by hand, per the lab handout's bit-level requirements.

Demoed result: typing "Happy Birthday Joseph" wraps correctly onto the second LCD line (32 visible characters, second-line wrap confirmed in the report).

## Part II — Non-preemptive priority scheduler

`Lab3Part2_Scheduler/Lab3Part2_Scheduler.ino`

A cooperative scheduler built from scratch around Task Control Blocks (TCB: function pointer, name, PID, priority, state, remaining sleep time), driven by a 1 ms hardware timer tick. The ISR itself only increments a volatile tick counter — the main loop does all the real work: updating sleeping tasks, selecting the highest-priority ready task (priority 1 = highest, round-robin among equal priorities), and running it. Six tasks: LED blinker, LCD counter, buzzer "music" player, alphabet printer, a priority updater, and a scheduler monitor.

Measured result: LED toggle period ~63 ms (target 8 Hz) measured at **7.94 Hz** on the oscilloscope — close enough to spec, and the report keeps the measured number rather than rounding it up to the target.

| Pin | Signal |
|---|---|
| GPIO 5 | LED |
| GPIO 6 | Buzzer |
| GPIO 7 | Scheduler monitor |

## Part III — Timer ISR + button ISR + BLE callback

`Lab3Part3_BLE_ISR/Lab3Part3_BLE_ISR.ino`

Combines three independent event sources — a 1-second hardware timer interrupt, a debounced button interrupt, and a BLE characteristic write callback — using the same discipline throughout: **ISRs and BLE callbacks only set a flag; the main loop owns every LCD update and all "slow" work**, so nothing blocking ever runs inside an interrupt context.

| | |
|---|---|
| Button pin | GPIO 4 (debounced, 200 ms) |
| BLE device name | `MyESP32-Lab3` |
| Service UUID | `2d1cf4d8-3a4a-4d3e-b17b-421a1d9a4743` |
| Characteristic UUID | `b9638f04-9d0a-4f86-9c88-2a147d9c4743` |

Tested with the LightBlue app writing to the characteristic to trigger a temporary LCD message.

## Hardware

ESP32-S3, 16×2 I2C LCD (PCF8574 backpack, address `0x27` — some backpacks need `0x3F`, check yours), external LED with current-limiting resistor, buzzer, push button. LCD wiring: SDA → A4, SCL → A5 (Nano ESP32 reference wiring per the course materials).

## Build

Arduino IDE with the ESP32 board package (ESP32-S3), plus `LiquidCrystal_I2C` for Parts I–III and `BLEDevice`/`BLEUtils`/`BLEServer` (bundled with the ESP32 core) for Part III. Each `.ino` is self-contained.

## AI-assistance disclosure

Per the ECE 474 course's AI-use policy, each file header carries a ChatGPT usage tag and the note: "This code was drafted with ChatGPT assistance and should be reviewed and understood by the submitting team before turn-in, as required by the course code guidelines." Part I's header also credits the course's Lab 3 handout for the PCF8574 bit-mapping skeleton, and Part III's header credits the course's BLE setup skeleton. Keeping both disclosures here rather than dropping them.

## Repository layout

```
Lab3Part1_I2C_LCD/Lab3Part1_I2C_LCD.ino     # Part I — raw I2C LCD driving
Lab3Part2_Scheduler/Lab3Part2_Scheduler.ino  # Part II — non-preemptive priority scheduler (TCBs)
Lab3Part3_BLE_ISR/Lab3Part3_BLE_ISR.ino      # Part III — timer ISR + button ISR + BLE callback
docs/ECE474_Lab3_Report.pdf                  # Final report — hardware photos, oscilloscope traces, results
```

## License

MIT — see [LICENSE](LICENSE).
