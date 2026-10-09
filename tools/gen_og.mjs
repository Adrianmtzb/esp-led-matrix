#!/usr/bin/env node
// Renders the landing's social card (1200x630 PNG) in plain Node: pixels in a Uint8Array, PNG
// written with zlib. Title and version come from docs/manifest.json, letters from
// shared/font5x7.json and the effect count from shared/effects.json.
// Usage: node tools/gen_og.mjs [out.png]   (default _site/og.png)
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2] || join(root, '_site/og.png');
const manifest = JSON.parse(readFileSync(join(root, 'docs/manifest.json'), 'utf8'));
const FONT = JSON.parse(readFileSync(join(root, 'shared/font5x7.json'), 'utf8')).glyphs.map((g) => g.cols);
const effects = JSON.parse(readFileSync(join(root, 'shared/effects.json'), 'utf8')).effects;

const W = 1200, H = 630;
const px = new Uint8Array(W * H * 3);

function blendPixel(x, y, c, a) {
  if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
  const i = (y * W + x) * 3;
  for (let k = 0; k < 3; k++) px[i + k] = Math.round(px[i + k] + (c[k] - px[i + k]) * Math.min(1, a));
}
function disc(cx, cy, r, c) {
  for (let y = Math.floor(cy - r - 1); y <= Math.ceil(cy + r + 1); y++)
    for (let x = Math.floor(cx - r - 1); x <= Math.ceil(cx + r + 1); x++) {
      const d = Math.hypot(x + 0.5 - cx, y + 0.5 - cy) - r;
      blendPixel(x, y, c, 0.5 - d);
    }
}
function rect(x0, y0, w, h, c) {
  for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) blendPixel(x, y, c, 1);
}
function hsv(h, s, v) {
  const f = (n) => { const k = (n + h * 6) % 6; return v * (1 - s * Math.max(0, Math.min(k, 4 - k, 1))); };
  return [f(5), f(3), f(1)].map((x) => Math.round(x * 255));
}

// Text -> glyph columns (ASCII only here), proportional like the firmware.
function columns(text) {
  const cols = [];
  for (const ch of text) {
    const g = FONT[ch.codePointAt(0) - 0x20] || FONT[31];
    const used = g.map((b, i) => (b ? i : -1)).filter((i) => i >= 0);
    if (!used.length) { cols.push(0, 0, 0); continue; }
    for (let i = used[0]; i <= used[used.length - 1]; i++) cols.push(g[i]);
    cols.push(0);
  }
  cols.pop();
  return cols;
}
// Small solid-pixel text for the captions.
function pixelText(text, x, y, size, c) {
  for (const col of columns(text)) {
    for (let r = 0; r < 7; r++) if (col & (1 << r)) rect(x, y + r * size, size, size, c);
    x += size;
  }
  return x;
}

// Background: vertical gradient.
for (let y = 0; y < H; y++) {
  const t = y / H;
  const c = [11 + 6 * t, 13 + 4 * t, 18 + 10 * t];
  for (let x = 0; x < W; x++) { const i = (y * W + x) * 3; px[i] = c[0]; px[i + 1] = c[1]; px[i + 2] = c[2]; }
}

// LED panel with the title.
const title = columns('LED MATRIX');
const pitch = 18, rows = 11, colsN = title.length + 4;
const panelW = colsN * pitch, panelH = rows * pitch;
const px0 = Math.round((W - panelW) / 2), py0 = 140;
rect(px0 - 18, py0 - 18, panelW + 36, panelH + 36, [6, 7, 10]);
for (let r = 0; r < rows; r++)
  for (let c = 0; c < colsN; c++) {
    const tc = c - 2, tr = r - 2;
    const on = tc >= 0 && tc < title.length && tr >= 0 && tr < 7 && title[tc] & (1 << tr);
    const color = on ? hsv(((tc / title.length) * 0.85 + 0.52) % 1, 0.85, 1) : [24, 25, 31];
    const cx = px0 + c * pitch + pitch / 2, cy = py0 + r * pitch + pitch / 2;
    if (on) disc(cx, cy, pitch * 0.62, color.map((v) => v * 0.25));  // glow
    disc(cx, cy, pitch * 0.38, color);
  }

// Captions.
const cap = 'ESP32 + LCD  -  WEB  -  API  -  MCP';
const capSize = 5, capW = columns(cap).length * capSize;
pixelText(cap, Math.round((W - capW) / 2), py0 + panelH + 70, capSize, [200, 208, 224]);
const info = `${effects.length} EFFECTS  -  v${manifest.version}`;
const infoW = columns(info).length * 4;
pixelText(info, Math.round((W - infoW) / 2), py0 + panelH + 135, 4, [0, 200, 255]);

// PNG encoding.
const CRC = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
function crc32(b) { let c = -1; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; }
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;  // 8-bit RGB
const raw = Buffer.alloc((W * 3 + 1) * H);
for (let y = 0; y < H; y++) Buffer.from(px.buffer, y * W * 3, W * 3).copy(raw, y * (W * 3 + 1) + 1);
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)),
]);
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, png);
console.log(`og.png: ${W}x${H}, ${png.length} bytes -> ${out}`);
