#pragma once

// GPIO assignments for the Wemos D1 mini (ESP8266).
// Override from platformio.ini via build_flags, e.g.:
//   build_flags = -D DHTPIN=D6 -D SOLENOID_PIN=D2

// Defaults below are the real, confirmed factory pins for this project's
// actual shields: DHT11 -> D4, SHT30 -> SCL=D1/SDA=D2, Relay -> D1 (SCL_PIN
// and SOLENOID_PIN collide by nature — you can't stack the Relay Shield with
// an I2C shield using only factory pins). DHTPIN=D4 also collides with
// LED_BUILTIN (same GPIO2) — see the comment on LED_BUILTIN sharing at the
// bottom of this file. None of this matters here: this project resolves the
// actual wiring per build via platformio.ini build_flags, not by picking
// artificial non-colliding defaults in this header.

#ifndef DHTPIN
#define DHTPIN D4 // DHT11 shield default. Also LED_BUILTIN (GPIO2) — see note below.
#endif

// I2C bus — matches the Wemos SHT30 Shield's factory default.
#ifndef SDA_PIN
#define SDA_PIN D2
#endif
#ifndef SCL_PIN
#define SCL_PIN D1 // Also SOLENOID_PIN below (Relay Shield default) — override in build_flags.
#endif

#ifndef SOLENOID_PIN
#define SOLENOID_PIN D1 // Relay Shield default (MOSFET trigger module doubles as the "relay" here)
#endif

// The status LED is the board's built-in LED (GPIO2 / D4, active-low) — not
// redefined here since it's fixed by the board, not a wiring choice.
//
// When DHTPIN=D4 (its default above), the DHT11 data line and the built-in
// LED share GPIO2. This is survivable but not free: task_led.cpp reclaims
// pinMode(LED_BUILTIN, OUTPUT) every tick to undo whatever the DHT read left
// the pin in (see the comment there for why that's necessary) — don't remove
// that reclaim, and expect a brief LED flicker during each DHT11 read.
