# Base reutilizable para proyectos ESP32 con pantalla y web embebida

Plantilla de arranque (octubre de 2026) para cualquier dispositivo ESP32 con pantalla. Describe
la funcionalidad base que conviene montar el primer día: portal cautivo de configuración WiFi,
web servida por el chip, API JSON, ajustes persistentes, hora por NTP, mDNS, consola serie,
generación de assets, landing con instalador web en GitHub Pages y CI. Toolchain:
`arduino-cli`, core `esp32:esp32` 3.2.0, `ArduinoJson` 7, `GFX Library for Arduino` 1.6.4,
Node ≥ 18 para los generadores. Las fichas de las placas y las trampas de hardware están en
`waveshare-esp32-lcd-notas.md`.

---

## 1. Estructura del repo

```
firmware/<app>/        sketch Arduino
  config.h             constantes del proyecto + selección de placa por target del compilador
  boards/<placa>.h     un header de pinout por placa, con guardas HAS_* (LED, táctil, zumbador, batería)
  hw.h/.cpp            todo lo que depende de la placa (LED, zumbador, batería, latch de alimentación)
  net.h/.cpp           WiFi, AP cautivo, servidor HTTP, mDNS, NTP, ajustes en Preferences
  ui.h/.cpp            pantalla (Arduino_Canvas), layout derivado de W/H
  touch.h/.cpp         táctil (si la placa lo tiene)
  <dominio>.h/.cpp     lógica de la app
  web_assets.h         GENERADO: web/index.html y datos compartidos en gzip PROGMEM
  assets.h             GENERADO: gráficos u otros datos desde shared/
  fonts/               FreeSans de Adafruit vendorizadas (Arduino_GFX no las trae)
web/index.html         web embebida, vanilla JS sin dependencias, un solo fichero
shared/*.json          fuente única de datos compartidos firmware/web/landing
tools/                 gen_web.mjs, gen_assets.mjs, gen_og.mjs, check_consistency.mjs, screenshot.py
scripts/               build.sh, flash.sh, monitor.sh, check-manifest.py
docs/                  landing + manifest.json de esp-web-tools (los .bin los pone el CI)
mcp/                   servidor MCP opcional que envuelve la API HTTP
Makefile               atajos; el CI ejecuta exactamente estos targets
.github/workflows/ci.yml
AGENTS.md / CONTRIBUTING.md / SECURITY.md / BOARD*.md / CHANGELOG.md
```

Reglas que han funcionado: código, comentarios, commits, UI y logs en inglés; documentos en el
idioma del proyecto; los ficheros generados se commitean (para compilar sin Node) pero llevan
cabecera de aviso y `make check` falla si están desactualizados.

### Selección de placa en `config.h`

```cpp
#include <sdkconfig.h>
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#include "boards/c6_lcd147.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "boards/s3_touch169.h"
#else
#error "Unsupported target: add a header under boards/ and select it here"
#endif
```

Cada header define pines, `LCD_NATIVE_W/H`, offsets, `LCD_CORNER_RADIUS` y los `HAS_*`. El
sketch nunca usa `#ifdef` de placa: todo pasa por `hw.h`/`touch.h` con stubs vacíos cuando el
periférico no existe. Así `make build BOARD=s3` solo cambia el FQBN.

---

## 2. Orden de arranque (`setup()`)

```cpp
Serial.begin(115200);
hwBegin();            // PRIMERO: en placas con batería sube SYS_EN para no apagarse
app.load();           // estado de la app desde Preferences
net.loadSettings();   // rotación, brillo, TZ, hostname... antes de la UI
ui.begin(net.rotation);
if (HAS_TOUCH) touch.begin(net.rotation);
ui.bootAnimation();   // bloqueante, corto; el WiFi arranca después para que no se solape
net.begin(&app);      // WiFi STA o AP cautivo + servidor HTTP
```

En `loop()`: `net.loop()` (DNS cautivo, HTTP, estado WiFi), `hwLoop()` (melodías no bloqueantes,
batería), entrada (botón, táctil, CLI serie), tick de simulación una vez por segundo, render a
~10 fps con `flush()` del canvas, y `if (net.restartRequested) ESP.restart()` al final para que
la respuesta HTTP salga antes de reiniciar.

---

## 3. Ajustes persistentes

`Preferences` con un namespace por módulo (`"net"`, `"app"`). Claves cortas (≤ 15 chars):

| Clave | Tipo | Uso |
|---|---|---|
| `ssid`, `pass` | String | credenciales WiFi; la API **nunca** devuelve `pass` |
| `tz` | String | zona horaria POSIX (`CST6`, `CET-1CEST,M3.5.0,M10.5.0/3`) |
| `host` | String | hostname mDNS, 1–24 chars `[a-z0-9-]`, se aplica al reiniciar |
| `bl` | uchar | brillo 5–255 |
| `ndim` | bool | atenuación nocturna 22:00–07:00 |
| `rot` | uchar | rotación 0–3; cambiarla reinicia (el framebuffer se reserva al arrancar) |

El estado de la app se guarda cada `SAVE_INTERVAL_MS` (60 s) y de inmediato tras cada acción
del usuario, junto con el epoch si hay hora NTP. Si la app simula algo con el tiempo, al
arrancar y recibir la hora se hace `catchUp(epoch)` para cubrir lo que la placa estuvo apagada.

---

## 4. WiFi: STA con caída a AP cautivo

Máquina de estados en `Net::loop()`:

1. Con credenciales guardadas → `WIFI_STA`, `WiFi.setHostname(host)`, `WiFi.begin()`.
2. Si en **30 s** no conecta → abre el AP **sin cerrar STA** (`WIFI_AP_STA`), marca
   `staFailed = true` y sigue intentando `WiFi.reconnect()` cada 15 s en segundo plano. Si al
   final conecta, el portal deja de redirigir solo.
3. Sin credenciales → AP directamente.

```cpp
static String apSsid() {
  uint64_t mac = ESP.getEfuseMac();   // eFuse, no WiFi.macAddress(): esa cambia entre AP y STA
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%02X%02X", AP_SSID_PREFIX, (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return String(buf);
}

void Net::startAp() {
  apMode = true;
  WiFi.mode(WIFI_AP_STA);          // STA viva para poder escanear redes desde el portal
  WiFi.softAP(apSsid().c_str());   // red abierta, sin contraseña
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());   // todo dominio resuelve a 192.168.4.1
  WiFi.scanNetworks(true);         // escaneo asíncrono: el portal ya tiene lista al abrirse
}
```

Al conectar (`onConnected`): `MDNS.begin(host)` + `addService("http","tcp",80)` +
`addServiceTxt`, `configTzTime(tz, "pool.ntp.org")` una sola vez, log del evento.

---

## 5. Portal cautivo

Tres piezas, todas en `setupRoutes()`:

**a) Sondas de conectividad de cada sistema operativo.** Si estamos en modo setup, 302 a la
página; si no, 204 para que el SO no muestre nada.

```cpp
const char *probes[] = {"/hotspot-detect.html", "/library/test/success.html",   // Apple
                        "/generate_204", "/gen_204",                            // Android
                        "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", // Windows
                        "/success.txt", "/canonical.html"};                     // Firefox
for (const char *p : probes) server.on(p, HTTP_ANY, []() { inSetupMode() ? captiveRedirect() : server.send(204); });

static void captiveRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/setup", true);
  server.send(302, "text/plain", "");
}
```

**b) `onNotFound`:** `OPTIONS` → 204 (CORS); en modo setup cualquier ruta que no sea `/api/*`
redirige al portal; si no, 404 JSON.

**c) `GET /` y `GET /setup`:** en modo setup devuelven la página generada en el chip (un `String`
con `F()`), fuera de él `/` sirve la web embebida.

### Página de setup (sin JS externo, cabe en 2 KB)

- Título con el nombre del producto y una frase de por qué estás aquí; si `staFailed`, aviso
  "no pude unirme a X".
- `<select name="ssid">` con las redes de `WiFi.scanComplete()` (máx. 20, con RSSI), opción
  "Other network…" que muestra un `<input name="network">` para SSID ocultos.
- `<input name="pass" type="password" autocomplete="new-password">` y botón "Save and reboot".
- Enlace "rescan" que vuelve a `/setup`; al salir se hace `scanDelete()` + `scanNetworks(true)`
  para que la siguiente carga tenga lista fresca.
- **Escapar siempre el SSID** (`<`, `&`, `"`) al insertarlo en HTML: un SSID puede ser cualquier
  cosa, incluso `localhost` o `<script>`. Nada de asumir que un valor raro es autocompletado.

### `POST /api/wifi`

Acepta formulario clásico (`ssid`/`network` + `pass`) **y** JSON `{"ssid","pass"}`, para que
sirva tanto al portal como a la web y al CLI. Valida 1–32 chars, guarda, responde una página
"Saved, rebooting, open http://<host>.local" y pone `restartRequested = true` (el reinicio
ocurre en el loop, tras enviar la respuesta).

Alternativas por serie: `wifi <ssid> <pass>` y `forget` (borra credenciales y vuelve al AP).

---

## 6. Web embebida

- Un solo `web/index.html` (vanilla JS, CSS inline). `tools/gen_web.mjs` lo comprime con gzip
  nivel 9 y lo vuelca como `static const uint8_t WEB_INDEX_GZ[] PROGMEM` en `web_assets.h`.
- Servir con `server.send_P(200, "text/html", (const char*)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN)` y
  cabeceras `Content-Encoding: gzip` + `Cache-Control: no-cache` (datos estáticos grandes:
  `max-age=86400`).
- `server.enableCORS(true)` y `OPTIONS` → 204 para poder desarrollar la web desde el disco:
  `web/index.html?host=<IP-de-la-placa>` apunta el `fetch` a la placa sin reflashear.
- `gen_web.mjs --check` (en `make check`) **descomprime** lo commiteado y lo compara con las
  fuentes, no compara bytes gzip: la zlib de macOS y la de Linux generan streams distintos.
- Si cambias la web sin regenerar, el navegador seguirá viendo la versión vieja. `make build`
  siempre regenera.
- Secciones típicas de la web: estado en vivo (poll de `/api/state` cada 1–2 s), acciones con
  toast de resultado, ajustes (nombre, TZ con selector de ciudades + campo POSIX, brillo,
  orientación, hostname, modo noche), WiFi (formulario que llama a `/api/wifi`) e info del
  dispositivo (placa, firmware, IP, RSSI, mDNS, hora local, heap).

---

## 7. API JSON: convenciones

- `WebServer` del core (síncrono) basta para una web ligera; `ArduinoJson` 7 con `JsonDocument`.
- Cuerpo: `server.arg("plain")` → `deserializeJson`. Helper `readBody(doc)`; error →
  `sendError("expected JSON {\"name\": ...}")` con 400.
- Respuestas siempre JSON con `ok: true/false` y, en acciones, qué pasó (`applied`, `queued`,
  `position`, o `error` con 4xx: 400 datos malos, 404, 429 ocupado).
- **Validar contra el rango real** todo índice o tipo que llega por HTTP (acción, orientación,
  brillo 5–255, hostname con regex). Nunca devolver la contraseña WiFi.
- Rutas base:

| Método y ruta | Qué hace |
|---|---|
| `GET /api/state` | estado de la app + flags de dispositivo |
| `GET /api/events` | últimos N eventos (ring buffer en RAM) |
| `GET /api/info` | placa, firmware, IP, RSSI, heap, TZ, hora local, brillo, batería |
| `POST /api/action` | `{"type": "..."}` acciones de la app |
| `POST /api/settings` | `{tz, brightness, orientation, hostname, nightDim}`; devuelve `rebooting` |
| `POST /api/wifi` | credenciales (JSON o form) |

Si la app tiene animaciones que no deben pisarse, **encola** las acciones (ring buffer de 4) y
responde `queued`/`position`; con la cola llena, 429 con `busyMs`.

---

## 8. Hora, zona horaria y modo noche

- `configTzTime(tzPosix, "pool.ntp.org")` al conectar; `epoch()` devuelve 0 hasta sincronizar.
- La TZ se guarda como string POSIX. En la web, un selector de ciudades mapea a POSIX y deja un
  campo libre.
- Modo noche (22:00–07:00 hora local) limita el duty del backlight; hazlo desactivable
  (`nightDim`) y **documenta la trampa**: con una TZ por defecto equivocada "el brillo no
  funciona" en pleno día.

---

## 9. mDNS y descubrimiento entre placas

- `MDNS.begin(host)`; hostname configurable por API/CLI y guardado en Preferences (se aplica al
  siguiente arranque porque `WiFi.setHostname` va antes de `begin`).
- Anuncia un servicio propio (`_miapp._tcp`) con TXT para que otras placas o un script las
  encuentren. Para buscar sin bloquear el loop usa la API IDF
  `mdns_query_async_new("_miapp","_tcp", …)` y `mdns_query_async_get_results(search, 0, &res, …)`;
  `MDNS.queryService` bloquea ~2 s.
- Placa a placa con `HTTPClient`: timeout ≥ 1,5 s (una C6 tarda ~300 ms en servir un JSON
  pequeño); filtra tu propia IP de los resultados.

---

## 10. Consola serie (CLI)

Lectura línea a línea en `loop()` a 115200, comando + resto. Conjunto base que ahorra horas:

| Comando | Para qué |
|---|---|
| `help`, `status` | lista y volcado del estado (app + WiFi + placa) |
| `wifi <ssid> <pass>`, `forget`, `tz <posix>`, `host <name>` | configuración sin web |
| `bl [0-255]` | leer/forzar brillo (`ledcRead` devuelve el duty real) |
| `shot` | volcar el framebuffer en base64 → `tools/screenshot.py PORT out.png` |
| `press`, `hold` | simular el botón físico |
| `tp`, `tcal` | debug del táctil y calibración con cruces |
| `gpio <n>` | leer un pin (bring-up de placa nueva) |
| `corners` | medir el radio de las esquinas del cristal |
| `boot`, `reboot` | repetir la animación de arranque; reiniciar |

Sin pyserial: `stty -f PORT 115200 raw -echo; cat PORT & printf 'status\n' > PORT`.
Mata el `cat` antes de usar `screenshot.py`, que abre el puerto él mismo.

---

## 11. Entrada: botón y táctil

- Un botón: clic corto, doble clic (350 ms), pulsación larga (600 ms) y pulsación de reset
  (6 s). Los menús se cierran por timeout (6 s) y la página de info a los 10 s.
- **Nunca compares `now - marca` sin signo** si `marca` puede fijarse después de muestrear
  `now` en la misma vuelta del loop: usa `(int32_t)(millis() - marca)`. Un menú se cerraba al
  instante por ese desbordamiento.
- Con táctil, el menú y el hit-test comparten la misma geometría (`menuHit()`): el layout se
  deriva de W/H y de `edgeInset()` en `Ui::begin`, nunca números a mano.

---

## 12. Makefile y CI

Targets que han demostrado valer la pena (el CI llama exactamente a estos):

```
make deps        core + librerías con versión fijada
make gen         regenera los ficheros embebidos
make build       gen + compile (BOARD=c6|s3)      make build-all
make flash       build + upload (PORT=... si hay dos placas)
make monitor     consola serie
make check       manifest + versión + generados al día + coherencia firmware/web/landing/MCP
make shot        captura de pantalla real en PNG
make og          tarjeta social de la landing en _site/og.png
make site/serve  arma el instalador web en _site/ y lo sirve en localhost (Web Serial funciona ahí)
make bump VERSION=X.Y.Z   sube la versión en config.h y en el manifest a la vez
```

CI (`ci.yml`), disparado por push a `main`, tags `v*`, PR y `workflow_dispatch`:

- Job `build`, matriz por placa (`fail-fast: false`): `setup-node`, `setup-arduino-cli`, cache
  de `~/.arduino15` con clave `hashFiles('Makefile')` (ahí están fijadas las versiones),
  `make deps` → `make check` → `make build` → `make bootapp0`, renombrado de binarios a
  `<parte>-<placa>.bin` y `upload-artifact` como `firmware-<placa>`.
- Job `release` (solo tag): `download-artifact` con `merge-multiple` y
  `gh release create --generate-notes` con los `.bin` y `merged.bin` de cada placa.
- Job `pages` (tag o manual): ver sección 14.

**Compilaciones en paralelo con el mismo sketch colisionan** en la caché de arduino-cli
("file in wrong format"): usa `--build-path` distintos o compila en serie.

---

## 13. Instalador web (esp-web-tools)

`docs/manifest.json` con un build por chip (`ESP32-C6`, `ESP32-S3`); el navegador elige según
la placa conectada. Partes y offsets, iguales en C6 y S3:

| Parte | Offset |
|---|---|
| bootloader | `0x0` |
| partitions | `0x8000` |
| boot_app0 | `0xE000` |
| app | `0x10000` |

```json
{
  "name": "<App>", "version": "0.1.0", "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-C6", "parts": [
      { "path": "bootloader-c6.bin", "offset": 0 },
      { "path": "partitions-c6.bin", "offset": 32768 },
      { "path": "boot_app0-c6.bin",  "offset": 57344 },
      { "path": "<app>-c6.bin",      "offset": 65536 } ] },
    { "chipFamily": "ESP32-S3", "parts": [ "...igual con sufijo -s3..." ] }
  ]
}
```

`boot_app0.bin` no lo produce el compilador: se copia del core instalado. Un script
(`check-manifest.py`) comprueba offsets y que la versión del manifest sea la de `FW_VERSION`;
si se desvían, esptool escribe en el sitio equivocado sin ningún error.

---

## 14. Landing en GitHub Pages

Un único `docs/index.html` sin build step, vanilla JS y CSS inline. Los binarios **nunca viven
en el repo** (ignorados a propósito): los pone el CI al desplegar, así la página publicada no
puede quedarse detrás del firmware.

### Qué lleva la página

- `<head>`: `meta description` bilingüe en una línea (los buscadores no siguen el toggle de
  idioma), Open Graph y Twitter card (`og:type`, `og:site_name`, `og:title`, `og:description`,
  `og:url`, `og:image` 1200×630 con `width`, `height` y `alt`,
  `twitter:card=summary_large_image`), favicon SVG inline en `data:`,
  `meta name="color-scheme" content="dark light"` con tokens CSS en `:root` y override en
  `@media (prefers-color-scheme: light)`, fuentes de Google con `preconnect`.
- esp-web-tools desde CDN:
  `<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module">`
  y `<esp-web-install-button manifest="manifest.json">` con un botón propio en `slot="activate"`
  y un `slot="unsupported"` vacío. Si `!('serial' in navigator)` se añade la clase `unsupported`
  al body: CSS oculta el botón y muestra "abre esto en Chrome o Edge de escritorio". Web Serial
  solo funciona en HTTPS o en `localhost`.
- La versión se lee de `manifest.json` con `fetch` y se pinta junto al botón, así nunca
  desentona con lo que instala.
- **Bilingüe sin framework**: cada texto se duplica en `<span lang="es">` / `<span lang="en">`
  y CSS oculta el idioma que no toca según `html[lang]`:
  `html[lang=es] [lang=en], html[lang=en] [lang=es]{display:none}`. Un botón alterna
  `document.documentElement.lang` y lo guarda en `localStorage` dentro de `try/catch` (en
  navegación privada puede lanzar). Variante `.inl` para spans dentro de botones.
- **Demo en vivo** en un `<canvas>` con `image-rendering: pixelated`, dibujada con los mismos
  datos de `shared/*.json` que usa el firmware (se copian a `_site/`), con tiempo acelerado y
  los mismos gestos de botón que la placa (clic, pulsación larga de 600 ms, reset a 6 s). La
  elección del visitante (tema, variante) también va a `localStorage`.
- Secciones, cada una con un "eyebrow" corto y un `id` para la nav: hero con botón de instalar
  y cifras, panel web, catálogo, cómo se usa, controles físicos, hardware soportado (tabla por
  placa), instalar (web + alternativa con `arduino-cli`), por dentro (arquitectura, API, cómo
  contribuir), autor con enlace de follow, footer con licencia.
- Los datos que vienen de JSON se insertan con `textContent`/`createElement`, nunca con
  `innerHTML`.
- Si hay un check de coherencia (sección 15), la landing entra: sus arrays de menú y de
  acciones se comparan con el firmware.

### Tarjeta social

`tools/gen_og.mjs` renderiza `og.png` (1200×630) en Node puro: pinta píxeles en un
`Uint8Array`, escribe el PNG con `zlib.deflateSync` y toma título, versión y gráficos de
`manifest.json` y `shared/`. Se genera en el deploy (`make og` en local); no se commitea.

### Deploy

Job `pages` del CI, con `needs: [build]`, condicionado a
`startsWith(github.ref, 'refs/tags/v') || github.event_name == 'workflow_dispatch'`,
`permissions: pages: write, id-token: write` y `environment: github-pages` (la URL sale en
`steps.deploy.outputs.page_url`):

1. `download-artifact` con `pattern: firmware-*` y `merge-multiple: true` a `dist/`.
2. Arma `_site/`: `docs/index.html`, `docs/manifest.json`, `shared/*.json`, `og.png` y los
   `.bin` copiados con los nombres `<parte>-<placa>.bin` que espera el manifest.
3. `actions/upload-pages-artifact@v3` con `path: _site` y `actions/deploy-pages@v4`.

En Settings → Pages la fuente es "GitHub Actions", no una rama. `make site` reproduce el mismo
`_site/` en local y `make serve` lo sirve en `http://localhost:8000` para probar el instalador
antes de etiquetar. Flujo de publicación: `make bump VERSION=X.Y.Z`, commit, `git tag vX.Y.Z`,
`git push --tags`; el CI compila, publica la release y despliega la página.

---

## 15. Assets compartidos y coherencia

- Una fuente única (`shared/*.json`) para todo lo que la web, la landing y el firmware deban
  ver igual (gráficos, listas, enums). Los generadores escriben los `.h`.
- `tools/check_consistency.mjs` (sin dependencias) parsea el enum del firmware, los arrays de
  la web/landing y el enum del MCP, y falla si difieren en cantidad u orden. Ignora entradas
  bajo `#if` de placa. Evita que un cliente ofrezca acciones que el firmware no acepta.

---

## 16. MCP opcional

Si quieres que una IA (Claude Code, Claude Desktop o cualquier cliente MCP) opere el
dispositivo, un `mcp/server.mjs` (Node, stdio, SDK oficial `@modelcontextprotocol/sdk`)
envuelve la API HTTP como herramientas: `get_state`, `get_info`, una herramienta por acción
(`POST /api/action`), `set_settings`. Patrón:

- Un helper `api(path, body)` con `fetch` al host de la placa (variable de entorno o argumento;
  por defecto `http://<host>.local`).
- Cada herramienta devuelve texto corto y legible, no el JSON crudo, y traduce los códigos de
  error (`429` → "ocupado, reintenta en N s"; `queued` → "en cola, posición N").
- Enums de la API (acciones, opciones) duplicados en el servidor y vigilados por
  `check_consistency.mjs` para que no se desvíen del firmware.
- Se instala apuntando el cliente a `node mcp/server.mjs` desde el repo; no hace falta
  publicarlo en npm.

---

## 17. Seguridad mínima

- La API no devuelve credenciales. El AP de setup es abierto pero solo sirve el portal y la
  API local; no exponer la placa a Internet.
- Todo lo que viene de la red se escapa al insertarlo en HTML (`textContent` en el cliente,
  `htmlEscape` en el chip) y se valida contra rangos reales.
- Documentarlo en `SECURITY.md` aunque sea corto.

---

## 18. Checklist para un proyecto nuevo

1. Copiar `boards/`, `config.h`, `hw.*`, `net.*`, `touch.*`, `tools/gen_web.mjs`,
   `tools/gen_og.mjs`, `tools/screenshot.py`, `Makefile`, `ci.yml`, `scripts/check-manifest.py`
   y el esqueleto de `docs/index.html` + `docs/manifest.json`.
2. Cambiar `AP_SSID_PREFIX`, `MDNS_HOST`, `DEFAULT_TZ`, `FW_VERSION`, nombre de servicio mDNS,
   `name` del manifest, metas OG y URL de Pages.
3. Escribir el módulo de dominio de la app y las rutas `/api/*` que correspondan.
4. Escribir `web/index.html` nuevo, mantener el patrón `?host=` y el toast de acciones.
5. `make deps && make build && make flash`, conectarse al AP, configurar WiFi, abrir
   `http://<host>.local`.
6. Activar Pages con fuente "GitHub Actions", probar `make serve`, etiquetar `v0.1.0`.
7. Añadir `shot` y verificar cada cambio de layout con una captura antes de darlo por bueno.
