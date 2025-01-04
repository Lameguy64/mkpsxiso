#include "common.h"
#include "platform.h"
#include <iterator>
#include <memory>
#include <cstring>
#include <cstdarg>

using namespace cd;

ISO_DATESTAMP GetDateFromString(const char* str, bool* success)
{
	bool succeeded = false;

	ISO_DATESTAMP result {};

	unsigned int year;
	const int argsRead = sscanf( str, "%4u%2hhu%2hhu%2hhu%2hhu%2hhu%*2u%hhd",
		&year, &result.month, &result.day,
		&result.hour, &result.minute, &result.second, &result.GMToffs );
	if (argsRead >= 6)
	{
		result.year = year >= 1900 ? year - 1900 : 0;
		if (argsRead < 7)
		{
			// Consider GMToffs optional
			result.GMToffs = 36;
		}
		succeeded = true;
	}

	if (success != nullptr)
	{
		*success = succeeded;
	}
	return result;
}

ISO_LONG_DATESTAMP GetLongDateFromString(const char* str)
{
	ISO_LONG_DATESTAMP result {};

	if (str) {
		unsigned int year, month, day, hour, minute, second, hsecond {};

		const int argsRead = sscanf( str, "%4u%2u%2u%2u%2u%2u%2u%hhd",
			&year, &month, &day, &hour, &minute, &second, &hsecond, &result.GMToffs );

		if (argsRead >= 6) {
			if (argsRead < 8) {
				// Consider GMToffs optional
				result.GMToffs = 36;
			}

			auto intToChars = [](unsigned int src, char* dest, unsigned int size) {
				for (int i = size - 1; i >= 0; --i) {
					dest[i] = '0' + (src % 10);
					src /= 10;
				}
			};

			intToChars(year, result.year, 4);
			intToChars(month, result.month, 2);
			intToChars(day, result.day, 2);
			intToChars(hour, result.hour, 2);
			intToChars(minute, result.minute, 2);
			intToChars(second, result.second, 2);
			intToChars(hsecond, result.hsecond, 2);

			return result;
		}
	}

	return GetUnspecifiedLongDate();
}

ISO_LONG_DATESTAMP GetUnspecifiedLongDate()
{
	ISO_LONG_DATESTAMP result;

	memset(&result, '0', sizeof(result));
	result.GMToffs = 0;

	return result;
}

std::string LongDateToString(const cd::ISO_LONG_DATESTAMP& src)
{
	// Interpret ISO_LONG_DATESTAMP as 16 characters, manually write out GMT offset
	const char* srcStr = reinterpret_cast<const char*>(&src);

	std::string result(srcStr, srcStr+16);

	char GMTbuf[4];
	snprintf(GMTbuf, sizeof(GMTbuf), "%+hhd", src.GMToffs);
	result.append(GMTbuf);

	return result;
}

uint32_t GetSizeInSectors(uint64_t size, uint32_t sectorSize)
{
	return size ? static_cast<uint32_t>((size + (sectorSize - 1)) / sectorSize) : 1;
}

int32_t TimecodeToSectors(const std::string timecode)
{
	unsigned int minutes, seconds, frames;
	if (sscanf(timecode.c_str(), "%u:%u:%u", &minutes, &seconds, &frames) != 3 || (minutes > INT_MAX) || (seconds > 59) || (frames > 74)) {
		return -1;
	}
	return (minutes * 60 + seconds) * 75 + frames;
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

std::string CleanIdentifier(std::string_view id)
{
	std::string result(id.substr(0, id.find_last_of(';')));
	return result;
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
	if (ParseArgument(argv, command, longCommand))
	{
		if (*(argv+1) != nullptr && **(argv+1) != '-')
		{
			argv++;
		}
		return *argv;
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
