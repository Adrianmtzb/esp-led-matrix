// Project-wide constants and board selection by compiler target.
#pragma once
#include <Arduino.h>
#include <sdkconfig.h>

#define FW_NAME "LED Matrix"
#define FW_VERSION "0.1.0"
#define AP_SSID_PREFIX "LedMatrix-"
#define DEFAULT_TZ "CST6"
#define MDNS_SERVICE "ledmatrix"

// Virtual matrix limits
#define MX_MIN_DIM 4
#define MX_MAX_DIM 64
#define MX_MAX_PIXELS 4096
#define MX_DEFAULT_W 32
#define MX_DEFAULT_H 16

#define TICK_FPS 50
#define MAX_TEXT_LEN 240
#define PIXELS_PERSIST_MAX 3072  // static images up to this many bytes survive a reboot

#if defined(CONFIG_IDF_TARGET_ESP32C6)
#include "boards/c6_lcd147.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "boards/s3_touch169.h"
#else
#error "Unsupported target: add a header under boards/ and select it here"
#endif
