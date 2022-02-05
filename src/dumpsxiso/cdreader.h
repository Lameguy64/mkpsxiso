#ifndef _CDREADER_H
#define _CDREADER_H

#include "cd.h"
#include "xa.h"
#include "listview.h"
#include "fs.h"
#include <memory>
#include <optional>
#include <vector>
#include <string>

namespace cd {

    // ISO reader class which allows you to read data from an ISO image whilst filtering out CD encoding
    // data such as Sync, address and mode codes as well as the EDC/ECC data.
    class IsoReader {

        // File pointer to opened ISO file
        FILE*		filePtr = nullptr;
        // Sector buffer size
        unsigned char sectorBuff[CD_SECTOR_SIZE] {};
        // Mode 2 Form 1 sector struct for simplified reading of sectors (usually points to sectorBuff[])
        SECTOR_M2F1* sectorM2F1 = nullptr;
        // Mode 2 Form 2 sector struct for simplified reading of sectors (usually points to sectorBuff[])
        SECTOR_M2F2* sectorM2F2 = nullptr;
        // Current sector number
        int			currentSector = 0;
        // Current data offset in current sector
        size_t		currentByte = 0;
		// Total number of sectors in the iso
		int totalSectors = 0;
    public:

        // Initializer
        IsoReader();
        // De-initializer
        ~IsoReader();

        // Open ISO image
        bool Open(const fs::path& fileName);

        // Read data sectors in bytes (supports sequential reading)
        size_t ReadBytes(void* ptr, size_t bytes, bool singleSector = false);

        size_t ReadBytesXA(void* ptr, size_t bytes, bool singleSector = false);

        size_t ReadBytesDA(void* ptr, size_t bytes, bool singleSector = false);

        // Skip bytes in data sectors (supports sequential skipping)
        void SkipBytes(size_t bytes, bool singleSector = false);

        // Seek to a sector in the ISO image in sector units
        int SeekToSector(int sector);

        // Seek to a data offset in the ISO image in byte units
        size_t SeekToByte(size_t offs);

        // Get current offset in byte units
        size_t GetPos() const;

        // Close ISO file
        void Close();

    private:
        bool PrepareNextSector();

    };

    class IsoPathTable
    {
    public:
        struct Entry
        {
            ISO_PATHTABLE_ENTRY entry;
            std::string name;
        };

        std::vector<Entry> pathTableList;

        void FreePathTable();
        size_t ReadPathTable(cd::IsoReader* reader, int lba);

        fs::path GetFullDirPath(int dirEntry) const;
    };


    class IsoDirEntries
    {
    public:
        struct Entry
        {
            ISO_DIR_ENTRY entry;
            cdxa::ISO_XA_ATTRIB extData;
            std::string identifier;
            fs::path virtualPath;

            std::unique_ptr<IsoDirEntries> subdir;
        };
        ListView<Entry> dirEntryList;

        IsoDirEntries(ListView<Entry> view);
        void ReadDirEntries(cd::IsoReader* reader, int lba, int sectors);
        void ReadRootDir(cd::IsoReader* reader, int lba);

    private:
        std::optional<Entry> ReadEntry(cd::IsoReader* reader) const;
    };

}

#endif // _CDREADER_H
