// Board-dependent peripherals: power latch, BOOT button, status LED, battery.
#pragma once
#include "config.h"

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

void hwBegin();
ButtonEvent hwButtonPoll();
void hwStatusLed(uint8_t r, uint8_t g, uint8_t b);  // no-op without an RGB LED
int hwBatteryMv();                                   // -1 without a battery
int hwBatteryPercent(int mv);
