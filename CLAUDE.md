# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a water nebulizer/misting system, running on a Wemos D1 mini (ESP8266). It reads temperature/humidity from an ambient sensor (DHT11 or SHT30, see below), computes a heat index, and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough — using non-blocking, cooperatively-scheduled "task" modules, not `delay()`-based sleeping between cycles.

There are two firmware targets in [platformio.ini](platformio.ini), sharing everything except which physical sensor shield is wired up: `env:d1_mini` (DHT11, the default) and `env:d1_mini_sht30` (SHT30, I2C). Built with PlatformIO; there is no legacy Arduino UNO / Arduino IDE target in this codebase.

## Build / flash

```
pio run                       # compile the default env (d1_mini, DHT11)
pio run -e d1_mini_sht30       # compile the SHT30 variant instead
pio run -t upload             # compile + flash the default env
pio device monitor            # serial monitor, 9600 baud
```

`lib_deps` in `platformio.ini` pulls the DHT sensor library (or `robtillaart/SHT31` for the SHT30 env), Adafruit Unified Sensor, ElegantOTA, ESP_MultiResetDetector and PubSubClient from the PlatformIO registry — no manual library install needed. **`include/config.h` must exist locally before building** (WiFi/OTA/MQTT credentials, gitignored — see `include/.gitignore`) since `task_ota.cpp` and `task_mqtt.cpp` include it unconditionally; the build fails without it. There is no linter or test suite; `pio run` (compile-only, no board attached) is the way to check correctness after edits — run it for **both** environments if you touch `task_sensors.h` or anything either sensor backend depends on. The firmware prints to Serial at 9600 baud, prefixed by task (`[MAIN]`, `[SENSORS]`, `[VALVE]`, `[OTA]`, `[MQTT]`, `[CONFIG]`) — the primary way to observe/debug behavior since there's no test harness.

## Pin configuration

All GPIO assignments live in [include/pins.h](include/pins.h) as `#ifndef`-guarded macros (`DHTPIN`, `SDA_PIN`/`SCL_PIN`, `SOLENOID_PIN`), so they're overridable from `platformio.ini` via `build_flags` without touching code:

```ini
build_flags =
    -D DHTPIN=D6
    -D SOLENOID_PIN=D2
```

Defaults: `DHTPIN=D5` (`env:d1_mini` only), `SDA_PIN=D2`/`SCL_PIN=D6` (`env:d1_mini_sht30` only), `SOLENOID_PIN=D1` (both). The I2C pins are **not** the conventional ESP8266 default (SDA=D2/SCL=D1) — D1 is already `SOLENOID_PIN`, so `SCL` was moved to `D6` instead; keep this in mind if you ever need to free up `D1` for something else, since both defaults would need revisiting together. The status LED is *not* in `pins.h` — it's always `LED_BUILTIN` (GPIO2 / `D4`, active-low on the D1 mini), since that's fixed by the board rather than a wiring choice.

**Deliberately removed** (do not reintroduce without being asked): the luminosity/LDR sensor, the physical manual-override button, and separate activity/error status LEDs. The only status indicator is the board's built-in LED — it now carries a heartbeat + error-code blink system (see `task_led` below), so device errors *are* visible on it, just multiplexed onto the one LED rather than a dedicated pin.

## Task architecture — and why it isn't FreeRTOS

This firmware's structure is modeled on a sibling project, **Neverina** (an ESP32 fridge controller), which uses genuine FreeRTOS tasks (`xTaskCreatePinnedToCore`, queues, event groups) — real preemptive threads pinned to specific cores.

**The Wemos D1 mini here is an ESP8266, not an ESP32.** The `platform = espressif8266` Arduino core has no FreeRTOS: no `freertos/*.h` headers ship with the framework at all (verified — there's nothing to include). So "tasks" in this codebase are **cooperative, single-threaded modules**, not real threads:

- Each task module exposes a `xxxTaskLoop(now, ...)` function that does a small unit of work and returns immediately (never blocks, except deliberately inside `otaTaskEnter()` — see below).
- `src/nebulizadorv3.ino`'s `loop()` calls each task's `Loop()` function once per 250ms tick (`LOOP_DELAY_MS`), in a fixed order, and that's the entire scheduler. There's no preemption, no priorities, no separate stacks.
- Modules talk to each other through small, explicit getter/setter functions (e.g. `sensorsGetHeatIndex()`, `setError()`/`getErrors()`) — not shared queues, since there's no concurrency to guard against.

If real concurrency (e.g. genuinely parallel WiFi handling while doing time-sensitive GPIO work) is ever needed, the only way to get actual FreeRTOS on this hardware is to swap to an ESP32-based board (e.g. Wemos D1 mini32, which is what Neverina runs on) — that decision was explicitly deferred; see the "hardware target" discussion in project history if you need to revisit it.

## Source structure

```
include/
  pins.h              — DHTPIN / SDA_PIN / SCL_PIN / SOLENOID_PIN, #ifndef-guarded, overridable via build_flags
  config.h            — WiFi/OTA/MQTT credentials (gitignored, not committed — create locally)
  errors.h            — ErrorFlags bitmask + getErrors()/setError()/clearError()
  config_store.h      — ValveConfig struct + configStoreLoad/SaveValveConfig() (LittleFS-backed)
  heat_index.h        — computeHeatIndexC(), shared by both sensor backends
src/
  errors.cpp           — plain uint8_t bitmask; no locking needed, everything runs on one cooperative thread
  config_store.cpp     — persists ValveConfig to /valve_config.bin (magic+version header, atomic rename-on-save)
  heat_index.cpp        — NOAA/Rothfusz heat index formula, Celsius in/out
  nebulizadorv3.ino    — setup()/loop() orchestrator only; wires task modules together
  tasks/
    task_sensors.h        — shared interface; exactly one of the two .cpp below is compiled in (see below)
    task_sensors_dht.cpp   — DHT11 backend (env:d1_mini)
    task_sensors_sht30.cpp — SHT30 backend (env:d1_mini_sht30)
    task_valve.h/.cpp   — proportional-control valve decision + SOLENOID_PIN; owns the 5 HA-configurable control parameters
    task_led.h/.cpp     — LED_BUILTIN state machine (heartbeat / error codes / OTA fast-blink)
    task_ota.h/.cpp     — triple-reset detection + ElegantOTA over WiFi
    task_mqtt.h/.cpp     — Home Assistant integration over MQTT (device discovery, state, config)
```

### task_sensors — swappable DHT11 / SHT30 backends
`task_sensors.h` is a fixed interface (`sensorsTaskBegin()`, `sensorsTaskLoop(now, intervalMs)`, `sensorsGetTemperature()`/`sensorsGetHumidity()`/`sensorsGetHeatIndex()`, `sensorsReadIsValid()`) implemented by **exactly one** of two mutually-exclusive `.cpp` files, selected per PlatformIO environment via `build_src_filter` in `platformio.ini` (`env:d1_mini` excludes `task_sensors_sht30.cpp`, `env:d1_mini_sht30` excludes `task_sensors_dht.cpp`) — never both at once, so there's no runtime branching or class hierarchy, just a straight swap of which translation unit provides the symbols. Every other module only ever calls the `task_sensors.h` functions, so nothing outside this pair of files needs to know which physical sensor is installed.

- **`task_sensors_dht.cpp`** (DHT11, single-wire, `DHTPIN`): unchanged from before — `s_dht.readHumidity()`/`readTemperature()` block briefly (a few ms, bit-banged) once per `intervalMs`, which is fine since reads are infrequent (5–30min cadence, driven by `task_valve`).
- **`task_sensors_sht30.cpp`** (SHT30, I2C, `SDA_PIN`/`SCL_PIN`, `robtillaart/SHT31`): genuinely non-blocking, because the library's *async* interface splits what would otherwise be one blocking `read()` call into three pieces — `requestData()` (fires the I2C measurement command, returns immediately), `dataReady()` (a pure `millis()` check against the ~15ms measurement window, no I2C traffic — safe to poll every tick without ever blocking), and `readData()` (the actual I2C read, fast). `sensorsTaskLoop()` is a 2-state machine (`Idle`/`Measuring`) across this: when `intervalMs` elapses it calls `requestData()` and moves to `Measuring`; subsequent ticks just check `dataReady()` until it's true, then `readData()` and back to `Idle`. **Do not swap this for the library's plain `read()` method** — that blocks the entire cooperative `loop()` (valve, LED, MQTT, OTA-window checks) for the measurement duration, defeating the point of the task architecture.

Both backends compute the heat index via the shared `computeHeatIndexC()` (`heat_index.h`/`.cpp`, extracted from the Adafruit DHT library's NOAA/Rothfusz formula) rather than each having their own copy — this guarantees the same temperature+humidity pair produces the same heat index regardless of which shield is installed. Both call `setError(ErrorFlags::SENSOR)` / `clearError(ErrorFlags::SENSOR)` on read failure/success to report that state to `task_led` (and, transitively, `task_mqtt`) — the error bit is named generically (`SENSOR`, not `DHT`) precisely because it means "the ambient sensor task failed to read," independent of which physical chip is behind it.

### task_valve
The core decision function, `valveTaskLoop(now, hIndex, sensorValid)`:
- `!sensorValid` → sensor-error path: closes the valve if it was open, logs, and returns (retries next tick, cadence unchanged — see caveat below).
- `hIndex >= s_minHIndexThreshold` (default 29.8°C) → opens the valve for `s_valveActiveTimeMs` (default 5s), then computes the next check interval via an ease-out curve mapping `hIndex` within `[s_minHIndexThreshold, s_maxHIndexThreshold]` to a delay within `[s_minFrequencyMs, s_maxFrequencyMs]` (defaults `[5min, 30min]`) — hotter means more frequent misting.
- `hIndex < s_minHIndexThreshold` → valve stays closed, next check pinned to `s_maxFrequencyMs`.

`controlSolenoidValve()` drives `SOLENOID_PIN` (active-high: `HIGH` = valve open) and calls `ledTaskSetValveActive()` so the LED holds solid on while misting — see `task_led` below for priority vs. the heartbeat/error/OTA displays.

**All 5 control parameters are runtime-configurable and persisted**, not `constexpr` anymore: `s_minHIndexThreshold`, `s_maxHIndexThreshold`, `s_maxFrequencyMs`, `s_minFrequencyMs`, `s_valveActiveTimeMs` live as anonymous-namespace mutable state, loaded in `valveTaskBegin()` via `config_store` (falling back to the `DEFAULT_*` constants and immediately persisting them if no config file exists yet — so the file is always valid after first boot). Each has a `valveTaskGetXxx()` getter and a `valveTaskSetXxx()` setter; setters `constrain()` the incoming value to a hardcoded sane range **independent of whatever Home Assistant's own `number` entity min/max says** (the MQTT command topic is reachable by any client, not just HA's UI — defense in depth), then call `configStoreSaveValveConfig()` before returning. The only caller of the setters is `task_mqtt`'s command callback — `task_valve` itself has no knowledge that MQTT exists (see `task_mqtt` below for why the dependency is one-directional).

**Pre-existing quirk, carried forward from the pre-task-refactor code:** because `task_sensors`' read cadence is driven by `task_valve`'s own interval, a sensor fault during a "calm" (cold) interval won't get a fresh retry read until that interval elapses (up to `s_maxFrequencyMs`) — `valveTaskLoop`'s error branch re-checks the same stale `NAN` every 250ms but doesn't force an earlier re-read. This was true before the task refactor too; fixing it is a behavior change, not an architecture one.

### task_led
The built-in LED is the device's only status output, so it multiplexes four things through `ledTaskLoop(now)`, in priority order (each check `return`s before the next, lower-priority one runs):
1. **OTA mode** — set once via `ledTaskSetOtaMode()` (a dead end, see `task_ota`): fast 100ms blink, overrides everything else.
2. **Valve active** — `ledTaskSetValveActive(true)`, called from `task_valve`'s `controlSolenoidValve()`: solid on for as long as the valve is open (`VALVE_ACTIVE_TIME_MS`, 5s). Overrides the heartbeat/error display below, but not OTA.
3. **Healthy heartbeat** — no bits set in `getErrors()`: a single blink every ~5s (`PAUSE_MS`), i.e. the same burst machinery below with a blink count of 1.
4. **Error codes** — `getErrors()` non-zero: blinks the lowest set bit's code (`__builtin_ctz(errors) + 2`, so bit 0 → 2 blinks, bit 1 → 3, ...) as a burst, then pauses ~5s before repeating. Only the lowest set bit is shown; if more than one error is active simultaneously, others queue behind it silently until it's cleared (there's currently only one error source, `ErrorFlags::SENSOR`, so this doesn't come up yet — worth revisiting once a second one exists).

Heartbeat and error bursts share one state machine (`s_displayedCode`/`s_step`/`s_lastToggle`): a burst of `blinkCount * 2` on/off steps (`BLINK_MS` each) followed by a `PAUSE_MS` gap. If the error code changes mid-cycle (including healthy ↔ error transitions), the burst restarts immediately rather than waiting out the old cycle, so changes are visible within one tick, not up to 5s late. Coming out of valve-active back to this display also forces a clean restart (`ledTaskSetValveActive(false)` resets `s_displayedCode` to the same sentinel used at boot) rather than resuming a stale mid-burst position from before the valve opened.

Error sources live in `errors.h`/`errors.cpp` (project root, not under `tasks/`, since it's a cross-cutting concern multiple tasks touch) — a plain `ErrorFlags` bitmask with `getErrors()`/`setError()`/`clearError()`. Add new error sources as new bits there; `task_led` derives the blink count automatically, no changes needed on the LED side.

### task_ota
Modeled on Neverina's OTA mode, adapted for ESP8266 (`ESP8266WiFi`/`ESP8266WebServer` instead of `WiFi`/`WebServer`, `ESP.getChipId()` instead of `ESP.getEfuseMac()`). No BLE trigger exists in this project (there's no BLE at all), so **the only OTA trigger is a triple power-cycle within 10s**, detected via `ESP_MultiResetDetector` (EEPROM-backed):
- `otaTaskCheckTrigger()` — call once, early in `setup()`. Returns `true` if this boot is the 3rd rapid reset.
- If triggered, `otaTaskEnter()` is called and **never returns**: it sets the LED to OTA mode, force-closes the valve, calls `mqttTaskDisconnect()` (publishes `"offline"` and disconnects — a no-op if `task_mqtt` never began this boot, e.g. a cold-boot triple-reset), then connects WiFi **only if not already connected** (`WiFi.status() != WL_CONNECTED`) — since ESP8266 persists STA credentials and can auto-reconnect from a previous normal-mode boot independently of this boot's own code, checking status first avoids tearing down and reconnecting a link that's already up. Falls back to AP mode `Nebulizador-OTA-<chipid>` + `OTA_AP_PASSWORD` if it can't connect. Serves ElegantOTA at `/update`, and blocks forever in its own loop (calling `ledTaskLoop()` itself, since the main `loop()` never regains control).
- If not triggered, `otaTaskLoop()` is called every normal-mode tick — it just forwards to `MultiResetDetector::loop()`, which owns its own `millis()`-based timeout internally and calls `stop()` once the reset-detection window elapses. Don't reimplement that timing by hand; call the library's `loop()`.

**OTA is a special mode that overrides everything else** — it's the only thing allowed to tear down or bypass MQTT/valve/sensor state, and once entered there's no way back except a reboot.

### task_mqtt
Home Assistant integration over MQTT (`knolleary/PubSubClient`), using HA's ["device discovery"](https://www.home-assistant.io/integrations/mqtt/) format — one retained payload (`DISCOVERY_PAYLOAD`, a static `R"JSON(...)"` literal, ~3.5KB, hence `setBufferSize(5120)`) registers every entity at once, so there's no manual YAML on the HA side. Device id / topic prefix: `nebulizador`.

- **Read-only telemetry** — one shared, retained JSON topic (`homeassistant/nebulizador/state`) carries temperature, humidity, heat index, valve state, the raw `error_flags` bitmask, and the current value of all 5 configurable parameters. `heat_index`/`temperature`/`humidity` are emitted as JSON `null` (not the string `"nan"`, which isn't valid JSON) when `!sensorsReadIsValid()`. `error_flags` is published raw/undecoded — HA's own `value_template` derives a `problem` binary_sensor and a diagnostic sensor from it, so adding a second error bit later needs zero changes here, mirroring how `task_led` already derives its blink count from the same bitmask.
- **Writable config** — 5 MQTT `number` entities, one command topic each (`homeassistant/nebulizador/number/<name>/set`), in human units (°C, minutes, seconds) that `task_mqtt` converts to/from `task_valve`'s internal ms/°C representation. The command callback calls the matching `valveTaskSetXxx()`, then immediately republishes the shared state topic — that's the "confirm what actually got applied" round-trip, since the setter may have clamped the value.
- **Availability** — `homeassistant/nebulizador/availability`, `"online"`/`"offline"`, backed by an MQTT Last Will Testament set on `connect()`. A clean `disconnect()` does **not** trigger the LWT (only an ungraceful drop does), so `mqttTaskDisconnect()` explicitly publishes `"offline"` before disconnecting — otherwise HA would keep showing the device online throughout an entire OTA session.
- **Connection**: `mqttTaskLoop(now)` is a small non-blocking state machine — WiFi down → nothing to do (ESP8266 SDK auto-reconnects WiFi in the background); WiFi up but MQTT not connected → throttled reconnect attempt every `MQTT_RECONNECT_INTERVAL_MS`, and on success republishes discovery + availability + full state (every reconnect, not just cold boot — idempotent, self-heals a broker that lost its retained messages) before subscribing to the 5 command topics; otherwise `PubSubClient::loop()` plus an edge-triggered check that republishes state only when something actually changed (comparing cached previous values — `sensorValid` is compared separately from `heatIndex` specifically to avoid `NAN != NAN`, which is always true in IEEE754 and would otherwise republish every tick while a sensor error persists).
- **One-directional dependency**: `task_mqtt → task_valve`/`task_sensors`/`errors.h`, never the reverse — `task_valve` has no `#include "task_mqtt.h"` and doesn't know MQTT exists, consistent with every other module boundary in this codebase.

WiFi is now brought up whenever `task_mqtt` is running (i.e. always, in normal mode) — this replaced the old `otaTaskDisableWifi()`, which no longer exists.

## Git history note

This repository has diverged branches with substantially different firmware architectures (an older Arduino UNO "caso"-table design vs. this proportional-control D1 mini rewrite). If you're comparing against old commits or other branches, don't assume their logic, pin maps, or file layout apply here — this file describes the code as it exists on the current branch only.
