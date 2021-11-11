#include "cd.h"
#include <iterator>
#include <memory>

using namespace cd;

static void snprintfZeroPad(char* s, size_t n, const char* format, ...)
{
	// We need a temporary buffer that is 1 byte bigger than the specified one,
	// then memcpy without the null terminator/pad with zeroes
	auto buf = std::make_unique<char[]>(n + 1);

	va_list args;
	va_start(args, format);

	const int bytesWritten = vsnprintf(buf.get(), n + 1, format, args);
	memcpy(s, buf.get(), bytesWritten);
	std::fill(s + bytesWritten, s + n, '\0');

	va_end(args);
}

ISO_LONG_DATESTAMP GetLongDateFromDate(const ISO_DATESTAMP& src)
{
	ISO_LONG_DATESTAMP result;

	snprintfZeroPad(result.year, std::size(result.year), "%03d", 1900 + src.year);
	snprintfZeroPad(result.month, std::size(result.month), "%02d", src.month);
	snprintfZeroPad(result.day, std::size(result.day), "%02d", src.day);
	snprintfZeroPad(result.hour, std::size(result.hour), "%02d", src.hour);
	snprintfZeroPad(result.minute, std::size(result.minute), "%02d", src.minute);
	snprintfZeroPad(result.second, std::size(result.second), "%02d", src.second);
	strncpy(result.hsecond, "00", std::size(result.hsecond));
	result.GMToffs = src.GMToffs;

	return result;
}

ISO_LONG_DATESTAMP GetUnspecifiedLongDate()
{
	ISO_LONG_DATESTAMP result;

	strncpy(result.year, "0000", std::size(result.year));
	strncpy(result.month, "00", std::size(result.month));
	strncpy(result.day, "00", std::size(result.day));
	strncpy(result.hour, "00", std::size(result.hour));
	strncpy(result.minute, "00", std::size(result.minute));
	strncpy(result.second, "00", std::size(result.second));
	strncpy(result.hsecond, "00", std::size(result.hsecond));
	result.GMToffs = 0;

	return result;
}