#include "platform.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#else
#include <fcntl.h>
#endif

#ifdef _WIN32
static std::wstring UTF8ToUTF16(std::string_view str)
{
	std::wstring result;
	const int count = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), nullptr, 0);
	if (count != 0)
	{
		result.resize(count);
		MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), result.data(), count);
	}
	return result;
}

static std::string UTF16ToUTF8(std::wstring_view str)
{
	std::string result;
	int count = WideCharToMultiByte(CP_UTF8, 0, str.data(), str.length(), nullptr, 0, nullptr, nullptr);
	if (count != 0)
	{
		result.resize(count);
		WideCharToMultiByte(CP_UTF8, 0, str.data(), str.length(), result.data(), count, nullptr, nullptr);
	}
	return result;
}

static FILETIME TimetToFileTime(time_t t)
{
	FILETIME ft;
	LARGE_INTEGER ll;
	ll.QuadPart = t * 10000000ll + 116444736000000000ll;
	ft.dwLowDateTime = ll.LowPart;
	ft.dwHighDateTime = ll.HighPart;
	return ft;
}

static time_t FileTimeToTimet(const FILETIME& ft)
{
	LARGE_INTEGER ll;
	ll.LowPart = ft.dwLowDateTime;
	ll.HighPart = ft.dwHighDateTime;
	return (ll.QuadPart / 10000000LL) - 11644473600LL;
}
#endif

FILE* OpenFile(const fs::path& path, const char* mode)
{
#ifdef _WIN32
	return _wfopen(path.c_str(), UTF8ToUTF16(mode).c_str());
#else
	return fopen(path.c_str(), mode);
#endif
}

std::optional<struct stat64> Stat(const fs::path& path)
{
	struct stat64 fileAttrib;
#ifdef _WIN32
	// Windows _wstat64 can't handle timestamps prior to 1970
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
	{
		fileAttrib.st_size = ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
		fileAttrib.st_mtime = FileTimeToTimet(fad.ftLastWriteTime);
	}
	else
#else
	if (stat64(path.c_str(), &fileAttrib) != 0)
#endif
	{
		return std::nullopt;
	}

	return fileAttrib;
}

int64_t GetSize(const fs::path& path)
{
	auto fileAttrib = Stat(path);
	return fileAttrib.has_value() ? fileAttrib->st_size : -1;
}

// Returns local UTC `struct tm` from UTC+0 `time_t`
struct tm CustomLocalTime(const time_t* timeSec)
{
#ifdef _WIN32
	using namespace std::chrono;
	tm timeBuf {};

	// Convert time_t to sys_time (compatible with std::chrono)
	auto tp = system_clock::from_time_t(*timeSec - SYSTEM_TIMEZONE);

	// Break down the time into year/month/day/hour/minute/second
	auto num_of_days = floor<days>(tp);
	auto time_of_day = tp - num_of_days; // Time within the day
	auto ymd = year_month_day{num_of_days};
	auto h = duration_cast<hours>(time_of_day);
	auto m = duration_cast<minutes>(time_of_day - h);
	auto s = duration_cast<seconds>(time_of_day - h - m);

	// Populate the struct tm fields
	timeBuf.tm_year = (int)ymd.year() - 1900;
	timeBuf.tm_mon = (unsigned int)ymd.month() - 1;
	timeBuf.tm_mday = (unsigned int)ymd.day();
	timeBuf.tm_hour = h.count();
	timeBuf.tm_min = m.count();
	timeBuf.tm_sec = s.count();

	return timeBuf;
#else
	return *localtime(timeSec);
#endif
}

// Returns UTC+0 `time_t` from local UTC `struct tm`
time_t CustomMkTime(struct tm* timeBuf)
{
#ifdef _WIN32
	using namespace std::chrono;

	// Create sys_days for date and add time components
	auto chronoTime = sys_days{year{timeBuf->tm_year + 1900} /
								   (timeBuf->tm_mon + 1) /
									timeBuf->tm_mday} +
							  hours{timeBuf->tm_hour} +
							minutes{timeBuf->tm_min} +
							seconds{timeBuf->tm_sec};

	return system_clock::to_time_t(chronoTime) + SYSTEM_TIMEZONE;
#else
	return mktime(timeBuf);
#endif
}

void UpdateTimestamps(const fs::path& path, const cd::ISO_DATESTAMP& entryDate)
{
	tm timeBuf {};
	timeBuf.tm_year = entryDate.year;
	timeBuf.tm_mon = entryDate.month - 1;
	timeBuf.tm_mday = entryDate.day;
	timeBuf.tm_hour = entryDate.hour;
	timeBuf.tm_min = entryDate.minute;
	timeBuf.tm_sec = entryDate.second;
	const time_t time = CustomMkTime(&timeBuf);

// utime can't update timestamps of directories on Windows, so a platform-specific approach is needed
#ifdef _WIN32
	HANDLE hFile = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		const FILETIME ft = TimetToFileTime(time);
		if(0 == SetFileTime(hFile, &ft, nullptr, &ft))
		{
			printf("ERROR: unable to update timestamps for %ls\n", path.c_str());
		}

		CloseHandle(hFile);
	}
#else
	struct timespec times[2];
	times[0].tv_nsec = UTIME_OMIT;

	times[1].tv_sec = time;
	times[1].tv_nsec = 0;
	if(0 != utimensat(AT_FDCWD, path.c_str(), times, 0))
	{
		printf("ERROR: unable to update timestamps for %s\n", path.c_str());
	}
#endif
}

extern int Main(int argc, char* argv[]);

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
	std::vector<std::string> u8Arguments;
	u8Arguments.reserve(argc);
	for (int i = 0; i < argc; ++i)
	{
		u8Arguments.emplace_back(UTF16ToUTF8(argv[i]));
	}

	std::vector<char*> u8argv;
	u8Arguments.reserve(argc + 1);
	for (std::string& str : u8Arguments)
	{
		u8argv.emplace_back(str.data());
	}
	u8argv.emplace_back(nullptr);

	return Main(argc, u8argv.data());
}
#else
int main(int argc, char* argv[])
{
	return Main(argc, argv);
}
#endif
