#include "platform.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
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
	ll.QuadPart = (t * 10000000LL) + 116444736000000000LL;
	ft.dwLowDateTime = ll.LowPart;
	ft.dwHighDateTime = ll.HighPart;
	return ft;
}

static time_t FileTimeToTimet(const FILETIME& ft)
{
	LARGE_INTEGER ll;
	ll.LowPart = ft.dwLowDateTime;
	ll.HighPart = ft.dwHighDateTime;
	return (ll.QuadPart - 116444736000000000LL) / 10000000LL;
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
		fileAttrib.st_size = ULARGE_INTEGER{fad.nFileSizeLow, fad.nFileSizeHigh}.QuadPart;
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
{ // Windows localtime() can't handle timestamps prior to 1970
#ifdef _WIN32
	tm timeBuf {};
	SYSTEMTIME localST {};
	FILETIME localFT = TimetToFileTime(*timeSec - SYSTEM_TIMEZONE);
	FileTimeToSystemTime(&localFT, &localST);

	timeBuf.tm_year = localST.wYear - 1900;
	timeBuf.tm_mon  = localST.wMonth - 1;
	timeBuf.tm_mday = localST.wDay;
	timeBuf.tm_hour = localST.wHour;
	timeBuf.tm_min  = localST.wMinute;
	timeBuf.tm_sec  = localST.wSecond;

	return timeBuf;
#else
	return *localtime(timeSec);
#endif
}

// Returns UTC+0 `time_t` from local UTC `struct tm`
time_t CustomMkTime(struct tm* timeBuf)
{ // Windows mktime() can't handle timestamps prior to 1970
#ifdef _WIN32
	FILETIME localFT;
	SYSTEMTIME localST
		{
			(WORD)(timeBuf->tm_year + 1900),
			(WORD)(timeBuf->tm_mon + 1), 0,
			(WORD)timeBuf->tm_mday,
			(WORD)timeBuf->tm_hour,
			(WORD)timeBuf->tm_min,
			(WORD)timeBuf->tm_sec
		};

	SystemTimeToFileTime(&localST, &localFT);

	return FileTimeToTimet(localFT) + SYSTEM_TIMEZONE;
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
	SetConsoleOutputCP(CP_UTF8);

	return Main(argc, u8argv.data());
}
#else
int main(int argc, char* argv[])
{
	return Main(argc, argv);
}
#endif
