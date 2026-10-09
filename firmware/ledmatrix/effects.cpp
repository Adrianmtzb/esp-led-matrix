#include "effects.h"
#include <math.h>
#include <time.h>
#include "motion.h"

using namespace mx;

// Shared scratch memory; only one effect runs at a time.
static uint8_t scratch[2 * MX_MAX_PIXELS];

static uint32_t rngState = 0x12345678;
static inline uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}
static inline int rndRange(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }
static inline float rndf() { return (rnd() & 0xFFFFFF) / 16777216.0f; }

static inline RGB colorOf(const EffectParams &p, RGB def) { return p.hasColor ? p.color : def; }
static inline float speedK(const EffectParams &p) { return p.speed / 5.0f; }

// ---------------------------------------------------------------- rainbow
static float rbPhase = 0;
static void rainbowStart(const EffectParams &) { rbPhase = 0; }
static void rainbowFrame(uint32_t, float dt, const EffectParams &p) {
  rbPhase += dt * 60.0f * speedK(p);
  uint8_t base = (uint8_t)rbPhase;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) set(x, y, hsv(base + (x * 256 / W) + y * 4, 255, 255));
}

// ---------------------------------------------------------------- plasma
static float plT = 0;
static void plasmaStart(const EffectParams &) { plT = 0; }
static void plasmaFrame(uint32_t, float dt, const EffectParams &p) {
  plT += dt * 40.0f * speedK(p);
  uint8_t t = (uint8_t)plT, t2 = (uint8_t)(plT * 0.7f), t3 = (uint8_t)(plT * 1.3f);
  int kx = 512 / W, ky = 512 / H;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int v = sin8(x * kx + t) + sin8(y * ky + t2) + sin8((x * kx + y * ky) / 2 + t3) +
              sin8(sin8(x * 4 + t) / 2 + y * ky / 2);
      set(x, y, hsv((uint8_t)(v / 4 + t2), 240, 255));
    }
  }
}

// ---------------------------------------------------------------- fire
// "Doom fire": the bottom row is the fuel; every other cell copies the heat of a cell below it
// (with a little sideways drift) minus a random loss, so flames lick up and die out at about
// two thirds of the height whatever the matrix size.
static uint32_t fireLast = 0;
static void fireStart(const EffectParams &) {
  memset(scratch, 0, sizeof(scratch));
  fireLast = 0;
}
static RGB heatColor(uint8_t h) {
  if (h < 70) return {(uint8_t)(h * 255 / 70 * h / 70), 0, 0};                          // black -> red
  if (h < 160) return {255, (uint8_t)((h - 70) * 170 / 90), 0};                         // red -> orange
  if (h < 225) return {255, (uint8_t)(170 + (h - 160) * 85 / 65), (uint8_t)((h - 160) * 60 / 65)};  // -> yellow
  return {255, 255, (uint8_t)(60 + (h - 225) * 6)};                                     // -> pale yellow
}
static void fireFrame(uint32_t ms, float, const EffectParams &p) {
  uint8_t *heat = scratch;
  uint32_t interval = 75 - p.speed * 5;  // 25-70 ms per step
  if ((int32_t)(ms - fireLast) >= (int32_t)interval) {
    fireLast = ms;
    // Fuel: hot bottom row with flickering columns.
    for (int x = 0; x < W; x++) heat[(H - 1) * W + x] = rndRange(100) < 85 ? 200 + rndRange(56) : 120 + rndRange(60);
    // Average loss per row so flames reach ~65% of the height.
    int loss = max(4, 255 * 100 / (H * 65));
    for (int y = 0; y < H - 1; y++) {
      for (int x = 0; x < W; x++) {
        int sx = x + rndRange(3) - 1;
        if (sx < 0) sx = 0;
        if (sx >= W) sx = W - 1;
        int h = heat[(y + 1) * W + sx] - rndRange(loss * 2 + 1);
        heat[y * W + x] = h > 0 ? h : 0;
      }
    }
  }
  RGB tint = p.hasColor ? p.color : RGB{255, 255, 255};
  for (int i = 0; i < count(); i++) {
    RGB c = heatColor(heat[i]);
    buf[i] = p.hasColor ? scale(tint, max(c.r, max(c.g, c.b)) * heat[i] / 255) : c;
  }
}

// ---------------------------------------------------------------- rain
static float dropY[MX_MAX_DIM], dropV[MX_MAX_DIM];
static void rainReset(int x) {
  dropY[x] = -rndRange(H * 2);
  dropV[x] = 6.0f + rndf() * 12.0f;
}
static void rainStart(const EffectParams &) {
  fill(BLACK_RGB);
  for (int x = 0; x < W; x++) rainReset(x);
}
static void rainFrame(uint32_t, float dt, const EffectParams &p) {
  RGB c = colorOf(p, {0, 255, 70});
  fade(40);
  for (int x = 0; x < W; x++) {
    dropY[x] += dropV[x] * dt * speedK(p);
    int y = (int)dropY[x];
    if (y >= 0 && y < H) {
      set(x, y, blend(c, {255, 255, 255}, 140));
      if (y > 0) set(x, y - 1, c);
    }
    if (y > H + 4) rainReset(x);
  }
}

// ---------------------------------------------------------------- life
static uint32_t lifeLast = 0;
static uint16_t lifeGen = 0, lifeStable = 0, lifePrevPop = 0;
static void lifeSeed() {
  for (int i = 0; i < count(); i++) scratch[i] = rndRange(100) < 30 ? 1 : 0;
  lifeGen = 0;
  lifeStable = 0;
}
static void lifeStart(const EffectParams &) { lifeSeed(); }
static void lifeFrame(uint32_t ms, float, const EffectParams &p) {
  uint8_t *cur = scratch, *nxt = scratch + MX_MAX_PIXELS;
  uint32_t interval = 40 + (10 - p.speed) * 35;
  if ((int32_t)(ms - lifeLast) >= (int32_t)interval) {
    lifeLast = ms;
    uint16_t pop = 0;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        int n = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int xx = (x + dx + W) % W, yy = (y + dy + H) % H;
            n += cur[yy * W + xx] ? 1 : 0;
          }
        uint8_t age = cur[y * W + x];
        uint8_t alive = age ? (n == 2 || n == 3) : (n == 3);
        nxt[y * W + x] = alive ? (age ? min(255, age + 1) : 1) : 0;
        pop += alive;
      }
    }
    memcpy(cur, nxt, count());
    lifeGen++;
    lifeStable = (pop == lifePrevPop) ? lifeStable + 1 : 0;
    lifePrevPop = pop;
    if (pop == 0 || lifeStable > 25 || lifeGen > 600) lifeSeed();
  }
  RGB c = colorOf(p, {60, 200, 255});
  for (int i = 0; i < count(); i++) {
    uint8_t a = cur[i];
    buf[i] = a == 0 ? BLACK_RGB : a == 1 ? blend(c, {255, 255, 255}, 150) : scale(c, max(110, 255 - a * 3));
  }
}

// ---------------------------------------------------------------- twinkle
static void twinkleStart(const EffectParams &) { fill(BLACK_RGB); }
static void twinkleFrame(uint32_t, float, const EffectParams &p) {
  fade(10 + p.speed * 2);
  int spawns = max(1, count() / 80) * p.speed / 5 + 1;
  for (int i = 0; i < spawns; i++) {
    if (rndRange(3) == 0) continue;
    RGB c = p.hasColor ? p.color : hsv(rnd(), 180 + rndRange(75), 255);
    set(rndRange(W), rndRange(H), c);
  }
}

// ---------------------------------------------------------------- stars
struct Star { float x; uint8_t y, layer; };
static Star stars[48];
static int starCount = 0;
static void starsStart(const EffectParams &) {
  starCount = min(48, max(8, count() / 12));
  for (int i = 0; i < starCount; i++) stars[i] = {(float)rndRange(W), (uint8_t)rndRange(H), (uint8_t)(1 + rndRange(3))};
}
static void starsFrame(uint32_t, float dt, const EffectParams &p) {
  RGB c = colorOf(p, {255, 255, 255});
  fill(BLACK_RGB);
  for (int i = 0; i < starCount; i++) {
    Star &s = stars[i];
    s.x -= s.layer * 4.0f * dt * speedK(p);
    if (s.x < 0) {
      s.x += W;
      s.y = rndRange(H);
      s.layer = 1 + rndRange(3);
    }
    set((int)s.x, s.y, scale(c, 70 + s.layer * 60));
  }
}

// ---------------------------------------------------------------- ripple
struct Ring { float cx, cy, r; uint8_t hue; bool on; };
static Ring rings[4];
static void rippleStart(const EffectParams &) {
  fill(BLACK_RGB);
  for (auto &r : rings) r.on = false;
}
static void rippleFrame(uint32_t, float dt, const EffectParams &p) {
  fade(60);
  float maxR = sqrtf(W * W + H * H);
  for (auto &r : rings) {
    if (!r.on) {
      if (rndRange(40) == 0) r = {(float)rndRange(W), (float)rndRange(H), 0, (uint8_t)rnd(), true};
      continue;
    }
    r.r += dt * 10.0f * speedK(p);
    if (r.r > maxR) {
      r.on = false;
      continue;
    }
    RGB c = p.hasColor ? p.color : hsv(r.hue, 220, 255);
    uint8_t fadeOut = (uint8_t)(255 * (1.0f - r.r / maxR));
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        float d = fabsf(sqrtf((x - r.cx) * (x - r.cx) + (y - r.cy) * (y - r.cy)) - r.r);
        if (d < 1.0f) add(x, y, scale(c, (uint8_t)(fadeOut * (1.0f - d))));
      }
  }
}

// ---------------------------------------------------------------- bounce
// Balls with real physics: gravity from the IMU (tilt to roll them around), restitution on
// walls, rolling friction, elastic ball-to-ball collisions and kicks when shaken.
struct Ball { float x, y, vx, vy; uint8_t hue; };
static Ball balls[8];
static int ballCount = 0;
static float ballR = 0.5f;
static float bounceIdle = 0;

static void bounceStart(const EffectParams &) {
  fill(BLACK_RGB);
  ballR = max(0.5f, min(W, H) / 14.0f);
  ballCount = constrain(count() / 90, 3, 8);
  for (int i = 0; i < ballCount; i++)
    balls[i] = {ballR + rndf() * (W - 2 * ballR), ballR + rndf() * (H / 2.0f), (rndf() - 0.5f) * W, 0,
                (uint8_t)(i * 256 / ballCount)};
  bounceIdle = 0;
}

static void bounceFrame(uint32_t, float dt, const EffectParams &p) {
  float gx, gy;
  motionGravity(gx, gy);
  float shake = motionShake();
  float k = speedK(p);
  float g = H * 6.0f * k;  // cells / s^2 at 1 g
  const float restitution = 0.75f, friction = 0.15f;
  float minX = ballR - 0.5f, maxX = W - 0.5f - ballR, minY = ballR - 0.5f, maxY = H - 0.5f - ballR;

  const int sub = 4;
  float h = dt / sub;
  float energy = 0;
  for (int s = 0; s < sub; s++) {
    for (int i = 0; i < ballCount; i++) {
      Ball &b = balls[i];
      b.vx += gx * g * h;
      b.vy += gy * g * h;
      if (shake > 0.1f && rndf() < shake * 0.3f) {
        b.vx += (rndf() - 0.5f) * shake * W * 4;
        b.vy += (rndf() - 0.5f) * shake * H * 4 - gy * shake * H * 2;
      }
      b.x += b.vx * h;
      b.y += b.vy * h;
      // Walls: bounce with energy loss; friction while touching.
      if (b.x < minX) { b.x = minX; b.vx = -b.vx * restitution; b.vy -= b.vy * friction * h; }
      if (b.x > maxX) { b.x = maxX; b.vx = -b.vx * restitution; b.vy -= b.vy * friction * h; }
      if (b.y < minY) { b.y = minY; b.vy = -b.vy * restitution; b.vx -= b.vx * friction * h; }
      if (b.y > maxY) { b.y = maxY; b.vy = -b.vy * restitution; b.vx -= b.vx * friction * h; }
    }
    // Ball-ball: separate overlapping pairs and exchange the normal velocity (equal masses).
    for (int i = 0; i < ballCount; i++) {
      for (int j = i + 1; j < ballCount; j++) {
        Ball &a = balls[i], &c = balls[j];
        float dx = c.x - a.x, dy = c.y - a.y;
        float d2 = dx * dx + dy * dy, minD = 2 * ballR;
        if (d2 >= minD * minD || d2 < 1e-6f) continue;
        float d = sqrtf(d2), nx = dx / d, ny = dy / d;
        float push = (minD - d) / 2;
        a.x -= nx * push; a.y -= ny * push;
        c.x += nx * push; c.y += ny * push;
        float rel = (c.vx - a.vx) * nx + (c.vy - a.vy) * ny;
        if (rel < 0) {
          float imp = -(1 + restitution) * rel / 2;
          a.vx -= imp * nx; a.vy -= imp * ny;
          c.vx += imp * nx; c.vy += imp * ny;
        }
      }
    }
  }
  for (int i = 0; i < ballCount; i++) energy += fabsf(balls[i].vx) + fabsf(balls[i].vy);

  // Without an IMU nobody tilts the board: kick the balls when they come to rest.
  bounceIdle = energy < ballCount * 0.6f ? bounceIdle + dt : 0;
  if (bounceIdle > 2.0f && !motionHasImu()) {
    for (int i = 0; i < ballCount; i++) {
      balls[i].vy -= H * (2.0f + rndf() * 2.0f);
      balls[i].vx += (rndf() - 0.5f) * W * 2;
    }
    bounceIdle = 0;
  }

  // Draw: short trails plus anti-aliased discs.
  fade(110);
  for (int i = 0; i < ballCount; i++) {
    const Ball &b = balls[i];
    RGB c = p.hasColor ? p.color : hsv(b.hue, 230, 255);
    int x0 = (int)floorf(b.x - ballR - 1), x1 = (int)ceilf(b.x + ballR + 1);
    int y0 = (int)floorf(b.y - ballR - 1), y1 = (int)ceilf(b.y + ballR + 1);
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        float d = sqrtf((x - b.x) * (x - b.x) + (y - b.y) * (y - b.y)) - ballR;
        if (d < 0.5f) {
          float cov = d < -0.5f ? 1.0f : 0.5f - d;
          RGB cur = get(x, y);
          set(x, y, blend(cur, c, (uint8_t)(cov * 255)));
        }
      }
  }
}

// ---------------------------------------------------------------- clock
static void clockStart(const EffectParams &) {}
static void clockFrame(uint32_t ms, float, const EffectParams &p) {
  RGB c = colorOf(p, {120, 220, 255});
  fill(BLACK_RGB);
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  bool synced = now > 1700000000;
  char hh[3], mm[3];
  if (synced) {
    snprintf(hh, sizeof(hh), "%02d", t.tm_hour);
    snprintf(mm, sizeof(mm), "%02d", t.tm_min);
  } else {
    strcpy(hh, "--");
    strcpy(mm, "--");
  }
  uint8_t gh[2], gm[2];
  decodeUtf8(hh, gh, 2);
  decodeUtf8(mm, gm, 2);
  bool colon = !synced || (ms % 1000) < 500;
  uint8_t gc = ':' - 0x20;

  // Single line "HH:MM" when it fits, else stacked HH over MM.
  int digitsW = textWidth(gh, 2, 1, true);  // 11
  int lineW = digitsW * 2 + 1 + glyphWidth(gc) + 1;
  int size = 1;
  while (lineW * (size + 1) <= W && 7 * (size + 1) <= H) size++;
  if (lineW <= W) {
    int x = (W - lineW * size) / 2, y = (H - 7 * size) / 2;
    x = drawText(x, y, gh, 2, c, size, true);
    if (colon) drawGlyph(x, y, gc, c, size);
    x += (glyphWidth(gc) + 1) * size;
    drawText(x, y, gm, 2, c, size, true);
    if (synced && H - (y + 7 * size) >= 2) {
      int bar = (W * t.tm_sec) / 59;
      for (int i = 0; i < bar; i++) set(i, H - 1, scale(c, 90));
    }
  } else {
    int x = (W - digitsW) / 2, y = (H - 15) / 2;
    drawText(x, y, gh, 2, c, 1, true);
    drawText(x, y + 8, gm, 2, scale(c, colon ? 255 : 200), 1, true);
  }
}

// ---------------------------------------------------------------- equalizer
static float eqLevel[MX_MAX_DIM], eqTarget[MX_MAX_DIM], eqPeak[MX_MAX_DIM];
static void eqStart(const EffectParams &) {
  for (int i = 0; i < MX_MAX_DIM; i++) eqLevel[i] = eqTarget[i] = eqPeak[i] = 0;
}
static void eqFrame(uint32_t ms, float dt, const EffectParams &p) {
  int bw = W >= 32 ? 2 : 1;
  int bars = (W + 1) / (bw + 1);
  float k = speedK(p);
  fill(BLACK_RGB);
  float beat = (sin8((uint8_t)(ms / 4)) / 255.0f);
  for (int b = 0; b < bars && b < MX_MAX_DIM; b++) {
    if (rndRange(6) == 0) {
      float bias = 1.0f - fabsf((float)b / bars - 0.3f);
      eqTarget[b] = (rndf() * 0.7f + 0.3f * beat) * bias * H;
    }
    eqLevel[b] += (eqTarget[b] - eqLevel[b]) * min(1.0f, dt * 12 * k);
    if (eqLevel[b] > eqPeak[b]) eqPeak[b] = eqLevel[b];
    else eqPeak[b] -= dt * H * 0.6f * k;
    int h = (int)eqLevel[b];
    for (int y = 0; y < h; y++) {
      uint8_t hue = 96 - (y * 96 / max(1, H - 1));  // green -> red
      RGB c = p.hasColor ? scale(p.color, 120 + y * 135 / H) : hsv(hue, 255, 255);
      for (int dx = 0; dx < bw; dx++) set(b * (bw + 1) + dx, H - 1 - y, c);
    }
    int py = H - 1 - (int)eqPeak[b];
    for (int dx = 0; dx < bw; dx++) set(b * (bw + 1) + dx, py, {255, 255, 255});
  }
}

// ---------------------------------------------------------------- liquid
// Particle liquid: one particle per cell at most, each with a fixed-point position and velocity
// (1/256 cell). Gravity comes from the IMU, so tilting gives momentum and sloshing. Blocked
// particles get a sideways push perpendicular to gravity so the surface levels out like water.
// Shaking kicks particles against gravity (splashes) and adds brightness and sparkles.
struct Drop { int16_t x, y, vx, vy; };
static Drop drops[MX_MAX_PIXELS];
static uint8_t dropShade[MX_MAX_PIXELS];
static int dropCount = 0;
static uint16_t *occ = (uint16_t *)scratch;  // cell -> drop index + 1, 0 = empty

struct Sparkle { uint16_t idx; uint8_t life; };
static Sparkle sparkles[64];
static float liqBoost = 0;

static void liquidStart(const EffectParams &p) {
  memset(scratch, 0, sizeof(scratch));
  int n = count();
  dropCount = n * constrain(p.level, 5, 95) / 100;
  // Fill from the bottom row up.
  for (int i = 0; i < dropCount; i++) {
    int cell = n - 1 - i;
    drops[i] = {(int16_t)((cell % W) * 256 + 128), (int16_t)((cell / W) * 256 + 128), 0, 0};
    dropShade[i] = 175 + rndRange(81);
    occ[cell] = i + 1;
  }
  for (auto &s : sparkles) s.life = 0;
  liqBoost = 0;
}

static inline int cellOf(int x, int y) { return (y >> 8) * W + (x >> 8); }

static const int8_t DIR8[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

// Fallback moves for a particle that could not move: the cells "downhill" of it, best first.
// Diagonals make piles slide; sideways moves let the surface level out like water.
struct FlowDir { int8_t dx, dy; bool sideways; };
static FlowDir flowDirs[4];
static int flowCount = 0;
static bool flowTie = false;           // first two candidates are mirror images (equal weight)
static float flowUx, flowUy;           // unit gravity
static float flowTx, flowTy;           // unit tangent (along the liquid surface)

static void computeFlowDirs(float gx, float gy) {
  float mag = sqrtf(gx * gx + gy * gy);
  flowCount = 0;
  if (mag < 0.12f) return;
  float ux = gx / mag, uy = gy / mag;
  flowUx = ux; flowUy = uy;
  flowTx = -uy; flowTy = ux;
  struct Cand { int8_t dx, dy; float dot; bool side; } c[8];
  int n = 0;
  for (int k = 0; k < 8; k++) {
    float len = (DIR8[k][0] && DIR8[k][1]) ? 1.4142f : 1.0f;
    float dot = (DIR8[k][0] * ux + DIR8[k][1] * uy) / len;
    // Skip the straight-down move (already tried by the physics) and anything uphill.
    if (dot > 0.93f || dot < -0.25f) continue;
    c[n++] = {DIR8[k][0], DIR8[k][1], dot, dot < 0.35f};
  }
  for (int i = 0; i < n; i++)  // sort by dot, descending
    for (int j = i + 1; j < n; j++)
      if (c[j].dot > c[i].dot) { Cand t = c[i]; c[i] = c[j]; c[j] = t; }
  for (int i = 0; i < n && flowCount < 4; i++) flowDirs[flowCount++] = {c[i].dx, c[i].dy, c[i].side};
  flowTie = n > 1 && fabsf(c[0].dot - c[1].dot) < 0.02f;
}

// Pressure stand-in: a jammed particle looks along the surface for a free cell that is lower in
// the gravity field (up to `reach` cells away) and moves there. This is what lets a flat layer
// build a slope when the container is tilted a little.
static int surfaceSlide(int cx, int cy, int reach) {
  int sign = (rnd() & 1) ? 1 : -1;
  for (int pass = 0; pass < 2; pass++, sign = -sign) {
    for (int d = 2; d <= reach; d++) {
      int fx = cx + (int)lroundf(sign * d * flowTx), fy = cy + (int)lroundf(sign * d * flowTy);
      if ((unsigned)fx >= W || (unsigned)fy >= H) break;
      if (occ[fy * W + fx]) continue;
      // Lower potential = further along gravity; demand a real gain so it does not jitter.
      if ((fx - cx) * flowUx + (fy - cy) * flowUy > 0.2f) return fy * W + fx;
    }
  }
  return -1;
}

static void liquidStep(int ax, int ay, int tx, int ty, int kick, bool gravityOn) {
  const int maxX = W * 256 - 1, maxY = H * 256 - 1;
  for (int i = 0; i < dropCount; i++) {
    Drop &d = drops[i];
    int vx = d.vx + ax + (int)(rnd() % 7) - 3;
    int vy = d.vy + ay + (int)(rnd() % 7) - 3;
    if (kick && rndRange(100) < 18) {  // shake: random kick biased against gravity
      vx += (rndRange(2 * kick + 1) - kick) - ax * 6;
      vy += (rndRange(2 * kick + 1) - kick) - ay * 6;
    }
    vx -= vx >> 4;  // viscosity: enough to settle, low enough that a small tilt still flows
    vy -= vy >> 4;
    // Speed cap below one cell per step keeps collisions stable and motion calm.
    int v2 = vx * vx + vy * vy;
    if (v2 > 180 * 180) {
      float k = 180.0f / sqrtf((float)v2);
      vx = (int)(vx * k);
      vy = (int)(vy * k);
    }

    int nx = d.x + vx, ny = d.y + vy;
    if (nx < 0) { nx = 0; vx = -vx / 8; }
    else if (nx > maxX) { nx = maxX; vx = -vx / 8; }
    if (ny < 0) { ny = 0; vy = -vy / 8; }
    else if (ny > maxY) { ny = maxY; vy = -vy / 8; }

    int oldC = cellOf(d.x, d.y), newC = cellOf(nx, ny);
    if (newC != oldC && occ[newC]) {
      int hC = cellOf(nx, d.y), vC = cellOf(d.x, ny);
      bool hFree = hC != oldC && !occ[hC], vFree = vC != oldC && !occ[vC];
      bool preferH = abs(vx) >= abs(vy);
      if (hFree && (preferH || !vFree)) {
        ny = d.y; vy = -vy / 6; newC = hC;
      } else if (vFree) {
        nx = d.x; vx = -vx / 6; newC = vC;
      } else {
        nx = d.x; ny = d.y; vx = -vx / 6; vy = -vy / 6; newC = oldC;
      }
    }
    // Liquid: a particle that could not fall flows to a free cell downhill (diagonal first, then
    // sideways), so piles collapse and the surface levels even with a small tilt.
    if (newC == oldC && gravityOn) {
      int cx = d.x >> 8, cy = d.y >> 8;
      // Randomize ties between mirrored candidates (equal dot) so there is no left/right bias.
      int start = flowTie && (rnd() & 1) ? 1 : 0;
      for (int k = 0; k < flowCount; k++) {
        int idx = k;
        if (k < 2) idx = (k + start) & 1;
        const FlowDir &f = flowDirs[idx];
        if (f.sideways && rndRange(100) < 45) continue;  // sideways spread is slower than falling
        int fx = cx + f.dx, fy = cy + f.dy;
        if ((unsigned)fx >= W || (unsigned)fy >= H || occ[fy * W + fx]) continue;
        nx = fx * 256 + 128;
        ny = fy * 256 + 128;
        vx += f.dx * 24 + tx / 8;
        vy += f.dy * 24 + ty / 8;
        newC = fy * W + fx;
        break;
      }
      if (newC == oldC && rndRange(100) < 35) {
        int c = surfaceSlide(cx, cy, 3 + W / 6);
        if (c >= 0) {
          nx = (c % W) * 256 + 128;
          ny = (c / W) * 256 + 128;
          newC = c;
        }
      }
    }
    if (newC != oldC) {
      occ[oldC] = 0;
      occ[newC] = i + 1;
    }
    d.x = nx; d.y = ny; d.vx = vx; d.vy = vy;
  }
}

static void liquidFrame(uint32_t, float dt, const EffectParams &p) {
  float gx, gy;
  motionGravity(gx, gy);
  float mag = sqrtf(gx * gx + gy * gy);
  float energy = motionShake();
  bool gravityOn = mag > 0.12f;

  // Gravity per step in 1/256 cell per step^2; speed scales the whole simulation.
  float accel = 14.0f + p.speed * 3.0f;
  int ax = (int)(gx * accel), ay = (int)(gy * accel);
  // Tangent (perpendicular to gravity) for the sideways spread.
  float inv = gravityOn ? 1.0f / mag : 0;
  int spread = 30 + p.speed * 4;
  int tx = (int)(-gy * inv * spread), ty = (int)(gx * inv * spread);
  int kick = energy > 0.1f ? (int)(energy * 420) : 0;

  computeFlowDirs(gx, gy);
  int steps = 2 + p.speed / 4;  // substeps per frame
  for (int s = 0; s < steps; s++) liquidStep(ax, ay, tx, ty, kick, gravityOn);

  float target = energy;
  liqBoost += (target - liqBoost) * min(1.0f, dt * (target > liqBoost ? 10.0f : 1.5f));

  // Sparkles: rare when calm, a shower when shaken.
  float rate = 0.04f + liqBoost * 6.0f;
  int spawn = (int)rate + (rndf() < rate - (int)rate ? 1 : 0);
  for (auto &s : sparkles) {
    if (spawn <= 0) break;
    if (s.life) continue;
    int idx = rndRange(count());
    if (!occ[idx] && rndRange(4) != 0) continue;  // mostly on the liquid, some spray in the air
    s = {(uint16_t)idx, (uint8_t)(160 + rndRange(96))};
    spawn--;
  }

  RGB base = colorOf(p, {0, 110, 255});
  RGB white = {255, 255, 255};
  uint8_t level = (uint8_t)(185 + liqBoost * 70);
  uint8_t whiten = (uint8_t)(liqBoost * 110);
  int ux = gravityOn ? -(int)roundf(gx * inv) : 0, uy = gravityOn ? -(int)roundf(gy * inv) : -1;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int idx = y * W + x;
      uint16_t o = occ[idx];
      if (!o) {
        buf[idx] = BLACK_RGB;
        continue;
      }
      const Drop &d = drops[o - 1];
      int ax2 = x + ux, ay2 = y + uy;
      bool surface = (unsigned)ax2 < W && (unsigned)ay2 < H && !occ[ay2 * W + ax2];
      // Fast-moving water turns to foam.
      int speed = (abs(d.vx) + abs(d.vy)) >> 1;
      uint8_t foam = (uint8_t)min(170, speed * 2 / 3 + (surface ? 95 : 0));
      RGB c = blend(scale(base, dropShade[o - 1]), white, foam);
      buf[idx] = scale(blend(c, white, whiten), level);
    }
  }
  for (auto &s : sparkles) {
    if (!s.life) continue;
    if (s.idx < count()) buf[s.idx] = blend(buf[s.idx], blend({255, 240, 160}, white, rndRange(255)), s.life);
    s.life = s.life > 28 ? s.life - 28 : 0;
  }
}

// ---------------------------------------------------------------- registry
const EffectDef EFFECTS[] = {
    {"rainbow", "Diagonal rainbow wash", {255, 0, 0}, rainbowStart, rainbowFrame},
    {"plasma", "Classic demoscene plasma", {255, 0, 255}, plasmaStart, plasmaFrame},
    {"fire", "Rising flames", {255, 80, 0}, fireStart, fireFrame},
    {"rain", "Digital rain, Matrix style", {0, 255, 70}, rainStart, rainFrame},
    {"life", "Conway's Game of Life, reseeds itself", {60, 200, 255}, lifeStart, lifeFrame},
    {"twinkle", "Random twinkling pixels", {255, 255, 255}, twinkleStart, twinkleFrame},
    {"stars", "Parallax starfield", {255, 255, 255}, starsStart, starsFrame},
    {"ripple", "Expanding rings", {0, 180, 255}, rippleStart, rippleFrame},
    {"bounce", "Balls with physics; tilt to roll them, shake to kick", {255, 200, 0}, bounceStart, bounceFrame},
    {"clock", "HH:MM clock (NTP)", {120, 220, 255}, clockStart, clockFrame},
    {"equalizer", "Fake spectrum analyzer", {0, 255, 0}, eqStart, eqFrame},
    {"liquid", "Liquid in a container; tilt with the IMU, shake for sparkles", {0, 110, 255}, liquidStart,
     liquidFrame},
};
const int EFFECT_COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);

int effectFind(const char *name) {
  if (!name) return -1;
  for (int i = 0; i < EFFECT_COUNT; i++)
    if (!strcmp(name, EFFECTS[i].name)) return i;
  return -1;
}
