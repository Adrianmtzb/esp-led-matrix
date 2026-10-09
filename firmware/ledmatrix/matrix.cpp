#include "matrix.h"
#include "font5x7.h"

namespace mx {

uint8_t W = MX_DEFAULT_W, H = MX_DEFAULT_H;
RGB buf[MX_MAX_PIXELS];

bool resize(int w, int h) {
  if (w < MX_MIN_DIM || h < MX_MIN_DIM || w > MX_MAX_DIM || h > MX_MAX_DIM) return false;
  if (w * h > MX_MAX_PIXELS) return false;
  W = w;
  H = h;
  fill(BLACK_RGB);
  return true;
}

void fill(RGB c) {
  int n = count();
  for (int i = 0; i < n; i++) buf[i] = c;
}

void fade(uint8_t amount) {
  uint16_t k = 255 - amount;
  int n = count();
  for (int i = 0; i < n; i++) {
    buf[i].r = (buf[i].r * k) >> 8;
    buf[i].g = (buf[i].g * k) >> 8;
    buf[i].b = (buf[i].b * k) >> 8;
  }
}

void add(int x, int y, RGB c) {
  if ((unsigned)x >= W || (unsigned)y >= H) return;
  RGB &p = buf[y * W + x];
  p.r = min(255, p.r + c.r);
  p.g = min(255, p.g + c.g);
  p.b = min(255, p.b + c.b);
}

// ---- Text ----

static uint8_t glyphFor(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return cp - 0x20;
  switch (cp) {
    case 0xE1: return 95;   // á
    case 0xE9: return 96;   // é
    case 0xED: return 97;   // í
    case 0xF3: return 98;   // ó
    case 0xFA: return 99;   // ú
    case 0xF1: return 100;  // ñ
    case 0xD1: return 101;  // Ñ
    case 0xFC: return 102;  // ü
    case 0xBF: return 103;  // ¿
    case 0xA1: return 104;  // ¡
    case 0xB0: return 105;  // °
    case 0x20AC: return 106;  // €
    case 0x2665: case 0x2764: return 107;  // ♥ ❤
    // Uppercase accents do not fit in 7 rows: fall back to the plain letter.
    case 0xC1: return 'A' - 0x20;
    case 0xC9: return 'E' - 0x20;
    case 0xCD: return 'I' - 0x20;
    case 0xD3: return 'O' - 0x20;
    case 0xDA: case 0xDC: return 'U' - 0x20;
  }
  return FONT_GLYPH_UNKNOWN;
}

int decodeUtf8(const char *s, uint8_t *out, int maxLen) {
  int n = 0;
  const uint8_t *p = (const uint8_t *)s;
  while (*p && n < maxLen) {
    uint32_t cp;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && p[1]) {
      cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
      p += 4;
      cp = '?';
    } else {
      p++;
      continue;
    }
    if (cp == 0xFE0F) continue;  // emoji variation selector
    if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
    out[n++] = glyphFor(cp);
  }
  return n;
}

static void glyphSpan(uint8_t g, int &first, int &last) {
  first = 5;
  last = -1;
  if (g >= FONT_GLYPH_COUNT) return;
  for (int c = 0; c < 5; c++) {
    if (FONT5X7[g][c]) {
      if (first == 5) first = c;
      last = c;
    }
  }
}

int glyphWidth(uint8_t g) {
  int f, l;
  glyphSpan(g, f, l);
  return l < 0 ? 0 : l - f + 1;
}

static int advance(uint8_t g, bool fixed) {
  if (fixed) return 6;
  int w = glyphWidth(g);
  return w == 0 ? 3 : w + 1;  // blank glyph (space) is 3 columns wide
}

int textWidth(const uint8_t *glyphs, int n, uint8_t size, bool fixed) {
  int w = 0;
  for (int i = 0; i < n; i++) w += advance(glyphs[i], fixed);
  if (n > 0) w -= 1;  // no trailing spacing
  return w * size;
}

int drawGlyph(int x, int y, uint8_t g, RGB c, uint8_t size, bool fixed) {
  if (g >= FONT_GLYPH_COUNT) g = FONT_GLYPH_UNKNOWN;
  int first = 0, last = 4;
  if (!fixed) {
    glyphSpan(g, first, last);
    if (last < 0) return 3 * size;
  }
  int cx = x;
  for (int col = first; col <= last; col++, cx += size) {
    if (cx + size <= 0 || cx >= W) continue;
    uint8_t bits = FONT5X7[g][col];
    for (int row = 0; row < 7; row++) {
      if (!(bits & (1 << row))) continue;
      for (int dy = 0; dy < size; dy++)
        for (int dx = 0; dx < size; dx++) set(cx + dx, y + row * size + dy, c);
    }
  }
  return advance(g, fixed) * size;
}

int drawText(int x, int y, const uint8_t *glyphs, int n, RGB c, uint8_t size, bool fixed) {
  for (int i = 0; i < n; i++) {
    if (x >= W) break;
    x += drawGlyph(x, y, glyphs[i], c, size, fixed);
  }
  return x;
}

// ---- Color ----

RGB hsv(uint8_t h, uint8_t s, uint8_t v) {
  if (s == 0) return {v, v, v};
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
  }
}

RGB scale(RGB c, uint8_t s) {
  return {(uint8_t)((c.r * (s + 1)) >> 8), (uint8_t)((c.g * (s + 1)) >> 8), (uint8_t)((c.b * (s + 1)) >> 8)};
}

RGB blend(RGB a, RGB b, uint8_t t) {
  return {(uint8_t)(a.r + (((int)b.r - a.r) * t) / 255), (uint8_t)(a.g + (((int)b.g - a.g) * t) / 255),
          (uint8_t)(a.b + (((int)b.b - a.b) * t) / 255)};
}

uint8_t sin8(uint8_t x) {
  static uint8_t table[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)(127.5f + 127.5f * sinf(i * 6.2831853f / 256.0f));
    ready = true;
  }
  return table[x];
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseColor(const char *s, RGB &out) {
  if (!s) return false;
  if (*s == '#') s++;
  size_t len = strlen(s);
  int v[6];
  if (len == 6) {
    for (int i = 0; i < 6; i++)
      if ((v[i] = hexVal(s[i])) < 0) return false;
    out = {(uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]), (uint8_t)(v[4] * 16 + v[5])};
    return true;
  }
  if (len == 3) {
    for (int i = 0; i < 3; i++)
      if ((v[i] = hexVal(s[i])) < 0) return false;
    out = {(uint8_t)(v[0] * 17), (uint8_t)(v[1] * 17), (uint8_t)(v[2] * 17)};
    return true;
  }
  return false;
}

void formatColor(RGB c, char *out) { snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b); }

}  // namespace mx
