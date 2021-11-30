#ifdef _WIN32
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
#include <algorithm>

cd::IsoReader::IsoReader() {

	cd::IsoReader::filePtr			= NULL;
    cd::IsoReader::currentByte		= 0;
    cd::IsoReader::currentSector	= 0;
	cd::IsoReader::totalSectors		= 0;

}

cd::IsoReader::~IsoReader() {

    if (cd::IsoReader::filePtr != NULL)
		fclose(cd::IsoReader::filePtr);

}


bool cd::IsoReader::Open(const std::filesystem::path& fileName) {

	cd::IsoReader::Close();

    cd::IsoReader::filePtr = OpenFile(fileName, "rb");

    if (cd::IsoReader::filePtr == NULL)
		return(false);

	fseek(cd::IsoReader::filePtr, 0, SEEK_END);
	cd::IsoReader::totalSectors = ftell(cd::IsoReader::filePtr) / CD_SECTOR_SIZE;
	fseek(cd::IsoReader::filePtr, 0, SEEK_SET);

    fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr);

    cd::IsoReader::currentByte		= 0;
    cd::IsoReader::currentSector	= 0;

    cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return(true);

}

size_t cd::IsoReader::ReadBytes(void* ptr, size_t bytes) {

	size_t	bytesRead = 0;
    char*  	dataPtr = (char*)ptr;
    int		toRead;

    while(bytes > 0) {

        if (bytes > 2048)
			toRead = 2048;
		else
			toRead = bytes;

        memcpy(dataPtr, &cd::IsoReader::sectorM2F1->data[cd::IsoReader::currentByte], toRead);
		cd::IsoReader::currentByte += toRead;
		dataPtr += toRead;
		bytes -= toRead;

		if (cd::IsoReader::currentByte >= 2048) {

            if (fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr) == 0) {
				cd::IsoReader::currentByte = 0;
				return(bytesRead);
            }

            cd::IsoReader::currentByte = 0;
            cd::IsoReader::currentSector++;

            cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		bytesRead += toRead;

    }

    return(bytesRead);

}

size_t cd::IsoReader::ReadBytesXA(void* ptr, size_t bytes) {

	size_t	bytesRead = 0;
    char*  	dataPtr = (char*)ptr;
    int		toRead;

    while(bytes > 0) {

        if (bytes > 2336)
			toRead = 2336;
		else
			toRead = bytes;

        memcpy(dataPtr, &cd::IsoReader::sectorM2F2->data[cd::IsoReader::currentByte], toRead);

		cd::IsoReader::currentByte += toRead;
		dataPtr += toRead;
		bytes -= toRead;

		if (cd::IsoReader::currentByte >= 2336) {

            if (fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr) == 0) {
				cd::IsoReader::currentByte = 0;
				return(bytesRead);
            }

            cd::IsoReader::currentByte = 0;
            cd::IsoReader::currentSector++;

            cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		bytesRead += toRead;

    }

    return(bytesRead);

}

size_t cd::IsoReader::ReadBytesDA(void* ptr, size_t bytes) {

	size_t	bytesRead = 0;
    char*  	dataPtr = (char*)ptr;
    int		toRead;

    while(bytes > 0) {

        if (bytes > 2352)
			toRead = 2352;
		else
			toRead = bytes;

        memcpy(dataPtr, &cd::IsoReader::sectorBuff[cd::IsoReader::currentByte], toRead);

		cd::IsoReader::currentByte += toRead;
		dataPtr += toRead;
		bytes -= toRead;

		if (cd::IsoReader::currentByte >= 2352) {

            if (fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr) == 0) {
				cd::IsoReader::currentByte = 0;
				return(bytesRead);
            }

            cd::IsoReader::currentByte = 0;
            cd::IsoReader::currentSector++;

            cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		bytesRead += toRead;

    }

	return(bytesRead);

}

void cd::IsoReader::SkipBytes(size_t bytes) {

	size_t	bytesRead = 0;
    int		toRead;

    while(bytes > 0) {

        if (bytes > 2048)
			toRead = 2048;
		else
			toRead = bytes;

		cd::IsoReader::currentByte += toRead;
		bytes -= toRead;

		if (currentByte >= 2048) {

            if (fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr) == 0) {
				cd::IsoReader::currentByte = 0;
				return;
            }

            cd::IsoReader::currentByte = 0;
            cd::IsoReader::currentSector++;

			cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		bytesRead += toRead;

    }

    return;

}

int cd::IsoReader::SeekToSector(int sector) {

	if (sector >= cd::IsoReader::totalSectors)
		return -1;

    fseek(cd::IsoReader::filePtr, CD_SECTOR_SIZE*sector, SEEK_SET);
	fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr);

	cd::IsoReader::currentSector = sector;
	cd::IsoReader::currentByte = 0;

	cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return ferror(cd::IsoReader::filePtr);

}

size_t cd::IsoReader::SeekToByte(size_t offs) {

	int sector = (offs/CD_SECTOR_SIZE);

	fseek(cd::IsoReader::filePtr, CD_SECTOR_SIZE*sector, SEEK_SET);
    fread(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoReader::filePtr);

	cd::IsoReader::currentSector = sector;
	cd::IsoReader::currentByte = offs%CD_SECTOR_SIZE;

	cd::IsoReader::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    cd::IsoReader::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return( (CD_SECTOR_SIZE*cd::IsoReader::currentSector)+cd::IsoReader::currentByte );

}

size_t cd::IsoReader::GetPos() {

    return( (CD_SECTOR_SIZE*cd::IsoReader::currentSector)+cd::IsoReader::currentByte );

}

void cd::IsoReader::Close() {

    if (cd::IsoReader::filePtr != NULL) {
		fclose(cd::IsoReader::filePtr);
		cd::IsoReader::filePtr = NULL;
    }

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

std::filesystem::path cd::IsoPathTable::GetFullDirPath(int dirEntry) const
{
	std::filesystem::path path;

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
        size_t sectorBytesRead = 0;
        reader->SeekToSector(lba + sec);
		while (true)
		{
            //check if there is enough data to read in the current sector. In case there is not, we must move to next sector.
			if (2048 - sectorBytesRead < sizeof(Entry))
			{
                break;
			}

			auto entry = ReadEntry(reader, &sectorBytesRead);
			if (!entry)
			{
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

std::optional<cd::IsoDirEntries::Entry> cd::IsoDirEntries::ReadEntry(cd::IsoReader* reader, size_t* bytesRead) const
{
	Entry entry;

	// Read 33 byte directory entry
	size_t read = reader->ReadBytes(&entry.entry, sizeof(entry.entry));

	// The file entry table usually ends with null bytes so break if we reached that area
	if (entry.entry.entryLength == 0)
		return std::nullopt;

	// Read identifier string
	entry.identifier.resize(entry.entry.identifierLen);
	read += reader->ReadBytes(entry.identifier.data(), entry.entry.identifierLen);

	// Strip trailing zeroes, if any
	entry.identifier.resize(strlen(entry.identifier.c_str()));

	// ECMA-119 9.1.12 - 00 field present only if file identifier length is an even number
	if ((entry.entry.identifierLen % 2) == 0)
    {
        reader->SkipBytes(1);
        read++;
    }

	// Read XA attribute data
	read += reader->ReadBytes(&entry.extData, sizeof(entry.extData));

	// XA attributes are big endian, swap them
	entry.extData.attributes = SwapBytes16(entry.extData.attributes);
	entry.extData.ownergroupid = SwapBytes16(entry.extData.ownergroupid);
	entry.extData.owneruserid = SwapBytes16(entry.extData.owneruserid);

	if (bytesRead != nullptr)
	{
		*bytesRead += read;
	}

	return entry;
}

void cd::IsoDirEntries::ReadRootDir(cd::IsoReader* reader, int lba)
{
	reader->SeekToSector(lba);
	auto entry = ReadEntry(reader, nullptr);
	if (entry)
	{
		dirEntryList.emplace(std::move(entry.value()));
	}
}