// LED Matrix: emulates a mono/RGB LED matrix on Waveshare ESP32 LCD boards.
// Controlled from the embedded web UI, the HTTP API (and the MCP server on top of it) or serial.
#include <ArduinoJson.h>
#include "config.h"
#include "display.h"
#include "effects.h"
#include "hw.h"
#include "matrix.h"
#include "motion.h"
#include "net.h"
#include "scene.h"
#include "settings.h"

static String cliLine;

static void printStatus() {
  JsonDocument doc;
  sceneToJson(doc.to<JsonObject>());
  serializeJson(doc, Serial);
  Serial.println();
  Serial.println(netStatusLine());
  Serial.printf("board: %s fw=%s matrix=%dx%d pitch=%d screen=%dx%d heap=%u psram=%u imu=%d\n", BOARD_NAME, FW_VERSION,
                mx::W, mx::H, displayPitch(), displayWidth(), displayHeight(), ESP.getFreeHeap(), ESP.getFreePsram(),
                motionHasImu());
  int mv = hwBatteryMv();
  if (mv >= 0) Serial.printf("battery: %dmV (%d%%)\n", mv, hwBatteryPercent(mv));
}

// Dumps the LCD framebuffer as base64 between markers; tools/screenshot.py turns it into a PNG.
static void printShot() {
  const uint16_t *fb = displayFramebuffer();
  if (!fb) return;
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t *bytes = (const uint8_t *)fb;
  size_t len = (size_t)displayWidth() * displayHeight() * 2;
  Serial.printf("SHOT %d %d\n", displayWidth(), displayHeight());
  char line[77];
  size_t col = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = bytes[i] << 16;
    if (i + 1 < len) v |= bytes[i + 1] << 8;
    if (i + 2 < len) v |= bytes[i + 2];
    line[col++] = B64[(v >> 18) & 63];
    line[col++] = B64[(v >> 12) & 63];
    line[col++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
    line[col++] = i + 2 < len ? B64[v & 63] : '=';
    if (col >= 76) {
      line[col] = 0;
      Serial.println(line);
      col = 0;
    }
  }
  if (col) {
    line[col] = 0;
    Serial.println(line);
  }
  Serial.println("ENDSHOT");
}

static void applyCli(JsonDocument &doc) {
  String err;
  if (!sceneApply(doc.as<JsonVariantConst>(), err, true)) Serial.println("error: " + err);
  else Serial.println("ok");
}

static void handleCli(String line) {
  line.trim();
  if (line.isEmpty()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String rest = sp < 0 ? "" : line.substring(sp + 1);
  rest.trim();

  if (cmd == "help") {
    Serial.println(
        "commands: status | text <msg> | notify <msg> | effect <name> | effects | next | json <scene json> | clear\n"
        "          size <w> <h> | bri <1-255> | bl <5-255> | mode rgb|mono|gray | imu | shake | tilt <gx> <gy>\n"
        "          wifi <ssid> <pass> | forget | host <name> | tz <posix> | shot | reboot");
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "text" || cmd == "notify") {
    if (cmd == "notify") {
      sceneNotify(rest.c_str(), {255, 255, 255}, 1);
      Serial.println("ok");
      return;
    }
    JsonDocument doc;
    doc["mode"] = "text";
    doc["text"] = rest;
    applyCli(doc);
  } else if (cmd == "effect") {
    JsonDocument doc;
    doc["mode"] = "effect";
    doc["name"] = rest;
    applyCli(doc);
  } else if (cmd == "effects") {
    for (int i = 0; i < EFFECT_COUNT; i++) Serial.printf("  %-10s %s\n", EFFECTS[i].name, EFFECTS[i].desc);
  } else if (cmd == "next") {
    sceneNextEffect(1);
    Serial.println(sceneModeName());
  } else if (cmd == "json") {
    JsonDocument doc;
    if (deserializeJson(doc, rest)) Serial.println("error: invalid JSON");
    else applyCli(doc);
  } else if (cmd == "clear") {
    String err;
    scenePixelsFill(BLACK_RGB, err);
    Serial.println("ok");
  } else if (cmd == "size") {
    int w = 0, h = 0;
    sscanf(rest.c_str(), "%d %d", &w, &h);
    if (!mx::resize(w, h)) {
      Serial.println("error: 4-64 each, w*h <= 4096");
      return;
    }
    settings.fit = false;
    settings.width = w;
    settings.height = h;
    settingsSave();
    sceneOnMatrixResized();
    displayRelayout();
    Serial.println("ok");
  } else if (cmd == "bri") {
    settings.brightness = constrain(rest.toInt(), 1, 255);
    settingsSave();
    displayRelayout();
    Serial.printf("brightness=%d\n", settings.brightness);
  } else if (cmd == "bl") {
    settings.backlight = constrain(rest.toInt(), 5, 255);
    settingsSave();
    displaySetBacklight(settings.backlight);
    Serial.printf("backlight=%d duty=%u\n", settings.backlight, ledcRead(LCD_BL));
  } else if (cmd == "mode") {
    if (!colorModeFromName(rest.c_str(), settings.colorMode)) {
      Serial.println("error: rgb, mono or gray");
      return;
    }
    settingsSave();
    displayRelayout();
    Serial.println("ok");
  } else if (cmd == "imu") {
    float ax, ay, az, gx, gy;
    bool has = motionRaw(ax, ay, az);
    motionGravity(gx, gy);
    Serial.printf("imu=%d raw=(%.2f, %.2f, %.2f) gravity=(%.2f, %.2f) shake=%.2f\n", has, ax, ay, az, gx, gy,
                  motionShake());
  } else if (cmd == "shake") {
    motionInjectShake(1.0f);
    Serial.println("ok");
  } else if (cmd == "tilt") {
    float gx = 0, gy = 1;
    sscanf(rest.c_str(), "%f %f", &gx, &gy);
    motionInjectTilt(gx, gy, 10000);
    Serial.println("ok");
  } else if (cmd == "wifi") {
    int s2 = rest.indexOf(' ');
    String ssid = s2 < 0 ? rest : rest.substring(0, s2);
    String pass = s2 < 0 ? "" : rest.substring(s2 + 1);
    netSaveWifi(ssid, pass);
    Serial.println("saved, rebooting");
    delay(200);
    ESP.restart();
  } else if (cmd == "forget") {
    netForgetWifi();
  } else if (cmd == "host") {
    rest.toLowerCase();
    if (!validHostname(rest)) {
      Serial.println("error: 1-24 chars a-z 0-9 -");
      return;
    }
    settings.hostname = rest;
    settingsSave();
    Serial.println("saved, applies after reboot");
  } else if (cmd == "tz") {
    settings.tz = rest;
    settingsSave();
    setenv("TZ", rest.c_str(), 1);
    tzset();
    Serial.println("ok");
  } else if (cmd == "shot") {
    printShot();
  } else if (cmd == "reboot") {
    ESP.restart();
  } else {
    Serial.println("unknown command, try help");
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cliLine.length()) handleCli(cliLine);
      cliLine = "";
    } else if (cliLine.length() < 4096) {
      cliLine += c;
    }
  }
}

static void handleButton() {
  ButtonEvent e = hwButtonPoll();
  if (e == BTN_SHORT) {
    // In the liquid effect the button acts as a "shake" (essential on boards without an IMU).
    if (sceneIsEffect("liquid")) motionInjectShake(0.9f);
    else sceneNextEffect(1);
  } else if (e == BTN_LONG) {
    String msg = netConnected() ? settings.hostname + ".local " + netIp() : "WiFi: " + netApSsid();
    sceneNotify(msg.c_str(), {0, 200, 255}, 1);
  }
}

// Mirrors the average matrix color on the board's RGB LED, dimmed.
static void updateStatusLed() {
#if HAS_RGB_LED
  static uint32_t last = 0;
  if (millis() - last < 100) return;
  last = millis();
  uint32_t r = 0, g = 0, b = 0;
  int n = mx::count();
  for (int i = 0; i < n; i++) {
    RGB c = displayMap(mx::buf[i]);
    r += c.r;
    g += c.g;
    b += c.b;
  }
  hwStatusLed(r / n / 6, g / n / 6, b / n / 6);
#endif
}

void setup() {
  Serial.begin(115200);
  hwBegin();
  settingsLoad();
  mx::resize(settings.width, settings.height);
  displayBegin();
  motionBegin(settings.rotation);
  setenv("TZ", settings.tz.c_str(), 1);
  tzset();
  sceneBegin();
  netBegin();
  Serial.printf("%s %s on %s\n", FW_NAME, FW_VERSION, BOARD_NAME);
}

void loop() {
  netLoop();
  motionLoop();
  pollSerial();
  handleButton();
  sceneLoop();
  if (sceneTick() || displayNeedsRedraw()) displayRender();
  updateStatusLed();
  if (netRestartRequested()) {
    delay(300);  // let the HTTP response go out
    ESP.restart();
  }
  delay(1);
}
