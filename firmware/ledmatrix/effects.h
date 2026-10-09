// Built-in animations. Each effect draws directly into mx::buf once per tick.
#pragma once
#include "matrix.h"

struct EffectParams {
  uint8_t speed = 5;   // 1-10
  RGB color;           // effect-specific main color
  bool hasColor = false;
  uint8_t level = 50;  // liquid fill percentage, 5-95
};

struct EffectDef {
  const char *name;
  const char *desc;
  RGB defaultColor;
  void (*start)(const EffectParams &p);
  void (*frame)(uint32_t ms, float dt, const EffectParams &p);
};

extern const EffectDef EFFECTS[];
extern const int EFFECT_COUNT;

int effectFind(const char *name);  // -1 when unknown
