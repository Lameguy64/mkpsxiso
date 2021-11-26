#include "platform.h"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#include <string>
#include <vector>
#endif

#include <sys/stat.h>

#if defined(_WIN32)
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
#endif

FILE* OpenFile(const std::filesystem::path& path, const char* mode)
{
#if defined(_WIN32)
	FILE* file = nullptr;
	_wfopen_s(&file, path.c_str(), UTF8ToUTF16(mode).c_str());
	return file;
#else
	return ::fopen(path.c_str(), mode);
#endif
}

std::optional<struct _stat64> Stat(const std::filesystem::path& path)
{
	struct _stat64 fileAttrib;
#if defined(_WIN32)
	if (_wstat64(path.c_str(), &fileAttrib) != 0)
#else
    if (_stat64(path.c_str(), &fileAttrib) != 0)
#endif
	{
		return std::nullopt;
	}

	return fileAttrib;
}

int64_t GetSize(const std::filesystem::path& path)
{
	auto fileAttrib = Stat(path);
	return fileAttrib.has_value() ? fileAttrib->st_size : -1;
}

extern int Main(int argc, const char* argv[]);

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
{
	std::vector<std::string> u8Arguments;
	u8Arguments.reserve(argc);
	for (int i = 0; i < argc; ++i)
	{
		u8Arguments.emplace_back(UTF16ToUTF8(argv[i]));
	}

	std::vector<const char*> u8argv;
	u8Arguments.reserve(argc + 1);
	for (std::string& str : u8Arguments)
	{
		u8argv.emplace_back(str.c_str());
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