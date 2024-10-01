#include "common.h"
#include "cue.h"
#include "platform.h"

bool multiBinSeeker(const unsigned int sector, const cd::IsoDirEntries::Entry &entry, cd::IsoReader &reader, const CueFile &cueFile) {
	unsigned trackIndex = (entry.trackid.empty() ? std::stoi(entry.identifier.substr(8, 2)) : std::stoi(entry.trackid)) - 1;
	reader.Open(cueFile.tracks[trackIndex].fileName);
	return reader.SeekToSector(sector - cueFile.tracks[trackIndex - 1].endSector);
}

CueFile parseCueFile(std::filesystem::path& filePath) {
	std::ifstream file(filePath);
	std::string line, fileName, fileType;
	CueFile cueFile;
	unsigned int previousStartSector = 0;
	unsigned int pauseStartSector = 1;

	while (std::getline(file, line)) {
		if (line.find("FILE") != std::string::npos) {

			if (!cueFile.tracks.empty()) {
				TrackInfo &lastTrack = cueFile.tracks.back();
				lastTrack.sizeInSectors = cueFile.totalSectors - lastTrack.startSector;
				lastTrack.endSector = lastTrack.startSector + lastTrack.sizeInSectors;
			}

			size_t firstQuote = line.find("\"");
			size_t lastQuote = line.rfind("\"");
			fileName = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
			if (GetSize(fileName) < 2352) {
				printf("Error: Failed to get the file size for \"%s\"", fileName.c_str());
				exit(EXIT_FAILURE);
			}
			cueFile.totalSectors += GetSize(fileName) / CD_SECTOR_SIZE;
			cueFile.multiBIN = true;

			if (line.find("BINARY") != std::string::npos) {
				fileType = "BINARY";
			}
			else {
				fileType = "UNKNOWN";
			}

			if (cueFile.tracks.empty()) {
				filePath.replace_filename(fileName);
				cueFile.multiBIN = false;
			}
		}
		else if (line.find("TRACK") != std::string::npos) {
			TrackInfo track;
			size_t trackNumStart = line.find("TRACK") + 6;
			track.number = line.substr(trackNumStart, 2);
			track.fileName = fileName;
			track.fileType = fileType;

			if (line.find("AUDIO") != std::string::npos) {
				track.type = "AUDIO";
			}
			else if (line.find("MODE2/2352") != std::string::npos) {
				track.type = "MODE2/2352";
			}
			else {
				track.type = "UNKNOWN";
			}

			cueFile.tracks.push_back(track);
		}
		else if (line.find("INDEX 00") != std::string::npos) {
			size_t timeStart = line.find("INDEX 00") + 9;
			pauseStartSector = TimecodeToSectors(line.substr(timeStart, 8));

			if (pauseStartSector) {
				cueFile.tracks[cueFile.tracks.size() - 2].sizeInSectors = pauseStartSector - previousStartSector;
				cueFile.tracks[cueFile.tracks.size() - 2].endSector = pauseStartSector;
			}
		}
		else if (line.find("INDEX 01") != std::string::npos) {
			size_t timeStart = line.find("INDEX 01") + 9;
			std::string startTime = line.substr(timeStart, 8);
			unsigned int startSector = TimecodeToSectors(startTime);

			if (!pauseStartSector) {
				startSector = cueFile.tracks[cueFile.tracks.size() - 2].endSector + startSector;
				startTime = SectorsToTimecode(startSector);
			}

			cueFile.tracks.back().startTime = startTime;
			cueFile.tracks.back().startSector = startSector;
			previousStartSector = startSector;
		}
	}

	if (!cueFile.tracks.empty()) {
		TrackInfo &lastTrack = cueFile.tracks.back();
		lastTrack.sizeInSectors = cueFile.totalSectors - lastTrack.startSector;
		lastTrack.endSector = lastTrack.startSector + lastTrack.sizeInSectors;
	}

	return cueFile;
}
