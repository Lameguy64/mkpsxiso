#include <unistd.h>
#include <string.h>
#include "cdwriter.h"
#include "edcecc.h"


void cd::SwapBytes(void *var, int size) {

	unsigned char temp[size];

	memcpy(temp, var, size);
	for(short i=0; i<size; i++) {
		((unsigned char*)var)[i] = temp[(size-1)-i];
	}

}

void cd::SetPair16(cd::ISO_USHORT_PAIR* pair, unsigned short val) {

    pair->lsb = val;
    pair->msb = val;

    SwapBytes(&pair->msb, 2);

}

void cd::SetPair32(cd::ISO_UINT_PAIR* pair, unsigned int val) {

    pair->lsb = val;
    pair->msb = val;

    SwapBytes(&pair->msb, 4);

}


cd::IsoWriter::IsoWriter() {

	cd::IsoWriter::filePtr			= nullptr;
	cd::IsoWriter::sectorM2F1		= nullptr;
	cd::IsoWriter::sectorM2F2		= nullptr;
	cd::IsoWriter::currentByte		= 0;
	cd::IsoWriter::currentSector	= 0;
	cd::IsoWriter::bytesWritten		= 0;
	cd::IsoWriter::lastSectorType	= 0;

	memset(cd::IsoWriter::sectorBuff, 0, CD_SECTOR_SIZE);

}

cd::IsoWriter::~IsoWriter() {

	if (cd::IsoWriter::filePtr != nullptr)
		fclose(cd::IsoWriter::filePtr);

	cd::IsoWriter::filePtr = nullptr;

}

void cd::IsoWriter::PrepSector(int edcEccMode) {

    memset(cd::IsoWriter::sectorM2F1->sync, 0xff, 12);
    cd::IsoWriter::sectorM2F1->sync[0]	= 0x00;
    cd::IsoWriter::sectorM2F1->sync[11]	= 0x00;

	int taddr,addr = 150+cd::IsoWriter::currentSector;	// Sector addresses always start at LBA 150

	taddr = addr%75;
	cd::IsoWriter::sectorM2F1->addr[2] = (16*(taddr/10))+(taddr%10);

	taddr = (addr/75)%60;
	cd::IsoWriter::sectorM2F1->addr[1] = (16*(taddr/10))+(taddr%10);

	taddr = (addr/75)/60;
	cd::IsoWriter::sectorM2F1->addr[0] = (16*(taddr/10))+(taddr%10);

	cd::IsoWriter::sectorM2F1->mode = 0x02;

	if (edcEccMode == cd::IsoWriter::EdcEccForm1) {

		// Encode EDC data
		edcEccGen.ComputeEdcBlock(cd::IsoWriter::sectorM2F1->subHead, 0x808, cd::IsoWriter::sectorM2F1->edc);

		// Encode ECC data
		unsigned char tempAddr[4];

		for(int i=0; i<4; i++) {

			tempAddr[i] = cd::IsoWriter::sectorM2F1->addr[i];
			cd::IsoWriter::sectorM2F1->addr[i] = 0x00;

		}

		// Compute ECC P code
		edcEccGen.ComputeEccBlock(cd::IsoWriter::sectorBuff+0xC, 86, 24, 2, 86, cd::IsoWriter::sectorBuff+0x81C);
		// Compute ECC Q code
		edcEccGen.ComputeEccBlock(cd::IsoWriter::sectorBuff+0xC, 52, 43, 86, 88, cd::IsoWriter::sectorBuff+0x8C8);

		for(int i=0; i<4; i++) {

			cd::IsoWriter::sectorM2F1->addr[i] = tempAddr[i];

		}

	} else if (edcEccMode == cd::IsoWriter::EdcEccForm2) {

		edcEccGen.ComputeEdcBlock(cd::IsoWriter::sectorM2F1->subHead, 0x91C, &cd::IsoWriter::sectorM2F2->data[2332]);

	}

}

bool cd::IsoWriter::Create(const char* fileName) {

	cd::IsoWriter::filePtr	= fopen(fileName, "wb");

	if (cd::IsoWriter::filePtr == nullptr)
		return(false);

	cd::IsoWriter::currentByte		= 0;
	cd::IsoWriter::currentSector	= 0;

	cd::IsoWriter::sectorM2F1		= (cd::SECTOR_M2F1*)cd::IsoWriter::sectorBuff;
	cd::IsoWriter::sectorM2F2		= (cd::SECTOR_M2F2*)cd::IsoWriter::sectorBuff;

	cd::IsoWriter::bytesWritten		= 0;

	memset(cd::IsoWriter::sectorBuff, 0, CD_SECTOR_SIZE);

	return(true);

}

int cd::IsoWriter::SeekToSector(int sector) {

	if (cd::IsoWriter::currentByte) {

		cd::IsoWriter::PrepSector(cd::IsoWriter::lastSectorType);
		fwrite(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoWriter::filePtr);

	}

	fseek(cd::IsoWriter::filePtr, CD_SECTOR_SIZE*sector, SEEK_SET);

	cd::IsoWriter::currentSector = sector;
	cd::IsoWriter::currentByte = 0;

	cd::IsoWriter::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    cd::IsoWriter::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return sector;

}

int cd::IsoWriter::SeekToEnd() {

	if (cd::IsoWriter::currentByte) {

		cd::IsoWriter::PrepSector(cd::IsoWriter::lastSectorType);
		fwrite(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoWriter::filePtr);

	}

	fseek(cd::IsoWriter::filePtr, 0, SEEK_END);

	int sector = ftell(cd::IsoWriter::filePtr)/CD_SECTOR_SIZE;

	cd::IsoWriter::currentSector = sector;
	cd::IsoWriter::currentByte = 0;

	cd::IsoWriter::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
    cd::IsoWriter::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

	return sector;

}

size_t cd::IsoWriter::WriteBytes(void* data, size_t bytes, int edcEccEncode) {

    size_t writeBytes = 0;

	char*  	dataPtr = (char*)data;
    int		toWrite;

	memcpy(cd::IsoWriter::sectorM2F1->subHead, cd::IsoWriter::subHeadBuff, 8);

	cd::IsoWriter::lastSectorType = edcEccEncode;

    while(bytes > 0) {

        if (bytes > 2048)
			toWrite = 2048;
		else
			toWrite = bytes;

		memcpy(&cd::IsoWriter::sectorM2F1->data[cd::IsoWriter::currentByte], dataPtr, toWrite);

		cd::IsoWriter::currentByte += toWrite;

		dataPtr += toWrite;
		bytes -= toWrite;

		if (cd::IsoWriter::currentByte >= 2048) {

			cd::IsoWriter::PrepSector(edcEccEncode);

            if (fwrite(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoWriter::filePtr) == 0) {

				cd::IsoWriter::currentByte = 0;
				return(writeBytes);

            }

            cd::IsoWriter::currentByte = 0;
            cd::IsoWriter::currentSector++;

            cd::IsoWriter::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoWriter::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		writeBytes += toWrite;

    }

	return(writeBytes);

}

size_t cd::IsoWriter::WriteBytesXA(void* data, size_t bytes, int edcEccEncode) {

    size_t writeBytes = 0;

	char*  	dataPtr = (char*)data;
    int		toWrite;

	cd::IsoWriter::lastSectorType = edcEccEncode;

    while(bytes > 0) {

        if (bytes > 2336)
			toWrite = 2336;
		else
			toWrite = bytes;

		memcpy(&cd::IsoWriter::sectorM2F2->data[cd::IsoWriter::currentByte], dataPtr, toWrite);

		cd::IsoWriter::currentByte += toWrite;

		dataPtr += toWrite;
		bytes -= toWrite;

		if (cd::IsoWriter::currentByte >= 2336) {

			cd::IsoWriter::PrepSector(edcEccEncode);

            if (fwrite(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoWriter::filePtr) == 0) {

				cd::IsoWriter::currentByte = 0;
				return(writeBytes);

            }

            cd::IsoWriter::currentByte = 0;
            cd::IsoWriter::currentSector++;

            cd::IsoWriter::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoWriter::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		writeBytes += toWrite;

    }

	return(writeBytes);

}

size_t cd::IsoWriter::WriteBytesRaw(void* data, size_t bytes) {

    size_t writeBytes = 0;

	char*  	dataPtr = (char*)data;
    int		toWrite;

	cd::IsoWriter::lastSectorType = EdcEccNone;

    while(bytes > 0) {

        if (bytes > 2352)
			toWrite = 2352;
		else
			toWrite = bytes;

		memcpy(&cd::IsoWriter::sectorBuff[cd::IsoWriter::currentByte], dataPtr, toWrite);

		cd::IsoWriter::currentByte += toWrite;

		dataPtr += toWrite;
		bytes -= toWrite;

		if (cd::IsoWriter::currentByte >= 2352) {

            if (fwrite(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoWriter::filePtr) == 0) {

				cd::IsoWriter::currentByte = 0;
				return(writeBytes);

            }

            cd::IsoWriter::currentByte = 0;
            cd::IsoWriter::currentSector++;

            cd::IsoWriter::sectorM2F1 = (cd::SECTOR_M2F1*)sectorBuff;
			cd::IsoWriter::sectorM2F2 = (cd::SECTOR_M2F2*)sectorBuff;

		}

		writeBytes += toWrite;

    }

	return(writeBytes);

}

int cd::IsoWriter::CurrentSector() {

	return currentSector;

}

void cd::IsoWriter::SetSubheader(unsigned char* data) {

	memcpy(cd::IsoWriter::subHeadBuff, data, 4);
	memcpy(cd::IsoWriter::subHeadBuff+4, data, 4);

}

void cd::IsoWriter::SetSubheader(unsigned int data) {

	memcpy(cd::IsoWriter::subHeadBuff, &data, 4);
	memcpy(cd::IsoWriter::subHeadBuff+4, &data, 4);

}

void cd::IsoWriter::Close() {

	if (cd::IsoWriter::filePtr != nullptr) {

		if (cd::IsoWriter::currentByte > 0) {
			cd::IsoWriter::PrepSector(cd::IsoWriter::lastSectorType);
			fwrite(sectorBuff, CD_SECTOR_SIZE, 1, cd::IsoWriter::filePtr);
		}

		fclose(cd::IsoWriter::filePtr);

	}

	cd::IsoWriter::filePtr = nullptr;

}
