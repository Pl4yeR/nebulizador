#include "task_sensors.h"

#include <DHT.h>

#include "errors.h"
#include "heat_index.h"
#include "pins.h"

// The DHT11 hardware-interprets any >18ms LOW on its data line as a read
// start signal, so sharing its pin with the (active-low, blinking) built-in
// LED causes phantom reads and bus contention — the same reason ESPHome
// refuses "pin used in multiple places". pins.h's bare DHTPIN default is the
// shield's factory pin (D4 == LED_BUILTIN) kept for documentation; building
// requires rewiring the sensor and overriding DHTPIN in platformio.ini.
static_assert(DHTPIN != LED_BUILTIN, "DHTPIN collides with LED_BUILTIN (GPIO2/D4): the DHT11 would treat every LED "
                                     "blink as a read start signal. Rewire the sensor and override DHTPIN in "
                                     "platformio.ini build_flags.");

namespace {
constexpr int DHT_TYPE = DHT11;
DHT s_dht(DHTPIN, DHT_TYPE);

float s_humidity = NAN;
float s_temperature = NAN;
float s_heatIndex = NAN;
unsigned long s_lastReadTime = 0;
} // namespace

void sensorsTaskBegin() {
  s_dht.begin();
  delay(50);
}

void sensorsTaskLoop(unsigned long now, unsigned long intervalMs) {
  if (now - s_lastReadTime < intervalMs)
    return;
  s_lastReadTime = now;

  Serial.println(F("[SENSORS] Reading DHT11..."));

  s_humidity = s_dht.readHumidity();
  s_temperature = s_dht.readTemperature();

  if (isnan(s_humidity) || isnan(s_temperature)) {
    Serial.println(F("[SENSORS] Failed to read from DHT sensor!"));
    s_heatIndex = NAN;
    setError(ErrorFlags::SENSOR);
    return;
  }
  clearError(ErrorFlags::SENSOR);

  s_heatIndex = computeHeatIndexC(s_temperature, s_humidity);

  Serial.print(F("[SENSORS] Humidity: "));
  Serial.print(s_humidity);
  Serial.println(F(" %"));

  Serial.print(F("[SENSORS] Temperature: "));
  Serial.print(s_temperature);
  Serial.println(F(" °C"));

  Serial.print(F("[SENSORS] Heat Index: "));
  Serial.print(s_heatIndex);
  Serial.println(F(" °C"));
}

float sensorsGetHeatIndex() { return s_heatIndex; }

float sensorsGetTemperature() { return s_temperature; }

float sensorsGetHumidity() { return s_humidity; }

bool sensorsReadIsValid() { return !isnan(s_humidity) && !isnan(s_temperature) && !isnan(s_heatIndex); }
