#include "iso.h"
#include "xa.h"
#include "miniaudio_helpers.h"
#include <ctime>

namespace global
{
	extern time_t	BuildTime;
	extern bool		noWarns;
	extern bool		QuietMode;
	extern bool		noXA;
	extern int		trackNum;
};

static const int MinimumOne(const int val)
{
	return val > 1 ? val : 1;
}

static const int RoundToEven(const int val)
{
	return (val + 1) & -2;
}

static cd::ISO_USHORT_PAIR SetPair16(unsigned short val)
{
    return { val, SwapBytes16(val) };
}

static cd::ISO_UINT_PAIR SetPair32(unsigned int val)
{
	return { val, SwapBytes32(val) };
}

static cd::ISO_DATESTAMP GetISODateStamp(time_t time, signed char GMToffs, const char* date)
{
	cd::ISO_DATESTAMP result;
	if (ParseDateFromString(result, date, GMToffs))
		return result;

	tm* timestamp;
	if (global::cdvd_style.has_value()) {
		timestamp = CustomLocalTime(&time);
	}
	else {
		// GMToffs is specified in 15 minute units
		const time_t GMToffsSeconds = static_cast<time_t>(15) * 60 * GMToffs;
		time += GMToffsSeconds;
		timestamp = gmtime( &time );
	}

	if (timestamp != nullptr)
	{
		result.year		= timestamp->tm_year;
		result.month	= timestamp->tm_mon + 1;
		result.day		= timestamp->tm_mday;
		result.hour		= timestamp->tm_hour;
		result.minute	= timestamp->tm_min;
		result.second	= timestamp->tm_sec;
	}

	return result;
}

int iso::DirTreeClass::GetAudioSize(const fs::path& audioFile)
{
	ma_decoder decoder;
	VirtualWav vw;
	bool isLossy;
	bool isPCM;
	if(ma_redbook_decoder_init_path_by_ext(audioFile, &decoder, &vw, isLossy, isPCM) != MA_SUCCESS)
	{
		ma_decoder_uninit(&decoder);
		return 0;
	}

	ma_uint64 expectedPCMFrames;
	if(ma_decoder_get_length_in_pcm_frames(&decoder, &expectedPCMFrames) != MA_SUCCESS)
	{
		printf("\n    ERROR: corrupt file? unable to get_length_in_pcm_frames\n");
		ma_decoder_uninit(&decoder);
		return 0;
	}

	ma_decoder_uninit(&decoder);
	return GetSizeInSectors(expectedPCMFrames * 2 * (sizeof(int16_t)), CD_SECTOR_SIZE)*CD_SECTOR_SIZE;
}

iso::DirTreeClass::DirTreeClass(EntryList& entries, DIRENTRY* entry, DirTreeClass* parent)
	: m_entry(entry), m_parent(parent), entries(entries)
{
}

iso::DirTreeClass::~DirTreeClass()
{
}

iso::DIRENTRY& iso::DirTreeClass::CreateRootDirectory(EntryList& entries, const cd::ISO_DATESTAMP& volumeDate, const EntryAttributes& attributes)
{
	DIRENTRY& entry = entries.emplace_back();

	entry.type		= EntryType::EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries, &entry);
	entry.date		= volumeDate;
	if (!global::cdvd_style.value_or(false))
	{
		entry.date.year = volumeDate.year % 0x64; // Root overflows dates past 99 for games built with Sony CD-ROM Generator 1.40 and older
	}
	entry.length	= 0; // We will calculate the length later when all entries have been processed
	entry.HF		= attributes.HFLAG % 4;
	entry.attribs	= attributes.XAAttrib;
	entry.perms		= attributes.XAPerm;
	entry.GID		= attributes.GID;
	entry.UID		= attributes.UID;
	entry.flba		= attributes.FLBA;

	return entry;
}

bool iso::DirTreeClass::AddFileEntry(std::string id, EntryType type, fs::path srcfile, const EntryAttributes& attributes, const char *trackid, const char* date)
{
	auto fileAttrib = Stat(srcfile);
    if ( !fileAttrib )
	{
		if ( !global::QuietMode )
		{
			printf("      ");
		}

		printf("ERROR: File not found: %" PRFILESYSTEM_PATH "\n", srcfile.c_str());
		return false;
    }

	// Check if XA data is valid
	if ( type == EntryType::EntryXA )
	{
		// Check header
		bool validHeader = false;
		FILE* fp = OpenFile(srcfile, "rb");
		if (fp != nullptr)
		{
			char buff[4];
			if (fread(buff, 1, std::size(buff), fp) == std::size(buff))
			{
				validHeader = strncmp(buff, "RIFF", std::size(buff)) != 0;
			}
			fclose(fp);
		}

		// Check if its a RIFF (WAV container)
		if (!validHeader)
		{
			if (!global::QuietMode)
			{
				printf("      ");
			}

			printf("ERROR: %" PRFILESYSTEM_PATH " is a WAV or is not properly ripped.\n", srcfile.c_str());

			return false;
		}

		// Check if size is a multiple of 2336 bytes
		if ( ( fileAttrib->st_size % XA_DATA_SIZE ) != 0 )
		{
			if ( ( fileAttrib->st_size % F1_DATA_SIZE) == 0 )
			{
				type = EntryType::EntryXA_DO;
			}
			else
			{
				if ( !global::QuietMode )
				{
					printf("      ");
				}

				printf("ERROR: %" PRFILESYSTEM_PATH " is not a multiple of 2336 or 2048 bytes.\n",
					srcfile.c_str());

				return false;
			}
		}
	}

	id += ";1";

	// Check if file entry already exists
    for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( !entry.id.empty() )
		{
            if ( ( entry.type != EntryType::EntryDir )
				&& ( CompareICase( entry.id, id ) ) )
			{
				if (!global::QuietMode)
				{
					printf("      ");
				}

				printf("ERROR: Duplicate file entry: %s\n", CleanIdentifier(id).c_str());

				return false;
            }
		}
    }

	DIRENTRY& entry = entries.emplace_back();

	entry.id		= std::move(id);
	entry.type		= type;
	entry.subdir	= nullptr;
	entry.HF		= attributes.HFLAG % 4;
	entry.attribs	= attributes.XAAttrib;
	entry.perms		= attributes.XAPerm;
	entry.GID		= attributes.GID;
	entry.UID		= attributes.UID;
	entry.order		= attributes.ORDER;
	entry.flba		= attributes.FLBA;
	entry.srcfile	= std::move(srcfile);
	entry.date		= GetISODateStamp( fileAttrib->st_mtime, attributes.GMTOffs, date );

	if ( type == EntryType::EntryDA )
	{
		entry.length = GetAudioSize( entry.srcfile );
		entry.trackid = trackid;
	}
	else
	{
		entry.length = fileAttrib->st_size;
	}

	entriesInDir.emplace_back(entry);

	return true;

}

void iso::DirTreeClass::AddDummyEntry(const unsigned int sectors, const unsigned char submode, const unsigned int flba, const bool eccAddr)
{
	DIRENTRY& entry = entries.emplace_back();

	entry.attribs	= submode;
	entry.type		= EntryType::EntryDummy;
	entry.length	= F1_DATA_SIZE * sectors;
	entry.flba		= flba;
	entry.order		= -1;
	entry.HF		= eccAddr;

	entriesInDir.emplace_back(entry);
}

iso::DirTreeClass* iso::DirTreeClass::AddSubDirEntry(std::string id, const fs::path& srcDir, const EntryAttributes& attributes, bool& alreadyExists, const char* date)
{
	// Duplicate directory entries are allowed, but the subsequent occurences will not add
	// a new directory to 'entries'.
	// TODO: It's not possible now, but a warning should be issued if entry attributes are specified for the subsequent occurences
	// of the directory. This check probably needs to be moved outside of the function.
	for(auto& e : entriesInDir)
	{
		const iso::DIRENTRY& entry = e.get();
		if((entry.type == EntryType::EntryDir) && (entry.id == id))
		{
			alreadyExists = true;
			return entry.subdir.get();
		}
	}

	auto fileAttrib = Stat(srcDir);
	if (!fileAttrib.has_value())
	{
		fileAttrib.emplace().st_mtime = global::BuildTime;
	}

	DIRENTRY& entry = entries.emplace_back();

	entry.id		= std::move(id);
	entry.type		= EntryType::EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries, &entry, this);
	entry.HF		= attributes.HFLAG % 4;
	entry.attribs	= attributes.XAAttrib;
	entry.perms		= attributes.XAPerm;
	entry.GID		= attributes.GID;
	entry.UID		= attributes.UID;
	entry.order		= attributes.ORDER;
	entry.flba		= attributes.FLBA;
	entry.date		= GetISODateStamp( fileAttrib->st_mtime, attributes.GMTOffs, date );
	entry.length	= 0; // We will calculate the length later when all entries have been processed

	entriesInDir.emplace_back(entry);

	return entry.subdir.get();
}

void iso::DirTreeClass::PrintRecordPath() const
{
	if ( m_parent == nullptr )
	{
		return;
	}

	m_parent->PrintRecordPath();
	printf( "/%s", m_entry->id.c_str() );
}

int iso::DirTreeClass::CalculateTreeLBA(int lba)
{
	int size;

	for ( DIRENTRY& entry : entries )
	{
		// Set current LBA to directory record entry
		entry.lba = (entry.flba)
			? entry.flba
			: lba;

		// If it is a subdir
		if (entry.subdir != nullptr)
		{
			entry.length = entry.subdir->CalculateDirEntryLen();
			size = GetSizeInSectors(entry.length);
		}
		else if (entry.type != EntryType::EntryDA)
		{
			// Increment LBA by the size of file
			size = GetSizeInSectors(entry.length, entry.type == EntryType::EntryXA ? XA_DATA_SIZE : F1_DATA_SIZE);
		}
		else
		{
			// DA files don't take up any space in the ISO filesystem, they are just links to CD tracks
			entry.lba = iso::DA_FILE_PLACEHOLDER_LBA; // we will write the lba into the filesystem when writing the CDDA track
			continue;
		}

		// Prevent rewind on forced LBAs
		lba = std::max(lba, entry.lba + size);
	}

	return lba;
}

int iso::DirTreeClass::CalculateDirEntryLen() const
{
	int dirEntryLen = 68;

	if ( !global::noXA )
	{
		dirEntryLen += 28;
	}

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.id.empty() || entry.HF > 1 )
		{
			continue;
		}

		int dataLen = sizeof(cd::ISO_DIR_ENTRY);

		dataLen += entry.id.length();
		dataLen = RoundToEven(dataLen);

		if ( !global::noXA )
		{
			dataLen += sizeof( cdxa::ISO_XA_ATTRIB );
		}

		constexpr int SECTOR_MASK = F1_DATA_SIZE - 1;
		if ( ((dirEntryLen & SECTOR_MASK) + dataLen) > F1_DATA_SIZE )
		{
			// Round dirEntryLen to the nearest multiple of 2048 as the rest of that sector is "unusable"
			dirEntryLen = (dirEntryLen + SECTOR_MASK) & ~SECTOR_MASK;
		}

		dirEntryLen += dataLen;
	}

	return dirEntryLen;
}

void iso::DirTreeClass::SortDirectoryEntries(const bool byOrder, const bool byLBA)
{
	// Search for directories
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryType::EntryDir )
		{
			// Perform recursive call
			if ( entry.subdir != nullptr )
			{
				entry.subdir->SortDirectoryEntries(byOrder, byLBA);
			}
		}
	}

	if (byOrder)
	{
		std::stable_sort(entriesInDir.begin(), entriesInDir.end(), [](const auto& left, const auto& right)
			{
				return left.get().order < right.get().order;
			});
	}
	else if (byLBA)
	{
		std::sort(entriesInDir.begin(), entriesInDir.end(), [](const auto& left, const auto& right)
			{
				return left.get().lba < right.get().lba;
			});
	}
	else
	{
		std::sort(entriesInDir.begin(), entriesInDir.end(), [&](const auto& left, const auto& right)
			{
				return CleanIdentifier(left.get().id) < CleanIdentifier(right.get().id);
			});
	}
}

void iso::DirTreeClass::WriteDirEntries(cd::IsoWriter* writer, const DIRENTRY* parentDir, const int totalDirs) const
{
	auto sectorView = writer->GetSectorViewM2F1(m_entry->lba, GetSizeInSectors(m_entry->length), cd::IsoWriter::EdcEccForm::Form1);

	auto writeOneEntry = [&sectorView, totalDirs](const DIRENTRY& entry, std::optional<bool> currentOrParent = std::nullopt) -> void
	{
		std::byte buffer[128] {};

		auto dirEntry = reinterpret_cast<cd::ISO_DIR_ENTRY*>(buffer);

		if ( entry.type == EntryType::EntryDir )
		{
			dirEntry->flags = 0x02 | entry.HF;
		}
		else
		{
			dirEntry->flags = 0x00 | entry.HF;
		}

		// File length correction for certain file types
		int length;

		if ( entry.type == EntryType::EntryXA )
		{
			length = F1_DATA_SIZE * GetSizeInSectors(entry.length, XA_DATA_SIZE);
		}
		else if ( entry.type == EntryType::EntryDA )
		{
			if(entry.lba == iso::DA_FILE_PLACEHOLDER_LBA && !global::noWarns)
			{
				printf("\nWARNING: DA file still has placeholder value 0x%X... ", iso::DA_FILE_PLACEHOLDER_LBA);
			}
			length = F1_DATA_SIZE * GetSizeInSectors(entry.length, CD_SECTOR_SIZE);
		}
		else if (entry.type == EntryType::EntryDir)
		{
			length = F1_DATA_SIZE * GetSizeInSectors(entry.length);
		}
		else
		{
			length = entry.length;
		}

		dirEntry->entryOffs = SetPair32( entry.lba );
		dirEntry->entrySize = SetPair32( length );
		dirEntry->volSeqNum = SetPair16( 1 );
		dirEntry->entryDate = entry.date;

		// Normal case - write out the identifier
		char* identifierBuffer = reinterpret_cast<char*>(dirEntry+1);
		if (!currentOrParent.has_value())
		{
			dirEntry->identifierLen = entry.id.length();
			strncpy(identifierBuffer, entry.id.c_str(), entry.id.length());
		}
		else
		{
			// Special cases - current/parent directory entry
			dirEntry->identifierLen = 1;
			identifierBuffer[0] = *currentOrParent ? '\1' : '\0';
		}
		int entryLength = sizeof(*dirEntry) + dirEntry->identifierLen;
		entryLength = RoundToEven(entryLength);

		if ( !global::noXA )
		{
			auto xa = reinterpret_cast<cdxa::ISO_XA_ATTRIB*>(buffer+entryLength);

			xa->id[0] = 'X';
			xa->id[1] = 'A';

			unsigned short attributes = entry.perms;
			if (entry.type == EntryType::EntryDA)
			{
				attributes |= 0x4000;
			}
			else if (entry.type == EntryType::EntryXA)
			{
				attributes |= entry.attribs != 0xFFu ? (entry.attribs << 8) : 0x3800;
				xa->filenum = MinimumOne(fgetc(OpenScopedFile(entry.srcfile, "rb").get()));
			}
			else if (entry.type == EntryType::EntryDir)
			{
				attributes |= 0x8800;
			}
			else
			{
				attributes |= 0x800;
			}

			xa->attributes = SwapBytes16(attributes);
			xa->ownergroupid = SwapBytes16(entry.GID);
			xa->owneruserid = SwapBytes16(entry.UID);

			entryLength += sizeof(*xa);
		}

		dirEntry->entryLength = entryLength;

		if (sectorView->GetSpaceInCurrentSector() < entryLength)
		{
			sectorView->NextSector();
		}
		sectorView->WriteMemory(buffer, entryLength, totalDirs == 0);
	};

	writeOneEntry(*m_entry, false);
	writeOneEntry(*parentDir, true);

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( !entry.id.empty() && entry.HF < 2 )
		{
			writeOneEntry(entry);
		}
	}
}

void iso::DirTreeClass::WriteDirectoryRecords(cd::IsoWriter* writer) const
{
	int totalDirs = global::cdvd_style.value_or(false) ? GetDirCountTotal() : 0;

	// Write Root
	WriteDirEntries( writer, m_entry, totalDirs );

	// Root is always the first entry, so skip it
	for ( auto it = std::next(entries.begin()); it != entries.end(); ++it )
	{
		if ( it->type == EntryType::EntryDir )
		{
			if (totalDirs > 0)
			{
				totalDirs--;
			}
			it->subdir->WriteDirEntries(writer, it->subdir->m_parent->m_entry, totalDirs);
		}
	}
}

void iso::DirTreeClass::WriteFiles(cd::IsoWriter* writer) const
{
	for ( const DIRENTRY& entry : entries )
	{
		// Write files as regular data sectors
		if ( entry.type == EntryType::EntryFile )
		{
			if ( !global::QuietMode )
			{
				printf( "    Packing \"%" PRFILESYSTEM_PATH "\"... ", entry.srcfile.c_str() );
				fflush(stdout);
			}

			auto fp = OpenScopedFile( entry.srcfile, "rb" );
			auto sectorView = writer->GetSectorViewM2F1(entry.lba, GetSizeInSectors(entry.length), cd::IsoWriter::EdcEccForm::Form1);
			sectorView->WriteFile(fp.get());

			if ( !global::QuietMode )
			{
				printf("Done.\n");
			}

		// Write XA/STR video streams as Mode 2 Form 1 (video sectors) and Mode 2 Form 2 (XA audio sectors)
		// Video sectors have EDC/ECC while XA does not
		}
		else if ( entry.type == EntryType::EntryXA )
		{
			if ( !global::QuietMode )
			{
				printf( "    Packing XA \"%" PRFILESYSTEM_PATH "\"... ", entry.srcfile.c_str() );
				fflush(stdout);
			}

			auto fp = OpenScopedFile( entry.srcfile, "rb" );
			auto sectorView = writer->GetSectorViewM2F2(entry.lba, GetSizeInSectors(entry.length, XA_DATA_SIZE), cd::IsoWriter::EdcEccForm::Autodetect);
			sectorView->WriteFile(fp.get());

			if (!global::QuietMode)
			{
				printf( "Done.\n" );
			}

		// Write data only STR streams as Mode 2 Form 1
		}
		else if ( entry.type == EntryType::EntryXA_DO )
		{
			if ( !global::QuietMode )
			{
				printf( "    Packing XA-DO \"%" PRFILESYSTEM_PATH "\"... ", entry.srcfile.c_str() );
				fflush(stdout);
			}

			auto fp = OpenScopedFile( entry.srcfile, "rb" );
			auto sectorView = writer->GetSectorViewM2F1(entry.lba, GetSizeInSectors(entry.length), cd::IsoWriter::EdcEccForm::Form1);
			sectorView->SetSubheader(cd::IsoWriter::SubSTR);
			sectorView->WriteFile(fp.get());

			if ( !global::QuietMode )
			{
				printf("Done.\n");
			}
		}
		// Write dummies as gaps without data
		else if ( entry.type == EntryType::EntryDummy )
		{
			// TODO: HUGE HACK, will be removed once EntryDummy is unified with EntryFile again
			const bool isForm2 = entry.attribs & 0x20;

			const uint32_t sizeInSectors = GetSizeInSectors(entry.length);
			auto sectorView = writer->GetSectorViewM2F1(entry.lba, sizeInSectors, isForm2 ? cd::IsoWriter::EdcEccForm::Form2 : cd::IsoWriter::EdcEccForm::Form1);

			sectorView->WriteBlankSectors(sizeInSectors, entry.attribs, entry.HF);
		}
		// Write DA files as audio tracks
		else
		{
			continue;
		}
	}
}

void iso::DirTreeClass::OutputHeaderListing(FILE* fp, const int level, const char* name) const
{
	if ( level == 0 )
	{
		fprintf( fp, "#ifndef _ISO_FILES\n" );
		fprintf( fp, "#define _ISO_FILES\n\n" );
	}

	fprintf( fp, "/* %s */\n", name );

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( !entry.id.empty() && entry.type != EntryType::EntryDir )
		{
			std::string temp_name = "LBA_" + entry.id;

			for ( char& ch : temp_name )
			{
				if ( ch == '.' || ch == ' ' || ch == '-' )
				{
					ch = '_';
				}

				if ( ch == ';' )
				{
					ch = '\0';
					break;
				}
			}

			fprintf( fp, "#define %-17s %d\n", temp_name.c_str(), entry.lba );
		}
	}

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryType::EntryDir )
		{
			fprintf( fp, "\n" );
			entry.subdir->OutputHeaderListing( fp, level+1, entry.id.c_str() );
		}
	}
}

void iso::DirTreeClass::OutputLBAlisting(FILE* fp, int level) const
{
	// Helper lambda to print common details
	auto printEntryDetails = [&](const char* type, const char* name, const char* sectors, const DIRENTRY& entry)
	{
		// Write entry type with 4 spaces at start
		fprintf(fp, "%*s%-6s|", level + 4, "", type);
		// Write entry name
		fprintf(fp, "%-14s|", name);
		// Write entry length in sectors
		fprintf(fp, "%-8s|", sectors);
		// Write LBA offset
		fprintf(fp, "%-7d|", entry.lba);
		// Write timecode
		fprintf(fp, "%-10s|", SectorsToTimecode(150 + entry.lba).c_str());
		// Write size in byte units
		fprintf(fp, "%-11s|", entry.type != EntryType::EntryDir ? std::to_string(entry.length).c_str() : "");
		// Write source file path
		fprintf(fp, "%s\n", reinterpret_cast<const char*>(entry.srcfile.generic_u8string().c_str()));
	};

	int maxlba = 0;
	if (level == 0)
	{
		for (const auto& e : entriesInDir)
		{
			const DIRENTRY& entry = e.get();
			if (entry.type != EntryType::EntryDummy && entry.type != EntryType::EntryDA)
			{
				maxlba = std::max<int>(entry.lba, maxlba);
			}
		}
	}

	// Print first the files in the directory
	for (const auto& e : entriesInDir)
	{
		const DIRENTRY& entry = e.get();
		// Skip directories and postgap dummy
		if (entry.type == EntryType::EntryDir || (entry.type == EntryType::EntryDummy && level == 0 && entry.lba > maxlba))
			continue;

		const char* typeStr;
		std::string nameStr = CleanIdentifier(entry.id);
		uint32_t sectors = GetSizeInSectors(entry.length);

		switch (entry.type)
		{
			case EntryType::EntryFile:
				typeStr = " File";
				break;
			case EntryType::EntryXA_DO:
				typeStr = "   XA";
				break;
			case EntryType::EntryDummy:
				typeStr = "Dummy";
				nameStr = "<DUMMY>";
				break;
			case EntryType::EntryXA:
				typeStr = "   XA";
				sectors = GetSizeInSectors(entry.length, XA_DATA_SIZE);
				break;
			case EntryType::EntryDA:
				typeStr = " CDDA";
				sectors = 150 + GetSizeInSectors(entry.length, CD_SECTOR_SIZE);
				break;
			default:
				continue;
		}

		// Print the entry details
		printEntryDetails(typeStr, nameStr.c_str(), std::to_string(sectors).c_str(), entry);
	}

	// Print directories and postgap dummy
	for (const auto& e : entriesInDir)
	{
		const DIRENTRY& entry = e.get();
		if (entry.type == EntryType::EntryDir)
		{
			printEntryDetails(" Dir", CleanIdentifier(entry.id).c_str(), "", entry);
			entry.subdir->OutputLBAlisting( fp, level+1 );
		}
		else if (entry.type == EntryType::EntryDummy && level == 0 && entry.lba > maxlba)
		{
			printEntryDetails("Dummy", "<DUMMY>", std::to_string(GetSizeInSectors(entry.length)).c_str(), entry);
		}
	}

	if ( level > 0 )
	{
		fprintf(fp, "%*s End  |%-14s|%-8s|%-7s|%-10s|%-11s|\n", level + 3, "", m_entry->id.c_str(), "", "", "", "");
	}
}


int iso::DirTreeClass::CalculatePathTableLen(const DIRENTRY& dirEntry) const
{
	// Put identifier (empty if first entry)
	int len = sizeof(cd::ISO_PATHTABLE_ENTRY) + RoundToEven(MinimumOne(dirEntry.id.length()));

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryType::EntryDir )
		{
			len += entry.subdir->CalculatePathTableLen( entry );
		}
	}

	return len;
}

int iso::DirTreeClass::GetFileCountTotal() const
{
    int numfiles = 0;

    for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type != EntryType::EntryDir )
		{
			if ( !entry.id.empty() )
			{
				numfiles++;
			}
		}
		else
		{
            numfiles += entry.subdir->GetFileCountTotal();
		}
    }

    return numfiles;
}

int iso::DirTreeClass::GetDirCountTotal() const
{
	int numdirs = 0;

   for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
        if ( entry.type == EntryType::EntryDir )
		{
			numdirs++;
            numdirs += entry.subdir->GetDirCountTotal();
		}
    }

    return numdirs;
}

int iso::DirTreeClass::GetPathDepth(size_t* pathLength) const
{
	int depth = 0;
	for (auto current = this; current->m_parent != nullptr; current = current->m_parent)
	{
		depth++;
		if (pathLength != nullptr)
			*pathLength += current->m_entry->id.length();
	}
	return depth;
}

void iso::WriteLicenseData(cd::IsoWriter* writer, void* data, const bool& ps2)
{
	auto licenseSectors = writer->GetSectorViewM2F2(0, 12, cd::IsoWriter::EdcEccForm::Form1);
	licenseSectors->WriteMemory(data, XA_DATA_SIZE * 12);

	if (!ps2)
	{
		auto licenseBlankSectors = writer->GetSectorViewM2F1(12, 4, cd::IsoWriter::EdcEccForm::Form2);
		licenseBlankSectors->WriteBlankSectors(4);
	}
	else
	{
		auto licenseBlankSectors = writer->GetSectorViewM2F1(12, 4, cd::IsoWriter::EdcEccForm::Form1);
		licenseBlankSectors->WriteBlankSectors(4, 0x08);
	}
}

template<size_t N>
static void CopyStringPadWithSpaces(char (&dest)[N], const char* src)
{
	auto begin = std::begin(dest);
	auto end = std::end(dest);

	if ( src != nullptr )
	{
		for (; begin != end && *src != 0; ++begin, ++src)
		{
			*begin = std::toupper( (unsigned char)*src );
		}
	}

	// Pad the remaining space with spaces
	std::fill( begin, end, ' ' );
}

void iso::DirTreeClass::WriteDescriptor(cd::IsoWriter* writer, const iso::IDENTIFIERS& id, const int imageLen) const
{
	cd::ISO_DESCRIPTOR isoDescriptor {};

	isoDescriptor.header.type = 1;
	isoDescriptor.header.version = 1;
	CopyStringPadWithSpaces( isoDescriptor.header.id, "CD001" );

	// Set System identifier
	CopyStringPadWithSpaces( isoDescriptor.systemID, id.SystemID );

	// Set Volume identifier
	CopyStringPadWithSpaces( isoDescriptor.volumeID, id.VolumeID );

	// Set Application identifier
	CopyStringPadWithSpaces( isoDescriptor.applicationIdentifier, id.Application );

	// Volume Set identifier
	CopyStringPadWithSpaces( isoDescriptor.volumeSetIdentifier, id.VolumeSet );

	// Publisher identifier
	CopyStringPadWithSpaces( isoDescriptor.publisherIdentifier, id.Publisher );

	// Data preparer identifier
	CopyStringPadWithSpaces( isoDescriptor.dataPreparerIdentifier, id.DataPreparer );

	// Copyright (file) identifier
	CopyStringPadWithSpaces( isoDescriptor.copyrightFileIdentifier, id.Copyright );

	// Unneeded identifiers
	CopyStringPadWithSpaces( isoDescriptor.abstractFileIdentifier, id.AbstractFile );
	CopyStringPadWithSpaces( isoDescriptor.bibliographicFilelIdentifier, id.BibliographicFile );

	ParseLongDateFromString( isoDescriptor.volumeCreateDate, id.CreationDate );
	ParseLongDateFromString( isoDescriptor.volumeModifyDate, id.ModificationDate );
	isoDescriptor.volumeEffectiveDate = isoDescriptor.volumeExpiryDate = GetUnspecifiedLongDate();
	isoDescriptor.fileStructVersion = 1;

	if ( !global::noXA )
	{
		strncpy( (char*)&isoDescriptor.appData[141], "CD-XA001", 8 );
	}

	unsigned int pathTableLen = CalculatePathTableLen(*m_entry);
	unsigned int pathTableSectors = GetSizeInSectors(pathTableLen);

	isoDescriptor.volumeSetSize = SetPair16( 1 );
	isoDescriptor.volumeSeqNumber = SetPair16( 1 );
	isoDescriptor.sectorSize = SetPair16( F1_DATA_SIZE );
	isoDescriptor.pathTableSize = SetPair32( pathTableLen );

	// Setup the root directory record
	isoDescriptor.rootDirRecord.entryLength = 34;
	isoDescriptor.rootDirRecord.extLength	= 0;
	isoDescriptor.rootDirRecord.entryOffs = SetPair32( 18+(pathTableSectors*4) );
	isoDescriptor.rootDirRecord.entrySize = SetPair32( GetSizeInSectors(m_entry->length)*F1_DATA_SIZE );
	isoDescriptor.rootDirRecord.flags = 0x02 | m_entry->HF;
	isoDescriptor.rootDirRecord.volSeqNum = SetPair16( 1 );
	isoDescriptor.rootDirRecord.identifierLen = 1;
	isoDescriptor.rootDirRecord.identifier = 0x0;

	isoDescriptor.rootDirRecord.entryDate = m_entry->date;

	isoDescriptor.pathTable1Offs = 18;
	isoDescriptor.pathTable2Offs = isoDescriptor.pathTable1Offs + pathTableSectors;
	isoDescriptor.pathTable1MSBoffs = isoDescriptor.pathTable2Offs+1;
	isoDescriptor.pathTable2MSBoffs = isoDescriptor.pathTable1MSBoffs + pathTableSectors;
	isoDescriptor.pathTable1MSBoffs = SwapBytes32( isoDescriptor.pathTable1MSBoffs );
	isoDescriptor.pathTable2MSBoffs = SwapBytes32( isoDescriptor.pathTable2MSBoffs );

	isoDescriptor.volumeSize = SetPair32( imageLen );

	// Write the descriptor
	unsigned int currentHeaderLBA = 16;
	const bool setEnd = !global::cdvd_style.value_or(false);

	auto isoDescriptorSectors = writer->GetSectorViewM2F1(currentHeaderLBA, 2, cd::IsoWriter::EdcEccForm::Form1);
	isoDescriptorSectors->SetSubheader(setEnd ? cd::IsoWriter::SubEOR : cd::IsoWriter::SubData);

	isoDescriptorSectors->WriteMemory(&isoDescriptor, sizeof(isoDescriptor));

	// Write descriptor terminator;
	memset( &isoDescriptor, 0x00, sizeof(cd::ISO_DESCRIPTOR) );
	isoDescriptor.header.type = 255;
	isoDescriptor.header.version = 1;
	CopyStringPadWithSpaces( isoDescriptor.header.id, "CD001" );

	isoDescriptorSectors->WriteMemory(&isoDescriptor, sizeof(isoDescriptor), setEnd);
	currentHeaderLBA += 2;

	// Generate and write L-path table
	const size_t pathTableSize = static_cast<size_t>(F1_DATA_SIZE)*pathTableSectors;
	auto sectorBuff = std::make_unique<unsigned char[]>(pathTableSize);

	GeneratePathTable( sectorBuff.get(), false );
	auto lpathTable1 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	lpathTable1->WriteMemory(sectorBuff.get(), pathTableSize, setEnd);
	currentHeaderLBA += pathTableSectors;

	auto lpathTable2 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	lpathTable2->WriteMemory(sectorBuff.get(), pathTableSize, setEnd);
	currentHeaderLBA += pathTableSectors;

	// Generate and write M-path table
	GeneratePathTable( sectorBuff.get(), true );
	auto mpathTable1 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	mpathTable1->WriteMemory(sectorBuff.get(), pathTableSize, setEnd);
	currentHeaderLBA += pathTableSectors;

	auto mpathTable2 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	mpathTable2->WriteMemory(sectorBuff.get(), pathTableSize, setEnd);
}

int iso::DirTreeClass::GeneratePathTable(unsigned char* buff, bool msb) const
{
	unsigned short index = 1;
	PathTableClass pathTable;

	// Write out root explicitly first
	pathTable.entries.push_back({
		m_entry->id,
		index, // Self for Root
		m_entry->lba
	});

	// Initialize Breadth-First Search Queue
	std::queue<std::tuple<const DirTreeClass*, unsigned short>> dirsToProcess;
	dirsToProcess.emplace(this, index++);

	// Process directories using BFS
	while (!dirsToProcess.empty())
	{
		const auto [currentDir, parentIndex] = dirsToProcess.front();
		dirsToProcess.pop();

		for (const DIRENTRY& entry : currentDir->entriesInDir)
		{
			if (entry.type == EntryType::EntryDir)
			{
				pathTable.entries.push_back({
					entry.id,
					parentIndex,
					entry.lba
				});

				// Queue subdirectories
				dirsToProcess.emplace(entry.subdir.get(), index++);
			}
		}
	}

	// Generate data buffer
	const unsigned char* endPtr = pathTable.GenTableData(buff, msb);

	return static_cast<int>(endPtr - buff);
}

unsigned char* iso::PathTableClass::GenTableData(unsigned char* buff, bool msb)
{
	for ( const PathEntry& entry : entries )
	{
		const int idLength = MinimumOne(entry.dir_id.length());
		*buff++ = idLength;	// Directory identifier length
		*buff++ = 0;		// Extended attribute record length (unused)

		// Write LBA and directory number index
		unsigned int lba = entry.dir_lba;
		unsigned short parentDirNumber = entry.dir_parent_index;

		if ( msb )
		{
			lba = SwapBytes32( lba );
			parentDirNumber = SwapBytes16( parentDirNumber );
		}
		memcpy( buff, &lba, sizeof(lba) );
		memcpy( buff+4, &parentDirNumber, sizeof(parentDirNumber) );

		buff += 6;

		// Put identifier (nullptr if first entry)
		strncpy( (char*)buff, entry.dir_id.c_str(),
			entry.dir_id.length() );

		buff += RoundToEven(idLength);
	}

	return buff;
}
