#include "settings.h"
#include <Preferences.h>

Settings settings;

void settingsLoad() {
  Preferences p;
  p.begin("lm", true);
  settings.fit = p.getBool("fit", true);
  settings.pitch = p.getUChar("pitch", 10);
  settings.width = p.getUChar("w", MX_DEFAULT_W);
  settings.height = p.getUChar("h", MX_DEFAULT_H);
  settings.colorMode = (ColorMode)p.getUChar("cmode", CM_RGB);
  uint32_t mono = p.getUInt("mcol", 0xFF2800);
  settings.monoColor = {(uint8_t)(mono >> 16), (uint8_t)(mono >> 8), (uint8_t)mono};
  settings.shape = (LedShape)p.getUChar("shape", SHAPE_ROUND);
  settings.gap = p.getUChar("gap", 25);
  settings.brightness = p.getUChar("bri", 255);
  settings.showOff = p.getBool("off", true);
  settings.backlight = p.getUChar("bl", 200);
  settings.rotation = p.getUChar("rot", LCD_DEFAULT_ROTATION);
  settings.hostname = p.getString("host", DEFAULT_HOSTNAME);
  String tz = p.getString("tz", "");
  p.end();

  if (tz.isEmpty()) {
    // First boot: inherit the time zone a previous firmware stored, if any.
    Preferences net;
    net.begin("net", true);
    tz = net.getString("tz", DEFAULT_TZ);
    net.end();
  }
  settings.tz = tz;

  if (settings.width < MX_MIN_DIM || settings.width > MX_MAX_DIM) settings.width = MX_DEFAULT_W;
  if (settings.height < MX_MIN_DIM || settings.height > MX_MAX_DIM) settings.height = MX_DEFAULT_H;
  if (settings.width * settings.height > MX_MAX_PIXELS) {
    settings.width = MX_DEFAULT_W;
    settings.height = MX_DEFAULT_H;
  }
  if (settings.colorMode > CM_GRAY) settings.colorMode = CM_RGB;
  if (settings.shape > SHAPE_ROUNDED) settings.shape = SHAPE_ROUND;
  if (settings.gap > 60) settings.gap = 25;
  if (settings.brightness == 0) settings.brightness = 255;
  if (settings.backlight < 5) settings.backlight = 5;
  settings.rotation &= 3;
  if (settings.pitch < 4 || settings.pitch > 40) settings.pitch = 10;
  if (settings.fit) {
    bool landscape = settings.rotation & 1;
    fitDims(landscape ? LCD_NATIVE_H : LCD_NATIVE_W, landscape ? LCD_NATIVE_W : LCD_NATIVE_H, settings.pitch,
            settings.width, settings.height);
  }
  if (!validHostname(settings.hostname)) settings.hostname = DEFAULT_HOSTNAME;
}

void settingsSave() {
  Preferences p;
  p.begin("lm", false);
  p.putBool("fit", settings.fit);
  p.putUChar("pitch", settings.pitch);
  p.putUChar("w", settings.width);
  p.putUChar("h", settings.height);
  p.putUChar("cmode", settings.colorMode);
  RGB m = settings.monoColor;
  p.putUInt("mcol", ((uint32_t)m.r << 16) | ((uint32_t)m.g << 8) | m.b);
  p.putUChar("shape", settings.shape);
  p.putUChar("gap", settings.gap);
  p.putUChar("bri", settings.brightness);
  p.putBool("off", settings.showOff);
  p.putUChar("bl", settings.backlight);
  p.putUChar("rot", settings.rotation);
  p.putString("host", settings.hostname);
  p.putString("tz", settings.tz);
  p.end();
}

static const char *COLOR_MODES[] = {"rgb", "mono", "gray"};
static const char *SHAPES[] = {"round", "square", "rounded"};

const char *colorModeName(ColorMode m) { return COLOR_MODES[m <= CM_GRAY ? m : 0]; }

bool colorModeFromName(const char *s, ColorMode &out) {
  for (uint8_t i = 0; i < 3; i++)
    if (!strcmp(s, COLOR_MODES[i])) {
      out = (ColorMode)i;
      return true;
    }
  return false;
}

const char *shapeName(LedShape s) { return SHAPES[s <= SHAPE_ROUNDED ? s : 0]; }

bool shapeFromName(const char *s, LedShape &out) {
  for (uint8_t i = 0; i < 3; i++)
    if (!strcmp(s, SHAPES[i])) {
      out = (LedShape)i;
      return true;
    }
  return false;
}

bool validHostname(const String &h) {
  if (h.length() < 1 || h.length() > 24) return false;
  for (size_t i = 0; i < h.length(); i++) {
    char c = h[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return h[0] != '-' && h[h.length() - 1] != '-';
}

void fitDims(int sw, int sh, int pitch, uint8_t &w, uint8_t &h) {
  int fw = constrain((sw + pitch / 2) / pitch, MX_MIN_DIM, MX_MAX_DIM);
  int fh = constrain((sh + pitch / 2) / pitch, MX_MIN_DIM, MX_MAX_DIM);
  while (fw * fh > MX_MAX_PIXELS) {  // keep the aspect ratio while shrinking
    if (fw * sh >= fh * sw) fw--;
    else fh--;
  }
  w = fw;
  h = fh;
}
