#include "scene.h"
#include <Preferences.h>
#include "effects.h"
#include "motion.h"

enum SceneMode : uint8_t { SM_TEXT, SM_EFFECT, SM_PIXELS, SM_ANIM };
static const char *MODE_NAMES[] = {"text", "effect", "pixels", "anim"};
enum ScrollMode : uint8_t { SCROLL_AUTO, SCROLL_ON, SCROLL_STATIC };
static const char *SCROLL_NAMES[] = {"auto", "scroll", "static"};

static const char *DEFAULT_SCENE = "{\"mode\":\"text\",\"text\":\"LED Matrix\",\"rainbow\":true}";

static SceneMode mode = SM_TEXT;
static bool dirty = true;
static uint32_t lastTick = 0;

// Text
struct TextState {
  String raw;
  uint8_t glyphs[MAX_TEXT_LEN];
  int n = 0;
  RGB fg = {255, 255, 255}, bg = BLACK_RGB;
  float speed = 20;  // pixels per second
  ScrollMode scroll = SCROLL_AUTO;
  uint8_t sizeReq = 0;  // 0 = auto
  bool rainbow = false;
  float pos = 0;
  uint16_t loops = 0;
  uint8_t repeat = 0;  // 0 = forever; >0 only for notifications
  uint32_t startedAt = 0;
};
static TextState txt;

// Effect
static int effectIdx = 0;
static EffectParams effectParams;

// Static image
static RGB pix[MX_MAX_PIXELS];
static uint8_t pixW = 0, pixH = 0;

// Animation
static uint8_t *animBuf = nullptr;
static size_t animCap = 0;
static uint8_t animW = 0, animH = 0;
static uint16_t animFrames = 0, animFps = 8, animFrame = 0;
static bool animLoop = true;
static uint32_t animLastFrame = 0;

// Persistence and notifications
static String sceneSpec;     // JSON of the active (persistent) scene
static String restoreSpec;   // scene to return to after a notification
static bool notifying = false;
static uint32_t saveAt = 0;  // deferred NVS write, 0 = nothing pending
static bool savePixels = false;

// ------------------------------------------------------------------ helpers

static int hexNib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int b64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

// Decodes npix RGB pixels from hex (6 chars/pixel) or base64 (4 chars/pixel).
static bool decodeFrame(const char *s, int npix, uint8_t *out, String &err) {
  size_t len = s ? strlen(s) : 0;
  size_t bytes = (size_t)npix * 3;
  if (len == bytes * 2) {
    for (size_t i = 0; i < bytes; i++) {
      int hi = hexNib(s[2 * i]), lo = hexNib(s[2 * i + 1]);
      if (hi < 0 || lo < 0) {
        err = "invalid hex in frame data";
        return false;
      }
      out[i] = hi * 16 + lo;
    }
    return true;
  }
  if (len == (size_t)npix * 4) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 4) {
      int v[4];
      for (int j = 0; j < 4; j++) {
        v[j] = s[i + j] == '=' ? 0 : b64Val(s[i + j]);
        if (v[j] < 0) {
          err = "invalid base64 in frame data";
          return false;
        }
      }
      uint32_t w = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
      if (o < bytes) out[o++] = w >> 16;
      if (o < bytes) out[o++] = w >> 8;
      if (o < bytes) out[o++] = w;
    }
    return true;
  }
  err = "frame data length must be w*h*6 (hex) or w*h*4 (base64) chars; got " + String(len) + " for " +
        String(npix) + " pixels";
  return false;
}

static void blit(const uint8_t *rgb, int w, int h) {
  mx::fill(BLACK_RGB);
  int ox = ((int)mx::W - w) / 2, oy = ((int)mx::H - h) / 2;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint8_t *p = rgb + (y * w + x) * 3;
      mx::set(x + ox, y + oy, {p[0], p[1], p[2]});
    }
}

static void scheduleSave(bool withPixels) {
  saveAt = millis() + 2000;
  if (!saveAt) saveAt = 1;
  savePixels = savePixels || withPixels;
}

static uint8_t textSize() {
  if (txt.sizeReq) return max<int>(1, min<int>(txt.sizeReq, mx::H / 7));
  return max(1, min(4, (int)mx::H / 8));
}

// ------------------------------------------------------------------ apply

static bool applyText(JsonVariantConst o, String &err) {
  const char *t = o["text"] | (const char *)nullptr;
  if (!t) {
    err = "text mode needs \"text\"";
    return false;
  }
  TextState n;
  n.raw = t;
  if (n.raw.length() > MAX_TEXT_LEN) n.raw = n.raw.substring(0, MAX_TEXT_LEN);
  n.n = mx::decodeUtf8(n.raw.c_str(), n.glyphs, MAX_TEXT_LEN);
  if (o["color"].is<const char *>() && !mx::parseColor(o["color"], n.fg)) {
    err = "bad color, use #rrggbb";
    return false;
  }
  if (o["bg"].is<const char *>() && !mx::parseColor(o["bg"], n.bg)) {
    err = "bad bg color, use #rrggbb";
    return false;
  }
  n.speed = constrain(o["speed"] | 20.0f, 1.0f, 120.0f);
  const char *sc = o["scroll"] | "auto";
  n.scroll = SCROLL_AUTO;
  for (uint8_t i = 0; i < 3; i++)
    if (!strcmp(sc, SCROLL_NAMES[i])) n.scroll = (ScrollMode)i;
  n.sizeReq = constrain(o["size"] | 0, 0, 8);
  n.rainbow = o["rainbow"] | false;
  n.repeat = constrain(o["repeat"] | 0, 0, 50);
  n.startedAt = millis();
  txt = n;
  mode = SM_TEXT;
  return true;
}

static bool applyEffect(JsonVariantConst o, String &err) {
  const char *name = o["name"] | (const char *)nullptr;
  int idx = effectFind(name);
  if (idx < 0) {
    err = "unknown effect; valid: ";
    for (int i = 0; i < EFFECT_COUNT; i++) err += String(i ? ", " : "") + EFFECTS[i].name;
    return false;
  }
  EffectParams p;
  p.speed = constrain(o["speed"] | 5, 1, 10);
  p.level = constrain(o["level"] | 50, 5, 95);
  p.color = EFFECTS[idx].defaultColor;
  if (o["color"].is<const char *>()) {
    if (!mx::parseColor(o["color"], p.color)) {
      err = "bad color, use #rrggbb";
      return false;
    }
    p.hasColor = true;
  }
  effectIdx = idx;
  effectParams = p;
  mx::fill(BLACK_RGB);
  EFFECTS[idx].start(p);
  mode = SM_EFFECT;
  return true;
}

bool sceneApply(JsonVariantConst spec, String &err, bool persist) {
  const char *m = spec["mode"] | "";
  bool ok = false;
  bool withPixels = false;
  if (!strcmp(m, "text")) {
    ok = applyText(spec, err);
  } else if (!strcmp(m, "effect")) {
    ok = applyEffect(spec, err);
  } else if (!strcmp(m, "pixels")) {
    if (spec["data"].is<const char *>()) {
      int w = spec["w"] | (int)mx::W, h = spec["h"] | (int)mx::H;
      if (!scenePixelsLoad(w, h, spec["data"], err)) return false;
      return true;  // scenePixelsLoad already switched mode and scheduled persistence
    }
    if (!pixW) {
      pixW = mx::W;
      pixH = mx::H;
      memset(pix, 0, sizeof(pix));
    }
    mode = SM_PIXELS;
    ok = true;
    withPixels = true;
  } else if (!strcmp(m, "anim")) {
    if (!animFrames) {
      err = "no animation loaded; POST /api/anim first";
      return false;
    }
    animFrame = 0;
    animLastFrame = 0;
    mode = SM_ANIM;
    ok = true;
  } else {
    err = "mode must be text, effect, pixels or anim";
    return false;
  }
  if (!ok) return false;
  dirty = true;
  notifying = txt.repeat > 0 && mode == SM_TEXT;
  if (persist && !notifying) {
    // Store a normalized copy without bulky fields.
    JsonDocument doc;
    doc.set(spec);
    doc.remove("data");
    doc.remove("repeat");
    sceneSpec = "";
    serializeJson(doc, sceneSpec);
    scheduleSave(withPixels);
  }
  return true;
}

void sceneNotify(const char *text, RGB color, uint8_t repeat) {
  if (!notifying) restoreSpec = sceneSpec;
  JsonDocument doc;
  doc["mode"] = "text";
  doc["text"] = text;
  char hex[8];
  mx::formatColor(color, hex);
  doc["color"] = hex;
  doc["scroll"] = "scroll";
  doc["repeat"] = max<int>(1, repeat);
  String err;
  sceneApply(doc.as<JsonVariantConst>(), err, false);
}

static void restoreAfterNotify() {
  notifying = false;
  JsonDocument doc;
  String err;
  if (deserializeJson(doc, restoreSpec) || !sceneApply(doc.as<JsonVariantConst>(), err, false)) {
    deserializeJson(doc, DEFAULT_SCENE);
    sceneApply(doc.as<JsonVariantConst>(), err, false);
  }
}

void sceneNextEffect(int dir) {
  int next = mode == SM_EFFECT ? (effectIdx + dir + EFFECT_COUNT) % EFFECT_COUNT : 0;
  JsonDocument doc;
  doc["mode"] = "effect";
  doc["name"] = EFFECTS[next].name;
  String err;
  sceneApply(doc.as<JsonVariantConst>(), err, true);
}

void sceneOnMatrixResized() {
  if (mode == SM_EFFECT) {
    mx::fill(BLACK_RGB);
    EFFECTS[effectIdx].start(effectParams);
  }
  txt.pos = 0;
  dirty = true;
}

const char *sceneModeName() { return MODE_NAMES[mode]; }
bool sceneIsEffect(const char *name) { return mode == SM_EFFECT && !strcmp(EFFECTS[effectIdx].name, name); }

void sceneToJson(JsonObject out) {
  out["mode"] = MODE_NAMES[mode];
  out["notifying"] = notifying;
  char hex[8];
  if (mode == SM_TEXT) {
    out["text"] = txt.raw;
    mx::formatColor(txt.fg, hex);
    out["color"] = hex;
    mx::formatColor(txt.bg, hex);
    out["bg"] = hex;
    out["speed"] = txt.speed;
    out["scroll"] = SCROLL_NAMES[txt.scroll];
    out["size"] = txt.sizeReq;
    out["rainbow"] = txt.rainbow;
  } else if (mode == SM_EFFECT) {
    out["name"] = EFFECTS[effectIdx].name;
    out["speed"] = effectParams.speed;
    mx::formatColor(effectParams.color, hex);
    out["color"] = hex;
    out["level"] = effectParams.level;
  } else if (mode == SM_PIXELS) {
    out["w"] = pixW;
    out["h"] = pixH;
  } else {
    out["w"] = animW;
    out["h"] = animH;
    out["frames"] = animFrames;
    out["fps"] = animFps;
    out["loop"] = animLoop;
  }
}

// ------------------------------------------------------------------ pixels & anim

bool scenePixelsLoad(int w, int h, const char *data, String &err) {
  if (w < 1 || h < 1 || w > MX_MAX_DIM || h > MX_MAX_DIM || w * h > MX_MAX_PIXELS) {
    err = "w and h must be 1-64 and w*h <= 4096";
    return false;
  }
  if (!decodeFrame(data, w * h, (uint8_t *)pix, err)) return false;
  pixW = w;
  pixH = h;
  JsonDocument doc;
  doc["mode"] = "pixels";
  return sceneApply(doc.as<JsonVariantConst>(), err, true);
}

bool scenePixelsOps(JsonVariantConst ops, String &err) {
  if (!ops.is<JsonArrayConst>()) {
    err = "\"set\" must be an array of [x, y, \"#rrggbb\"]";
    return false;
  }
  if (mode != SM_PIXELS || pixW != mx::W || pixH != mx::H) {
    // Start drawing on top of what is on screen right now, at matrix size.
    memcpy(pix, mx::buf, sizeof(RGB) * mx::count());
    pixW = mx::W;
    pixH = mx::H;
  }
  for (JsonArrayConst op : ops.as<JsonArrayConst>()) {
    int x = op[0] | -1, y = op[1] | -1;
    RGB c;
    if (!mx::parseColor(op[2] | "", c)) {
      err = "bad color in set op";
      return false;
    }
    if (x >= 0 && y >= 0 && x < pixW && y < pixH) pix[y * pixW + x] = c;
  }
  JsonDocument doc;
  doc["mode"] = "pixels";
  return sceneApply(doc.as<JsonVariantConst>(), err, true);
}

bool scenePixelsFill(RGB c, String &err) {
  pixW = mx::W;
  pixH = mx::H;
  for (int i = 0; i < pixW * pixH; i++) pix[i] = c;
  JsonDocument doc;
  doc["mode"] = "pixels";
  return sceneApply(doc.as<JsonVariantConst>(), err, true);
}

bool sceneAnimLoad(int w, int h, uint16_t fps, bool loop, JsonArrayConst frames, bool append, String &err,
                   int &status) {
  status = 400;
  if (w < 1 || h < 1 || w > MX_MAX_DIM || h > MX_MAX_DIM || w * h > MX_MAX_PIXELS) {
    err = "w and h must be 1-64 and w*h <= 4096";
    return false;
  }
  if (append && (w != animW || h != animH || !animFrames)) {
    err = "append needs an existing animation with the same w and h";
    return false;
  }
  size_t frameBytes = (size_t)w * h * 3;
  size_t start = append ? animFrames : 0;
  size_t total = start + frames.size();
  if (frames.size() == 0) {
    err = "frames must be a non-empty array";
    return false;
  }
  if (total * frameBytes > ANIM_MAX_BYTES) {
    status = 413;
    err = "animation too large: max " + String(ANIM_MAX_BYTES / frameBytes) + " frames at this size";
    return false;
  }
  size_t need = total * frameBytes;
  if (need > animCap) {
    if (mode == SM_ANIM) mode = SM_PIXELS;  // never play from a buffer being moved
#if ANIM_USE_PSRAM
    uint8_t *nb = (uint8_t *)heap_caps_realloc(animBuf, need, MALLOC_CAP_SPIRAM);
#else
    uint8_t *nb = (uint8_t *)realloc(animBuf, need);
#endif
    if (!nb) {
      status = 507;
      err = "out of memory for animation";
      return false;
    }
    animBuf = nb;
    animCap = need;
  }
  for (size_t i = 0; i < frames.size(); i++) {
    if (!decodeFrame(frames[i] | "", w * h, animBuf + (start + i) * frameBytes, err)) {
      err = "frame " + String(start + i) + ": " + err;
      return false;
    }
  }
  animW = w;
  animH = h;
  animFrames = total;
  animFps = constrain(fps, 1, 60);
  animLoop = loop;
  animFrame = append ? animFrame : 0;
  animLastFrame = 0;
  mode = SM_ANIM;
  dirty = true;
  notifying = false;
  // Animations live in RAM only: remember the mode so notifications return to it, but do not
  // persist it (after a reboot the previous persisted scene comes back).
  sceneSpec = "{\"mode\":\"anim\"}";
  return true;
}

// ------------------------------------------------------------------ tick

static void drawTextScene(uint32_t now, float dt) {
  uint8_t s = textSize();
  int width = mx::textWidth(txt.glyphs, txt.n, s);
  bool scrolling = txt.scroll == SCROLL_ON || (txt.scroll == SCROLL_AUTO && width > mx::W);
  int y = ((int)mx::H - 7 * s) / 2;
  int x;
  if (scrolling) {
    txt.pos += txt.speed * dt;
    x = mx::W - (int)txt.pos;
    if (x < -width) {
      txt.pos = 0;
      x = mx::W;
      txt.loops++;
    }
  } else {
    x = ((int)mx::W - width) / 2;
    // A static notification counts one "loop" every 2.5 s.
    txt.loops = (now - txt.startedAt) / 2500;
  }
  mx::fill(txt.bg);
  if (txt.rainbow) {
    uint8_t base = now / 12;
    for (int i = 0; i < txt.n && x < mx::W; i++) x += mx::drawGlyph(x, y, txt.glyphs[i], mx::hsv(base + i * 18, 255, 255), s);
  } else {
    mx::drawText(x, y, txt.glyphs, txt.n, txt.fg, s);
  }
}

bool sceneTick() {
  uint32_t now = millis();
  uint32_t interval = 1000 / TICK_FPS;
  if (!dirty && (int32_t)(now - lastTick) < (int32_t)interval) return false;
  float dt = lastTick ? (now - lastTick) / 1000.0f : 0;
  if (dt > 0.2f) dt = 0.2f;
  lastTick = now;
  bool wasDirty = dirty;
  dirty = false;

  switch (mode) {
    case SM_TEXT: {
      uint8_t s = textSize();
      bool scrolling = txt.scroll == SCROLL_ON ||
                       (txt.scroll == SCROLL_AUTO && mx::textWidth(txt.glyphs, txt.n, s) > mx::W);
      if (!scrolling && !txt.rainbow && !wasDirty && !notifying) return false;
      drawTextScene(now, dt);
      if (notifying && txt.loops >= txt.repeat) restoreAfterNotify();
      return true;
    }
    case SM_EFFECT:
      EFFECTS[effectIdx].frame(now, dt, effectParams);
      return true;
    case SM_PIXELS:
      if (!wasDirty) return false;
      blit((const uint8_t *)pix, pixW, pixH);
      return true;
    case SM_ANIM: {
      uint32_t frameMs = 1000 / animFps;
      if (!wasDirty && animLastFrame && (int32_t)(now - animLastFrame) < (int32_t)frameMs) return false;
      if (animLastFrame && !wasDirty) {
        animFrame++;
        if (animFrame >= animFrames) animFrame = animLoop ? 0 : animFrames - 1;
      }
      animLastFrame = now;
      blit(animBuf + (size_t)animFrame * animW * animH * 3, animW, animH);
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ persistence

void sceneBegin() {
  Preferences p;
  p.begin("scene", true);
  String spec = p.getString("spec", DEFAULT_SCENE);
  uint8_t w = p.getUChar("pw", 0), h = p.getUChar("ph", 0);
  if (w && h && (size_t)w * h * 3 <= PIXELS_PERSIST_MAX) {
    if (p.getBytes("pix", pix, (size_t)w * h * 3) == (size_t)w * h * 3) {
      pixW = w;
      pixH = h;
    }
  }
  p.end();

  JsonDocument doc;
  String err;
  if (deserializeJson(doc, spec) || !sceneApply(doc.as<JsonVariantConst>(), err, false)) {
    deserializeJson(doc, DEFAULT_SCENE);
    sceneApply(doc.as<JsonVariantConst>(), err, false);
  }
  sceneSpec = spec;
}

void sceneLoop() {
  if (!saveAt || (int32_t)(millis() - saveAt) < 0) return;
  saveAt = 0;
  Preferences p;
  p.begin("scene", false);
  p.putString("spec", sceneSpec);
  if (savePixels) {
    size_t bytes = (size_t)pixW * pixH * 3;
    if (bytes && bytes <= PIXELS_PERSIST_MAX) {
      p.putUChar("pw", pixW);
      p.putUChar("ph", pixH);
      p.putBytes("pix", pix, bytes);
    } else {
      p.putUChar("pw", 0);
    }
    savePixels = false;
  }
  p.end();
}
