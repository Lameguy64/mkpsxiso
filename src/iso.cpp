#include "global.h"
#include "iso.h"
#include "cd.h"
#include "xa.h"

#include <algorithm>

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

int iso::DirTreeClass::GetWavSize(const char* wavFile)
{
	FILE *fp;

	if ( !( fp = fopen( wavFile, "rb" ) ) )
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

int iso::DirTreeClass::PackWaveFile(cd::IsoWriter* writer, const char* wavFile, int pregap)
{
	FILE *fp;
	int waveLen;
	unsigned char buff[CD_SECTOR_SIZE];

	if ( !( fp = fopen( wavFile, "rb" ) ) )
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

bool iso::DirTreeClass::AddFileEntry(const char* id, int type, const char* srcfile, const EntryAttributes& attributes)
{
	struct stat fileAttrib;

    if ( stat( srcfile, &fileAttrib ) != 0 )
	{
		if ( !global::QuietMode )
		{
			printf("      ");
		}

		printf("ERROR: File not found: %s\n", srcfile);
		return false;
    }

	// Check if XA data is valid
	if ( type == EntryXA )
	{
		// Check header
		char buff[4];
		FILE* fp = fopen(srcfile, "rb");
		fread(buff, 1, 4, fp);
		fclose(fp);

		// Check if its a RIFF (WAV container)
		if ( strncmp(buff, "RIFF", 4) == 0 )
		{
			if (!global::QuietMode)
			{
				printf("      ");
			}

			printf("ERROR: %s is a WAV or is not properly ripped!\n", srcfile);

			return false;
		}

		// Check if size is a multiple of 2336 bytes
		if ( ( fileAttrib.st_size % 2336 ) != 0 )
		{
			if ( !global::QuietMode )
			{
				printf("      ");
			}

			printf("ERROR: %s is not a multiple of 2336 bytes.\n", srcfile);

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

			printf("WARNING: %s may not have a valid subheader. ", srcfile);
		}

	// Check STR data
	} else if ( type == EntrySTR ) {

		// Check header
		char buff[4];
		FILE* fp = fopen(srcfile, "rb");
		fread(buff, 1, 4, fp);
		fclose(fp);

		// Check if its a RIFF (WAV container)
		if ( strncmp(buff, "RIFF", 4) == 0 )
		{
			if (!global::QuietMode)
			{
				printf("      ");
			}

			printf("ERROR: %s is a WAV or is not properly ripped.\n", srcfile);

			return false;
		}

		// Check if size is a multiple of 2336 bytes
		if ( ( fileAttrib.st_size % 2336 ) != 0 )
		{
			if ( ( fileAttrib.st_size % 2048) == 0 )
			{
				type = EntrySTR_DO;
			}
			else
			{
				if ( !global::QuietMode )
				{
					printf("      ");
				}

				printf("ERROR: %s is not a multiple of 2336 or 2048 bytes.\n",
					srcfile);

				return false;
			}
		}

	}


	std::string temp_name = id;

	for ( int i=0; temp_name[i] != 0x00; i++ )
	{
		temp_name[i] = std::toupper( temp_name[i] );
	}

	temp_name += ";1";


	// Check if file entry already exists
    for ( int i=0; i<entries.size(); i++ )
	{
		if ( !entries[i].id.empty() )
		{
            if ( ( entries[i].type == EntryFile )
				&& ( icompare( entries[i].id, temp_name ) ) )
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

	if ( srcfile != nullptr )
	{
		entry.srcfile = srcfile;
	}

	if ( type == EntryDA )
	{
		entry.length = GetWavSize( srcfile );
	}
	else if ( type != EntryDir )
	{
		entry.length = fileAttrib.st_size;
	}

    entry.date = GetISODateStamp( fileAttrib.st_mtime, attributes.GMTOffs.value() );

	entriesInDir.push_back(entries.size());
	entries.emplace_back( std::move(entry) );

	return true;

}

void iso::DirTreeClass::AddDummyEntry(int sectors, int type)
{
	DIRENTRY entry {};

	// TODO: HUGE HACK, will be removed once EntryDummy is unified with EntryFile again
	entry.perms	=	type;
	entry.type		= EntryDummy;
	entry.length	= 2048*sectors;

	entriesInDir.push_back(entries.size());
	entries.emplace_back( std::move(entry) );
}

iso::DirTreeClass* iso::DirTreeClass::AddSubDirEntry(const char* id, const char* srcDir, const EntryAttributes& attributes)
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
		return currentSubdir->subdir.get();
	}

	time_t dirTime;
	struct stat fileAttrib;
    if ( stat( srcDir, &fileAttrib ) == 0 )
	{
		dirTime = fileAttrib.st_mtime;
	}
	else
	{
		dirTime = global::BuildTime;
	}

	DIRENTRY entry {};

	entry.id		= id;
	for ( char& ch : entry.id )
	{
		ch = toupper( ch );
	}

	entry.type		= EntryDir;
	entry.subdir	= std::make_unique<DirTreeClass>(entries, this);
	entry.attribs	= attributes.XAAttrib.value();
	entry.perms		= attributes.XAPerm.value();
	entry.GID		= attributes.GID.value();
	entry.UID		= attributes.UID.value();
	entry.date		= GetISODateStamp( dirTime, attributes.GMTOffs.value() );
	entry.length = entry.subdir->CalculateDirEntryLen();

	entriesInDir.push_back(entries.size());
	entries.emplace_back( std::move(entry) );

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
	// Set LBA of directory record of this class
	recordLBA = lba;

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

	for ( DIRENTRY& entry : entries )
	{
		// Set current LBA to directory record entry
		entry.lba = lba;

		// If it is a subdir
		if (entry.subdir != nullptr)
		{
			entry.subdir->name = entry.id;

			lba += entry.subdir->CalculateDirEntryLen() / 2048;

			// Recursively calculate the LBA of subdirectories
			//lba = entries[i].subdir->CalculateTreeLBA(lba);

			//entries[i].length = entries[i].subdir->CalculateDirEntryLen();
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
				if ( !first_track )
				{
					lba += ((entry.length+2351)/2352)+150;
					first_track = true;
				}
				else
				{
					lba += ((entry.length+2351)/2352);
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

	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];

		if ( entry.id.empty() )
		{
			continue;
		}

		int dataLen = 33;

		dataLen += 2*((entry.id.size()+1)/2);

		if ( ( entry.id.size()%2 ) == 0 )
		{
			dataLen++;
		}

		if ( !global::noXA )
		{
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
	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];

		if ( entry.type == EntryDir )
		{
			// Perform recursive call
            if ( entry.subdir != nullptr )
			{
				entry.subdir->SortDirectoryEntries();
			}
		}
	}

	std::sort(entriesInDir.begin(), entriesInDir.end(), [this](size_t left, size_t right)
		{
			return entries[left].id < entries[right].id;
		});
}

int iso::DirTreeClass::WriteDirEntries(cd::IsoWriter* writer, int LBA, int parentLBA, const cd::ISO_DATESTAMP& currentDirDate, const cd::ISO_DATESTAMP& parentDirDate) const
{
	char	dataBuff[2048] {};
	char*	dataBuffPtr=dataBuff;
	char	entryBuff[128];
	int		dirlen;

	writer->SeekToSector( LBA );

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
			dirEntry->entryOffs = cd::SetPair32( LBA );
			dirEntry->entryDate = currentDirDate;
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
			dirEntry->entryOffs = cd::SetPair32( parentLBA );
			dirEntry->entryDate = parentDirDate;
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

	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
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

		if ( (dirEntry->identifierLen%2) == 0 )
		{
			dataLen++;
		}

		if ( !global::noXA )
		{
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

int iso::DirTreeClass::WriteDirectoryRecords(cd::IsoWriter* writer, int LBA, int parentLBA, const cd::ISO_DATESTAMP& currentDirDate, const cd::ISO_DATESTAMP& parentDirDate)
{
	WriteDirEntries( writer, LBA, parentLBA, currentDirDate, parentDirDate );

	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
		if ( entry.type == EntryDir )
		{
			if ( !entry.subdir->WriteDirectoryRecords(
				writer, entry.lba, LBA, entry.date, currentDirDate ) )
			{
				return 0;
			}
		}
	}

	return 1;
}

int iso::DirTreeClass::WriteFiles(cd::IsoWriter* writer)
{
	first_track = false;

	for ( const DIRENTRY& entry : entries )
	{
		if ( ( entry.type == EntryDA ) && ( first_track ) )
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
					printf( "      Packing %s... ", entry.srcfile.c_str() );
				}

				FILE *fp = fopen( entry.srcfile.c_str(), "rb" );

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
				printf( "      Packing XA %s... ", entry.srcfile.c_str() );
			}

			FILE *fp = fopen( entry.srcfile.c_str(), "rb" );

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
				printf( "      Packing STR %s... ", entry.srcfile.c_str() );
			}

			FILE *fp = fopen( entry.srcfile.c_str(), "rb" );

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
					printf( "      Packing STR-DO %s... ", entry.srcfile.c_str() );
				}

				FILE *fp = fopen( entry.srcfile.c_str(), "rb" );

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
				printf( "      Packing DA %s... ", entry.srcfile.c_str() );
			}

			if ( PackWaveFile( writer, entry.srcfile.c_str(), first_track ) )
			{
				if (!global::QuietMode)
				{
					printf( "Done.\n" );
				}
			}

			first_track = true;
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

void iso::DirTreeClass::OutputHeaderListing(FILE* fp, int level)
{
	if ( level == 0 )
	{
		fprintf( fp, "#ifndef _ISO_FILES\n" );
		fprintf( fp, "#define _ISO_FILES\n\n" );
	}

	fprintf( fp, "/* %s */\n", name.c_str() );

	for ( int i=0; i<entries.size(); i++ )
	{
		if ( ( !entries[i].id.empty() ) && ( entries[i].type != EntryDir ) )
		{
			std::string temp_name;

			temp_name = "LBA_" + entries[i].id;

			for ( int c=0; c<temp_name.length(); c++ )
			{
				temp_name[c] = std::toupper( temp_name[c] );

				if ( temp_name[c] == '.' )
				{
					temp_name[c] = '_';
				}

				if ( temp_name[c] == ';' )
				{
					temp_name[c] = 0x00;
				}
			}

			fprintf( fp, "#define %s", temp_name.c_str() );

			for( int s=0; s<17-(int)entries[i].id.size(); s++ )
			{
				fprintf( fp, " " );
			}

			fprintf( fp, "%d\n", entries[i].lba );

		}
	}

	for ( int i=0; i<entries.size(); i++ )
	{
		if ( entries[i].type == EntryDir )
		{
			fprintf( fp, "\n" );
			entries[i].subdir->OutputHeaderListing( fp, level+1 );
		}
	}

	if ( level == 0 )
	{
		fprintf( fp, "\n#endif\n" );
	}
}

int iso::DirTreeClass::WriteCueEntries(FILE* fp, int* trackNum) const
{
	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
		if ( entry.type == EntryDA )
		{
			*trackNum += 1;
			fprintf( fp, "  TRACK %02d AUDIO\n", *trackNum );

			int trackLBA = entry.lba-150;

			if ( *trackNum == 2 )
			{
				fprintf( fp, "    PREGAP 00:02:00\n" );
			}
			else
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
	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];

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
			fprintf( fp, "%-10d", ((entry.length+2047)/2048) );
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
			fprintf( fp, "%-10d", entry.length );
		}
		else
		{
			fprintf( fp, "%-10s", "" );
		}

		// Write source file path
		if ( (!entry.id.empty()) && (entry.type != EntryDir) )
		{
			fprintf( fp, "%s", entry.srcfile.c_str() );
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


int iso::DirTreeClass::CalculatePathTableLenSub(const DIRENTRY& dirEntry) const
{
	int len = 8;

	// Put identifier (empty if first entry)
	len += 2*((dirEntry.id.size()+1)/2);

	for ( size_t index : dirEntry.subdir->entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
		if ( entry.type == EntryDir )
		{
			len += CalculatePathTableLenSub( entry );
		}
	}

	return len;
}

int iso::DirTreeClass::CalculatePathTableLen() const
{
	int len = 10;

	for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
		if ( entry.type == EntryDir )
		{
			len += CalculatePathTableLenSub( entry );
		}
	}

	return len;
}

void iso::DirTreeClass::GenPathTableSub(PathTableClass* table,
	DirTreeClass* dir, int parentIndex)
{
	for ( size_t index : dir->entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
		if ( entry.type == EntryDir )
		{
			auto pathEntry = std::make_unique<PathEntryClass>();

			pathEntry->dir_id		= entry.id;
			pathEntry->dir_level	= parentIndex;
			pathEntry->dir_lba		= entry.lba;
			pathEntry->dir			= entry.subdir.get();

			table->entries.push_back( std::move(pathEntry) );
		}
	}

	for ( std::unique_ptr<PathEntryClass>& entry : table->entries )
	{
		DirTreeClass* subdir = entry->dir;
		auto sub = std::make_unique<PathTableClass>();

		GenPathTableSub( sub.get(), subdir, parentIndex + 1 );
		entry->sub = std::move(sub);
	}
}

int iso::DirTreeClass::GeneratePathTable(unsigned char* buff, bool msb)
{
	PathTableClass pathTable;

	GenPathTableSub(&pathTable, this, 1);

	*buff++ = 1;	// Directory identifier length
	*buff++ = 0;	// Extended attribute record length (unused)

	// Write LBA and directory number index
	unsigned int lba = recordLBA;
	unsigned short dirIndex = 1;
	if ( msb )
	{
		lba = SwapBytes32( lba );
		dirIndex = SwapBytes16( dirIndex );
	}
	memcpy( buff, &lba, sizeof(lba) );
	memcpy( buff+4, &dirIndex, sizeof(dirIndex) );

	buff += 6;

	// Put identifier (nullptr if first entry)
	memset( buff, 0x00, 2 );
	buff += 2;

	unsigned char* newbuff = pathTable.GenTableData( buff, msb );

	return (int)(newbuff-buff);

}

int iso::DirTreeClass::GetFileCountTotal() const
{
    int numfiles = 0;

    for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
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

    for ( size_t index : entriesInDir )
	{
		const DIRENTRY& entry = entries[index];
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

void iso::WriteDescriptor(cd::IsoWriter* writer, iso::IDENTIFIERS id,
	iso::DirTreeClass* dirTree, const cd::ISO_DATESTAMP& volumeDate, int imageLen)
{
	cd::ISO_DESCRIPTOR	isoDescriptor {};

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

	isoDescriptor.volumeCreateDate = GetLongDateFromDate( volumeDate );
	isoDescriptor.volumeModifyDate = isoDescriptor.volumeEffectiveDate = isoDescriptor.volumeExpiryDate = GetUnspecifiedLongDate();
	isoDescriptor.fileStructVersion = 1;

	if ( !global::noXA )
	{
		strncpy( (char*)&isoDescriptor.appData[141], "CD-XA001", 8 );
	}

	int pathTableLen = dirTree->CalculatePathTableLen();
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

	isoDescriptor.rootDirRecord.entryDate = volumeDate;

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
	writer->WriteBytes( &isoDescriptor, sizeof(cd::ISO_DESCRIPTOR),
		cd::IsoWriter::EdcEccForm1 );

	// Write descriptor terminator;
	memset( &isoDescriptor, 0x00, sizeof(cd::ISO_DESCRIPTOR) );
	isoDescriptor.header.type = 255;
	isoDescriptor.header.version = 1;
	CopyStringPadWithSpaces( isoDescriptor.header.id, "CD001" );

	writer->SetSubheader( cd::IsoWriter::SubEOF );
	writer->WriteBytes( &isoDescriptor, sizeof(cd::ISO_DESCRIPTOR),
		cd::IsoWriter::EdcEccForm1 );

	// Generate and write L-path table
	auto sectorBuff = std::make_unique<unsigned char[]>(static_cast<size_t>(2048)*pathTableSectors);

	dirTree->GeneratePathTable( sectorBuff.get(), false );
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
	dirTree->GeneratePathTable( sectorBuff.get(), true );
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

iso::PathEntryClass::PathEntryClass() {

	dir_level = 0;
	dir_lba = 0;

	dir = nullptr;
	sub = nullptr;

}

iso::PathEntryClass::~PathEntryClass() {
}

iso::PathTableClass::PathTableClass() {

}

iso::PathTableClass::~PathTableClass() {
}

unsigned char* iso::PathTableClass::GenTableData(unsigned char* buff, bool msb)
{
	for ( const std::unique_ptr<PathEntryClass>& entry : entries )
	{
		*buff++ = entry->dir_id.length();	// Directory identifier length
		*buff++ = 0;						// Extended attribute record length (unused)

		// Write LBA and directory number index
		unsigned int lba = entry->dir_lba;
		unsigned short dirLevel = entry->dir_level;

		if ( msb )
		{
			lba = SwapBytes32( lba );
			dirLevel = SwapBytes16( dirLevel );
		}
		memcpy( buff, &lba, sizeof(lba) );
		memcpy( buff+4, &dirLevel, sizeof(dirLevel) );

		buff += 6;

		// Put identifier (nullptr if first entry)
		strncpy( (char*)buff, entry->dir_id.c_str(),
			entry->dir_id.length() );

		buff += 2*((entry->dir_id.length()+1)/2);
	}

	for ( const std::unique_ptr<PathEntryClass>& entry : entries )
	{
		if ( entry->sub )
		{
			buff = entry->sub->GenTableData( buff, msb );
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
