#include "display.h"
#include <Arduino_GFX_Library.h>

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX *panel = nullptr;
static Arduino_Canvas *gfx = nullptr;

static int16_t screenW = 0, screenH = 0;
// Cells tile the whole screen: column i spans [colX[i], colX[i+1]), likewise for rows. Each LED is
// a square of ledSize centered in its cell, so any matrix size fills the screen edge to edge.
static int16_t colX[MX_MAX_DIM + 1], rowY[MX_MAX_DIM + 1];
static int16_t pitch = 1, ledSize = 1;
static uint16_t shown[MX_MAX_PIXELS];
static bool fullRedraw = true;

static inline uint16_t to565(RGB c) { return ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3); }

void displayBegin() {
  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED, FSPI);
  panel = new Arduino_ST7789(bus, LCD_RST, settings.rotation, true, LCD_NATIVE_W, LCD_NATIVE_H, LCD_COL_OFFSET,
                             LCD_ROW_OFFSET, LCD_COL_OFFSET, LCD_ROW_OFFSET);
  bool landscape = settings.rotation & 1;
  screenW = landscape ? LCD_NATIVE_H : LCD_NATIVE_W;
  screenH = landscape ? LCD_NATIVE_W : LCD_NATIVE_H;
  gfx = new Arduino_Canvas(screenW, screenH, panel);
  gfx->begin(80000000);
  gfx->fillScreen(0);
  gfx->flush();
  ledcAttachChannel(LCD_BL, 5000, 8, 0);  // channel 0 / timer 0, see LEDC notes
  displaySetBacklight(settings.backlight);
  displayRelayout();
}

void displaySetBacklight(uint8_t v) { ledcWrite(LCD_BL, v); }

void displayRelayout() {
  int mw = mx::W, mh = mx::H;
  for (int i = 0; i <= mw; i++) colX[i] = i * screenW / mw;
  for (int j = 0; j <= mh; j++) rowY[j] = j * screenH / mh;
  int p = max(1, min(screenW / mw, screenH / mh));  // smallest cell side
  pitch = p;

  int gap = 0;
  if (p >= 4) {
    gap = (p * settings.gap + 50) / 100;
    if (settings.gap > 0 && gap == 0) gap = 1;
    if (gap > p - 2) gap = p - 2;
  } else if (p == 3 && settings.gap > 0) {
    gap = 1;
  }
  ledSize = p - gap;
  fullRedraw = true;
}

RGB displayMap(RGB c) {
  uint8_t bri = settings.brightness;
  switch (settings.colorMode) {
    case CM_MONO: {
      uint8_t lum = max(c.r, max(c.g, c.b));
      return lum >= 80 ? mx::scale(settings.monoColor, bri) : BLACK_RGB;
    }
    case CM_GRAY: {
      uint8_t lum = max(c.r, max(c.g, c.b));
      return mx::scale(mx::scale(settings.monoColor, lum), bri);
    }
    default:
      return bri == 255 ? c : mx::scale(c, bri);
  }
}

RGB displayOffColor() {
  if (!settings.showOff) return BLACK_RGB;
  if (settings.colorMode == CM_RGB) return {22, 22, 26};
  RGB m = mx::scale(settings.monoColor, 34);
  return {(uint8_t)max<int>(m.r, 8), (uint8_t)max<int>(m.g, 8), (uint8_t)max<int>(m.b, 8)};
}

static void drawLed(int i, uint16_t color) {
  int cx = i % mx::W, cy = i / mx::W;
  int s = ledSize;
  int x = colX[cx] + (colX[cx + 1] - colX[cx] - s) / 2;
  int y = rowY[cy] + (rowY[cy + 1] - rowY[cy] - s) / 2;
  if (s < 3 || settings.shape == SHAPE_SQUARE) {
    gfx->fillRect(x, y, s, s, color);
  } else if (settings.shape == SHAPE_ROUNDED) {
    gfx->fillRoundRect(x, y, s, s, max(1, s / 4), color);
  } else {
    gfx->fillRoundRect(x, y, s, s, s / 2, color);
  }
}

void displayRender() {
  if (!gfx) return;
  bool changed = false;
  if (fullRedraw) {
    gfx->fillScreen(0);
    changed = true;
  }
  RGB off = displayOffColor();
  int n = mx::count();
  for (int i = 0; i < n; i++) {
    RGB c = displayMap(mx::buf[i]);
    uint16_t col = to565(c.isBlack() ? off : c);
    if (!fullRedraw && shown[i] == col) continue;
    shown[i] = col;
    drawLed(i, col);
    changed = true;
  }
  fullRedraw = false;
  if (changed) gfx->flush();
}

bool displayNeedsRedraw() { return fullRedraw; }
int displayPitch() { return pitch; }
uint16_t displayWidth() { return screenW; }
uint16_t displayHeight() { return screenH; }
const uint16_t *displayFramebuffer() { return gfx ? gfx->getFramebuffer() : nullptr; }
