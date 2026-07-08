#include "heat_index.h"

#include <math.h>

namespace {
float celsiusToFahrenheit(float c) { return c * 9.0f / 5.0f + 32.0f; }

float fahrenheitToCelsius(float f) { return (f - 32.0f) * 5.0f / 9.0f; }
} // namespace

float computeHeatIndexC(float temperatureC, float humidityPercent) {
  float t = celsiusToFahrenheit(temperatureC);
  float rh = humidityPercent;

  float hi = 0.5f * (t + 61.0f + ((t - 68.0f) * 1.2f) + (rh * 0.094f));

  if (hi > 79.0f) {
    hi = -42.379f + 2.04901523f * t + 10.14333127f * rh + -0.22475541f * t * rh + -0.00683783f * t * t +
         -0.05481717f * rh * rh + 0.00122874f * t * t * rh + 0.00085282f * t * rh * rh +
         -0.00000199f * t * t * rh * rh;

    if ((rh < 13.0f) && (t >= 80.0f) && (t <= 112.0f)) {
      hi -= ((13.0f - rh) * 0.25f) * sqrtf((17.0f - fabsf(t - 95.0f)) * 0.05882f);
    } else if ((rh > 85.0f) && (t >= 80.0f) && (t <= 87.0f)) {
      hi += ((rh - 85.0f) * 0.1f) * ((87.0f - t) * 0.2f);
    }
  }

  return fahrenheitToCelsius(hi);
}
