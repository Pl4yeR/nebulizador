#pragma once

// GPIO assignments for the Wemos D1 mini (ESP8266).
// Override from platformio.ini via build_flags, e.g.:
//   build_flags = -D DHTPIN=D6 -D SOLENOID_PIN=D2

// Defaults below are the real, confirmed factory pins for this project's
// actual shields: DHT11 -> D4, SHT30 -> SCL=D1/SDA=D2, Relay -> D1 (SCL_PIN
// and SOLENOID_PIN collide by nature — you can't stack the Relay Shield with
// an I2C shield using only factory pins). This project resolves the actual
// wiring per build via platformio.ini build_flags, not by picking artificial
// non-colliding defaults in this header.

#ifndef DHTPIN
// D4 is the DHT11 shield's factory pin, but it's ALSO LED_BUILTIN (GPIO2) and
// the two cannot coexist: any LED blink holds the line LOW >18ms, which the
// DHT11 interprets as a read start signal (phantom reads + bus contention —
// see CLAUDE.md "Pin configuration" for the full investigation). A
// static_assert in task_sensors_dht.cpp refuses to build with this collision,
// so this factory default is documentation, not a usable fallback: the shield
// must be physically rewired and DHTPIN overridden in platformio.ini (this
// project rewires it to D5).
#define DHTPIN D4
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
// redefined here since it's fixed by the board, not a wiring choice. It owns
// GPIO2 exclusively; see the DHTPIN note above for why sharing it with the
// DHT11 is rejected at compile time.
