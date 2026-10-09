#include "net.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include "display.h"
#include "effects.h"
#include "hw.h"
#include "motion.h"
#include "scene.h"
#include "settings.h"
#include "web_assets.h"

static const uint32_t STA_TIMEOUT_MS = 30000;
static const uint32_t STA_RETRY_MS = 15000;
// Largest raw frame payload per /api/anim request that the board parses comfortably.
static const size_t ANIM_CHUNK_BYTES = ANIM_USE_PSRAM ? 96 * 1024 : 6 * 1024;

static WebServer server(80);
static DNSServer dnsServer;
static String ssid, pass;
static bool apMode = false;
static bool staFailed = false;
static bool connected = false;
static bool mdnsStarted = false;
static bool restartRequested = false;
static uint32_t staStartedAt = 0;
static uint32_t lastRetry = 0;

// ------------------------------------------------------------------ helpers

String netApSsid() {
  uint64_t mac = ESP.getEfuseMac();  // eFuse MAC: stable across AP/STA
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%02X%02X", AP_SSID_PREFIX, (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return String(buf);
}

bool netConnected() { return connected; }
bool netApMode() { return apMode; }
bool netRestartRequested() { return restartRequested; }
String netIp() { return connected ? WiFi.localIP().toString() : (apMode ? WiFi.softAPIP().toString() : String("")); }

static bool inSetupMode() { return apMode && !connected; }

static String htmlEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static void sendJson(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(int code, const String &msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(code, doc);
}

static void sendOk() {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

static bool readBody(JsonDocument &doc) {
  if (!server.hasArg("plain")) {
    sendError(400, "expected a JSON body (Content-Type: application/json)");
    return false;
  }
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  if (e) {
    sendError(e == DeserializationError::NoMemory ? 413 : 400, String("invalid JSON: ") + e.c_str());
    return false;
  }
  return true;
}

static void sendSceneResult(bool ok, const String &err) {
  if (!ok) {
    sendError(400, err);
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  sceneToJson(doc["scene"].to<JsonObject>());
  sendJson(200, doc);
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void appendB64(String &out, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = data[i] << 16;
    if (i + 1 < len) v |= data[i + 1] << 8;
    if (i + 2 < len) v |= data[i + 2];
    out += B64[(v >> 18) & 63];
    out += B64[(v >> 12) & 63];
    out += i + 1 < len ? B64[(v >> 6) & 63] : '=';
    out += i + 2 < len ? B64[v & 63] : '=';
  }
}

// ------------------------------------------------------------------ WiFi

void netSaveWifi(const String &s, const String &p) {
  Preferences pr;
  pr.begin("net", false);
  pr.putString("ssid", s);
  pr.putString("pass", p);
  pr.end();
}

void netForgetWifi() {
  Preferences pr;
  pr.begin("net", false);
  pr.remove("ssid");
  pr.remove("pass");
  pr.end();
  restartRequested = true;
}

static void startAp() {
  if (apMode) return;
  apMode = true;
  WiFi.mode(WIFI_AP_STA);  // keep STA alive to scan networks from the portal
  WiFi.softAP(netApSsid().c_str());
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);
  Serial.printf("wifi: AP %s at %s\n", netApSsid().c_str(), WiFi.softAPIP().toString().c_str());
  sceneNotify(("WiFi setup: join " + netApSsid()).c_str(), {255, 180, 0}, 3);
}

static void stopAp() {
  if (!apMode) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apMode = false;
}

static void onConnected() {
  connected = true;
  staFailed = false;
  stopAp();
  String ip = WiFi.localIP().toString();
  Serial.printf("wifi: connected ssid=%s ip=%s rssi=%d\n", ssid.c_str(), ip.c_str(), WiFi.RSSI());
  if (!mdnsStarted && MDNS.begin(settings.hostname.c_str())) {
    mdnsStarted = true;
    MDNS.addService("http", "tcp", 80);
    MDNS.addService(MDNS_SERVICE, "tcp", 80);
    MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "board", BOARD_ID);
    MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "fw", FW_VERSION);
  }
  static bool ntpStarted = false;
  if (!ntpStarted) {
    configTzTime(settings.tz.c_str(), "pool.ntp.org", "time.google.com");
    ntpStarted = true;
  }
  sceneNotify((settings.hostname + ".local  " + ip).c_str(), {0, 200, 255}, 1);
}

// ------------------------------------------------------------------ setup portal

static void captiveRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/setup", true);
  server.send(302, "text/plain", "");
}

static void handleSetupPage() {
  String h;
  h.reserve(3000);
  h += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>LED Matrix setup</title><style>body{font-family:system-ui;background:#0b0d12;color:#e8e8ef;max-width:420px;"
         "margin:auto;padding:20px}select,input,button{width:100%;padding:10px;margin:6px 0;border-radius:8px;border:1px solid #333;"
         "background:#161a22;color:inherit;font-size:16px;box-sizing:border-box}button{background:#ff5a1f;border:0;color:#fff;"
         "font-weight:600}a{color:#7cc4ff}.warn{background:#3a2410;padding:10px;border-radius:8px}</style></head><body>"
         "<h2>LED Matrix</h2><p>Connect this matrix to your WiFi network.</p>");
  if (staFailed) h += "<p class=warn>Could not join <b>" + htmlEscape(ssid) + "</b>. Check the password.</p>";
  h += F("<form method=post action=/api/wifi><label>Network</label><select name=ssid id=s onchange=\"o.style.display=this.value==''?'block':'none'\">");
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) h += F("<option disabled>Scanning...</option>");
  for (int i = 0; i < n && i < 20; i++) {
    String s = htmlEscape(WiFi.SSID(i));
    if (!s.length()) continue;
    h += "<option value=\"" + s + "\">" + s + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  h += F("<option value=''>Other network...</option></select><input name=network id=o placeholder='Network name' style='display:none'>"
         "<label>Password</label><input name=pass type=password autocomplete=new-password>"
         "<button>Save and reboot</button></form><p><a href=/setup>Rescan</a></p></body></html>");
  if (n != WIFI_SCAN_RUNNING) {  // fresh list for the next visit
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
  }
  server.send(200, "text/html", h);
}

static void handleWifi() {
  String s, p;
  if (server.hasArg("ssid") || server.hasArg("network")) {
    s = server.arg("ssid");
    if (s.isEmpty()) s = server.arg("network");
    p = server.arg("pass");
  } else {
    JsonDocument doc;
    if (!readBody(doc)) return;
    s = doc["ssid"] | "";
    p = doc["pass"] | "";
  }
  if (s.length() < 1 || s.length() > 32 || p.length() > 64) {
    sendError(400, "ssid must be 1-32 chars, pass up to 64");
    return;
  }
  netSaveWifi(s, p);
  restartRequested = true;
  String host = settings.hostname + ".local";
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><body style='font-family:"
              "system-ui;background:#0b0d12;color:#eee;padding:20px'><h3>Saved. Rebooting...</h3><p>Join <b>" +
                  htmlEscape(s) + "</b> and open <a style='color:#7cc4ff' href='http://" + host + "'>http://" + host +
                  "</a></p>");
}

// ------------------------------------------------------------------ API handlers

static void displayToJson(JsonObject d) {
  d["fit"] = settings.fit;
  d["fit_pitch"] = settings.pitch;
  d["width"] = mx::W;
  d["height"] = mx::H;
  d["color_mode"] = colorModeName(settings.colorMode);
  char hex[8];
  mx::formatColor(settings.monoColor, hex);
  d["mono_color"] = hex;
  d["shape"] = shapeName(settings.shape);
  d["gap"] = settings.gap;
  d["brightness"] = settings.brightness;
  d["show_off"] = settings.showOff;
  d["backlight"] = settings.backlight;
  d["rotation"] = settings.rotation;
  d["pitch"] = displayPitch();
  d["screen"] = String(displayWidth()) + "x" + String(displayHeight());
}

static void handleState() {
  JsonDocument doc;
  doc["ok"] = true;
  sceneToJson(doc["scene"].to<JsonObject>());
  displayToJson(doc["display"].to<JsonObject>());
  JsonObject m = doc["motion"].to<JsonObject>();
  float gx, gy;
  motionGravity(gx, gy);
  m["imu"] = motionHasImu();
  m["gx"] = roundf(gx * 100) / 100;
  m["gy"] = roundf(gy * 100) / 100;
  m["shake"] = roundf(motionShake() * 100) / 100;
  sendJson(200, doc);
}

static void handleInfo() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["name"] = FW_NAME;
  doc["firmware"] = FW_VERSION;
  doc["board"] = BOARD_NAME;
  doc["board_id"] = BOARD_ID;
  doc["hostname"] = settings.hostname;
  doc["ip"] = netIp();
  doc["ssid"] = connected ? WiFi.SSID() : String("");
  doc["rssi"] = connected ? WiFi.RSSI() : 0;
  doc["ap_mode"] = apMode;
  doc["tz"] = settings.tz;
  time_t now = time(nullptr);
  if (now > 1700000000) {
    char buf[32];
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    doc["local_time"] = buf;
  }
  doc["uptime_s"] = millis() / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["psram_free"] = ESP.getFreePsram();
  doc["imu"] = motionHasImu();
  doc["anim_max_bytes"] = ANIM_MAX_BYTES;
  doc["anim_chunk_bytes"] = ANIM_CHUNK_BYTES;
  int mv = hwBatteryMv();
  if (mv >= 0) {
    doc["battery_mv"] = mv;
    doc["battery_pct"] = hwBatteryPercent(mv);
  }
  JsonArray fx = doc["effects"].to<JsonArray>();
  for (int i = 0; i < EFFECT_COUNT; i++) fx.add(EFFECTS[i].name);
  sendJson(200, doc);
}

static void handleFrame() {
  String data;
  int n = mx::count();
  data.reserve(n * 4 + 4);
  static uint8_t row[MX_MAX_DIM * 3];
  // Encode row by row (row bytes are a multiple of 3, so base64 chunks concatenate cleanly).
  for (int y = 0; y < mx::H; y++) {
    for (int x = 0; x < mx::W; x++) {
      RGB c = displayMap(mx::buf[y * mx::W + x]);
      row[x * 3] = c.r;
      row[x * 3 + 1] = c.g;
      row[x * 3 + 2] = c.b;
    }
    appendB64(data, row, mx::W * 3);
  }
  JsonDocument doc;
  doc["w"] = mx::W;
  doc["h"] = mx::H;
  char hex[8];
  mx::formatColor(displayOffColor(), hex);
  doc["off"] = hex;
  doc["shape"] = shapeName(settings.shape);
  doc["gap"] = settings.gap;
  doc["data"] = data;
  sendJson(200, doc);
}

static void handleEffects() {
  JsonDocument doc;
  JsonArray arr = doc["effects"].to<JsonArray>();
  for (int i = 0; i < EFFECT_COUNT; i++) {
    JsonObject e = arr.add<JsonObject>();
    e["name"] = EFFECTS[i].name;
    e["description"] = EFFECTS[i].desc;
    char hex[8];
    mx::formatColor(EFFECTS[i].defaultColor, hex);
    e["default_color"] = hex;
  }
  doc["ok"] = true;
  sendJson(200, doc);
}

static void handleSceneWithMode(const char *mode) {
  JsonDocument doc;
  if (!readBody(doc)) return;
  if (mode) doc["mode"] = mode;
  String err;
  bool ok = sceneApply(doc.as<JsonVariantConst>(), err, true);
  sendSceneResult(ok, err);
}

static void handleNotify() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  const char *text = doc["text"] | (const char *)nullptr;
  if (!text || !*text) {
    sendError(400, "notify needs \"text\"");
    return;
  }
  RGB c = {255, 255, 255};
  if (doc["color"].is<const char *>() && !mx::parseColor(doc["color"], c)) {
    sendError(400, "bad color, use #rrggbb");
    return;
  }
  sceneNotify(text, c, constrain(doc["repeat"] | 1, 1, 10));
  sendOk();
}

static void handlePixels() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  String err;
  bool ok;
  if (doc["set"].is<JsonArray>()) {
    ok = scenePixelsOps(doc["set"], err);
  } else if (doc["fill"].is<const char *>()) {
    RGB c;
    if (!mx::parseColor(doc["fill"], c)) {
      sendError(400, "bad fill color");
      return;
    }
    ok = scenePixelsFill(c, err);
  } else if (doc["data"].is<const char *>()) {
    ok = scenePixelsLoad(doc["w"] | (int)mx::W, doc["h"] | (int)mx::H, doc["data"], err);
  } else {
    sendError(400, "expected {data, w, h}, {set: [[x, y, \"#rrggbb\"], ...]} or {fill: \"#rrggbb\"}");
    return;
  }
  sendSceneResult(ok, err);
}

static void handleAnim() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  if (!doc["frames"].is<JsonArray>()) {
    sendError(400, "expected {w, h, fps, loop, frames: [hex|base64, ...], append}");
    return;
  }
  String err;
  int status = 400;
  bool ok = sceneAnimLoad(doc["w"] | (int)mx::W, doc["h"] | (int)mx::H, doc["fps"] | 8, doc["loop"] | true,
                          doc["frames"].as<JsonArrayConst>(), doc["append"] | false, err, status);
  if (!ok) {
    sendError(status, err);
    return;
  }
  sendSceneResult(true, "");
}

static void handleClear() {
  String err;
  sendSceneResult(scenePixelsFill(BLACK_RGB, err), err);
}

static void handleDisplay() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  Settings next = settings;
  // Explicit width/height switch fit off; fit/fit_pitch derive the size from the screen.
  if (!doc["width"].isNull() || !doc["height"].isNull()) next.fit = false;
  if (!doc["fit"].isNull()) next.fit = doc["fit"].as<bool>();
  if (!doc["fit_pitch"].isNull()) next.pitch = constrain(doc["fit_pitch"].as<int>(), 4, 40);
  int w = doc["width"] | (int)next.width, h = doc["height"] | (int)next.height;
  if (next.fit) {
    uint8_t fw, fh;
    fitDims(displayWidth(), displayHeight(), next.pitch, fw, fh);
    w = fw;
    h = fh;
  }
  if (w < MX_MIN_DIM || h < MX_MIN_DIM || w > MX_MAX_DIM || h > MX_MAX_DIM || w * h > MX_MAX_PIXELS) {
    sendError(400, "width and height must be 4-64 and width*height <= 4096");
    return;
  }
  next.width = w;
  next.height = h;
  if (doc["color_mode"].is<const char *>() && !colorModeFromName(doc["color_mode"], next.colorMode)) {
    sendError(400, "color_mode must be rgb, mono or gray");
    return;
  }
  if (doc["mono_color"].is<const char *>() && !mx::parseColor(doc["mono_color"], next.monoColor)) {
    sendError(400, "bad mono_color, use #rrggbb");
    return;
  }
  if (doc["shape"].is<const char *>() && !shapeFromName(doc["shape"], next.shape)) {
    sendError(400, "shape must be round, square or rounded");
    return;
  }
  if (!doc["gap"].isNull()) next.gap = constrain(doc["gap"].as<int>(), 0, 60);
  if (!doc["brightness"].isNull()) next.brightness = constrain(doc["brightness"].as<int>(), 1, 255);
  if (!doc["show_off"].isNull()) next.showOff = doc["show_off"].as<bool>();
  if (!doc["backlight"].isNull()) next.backlight = constrain(doc["backlight"].as<int>(), 5, 255);
  if (!doc["rotation"].isNull()) next.rotation = doc["rotation"].as<int>() & 3;

  bool resized = next.width != mx::W || next.height != mx::H;
  bool rotated = next.rotation != settings.rotation;
  settings = next;
  settingsSave();
  if (resized) {
    mx::resize(settings.width, settings.height);
    sceneOnMatrixResized();
  }
  displaySetBacklight(settings.backlight);
  displayRelayout();
  if (rotated) restartRequested = true;  // the canvas is allocated for one orientation

  JsonDocument out;
  out["ok"] = true;
  out["rebooting"] = rotated;
  displayToJson(out["display"].to<JsonObject>());
  sendJson(200, out);
}

static void handleSettings() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  bool reboot = false;
  if (doc["hostname"].is<const char *>()) {
    String h = doc["hostname"].as<String>();
    h.toLowerCase();
    if (!validHostname(h)) {
      sendError(400, "hostname must be 1-24 chars of a-z, 0-9 and -");
      return;
    }
    reboot = reboot || h != settings.hostname;
    settings.hostname = h;
  }
  if (doc["tz"].is<const char *>()) {
    String tz = doc["tz"].as<String>();
    if (tz.length() < 3 || tz.length() > 60) {
      sendError(400, "tz must be a POSIX TZ string such as CST6 or CET-1CEST,M3.5.0,M10.5.0/3");
      return;
    }
    settings.tz = tz;
    setenv("TZ", tz.c_str(), 1);
    tzset();
  }
  settingsSave();
  if (reboot) restartRequested = true;
  JsonDocument out;
  out["ok"] = true;
  out["rebooting"] = reboot;
  sendJson(200, out);
}

static void handleMotion() {
  if (server.method() == HTTP_POST) {
    JsonDocument doc;
    if (!readBody(doc)) return;
    if (!doc["tilt_x"].isNull() || !doc["tilt_y"].isNull()) {
      motionInjectTilt(doc["tilt_x"] | 0.0f, doc["tilt_y"] | 1.0f, constrain(doc["hold_ms"] | 5000, 100, 600000));
    }
    if (!doc["shake"].isNull()) motionInjectShake(doc["shake"].as<float>());
  }
  JsonDocument out;
  float gx, gy;
  motionGravity(gx, gy);
  out["ok"] = true;
  out["imu"] = motionHasImu();
  out["gx"] = gx;
  out["gy"] = gy;
  out["shake"] = motionShake();
  float ax, ay, az;
  if (motionRaw(ax, ay, az)) {
    JsonArray raw = out["raw"].to<JsonArray>();
    raw.add(ax);
    raw.add(ay);
    raw.add(az);
  }
  sendJson(200, out);
}

static void handleRoot() {
  if (inSetupMode()) {
    handleSetupPage();
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static void setupRoutes() {
  server.enableCORS(true);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetupPage);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/frame", HTTP_GET, handleFrame);
  server.on("/api/effects", HTTP_GET, handleEffects);
  server.on("/api/scene", HTTP_POST, []() { handleSceneWithMode(nullptr); });
  server.on("/api/text", HTTP_POST, []() { handleSceneWithMode("text"); });
  server.on("/api/effect", HTTP_POST, []() { handleSceneWithMode("effect"); });
  server.on("/api/notify", HTTP_POST, handleNotify);
  server.on("/api/pixels", HTTP_POST, handlePixels);
  server.on("/api/anim", HTTP_POST, handleAnim);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.on("/api/display", HTTP_POST, handleDisplay);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/motion", HTTP_ANY, handleMotion);
  server.on("/api/reboot", HTTP_POST, []() {
    restartRequested = true;
    sendOk();
  });

  const char *probes[] = {"/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
                          "/connecttest.txt",     "/ncsi.txt",                  "/redirect",     "/fwlink",
                          "/success.txt",         "/canonical.html"};
  for (const char *p : probes)
    server.on(p, HTTP_ANY, []() { inSetupMode() ? captiveRedirect() : server.send(204); });

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.send(204);
      return;
    }
    if (inSetupMode() && !server.uri().startsWith("/api/")) {
      captiveRedirect();
      return;
    }
    sendError(404, "not found: " + server.uri());
  });
}

// ------------------------------------------------------------------ lifecycle

void netBegin() {
  Preferences pr;
  pr.begin("net", true);
  ssid = pr.getString("ssid", "");
  pass = pr.getString("pass", "");
  pr.end();

  WiFi.persistent(false);
  setupRoutes();
  if (ssid.isEmpty()) {
    startAp();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(settings.hostname.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    staStartedAt = millis();
    Serial.printf("wifi: joining \"%s\"\n", ssid.c_str());
  }
  server.begin();
}

void netLoop() {
  if (apMode) dnsServer.processNextRequest();
  server.handleClient();

  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !connected) {
    onConnected();
  } else if (!up && connected) {
    connected = false;
    staStartedAt = now;
    Serial.println("wifi: connection lost, reconnecting");
  }
  if (!connected && !ssid.isEmpty()) {
    if (!apMode && (int32_t)(now - staStartedAt) > (int32_t)STA_TIMEOUT_MS) {
      staFailed = true;
      startAp();
      lastRetry = now;
    }
    if (apMode && (int32_t)(now - lastRetry) > (int32_t)STA_RETRY_MS) {
      lastRetry = now;
      WiFi.reconnect();
    }
  }
}

String netStatusLine() {
  String s = "wifi: ";
  s += connected ? "connected" : (apMode ? "ap" : "connecting");
  s += " ssid=" + ssid + " ip=" + netIp() + " rssi=" + String(connected ? WiFi.RSSI() : 0);
  s += " host=" + settings.hostname + ".local";
  if (apMode) s += " ap_ssid=" + netApSsid();
  return s;
}
