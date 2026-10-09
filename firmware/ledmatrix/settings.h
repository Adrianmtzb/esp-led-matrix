// Persistent device and display settings (Preferences namespace "lm").
#pragma once
#include "matrix.h"

enum ColorMode : uint8_t { CM_RGB = 0, CM_MONO = 1, CM_GRAY = 2 };
enum LedShape : uint8_t { SHAPE_ROUND = 0, SHAPE_SQUARE = 1, SHAPE_ROUNDED = 2 };

struct Settings {
  bool fit = true;           // derive width/height from the screen and `pitch`
  uint8_t pitch = 10;        // target LED pitch in screen pixels when fit is on, 4-40
  uint8_t width = MX_DEFAULT_W;
  uint8_t height = MX_DEFAULT_H;
  ColorMode colorMode = CM_RGB;
  RGB monoColor = {255, 40, 0};
  LedShape shape = SHAPE_ROUND;
  uint8_t gap = 25;          // percent of the LED pitch left dark between LEDs, 0-60
  uint8_t brightness = 255;  // matrix intensity, 1-255
  bool showOff = true;       // draw unlit LEDs as dim dots
  uint8_t backlight = 200;   // LCD backlight, 5-255
  uint8_t rotation = LCD_DEFAULT_ROTATION;
  String hostname = DEFAULT_HOSTNAME;
  String tz = DEFAULT_TZ;
};

extern Settings settings;

void settingsLoad();
void settingsSave();

const char *colorModeName(ColorMode m);
bool colorModeFromName(const char *s, ColorMode &out);
const char *shapeName(LedShape s);
bool shapeFromName(const char *s, LedShape &out);
bool validHostname(const String &h);
// Matrix size that fills a screen of sw x sh with LEDs of roughly `pitch` pixels.
void fitDims(int sw, int sh, int pitch, uint8_t &w, uint8_t &h);
