#include "global.h"
#include "iso.h"
#include "cd.h"
#include "xa.h"
#include "platform.h"

#include <algorithm>
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

int iso::DirTreeClass::GetWavSize(const std::filesystem::path& wavFile)
{
	FILE *fp;

	if ( !( fp = OpenFile( wavFile, "rb" ) ) )
	{
		printf("ERROR: File not found.\n");
		return false;
	}

	// Get header chunk
	struct
	{
		char	id[4];
		int		size;
		char	format[4];
	} HeaderChunk;

	fread( &HeaderChunk, 1, sizeof(HeaderChunk), fp );

	if ( memcmp( &HeaderChunk.id, "RIFF", 4 ) ||
		memcmp( &HeaderChunk.format, "WAVE", 4 ) )
	{
		// It must be a raw
		fseek(fp, 0, SEEK_END);
		int wavlen = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		fclose( fp );
		return wavlen;
	}

	// Get header chunk
	struct
	{
		char	id[4];
		int		size;
		short	format;
		short	chan;
		int		freq;
		int		brate;
		short	balign;
		short	bps;
	} WAV_Subchunk1;

	fread( &WAV_Subchunk1, 1, sizeof(WAV_Subchunk1), fp );

	// Check if its a valid WAVE file
	if ( memcmp( &WAV_Subchunk1.id, "fmt ", 4 ) )
	{
		fclose( fp );
		return 0;
	}

	// Search for the data chunk
	struct
	{
		char	id[4];
		int		len;
	} WAV_Subchunk2;

	while ( 1 )
	{
		fread( &WAV_Subchunk2, 1, sizeof(WAV_Subchunk2), fp );

		if ( memcmp( &WAV_Subchunk2.id, "data", 4 ) )
		{
			fseek( fp, WAV_Subchunk2.len, SEEK_CUR );
		}
		else
		{
			break;
		}
	}

	fclose( fp );

	return 2352*((WAV_Subchunk2.len+2351)/2352);
}

int iso::DirTreeClass::PackWaveFile(cd::IsoWriter* writer, const std::filesystem::path& wavFile, bool pregap)
{
	FILE *fp;
	int waveLen;
	unsigned char buff[CD_SECTOR_SIZE];

	if ( !( fp = OpenFile( wavFile, "rb" ) ) )
	{
		printf("ERROR: File not found.\n");
		return false;
	}

	// Get header chunk
	struct
	{
		char	id[4];
		int		size;
		char	format[4];
	} HeaderChunk;

	fread( &HeaderChunk, 1, sizeof(HeaderChunk), fp );

	if ( memcmp( &HeaderChunk.id, "RIFF", 4 ) ||
		memcmp( &HeaderChunk.format, "WAVE", 4 ) )
	{

		// File must be a raw, pack it anyway
		memset(buff, 0x00, CD_SECTOR_SIZE);

		if ( pregap ) {

			// Write pregap region
			for ( int i=0; i<150; i++ ) {
				writer->WriteBytesRaw(buff, CD_SECTOR_SIZE);
			}

		}

		// Write data
		fseek(fp, 0, SEEK_END);
		waveLen = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		while ( waveLen > 0 )
		{
			memset(buff, 0x00, CD_SECTOR_SIZE);

			int readLen = waveLen;

			if (readLen > CD_SECTOR_SIZE)
			{
				readLen = CD_SECTOR_SIZE;
			}

			fread( buff, 1, readLen, fp );
			writer->WriteBytesRaw( buff, CD_SECTOR_SIZE );

			waveLen -= readLen;
		}

		printf("Packed as raw... ");

		fclose( fp );
		return true;
	}

	// Get header chunk
	struct
	{
		char	id[4];
		int		size;
		short	format;
		short	chan;
		int		freq;
		int		brate;
		short	balign;
		short	bps;
	} WAV_Subchunk1;

	fread( &WAV_Subchunk1, 1, sizeof(WAV_Subchunk1), fp );

	// Check if its a valid WAVE file
	if ( memcmp( &WAV_Subchunk1.id, "fmt ", 4 ) )
	{
		if ( !global::QuietMode )
		{
			printf( "\n    " );
		}

		printf( "ERROR: Unsupported WAV format.\n" );

		fclose( fp );
		return false;;
	}


    if ( (WAV_Subchunk1.chan != 2) || (WAV_Subchunk1.freq != 44100) ||
		(WAV_Subchunk1.bps != 16) )
	{
		if ( !global::QuietMode )
		{
			printf( "\n    " );
		}

		printf( "ERROR: Only 44.1KHz, 16-bit Stereo WAV files are supported.\n" );

		fclose( fp );
		return false;
    }

	// Search for the data chunk
	struct
	{
		char	id[4];
		int		len;
	} WAV_Subchunk2;

	while ( 1 )
	{
		fread( &WAV_Subchunk2, 1, sizeof(WAV_Subchunk2), fp );

		if ( memcmp( &WAV_Subchunk2.id, "data", 4 ) )
		{
			fseek( fp, WAV_Subchunk2.len, SEEK_CUR );
		}
		else
		{
			break;
		}
	}

	waveLen = WAV_Subchunk2.len;

	// Write pregap region
	memset(buff, 0x00, CD_SECTOR_SIZE);
	if ( pregap )
	{
		for ( int i=0; i<150; i++ )
		{
			writer->WriteBytesRaw(buff, CD_SECTOR_SIZE);
		}
	}

	// Write data
	while ( waveLen > 0 )
	{
		memset(buff, 0x00, CD_SECTOR_SIZE);

        int readLen = waveLen;

        if (readLen > CD_SECTOR_SIZE)
		{
			readLen = CD_SECTOR_SIZE;
		}

		fread( buff, 1, readLen, fp );
        writer->WriteBytesRaw( buff, CD_SECTOR_SIZE );

        waveLen -= readLen;
	}

	fclose( fp );
	return true;
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

	entry.type		= EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries);
	entry.date		= volumeDate;
	entry.length	= entry.subdir->CalculateDirEntryLen();

	entries.emplace_back( std::move(entry) );

	return entries.back();
}

bool iso::DirTreeClass::AddFileEntry(const char* id, int type, const std::filesystem::path& srcfile, const EntryAttributes& attributes)
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
	if ( type == EntryXA )
	{
		// Check header
		char buff[4];
		FILE* fp = OpenFile(srcfile, "rb");
		fread(buff, 1, 4, fp);
		fclose(fp);

		// Check if its a RIFF (WAV container)
		if ( strncmp(buff, "RIFF", 4) == 0 )
		{
			if (!global::QuietMode)
			{
				printf("      ");
			}

			printf("ERROR: %" PRFILESYSTEM_PATH " is a WAV or is not properly ripped!\n", srcfile.lexically_normal().c_str());

			return false;
		}

		// Check if size is a multiple of 2336 bytes
		if ( ( fileAttrib->st_size % 2336 ) != 0 )
		{
			if ( !global::QuietMode )
			{
				printf("      ");
			}

			printf("ERROR: %" PRFILESYSTEM_PATH " is not a multiple of 2336 bytes.\n", srcfile.lexically_normal().c_str());

			if ( !global::QuietMode )
			{
				printf("      ");
			}

			printf("Did you create your multichannel XA file with subheader data?\n");

			return false;
		}

		// Check if first sub header is valid usually by checking
		// if the first four bytes match the next four bytes
		else if ( ((int*)buff)[0] == ((int*)buff)[1] )
		{
			if ( !global::QuietMode )
			{
				printf("      ");
			}

			printf("WARNING: %" PRFILESYSTEM_PATH " may not have a valid subheader. ", srcfile.lexically_normal().c_str());
		}

	// Check STR data
	} else if ( type == EntrySTR ) {

		// Check header
		char buff[4];
		FILE* fp = OpenFile(srcfile, "rb");
		fread(buff, 1, 4, fp);
		fclose(fp);

		// Check if its a RIFF (WAV container)
		if ( strncmp(buff, "RIFF", 4) == 0 )
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
				type = EntrySTR_DO;
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
            if ( ( entry.type == EntryFile )
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
	entry.attribs	= attributes.XAAttrib.value();
	entry.perms		= attributes.XAPerm.value();
	entry.GID		= attributes.GID.value();
	entry.UID		= attributes.UID.value();

	if ( !srcfile.empty() )
	{
		entry.srcfile = srcfile;
	}

	if ( type == EntryDA )
	{
		entry.length = GetWavSize( srcfile );
	}
	else if ( type != EntryDir )
	{
		entry.length = fileAttrib->st_size;
	}

    entry.date = GetISODateStamp( fileAttrib->st_mtime, attributes.GMTOffs.value() );

	entries.emplace_back(std::move(entry));
	entriesInDir.emplace_back(entries.back());

	return true;

}

void iso::DirTreeClass::AddDummyEntry(int sectors, int type)
{
	DIRENTRY entry {};

	// TODO: HUGE HACK, will be removed once EntryDummy is unified with EntryFile again
	entry.perms	=	type;
	entry.type		= EntryDummy;
	entry.length	= 2048*sectors;

	entries.emplace_back(std::move(entry));
	entriesInDir.emplace_back(entries.back());
}

iso::DirTreeClass* iso::DirTreeClass::AddSubDirEntry(const char* id, const std::filesystem::path& srcDir, const EntryAttributes& attributes, bool& alreadyExists)
{
	// Duplicate directory entries are allowed, but the subsequent occurences will not add
	// a new directory to 'entries'.
	// TODO: It's not possible now, but a warning should be issued if entry attributes are specified for the subsequent occurences
	// of the directory. This check probably needs to be moved outside of the function.
	auto currentSubdir = std::find_if(entries.begin(), entries.end(), [id](const auto& e)
		{
			return e.type == EntryDir && e.id == id;
		});

	if (currentSubdir != entries.end())
	{
		alreadyExists = true;
		return currentSubdir->subdir.get();
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

	entry.type		= EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries, this);
	entry.attribs	= attributes.XAAttrib.value();
	entry.perms		= attributes.XAPerm.value();
	entry.GID		= attributes.GID.value();
	entry.UID		= attributes.UID.value();
	entry.date		= GetISODateStamp( dirTime, attributes.GMTOffs.value() );
	entry.length = entry.subdir->CalculateDirEntryLen();

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
	bool passedSector = false;
	lba += CalculateDirEntryLen(&passedSector) / 2048;

	if ( ( global::NoLimit == false) && passedSector )
	{
		if (!global::QuietMode)
			printf("      ");

		printf("WARNING: Directory record ");
		PrintRecordPath();
		printf(" exceeds 2048 bytes.\n");
	}

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

			lba += entry.subdir->CalculateDirEntryLen() / 2048;
		}
		else
		{
			// Increment LBA by the size of file
			if ( entry.type == EntryFile || entry.type == EntrySTR_DO || entry.type == EntryDummy )
			{
				lba += (entry.length+2047)/2048;
			}
			else if ( entry.type == EntryXA || entry.type == EntrySTR )
			{
				lba += (entry.length+2335)/2336;
			}
			else if ( entry.type == EntryDA )
			{
				lba += ((entry.length+2351)/2352);

				// TODO: Configurable pregap
				if (!firstDAWritten)
				{
					lba += 150;
					//firstDAWritten = true;
				}
			}
		}
	}

	return lba;
}

int iso::DirTreeClass::CalculateDirEntryLen(bool* passedSector) const
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

		int dataLen = 33;

		dataLen += GetIDLength(entry.id);

		if ( !global::noXA )
		{
			dataLen = (dataLen + 1) & -2; // Align up to 2b
			dataLen += sizeof( cdxa::ISO_XA_ATTRIB );
		}

		if ( ((dirEntryLen%2048)+dataLen) > 2048 )
		{
			dirEntryLen = (2048*(dirEntryLen/2048))+(dirEntryLen%2048);
		}

		dirEntryLen += dataLen;
	}

	if (dirEntryLen > 2048 && passedSector != nullptr)
	{
		*passedSector = true;
	}

	return 2048*((dirEntryLen+2047)/2048);
}

void iso::DirTreeClass::SortDirectoryEntries()
{
	// Search for directories
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryDir )
		{
			// Perform recursive call
            if ( entry.subdir != nullptr )
			{
				entry.subdir->SortDirectoryEntries();
			}
		}
	}

	std::sort(entriesInDir.begin(), entriesInDir.end(), [this](const auto& left, const auto& right)
		{
			return left.get().id < right.get().id;
		});
}

int iso::DirTreeClass::WriteDirEntries(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir) const
{
	char	dataBuff[2048] {};
	char*	dataBuffPtr=dataBuff;
	char	entryBuff[128];
	int		dirlen;

	writer->SeekToSector( dir.lba );

	for( int i=0; i<2; i++ )
	{
		cd::ISO_DIR_ENTRY* dirEntry = (cd::ISO_DIR_ENTRY*)dataBuffPtr;

		dirEntry->volSeqNum = cd::SetPair16( 1 );
		dirEntry->identifierLen = 1;

		int dataLen = 32;

		if (i == 0)
		{
			// Current
			dirlen = 2048*((CalculateDirEntryLen()+2047)/2048);

			dirEntry->entrySize = cd::SetPair32( dirlen );
			dirEntry->entryOffs = cd::SetPair32( dir.lba );
			dirEntry->entryDate = dir.date;
		}
		else
		{
			// Parent
			if ( parent ) {
				dirlen = parent->CalculateDirEntryLen();
			} else {
				dirlen = CalculateDirEntryLen();
			}
			dirlen = 2048*((dirlen+2047)/2048);

			dirEntry->entrySize = cd::SetPair32( dirlen );
			dirEntry->entryOffs = cd::SetPair32( parentDir.lba );
			dirEntry->entryDate = parentDir.date;
			dataBuffPtr[dataLen+1] = 0x01;
		}

		dataLen += 2;

		if ( !global::noXA )
		{
			cdxa::ISO_XA_ATTRIB* xa = (cdxa::ISO_XA_ATTRIB*)(dataBuffPtr+dataLen);
			memset( xa, 0x00, sizeof(*xa) );

			xa->id[0] = 'X';
			xa->id[1] = 'A';
			xa->attributes  = 0x558d;

			dataLen += sizeof(*xa);
		}

		dirEntry->flags = 0x02;
		dirEntry->entryLength = dataLen;

		dataBuffPtr += dataLen;
	}

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.id.empty() )
		{
			continue;
		}

		memset( entryBuff, 0x00, 128 );
		cd::ISO_DIR_ENTRY* dirEntry = (cd::ISO_DIR_ENTRY*)entryBuff;

		if ( entry.type == EntryDir )
		{
			dirEntry->flags = 0x02;
		}
		else
		{
			dirEntry->flags = 0x00;
		}

		// File length correction for certain file types
		int lba = entry.lba;
		int length = 0;

		if ( ( entry.type == EntryXA ) || ( entry.type == EntrySTR ) )
		{
			length = 2048*((entry.length+2335)/2336);
		}
		else if ( entry.type == EntrySTR_DO )
		{
			length = 2048*((entry.length+2047)/2048);
		}
		else if ( entry.type == EntryDA )
		{
			length = 2048*((entry.length+2351)/2352);
			lba += 150;
		}
		else
		{
			length = entry.length;
		}

		dirEntry->entryOffs = cd::SetPair32( lba );
		dirEntry->entrySize = cd::SetPair32( length );
		dirEntry->volSeqNum = cd::SetPair16( 1 );

		dirEntry->identifierLen = entry.id.length();
		dirEntry->entryDate = entry.date;

		int dataLen = 33;

		strncpy( &entryBuff[dataLen], entry.id.c_str(), dirEntry->identifierLen );
		dataLen += dirEntry->identifierLen;

		if ( !global::noXA )
		{
			dataLen = (dataLen + 1) & -2; // Align up to 2b
			cdxa::ISO_XA_ATTRIB* xa = (cdxa::ISO_XA_ATTRIB*)(entryBuff+dataLen);
			memset(xa, 0x00, sizeof(*xa));

			xa->id[0] = 'X';
			xa->id[1] = 'A';

			unsigned short attributes = entry.perms;
			if ( (entry.type == EntryFile) ||
				(entry.type == EntrySTR_DO) ||
				(entry.type == EntryDummy) )
			{
				attributes |= 0x800;
			}
			else if (entry.type == EntryDA)
			{
				attributes |= 0x4000;
			}
			else if ( (entry.type == EntrySTR) ||
				(entry.type == EntryXA) )
			{
				attributes |= entry.attribs != 0xFFu ? (entry.attribs << 8) : 0x3800;
				xa->filenum = 1;
			}
			else if (entry.type == EntryDir)
			{
				attributes |= 0x8800;
			}

			if ( (entry.type == EntrySTR) ||
				(entry.type == EntryXA) )
			{
				xa->filenum = 1;
			}

			xa->attributes = SwapBytes16(attributes);
			xa->ownergroupid = SwapBytes16(entry.GID);
			xa->owneruserid = SwapBytes16(entry.UID);

			dataLen += sizeof(*xa);
		}

		dirEntry->entryLength = dataLen;

		if ( (dataBuffPtr+dataLen) > (dataBuff+2047) )
		{
			writer->SetSubheader( cd::IsoWriter::SubData );
			writer->WriteBytes( dataBuff, 2048, cd::IsoWriter::EdcEccForm1 );

			memset( dataBuff, 0x00, 2048 );
			dataBuffPtr = dataBuff;
		}

		memcpy( dataBuffPtr, entryBuff, dataLen );
		dataBuffPtr += dataLen;
	}

	writer->SetSubheader( cd::IsoWriter::SubEOF );
	writer->WriteBytes( dataBuff, 2048, cd::IsoWriter::EdcEccForm1 );

	return 1;
}

int iso::DirTreeClass::WriteDirectoryRecords(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir)
{
	WriteDirEntries( writer, dir, parentDir );

	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryDir )
		{
			if ( !entry.subdir->WriteDirectoryRecords(writer, entry, dir) )
			{
				return 0;
			}
		}
	}

	return 1;
}

int iso::DirTreeClass::WriteFiles(cd::IsoWriter* writer)
{
	bool firstDAWritten = false;

	for ( const DIRENTRY& entry : entries )
	{
		// TODO: Configurable pregap
		if ( ( entry.type == EntryDA ) && firstDAWritten )
		{
			writer->SeekToSector( entry.lba-150 );
		}
		else
		{
			writer->SeekToSector( entry.lba );
		}

		// Write files as regular data sectors
		if ( entry.type == EntryFile )
		{
			char buff[2048];

			if ( !entry.srcfile.empty() )
			{
				if ( !global::QuietMode )
				{
					printf( "      Packing %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
				}

				FILE *fp = OpenFile( entry.srcfile, "rb" );

				writer->SetSubheader( cd::IsoWriter::SubData );

				size_t totalBytesRead = 0;
				while( totalBytesRead < entry.length )
				{
					memset( buff, 0x00, 2048 );
					totalBytesRead += fread( buff, 1, 2048, fp );

					if ( totalBytesRead >= entry.length )
					{
						writer->SetSubheader( cd::IsoWriter::SubEOF );
					}

					writer->WriteBytes( buff, 2048, cd::IsoWriter::EdcEccForm1 );
				}

				fclose( fp );

				if ( !global::QuietMode )
				{
					printf("Done.\n");
				}

			}
			else
			{
				memset( buff, 0x00, 2048 );

				writer->SetSubheader( cd::IsoWriter::SubData );

				size_t totalBytesRead = 0;
				while( totalBytesRead < entry.length )
				{
					totalBytesRead += 2048;
					if ( totalBytesRead >= entry.length )
					{
						writer->SetSubheader( cd::IsoWriter::SubEOF );
					}
					writer->WriteBytes( buff, 2048, cd::IsoWriter::EdcEccForm1 );
				}
			}

		// Write XA audio streams as Mode 2 Form 2 sectors without ECC
		}
		else if (entry.type == EntryXA)
		{
			char buff[2336];

			if (!global::QuietMode)
			{
				printf( "      Packing XA %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
			}

			FILE *fp = OpenFile( entry.srcfile, "rb" );

			while( !feof( fp ) )
			{
				fread( buff, 1, 2336, fp );
				writer->WriteBytesXA(buff, 2336, cd::IsoWriter::EdcEccForm2);
			}

			fclose( fp );

			if ( !global::QuietMode )
			{
				printf( "Done.\n" );
			}

		// Write STR video streams as Mode 2 Form 1 (video sectors) and Mode 2 Form 2 (XA audio sectors)
		// Video sectors have EDC/ECC while XA does not
		}
		else if ( entry.type == EntrySTR )
		{
			char buff[2336];

			if ( !global::QuietMode )
			{
				printf( "      Packing STR %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
			}

			FILE *fp = OpenFile( entry.srcfile, "rb" );

			while ( !feof( fp ) )
			{
				memset( buff, 0x00, 2336 );
				fread( buff, 1, 2336, fp );

				// Check submode if sector is mode 2 form 2
				if ( buff[2]&0x20 )
				{
				    // If so, write it as an XA sector
					writer->WriteBytesXA( buff, 2336, cd::IsoWriter::EdcEccForm2 );

				}
				else
				{
					// Otherwise, write it as Mode 2 Form 1
					writer->WriteBytesXA( buff, 2336, cd::IsoWriter::EdcEccForm1 );
				}

			}

			fclose( fp );

			if (!global::QuietMode)
			{
				printf( "Done.\n" );
			}

		// Write data only STR streams as Mode 2 Form 1
		}
		else if ( entry.type == EntrySTR_DO )
		{
			char buff[2048];

			if ( !entry.srcfile.empty() )
			{
				if ( !global::QuietMode )
				{
					printf( "      Packing STR-DO %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
				}

				FILE *fp = OpenFile( entry.srcfile, "rb" );

				writer->SetSubheader( cd::IsoWriter::SubSTR );

				while( !feof( fp ) )
				{
					memset( buff, 0x00, 2048 );
					fread( buff, 1, 2048, fp );

					writer->WriteBytes( buff, 2048, cd::IsoWriter::EdcEccForm1 );
				}

				fclose( fp );

				writer->SetSubheader(0x00080000);

				if ( !global::QuietMode )
				{
					printf("Done.\n");
				}

			}
			else
			{
				memset( buff, 0x00, 2048 );

				writer->SetSubheader( cd::IsoWriter::SubData );

				size_t totalBytesRead = 0;
				while( totalBytesRead < entry.length )
				{
					totalBytesRead += 2048;
					if ( totalBytesRead >= entry.length )
					{
						writer->SetSubheader( cd::IsoWriter::SubEOF );
					}
					writer->WriteBytes( buff, 2048, cd::IsoWriter::EdcEccForm1 );
				}
			}

		// Write DA files as audio tracks
		}
		else if ( entry.type == EntryDA )
		{
			if ( !global::QuietMode )
			{
				printf( "      Packing DA %" PRFILESYSTEM_PATH "... ", entry.srcfile.lexically_normal().c_str() );
			}

			// TODO: Configurable pregap
			if ( PackWaveFile( writer, entry.srcfile, true/*!firstDAWritten*/ ) )
			{
				if (!global::QuietMode)
				{
					printf( "Done.\n" );
				}
			}

			//firstDAWritten = true;
		}
		/*else if ( entry.type == EntryDir )
		{
			entry.subdir->WriteFiles( writer );
		}*/
		// Write dummies as gaps without data
		else if ( entry.type == EntryDummy )
		{
			char buff[2048] {};

			// TODO: HUGE HACK, will be removed once EntryDummy is unified with EntryFile again
			const bool isForm2 = entry.perms != 0;
			int eccEdcEncode;
			if (isForm2)
			{
				writer->SetSubheader(0x00200000);
				eccEdcEncode = cd::IsoWriter::EdcEccForm2;
			}
			else
			{
				writer->SetSubheader(0u);
				eccEdcEncode = cd::IsoWriter::EdcEccForm1;
			}

			size_t totalBytesRead = 0;
			while( totalBytesRead < entry.length )
			{
				totalBytesRead += 2048;
				writer->WriteBytes( buff, 2048, eccEdcEncode );
			}
		}
	}

	return 1;
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
		if ( !entry.id.empty() && entry.type != EntryDir )
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
		if ( entry.type == EntryDir )
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

int iso::DirTreeClass::WriteCueEntries(FILE* fp, int* trackNum) const
{
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		if ( entry.type == EntryDA )
		{
			*trackNum += 1;
			fprintf( fp, "  TRACK %02d AUDIO\n", *trackNum );

			int trackLBA = entry.lba;

			// TODO: Configurable pregap?
			/*if ( *trackNum == 2 )
			{
				fprintf( fp, "    PREGAP 00:02:00\n" );
			}
			else*/
			{
				fprintf( fp, "    INDEX 00 %02d:%02d:%02d\n",
					(trackLBA/75)/60, (trackLBA/75)%60,
					trackLBA%75 );
			}

			trackLBA += 150;

			fprintf( fp, "    INDEX 01 %02d:%02d:%02d\n",
				(trackLBA/75)/60, (trackLBA/75)%60, trackLBA%75 );

		}
		else if ( entry.type == EntryDir )
		{
			entry.subdir->WriteCueEntries( fp, trackNum );
		}

	}

	return *trackNum;
}

void LBAtoTimecode(int lba, char* timecode)
{
	sprintf( timecode, "%02d:%02d:%02d", (lba/75)/60, (lba/75)%60, (lba%75) );
}

void iso::DirTreeClass::OutputLBAlisting(FILE* fp, int level) const
{
	for ( const auto& e : entriesInDir )
	{
		const DIRENTRY& entry = e.get();
		fprintf( fp, "    " );

		if ( !entry.id.empty() )
		{
			if ( entry.type == EntryFile )
			{
				fprintf( fp, "File  " );
			}
			else if ( entry.type == EntryDir )
			{
				fprintf( fp, "Dir   " );
			}
			else if ( ( entry.type == EntrySTR ) ||
				( entry.type == EntrySTR_DO ) )
			{
				fprintf( fp, "STR   " );
			}
			else if ( entry.type == EntryXA )
			{
				fprintf( fp, "XA    " );
			}
			else if ( entry.type == EntryDA )
			{
				fprintf( fp, "CDDA  " );
			}

			fprintf( fp, "%-17s", entry.id.c_str() );
		} else {

			fprintf( fp, "Dummy <DUMMY>          " );

		}

		// Write size in sector units
		if (entry.type != EntryDir)
		{
			fprintf( fp, "%-10lld", ((entry.length+2047)/2048) );
		}
		else
		{
			fprintf( fp, "%-10s", "" );
		}

		// Write LBA offset
		fprintf( fp, "%-10d", entry.lba );

		// Write Timecode
		char timecode[12];
		LBAtoTimecode( 150+entry.lba, timecode );
		fprintf( fp, "%-12s", timecode );

		// Write size in byte units
		if (entry.type != EntryDir)
		{
			fprintf( fp, "%-10lld", entry.length );
		}
		else
		{
			fprintf( fp, "%-10s", "" );
		}

		// Write source file path
		if ( (!entry.id.empty()) && (entry.type != EntryDir) )
		{
			fprintf( fp, "%" PRFILESYSTEM_PATH, entry.srcfile.lexically_normal().c_str() );
		}
		fprintf( fp, "\n" );

		if ( entry.type == EntryDir )
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
		if ( entry.type == EntryDir )
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
		if ( entry.type == EntryDir )
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
		if ( entry.type == EntryDir )
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
        if ( entry.type != EntryDir )
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
        if ( entry.type == EntryDir )
		{
			numdirs++;
            numdirs += entry.subdir->GetDirCountTotal();
		}
    }

    return numdirs;
}

void iso::WriteLicenseData(cd::IsoWriter* writer, void* data)
{
	writer->SeekToSector( 0 );
	writer->WriteBytesXA( data, 2336*12, cd::IsoWriter::EdcEccForm1 );

	char blank[2048] {};

	writer->SetSubheader(0x00200000);

	for( int i=0; i<4; i++ ) {

		writer->WriteBytes( blank, 2048, cd::IsoWriter::EdcEccForm2 );
	}

	writer->SetSubheader(0x00080000);
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
	int pathTableLen = dirTree->CalculatePathTableLen(root);
	int pathTableSectors = (pathTableLen+2047)/2048;

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
		2048*((dirTree->CalculateDirEntryLen()+2047)/2048) );
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
	writer->SeekToSector( 16 );
	writer->SetSubheader( cd::IsoWriter::SubEOL );
	writer->WriteBytes( &isoDescriptor, sizeof(isoDescriptor),
		cd::IsoWriter::EdcEccForm1 );

	// Write descriptor terminator;
	memset( &isoDescriptor, 0x00, sizeof(cd::ISO_DESCRIPTOR) );
	isoDescriptor.header.type = 255;
	isoDescriptor.header.version = 1;
	CopyStringPadWithSpaces( isoDescriptor.header.id, "CD001" );

	writer->SetSubheader( cd::IsoWriter::SubEOF );
	writer->WriteBytes( &isoDescriptor, sizeof(isoDescriptor),
		cd::IsoWriter::EdcEccForm1 );

	// Generate and write L-path table
	auto sectorBuff = std::make_unique<unsigned char[]>(static_cast<size_t>(2048)*pathTableSectors);

	dirTree->GeneratePathTable( root, sectorBuff.get(), false );
	writer->SetSubheader( cd::IsoWriter::SubData );

	for ( int i=0; i<pathTableSectors; i++ )
	{
		if ( i == pathTableSectors-1 )
		{
			writer->SetSubheader( cd::IsoWriter::SubEOF );
		}
		writer->WriteBytes( &sectorBuff[2048*i], 2048,
			cd::IsoWriter::EdcEccForm1 );
	}

	writer->SetSubheader( cd::IsoWriter::SubData );

	for( int i=0; i<pathTableSectors; i++ )
	{
		if ( i == pathTableSectors-1 )
		{
			writer->SetSubheader( cd::IsoWriter::SubEOF );
		}
		writer->WriteBytes( &sectorBuff[2048*i], 2048,
			cd::IsoWriter::EdcEccForm1 );
	}


	// Generate and write M-path table
	dirTree->GeneratePathTable( root, sectorBuff.get(), true );
	writer->SetSubheader( cd::IsoWriter::SubData );

	for ( int i=0; i<pathTableSectors; i++ )
	{
		if ( i == pathTableSectors-1 )
		{
			writer->SetSubheader( cd::IsoWriter::SubEOF );
		}
		writer->WriteBytes( &sectorBuff[2048*i], 2048,
			cd::IsoWriter::EdcEccForm1 );
	}

	writer->SetSubheader( cd::IsoWriter::SubData );

	for ( int i=0; i<pathTableSectors; i++ )
	{
		if ( i == pathTableSectors-1 )
		{
			writer->SetSubheader( cd::IsoWriter::SubEOF );
		}
		writer->WriteBytes( &sectorBuff[2048*i], 2048,
			cd::IsoWriter::EdcEccForm1 );
	}

	writer->SetSubheader( cd::IsoWriter::SubData );
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

iso::EntryAttributes iso::EntryAttributes::MakeDefault()
{
	EntryAttributes result;

	result.GMTOffs = DEFAULT_GMFOFFS;
	result.XAAttrib = DEFAULT_XAATRIB;
	result.XAPerm = DEFAULT_XAPERM;
	result.GID = result.UID = DEFAULT_OWNER_ID;

	return result;
}

iso::EntryAttributes iso::EntryAttributes::Overlay(EntryAttributes base, const EntryAttributes& derived)
{
	if (derived.GMTOffs.has_value())
	{
		base.GMTOffs = derived.GMTOffs;
	}
	if (derived.XAAttrib.has_value())
	{
		base.XAAttrib = derived.XAAttrib;
	}
	if (derived.XAPerm.has_value())
	{
		base.XAPerm = derived.XAPerm;
	}
	if (derived.GID.has_value())
	{
		base.GID = derived.GID;
	}
	if (derived.UID.has_value())
	{
		base.UID = derived.UID;
	}

	return base;
}
