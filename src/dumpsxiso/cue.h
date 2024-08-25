#pragma once

#include "cdreader.h"
#include <fstream>
#include <vector>

struct TrackInfo {
	std::string fileName;
	std::string fileType;
	std::string number;
	std::string type;
	std::string startTime;
	unsigned int startSector;
	unsigned int sizeInSectors;
	unsigned int endSector;
};

struct CueFile {
	bool multiBIN = false;
	unsigned int totalSectors = 0;
	std::vector<TrackInfo> tracks;
};

CueFile parseCueFile(std::filesystem::path& filePath);
bool multiBinSeeker(const unsigned int sector, const cd::IsoDirEntries::Entry &entry, cd::IsoReader &reader, const CueFile &cueFile);
