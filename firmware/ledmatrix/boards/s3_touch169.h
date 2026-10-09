// Waveshare ESP32-S3-Touch-LCD-1.69: ST7789V2 240x280 rounded glass, 8 MB PSRAM,
// QMI8658C IMU and CST816T touch on I2C, battery with power latch.
#pragma once

#define BOARD_NAME "Waveshare ESP32-S3-Touch-LCD-1.69"
#define BOARD_ID "s3"
#define DEFAULT_HOSTNAME "ledmatrix-s3"

#define LCD_MOSI 7
#define LCD_SCLK 6
#define LCD_CS 5
#define LCD_DC 4
#define LCD_RST 8
#define LCD_BL 15
#define LCD_NATIVE_W 240
#define LCD_NATIVE_H 280
#define LCD_COL_OFFSET 0
#define LCD_ROW_OFFSET 20
#define LCD_CORNER_RADIUS 48
#define LCD_DEFAULT_ROTATION 1

#define BUTTON_PIN 0

#define HAS_RGB_LED 0

#define HAS_IMU 1
#define I2C_SDA 11
#define I2C_SCL 10
#define IMU_ADDR 0x6B

#define HAS_BATTERY 1
#define BAT_ADC_PIN 1
#define SYS_EN_PIN 41  // keep HIGH to stay powered from battery (35 on the first board revision)

#define ANIM_MAX_BYTES (2 * 1024 * 1024)
#define ANIM_USE_PSRAM 1
