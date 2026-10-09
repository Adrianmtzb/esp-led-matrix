#!/usr/bin/env node
// MCP server (stdio) that wraps the LED Matrix HTTP API as tools.
// Devices: LEDMATRIX_HOSTS="ledmatrix-c6.local,ledmatrix-s3.local" (hostnames or IPs).
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';

const HOSTS = (process.env.LEDMATRIX_HOSTS || 'ledmatrix-c6.local,ledmatrix-s3.local')
  .split(',')
  .map((h) => h.trim())
  .filter(Boolean);
const TIMEOUT_MS = Number(process.env.LEDMATRIX_TIMEOUT_MS || 6000);

// Must match the firmware registry (effects.cpp) and shared/effects.json; `make check` verifies it.
const EFFECTS = ['rainbow', 'plasma', 'fire', 'rain', 'life', 'twinkle', 'stars', 'ripple', 'bounce', 'clock', 'equalizer', 'liquid'];

// Default palette for pixel art. '.' and ' ' are unlit LEDs.
const DEFAULT_PALETTE = {
  '.': '#000000', ' ': '#000000', K: '#000000', W: '#ffffff', R: '#ff0000', G: '#00ff00', B: '#0050ff',
  Y: '#ffd000', O: '#ff7000', C: '#00e0ff', M: '#ff00ff', P: '#a040ff', N: '#ff60a0', D: '#404040',
};

// ------------------------------------------------------------------ device resolution & HTTP

function resolveDevices(device) {
  if (!device) return [HOSTS[0]];
  if (device === 'all') return HOSTS;
  const d = device.toLowerCase();
  const match = HOSTS.find((h) => h.toLowerCase() === d || h.toLowerCase().startsWith(d + '.') || h.toLowerCase().includes(d));
  return [match || device];
}

async function api(host, path, body) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  try {
    const res = await fetch(`http://${host}${path}`, {
      method: body === undefined ? 'GET' : 'POST',
      headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: ctrl.signal,
    });
    let json = {};
    try {
      json = await res.json();
    } catch {}
    if (!res.ok || json.ok === false) throw new Error(json.error || `HTTP ${res.status}`);
    return json;
  } catch (e) {
    if (e.name === 'AbortError') throw new Error(`${host} did not answer within ${TIMEOUT_MS} ms`);
    if (e.cause?.code === 'ENOTFOUND') throw new Error(`${host} not found (is it on the same network?)`);
    throw e;
  } finally {
    clearTimeout(timer);
  }
}

// Runs fn against every target device and returns one text result.
async function onDevices(device, fn) {
  const hosts = resolveDevices(device);
  const lines = await Promise.all(
    hosts.map(async (h) => {
      try {
        return { ok: true, text: `${h}: ${await fn(h)}` };
      } catch (e) {
        return { ok: false, text: `${h}: error - ${e.message}` };
      }
    }),
  );
  return {
    content: [{ type: 'text', text: lines.map((l) => l.text).join('\n') }],
    isError: lines.every((l) => !l.ok),
  };
}

function describeScene(s) {
  if (!s) return 'unknown';
  switch (s.mode) {
    case 'text': return `text "${s.text}" (${s.scroll}, ${s.rainbow ? 'rainbow' : s.color})`;
    case 'effect': return `effect ${s.name} (speed ${s.speed}, color ${s.color}${s.name === 'liquid' ? `, level ${s.level}%` : ''})`;
    case 'pixels': return `static image ${s.w}x${s.h}`;
    case 'anim': return `animation ${s.frames} frames ${s.w}x${s.h} @ ${s.fps} fps`;
    default: return s.mode;
  }
}

// ------------------------------------------------------------------ pixel art helpers

function hexToRgb(hex) {
  const m = /^#?([0-9a-f]{6}|[0-9a-f]{3})$/i.exec(hex);
  if (!m) throw new Error(`bad color "${hex}", use #rrggbb`);
  let h = m[1];
  if (h.length === 3) h = [...h].map((c) => c + c).join('');
  return [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
}

// rows: array of strings, one char per pixel. Returns {w, h, data(hex)}.
function rowsToFrame(rows, palette) {
  const pal = { ...DEFAULT_PALETTE, ...(palette || {}) };
  const h = rows.length;
  const w = Math.max(...rows.map((r) => [...r].length));
  if (!h || !w) throw new Error('rows must contain at least one pixel');
  if (w > 64 || h > 64 || w * h > 4096) throw new Error('image must be at most 64x64 (4096 pixels)');
  let data = '';
  for (const row of rows) {
    const chars = [...row];
    for (let x = 0; x < w; x++) {
      const ch = chars[x] ?? '.';
      const color = pal[ch];
      if (!color) throw new Error(`character "${ch}" is not in the palette`);
      data += hexToRgb(color).map((v) => v.toString(16).padStart(2, '0')).join('');
    }
  }
  return { w, h, data };
}

const ASCII_SET = [
  ['R', [255, 0, 0]], ['O', [255, 112, 0]], ['Y', [255, 208, 0]], ['G', [0, 255, 0]], ['C', [0, 224, 255]],
  ['B', [0, 80, 255]], ['P', [160, 64, 255]], ['M', [255, 0, 255]], ['W', [255, 255, 255]], ['D', [64, 64, 64]],
];

function frameToAscii(f) {
  const bytes = Buffer.from(f.data, 'base64');
  const lines = [];
  const used = new Set();
  for (let y = 0; y < f.h; y++) {
    let line = '';
    for (let x = 0; x < f.w; x++) {
      const i = (y * f.w + x) * 3;
      const [r, g, b] = [bytes[i], bytes[i + 1], bytes[i + 2]];
      if (Math.max(r, g, b) < 24) {
        line += '.';
        continue;
      }
      // Nearest palette color by hue-ish distance on normalized RGB.
      const m = Math.max(r, g, b);
      const n = [r / m, g / m, b / m];
      let best = 'W', bd = Infinity;
      for (const [ch, c] of ASCII_SET) {
        const cm = Math.max(...c);
        const d = c.reduce((s, v, k) => s + (v / cm - n[k]) ** 2, 0) + (ch === 'D' ? 0.5 : 0);
        if (d < bd) { bd = d; best = ch; }
      }
      if (m < 90 && best === 'W') best = 'D';
      used.add(best);
      line += best;
    }
    lines.push(line);
  }
  const legend = ASCII_SET.filter(([ch]) => used.has(ch)).map(([ch, c]) => `${ch}=rgb(${c.join(',')})`).join(' ');
  return `${f.w}x${f.h}, '.' = off${legend ? ', ' + legend : ''}\n${lines.join('\n')}`;
}

// ------------------------------------------------------------------ server & tools

const server = new McpServer({ name: 'ledmatrix', version: '0.1.0' });

const deviceArg = z
  .string()
  .optional()
  .describe(`Target device: hostname, short name (e.g. "s3", "c6"), IP, or "all". Defaults to ${HOSTS[0]}. Known: ${HOSTS.join(', ')}`);
const colorArg = (what) => z.string().regex(/^#?[0-9a-fA-F]{6}$/).optional().describe(`${what} as #rrggbb`);
const paletteArg = z
  .record(z.string(), z.string())
  .optional()
  .describe(`Map of single characters to #rrggbb colors, merged over the default palette: ${Object.entries(DEFAULT_PALETTE)
    .filter(([k]) => k !== ' ')
    .map(([k, v]) => `${k}=${v}`)
    .join(' ')}`);

server.registerTool(
  'list_devices',
  { title: 'List LED matrices', description: 'List the configured LED matrix boards, whether they are online, their size and what they are showing.', inputSchema: {} },
  async () =>
    onDevices('all', async (h) => {
      const [info, state] = await Promise.all([api(h, '/api/info'), api(h, '/api/state')]);
      const d = state.display;
      return `online - ${info.board}, ip ${info.ip}, matrix ${d.width}x${d.height} ${d.color_mode}, imu ${info.imu ? 'yes' : 'no'}, showing ${describeScene(state.scene)}`;
    }),
);

server.registerTool(
  'get_state',
  { title: 'Get matrix state', description: 'Current scene, display settings (size, color mode, LED style) and motion/IMU readings of a matrix.', inputSchema: { device: deviceArg } },
  async ({ device }) =>
    onDevices(device, async (h) => {
      const s = await api(h, '/api/state');
      return `showing ${describeScene(s.scene)}\ndisplay ${JSON.stringify(s.display)}\nmotion ${JSON.stringify(s.motion)}`;
    }),
);

server.registerTool(
  'get_frame',
  {
    title: 'See the matrix',
    description: 'Return what the matrix is showing right now as ASCII art (one character per LED), so you can check your work.',
    inputSchema: { device: deviceArg },
  },
  async ({ device }) => onDevices(device, async (h) => '\n' + frameToAscii(await api(h, '/api/frame'))),
);

server.registerTool(
  'show_text',
  {
    title: 'Show text',
    description: 'Display text on the matrix. Long text scrolls automatically. Supports Spanish characters (á é í ó ú ñ ¿ ¡ ü), ° € ♥.',
    inputSchema: {
      text: z.string().min(1).max(240),
      color: colorArg('Text color'),
      background: colorArg('Background color'),
      speed: z.number().min(1).max(120).optional().describe('Scroll speed in pixels per second (default 20)'),
      scroll: z.enum(['auto', 'scroll', 'static']).optional().describe('auto scrolls only when the text does not fit'),
      size: z.number().int().min(0).max(8).optional().describe('Font scale; 0 = auto (largest that fits the height)'),
      rainbow: z.boolean().optional().describe('Animated rainbow colors per letter'),
      device: deviceArg,
    },
  },
  async ({ text, color, background, speed, scroll, size, rainbow, device }) =>
    onDevices(device, async (h) => {
      const body = { text, color, bg: background, speed, scroll, size, rainbow };
      const r = await api(h, '/api/text', body);
      return `showing ${describeScene(r.scene)}`;
    }),
);

server.registerTool(
  'notify',
  {
    title: 'Notify',
    description: 'Scroll a one-off message over whatever is showing, then return to the previous scene.',
    inputSchema: {
      text: z.string().min(1).max(240),
      color: colorArg('Text color'),
      repeat: z.number().int().min(1).max(10).optional().describe('How many times to scroll it (default 1)'),
      device: deviceArg,
    },
  },
  async ({ text, color, repeat, device }) =>
    onDevices(device, async (h) => {
      await api(h, '/api/notify', { text, color, repeat });
      return 'notification queued';
    }),
);

server.registerTool(
  'list_effects',
  { title: 'List effects', description: 'List the built-in animated effects with a short description.', inputSchema: { device: deviceArg } },
  async ({ device }) =>
    onDevices(device, async (h) => {
      const r = await api(h, '/api/effects');
      return '\n' + r.effects.map((e) => `- ${e.name}: ${e.description} (default color ${e.default_color})`).join('\n');
    }),
);

server.registerTool(
  'show_effect',
  {
    title: 'Show effect',
    description:
      `Run a built-in animation: ${EFFECTS.join(', ')}. ` +
      '"liquid" simulates liquid in a container that follows the board IMU (tilt) and sparkles when shaken; use `level` for the fill.',
    inputSchema: {
      name: z.enum(EFFECTS).describe('Effect name (see list_effects)'),
      speed: z.number().int().min(1).max(10).optional().describe('1-10, default 5'),
      color: colorArg('Main color (effects that support it)'),
      level: z.number().int().min(5).max(95).optional().describe('liquid only: fill percentage, default 50'),
      device: deviceArg,
    },
  },
  async ({ name, speed, color, level, device }) =>
    onDevices(device, async (h) => {
      const r = await api(h, '/api/effect', { name, speed, color, level });
      return `showing ${describeScene(r.scene)}`;
    }),
);

server.registerTool(
  'move_liquid',
  {
    title: 'Tilt or shake the liquid',
    description:
      'Virtual accelerometer for the liquid effect (and any motion-driven effect). Tilt sets gravity in matrix coordinates ' +
      '(+x right, +y down; (0,1) = upright, (1,0) = pour to the right). Shake 0-1 adds energy: brighter liquid, splashes and sparkles. ' +
      'A tilt overrides the real IMU for hold_ms.',
    inputSchema: {
      tilt_x: z.number().min(-2).max(2).optional(),
      tilt_y: z.number().min(-2).max(2).optional(),
      shake: z.number().min(0).max(1).optional(),
      hold_ms: z.number().int().min(100).max(600000).optional().describe('How long the tilt overrides the IMU (default 5000)'),
      device: deviceArg,
    },
  },
  async ({ tilt_x, tilt_y, shake, hold_ms, device }) =>
    onDevices(device, async (h) => {
      const r = await api(h, '/api/motion', { tilt_x, tilt_y, shake, hold_ms });
      return `gravity (${r.gx.toFixed(2)}, ${r.gy.toFixed(2)}), shake ${r.shake.toFixed(2)}, imu ${r.imu ? 'yes' : 'no'}`;
    }),
);

server.registerTool(
  'draw_pixels',
  {
    title: 'Draw pixel art',
    description:
      'Show a static image. Give one string per row, one character per LED, colored through the palette. ' +
      'Images smaller than the matrix are centered. Call get_state first to know the matrix size.',
    inputSchema: {
      rows: z.array(z.string()).min(1).max(64),
      palette: paletteArg,
      device: deviceArg,
    },
  },
  async ({ rows, palette, device }) => {
    const f = rowsToFrame(rows, palette);
    return onDevices(device, async (h) => {
      await api(h, '/api/pixels', f);
      return `image ${f.w}x${f.h} shown`;
    });
  },
);

server.registerTool(
  'show_animation',
  {
    title: 'Play an animation',
    description:
      'Upload and play a frame animation. Each frame is a list of row strings (same format as draw_pixels); all frames must have the same size.',
    inputSchema: {
      frames: z.array(z.array(z.string()).min(1).max(64)).min(1).max(200),
      palette: paletteArg,
      fps: z.number().int().min(1).max(60).optional().describe('Frames per second, default 8'),
      loop: z.boolean().optional().describe('Loop forever (default true) or stop on the last frame'),
      device: deviceArg,
    },
  },
  async ({ frames, palette, fps = 8, loop = true, device }) => {
    const encoded = frames.map((rows) => rowsToFrame(rows, palette));
    const { w, h } = encoded[0];
    if (encoded.some((f) => f.w !== w || f.h !== h)) throw new Error('all frames must have the same size');
    return onDevices(device, async (host) => {
      const info = await api(host, '/api/info');
      const perChunk = Math.max(1, Math.floor((info.anim_chunk_bytes || 6144) / (w * h * 3)));
      for (let i = 0; i < encoded.length; i += perChunk) {
        const part = encoded.slice(i, i + perChunk).map((f) => f.data);
        await api(host, '/api/anim', { w, h, fps, loop, frames: part, append: i > 0 });
      }
      return `animation ${encoded.length} frames ${w}x${h} @ ${fps} fps playing`;
    });
  },
);

server.registerTool(
  'set_display',
  {
    title: 'Configure the matrix',
    description:
      'Change the emulated LED matrix: size (fit=true sizes it to cover the whole screen with LEDs of fit_pitch pixels; ' +
      'explicit width/height turn fit off), color mode (rgb, mono = classic single-color on/off, gray = single color with dimming), ' +
      'mono color, LED shape and gap, brightness, unlit-LED dots and LCD backlight. Only the given fields change.',
    inputSchema: {
      fit: z.boolean().optional().describe('Size the matrix from the screen so it fills it edge to edge'),
      fit_pitch: z.number().int().min(4).max(40).optional().describe('LED size in screen pixels used by fit (default 10)'),
      width: z.number().int().min(4).max(64).optional(),
      height: z.number().int().min(4).max(64).optional(),
      color_mode: z.enum(['rgb', 'mono', 'gray']).optional(),
      mono_color: colorArg('LED color in mono/gray modes (e.g. #ff2800 red, #ffa000 amber, #20ff40 green)'),
      shape: z.enum(['round', 'square', 'rounded']).optional(),
      gap: z.number().int().min(0).max(60).optional().describe('Dark gap between LEDs, percent of pitch'),
      brightness: z.number().int().min(1).max(255).optional(),
      show_off: z.boolean().optional().describe('Draw unlit LEDs as dim dots'),
      backlight: z.number().int().min(5).max(255).optional(),
      device: deviceArg,
    },
  },
  async ({ device, ...patch }) =>
    onDevices(device, async (h) => {
      const r = await api(h, '/api/display', patch);
      const d = r.display;
      return `matrix ${d.width}x${d.height}${d.fit ? ' (fit to screen)' : ''} ${d.color_mode}${d.color_mode !== 'rgb' ? ' ' + d.mono_color : ''}, ${d.shape} LEDs, gap ${d.gap}%, brightness ${d.brightness}, pitch ${d.pitch}px`;
    }),
);

server.registerTool(
  'clear',
  { title: 'Clear', description: 'Turn every LED off.', inputSchema: { device: deviceArg } },
  async ({ device }) =>
    onDevices(device, async (h) => {
      await api(h, '/api/clear', {});
      return 'cleared';
    }),
);

await server.connect(new StdioServerTransport());
