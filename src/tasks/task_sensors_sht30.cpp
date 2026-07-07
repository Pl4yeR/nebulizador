#include "task_sensors.h"

#include <SHT31.h>
#include <Wire.h>

#include "errors.h"
#include "heat_index.h"
#include "pins.h"

namespace {
SHT31 s_sht30;

// SHT31::requestData()/dataReady()/readData() split the ~15ms measurement
// into a fire-and-poll sequence instead of a single blocking read() call —
// dataReady() is a pure millis() check, no I2C traffic, so waiting for it
// never blocks the cooperative main loop.
enum class ReadState { Idle, Measuring };
ReadState s_state = ReadState::Idle;

float s_humidity = NAN;
float s_temperature = NAN;
float s_heatIndex = NAN;
unsigned long s_lastReadTime = 0; // Time the last completed read cycle started
} // namespace

void sensorsTaskBegin() {
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!s_sht30.begin()) {
    Serial.println(F("[SENSORS] SHT30 not responding at startup"));
  }
}

void sensorsTaskLoop(unsigned long now, unsigned long intervalMs) {
  if (s_state == ReadState::Idle) {
    if (now - s_lastReadTime < intervalMs)
      return;

    Serial.println(F("[SENSORS] Requesting SHT30 measurement..."));
    if (!s_sht30.requestData()) {
      Serial.println(F("[SENSORS] Failed to request SHT30 measurement!"));
      s_humidity = s_temperature = s_heatIndex = NAN;
      setError(ErrorFlags::SENSOR);
      s_lastReadTime = now;
      return;
    }

    s_state = ReadState::Measuring;
    return;
  }

  // ReadState::Measuring — poll without blocking; ready ~15ms after the request.
  if (!s_sht30.dataReady())
    return;

  s_lastReadTime = now;
  s_state = ReadState::Idle;

  if (!s_sht30.readData(false)) {
    Serial.println(F("[SENSORS] Failed to read SHT30 data!"));
    s_humidity = s_temperature = s_heatIndex = NAN;
    setError(ErrorFlags::SENSOR);
    return;
  }

  s_temperature = s_sht30.getTemperature();
  s_humidity = s_sht30.getHumidity();

  if (isnan(s_temperature) || isnan(s_humidity)) {
    Serial.println(F("[SENSORS] SHT30 returned an invalid reading!"));
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
