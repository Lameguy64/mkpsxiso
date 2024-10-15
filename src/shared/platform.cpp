#include "platform.h"
#include "cd.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#endif

#include <fcntl.h>

#include <string>
#include <vector>

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

static time_t FileTimeToTimet(const FILETIME& ft) {
	LARGE_INTEGER ll;
	ll.LowPart = ft.dwLowDateTime;
	ll.HighPart = ft.dwHighDateTime;
	return (ll.QuadPart / 10000000LL) - 11644473600LL;
}

HANDLE HandleFile(const fs::path& path) {
	HANDLE hFile = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile != INVALID_HANDLE_VALUE) {
		return hFile;
	}
	return nullptr;
}
#endif

FILE* OpenFile(const fs::path& path, const char* mode)
{
#ifdef _WIN32
	FILE* file = nullptr;
	_wfopen_s(&file, path.c_str(), UTF8ToUTF16(mode).c_str());
	return file;
#else
	return ::fopen(path.c_str(), mode);
#endif
}

std::optional<struct stat64> Stat(const fs::path& path)
{
	struct stat64 fileAttrib;
#ifdef _WIN32
	if (_wstat64(path.c_str(), &fileAttrib) != 0)
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

// Determines if a year is a leap year
bool IsLeapYear(int year) {
	return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

// Calculates the number of days in a given month
int DaysInMonth(int month, int year) {
	static const int month_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return (month == 1 && IsLeapYear(year)) ? 29 : month_days[month];
}

// Converts struct tm to time_t
time_t CustoMkTime(struct tm* timebuf) {
#ifdef _WIN32
	int days = 0;

	// Calculates the number of days from 1900 to a given date
	for (int y = 1900; y < timebuf->tm_year + 1900; ++y) {
		days += IsLeapYear(y) ? 366 : 365;
	}
	for (int m = 0; m < timebuf->tm_mon; ++m) {
		days += DaysInMonth(m, timebuf->tm_year + 1900);
	}

	// Calculate days from 1900 to the given date minus days from 1900 to 1970
	int days_since_epoch = days + timebuf->tm_mday - 1 - 25567;

	// Convert days and time to seconds
	time_t total_seconds = static_cast<time_t>(days_since_epoch) * 86400 + timebuf->tm_hour * 3600 + timebuf->tm_min * 60 + timebuf->tm_sec;

	// Adjust total_seconds to UTC 0
	const time_t now = time(nullptr);
	total_seconds -= (now - mktime(gmtime(&now)));

	return total_seconds;
#else
	return mktime(timebuf);
#endif
}

// Converts time_t to struct tm
struct tm CustomLocalTime(time_t seconds) {
#ifdef _WIN32
	struct tm timebuf = {0};
	const time_t now = time(nullptr);
	seconds += (now - mktime(gmtime(&now)));

	// Calculate the number of days since the epoch
	int remaining_seconds = seconds % 86400;
	if (remaining_seconds < 0) {
		remaining_seconds += 86400;
	}
	int total_days = (seconds - remaining_seconds) / 86400 + 25567; // 25567 = days from 1900 to 1970

	// Calculate year
	int year = 1900;
	while (true) {
		int days_in_year = IsLeapYear(year) ? 366 : 365;
		if (total_days < days_in_year) {
			break;
		}
		total_days -= days_in_year;
		++year;
	}
	timebuf.tm_year = year - 1900;

	// Calculate month
	int month = 0;
	while (true) {
		int days_in_current_month = DaysInMonth(month, year);
		if (total_days < days_in_current_month) {
			break;
		}
		total_days -= days_in_current_month;
		++month;
	}
	timebuf.tm_mon = month;

	// Calculate days
	timebuf.tm_mday = total_days + 1;

	// Calculate hour, minute, second
	timebuf.tm_hour = remaining_seconds / 3600;
	remaining_seconds %= 3600;
	timebuf.tm_min = remaining_seconds / 60;
	timebuf.tm_sec = remaining_seconds % 60;
	timebuf.tm_isdst = 0;

	return timebuf;
#else
	return *localtime(&seconds);
#endif
}

bool GetSrcTime(const fs::path& path, time_t& outTime) {
#ifdef _WIN32
	if (HANDLE hFile = HandleFile(path)) {
		FILETIME ftCreation, ftLastAccess, ftLastWrite;
		if (!GetFileTime(hFile, &ftCreation, &ftLastAccess, &ftLastWrite)) {
			CloseHandle(hFile);
			return false;
		}
		CloseHandle(hFile);

		// Convert from 100-nanosecond intervals since January 1, 1601 to seconds since January 1, 1970
		outTime = FileTimeToTimet(ftCreation);
		return true;
	}
	return false;
#else
	if (auto fileAttrib = Stat(path); fileAttrib.has_value()) {
		outTime = fileAttrib->st_mtime;
		return true;
	}
	return false;
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
	const time_t time = CustoMkTime(&timeBuf);

// utime can't update timestamps of directories on Windows, so a platform-specific approach is needed
#ifdef _WIN32
	if (HANDLE hFile = HandleFile(path)) {
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
