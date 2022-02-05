#pragma once

#include <cstdio>
#include <cstdint>
#include <optional>

#include <sys/types.h>
#include <sys/stat.h>

#include "fs.h"

// Printf format for fs::path::c_str()
#ifdef _WIN32
#define stat64 _stat64
#define PRFILESYSTEM_PATH "ws"
#else
#define PRFILESYSTEM_PATH "s"
#endif

namespace cd
{
	struct ISO_DATESTAMP;
}

FILE* OpenFile(const fs::path& path, const char* mode);
std::optional<struct stat64> Stat(const fs::path& path);
int64_t GetSize(const fs::path& path);
void UpdateTimestamps(const fs::path& path, const cd::ISO_DATESTAMP& entryDate);
