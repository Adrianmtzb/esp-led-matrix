// What the matrix is showing: scrolling text, an effect, a static image or an animation.
// Every scene change is described as JSON ({"mode": "...", ...}) so the web, the MCP, the
// serial CLI and the persisted state all go through the same path.
#pragma once
#include <ArduinoJson.h>
#include "matrix.h"

void sceneBegin();  // restores the persisted scene
void sceneLoop();   // deferred persistence
bool sceneTick();   // advances the current scene; true when mx::buf changed

bool sceneApply(JsonVariantConst spec, String &err, bool persist = true);
void sceneNotify(const char *text, RGB color, uint8_t repeat);
void sceneNextEffect(int dir);
void sceneOnMatrixResized();  // re-starts the scene for the new matrix size
const char *sceneModeName();
bool sceneIsEffect(const char *name);
void sceneToJson(JsonObject out);

// Image data. Frames are hex ("rrggbb" per pixel) or base64 of raw RGB, row-major.
bool scenePixelsLoad(int w, int h, const char *data, String &err);
bool scenePixelsOps(JsonVariantConst ops, String &err);  // [[x, y, "#rrggbb"], ...]
bool scenePixelsFill(RGB c, String &err);
// Animation upload; append=true adds frames to the current animation.
bool sceneAnimLoad(int w, int h, uint16_t fps, bool loop, JsonArrayConst frames, bool append, String &err,
                   int &status);
