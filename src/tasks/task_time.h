#pragma once
#include <Arduino.h>
#include <time.h>

// Starts SNTP time sync (non-blocking; resolves once WiFi is up). Call once
// from setup(), normal-mode path only. Re-syncs automatically every 60 min
// (see sntp_update_delay_MS_rfc_not_less_than_15000() override in the .cpp).
void timeTaskBegin();

// Logs the transition to synced, once. Call every normal-mode loop tick.
void timeTaskLoop(unsigned long now);

// True from the first successful SNTP sync onward. If this is never true,
// the device has no real clock — callers must fall back to "always allowed"
// behavior (see task_valve's schedule check) rather than treating it as an
// error condition.
bool timeTaskIsSynced();

// Local time as minutes since midnight (0-1439). Only meaningful if
// timeTaskIsSynced() is true.
int timeTaskMinutesOfDay();

// Formats a UTC epoch as ISO 8601 ("YYYY-MM-DDTHH:MM:SSZ") into out (must be
// at least 21 bytes). Returns false (and leaves out untouched) if !timeTaskIsSynced().
bool timeTaskFormatEpochUtc(time_t epoch, char *out, size_t len);
