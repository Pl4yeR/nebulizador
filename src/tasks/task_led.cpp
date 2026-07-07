#include "task_led.h"

#include "errors.h"

namespace {
constexpr uint32_t OTA_BLINK_MS = 100;  // OTA mode: fast blink half-period
constexpr uint32_t BLINK_MS = 250;      // Heartbeat/error blink on/off duration
constexpr uint32_t PAUSE_MS = 5000;     // Gap between bursts (heartbeat and error alike)

bool s_otaMode = false;
bool s_valveActive = false;
bool s_ledOn = false;
unsigned long s_lastToggle = 0;
uint8_t s_displayedCode = 0xFF; // Sentinel: forces a burst (re)start on the first tick
uint8_t s_step = 0;             // Step within the current burst
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

void ledTaskSetOtaMode() { s_otaMode = true; }

void ledTaskSetValveActive(bool active) {
  if (active == s_valveActive)
    return;
  s_valveActive = active;

  if (active) {
    s_ledOn = true;
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    // Don't resume a stale mid-burst position — show a clean heartbeat/error
    // cycle from scratch on the next ledTaskLoop() call.
    s_displayedCode = 0xFF;
    s_ledOn = false;
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void ledTaskLoop(unsigned long now) {
  if (s_otaMode) {
    if (now - s_lastToggle >= OTA_BLINK_MS) {
      s_lastToggle = now;
      s_ledOn = !s_ledOn;
      digitalWrite(LED_BUILTIN, s_ledOn ? LOW : HIGH);
    }
    return;
  }

  // Valve open: solid on, takes priority over the heartbeat/error display.
  if (s_valveActive)
    return;

  // Lowest set error bit -> N+2 blinks; no errors -> code 0, a single heartbeat blink.
  uint8_t errors = getErrors();
  uint8_t code = errors == 0 ? 0 : (uint8_t)(__builtin_ctz(errors) + 2);

  // Error code changed (including healthy <-> error transitions): restart the burst now
  // instead of waiting out the rest of the old cycle, so changes show up promptly.
  if (code != s_displayedCode) {
    s_displayedCode = code;
    s_step = 0;
    s_lastToggle = now;
    s_ledOn = false;
    digitalWrite(LED_BUILTIN, HIGH);
  }

  uint8_t blinkCount = (code == 0) ? 1 : code;
  uint8_t blinkSteps = blinkCount * 2; // on+off per blink

  if (s_step < blinkSteps) {
    if (now - s_lastToggle >= BLINK_MS) {
      s_lastToggle = now;
      s_ledOn = (s_step % 2 == 0);
      digitalWrite(LED_BUILTIN, s_ledOn ? LOW : HIGH);
      s_step++;
    }
  } else {
    // Pause between bursts.
    if (s_ledOn) {
      s_ledOn = false;
      digitalWrite(LED_BUILTIN, HIGH);
    }
    if (now - s_lastToggle >= PAUSE_MS) {
      s_lastToggle = now;
      s_step = 0;
    }
  }
}
