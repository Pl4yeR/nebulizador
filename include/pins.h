#pragma once

// GPIO assignments for the Wemos D1 mini (ESP8266).
// Override from platformio.ini via build_flags, e.g.:
//   build_flags = -D DHTPIN=D6 -D SOLENOID_PIN=D2

#ifndef DHTPIN
#define DHTPIN D5 // DHT11 temperature/humidity sensor data pin (env:d1_mini only)
#endif

// I2C bus for the SHT30 temperature/humidity sensor (env:d1_mini_sht30 only).
// Not the conventional ESP8266 I2C default (SDA=D2/SCL=D1) — D1 is already
// SOLENOID_PIN below, so SCL was moved to D6 instead.
#ifndef SDA_PIN
#define SDA_PIN D2
#endif
#ifndef SCL_PIN
#define SCL_PIN D6
#endif

#ifndef SOLENOID_PIN
#define SOLENOID_PIN D1 // Solenoid valve control pin (via MOSFET trigger module)
#endif

// The status LED is the board's built-in LED (GPIO2 / D4, active-low) —
// not redefined here since it's fixed by the board, not a wiring choice.
