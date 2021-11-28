#pragma once

#include "cd.h"
#include <string>

// Helper functions for datestamp manipulation
cd::ISO_DATESTAMP GetDateFromString(const char* str, bool* success = nullptr);

cd::ISO_LONG_DATESTAMP GetLongDateFromDate(const cd::ISO_DATESTAMP& src);
cd::ISO_LONG_DATESTAMP GetUnspecifiedLongDate();
std::string LongDateToString(const cd::ISO_LONG_DATESTAMP& src);

// Endianness swap
unsigned short SwapBytes16(unsigned short val);
unsigned int SwapBytes32(unsigned int val);
