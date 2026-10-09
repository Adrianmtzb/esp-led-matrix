# LED Matrix

Emula una matriz de LEDs (RGB o monocolor) en las placas Waveshare ESP32 con LCD. Muestra
texto, efectos animados, imágenes y animaciones por frames, y se controla desde la web
embebida, la API HTTP, un servidor MCP o la consola serie.

| Placa | Hostname por defecto | Matriz por defecto (fit, LED de 10 px) | Extras |
|---|---|---|---|
| ESP32-C6-LCD-1.47 | `ledmatrix-c6.local` | 32×17 | LED RGB que imita el color medio |
| ESP32-S3-Touch-LCD-1.69 | `ledmatrix-s3.local` | 28×24 | IMU QMI8658 para el líquido, batería |

Fichas de las placas y trampas de hardware: `waveshare-esp32-lcd-notas.md`. Base de la
arquitectura (WiFi, portal, API): `esp32-base-proyecto.md`.

## Puesta en marcha

```bash
make deps                                         # core esp32 3.2.0 + librerías
make flash BOARD=s3 PORT=/dev/cu.usbmodemXXXX     # o BOARD=c6
make mcp-install                                  # dependencias del servidor MCP
```

WiFi: si la placa ya tiene credenciales en NVS (namespace `net`), se conecta directamente.
Si no las tiene, o no logra unirse en 30 s, abre el AP abierto `LedMatrix-XXXX` con portal
cautivo. También se pueden cambiar desde la consola serie: `wifi <ssid> <pass>` / `forget`.
Al conectar, la matriz anuncia `hostname.local` y la IP con un scroll.

Abrir `http://ledmatrix-s3.local` (o la IP). Para desarrollar la web sin reflashear:
`web/index.html?host=<IP>`.

## Qué puede mostrar

- **Texto**: scroll automático si no cabe, tamaño automático según la altura, arcoíris,
  colores de texto y fondo. Admite á é í ó ú ñ Ñ ü ¿ ¡ ° € ♥. *Notify* muestra un mensaje
  puntual y luego vuelve a la escena anterior.
- **Efectos**: rainbow, plasma, fire, rain, life, twinkle, stars, ripple, bounce, clock
  (NTP), equalizer y **liquid**.
- **Liquid**: un contenedor de líquido con autómata celular. En la S3 la gravedad sale del
  IMU (inclinar mueve el líquido); al agitar, el líquido se vuelve más brillante, salpica
  contra la gravedad y suelta chispas. En la C6 (sin IMU) hay un vaivén suave y el botón
  BOOT hace de "agitar". En cualquier placa se puede inclinar o agitar desde la web
  (joystick + botón *Shake*), el MCP (`move_liquid`) o `POST /api/motion`.
- **Dibujo y animación**: editor de píxeles con frames, importación de imágenes y GIF
  (escalados a la matriz), envío como imagen o animación.

## Ajustes de display

- **Fit to screen** (por defecto): el tamaño de la matriz se calcula a partir del tamaño de
  LED (`fit_pitch`) para cubrir toda la pantalla. Fijar `width`/`height` a mano lo desactiva;
  aun así la rejilla se reparte por toda la pantalla.
- Modo de color: `rgb`, `mono` (encendido/apagado de un color, estilo MAX7219) o `gray`
  (un color con intensidad). Color mono, forma (`round`, `rounded`, `square`), separación,
  brillo, LEDs apagados visibles, backlight y rotación (al cambiarla, la placa reinicia).

En la S3 la rejilla llega hasta el cristal redondeado, así que el cristal tapa los LEDs de
las esquinas.

## Botón BOOT

- Pulsación corta: siguiente efecto (en *liquid*, agita).
- Pulsación larga: muestra hostname e IP.

## API HTTP

Cuerpos en JSON (`Content-Type: application/json`). Todas las respuestas llevan `ok`.

| Método y ruta | Qué hace |
|---|---|
| `GET /api/state` | escena actual, ajustes de display y lecturas de movimiento |
| `GET /api/info` | placa, firmware, red, heap, batería, efectos, límites de animación |
| `GET /api/frame` | lo que se ve ahora: `{w, h, data}` con RGB en base64 |
| `GET /api/effects` | efectos con descripción y color por defecto |
| `POST /api/text` | `{text, color, bg, speed, scroll: auto/scroll/static, size, rainbow}` |
| `POST /api/notify` | `{text, color, repeat}` |
| `POST /api/effect` | `{name, speed 1-10, color, level 5-95}` |
| `POST /api/scene` | igual que los anteriores con `mode`: text, effect, pixels, anim |
| `POST /api/pixels` | `{w, h, data}` (hex `rrggbb` por píxel o base64 de RGB), `{set: [[x, y, "#rrggbb"], ...]}` o `{fill}` |
| `POST /api/anim` | `{w, h, fps, loop, frames: [...], append}`; trocear según `anim_chunk_bytes` de `/api/info` |
| `POST /api/clear` | apaga todos los LEDs |
| `POST /api/display` | `{fit, fit_pitch, width, height, color_mode, mono_color, shape, gap, brightness, show_off, backlight, rotation}` |
| `POST /api/settings` | `{hostname, tz}` (cambiar el hostname reinicia) |
| `GET/POST /api/motion` | `{tilt_x, tilt_y, hold_ms, shake}`: acelerómetro virtual |
| `POST /api/wifi` | `{ssid, pass}` o formulario del portal |
| `POST /api/reboot` | reinicia |

La escena (texto o efecto) y las imágenes de hasta 3 KB sobreviven a un reinicio. Las
animaciones viven solo en RAM.

## MCP

`.mcp.json` registra el servidor para Claude Code en este repo. Para otro cliente:

```bash
LEDMATRIX_HOSTS=ledmatrix-c6.local,ledmatrix-s3.local node mcp/server.mjs
```

Herramientas: `list_devices`, `get_state`, `get_frame` (ASCII de lo que se ve, para que la IA
compruebe su trabajo), `show_text`, `notify`, `list_effects`, `show_effect`, `move_liquid`,
`draw_pixels` y `show_animation` (filas de caracteres + paleta), `set_display` y `clear`.
Cada herramienta acepta `device`: hostname, nombre corto (`s3`, `c6`), IP o `all`.

## Consola serie (115200)

`help`, `status`, `text <msg>`, `notify <msg>`, `effect <name>`, `effects`, `next`,
`json <escena>`, `clear`, `size <w> <h>`, `bri`, `bl`, `mode rgb|mono|gray`, `imu`, `shake`,
`tilt <gx> <gy>`, `wifi <ssid> <pass>`, `forget`, `host <name>`, `tz <posix>`, `shot`, `reboot`.

`make shot BOARD=s3 PORT=...` guarda una captura PNG real del LCD (`tools/screenshot.py`).

## Landing e instalador web (GitHub Pages)

`docs/index.html` es la landing: bilingüe (es/en), con el botón de instalación de
esp-web-tools y una demo en vivo que porta los efectos del firmware a JS. `docs/manifest.json`
describe los binarios por chip. Los `.bin` nunca se commitean: los pone el CI al desplegar.

```bash
make site       # compila ambas placas y arma _site/ (lo mismo que despliega el CI)
make serve      # http://localhost:8000; Web Serial funciona en localhost
make og         # solo la tarjeta social _site/og.png
```

Publicar una versión:

```bash
make bump VERSION=0.2.0       # config.h y manifest a la vez
git commit -am "Release 0.2.0" && git tag v0.2.0 && git push && git push --tags
```

El CI (`.github/workflows/ci.yml`) compila las dos placas, crea la release con los `.bin` y
despliega la página. La primera vez hay que activar Pages en *Settings → Pages* con fuente
*GitHub Actions*. El job de Pages también se puede lanzar a mano (`workflow_dispatch`).

## Coherencia

`make check` (también en el CI) falla si:

- `firmware/ledmatrix/font5x7.h` no coincide con `shared/font5x7.json` (la fuente se edita en
  el JSON y `make gen` regenera el header).
- `web_assets.h` no coincide con `web/index.html`.
- Los offsets o la versión de `docs/manifest.json` no cuadran con `FW_VERSION`.
- La lista de efectos difiere entre `effects.cpp`, `shared/effects.json`, el enum del MCP y la
  demo de la landing.

## Estructura

```
firmware/ledmatrix/   sketch: display (render de LEDs), matrix (framebuffer + fuente),
                      scene (texto/efectos/imagen/animación), effects, motion (IMU),
                      net (WiFi, portal, API), settings, hw, boards/
web/index.html        web embebida; `make gen` genera web_assets.h (gzip)
shared/*.json         fuente única: efectos y fuente 5x7 (firmware, landing, og.png)
docs/                 landing + manifest.json de esp-web-tools
mcp/server.mjs        servidor MCP sobre la API HTTP
tools/                gen_web.mjs, gen_assets.mjs, gen_og.mjs, check_consistency.mjs, screenshot.py
scripts/              check-manifest.py
.github/workflows/    ci.yml (build, release, pages)
```
