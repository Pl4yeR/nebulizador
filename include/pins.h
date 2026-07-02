#pragma once

// GPIO assignments for the Wemos D1 mini (ESP8266).
// Override from platformio.ini via build_flags, e.g.:
//   build_flags = -D DHTPIN=D6 -D SOLENOID_PIN=D2

#ifndef DHTPIN
#define DHTPIN D5 // DHT11 temperature/humidity sensor data pin
#endif

#ifndef SOLENOID_PIN
#define SOLENOID_PIN D1 // Solenoid valve control pin (via MOSFET trigger module)
#endif

// The status LED is the board's built-in LED (GPIO2 / D4, active-low) —
// not redefined here since it's fixed by the board, not a wiring choice.
