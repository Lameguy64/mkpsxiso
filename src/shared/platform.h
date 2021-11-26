#pragma once

#include <filesystem>
#include <cstdio>
#include <cstdint>
#include <optional>

// Printf format for std::filesystem::path::c_str()
#if defined(_WIN32)
#define PRFILESYSTEM_PATH "ws"
#else
#define PRFILESYSTEM_PATH "s"
#endif

FILE* OpenFile(const std::filesystem::path& path, const char* mode);
std::optional<struct _stat64> Stat(const std::filesystem::path& path);
int64_t GetSize(const std::filesystem::path& path);