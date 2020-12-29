#ifndef _CDREADER_H
#define _CDREADER_H

#include "cd.h"
#include "types.h"

namespace cd {

    // ISO reader class which allows you to read data from an ISO image whilst filtering out CD encoding
    // data such as Sync, address and mode codes as well as the EDC/ECC data.
    class IsoReader {

        // File pointer to opened ISO file
        FILE*		filePtr;
        // Sector buffer size
        u_char		sectorBuff[CD_SECTOR_SIZE];
        // Mode 2 Form 1 sector struct for simplified reading of sectors (usually points to sectorBuff[])
        SECTOR_M2F1* sectorM2F1;
        // Mode 2 Form 2 sector struct for simplified reading of sectors (usually points to sectorBuff[])
        SECTOR_M2F2* sectorM2F2;
        // Current sector number
        int			currentSector;
        // Current data offset in current sector
        int			currentByte;
		// Total number of sectors in the iso
		int totalSectors;
    public:

        // Initializer
        IsoReader();
        // De-initializer
        virtual ~IsoReader();

        // Open ISO image
        bool Open(const char* fileName);

        // Read data sectors in bytes (supports sequential reading)
        size_t ReadBytes(void* ptr, size_t bytes);

        size_t ReadBytesXA(void* ptr, size_t bytes);

        size_t ReadBytesDA(void* ptr, size_t bytes);

        // Skip bytes in data sectors (supports sequential skipping)
        void SkipBytes(size_t bytes);

        // Seek to a sector in the ISO image in sector units
        int SeekToSector(int sector);

        // Seek to a data offset in the ISO image in byte units
        size_t SeekToByte(size_t offs);

        // Get current offset in byte units
        size_t GetPos();

        // Close ISO file
        void Close();

    };

    class IsoPathTable {
    public:

        int numPathEntries;
        ISO_PATHTABLE_ENTRY* pathTableList;

        IsoPathTable();
        virtual ~IsoPathTable();

        void FreePathTable();
        int ReadPathTable(cd::IsoReader* reader, int lba);

        int GetFullDirPath(int dirEntry, char* pathBuff, int pathMaxLen);

    };


    class IsoDirEntries {
    public:

        int numDirEntries;
        ISO_DIR_ENTRY* dirEntryList;

        IsoDirEntries();
        virtual ~IsoDirEntries();

        void FreeDirEntries();
        int ReadDirEntries(cd::IsoReader* reader, int lba, int sectors=1);
        void SortByLBA();

    };

}

#endif // _CDREADER_H
