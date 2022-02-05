#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "cd.h"
#include "xa.h"
#include "common.h"
#include "cdreader.h"
#include "platform.h"
#include <string.h>
#include <stdlib.h>

cd::IsoReader::IsoReader()
{
}

cd::IsoReader::~IsoReader()
{
    if (filePtr != NULL)
		fclose(filePtr);

}


bool cd::IsoReader::Open(const fs::path& fileName)
{
	Close();

    filePtr = OpenFile(fileName, "rb");

    if (filePtr == NULL)
		return(false);

	fseek(filePtr, 0, SEEK_END);
	totalSectors = ftell(filePtr) / CD_SECTOR_SIZE;
	fseek(filePtr, 0, SEEK_SET);

    fread(sectorBuff, CD_SECTOR_SIZE, 1, filePtr);

    currentByte		= 0;
    currentSector	= 0;

    sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return(true);

}

size_t cd::IsoReader::ReadBytes(void* ptr, size_t bytes, bool singleSector)
{
	size_t bytesRead = 0;
    char* const dataPtr = (char*)ptr;
	constexpr size_t DATA_SIZE = sizeof(sectorM2F1->data);

    while(bytes > 0)
	{
		const size_t toRead = std::min(DATA_SIZE - currentByte, bytes);

        memcpy(dataPtr+bytesRead, &sectorM2F1->data[currentByte], toRead);

		currentByte += toRead;
		bytesRead += toRead;
		bytes -= toRead;

		if (currentByte >= DATA_SIZE)
		{
			if (singleSector || !PrepareNextSector())
			{
				return bytesRead;
			}
		}
    }

    return bytesRead;

}

size_t cd::IsoReader::ReadBytesXA(void* ptr, size_t bytes, bool singleSector)
{
	size_t bytesRead = 0;
    char* const dataPtr = (char*)ptr;
	constexpr size_t DATA_SIZE = sizeof(sectorM2F2->data);

    while(bytes > 0)
	{
		const size_t toRead = std::min(DATA_SIZE - currentByte, bytes);

        memcpy(dataPtr+bytesRead, &sectorM2F2->data[currentByte], toRead);

		currentByte += toRead;
		bytesRead += toRead;
		bytes -= toRead;

		if (currentByte >= DATA_SIZE)
		{
			if (singleSector || !PrepareNextSector())
			{
				return bytesRead;
			}
		}
    }

    return(bytesRead);

}

size_t cd::IsoReader::ReadBytesDA(void* ptr, size_t bytes, bool singleSector)
{
	size_t bytesRead = 0;
    char* const dataPtr = (char*)ptr;
	constexpr size_t DATA_SIZE = sizeof(sectorBuff);

    while(bytes > 0)
	{
		const size_t toRead = std::min(DATA_SIZE - currentByte, bytes);

        memcpy(dataPtr+bytesRead, &sectorBuff[currentByte], toRead);

		currentByte += toRead;
		bytesRead += toRead;
		bytes -= toRead;

		if (currentByte >= DATA_SIZE)
		{
			if (singleSector || !PrepareNextSector())
			{
				return bytesRead;
			}
		}
    }

	return bytesRead;

}

void cd::IsoReader::SkipBytes(size_t bytes, bool singleSector) {

	constexpr size_t DATA_SIZE = sizeof(sectorM2F1->data);

    while(bytes > 0) {

        const size_t toRead = std::min(DATA_SIZE - currentByte, bytes);

		currentByte += toRead;
		bytes -= toRead;

		if (currentByte >= DATA_SIZE) {

            if (singleSector || !PrepareNextSector())
			{
				return;
			}
		}
    }
}

int cd::IsoReader::SeekToSector(int sector) {

	if (sector >= totalSectors)
		return -1;

    fseek(filePtr, CD_SECTOR_SIZE*sector, SEEK_SET);
	fread(sectorBuff, CD_SECTOR_SIZE, 1, filePtr);

	currentSector = sector;
	currentByte = 0;

	sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return ferror(filePtr);

}

size_t cd::IsoReader::SeekToByte(size_t offs) {

	int sector = (offs/CD_SECTOR_SIZE);

	fseek(filePtr, CD_SECTOR_SIZE*sector, SEEK_SET);
    fread(sectorBuff, CD_SECTOR_SIZE, 1, filePtr);

	currentSector = sector;
	currentByte = offs%CD_SECTOR_SIZE;

	sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return (CD_SECTOR_SIZE*static_cast<size_t>(currentSector))+currentByte;

}

size_t cd::IsoReader::GetPos() const
{
    return (CD_SECTOR_SIZE*static_cast<size_t>(currentSector))+currentByte;
}

void cd::IsoReader::Close() {

    if (filePtr != NULL) {
		fclose(filePtr);
		filePtr = NULL;
    }

}

bool cd::IsoReader::PrepareNextSector()
{
	currentByte = 0;

    if (fread(sectorBuff, CD_SECTOR_SIZE, 1, filePtr) != 1)
	{
		return false;
    }
            
    currentSector++;

    sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
	sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;
	return true;
}


void cd::IsoPathTable::FreePathTable()
{
	pathTableList.clear();
}

size_t cd::IsoPathTable::ReadPathTable(cd::IsoReader* reader, int lba)
{
	if (lba >= 0)
		reader->SeekToSector(lba);

	FreePathTable();

	while (true)
	{
		Entry pathTableEntry;
		reader->ReadBytes(&pathTableEntry.entry, sizeof(pathTableEntry.entry));

		// Its the end of the path table when its nothing but zeros
		if (pathTableEntry.entry.nameLength == 0)
			break;


		// Read entry name
		{
			const size_t length = pathTableEntry.entry.nameLength;
			pathTableEntry.name.resize(length);
			reader->ReadBytes(pathTableEntry.name.data(), length);

			// ECMA-119 9.4.6 - 00 field present only if entry length is an odd number
			if ((length % 2) != 0)
			{
				reader->SkipBytes(1);
			}

			// Strip trailing zeroes, if any
			pathTableEntry.name.resize(strlen(pathTableEntry.name.c_str()));
		}

		pathTableList.emplace_back(std::move(pathTableEntry));
	}

	return pathTableList.size();

}

fs::path cd::IsoPathTable::GetFullDirPath(int dirEntry) const
{
	fs::path path;

	while (true)
	{
		if (pathTableList[dirEntry].name.empty())
			break;

		// Prepend!
		path = pathTableList[dirEntry].name / path;

        dirEntry = pathTableList[dirEntry].entry.parentDirIndex-1;
	}

	return path;
}

cd::IsoDirEntries::IsoDirEntries(ListView<Entry> view)
	: dirEntryList(std::move(view))
{
}

void cd::IsoDirEntries::ReadDirEntries(cd::IsoReader* reader, int lba, int sectors)
{
	size_t numEntries = 0; // Used to skip the first two entries, . and ..
    for (int sec = 0; sec < sectors; sec++)
    {
        reader->SeekToSector(lba + sec);
		while (true)
		{
			auto entry = ReadEntry(reader);
			if (!entry)
			{
				// Either end of the table, or end of sector
				break;
			}

			if (numEntries++ >= 2)
			{
				dirEntryList.emplace(std::move(entry.value()));
			}
		}
    }

	// Sort the directory by LBA for pretty printing
	dirEntryList.SortView([](const auto& left, const auto& right)
		{
			return left.get().entry.entryOffs.lsb < right.get().entry.entryOffs.lsb;
		});
}

std::optional<cd::IsoDirEntries::Entry> cd::IsoDirEntries::ReadEntry(cd::IsoReader* reader) const
{
	Entry entry;

	// Read 33 byte directory entry
	size_t bytesRead = reader->ReadBytes(&entry.entry, sizeof(entry.entry), true);

	// The file entry table usually ends with null bytes so break if we reached that area
	if (bytesRead != sizeof(entry.entry) || entry.entry.entryLength == 0)
		return std::nullopt;

	// Read identifier string
	entry.identifier.resize(entry.entry.identifierLen);
	reader->ReadBytes(entry.identifier.data(), entry.entry.identifierLen, true);

	if (entry.identifier == "ST0D_00D.BIN;1")
	{
		int i = 0;
	}

	// Strip trailing zeroes, if any
	entry.identifier.resize(strlen(entry.identifier.c_str()));

	// ECMA-119 9.1.12 - 00 field present only if file identifier length is an even number
	if ((entry.entry.identifierLen % 2) == 0)
    {
        reader->SkipBytes(1);
    }

	// Read XA attribute data
	reader->ReadBytes(&entry.extData, sizeof(entry.extData), true);

	// XA attributes are big endian, swap them
	entry.extData.attributes = SwapBytes16(entry.extData.attributes);
	entry.extData.ownergroupid = SwapBytes16(entry.extData.ownergroupid);
	entry.extData.owneruserid = SwapBytes16(entry.extData.owneruserid);

	return entry;
}

void cd::IsoDirEntries::ReadRootDir(cd::IsoReader* reader, int lba)
{
	reader->SeekToSector(lba);
	auto entry = ReadEntry(reader);
	if (entry)
	{
		dirEntryList.emplace(std::move(entry.value()));
	}
}