#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "cd.h"
#include "cdreader.h"
#include <string.h>
#include <stdlib.h>

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


bool cd::IsoReader::Open(const char* fileName) {

	cd::IsoReader::Close();

    cd::IsoReader::filePtr = fopen(fileName, "rb");

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


cd::IsoPathTable::IsoPathTable() {

	cd::IsoPathTable::numPathEntries = 0;
	cd::IsoPathTable::pathTableList = NULL;

}

cd::IsoPathTable::~IsoPathTable() {

	cd::IsoPathTable::FreePathTable();

}

void cd::IsoPathTable::FreePathTable() {

	if (cd::IsoPathTable::pathTableList != NULL) {

        for(int i = cd::IsoPathTable::numPathEntries-1; i>=0; i--) {
            free(cd::IsoPathTable::pathTableList[i].name);
        }

        free(cd::IsoPathTable::pathTableList);

	}

	cd::IsoPathTable::numPathEntries = 0;

}

int cd::IsoPathTable::ReadPathTable(cd::IsoReader* reader, int lba) {

	if (lba >= 0)
		reader->SeekToSector(lba);

	if (cd::IsoPathTable::pathTableList != NULL)
		cd::IsoPathTable::FreePathTable();

	while(1) {

		cd::ISO_PATHTABLE_ENTRY	pathTableEntry;
		reader->ReadBytes(&pathTableEntry, 8);

		// Its the end of the path table when its nothing but zeros
		if (pathTableEntry.nameLength == 0)
			break;


		// Read entry name
		if (pathTableEntry.nameLength) {

			pathTableEntry.name = (char*)calloc(1, pathTableEntry.nameLength+1);
			reader->ReadBytes(pathTableEntry.name, 2*((pathTableEntry.nameLength+1)/2));

		}


		// Allocate or reallocate path table list to make way for the next entry
		if (cd::IsoPathTable::pathTableList == NULL) {

			// Initial allocation
			cd::IsoPathTable::pathTableList = (cd::ISO_PATHTABLE_ENTRY*)calloc(sizeof(cd::ISO_PATHTABLE_ENTRY), 1);

		} else {

			// Append
			cd::IsoPathTable::pathTableList = (cd::ISO_PATHTABLE_ENTRY*)realloc(
				cd::IsoPathTable::pathTableList, sizeof(cd::ISO_PATHTABLE_ENTRY)*(cd::IsoPathTable::numPathEntries+1)
			);

		}

		// Transfer path table entry buffer to new path table entry in list
		cd::IsoPathTable::pathTableList[cd::IsoPathTable::numPathEntries] = pathTableEntry;
		cd::IsoPathTable::numPathEntries++;

	}

	return(cd::IsoPathTable::numPathEntries);

}

int cd::IsoPathTable::GetFullDirPath(int dirEntry, char* pathBuff, int pathMaxLen) {

	memset(pathBuff, 0x00, pathMaxLen);

	int dirEntryOrderCount=0;
    int dirEntryOrder[32];

	int dirEntryNum = dirEntry;

	while(1) {

		if (cd::IsoPathTable::pathTableList[dirEntryNum].name[0] == 0x00)
			break;

		dirEntryOrder[dirEntryOrderCount] = dirEntryNum;
		dirEntryOrderCount++;

        dirEntryNum = cd::IsoPathTable::pathTableList[dirEntryNum].dirLevel-1;

	}

    for(int i=dirEntryOrderCount-1; i>=0; i--) {

		char* dirName = cd::IsoPathTable::pathTableList[dirEntryOrder[i]].name;

        if ((strlen(pathBuff)+strlen(dirName)+1) > (unsigned int)pathMaxLen)
			break;

		strcat(pathBuff, dirName);

		if (i > 0)
			strcat(pathBuff, "/");

    }

	return(strlen(pathBuff));

}

cd::IsoDirEntries::IsoDirEntries() {

	cd::IsoDirEntries::numDirEntries = 0;
	cd::IsoDirEntries::dirEntryList = NULL;

}

cd::IsoDirEntries::~IsoDirEntries() {

	cd::IsoDirEntries::FreeDirEntries();

}

void cd::IsoDirEntries::FreeDirEntries() {

	if (cd::IsoDirEntries::dirEntryList != NULL) {

        for(int i=cd::IsoDirEntries::numDirEntries-1; i>=0; i--) {

            free(cd::IsoDirEntries::dirEntryList[i].identifier);

            if (cd::IsoDirEntries::dirEntryList[i].extData != NULL)
				free(cd::IsoDirEntries::dirEntryList[i].extData);

        }

        free(cd::IsoDirEntries::dirEntryList);

        cd::IsoDirEntries::dirEntryList = NULL;
        cd::IsoDirEntries::numDirEntries = 0;

	}

}

int cd::IsoDirEntries::ReadDirEntries(cd::IsoReader* reader, int lba, int sectors) {

	cd::IsoDirEntries::FreeDirEntries();
    for (int sec = 0; sec < sectors; sec++)
    {
        size_t sectorBytesRead = 0;
        reader->SeekToSector(lba +sec);
		while(1)
		{
			cd::ISO_DIR_ENTRY dirEntry;
			cd::ISO_XA_ATTRIB dirXAentry;

            //check if there is enough data to read in the current sector. In case there is not, we must move to next sector.
			if (2048 - sectorBytesRead < 33)
                break;

			// Read 33 byte directory entry
			sectorBytesRead += reader->ReadBytes(&dirEntry, 33);

			// The file entry table usually ends with null bytes so break if we reached that area
			if (dirEntry.entryLength == 0)
				break;

			// Read identifier string
			dirEntry.identifier = (char*)calloc(dirEntry.identifierLen+1, 1);
			sectorBytesRead += reader->ReadBytes(dirEntry.identifier, dirEntry.identifierLen);

			// Skip padding byte if identifier string length is even
			if ((dirEntry.identifierLen%2) == 0)
            {
                reader->SkipBytes(1);
                sectorBytesRead++;
            }

			// Read XA attribute data
			sectorBytesRead += reader->ReadBytes(&dirXAentry, 14);


			// Allocate or reallocate directory entry list to make way for the next entry
			if (cd::IsoDirEntries::dirEntryList == NULL) {

				// Initial allocation
				cd::IsoDirEntries::dirEntryList = (cd::ISO_DIR_ENTRY*)calloc(sizeof(cd::ISO_DIR_ENTRY), 1);

			} else {

				// Append
				cd::IsoDirEntries::dirEntryList = (cd::ISO_DIR_ENTRY*)realloc(
					cd::IsoDirEntries::dirEntryList, sizeof(cd::ISO_DIR_ENTRY)*(cd::IsoDirEntries::numDirEntries+1)
				);

				cd::IsoDirEntries::dirEntryList[cd::IsoDirEntries::numDirEntries].extData = NULL;

			}

			cd::IsoDirEntries::dirEntryList[cd::IsoDirEntries::numDirEntries] = dirEntry;
			cd::IsoDirEntries::dirEntryList[cd::IsoDirEntries::numDirEntries].extData = malloc(sizeof(cd::ISO_XA_ATTRIB));
			memcpy(cd::IsoDirEntries::dirEntryList[cd::IsoDirEntries::numDirEntries].extData, &dirXAentry, sizeof(cd::ISO_XA_ATTRIB));
			cd::IsoDirEntries::numDirEntries++;
		}
    }
	return(cd::IsoDirEntries::numDirEntries);

}

void cd::IsoDirEntries::SortByLBA() {

    ISO_DIR_ENTRY temp;

    for(int i=2; i<numDirEntries; i++) {
        for(int j=2; j<numDirEntries-1; j++) {

            if (dirEntryList[j].entryOffs.lsb > dirEntryList[j+1].entryOffs.lsb) {

                temp = dirEntryList[j];
                dirEntryList[j] = dirEntryList[j+1];
                dirEntryList[j+1] = temp;

            }

        }
    }

}
