#pragma once

#include "cd.h"
#include <filesystem>
#include <memory>
#include <string>

enum class EntryType
{
	EntryFile,
	EntryDir,
	EntryXA,
	EntryXA_DO,
	EntryDA,
	EntryDummy
};

// Helper functions for datestamp manipulation
cd::ISO_DATESTAMP GetDateFromString(const char* str, bool* success = nullptr);

cd::ISO_LONG_DATESTAMP GetLongDateFromDate(const cd::ISO_DATESTAMP& src);
cd::ISO_LONG_DATESTAMP GetUnspecifiedLongDate();
std::string LongDateToString(const cd::ISO_LONG_DATESTAMP& src);

uint32_t GetSizeInSectors(uint64_t size, uint32_t sectorSize = 2048);

// Endianness swap
unsigned short SwapBytes16(unsigned short val);
unsigned int SwapBytes32(unsigned int val);

// Scoped helpers for a few resources
struct file_deleter
{
	void operator()(FILE* file) const
	{
		if (file != nullptr)
		{
			std::fclose(file);
		}
	}
};
using unique_file = std::unique_ptr<FILE, file_deleter>;
unique_file OpenScopedFile(const std::filesystem::path& path, const char* mode);