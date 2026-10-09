#include "motion.h"
#include <Wire.h>
#include <math.h>

// QMI8658 registers
#define QMI_WHO_AM_I 0x00
#define QMI_CTRL1 0x02
#define QMI_CTRL2 0x03
#define QMI_CTRL7 0x08
#define QMI_AX_L 0x35
#define QMI_RESET 0x60

static uint8_t rot = 0;
static bool imuOk = false;
static float rawX = 0, rawY = 0, rawZ = 0;  // last IMU sample (g), sensor axes
static float gravX = 0, gravY = 1;          // low-passed gravity, matrix axes
static float lastAx = 0, lastAy = 0, lastAz = 1;
static float shake = 0;
static float injX = 0, injY = 1;
static uint32_t injUntil = 0;
static uint32_t lastSample = 0;
static uint32_t lastDecay = 0;

#if HAS_IMU
static bool qmiWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool qmiRead(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}
#endif

void motionBegin(uint8_t rotation) {
  rot = rotation & 3;
#if HAS_IMU
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  uint8_t id = 0;
  if (qmiRead(QMI_WHO_AM_I, &id, 1) && id == 0x05) {
    qmiWrite(QMI_RESET, 0xB0);
    delay(20);
    qmiWrite(QMI_CTRL1, 0x40);  // register auto-increment
    qmiWrite(QMI_CTRL2, 0x15);  // accel +-4g, ~250 Hz
    qmiWrite(QMI_CTRL7, 0x01);  // accel enable
    delay(10);
    imuOk = true;
  }
  Serial.printf("imu: %s (id=0x%02X)\n", imuOk ? "QMI8658 ok" : "not found", id);
#endif
}

// Sensor axes -> native portrait axes -> rotated matrix axes. Measured on the S3 board: landscape
// (rotation 1) with the buttons on top reads +1 g on sensor Y, which must map to "down" (+y).
static void toMatrix(float ax, float ay, float &mx, float &my) {
  float nx = -ay, ny = ax;  // gravity in native portrait coordinates
  switch (rot) {
    case 0: mx = nx; my = ny; break;
    case 1: mx = ny; my = -nx; break;
    case 2: mx = -nx; my = -ny; break;
    default: mx = -ny; my = nx; break;
  }
}

void motionLoop() {
  uint32_t now = millis();
  if ((int32_t)(now - lastSample) < 10) return;
  float dt = (now - lastSample) / 1000.0f;
  lastSample = now;
  if (dt > 0.1f) dt = 0.1f;

#if HAS_IMU
  if (imuOk) {
    uint8_t b[6];
    if (qmiRead(QMI_AX_L, b, 6)) {
      const float scale = 1.0f / 8192.0f;  // +-4g
      rawX = (int16_t)(b[0] | (b[1] << 8)) * scale;
      rawY = (int16_t)(b[2] | (b[3] << 8)) * scale;
      rawZ = (int16_t)(b[4] | (b[5] << 8)) * scale;

      float mx, my;
      toMatrix(rawX, rawY, mx, my);
      gravX += (mx - gravX) * 0.25f;
      gravY += (my - gravY) * 0.25f;

      // Shake = jerk between samples; ignore small hand tremor.
      float jx = rawX - lastAx, jy = rawY - lastAy, jz = rawZ - lastAz;
      float jerk = sqrtf(jx * jx + jy * jy + jz * jz);
      lastAx = rawX; lastAy = rawY; lastAz = rawZ;
      if (jerk > 0.25f) shake += (jerk - 0.25f) * 0.12f;
    }
  }
#endif

  // Decay of shake energy (about 1.5 s to fade from full).
  if (lastDecay == 0) lastDecay = now;
  float decay = (now - lastDecay) / 1000.0f;
  lastDecay = now;
  shake -= decay * 0.65f;
  if (shake < 0) shake = 0;
  if (shake > 1) shake = 1;
}

void motionGravity(float &gx, float &gy) {
  uint32_t now = millis();
  if ((int32_t)(injUntil - now) > 0) {
    gx = injX;
    gy = injY;
    return;
  }
  if (imuOk) {
    gx = gravX;
    gy = gravY;
    return;
  }
  // No IMU: a gentle slow sway so the liquid stays alive.
  float t = now / 1000.0f;
  gx = 0.35f * sinf(t * 0.9f) + 0.15f * sinf(t * 2.3f);
  gy = 1.0f;
}

float motionShake() { return shake; }
bool motionHasImu() { return imuOk; }

bool motionRaw(float &ax, float &ay, float &az) {
  ax = rawX; ay = rawY; az = rawZ;
  return imuOk;
}

void motionInjectTilt(float gx, float gy, uint32_t holdMs) {
  injX = constrain(gx, -2.0f, 2.0f);
  injY = constrain(gy, -2.0f, 2.0f);
  injUntil = millis() + holdMs;
}

void motionInjectShake(float amount) {
  shake += constrain(amount, 0.0f, 1.0f);
  if (shake > 1) shake = 1;
}
