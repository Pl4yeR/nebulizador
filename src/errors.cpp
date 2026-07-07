#include "errors.h"

namespace {
uint8_t s_errors = ErrorFlags::NONE;
} // namespace

uint8_t getErrors() { return s_errors; }

void setError(uint8_t flags) { s_errors |= flags; }

void clearError(uint8_t flags) { s_errors &= (uint8_t)~flags; }
