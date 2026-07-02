# Water Nebulization System

## Project Overview
This project implements a water nebulization system on a Wemos D1 mini (ESP8266). A DHT11 sensor measures temperature and humidity; the firmware computes a heat index from those readings and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough, using a non-blocking, proportional-control loop — the hotter it is, the more frequently the valve fires.

## Components Used
- **Wemos D1 mini (ESP8266)**: The microcontroller that runs the code.
- **DHT11 Sensor**: Measures temperature and humidity.
- **Solenoid Valve**: Controls the flow of water in the nebulization system.
- **MOSFET Trigger Switch Drive Module**: Switches the solenoid valve on and off.
- **Built-in LED**: The only status indicator — mirrors valve state and blinks at boot.

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
2. **Build / flash** (PlatformIO):
   ```
   pio run                # compile
   pio run -t upload      # compile + flash
   pio device monitor     # serial monitor, 9600 baud
   ```
   `lib_deps` in `platformio.ini` pulls the DHT sensor library + Adafruit Unified Sensor from the PlatformIO registry automatically.

## A tener en cuenta

- La sensación térmica (heat index) se calcula a partir de la temperatura y la humedad relativa, y puede ser distinta de la temperatura medida.
- Si la lectura del DHT11 falla (NaN), el sistema apaga la válvula si estaba activa, no opera y reintenta en el siguiente ciclo (250ms), quedando a la espera de una lectura válida.
- El LED integrado es el único indicador de estado: parpadea 3 veces al arrancar y se enciende mientras la válvula está activa. No hay indicación visual dedicada para errores de sensor — usar el monitor serie para depurar.

## Funcionamiento

- Cada 250ms el sistema comprueba si toca leer los sensores (`readSensorsLoop`) y gestiona el estado de la válvula (`manageValveLoop`).
- Si la sensación térmica (`hIndex`) es inferior a `MIN_HINDEX_THRESHOLD` (29.8°C), la válvula permanece cerrada y el sistema revisa de nuevo cada `MAX_FREQUENCY_MS` (30min).
- Si `hIndex` alcanza o supera el umbral, la válvula se abre durante `VALVE_ACTIVE_TIME_MS` (5s) y el intervalo hasta la siguiente comprobación se calcula proporcionalmente entre `MIN_FREQUENCY_MS` (5min) y `MAX_FREQUENCY_MS` (30min) mediante una curva ease-out: cuanto más calor, más frecuente la nebulización.

## Future Enhancements
- Implement additional sensors for more precise control.
- Integrate data logging for monitoring environmental conditions over time.
