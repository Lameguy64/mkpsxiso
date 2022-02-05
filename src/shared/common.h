#pragma once

#include "cd.h"
#include "fs.h"
#include <memory>
#include <optional>
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

struct cdtrack
{
	cdtrack(unsigned int lba, unsigned int size, std::string source = std::string())
		: lba(lba), size(size), source(std::move(source))
	{
	}

	unsigned int lba;
	unsigned int size;
	std::string source;
};

class EntryAttributes
{
private:
	static constexpr signed char DEFAULT_GMTOFFS = 0;
	static constexpr unsigned char DEFAULT_XAATRIB = 0xFF;
	static constexpr unsigned short DEFAULT_XAPERM = 0x555; // rx
	static constexpr unsigned short	DEFAULT_OWNER_ID = 0;

public:
	signed char GMTOffs = DEFAULT_GMTOFFS;
	unsigned char XAAttrib = DEFAULT_XAATRIB;
	unsigned short XAPerm = DEFAULT_XAPERM;
	unsigned short GID = DEFAULT_OWNER_ID;
	unsigned short UID = DEFAULT_OWNER_ID;
};

// Helper functions for datestamp manipulation
cd::ISO_DATESTAMP GetDateFromString(const char* str, bool* success = nullptr);

cd::ISO_LONG_DATESTAMP GetLongDateFromDate(const cd::ISO_DATESTAMP& src);
cd::ISO_LONG_DATESTAMP GetUnspecifiedLongDate();
std::string LongDateToString(const cd::ISO_LONG_DATESTAMP& src);

uint32_t GetSizeInSectors(uint64_t size, uint32_t sectorSize = 2048);

std::string SectorsToTimecode(const unsigned sectors);

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
unique_file OpenScopedFile(const fs::path& path, const char* mode);

bool CompareICase(std::string_view strLeft, std::string_view strRight);

// Argument parsing
bool ParseArgument(char** argv, std::string_view command, std::string_view longCommand = std::string_view{});
std::optional<fs::path> ParsePathArgument(char**& argv, std::string_view command, std::string_view longCommand = std::string_view{});
std::optional<std::string> ParseStringArgument(char**& argv, std::string_view command, std::string_view longCommand = std::string_view{});
