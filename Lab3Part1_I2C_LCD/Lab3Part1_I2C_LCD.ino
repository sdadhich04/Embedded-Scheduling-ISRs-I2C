/*
 * File: Lab3Part1_I2C_LCD.ino
 * Author(s): Jonathan Lu, Sparsh Dadhich
 * Date: 06-May-2026
 * ChatGPT : 672
 * Version: 1.0
 * Description: ECE 474 Lab 3 Part I. This sketch receives a line of text from
 *              the Arduino Serial Monitor and writes it to a 16x2 LCD through
 *              the PCF8574 I2C I/O expander using the Wire library. The
 *              LiquidCrystal_I2C library is used only for initialization in
 *              setup(), as allowed by the lab handout.
 *
 * External source acknowledgement: The LCD initialization skeleton and PCF8574
 * bit mapping are based on the ECE 474 Lab 3 handout. The direct I2C helper
 * functions below were written for this submission from the command/data bit
 * descriptions in that handout.
 */

// =============================== Includes ===============================
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================================ Macros =================================
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#define LCD_RS 0x01        // Register Select: 0 = command, 1 = data
#define LCD_RW 0x02        // Read/Write: this lab only writes, so this stays 0
#define LCD_EN 0x04        // Enable strobe bit
#define LCD_BACKLIGHT 0x08 // Keep the display backlight enabled

#define LCD_LINE0 0x00
#define LCD_LINE1 0x40

// ========================== Global Objects/State ==========================
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// ========================== Function Prototypes ===========================
void lcdWriteExpander(uint8_t value);
void lcdPulseEnable(uint8_t value);
void lcdWriteNibble(uint8_t nibble, uint8_t controlBits);
void lcdWriteByte(uint8_t value, uint8_t controlBits);
void lcdCommand(uint8_t command);
void lcdData(uint8_t dataByte);
void lcdClearDirect(void);
void lcdSetCursorDirect(uint8_t column, uint8_t row);
void lcdPrintStringDirect(const String &message);

// ============================== Arduino Setup =============================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // The lab permits this library initialization step so the HD44780 LCD starts
  // in 4-bit mode before the loop uses raw Wire transmissions.
  lcd.init();
  lcd.backlight();
  delay(2);

  lcdClearDirect();
  lcdSetCursorDirect(0, 0);
  lcdPrintStringDirect("Type in Serial");
  lcdSetCursorDirect(0, 1);
  lcdPrintStringDirect("then press Enter");

  Serial.println("Lab 3 Part I ready. Type a message and press Enter.");
}

// =============================== Arduino Loop =============================
void loop() {
  if (Serial.available() > 0) {
    String message = Serial.readStringUntil('\n');
    message.trim(); // Removes the carriage return that can appear before \n.

    lcdClearDirect();
    lcdSetCursorDirect(0, 0);
    lcdPrintStringDirect(message);

    Serial.print("Displayed on LCD: ");
    Serial.println(message);
  }
}

// ========================== Function Implementations ======================
/*
 * Function: lcdWriteExpander
 * Use: Sends one complete byte to the PCF8574 LCD backpack through I2C.
 * Parameters: value - the eight output bits to place on the expander pins.
 * Returns: none.
 */
void lcdWriteExpander(uint8_t value) {
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  Wire.write(value | LCD_BACKLIGHT);
  Wire.endTransmission();
}

/*
 * Function: lcdPulseEnable
 * Use: Toggles the enable bit high then low so the LCD latches the nibble on
 *      bits 4-7. The short delays give the LCD controller time to detect the
 *      strobe and process the input.
 * Parameters: value - expander byte containing the nibble and control bits.
 * Returns: none.
 */
void lcdPulseEnable(uint8_t value) {
  lcdWriteExpander(value | LCD_EN);
  delayMicroseconds(1);
  lcdWriteExpander(value & ~LCD_EN);
  delayMicroseconds(50);
}

/*
 * Function: lcdWriteNibble
 * Use: Places a four-bit command/data nibble into bits 4-7 of the PCF8574 byte
 *      while preserving the low control bits, then strobes it into the LCD.
 * Parameters: nibble - lower four bits hold the LCD nibble to send.
 *             controlBits - LCD_RS for data or 0 for command.
 * Returns: none.
 */
void lcdWriteNibble(uint8_t nibble, uint8_t controlBits) {
  uint8_t outputByte = ((nibble & 0x0F) << 4) | controlBits | LCD_BACKLIGHT;
  lcdPulseEnable(outputByte);
}

/*
 * Function: lcdWriteByte
 * Use: Sends an eight-bit command or data byte in the LCD's required order:
 *      high nibble first, then low nibble.
 * Parameters: value - command or data byte.
 *             controlBits - LCD_RS for data or 0 for command.
 * Returns: none.
 */
void lcdWriteByte(uint8_t value, uint8_t controlBits) {
  lcdWriteNibble(value >> 4, controlBits);
  lcdWriteNibble(value & 0x0F, controlBits);
}

/*
 * Function: lcdCommand
 * Use: Sends a command byte to the LCD controller.
 * Parameters: command - HD44780 command byte.
 * Returns: none.
 */
void lcdCommand(uint8_t command) {
  lcdWriteByte(command, 0);
}

/*
 * Function: lcdData
 * Use: Sends one displayable character byte to the LCD controller.
 * Parameters: dataByte - ASCII character value to display.
 * Returns: none.
 */
void lcdData(uint8_t dataByte) {
  lcdWriteByte(dataByte, LCD_RS);
}

/*
 * Function: lcdClearDirect
 * Use: Clears the LCD using the HD44780 clear-display command. This command
 *      needs more processing time than a normal character write.
 * Parameters: none.
 * Returns: none.
 */
void lcdClearDirect(void) {
  lcdCommand(0x01);
  delay(2);
}

/*
 * Function: lcdSetCursorDirect
 * Use: Moves the LCD cursor by sending the DDRAM-address command. Row 0 starts
 *      at 0x00 and row 1 starts at 0x40 on a 16x2 LCD.
 * Parameters: column - zero-based column number.
 *             row - zero-based row number.
 * Returns: none.
 */
void lcdSetCursorDirect(uint8_t column, uint8_t row) {
  uint8_t rowOffset = (row == 0) ? LCD_LINE0 : LCD_LINE1;
  lcdCommand(0x80 | (rowOffset + column));
}

/*
 * Function: lcdPrintStringDirect
 * Use: Prints a string onto the LCD without calling LiquidCrystal_I2C display
 *      functions. Text longer than 16 characters wraps once to the second row;
 *      extra characters are ignored because the display has only 32 cells.
 * Parameters: message - Arduino String to display.
 * Returns: none.
 */
void lcdPrintStringDirect(const String &message) {
  for (uint8_t i = 0; i < message.length() && i < LCD_COLUMNS * LCD_ROWS; i++) {
    if (i == LCD_COLUMNS) {
      lcdSetCursorDirect(0, 1);
    }
    lcdData(static_cast<uint8_t>(message[i]));
  }
}
