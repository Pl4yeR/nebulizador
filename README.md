# Water Nebulization System

## Project Overview
This project implements a water nebulization system on a Wemos D1 mini (ESP8266). An ambient sensor (DHT11 or SHT30 — swappable, see below) measures temperature and humidity; the firmware computes a heat index from those readings and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough, using a non-blocking, proportional-control loop — the hotter it is, the more frequently the valve fires.

The firmware is split into small, single-purpose "task" modules (sensors, valve, status LED, OTA, MQTT/Home Assistant) that are each polled cooperatively from a single `loop()` — see [CLAUDE.md](CLAUDE.md) for why this isn't real FreeRTOS on this chip.

## Components Used
- **Wemos D1 mini (ESP8266)**: The microcontroller that runs the code.
- **DHT11 or SHT30 sensor**: Measures temperature and humidity — pick one shield, select the matching PlatformIO environment (see below).
- **Solenoid Valve**: Controls the flow of water in the nebulization system.
- **MOSFET Trigger Switch Drive Module**: Switches the solenoid valve on and off.
- **Built-in LED**: The only status indicator — heartbeat blink when healthy, an error blink code when something's wrong, solid on while the valve is misting, and a fast blink in OTA mode.

## Sensor: DHT11 or SHT30

Two PlatformIO environments in `platformio.ini` share everything except which sensor backend gets compiled in (`src/tasks/task_sensors_dht.cpp` vs. `src/tasks/task_sensors_sht30.cpp` — both implement the same `task_sensors.h` interface, so nothing else in the firmware needs to know which one is active):

| Environment | Sensor | Interface |
|---|---|---|
| `d1_mini` (default) | DHT11 | 1-wire, `DHTPIN` |
| `d1_mini_sht30` | SHT30 | I2C, `SDA_PIN`/`SCL_PIN` |

```
pio run                       # builds d1_mini (DHT11) — the default
pio run -e d1_mini_sht30       # builds the SHT30 variant instead
pio run -e d1_mini_sht30 -t upload
```

Both sensor reads are non-blocking. The SHT30 backend uses the `robtillaart/SHT31` library's async interface (`requestData()` fires the measurement, `dataReady()` is a pure timing check, `readData()` reads the result once ready) instead of its plain blocking `read()`, so a ~15ms measurement never stalls the valve/LED/MQTT/OTA loop.

## Pin map

| Role | Pin | Notes |
|---|---|---|
| DHT11 data | `D5` (default) | `env:d1_mini` only. Configurable, see below |
| SHT30 SDA / SCL | `D2` / `D6` (default) | `env:d1_mini_sht30` only. Configurable, see below |
| Solenoid valve control | `D1` (default) | Configurable, see below. Active-high (`HIGH` = valve open) |
| Status LED | `LED_BUILTIN` (`D4` / GPIO2) | Fixed by the board, active-low |

`D6` (not the conventional `D1`) was picked for `SCL` because `D1` is already `SOLENOID_PIN`. Pin assignments live in [include/pins.h](include/pins.h) as `#ifndef`-guarded macros, so they can be overridden without touching code by setting `build_flags` in [platformio.ini](platformio.ini):

```ini
build_flags =
    -D DHTPIN=D6
    -D SOLENOID_PIN=D2
```

## Setup Instructions

1. **Hardware Connections**:
   - DHT11: connect the data pin to `D5` (or your configured `DHTPIN`).
   - SHT30: connect SDA/SCL to `D2`/`D6` (or your configured `SDA_PIN`/`SCL_PIN`).
   - Wire the solenoid valve to the MOSFET module, and the module's trigger input to `D1` (or your configured `SOLENOID_PIN`).
2. **WiFi/OTA/MQTT credentials**: create `include/config.h` (not committed, see `include/.gitignore`):
   ```cpp
   #define WIFI_SSID       "your-ssid"
   #define WIFI_PASSWORD   "your-password"
   #define OTA_AP_PASSWORD "your-ota-password" // min 8 characters

   #define MQTT_SERVER     "your-mqtt-broker-host-or-ip"
   #define MQTT_PORT       1883
   #define MQTT_USER       "your-mqtt-user"
   #define MQTT_PASSWORD   "your-mqtt-password"
   ```
3. **Build / flash** (PlatformIO):
   ```
   pio run                       # compile (env:d1_mini, DHT11, by default)
   pio run -e d1_mini_sht30       # or compile the SHT30 variant
   pio run -t upload             # compile + flash the default env
   pio device monitor            # serial monitor, 9600 baud
   ```
   `lib_deps` pulls the DHT sensor library (or `robtillaart/SHT31` for the SHT30 env), ElegantOTA, ESP_MultiResetDetector and PubSubClient from the PlatformIO registry automatically.

## Home Assistant (MQTT)

In normal operation the board connects to WiFi and to the MQTT broker configured in `config.h`, and registers itself in Home Assistant automatically via [MQTT discovery](https://www.home-assistant.io/integrations/mqtt/) — no YAML editing needed, the device just appears under **Settings → Devices & services → MQTT** as "Nebulizador" with these entities:

| Entity | Type | Notes |
|---|---|---|
| Temperatura / Humedad / Sensación térmica | sensor | `unknown` while the sensor read is failing |
| Válvula | binary_sensor | ON while misting |
| Error | binary_sensor (`problem`) | ON while any `ErrorFlags` bit is set |
| Código de error | sensor (diagnostic) | raw bitmask, for troubleshooting against `include/errors.h` |
| Umbral mínimo/máximo (sensación térmica) | number | °C, replaces `MIN_HINDEX_THRESHOLD`/`MAX_HINDEX_THRESHOLD` |
| Frecuencia mínima/máxima de chequeo | number | minutes, replaces `MIN_FREQUENCY_MS`/`MAX_FREQUENCY_MS` |
| Segundos de válvula abierta | number | seconds, replaces `VALVE_ACTIVE_TIME_MS` |

Changing any of the five `number` entities updates the running firmware immediately **and persists across reboots** (stored in LittleFS via `src/config_store.cpp`) — the device re-publishes its actual (possibly clamped) value back to HA right after applying it, and again on every boot/reconnect, so HA never shows a stale value.

## OTA updates

OTA is a special mode that overrides everything else — entering it disconnects MQTT (publishing `"offline"` first) and force-closes the valve. To flash new firmware over the air:

1. Power-cycle the board 3 times within 10 seconds (unplug/replug, or reset button if wired).
2. The board reuses its WiFi connection if it's already up (normal operation keeps WiFi on for MQTT), or connects to `WIFI_SSID` otherwise (15 s timeout, falls back to AP `Nebulizador-OTA-<chipid>` if it can't).
3. Browse to `http://<ip>/update` and upload the new `firmware.bin`, authenticating with user `admin` and your `OTA_AP_PASSWORD`.
4. OTA mode is a dead end — sensors/valve/MQTT stay off, and it stays in OTA mode until reflashed or power-cycled again. The device shows as "unavailable" in Home Assistant for the duration.

## LED de estado

El LED integrado (`LED_BUILTIN`) es el único indicador visual. Cuando varias situaciones coinciden, manda la de mayor prioridad (de arriba a abajo en la tabla):

| Situación | Patrón |
|---|---|
| Modo OTA | parpadeo rápido continuo (100ms) |
| Válvula abierta (bombeando) | encendido fijo |
| Error activo | ráfaga de N parpadeos cada ~5s, N según el código de error (ver tabla) |
| Funcionamiento normal (sin errores) | 1 parpadeo cada ~5s (heartbeat) |
| Arranque | 3 parpadeos rápidos (150ms), una vez, antes de lo anterior |

| Código de error | Nº de parpadeos | Causa |
|---|---|---|
| `ErrorFlags::SENSOR` | 2 | Fallo de lectura del sensor ambiente (DHT11 o SHT30, según el entorno compilado) |

Nuevos errores se añaden como bits adicionales en [include/errors.h](include/errors.h); el número de parpadeos se deriva automáticamente del bit más bajo activo (bit N → N+2 parpadeos), sin tocar `task_led`.

## A tener en cuenta

- La sensación térmica (heat index) se calcula a partir de la temperatura y la humedad relativa, y puede ser distinta de la temperatura medida.
- Si la lectura del sensor ambiente falla (NaN), el sistema apaga la válvula si estaba activa, no opera y reintenta en el siguiente ciclo (250ms), quedando a la espera de una lectura válida (y el LED pasa a parpadear el código de error correspondiente).

## Funcionamiento

- Cada 250ms el `loop()` principal llama a los "tasks" de sensores, válvula, MQTT, LED y OTA — ver [CLAUDE.md](CLAUDE.md) para el detalle de cada módulo.
- Si la sensación térmica (`hIndex`) es inferior al umbral mínimo (29.8°C por defecto, configurable desde Home Assistant), la válvula permanece cerrada y el sistema revisa de nuevo cada `max_frequency_min` (30min por defecto).
- Si `hIndex` alcanza o supera el umbral, la válvula se abre `valve_active_s` segundos (5s por defecto) y el intervalo hasta la siguiente comprobación se calcula proporcionalmente entre `min_frequency_min` y `max_frequency_min` (5–30min por defecto) mediante una curva ease-out: cuanto más calor, más frecuente la nebulización.

## Future Enhancements
- Implement additional sensors for more precise control.
- Integrate data logging for monitoring environmental conditions over time.
