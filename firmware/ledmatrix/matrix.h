// Virtual LED matrix: an RGB framebuffer of W x H "LEDs" plus drawing helpers and the font.
#pragma once
#include "config.h"

struct RGB {
  uint8_t r, g, b;
  bool operator==(const RGB &o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const RGB &o) const { return !(*this == o); }
  bool isBlack() const { return (r | g | b) == 0; }
};

static const RGB BLACK_RGB = {0, 0, 0};

namespace mx {

extern uint8_t W, H;
extern RGB buf[MX_MAX_PIXELS];

bool resize(int w, int h);

inline void set(int x, int y, RGB c) {
  if ((unsigned)x < W && (unsigned)y < H) buf[y * W + x] = c;
}
inline RGB get(int x, int y) {
  if ((unsigned)x < W && (unsigned)y < H) return buf[y * W + x];
  return BLACK_RGB;
}
inline int count() { return W * H; }

void fill(RGB c);
void fade(uint8_t amount);  // scale every pixel by (255 - amount) / 255
void add(int x, int y, RGB c);

// Text. Strings are decoded to glyph indices once; drawing works on those.
int decodeUtf8(const char *s, uint8_t *out, int maxLen);
int glyphWidth(uint8_t g);  // columns used, 0 for blank glyphs (space)
int textWidth(const uint8_t *glyphs, int n, uint8_t size, bool fixed = false);
int drawGlyph(int x, int y, uint8_t g, RGB c, uint8_t size, bool fixed = false);
int drawText(int x, int y, const uint8_t *glyphs, int n, RGB c, uint8_t size, bool fixed = false);

// Color helpers
RGB hsv(uint8_t h, uint8_t s, uint8_t v);
RGB scale(RGB c, uint8_t s);
RGB blend(RGB a, RGB b, uint8_t t);  // t=0 -> a, 255 -> b
uint8_t sin8(uint8_t x);
bool parseColor(const char *s, RGB &out);  // "#rrggbb", "rrggbb", "#rgb"
void formatColor(RGB c, char *out);        // writes "#rrggbb" (8 bytes)

}  // namespace mx
