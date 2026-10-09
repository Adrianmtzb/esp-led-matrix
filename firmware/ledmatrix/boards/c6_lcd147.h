// Waveshare ESP32-C6-LCD-1.47: ST7789 172x320, no PSRAM, WS2812 status LED, no IMU.
#pragma once

#define BOARD_NAME "Waveshare ESP32-C6-LCD-1.47"
#define BOARD_ID "c6"
#define DEFAULT_HOSTNAME "ledmatrix-c6"

#define LCD_MOSI 6
#define LCD_SCLK 7
#define LCD_CS 14
#define LCD_DC 15
#define LCD_RST 21
#define LCD_BL 22
#define LCD_NATIVE_W 172
#define LCD_NATIVE_H 320
#define LCD_COL_OFFSET 34
#define LCD_ROW_OFFSET 0
#define LCD_CORNER_RADIUS 0
#define LCD_DEFAULT_ROTATION 1

#define BUTTON_PIN 9

#define HAS_RGB_LED 1
#define RGB_LED_PIN 8

#define HAS_IMU 0
#define HAS_BATTERY 0

#define ANIM_MAX_BYTES (48 * 1024)
#define ANIM_USE_PSRAM 0
