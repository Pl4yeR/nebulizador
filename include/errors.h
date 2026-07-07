#pragma once
#include <stdint.h>

// Bitmask of active device errors. task_led derives a blink count from the
// lowest set bit (bit N => N+2 blinks per 5s cycle, so bit 0 is the minimum
// of 2) and displays it instead of the normal heartbeat. Add new error
// sources as additional bits as they come up.
namespace ErrorFlags {
constexpr uint8_t NONE = 0x00;
constexpr uint8_t DHT = 0x01; // DHT11 read failed (task_sensors)
} // namespace ErrorFlags

uint8_t getErrors();
void setError(uint8_t flags);
void clearError(uint8_t flags);
