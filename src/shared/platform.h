#pragma once

#include <filesystem>
#include <cstdio>
#include <cstdint>
#include <optional>

#include <sys/types.h>
#include <sys/stat.h>

// Printf format for std::filesystem::path::c_str()
#ifdef _WIN32
#define stat64 _stat64
#define PRFILESYSTEM_PATH "ls"
#elif defined(__APPLE__) && defined(__arm64__)
// __DARWIN_ONLY_64_BIT_INO_T is set on ARM-based Macs (which then sets __DARWIN_64_BIT_INO_T).
// This sets the following in Apple SDK's stat.h: struct stat __DARWIN_STRUCT_STAT64;
// So use stat over stat64 for ARM-based Macs
#define stat64 stat
#define PRFILESYSTEM_PATH "s"
#else
#define PRFILESYSTEM_PATH "s"
#endif

namespace cd
{
	struct ISO_DATESTAMP;
}

FILE* OpenFile(const std::filesystem::path& path, const char* mode);
std::optional<struct stat64> Stat(const std::filesystem::path& path);
int64_t GetSize(const std::filesystem::path& path);
void UpdateTimestamps(const std::filesystem::path& path, const cd::ISO_DATESTAMP& entryDate);
