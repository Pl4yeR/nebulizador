# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a water nebulizer/misting system, running on a Wemos D1 mini (ESP8266). It reads temperature/humidity from an ambient sensor (DHT11 or SHT30, see below), computes a heat index, and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough — using non-blocking, cooperatively-scheduled "task" modules, not `delay()`-based sleeping between cycles.

There are two firmware targets in [platformio.ini](platformio.ini), sharing everything except which physical sensor shield is wired up: `env:d1_mini_dht11` (DHT11, the default) and `env:d1_mini_sht30` (SHT30, I2C). Built with PlatformIO; there is no legacy Arduino UNO / Arduino IDE target in this codebase.

## Build / flash

```
pio run                       # compile the default env (d1_mini, DHT11)
pio run -e d1_mini_sht30       # compile the SHT30 variant instead
pio run -t upload             # compile + flash the default env
pio device monitor            # serial monitor, 9600 baud
```

`lib_deps` in `platformio.ini` pulls the DHT sensor library (or `robtillaart/SHT31` for the SHT30 env), Adafruit Unified Sensor, ElegantOTA, ESP_MultiResetDetector and PubSubClient from the PlatformIO registry — no manual library install needed. **`include/config.h` must exist locally before building** (WiFi/OTA/MQTT credentials, gitignored — see `include/.gitignore`) since `task_ota.cpp` and `task_mqtt.cpp` include it unconditionally; the build fails without it. There is no linter or test suite; `pio run` (compile-only, no board attached) is the way to check correctness after edits — run it for **both** environments if you touch `task_sensors.h` or anything either sensor backend depends on. The firmware prints to Serial at 9600 baud, prefixed by task (`[MAIN]`, `[SENSORS]`, `[VALVE]`, `[OTA]`, `[MQTT]`, `[CONFIG]`, `[TIME]`) — the primary way to observe/debug behavior since there's no test harness.

## Pin configuration

All GPIO assignments live in [include/pins.h](include/pins.h) as `#ifndef`-guarded macros (`DHTPIN`, `SDA_PIN`/`SCL_PIN`, `SOLENOID_PIN`), so they're overridable from `platformio.ini` via `build_flags` without touching code:

```ini
build_flags =
    -D DHTPIN=D6
    -D SOLENOID_PIN=D2
```

**`pins.h`'s bare `#ifndef` fallbacks are the real, confirmed factory/wiring defaults for this project's actual shields**: DHT11 → `DHTPIN=D4`, SHT30 → `SDA_PIN=D2`/`SCL_PIN=D1`, Relay → `SOLENOID_PIN=D1` (the latter two verified directly against `wemos.cc`'s own per-shield docs — their official "DHT Shield" is actually a DHT12 over the *same* I2C bus as the SHT30 shield, not a single-wire sensor, so it isn't this project's DHT11 default; `D4` for that came directly from the user confirming their actual hardware). Two real collisions follow from this:
- `SCL_PIN` vs `SOLENOID_PIN` (both `D1`) — you can't stack the Relay Shield with an I2C shield without reassigning one; `platformio.ini`'s `env:d1_mini_sht30` sets `SCL_PIN=D6` to resolve it.
- `DHTPIN` vs `LED_BUILTIN` (both GPIO2 / `D4`) — **unresolvable in software**, see the callout below; the sensor is physically rewired to `D5` instead.

This project's actual wiring is always resolved per environment in `platformio.ini`'s `build_flags`, never by relying on `pins.h`'s bare fallbacks alone: `env:d1_mini_dht11` sets `DHTPIN=D5` (physically rewired off the shield's factory `D4`, see below), `env:d1_mini_sht30` sets `SDA_PIN=D2`/`SCL_PIN=D6`. The status LED is *not* in `pins.h` — it's always `LED_BUILTIN` (GPIO2 / `D4`, active-low on the D1 mini, confirmed in the framework's own `variants/d1_mini/pins_arduino.h`), since that's fixed by the board rather than a wiring choice, and it owns GPIO2 exclusively.

**Why DHT11-on-D4 (the shield's factory pin) is fundamentally incompatible with the LED — investigated, not assumed:**
- Per the Aosong DHT11 datasheet, the sensor hardware-monitors its data line and treats **any LOW of ≥18ms followed by a rising edge** as a read start signal — no upper bound on the LOW duration is specified. It then drives the line itself for ~4ms (80µs+80µs response + 40 data bits).
- The built-in LED is active-low, so *every* LED pattern in `task_led` (heartbeat 250ms, error blinks 250ms, valve-active up to 5s+, OTA 100ms, boot blinks 150ms) holds the line LOW ≥18ms — meaning **every LED turn-off edge triggers a phantom sensor read**, with the sensor then driving LOW against our push-pull OUTPUT HIGH (bus contention, out of spec) and its state machine left mid-transaction. A real read arriving <1s after a phantom trigger (DHT11's minimum sampling interval) can fail or return stale data.
- Community confirmation: a documented case of this exact shield-on-D4 conflict ([blog.zs64.net](https://blog.zs64.net/2018/02/fixing-the-wemos-d1-mini-dht22-shield/)) reports the LED/TXD1 activity "confuses the sensor completely" and fixes it by *cutting the shield trace and rewiring to another pin*; ESPHome refuses to compile a `status_led` + `dht` sharing a pin ("Pin 2 is used in multiple places"); Tasmota's template system allows only one function per GPIO. Consensus: one pin, one function.
- **Resolution here**: the shield's data line is physically rewired from `D4` to `D5` (`env:d1_mini_dht11` sets `DHTPIN=D5`), and `task_sensors_dht.cpp` carries a `static_assert(DHTPIN != LED_BUILTIN, ...)` so the collision can never silently compile again (a `static_assert`, not `#if` — `D4` is a `static const`, invisible to the preprocessor). `pins.h`'s `DHTPIN=D4` fallback is documentation of the shield's factory wiring, deliberately un-buildable without an override.

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
    task_sensors_dht.cpp   — DHT11 backend (env:d1_mini_dht11)
    task_sensors_sht30.cpp — SHT30 backend (env:d1_mini_sht30)
    task_valve.h/.cpp   — proportional-control valve decision + SOLENOID_PIN; owns the 7 HA-configurable control parameters, the manual override and the schedule window
    task_led.h/.cpp     — LED_BUILTIN state machine (heartbeat / error codes / OTA fast-blink)
    task_ota.h/.cpp     — triple-reset detection + ElegantOTA over WiFi
    task_mqtt.h/.cpp     — Home Assistant integration over MQTT (device discovery, state, config)
    task_time.h/.cpp     — NTP/SNTP wall-clock sync, used by task_valve's schedule window and task_mqtt's timestamp telemetry
```

### task_sensors — swappable DHT11 / SHT30 backends
`task_sensors.h` is a fixed interface (`sensorsTaskBegin()`, `sensorsTaskLoop(now, intervalMs)`, `sensorsGetTemperature()`/`sensorsGetHumidity()`/`sensorsGetHeatIndex()`, `sensorsReadIsValid()`) implemented by **exactly one** of two mutually-exclusive `.cpp` files, selected per PlatformIO environment via `build_src_filter` in `platformio.ini` (`env:d1_mini_dht11` excludes `task_sensors_sht30.cpp`, `env:d1_mini_sht30` excludes `task_sensors_dht.cpp`) — never both at once, so there's no runtime branching or class hierarchy, just a straight swap of which translation unit provides the symbols. Every other module only ever calls the `task_sensors.h` functions, so nothing outside this pair of files needs to know which physical sensor is installed.

- **`task_sensors_dht.cpp`** (DHT11, single-wire, `DHTPIN`): unchanged from before — `s_dht.readHumidity()`/`readTemperature()` block briefly (a few ms, bit-banged) once per `intervalMs`, which is fine since reads are infrequent (5–30min cadence, driven by `task_valve`).
- **`task_sensors_sht30.cpp`** (SHT30, I2C, `SDA_PIN`/`SCL_PIN`, `robtillaart/SHT31`): genuinely non-blocking, because the library's *async* interface splits what would otherwise be one blocking `read()` call into three pieces — `requestData()` (fires the I2C measurement command, returns immediately), `dataReady()` (a pure `millis()` check against the ~15ms measurement window, no I2C traffic — safe to poll every tick without ever blocking), and `readData()` (the actual I2C read, fast). `sensorsTaskLoop()` is a 2-state machine (`Idle`/`Measuring`) across this: when `intervalMs` elapses it calls `requestData()` and moves to `Measuring`; subsequent ticks just check `dataReady()` until it's true, then `readData()` and back to `Idle`. **Do not swap this for the library's plain `read()` method** — that blocks the entire cooperative `loop()` (valve, LED, MQTT, OTA-window checks) for the measurement duration, defeating the point of the task architecture.

Both backends compute the heat index via the shared `computeHeatIndexC()` (`heat_index.h`/`.cpp`, extracted from the Adafruit DHT library's NOAA/Rothfusz formula) rather than each having their own copy — this guarantees the same temperature+humidity pair produces the same heat index regardless of which shield is installed. Both call `setError(ErrorFlags::SENSOR)` / `clearError(ErrorFlags::SENSOR)` on read failure/success to report that state to `task_led` (and, transitively, `task_mqtt`) — the error bit is named generically (`SENSOR`, not `DHT`) precisely because it means "the ambient sensor task failed to read," independent of which physical chip is behind it.

### task_valve
`valveTaskLoop(now, hIndex, sensorValid)` checks, in priority order (each branch `return`s before the next runs):
1. **Manual override** (`valveTaskSetManual()`, driven by `task_mqtt`'s switch) — takes priority over everything, including an invalid sensor reading: opens the valve for `s_valveActiveTimeMs` and self-clears back to OFF, no HA/user action needed to close it. While active, the automatic cycle's timers (`s_cycleStartTime`/`s_currentCycleDelayMs`) aren't evaluated at all — they resume exactly where they left off once the manual cycle ends.
2. **Schedule window** (`scheduleAllowsNow()`) — if `s_startMinuteOfDay == s_endMinuteOfDay` (disabled) or `!timeTaskIsSynced()` (no clock yet, see `task_time` below), automatic control runs unrestricted. Otherwise it only runs inside `[start, end]`; a window where `start > end` is treated as crossing midnight (`m >= start || m <= end`). Outside the window the valve is force-closed and the cycle timers are left frozen, so the moment the window reopens the cycle reads as already-elapsed and re-evaluates immediately rather than waiting out a stale delay.
3. **Sensor-error path**: closes the valve if it was open, logs, and returns (retries next tick, cadence unchanged — see caveat below).
4. `hIndex >= s_minHIndexThreshold` (default 29.8°C) → opens the valve for `s_valveActiveTimeMs` (default 5s), then computes the next check interval via an ease-out curve mapping `hIndex` within `[s_minHIndexThreshold, s_maxHIndexThreshold]` to a delay within `[s_minFrequencyMs, s_maxFrequencyMs]` (defaults `[5min, 30min]`) — hotter means more frequent misting.
5. `hIndex < s_minHIndexThreshold` → valve stays closed, next check pinned to `s_maxFrequencyMs`.

`controlSolenoidValve()` drives `SOLENOID_PIN` (active-high: `HIGH` = valve open) and calls `ledTaskSetValveActive()` so the LED holds solid on while misting — see `task_led` below for priority vs. the heartbeat/error/OTA displays.

**7 control parameters are runtime-configurable and persisted**, not `constexpr`: `s_minHIndexThreshold`, `s_maxHIndexThreshold`, `s_maxFrequencyMs`, `s_minFrequencyMs`, `s_valveActiveTimeMs`, `s_startMinuteOfDay`, `s_endMinuteOfDay` live as anonymous-namespace mutable state, loaded in `valveTaskBegin()` via `config_store` (falling back to the `DEFAULT_*` constants — schedule disabled by default — and immediately persisting them if no config file exists yet — so the file is always valid after first boot). Each has a `valveTaskGetXxx()` getter and a `valveTaskSetXxx()` setter; setters `constrain()` the incoming value to a hardcoded sane range **independent of whatever Home Assistant's own `number`/`time` entity min/max says** (the MQTT command topic is reachable by any client, not just HA's UI — defense in depth), then call `configStoreSaveValveConfig()` before returning. The only caller of the setters (and of `valveTaskSetManual()`) is `task_mqtt`'s command callback — `task_valve` itself has no knowledge that MQTT exists (see `task_mqtt` below for why the dependency is one-directional). The manual override's own state (`s_manualActive`/`s_manualStartTime`) is deliberately **not** persisted — a reboot always comes up with no manual cycle in progress.

**Pre-existing quirk, carried forward from the pre-task-refactor code:** because `task_sensors`' read cadence is driven by `task_valve`'s own interval, a sensor fault during a "calm" (cold) interval won't get a fresh retry read until that interval elapses (up to `s_maxFrequencyMs`) — `valveTaskLoop`'s error branch re-checks the same stale `NAN` every 250ms but doesn't force an earlier re-read. This was true before the task refactor too; fixing it is a behavior change, not an architecture one.

### task_led
The built-in LED is the device's only status output, so it multiplexes four things through `ledTaskLoop(now)`, in priority order (each check `return`s before the next, lower-priority one runs):
1. **OTA mode** — set once via `ledTaskSetOtaMode()` (a dead end, see `task_ota`): fast 100ms blink, overrides everything else.
2. **Valve active** — `ledTaskSetValveActive(true)`, called from `task_valve`'s `controlSolenoidValve()`: solid on for as long as the valve is open (`VALVE_ACTIVE_TIME_MS`, 5s). Overrides the heartbeat/error display below, but not OTA.
3. **Healthy heartbeat** — no bits set in `getErrors()`: a single blink every ~5s (`PAUSE_MS`), i.e. the same burst machinery below with a blink count of 1.
4. **Error codes** — `getErrors()` non-zero: blinks the lowest set bit's code (`__builtin_ctz(errors) + 2`, so bit 0 → 2 blinks, bit 1 → 3, ...) as a burst, then pauses ~5s before repeating. Only the lowest set bit is shown; if more than one error is active simultaneously, others queue behind it silently until it's cleared (there's currently only one error source, `ErrorFlags::SENSOR`, so this doesn't come up yet — worth revisiting once a second one exists).

Heartbeat and error bursts share one state machine (`s_displayedCode`/`s_step`/`s_lastToggle`): a burst of `blinkCount * 2` on/off steps (`BLINK_MS` each) followed by a `PAUSE_MS` gap. If the error code changes mid-cycle (including healthy ↔ error transitions), the burst restarts immediately rather than waiting out the old cycle, so changes are visible within one tick, not up to 5s late. Coming out of valve-active back to this display also forces a clean restart (`ledTaskSetValveActive(false)` resets `s_displayedCode` to the same sentinel used at boot) rather than resuming a stale mid-burst position from before the valve opened.

Error sources live in `errors.h`/`errors.cpp` (project root, not under `tasks/`, since it's a cross-cutting concern multiple tasks touch) — a plain `ErrorFlags` bitmask with `getErrors()`/`setError()`/`clearError()`. Add new error sources as new bits there; `task_led` derives the blink count automatically, no changes needed on the LED side.

`task_led` owns `LED_BUILTIN` (GPIO2) exclusively — nothing else in a successfully-compiled build can touch that pin, because `task_sensors_dht.cpp`'s `static_assert` rejects `DHTPIN == LED_BUILTIN` at compile time (see "Pin configuration" above). If the LED ever appears frozen or glitchy, suspect a new pin-sharing regression first.

### task_ota
Modeled on Neverina's OTA mode, adapted for ESP8266 (`ESP8266WiFi`/`ESP8266WebServer` instead of `WiFi`/`WebServer`, `ESP.getChipId()` instead of `ESP.getEfuseMac()`). No BLE trigger exists in this project (there's no BLE at all), so **the only OTA trigger is a triple power-cycle within 10s**, detected via `ESP_MultiResetDetector` (EEPROM-backed):
- `otaTaskCheckTrigger()` — call once, early in `setup()`. Returns `true` if this boot is the 3rd rapid reset.
- If triggered, `otaTaskEnter()` is called and **never returns**: it sets the LED to OTA mode, force-closes the valve, calls `mqttTaskDisconnect()` (publishes `"offline"` and disconnects — a no-op if `task_mqtt` never began this boot, e.g. a cold-boot triple-reset), then connects WiFi **only if not already connected** (`WiFi.status() != WL_CONNECTED`) — since ESP8266 persists STA credentials and can auto-reconnect from a previous normal-mode boot independently of this boot's own code, checking status first avoids tearing down and reconnecting a link that's already up. Falls back to AP mode `Nebulizador-OTA-<chipid>` + `OTA_AP_PASSWORD` if it can't connect. Serves ElegantOTA at `/update`, and blocks forever in its own loop (calling `ledTaskLoop()` itself, since the main `loop()` never regains control).
- If not triggered, `otaTaskLoop()` is called every normal-mode tick — it just forwards to `MultiResetDetector::loop()`, which owns its own `millis()`-based timeout internally and calls `stop()` once the reset-detection window elapses. Don't reimplement that timing by hand; call the library's `loop()`.

**OTA is a special mode that overrides everything else** — it's the only thing allowed to tear down or bypass MQTT/valve/sensor state, and once entered there's no way back except a reboot.

### task_time
Wall-clock sync via SNTP (`configTime()`, ESP8266 core), started from `timeTaskBegin()` in the normal-mode `setup()` path only (OTA mode has no use for it). `settimeofday_cb()` registers a callback that fires on every successful sync (initial and periodic); it only flips an internal `s_synced` flag — `timeTaskLoop()` is what logs the transition once, keeping side effects confined to a task's own `Loop()` like every other module here. Re-sync cadence is 60 minutes, made explicit by overriding the core's weak `sntp_update_delay_MS_rfc_not_less_than_15000()` (which otherwise silently defaults to 1 hour anyway — this just documents the requirement in code instead of relying on an undocumented default). Timezone is `TZ_Europe_Madrid` (`TZ.h`, handles CET/CEST automatically), overridable via a `TIME_TZ` build flag without touching `config.h`.

**Degrades to "no clock" cleanly, by design**: if the initial sync never completes (no WiFi, NTP server unreachable, etc.) `timeTaskIsSynced()` stays `false` forever, and every consumer treats that as "run unrestricted" rather than an error — `task_valve`'s `scheduleAllowsNow()` ignores the configured window entirely (24h operation, same as before this feature existed) and `task_mqtt` publishes `last_run` as JSON `null`. There's deliberately no `ErrorFlags` bit for this — a missing clock is an expected degraded mode with defined fallback behavior, not a fault to surface on the LED. Once the first sync succeeds, later failures don't matter: the C library's `time()` keeps advancing off the last known-good value regardless of SNTP's health.

Exposes `timeTaskIsSynced()`, `timeTaskMinutesOfDay()` (local time as minutes since midnight, for `task_valve`'s schedule check — only meaningful if synced) and `timeTaskFormatEpochUtc()` (ISO 8601 UTC string, for `task_mqtt`'s `device_class: timestamp` telemetry).

### task_mqtt
Home Assistant integration over MQTT (`knolleary/PubSubClient`), using HA's ["device discovery"](https://www.home-assistant.io/integrations/mqtt/) format — one retained payload (`DISCOVERY_PAYLOAD`, a static `R"JSON(...)"` literal, ~4.5KB across 16 `cmps`, hence `setBufferSize(6144)`) registers every entity at once, so there's no manual YAML on the HA side. Device id / topic prefix: `nebulizador`.

- **Read-only telemetry** — one shared, retained JSON topic (`homeassistant/nebulizador/state`) carries temperature, humidity, heat index, valve state, the raw `error_flags` bitmask, the current value of all 7 configurable parameters, plus `next_run_s` (seconds until the next automatic cycle, from `valveTaskGetSensorIntervalMs()`) and `last_run` (an ISO 8601 UTC timestamp built from `valveTaskGetCycleStartMs()` + `task_time`, or JSON `null` if `!timeTaskIsSynced()` — HA renders that `null` as `'None'`, which puts the `device_class: timestamp` sensor in `unknown`, the same mechanism already used for `temperature`/`humidity`/`heat_index` on a failed sensor read). `error_flags` is published raw/undecoded — HA's own `value_template` derives a `problem` binary_sensor and a diagnostic sensor from it, so adding a second error bit later needs zero changes here, mirroring how `task_led` already derives its blink count from the same bitmask.
- **Writable config** — 5 MQTT `number` entities plus 2 `time` entities, one command topic each, in human units (°C, minutes, seconds, `HH:MM:SS`) that `task_mqtt` converts to/from `task_valve`'s internal ms/°C/minute-of-day representation. `{start,end}_time/set` payloads are parsed with `sscanf("%2u:%2u")` (seconds ignored) and range-checked; an unparseable payload is dropped (setter not called) but the state topic is republished anyway, so HA snaps back to the real persisted value instead of showing the rejected input. The command callback calls the matching `valveTaskSetXxx()`, then immediately republishes the shared state topic — that's the "confirm what actually got applied" round-trip, since the setter may have clamped the value.
- **Manual trigger** — one `switch` entity (`homeassistant/nebulizador/switch/manual/set`), forwarding directly to `valveTaskSetManual()`. It's a "fire and forget" switch, not a persistent toggle: HA sees it flip back to OFF on its own once `task_valve`'s manual cycle finishes, because that transition is picked up by the same edge-triggered state-republish as everything else (see below) — no extra code needed on the MQTT side for the auto-off.
- **Availability** — `homeassistant/nebulizador/availability`, `"online"`/`"offline"`, backed by an MQTT Last Will Testament set on `connect()`. A clean `disconnect()` does **not** trigger the LWT (only an ungraceful drop does), so `mqttTaskDisconnect()` explicitly publishes `"offline"` before disconnecting — otherwise HA would keep showing the device online throughout an entire OTA session.
- **Connection**: `mqttTaskLoop(now)` is a small non-blocking state machine — WiFi down → nothing to do (ESP8266 SDK auto-reconnects WiFi in the background); WiFi up but MQTT not connected → throttled reconnect attempt every `MQTT_RECONNECT_INTERVAL_MS`, and on success republishes discovery + availability + full state (every reconnect, not just cold boot — idempotent, self-heals a broker that lost its retained messages) before subscribing to the 8 command topics; otherwise `PubSubClient::loop()` plus an edge-triggered check that republishes state only when something actually changed (comparing cached previous values, now including the valve's cycle-start time, next-cycle interval and manual-active flag alongside the original four — `sensorValid` is compared separately from `heatIndex` specifically to avoid `NAN != NAN`, which is always true in IEEE754 and would otherwise republish every tick while a sensor error persists).
- **One-directional dependency**: `task_mqtt → task_valve`/`task_sensors`/`task_time`/`errors.h`, never the reverse — `task_valve` has no `#include "task_mqtt.h"` and doesn't know MQTT exists, consistent with every other module boundary in this codebase.

WiFi is now brought up whenever `task_mqtt` is running (i.e. always, in normal mode) — this replaced the old `otaTaskDisableWifi()`, which no longer exists.

**Power tuning** (`mqttTaskBegin()`): `WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 5)` — unlike Modem Sleep (the ESP8266 core's default whenever the loop calls `delay()`, ~15mA), Light Sleep also gates the CPU clock between DTIM wake windows, not just the radio (~2mA), while keeping the connection up. `listenInterval=5` trades HA command latency (sub-second to ~1s on a typical AP) for lower average draw — tune down toward 1-3 first if the connection ever proves flaky on a given AP, rather than disabling it outright. `WiFi.setOutputPower(19.0f)` trims TX power to roughly 70% of actual radiated power (dBm is logarithmic — 70% of the raw *20.5 dBm figure* would be a much deeper ~27%-power cut); safe given the device sits on the home LAN with no range requirement. `PubSubClient::setKeepAlive(60)` (default 15s) cuts idle keepalive ping frequency 4x — safe because real state changes already publish immediately regardless of keepalive timing, this only governs the idle-connection heartbeat.

**Deliberately not touched**: `LOOP_DELAY_MS` (250ms, in `nebulizadorv3.ino`). It's tempting to raise it too, but it's a different sleep domain from the WiFi-level `listenInterval` above — the tick rate is bounded by `task_led`'s blink resolution (`BLINK_MS`=250ms; a coarser tick turns "blinks" into slow flashes and makes error-code burst counts harder to read) and, more importantly, by `task_valve`'s manual/automatic valve-active timing (`valveTaskSetValveActiveTimeMs()` allows configuring as low as 500ms — a tick slower than that means the valve could stay open for a full extra tick beyond what was requested, since the close check only runs once per tick). Whether Light Sleep actually engages doesn't depend on how long `delay()` runs, only on it running at all in station mode — so 250ms already gets the full benefit without those downsides.

## Git history note

This repository has diverged branches with substantially different firmware architectures (an older Arduino UNO "caso"-table design vs. this proportional-control D1 mini rewrite). If you're comparing against old commits or other branches, don't assume their logic, pin maps, or file layout apply here — this file describes the code as it exists on the current branch only.
