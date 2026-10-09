#include "hw.h"

static const uint32_t LONG_PRESS_MS = 700;
static const uint32_t DEBOUNCE_MS = 30;

void hwBegin() {
#if HAS_BATTERY
  pinMode(SYS_EN_PIN, OUTPUT);
  digitalWrite(SYS_EN_PIN, HIGH);
  analogReadResolution(12);
#endif
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

ButtonEvent hwButtonPoll() {
  static bool down = false;
  static bool longFired = false;
  static uint32_t downAt = 0;
  static uint32_t lastChange = 0;
  uint32_t now = millis();
  bool pressed = digitalRead(BUTTON_PIN) == LOW;

  if (pressed != down && (int32_t)(now - lastChange) > (int32_t)DEBOUNCE_MS) {
    lastChange = now;
    down = pressed;
    if (down) {
      downAt = now;
      longFired = false;
    } else if (!longFired) {
      return BTN_SHORT;
    }
  }
  if (down && !longFired && (int32_t)(now - downAt) >= (int32_t)LONG_PRESS_MS) {
    longFired = true;
    return BTN_LONG;
  }
  return BTN_NONE;
}

void hwStatusLed(uint8_t r, uint8_t g, uint8_t b) {
#if HAS_RGB_LED
  static uint32_t last = 0xFFFFFFFF;
  uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (packed == last) return;
  last = packed;
  rgbLedWrite(RGB_LED_PIN, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}

int hwBatteryMv() {
#if HAS_BATTERY
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  return (int)(sum / 8) * 3;
#else
  return -1;
#endif
}

int hwBatteryPercent(int mv) {
  if (mv < 0) return -1;
  static const int curve[][2] = {{4200, 100}, {4100, 90}, {4000, 78}, {3900, 62}, {3800, 45},
                                 {3700, 25},  {3600, 10}, {3500, 4},  {3300, 0}};
  if (mv >= curve[0][0]) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv >= curve[i][0]) {
      int dv = curve[i - 1][0] - curve[i][0];
      int dp = curve[i - 1][1] - curve[i][1];
      return curve[i][1] + (mv - curve[i][0]) * dp / dv;
    }
  }
  return 0;
}
