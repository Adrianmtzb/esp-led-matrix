// Renders the virtual matrix onto the LCD, emulating LEDs (shape, gap, unlit dots, mono color).
#pragma once
#include "settings.h"

void displayBegin();
void displayRelayout();               // call after matrix size or LED style changes
void displayRender();                 // draws changed LEDs and flushes when something changed
bool displayNeedsRedraw();            // true after a relayout until the next render
void displaySetBacklight(uint8_t v);
RGB displayMap(RGB c);                // final on-screen color of a lit pixel (mode + brightness)
RGB displayOffColor();                // color of an unlit LED (black when showOff is false)
int displayPitch();
uint16_t displayWidth();
uint16_t displayHeight();
const uint16_t *displayFramebuffer();  // RGB565, little-endian, displayWidth x displayHeight
