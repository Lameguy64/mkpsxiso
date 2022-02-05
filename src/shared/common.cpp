#include "common.h"
#include "platform.h"
#include <iterator>
#include <memory>
#include <cstring>
#include <cstdarg>

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

	snprintfZeroPad(result.year, std::size(result.year), "%04d", src.year != 0 ? 1900 + src.year : 0);
	snprintfZeroPad(result.month, std::size(result.month), "%02d", src.month);
	snprintfZeroPad(result.day, std::size(result.day), "%02d", src.day);
	snprintfZeroPad(result.hour, std::size(result.hour), "%02d", src.hour);
	snprintfZeroPad(result.minute, std::size(result.minute), "%02d", src.minute);
	snprintfZeroPad(result.second, std::size(result.second), "%02d", src.second);
	strncpy(result.hsecond, "00", std::size(result.hsecond));
	result.GMToffs = src.GMToffs;

	return result;
}

ISO_DATESTAMP GetDateFromString(const char* str, bool* success)
{
	bool succeeded = false;

	ISO_DATESTAMP result {};

	short int year;
	const int argsRead = sscanf( str, "%04hd%02hhu%02hhu%02hhu%02hhu%02hhu%*02u%hhd",
		&year, &result.month, &result.day,
		&result.hour, &result.minute, &result.second, &result.GMToffs );
	if (argsRead >= 6)
	{
		result.year = year != 0 ? year - 1900 : 0;
		if (argsRead < 7)
		{
			// Consider GMToffs optional
			result.GMToffs = 0;
		}
		succeeded = true;
	}

	if (success != nullptr)
	{
		*success = succeeded;
	}
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

std::string LongDateToString(const cd::ISO_LONG_DATESTAMP& src)
{
	// Interpret ISO_LONG_DATESTAMP as 16 characters, manually write out GMT offset
	const char* srcStr = reinterpret_cast<const char*>(&src);

	std::string result(srcStr, srcStr+16);

	char GMTbuf[8];
	sprintf(GMTbuf, "%+hhd", src.GMToffs);
	result.append(GMTbuf);

	return result;
}

uint32_t GetSizeInSectors(uint64_t size, uint32_t sectorSize)
{
	return static_cast<uint32_t>((size + (sectorSize - 1)) / sectorSize);
}

std::string SectorsToTimecode(const unsigned sectors)
{
	char timecode[16];
	snprintf( timecode, sizeof(timecode), "%02u:%02u:%02u", (sectors/75)/60, (sectors/75)%60, sectors%75);
	return std::string(timecode);
}	                

unsigned short SwapBytes16(unsigned short val)
{
	return  ((val & 0xFF) << 8) |
			((val & 0xFF00) >> 8); 
}

unsigned int SwapBytes32(unsigned int val)
{
	return  ((val & 0xFF) << 24) |
			((val & 0xFF00) << 8) |
			((val & 0xFF0000) >> 8) |
			((val & 0xFF000000) >> 24);
}

unique_file OpenScopedFile(const fs::path& path, const char* mode)
{
	return unique_file { OpenFile(path, mode) };
}

bool CompareICase(std::string_view strLeft, std::string_view strRight)
{
	return std::equal(strLeft.begin(), strLeft.end(), strRight.begin(), strRight.end(), [](char left, char right)
		{
			return left == right || std::tolower(left) == std::tolower(right);
		});
}

bool ParseArgument(char** argv, std::string_view command, std::string_view longCommand)
{
	const std::string_view arg(*argv);
	// Try the long command first, case insensitively
	if (!longCommand.empty() && arg.length() > 2 && arg[0] == '-' && arg[1] == '-' && CompareICase(arg.substr(2), longCommand))
	{
		return true;
	}
	
	// Short commands are case sensitive
	if (!command.empty() && arg.length() > 1 && arg[0] == '-' && arg.substr(1) == command)
	{
		return true;
	}
	return false;
}

std::optional<fs::path> ParsePathArgument(char**& argv, std::string_view command, std::string_view longCommand)
{
	if (ParseArgument(argv, command, longCommand) && *(argv+1) != nullptr)
	{
		argv++;
		return fs::u8path(*argv);
	}
	return std::nullopt;
}

std::optional<std::string> ParseStringArgument(char**& argv, std::string_view command, std::string_view longCommand)
{
	if (ParseArgument(argv, command, longCommand) && *(argv+1) != nullptr)
	{
		argv++;
		return std::string(*argv);
	}
	return std::nullopt;
}
