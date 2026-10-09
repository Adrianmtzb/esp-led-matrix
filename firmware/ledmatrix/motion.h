// Gravity and shake input for physics effects. Uses the IMU when the board has one; otherwise
// (or when a client injects values over the API) a virtual accelerometer.
#pragma once
#include "config.h"

void motionBegin(uint8_t rotation);
void motionLoop();

// Gravity in matrix coordinates (+x right, +y down), in g. Board upright -> roughly (0, 1).
void motionGravity(float &gx, float &gy);
// Shake energy 0..1, decays over time.
float motionShake();
bool motionHasImu();
bool motionRaw(float &ax, float &ay, float &az);  // last raw IMU sample, false without IMU

// Virtual input from the API/CLI/button. Tilt overrides the IMU for `holdMs`.
void motionInjectTilt(float gx, float gy, uint32_t holdMs);
void motionInjectShake(float amount);
