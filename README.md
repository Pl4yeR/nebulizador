# Water Nebulization System

## Project Overview
This project implements a water nebulization system on a Wemos D1 mini (ESP8266). A DHT11 sensor measures temperature and humidity; the firmware computes a heat index from those readings and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough, using a non-blocking, proportional-control loop — the hotter it is, the more frequently the valve fires.

The firmware is split into small, single-purpose "task" modules (sensors, valve, status LED, OTA) that are each polled cooperatively from a single `loop()` — see [CLAUDE.md](CLAUDE.md) for why this isn't real FreeRTOS on this chip.

## Components Used
- **Wemos D1 mini (ESP8266)**: The microcontroller that runs the code.
- **DHT11 Sensor**: Measures temperature and humidity.
- **Solenoid Valve**: Controls the flow of water in the nebulization system.
- **MOSFET Trigger Switch Drive Module**: Switches the solenoid valve on and off.
- **Built-in LED**: The only status indicator — mirrors valve state, blinks at boot, and fast-blinks in OTA mode.

## Pin map

| Role | Pin | Notes |
|---|---|---|
| DHT11 data | `D5` (default) | Configurable, see below |
| Solenoid valve control | `D1` (default) | Configurable, see below. Active-high (`HIGH` = valve open) |
| Status LED | `LED_BUILTIN` (`D4` / GPIO2) | Fixed by the board, active-low |

Pin assignments live in [include/pins.h](include/pins.h) as `#ifndef`-guarded macros, so they can be overridden without touching code by setting `build_flags` in [platformio.ini](platformio.ini):

```ini
build_flags =
    -D DHTPIN=D6
    -D SOLENOID_PIN=D2
```

## Setup Instructions

1. **Hardware Connections**:
   - Connect the DHT11 sensor's data pin to `D5` (or your configured `DHTPIN`).
   - Wire the solenoid valve to the MOSFET module, and the module's trigger input to `D1` (or your configured `SOLENOID_PIN`).
2. **WiFi/OTA credentials**: create `include/config.h` (not committed, see `include/.gitignore`):
   ```cpp
   #define WIFI_SSID       "your-ssid"
   #define WIFI_PASSWORD   "your-password"
   #define OTA_AP_PASSWORD "your-ota-password" // min 8 characters
   ```
3. **Build / flash** (PlatformIO):
   ```
   pio run                # compile
   pio run -t upload      # compile + flash
   pio device monitor     # serial monitor, 9600 baud
   ```
   `lib_deps` in `platformio.ini` pulls the DHT sensor library, ElegantOTA and ESP_MultiResetDetector from the PlatformIO registry automatically.

## OTA updates

Normal operation keeps WiFi off. To flash new firmware over the air:

1. Power-cycle the board 3 times within 10 seconds (unplug/replug, or reset button if wired).
2. The board connects to `WIFI_SSID` (15 s timeout, falls back to AP `Nebulizador-OTA-<chipid>` if it can't).
3. Browse to `http://<ip>/update` and upload the new `firmware.bin`, authenticating with user `admin` and your `OTA_AP_PASSWORD`.
4. OTA mode is a dead end — sensors/valve stop, and it stays in OTA mode until reflashed or power-cycled again.

## A tener en cuenta

- La sensación térmica (heat index) se calcula a partir de la temperatura y la humedad relativa, y puede ser distinta de la temperatura medida.
- Si la lectura del DHT11 falla (NaN), el sistema apaga la válvula si estaba activa, no opera y reintenta en el siguiente ciclo (250ms), quedando a la espera de una lectura válida.
- El LED integrado es el único indicador de estado: parpadea 3 veces al arrancar, se enciende mientras la válvula está activa, y parpadea rápido en modo OTA. No hay indicación visual dedicada para errores de sensor — usar el monitor serie para depurar.

## Funcionamiento

- Cada 250ms el `loop()` principal llama a los "tasks" de sensores, válvula, LED y OTA — ver [CLAUDE.md](CLAUDE.md) para el detalle de cada módulo.
- Si la sensación térmica (`hIndex`) es inferior al umbral mínimo (29.8°C), la válvula permanece cerrada y el sistema revisa de nuevo cada 30min (`MAX_FREQUENCY_MS`).
- Si `hIndex` alcanza o supera el umbral, la válvula se abre 5s (`VALVE_ACTIVE_TIME_MS`) y el intervalo hasta la siguiente comprobación se calcula proporcionalmente entre 5min y 30min mediante una curva ease-out: cuanto más calor, más frecuente la nebulización.

## Future Enhancements
- Implement additional sensors for more precise control.
- Integrate data logging for monitoring environmental conditions over time.
