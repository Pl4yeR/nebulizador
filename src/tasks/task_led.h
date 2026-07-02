#pragma once
#include <Arduino.h>

enum class LedMode : uint8_t {
  Idle,        // Valve closed, normal operation
  ValveActive, // Valve open
  Ota,         // OTA mode: fast blink
};

// Configures the built-in LED pin and runs the boot blink sequence. Call once from setup().
void ledTaskBegin();

// Reports the current system state so the LED can reflect it. Cheap, non-blocking.
void ledTaskSetMode(LedMode mode);

// Advances the LED's blink state machine. Must be called on every loop tick
// (including inside task_ota's blocking OTA loop) for blinking to work.
void ledTaskLoop(unsigned long now);
