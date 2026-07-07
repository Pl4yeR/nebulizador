#pragma once
#include <Arduino.h>

// Configures the built-in LED pin and runs the boot blink sequence. Call once from setup().
void ledTaskBegin();

// Switches the LED into fast-blink OTA mode. OTA mode is a dead end (see
// task_ota), so there's no corresponding "unset" — only a reboot leaves it.
void ledTaskSetOtaMode();

// Reports valve state so the LED can hold solid on while misting. Takes
// priority over the heartbeat/error display, but not over OTA mode.
void ledTaskSetValveActive(bool active);

// Advances the LED's blink state machine. Must be called on every loop tick
// (including inside task_ota's blocking OTA loop) for blinking to work.
void ledTaskLoop(unsigned long now);
