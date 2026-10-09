# Placas Waveshare ESP32 con LCD: specs, pinout y trampas resueltas

Notas portables sacadas de ESPgotchi (octubre de 2026). Todo lo de aquí está verificado en
hardware real salvo donde se indica. Toolchain: `arduino-cli` con core `esp32:esp32` 3.2.0 y
`GFX Library for Arduino` (Arduino_GFX) 1.6.4.

---

## 1. Waveshare ESP32-C6-LCD-1.47

- Wiki: https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47
- Esquemático: https://files.waveshare.com/wiki/ESP32-C6-LCD-1.47/ESP32-C6-LCD-1.47_schemetics.pdf

### MCU
- ESP32-C6FH4: RISC-V a 160 MHz, 4 MB de flash integrada, ~320 KB de SRAM, **sin PSRAM**.
- WiFi 6 (2,4 GHz), BLE 5, 802.15.4.
- USB-C al USB-Serial-JTAG nativo. En macOS aparece como `/dev/cu.usbmodem*`.

### Pantalla
- 1,47" IPS, ST7789, **172×320**, SPI a 80 MHz. Sin táctil. Cristal cuadrado.
- **Offset de columna 34** (la RAM del controlador es de 240 de ancho) y **INVON** (inversión
  de color). Sin el offset la imagen sale desplazada; sin inversión los colores salen al revés.
- Backlight por MOSFET SI2302 en GPIO 22, activo alto, admite PWM.

| Señal | GPIO |
|---|---|
| LCD MOSI | 6 |
| LCD SCLK | 7 |
| LCD CS | 14 |
| LCD DC | 15 |
| LCD RST | 21 |
| LCD BL | 22 |
| microSD CS / MISO | 4 / 5 (SPI compartido con la LCD) |
| LED RGB WS2812B | 8 (`rgbLedWrite(8, r, g, b)`) |
| Botón BOOT | 9 (activo bajo, pull-up 10K en placa; único botón programable) |
| GPIO libres | 0, 1, 2, 3, 18, 19, 20, 23 + UART0 |

### Compilación
- FQBN: `esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M` (2 MB para la app).
- Setup Arduino_GFX:

```cpp
Arduino_DataBus *bus = new Arduino_ESP32SPI(15 /*DC*/, 14 /*CS*/, 7 /*SCK*/, 6 /*MOSI*/, GFX_NOT_DEFINED, FSPI);
Arduino_GFX *panel = new Arduino_ST7789(bus, 21 /*RST*/, 1 /*rot*/, true /*ips*/, 172, 320, 34, 0, 34, 0);
ledcAttachChannel(22, 5000, 8, 0);  // backlight en canal 0 (ver trampa LEDC)
ledcWrite(22, 200);
```

---

## 2. Waveshare ESP32-S3-Touch-LCD-1.69

- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69
- Pines: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69
- Esquemático: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69/ESP32-S3-Touch-LCD-1.69-Sch.pdf

### MCU
- ESP32-S3R8: Xtensa LX7 doble núcleo a 240 MHz, 16 MB de flash, **8 MB PSRAM octal** (`PSRAM=opi`).
- WiFi 2,4 GHz y BLE 5. USB-C al USB-Serial-JTAG nativo (`/dev/cu.usbmodem*`).

### Pantalla
- 1,69" IPS, ST7789V2, **240×280 con esquinas redondeadas**, SPI de 4 hilos a 80 MHz.
- RAM del controlador 240×320: **offset de 20 filas, 0 columnas, igual en las cuatro
  rotaciones** (0 y 40 se probaron en las rotaciones espejo y las dos rompen la imagen). INVON.
- **Las esquinas del cristal tienen radio ~48 px.** Se midió pintando marcas a distintas
  distancias de la esquina: la de 8 px queda oculta, la de 16 se ve. Cualquier UI tiene que
  apartar de las esquinas la barra superior, los menús y el texto. Útil tener una función
  `edgeInset(dist)` que devuelva cuántos píxeles se come la esquina a esa distancia del borde.
- Backlight en GPIO 15 con PWM.

| Señal | GPIO |
|---|---|
| LCD MOSI ("DIN") | 7 |
| LCD CLK | 6 |
| LCD CS | 5 |
| LCD DC | 4 |
| LCD RST | 8 |
| LCD BL | 15 |

### I2C compartido (SDA 11, SCL 10, 400 kHz)
| Dispositivo | Dirección | Extra |
|---|---|---|
| Táctil CST816T (chip id 0xB5) | 0x15 | RST 13, INT 14 |
| IMU QMI8658C | 0x6B | INT 38 |
| RTC PCF85063 | 0x51 | INT 39 (41 en la revisión antigua) |

### Alimentación y otros
| Periférico | GPIO | Notas |
|---|---|---|
| Zumbador | 42 (33 en la revisión antigua) | PWM con `ledcWriteTone` |
| Batería ADC | 1 | `VBAT = 3 × VADC`. Cargador ETA6098. **No hay línea de VBUS ni de estado del cargador** hacia el ESP32 |
| SYS_EN | 41 (35 antigua) | mantener en HIGH para seguir alimentado desde batería; LOW apaga |
| SYS_OUT | 40 (36 antigua) | botón PWR: **HIGH en reposo, LOW mientras se pulsa** |
| Botón BOOT | 0 | strapping; usable como botón tras el arranque |
| Header | 2, 3, 17, 18 + SDA/SCL (11/10) + TX/RX (43/44) |

Hay dos revisiones de placa; los GPIO entre paréntesis son los de la primera tirada. No tiene
LED RGB.

### Compilación
- FQBN: `esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M,PSRAM=opi`.

```cpp
Arduino_DataBus *bus = new Arduino_ESP32SPI(4 /*DC*/, 5 /*CS*/, 6 /*SCK*/, 7 /*MOSI*/, GFX_NOT_DEFINED, FSPI);
Arduino_GFX *panel = new Arduino_ST7789(bus, 8 /*RST*/, 0 /*rot*/, true /*ips*/, 240, 280, 0, 20, 0, 20);
ledcAttachChannel(15, 5000, 8, 0);  // backlight en canal 0
```

---

## 3. Táctil CST816T: cómo quedó resuelto

### Inicialización
1. `Wire.begin(11, 10, 400000)`; INT como `INPUT_PULLUP`; RST en LOW 10 ms y HIGH 60 ms.
2. Leer el chip id en el registro `0xA7` (0xB4 = S, 0xB5 = T, 0xB6 = D). Si no responde, no hay táctil.
3. **Escribir `0xFF` en el registro `0xFE` (DisAutoSleep).** Sin esto el chip se duerme y el
   sondeo deja de devolver dedos a los pocos segundos.

### Lectura
- Sondeo cada 15 ms (sin usar INT). Registro `0x02`: 5 bytes → `fingers = b[0] & 0x0F`,
  `x = ((b[1] & 0x0F) << 8) | b[2]`, `y = ((b[3] & 0x0F) << 8) | b[4]`.
- **Una lectura I2C fallida o vacía devuelve `0xFF` en todo**: 15 dedos en (4095, 4095). Eso
  producía toques fantasma. Descarta `fingers == 0 || fingers > 2` y cualquier coordenada
  `>= 0x0FFF`.
- Debounce: el dedo tiene que verse en dos sondeos consecutivos antes de contar como "abajo".

### Calibración (lo más importante)
**El táctil no coincide 1:1 con el panel.** En X es 1:1 con un offset de −3 px; en Y el
controlador estira 1,2×: el crudo 0 cae en la fila 29 del panel y el 279 en la 261.

```cpp
// panel = raw * gain + offset, en coordenadas nativas (portrait 240x280)
#define TOUCH_X_GAIN 1.0f
#define TOUCH_X_OFFSET -3
#define TOUCH_Y_GAIN 0.83f
#define TOUCH_Y_OFFSET 29
```

Después se aplica `constrain` al tamaño del panel y la rotación a coordenadas de canvas:

```cpp
case 0: x = nx;               y = ny;               break;
case 1: x = ny;               y = NATIVE_W - 1 - nx; break;
case 2: x = NATIVE_W - 1 - nx; y = NATIVE_H - 1 - ny; break;
case 3: x = NATIVE_H - 1 - ny; y = nx;               break;
```

Método para medir en otra unidad: pintar cinco cruces (centro y cuatro esquinas), tocarlas y
registrar canvas vs crudo. **Medir en dos rotaciones opuestas (0 y 2) y promediar**: el dedo
cae unos 10 px abajo y a la derecha del centro de la cruz, y ese sesgo cambia de signo al girar
la placa; con una sola serie la calibración sale desviada ~20 px. No intentes "arreglar" un
toque desviado cambiando la rotación ni el offset de filas del panel.

### Gestos
Tap ≤ 400 ms y < 18 px de movimiento; pulsación larga ≥ 600 ms sin moverse; swipe ≥ 40 px en el
eje dominante. Los eventos de tap salen **al soltar**, así que para hit-tests sobre cosas que
se mueven conviene guardar la posición de cuando el dedo bajó.

---

## 4. Trampas de LEDC, zumbador y alimentación

- **Dos canales LEDC consecutivos comparten timer** (2n y 2n+1), y `ledcWriteTone` retunea ese
  timer. Con `ledcAttach` automático, el zumbador dejó el backlight parpadeando a 440 Hz. Usa
  `ledcAttachChannel` explícito: backlight en canal 0 (timer 0), zumbador en canal 2 (timer 1).
  `ledcRead()` devuelve el duty real y sirve para diagnosticar.
- **SYS_OUT está HIGH en reposo y LOW al pulsar.** Con la polaridad al revés la placa "se
  apagaba" cada 2 s; en USB solo se nota como un bip periódico.
- Batería: `analogReadResolution(12)`, promediar 8 lecturas de `analogReadMilliVolts(1)` × 3
  (el divisor está junto a la radio y el ADC es ruidoso), y una curva LiPo por tramos
  (4200→100 %, 4100→90, 4000→78, 3900→62, 3800→45, 3700→25, 3600→10, 3500→4, 3300→0). Como no
  hay VBUS, "cargando" solo se puede estimar por tendencia (subida ≥ 40 mV en ~60 s). Un pack
  lleno en USB marca ~4190 mV y nunca "cargando".
- Si usas modo noche para atenuar el backlight por hora, **mira primero la zona horaria** cuando
  alguien diga que "el brillo no funciona": una TZ europea por defecto con el usuario en México
  limitaba el duty en pleno día.

---

## 5. Gráficos: framebuffer y rendimiento

- `Arduino_Canvas(W, H, panel)` con `gfx->begin(80000000)` y `flush()` por frame va fluido en las
  dos placas (en la C6 sin PSRAM cabe: 172×320×2 = 110 KB).
- El framebuffer del canvas es `uint16_t` RGB565 **little-endian en memoria**. Si vuelcas los
  bytes y los decodificas al revés, rojo y azul salen intercambiados y parece un problema de
  MADCTL/BGR que no existe.
- Deriva el layout de W/H en el arranque; la S3 es 280×240 en horizontal y 240×280 en vertical,
  y la C6 320×172.
- Las fuentes FreeSans de Adafruit_GFX no vienen con Arduino_GFX: hay que vendorizarlas y
  quitar el include de Adafruit.

---

## 6. Flasheo, USB y consola

- Offsets (iguales en C6 y S3, verificados contra el `merged.bin`): bootloader `0x0`,
  particiones `0x8000`, `boot_app0` `0xE000`, app `0x10000`. **No** `0x1000` como el ESP32 clásico.
- Las dos placas enumeran como "USB JTAG_serial debug unit" de Espressif en `/dev/cu.usbmodem*`.
  Un `usbserial-*` que aparezca a la vez **no** es la placa. Si no enumera: cable de datos,
  conectar directo al Mac sin hub, y mantener BOOT, pulsar RST, soltar BOOT.
- Si la subida da "Failed to connect / No serial data received", mismo truco BOOT+RST.
- Hablar con la consola serie sin pyserial:

```bash
stty -f /dev/cu.usbmodemXXXX 115200 raw -echo
cat /dev/cu.usbmodemXXXX &
printf 'status\n' > /dev/cu.usbmodemXXXX
```

- Un comando serie que vuelque el framebuffer en base64 y un script que lo guarde en PNG es la
  mejor inversión del proyecto: permite verificar layouts sin tener la placa delante. Añade
  comandos para simular el botón (`press`/`hold`) y el táctil (`tp`, `tcal`).
- Dos placas conectadas a la vez: pasa el puerto explícito al flashear (`-p`), porque el primer
  `usbmodem` que liste el sistema no tiene por qué ser la que quieres.

---

## 7. WiFi y red (válido para ambas)

- El modo AP de configuración usa `WIFI_AP_STA`: hace falta la interfaz STA para escanear redes
  sin tumbar el punto de acceso, y el escaneo debe ser asíncrono (`scanNetworks(true)`).
- Toma el nombre del AP del **eFuse MAC**, no de la MAC de la interfaz: cambia entre AP y STA.
- Un SSID puede ser cualquier cosa, incluso `localhost`. No lo trates como autocompletado.
- Servir la web gzip desde PROGMEM con `Content-Encoding: gzip`.
- mDNS: `MDNS.addService("miapp","tcp",80)` + `addServiceTxt`. Para descubrir otras placas sin
  bloquear el loop usa la API IDF `mdns_query_async_new` / `mdns_query_async_get_results`
  (`MDNS.queryService` bloquea ~2 s). Un `HTTPClient` de placa a placa necesita **≥ 1,5 s** de
  timeout: una C6 tarda ~300 ms en servir un JSON pequeño y con 400 ms daba `-11` (read timeout).

---

## 8. Comparación rápida

| | C6-LCD-1.47 | S3-Touch-LCD-1.69 |
|---|---|---|
| CPU | RISC-V 160 MHz, 1 núcleo | Xtensa LX7 240 MHz, 2 núcleos |
| Flash / PSRAM | 4 MB / — | 16 MB / 8 MB OPI |
| Pantalla | 172×320 ST7789, cuadrada | 240×280 ST7789V2, esquinas r≈48 px |
| Offsets panel | col 34, fila 0 | col 0, fila 20 |
| Táctil | no | CST816T I2C 0x15, Y estirado 1,2× |
| Feedback | LED WS2812 (GPIO 8) | zumbador (GPIO 42) |
| Batería | no | ADC GPIO 1 ×3, sin VBUS |
| Botones | BOOT (9) | BOOT (0), PWR (SYS_OUT 40, activo bajo) |
| App máx. | 2 MB (`no_ota`) | 3 MB (`app3M_fat9M_16MB`) |
