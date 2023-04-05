#include "global.h"
#include "iso.h"
#include "common.h"
#include "cd.h"
#include "xa.h"
#include "platform.h"
#include "miniaudio_helpers.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <cstdarg>

char rootname[] = { "<root>" };

static bool icompare_func(unsigned char a, unsigned char b)
{
	return std::tolower( a ) == std::tolower( b );
}

static bool icompare(const std::string& a, const std::string& b)
{
	if ( a.length() == b.length() )
	{
		return std::equal( b.begin(), b.end(), a.begin(), icompare_func );
	}
	else
	{
		return false;
	}
}

static size_t GetIDLength(std::string_view id)
{
	size_t length = std::max<size_t>(1, id.length());
	length += length % 2;
	return length;
}

static cd::ISO_DATESTAMP GetISODateStamp(time_t time, signed char GMToffs)
{
	// GMToffs is specified in 15 minute units
	const time_t GMToffsSeconds = static_cast<time_t>(15) * 60 * GMToffs;

	time += GMToffsSeconds;
	const tm timestamp = *gmtime( &time );

	cd::ISO_DATESTAMP result;
	result.hour		= timestamp.tm_hour;
	result.minute	= timestamp.tm_min;
	result.second	= timestamp.tm_sec;
	result.month	= timestamp.tm_mon+1;
	result.day		= timestamp.tm_mday;
	result.year		= timestamp.tm_year;
	result.GMToffs	= GMToffs;

	return result;
}

template<typename T>
static T RoundToEven(T val)
{
	return (val + 1) & -2;
}

int iso::DirTreeClass::GetAudioSize(const fs::path& audioFile)
{
	ma_decoder decoder;
	VirtualWavEx vw;
	bool isLossy;
	if(ma_redbook_decoder_init_path_by_ext(audioFile, &decoder, &vw, isLossy) != MA_SUCCESS)
	{
		return 0;
	}

	const ma_uint64 expectedPCMFrames = ma_decoder_get_length_in_pcm_frames(&decoder);
	ma_decoder_uninit(&decoder);
    if(expectedPCMFrames == 0)
	{
		printf("\n    ERROR: corrupt file? unable to get_length_in_pcm_frames\n");
        return 0;
	}

	return GetSizeInSectors(expectedPCMFrames * 2 * (sizeof(int16_t)), CD_SECTOR_SIZE)*CD_SECTOR_SIZE;
}

iso::DirTreeClass::DirTreeClass(EntryList& entries, DirTreeClass* parent)
	: name(rootname), entries(entries), parent(parent)
{
}

iso::DirTreeClass::~DirTreeClass()
{
}

iso::DIRENTRY& iso::DirTreeClass::CreateRootDirectory(EntryList& entries, const cd::ISO_DATESTAMP& volumeDate)
{
	DIRENTRY entry {};

	entry.type		= EntryType::EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries);
	entry.date		= volumeDate;
	entry.length	= 0; // Length is meaningless for directories

	const EntryAttributes attributes; // Leave defaults
	entry.attribs	= attributes.XAAttrib;
	entry.perms		= attributes.XAPerm;
	entry.GID		= attributes.GID;
	entry.UID		= attributes.UID;

	entries.emplace_back( std::move(entry) );

	return entries.back();
}

bool iso::DirTreeClass::AddFileEntry(const char* id, EntryType type, const fs::path& srcfile, const EntryAttributes& attributes, const char *trackid)
{
    auto fileAttrib = Stat(srcfile);
    if ( !fileAttrib )
	{
		if ( !global::QuietMode )
		{
			printf("      ");
		}

		printf("ERROR: File not found: %" PRFILESYSTEM_PATH "\n", srcfile.lexically_normal().c_str());
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

			printf("ERROR: %" PRFILESYSTEM_PATH " is a WAV or is not properly ripped.\n", srcfile.lexically_normal().c_str());

			return false;
		}

		// Check if size is a multiple of 2336 bytes
		if ( ( fileAttrib->st_size % 2336 ) != 0 )
		{
			if ( ( fileAttrib->st_size % 2048) == 0 )
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
					srcfile.lexically_normal().c_str());

				return false;
			}
		}

	}


	std::string temp_name = id;
	for ( char& ch : temp_name )
	{
		ch = std::toupper( ch );
	}

	temp_name += ";1";


	// Check if file entry already exists
    for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( !entry.id.empty() )
		{
            if ( ( entry.type == EntryType::EntryFile )
				&& ( icompare( entry.id, temp_name ) ) )
			{
				if (!global::QuietMode)
				{
					printf("      ");
				}

				printf("ERROR: Duplicate file entry: %s\n", id);

				return false;
            }

		}

    }

	DIRENTRY entry {};

	entry.id = std::move(temp_name);
	entry.type		= type;
	entry.subdir	= nullptr;
	entry.attribs	= attributes.XAAttrib;
	entry.perms		= attributes.XAPerm;
	entry.GID		= attributes.GID;
	entry.UID		= attributes.UID;

	if ( !srcfile.empty() )
	{
		entry.srcfile = srcfile;
	}

	if ( type == EntryType::EntryDA )
	{
		entry.length = GetAudioSize( srcfile );
		if(trackid == nullptr)
		{
			printf("ERROR: no trackid for DA track\n");
			return false;
		}
		entry.trackid = trackid;
	}
	else if ( type != EntryType::EntryDir )
	{
		entry.length = fileAttrib->st_size;
	}

    entry.date = GetISODateStamp( fileAttrib->st_mtime, attributes.GMTOffs );

	entries.emplace_back(std::move(entry));
	entriesInDir.emplace_back(entries.back());

	return true;

}

void iso::DirTreeClass::AddDummyEntry(int sectors, int type)
{
	DIRENTRY entry {};

	// TODO: HUGE HACK, will be removed once EntryDummy is unified with EntryFile again
	entry.perms	=	type;
	entry.type		= EntryType::EntryDummy;
	entry.length	= 2048*sectors;

	entries.emplace_back(std::move(entry));
	entriesInDir.emplace_back(entries.back());
}

iso::DirTreeClass* iso::DirTreeClass::AddSubDirEntry(const char* id, const fs::path& srcDir, const EntryAttributes& attributes, bool& alreadyExists)
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
	time_t dirTime;
	if ( fileAttrib )
	{
		dirTime = fileAttrib->st_mtime;
	}
	else
	{
		dirTime = global::BuildTime;

		if ( id != nullptr )
		{
			if ( !global::QuietMode )
			{
				printf( "\n    " );
			}

			printf( "WARNING: 'source' attribute for subdirectory '%s' is invalid or empty.\n", id );
		}
	}

	DIRENTRY entry {};

	if (id != nullptr)
	{
		entry.id = id;
		for ( char& ch : entry.id )
		{
			ch = toupper( ch );
		}
	}

	entry.type		= EntryType::EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries, this);
	entry.attribs	= attributes.XAAttrib;
	entry.perms		= attributes.XAPerm;
	entry.GID		= attributes.GID;
	entry.UID		= attributes.UID;
	entry.date		= GetISODateStamp( dirTime, attributes.GMTOffs );
	entry.length	= 0; // Length is meaningless for directories

	entries.emplace_back(std::move(entry));
	entriesInDir.emplace_back(entries.back());

	return entries.back().subdir.get();
}

void iso::DirTreeClass::PrintRecordPath()
{
	if ( parent == nullptr )
	{
		return;
	}

	parent->PrintRecordPath();
	printf( "/%s", name.c_str() );
}

int iso::DirTreeClass::CalculateTreeLBA(int lba)
{
	bool firstDAWritten = false;
	for ( DIRENTRY& entry : entries )
	{
		// Set current LBA to directory record entry
		entry.lba = lba;

		// If it is a subdir
		if (entry.subdir != nullptr)
		{
			if (!entry.id.empty())
			{
				entry.subdir->name = entry.id;
			}

			lba += GetSizeInSectors(entry.subdir->CalculateDirEntryLen());
		}
		else
		{
			// Increment LBA by the size of file
			if ( entry.type == EntryType::EntryFile || entry.type == EntryType::EntryXA_DO || entry.type == EntryType::EntryDummy )
			{
				lba += GetSizeInSectors(entry.length, 2048);
			}
			else if ( entry.type == EntryType::EntryXA )
			{
				lba += GetSizeInSectors(entry.length, 2336);
			}
			else if ( entry.type == EntryType::EntryDA )
			{
				// DA files don't take up any space in the ISO filesystem, they are just links to CD tracks
				entry.lba = iso::DA_FILE_PLACEHOLDER_LBA; // we will write the lba into the filesystem when writing the CDDA track
			}
		}
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
		if ( entry.id.empty() )
		{
			continue;
		}

		int dataLen = sizeof(cd::ISO_DIR_ENTRY);

		dataLen += GetIDLength(entry.id);

		if ( !global::noXA )
		{
			dataLen = RoundToEven(dataLen);
			dataLen += sizeof( cdxa::ISO_XA_ATTRIB );
		}

		if ( ((dirEntryLen%2048)+dataLen) > 2048 )
		{
			// Round dirEntryLen to the nearest multiple of 2048 as the rest of that sector is "unusable"
			dirEntryLen = ((dirEntryLen + 2047) / 2048) * 2048;
		}

		dirEntryLen += dataLen;
	}

	return 2048 * GetSizeInSectors(dirEntryLen);
}

void iso::DirTreeClass::SortDirectoryEntries()
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
				entry.subdir->SortDirectoryEntries();
			}
		}
	}

	std::sort(entriesInDir.begin(), entriesInDir.end(), [](const auto& left, const auto& right)
		{
			return left.get().id < right.get().id;
		});
}

bool iso::DirTreeClass::WriteDirEntries(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir) const
{
	//char	dataBuff[2048] {};
	//char*	dataBuffPtr=dataBuff;
	//char	entryBuff[128];
	//int		dirlen;

	auto sectorView = writer->GetSectorViewM2F1(dir.lba, GetSizeInSectors(CalculateDirEntryLen()), cd::IsoWriter::EdcEccForm::Form1);

	//writer->SeekToSector( dir.lba );

	auto writeOneEntry = [&sectorView](const DIRENTRY& entry, std::optional<bool> currentOrParent = std::nullopt) -> void
	{
		std::byte buffer[128] {};

		auto dirEntry = reinterpret_cast<cd::ISO_DIR_ENTRY*>(buffer);
		int entryLength = sizeof(*dirEntry);

		if ( entry.type == EntryType::EntryDir )
		{
			dirEntry->flags = 0x02;
		}
		else
		{
			dirEntry->flags = 0x00;
		}

		// File length correction for certain file types
		int length;

		if ( entry.type == EntryType::EntryXA )
		{
			length = 2048* GetSizeInSectors(entry.length, 2336);
		}
		else if ( entry.type == EntryType::EntryXA_DO )
		{
			length = 2048 * GetSizeInSectors(entry.length, 2048);
		}
		else if ( entry.type == EntryType::EntryDA )
		{
			if(entry.lba == iso::DA_FILE_PLACEHOLDER_LBA)
			{
				printf("ERROR: DA file still has placeholder value 0x%X\n", iso::DA_FILE_PLACEHOLDER_LBA);
			}
			length = 2048 * GetSizeInSectors(entry.length, 2352);
		}
		else if (entry.type == EntryType::EntryDir)
		{
			length = entry.subdir->CalculateDirEntryLen();
		}
		else
		{
			length = entry.length;
		}

		dirEntry->entryOffs = cd::SetPair32( entry.lba );
		dirEntry->entrySize = cd::SetPair32( length );
		dirEntry->volSeqNum = cd::SetPair16( 1 );
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
			identifierBuffer[0] = currentOrParent.value() ? '\1' : '\0';
		}
		entryLength += dirEntry->identifierLen;

		if ( !global::noXA )
		{
			entryLength = RoundToEven(entryLength);
			auto xa = reinterpret_cast<cdxa::ISO_XA_ATTRIB*>(buffer+entryLength);

			xa->id[0] = 'X';
			xa->id[1] = 'A';

			unsigned short attributes = entry.perms;
			if ( (entry.type == EntryType::EntryFile) ||
				(entry.type == EntryType::EntryXA_DO) ||
				(entry.type == EntryType::EntryDummy) )
			{
				attributes |= 0x800;
			}
			else if (entry.type == EntryType::EntryDA)
			{
				attributes |= 0x4000;
			}
			else if (entry.type == EntryType::EntryXA)
			{
				attributes |= entry.attribs != 0xFFu ? (entry.attribs << 8) : 0x3800;
				xa->filenum = 1;
			}
			else if (entry.type == EntryType::EntryDir)
			{
				attributes |= 0x8800;
			}

			if (entry.type == EntryType::EntryXA)
			{
				xa->filenum = 1;
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
		sectorView->WriteMemory(buffer, entryLength);
	};

	writeOneEntry(dir, false);
	writeOneEntry(parentDir, true);

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.id.empty() )
		{
			continue;
		}
		
		writeOneEntry(e);
	}

	return true;
}

bool iso::DirTreeClass::WriteDirectoryRecords(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir)
{
	if(!WriteDirEntries( writer, dir, parentDir ))
	{
		return false;
	}

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryType::EntryDir )
		{
			if ( !entry.subdir->WriteDirectoryRecords(writer, entry, dir) )
			{
				return false;
			}
		}
	}

	return true;
}

bool iso::DirTreeClass::WriteFiles(cd::IsoWriter* writer) const
{
	for ( const DIRENTRY& entry : entries )
	{
		// Write files as regular data sectors
		if ( entry.type == EntryType::EntryFile )
		{
			if ( !entry.srcfile.empty() )
			{
				if ( !global::QuietMode )
				{
					printf( "      Packing %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
				}

				FILE *fp = OpenFile( entry.srcfile, "rb" );
				if (fp != nullptr)
				{
					auto sectorView = writer->GetSectorViewM2F1(entry.lba, GetSizeInSectors(entry.length), cd::IsoWriter::EdcEccForm::Form1);
					sectorView->WriteFile(fp);

					fclose(fp);
				}

				if ( !global::QuietMode )
				{
					printf("Done.\n");
				}

			}

		// Write XA/STR video streams as Mode 2 Form 1 (video sectors) and Mode 2 Form 2 (XA audio sectors)
		// Video sectors have EDC/ECC while XA does not
		}
		else if ( entry.type == EntryType::EntryXA )
		{
			if ( !global::QuietMode )
			{
				printf( "      Packing XA %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
			}

			FILE *fp = OpenFile( entry.srcfile, "rb" );
			if (fp != nullptr)
			{
				auto sectorView = writer->GetSectorViewM2F2(entry.lba, GetSizeInSectors(entry.length, 2336), cd::IsoWriter::EdcEccForm::Autodetect);
				sectorView->WriteFile(fp);

				fclose( fp );
			}			

			if (!global::QuietMode)
			{
				printf( "Done.\n" );
			}

		// Write data only STR streams as Mode 2 Form 1
		}
		else if ( entry.type == EntryType::EntryXA_DO )
		{
			if ( !entry.srcfile.empty() )
			{
				if ( !global::QuietMode )
				{
					printf( "      Packing XA-DO %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
				}

				FILE *fp = OpenFile( entry.srcfile, "rb" );
				if (fp != nullptr)
				{
					auto sectorView = writer->GetSectorViewM2F1(entry.lba, GetSizeInSectors(entry.length), cd::IsoWriter::EdcEccForm::Form1);
					sectorView->SetSubheader(cd::IsoWriter::SubSTR);
					sectorView->WriteFile(fp);

					fclose(fp);
				}

				if ( !global::QuietMode )
				{
					printf("Done.\n");
				}

			}
		// Write DA files as audio tracks
		}
		else if ( entry.type == EntryType::EntryDA )
		{
			continue;
		}
		// Write dummies as gaps without data
		else if ( entry.type == EntryType::EntryDummy )
		{
			// TODO: HUGE HACK, will be removed once EntryDummy is unified with EntryFile again
			const bool isForm2 = entry.perms != 0;

			const uint32_t sizeInSectors = GetSizeInSectors(entry.length);
			auto sectorView = writer->GetSectorViewM2F1(entry.lba, sizeInSectors, isForm2 ? cd::IsoWriter::EdcEccForm::Form2 : cd::IsoWriter::EdcEccForm::Form1);

			sectorView->WriteBlankSectors(sizeInSectors);
		}
	}

	return true;
}

void iso::DirTreeClass::OutputHeaderListing(FILE* fp, int level) const
{
	if ( level == 0 )
	{
		fprintf( fp, "#ifndef _ISO_FILES\n" );
		fprintf( fp, "#define _ISO_FILES\n\n" );
	}

	fprintf( fp, "/* %s */\n", name.c_str() );

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( !entry.id.empty() && entry.type != EntryType::EntryDir )
		{
			std::string temp_name = "LBA_" + entry.id;

			for ( char& ch : temp_name )
			{
				ch = std::toupper( ch );

				if ( ch == '.' )
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
			entry.subdir->OutputHeaderListing( fp, level+1 );
		}
	}

	if ( level == 0 )
	{
		fprintf( fp, "\n#endif\n" );
	}
}

void iso::DirTreeClass::OutputLBAlisting(FILE* fp, int level) const
{
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		fprintf( fp, "    " );

		if ( !entry.id.empty() )
		{
			if ( entry.type == EntryType::EntryFile )
			{
				fprintf( fp, "File  " );
			}
			else if ( entry.type == EntryType::EntryDir )
			{
				fprintf( fp, "Dir   " );
			}
			else if ( ( entry.type == EntryType::EntryXA ) ||
				( entry.type == EntryType::EntryXA_DO ) )
			{
				fprintf( fp, "XA    " );
			}
			else if ( entry.type == EntryType::EntryDA )
			{
				fprintf( fp, "CDDA  " );
			}

			fprintf( fp, "%-17s", entry.id.c_str() );
		} else {

			fprintf( fp, "Dummy <DUMMY>          " );

		}

		// Write size in sector units
		if (entry.type != EntryType::EntryDir)
		{
			fprintf( fp, "%-10" PRIu32, GetSizeInSectors(entry.length) );
		}
		else
		{
			fprintf( fp, "%-10s", "" );
		}

		// Write LBA offset
		fprintf( fp, "%-10d", entry.lba );

		// Write Timecode
		fprintf( fp, "%-12s", SectorsToTimecode(entry.lba).c_str());

		// Write size in byte units
		if (entry.type != EntryType::EntryDir)
		{
			fprintf( fp, "%-10" PRId64, entry.length );
		}
		else
		{
			fprintf( fp, "%-10s", "" );
		}

		// Write source file path
		if ( (!entry.id.empty()) && (entry.type != EntryType::EntryDir) )
		{
			fprintf( fp, "%" PRFILESYSTEM_PATH, entry.srcfile.lexically_normal().c_str() );
		}
		fprintf( fp, "\n" );

		if ( entry.type == EntryType::EntryDir )
		{
			entry.subdir->OutputLBAlisting( fp, level+1 );
		}
	}

	if ( level > 0 )
	{
		fprintf( fp, "    End   %s\n", name.c_str() );
	}
}


int iso::DirTreeClass::CalculatePathTableLen(const DIRENTRY& dirEntry) const
{
	// Put identifier (empty if first entry)
	int len = 8 + GetIDLength(dirEntry.id);

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

std::unique_ptr<iso::PathTableClass> iso::DirTreeClass::GenPathTableSub(unsigned short& index, const unsigned short parentIndex) const
{
	auto table = std::make_unique<PathTableClass>();
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryType::EntryDir )
		{
			PathEntryClass pathEntry;

			pathEntry.dir_id	= entry.id;
			pathEntry.dir_index = index++;
			pathEntry.dir_parent_index = parentIndex;
			pathEntry.dir_lba	= entry.lba;
			table->entries.emplace_back( std::move(pathEntry) );
		}
	}

	size_t entryID = 0;
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryType::EntryDir )
		{
			auto& pathEntry = table->entries[entryID++];
			auto sub = entry.subdir->GenPathTableSub(index, pathEntry.dir_index);
			if (!sub->entries.empty())
			{
				pathEntry.sub = std::move(sub);
			}
		}
	}
	return table;
}

int iso::DirTreeClass::GeneratePathTable(const DIRENTRY& root, unsigned char* buff, bool msb) const
{
	unsigned short index = 1;

	// Write out root explicitly (since there is no DirTreeClass including it)
	PathEntryClass rootEntry;
	rootEntry.dir_id = root.id;
	rootEntry.dir_parent_index = index;
	rootEntry.dir_index = index++;
	rootEntry.dir_lba = root.lba;
	rootEntry.sub = GenPathTableSub(index, rootEntry.dir_parent_index);

	PathTableClass pathTable;
	pathTable.entries.emplace_back(std::move(rootEntry));

	unsigned char* newbuff = pathTable.GenTableData( buff, msb );

	return (int)(newbuff-buff);

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

void iso::WriteLicenseData(cd::IsoWriter* writer, void* data)
{
	auto licenseSectors = writer->GetSectorViewM2F2(0, 12, cd::IsoWriter::EdcEccForm::Form1);
	licenseSectors->WriteMemory(data, 2336 * 12);

	auto licenseBlankSectors = writer->GetSectorViewM2F1(12, 4, cd::IsoWriter::EdcEccForm::Form2);
	licenseBlankSectors->WriteBlankSectors(4);
}

template<size_t N>
static void CopyStringPadWithSpaces(char (&dest)[N], const char* src)
{
	auto begin = std::begin(dest);
	auto end = std::end(dest);

	if ( src != nullptr )
	{
		size_t i = 0;
		const size_t len = strlen(src);
		for (; begin != end && i < len; ++begin, ++i)
		{
			*begin = std::toupper( src[i] );
		}
	}

	// Pad the remaining space with spaces
	std::fill( begin, end, ' ' );
}

static cd::ISO_LONG_DATESTAMP GetLongDateFromString(const char* str)
{
	cd::ISO_LONG_DATESTAMP date;

	bool gotDate = false;
	if (str)
	{
		cd::ISO_DATESTAMP shortDate = GetDateFromString(str, &gotDate);
		if (gotDate)
		{
			date = GetLongDateFromDate(shortDate);
		}
	}
	
	if (!gotDate)
	{
		date = GetUnspecifiedLongDate();
	}
	return date;
}

void iso::WriteDescriptor(cd::IsoWriter* writer, const iso::IDENTIFIERS& id, const DIRENTRY& root, int imageLen)
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
	CopyStringPadWithSpaces( isoDescriptor.abstractFileIdentifier, nullptr );
	CopyStringPadWithSpaces( isoDescriptor.bibliographicFilelIdentifier, nullptr );

	isoDescriptor.volumeCreateDate = GetLongDateFromDate( root.date );
	isoDescriptor.volumeModifyDate = GetLongDateFromString(id.ModificationDate);
	isoDescriptor.volumeEffectiveDate = isoDescriptor.volumeExpiryDate = GetUnspecifiedLongDate();
	isoDescriptor.fileStructVersion = 1;

	if ( !global::noXA )
	{
		strncpy( (char*)&isoDescriptor.appData[141], "CD-XA001", 8 );
	}

	const DirTreeClass* dirTree = root.subdir.get();
	unsigned int pathTableLen = dirTree->CalculatePathTableLen(root);
	unsigned int pathTableSectors = GetSizeInSectors(pathTableLen);

	isoDescriptor.volumeSetSize = cd::SetPair16( 1 );
	isoDescriptor.volumeSeqNumber = cd::SetPair16( 1 );
	isoDescriptor.sectorSize = cd::SetPair16( 2048 );
	isoDescriptor.pathTableSize = cd::SetPair32( pathTableLen );

	// Setup the root directory record
	isoDescriptor.rootDirRecord.entryLength = 34;
	isoDescriptor.rootDirRecord.extLength	= 0;
	isoDescriptor.rootDirRecord.entryOffs = cd::SetPair32(
		18+(pathTableSectors*4) );
	isoDescriptor.rootDirRecord.entrySize = cd::SetPair32(
		dirTree->CalculateDirEntryLen() );
	isoDescriptor.rootDirRecord.flags = 0x02;
	isoDescriptor.rootDirRecord.volSeqNum = cd::SetPair16( 1 );
	isoDescriptor.rootDirRecord.identifierLen = 1;
	isoDescriptor.rootDirRecord.identifier = 0x0;

	isoDescriptor.rootDirRecord.entryDate = root.date;

	isoDescriptor.pathTable1Offs = 18;
	isoDescriptor.pathTable2Offs = isoDescriptor.pathTable1Offs+
		pathTableSectors;
	isoDescriptor.pathTable1MSBoffs = isoDescriptor.pathTable2Offs+1;
	isoDescriptor.pathTable2MSBoffs =
		isoDescriptor.pathTable1MSBoffs+pathTableSectors;
	isoDescriptor.pathTable1MSBoffs = SwapBytes32( isoDescriptor.pathTable1MSBoffs );
	isoDescriptor.pathTable2MSBoffs = SwapBytes32( isoDescriptor.pathTable2MSBoffs );

	isoDescriptor.volumeSize = cd::SetPair32( imageLen );

	// Write the descriptor
	unsigned int currentHeaderLBA = 16;

	auto isoDescriptorSectors = writer->GetSectorViewM2F1(currentHeaderLBA, 2, cd::IsoWriter::EdcEccForm::Form1);
	isoDescriptorSectors->SetSubheader(cd::IsoWriter::SubEOL);

	isoDescriptorSectors->WriteMemory(&isoDescriptor, sizeof(isoDescriptor));

	// Write descriptor terminator;
	memset( &isoDescriptor, 0x00, sizeof(cd::ISO_DESCRIPTOR) );
	isoDescriptor.header.type = 255;
	isoDescriptor.header.version = 1;
	CopyStringPadWithSpaces( isoDescriptor.header.id, "CD001" );

	isoDescriptorSectors->WriteMemory(&isoDescriptor, sizeof(isoDescriptor));
	currentHeaderLBA += 2;

	// Generate and write L-path table
	const size_t pathTableSize = static_cast<size_t>(2048)*pathTableSectors;
	auto sectorBuff = std::make_unique<unsigned char[]>(pathTableSize);

	dirTree->GeneratePathTable( root, sectorBuff.get(), false );
	auto lpathTable1 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	lpathTable1->WriteMemory(sectorBuff.get(), pathTableSize);
	currentHeaderLBA += pathTableSectors;

	auto lpathTable2 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	lpathTable2->WriteMemory(sectorBuff.get(), pathTableSize);
	currentHeaderLBA += pathTableSectors;

	// Generate and write M-path table
	dirTree->GeneratePathTable( root, sectorBuff.get(), true );
	auto mpathTable1 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	mpathTable1->WriteMemory(sectorBuff.get(), pathTableSize);
	currentHeaderLBA += pathTableSectors;

	auto mpathTable2 = writer->GetSectorViewM2F1(currentHeaderLBA, pathTableSectors, cd::IsoWriter::EdcEccForm::Form1);
	mpathTable2->WriteMemory(sectorBuff.get(), pathTableSize);
	currentHeaderLBA += pathTableSectors;
}

unsigned char* iso::PathTableClass::GenTableData(unsigned char* buff, bool msb)
{
	for ( const PathEntryClass& entry : entries )
	{
		*buff++ = std::max<unsigned char>(1, entry.dir_id.length()); // Directory identifier length
		*buff++ = 0;						// Extended attribute record length (unused)

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

		buff += GetIDLength(entry.dir_id);
	}

	for ( const PathEntryClass& entry : entries )
	{
		if ( entry.sub != nullptr )
		{
			buff = entry.sub->GenTableData( buff, msb );
		}

	}

	return buff;

}
