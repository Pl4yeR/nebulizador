#include "task_time.h"

#include <TZ.h>
#include <coredecls.h> // settimeofday_cb()

// Overridable from platformio.ini build_flags without touching config.h
// (which holds credentials, not locale settings).
#ifndef TIME_TZ
#define TIME_TZ TZ_Europe_Madrid
#endif

namespace {
volatile bool s_synced = false;
bool s_loggedSync = false;

// settimeofday_cb runs deferred (next yield()/loop()), in normal task
// context, not an ISR — but we still keep it to just flipping a flag and let
// timeTaskLoop() do the logging, matching every other task's pattern of only
// producing side effects from their Loop() function.
void onTimeSet(bool fromSntp) {
  if (fromSntp)
    s_synced = true;
}
} // namespace

// Weak function defined by the ESP8266 core (default: 1 hour); overriding it
// here makes the 60-minute re-sync requirement explicit in code rather than
// relying on an undocumented default.
extern "C" uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() { return 60UL * 60UL * 1000UL; }

void timeTaskBegin() {
  settimeofday_cb(onTimeSet);
  configTime(TIME_TZ, "pool.ntp.org", "time.nist.gov");
}

void timeTaskLoop(unsigned long now) {
  (void)now;
  if (!s_synced || s_loggedSync)
    return;

  s_loggedSync = true;
  time_t nowEpoch = time(nullptr);
  struct tm tmVal;
  localtime_r(&nowEpoch, &tmVal);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmVal);
  Serial.print(F("[TIME] Synced: "));
  Serial.println(buf);
}

bool timeTaskIsSynced() { return s_synced; }

int timeTaskMinutesOfDay() {
  if (!s_synced)
    return -1;

  time_t nowEpoch = time(nullptr);
  struct tm tmVal;
  localtime_r(&nowEpoch, &tmVal);
  return tmVal.tm_hour * 60 + tmVal.tm_min;
}

bool timeTaskFormatEpochUtc(time_t epoch, char *out, size_t len) {
  if (!s_synced)
    return false;

  struct tm tmVal;
  gmtime_r(&epoch, &tmVal);
  strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &tmVal);
  return true;
}
