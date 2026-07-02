#include "task_led.h"

namespace {
constexpr uint32_t OTA_BLINK_MS = 100; // Fast blink half-period in OTA mode

LedMode s_mode = LedMode::Idle;
bool s_ledOn = false;
unsigned long s_lastToggle = 0;
} // namespace

void ledTaskBegin() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Active-low: HIGH = off

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(150);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(150);
  }
}

void ledTaskSetMode(LedMode mode) {
  if (mode == s_mode)
    return;
  s_mode = mode;

  if (mode == LedMode::ValveActive) {
    s_ledOn = true;
    digitalWrite(LED_BUILTIN, LOW);
  } else if (mode == LedMode::Idle) {
    s_ledOn = false;
    digitalWrite(LED_BUILTIN, HIGH);
  }
  // Ota mode is driven by ledTaskLoop(); leave the current LED state as-is
  // until the next toggle so the transition doesn't visibly glitch.
}

void ledTaskLoop(unsigned long now) {
  if (s_mode != LedMode::Ota)
    return;

  if (now - s_lastToggle >= OTA_BLINK_MS) {
    s_lastToggle = now;
    s_ledOn = !s_ledOn;
    digitalWrite(LED_BUILTIN, s_ledOn ? LOW : HIGH);
  }
}
