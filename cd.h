#ifndef _CD_H
#define _CD_H

#include <stdio.h>

#ifdef __WIN32__
#include <windows.h>
#else
#include <unistd.h>
#endif

// Sector size in bytes (do not change)
#define CD_SECTOR_SIZE		2352


// CD and ISO 9660 reader namespace
namespace cd {

	/// Structure for a mode 2 form 1 sector (used in regular files)
	typedef struct {
		u_char  sync[12];	/// Sync pattern (usually 00 FF FF FF FF FF FF FF FF FF FF 00)
		u_char  addr[3];	/// Sector address (see below for encoding details)
		u_char  mode;		/// Mode (usually 2 for Mode 2 Form 1/2 sectors)
		u_char  subHead[8];	/// Sub-header (00 00 08 00 00 00 08 00 for Form 1 data sectors)
		u_char	data[2048];	/// Data (form 1)
		u_char  edc[4];		/// Error-detection code (CRC32 of data area)
		u_char  ecc[276];	/// Error-correction code (uses Reed-Solomon ECC algorithm)
	} SECTOR_M2F1;

	/**	Regular data files are usually stored in this sector format as it has ECC error correction which
	 *	is important for program data and other sensitive data files.
	 *
	 *  The 3 address bytes of an M2F1 and M2F2 format sector are actually just timecode values. The 1st
	 *	byte is minutes, 2nd byte is seconds and the 3rd byte is frames (or sectors, 75 per second on CD).
	 *
	 *	The unusual thing about how the address bytes are encoded is that values must be specified in hex
	 *	rather than decimal (0x20 instead of 20, 0x15 instead of 15) so values must be converted by using
	 *	the following algorithm:
	 *
	 *		(16*(v/10))+(v%10)
	 *
	 *	So, for a sector address of 2 minutes, 30 seconds and 35 frames in timecode format, it should look
	 *	like this in hex:
	 *
	 *		0x02 0x35 0x30
	 *
	 */

	/// Structure for a mode 2 form 2 sector (used in STR/XA files)
	typedef struct {
		u_char  sync[12];	/// Sync pattern (usually 00 FF FF FF FF FF FF FF FF FF FF 00)
		u_char  addr[3];	/// Sector address (a 24-bit big-endian integer. starts at 200, 201 an onwards)
		u_char  mode;		/// Mode (usually 2 for Mode 2 Form 1/2 sectors)
		u_char	data[2336];	/// 8 bytes Subheader, 2324 bytes Data (form 2), and 4 bytes ECC
	} SECTOR_M2F2;

	// Set struct alignment to 1 because the ISO file system is not very memory alignment friendly which will
	// result to alignment issues when reading data entries with structs.
	#pragma pack(push, 1)

	/// Structure of a double-endian unsigned short word
	typedef struct {
		u_short lsb;	/// LSB format 16-bit word
		u_short msb;	/// MSB format 16-bit word
	} ISO_USHORT_PAIR;

	/// Structure of a double-endian unsigned int word
	typedef struct {
		u_int lsb;		/// LSB format 32-bit word
		u_int msb;		/// MSB format 32-bit word
	} ISO_UINT_PAIR;

	/// ISO descriptor header structure
	typedef struct {
		u_char type;		/// Volume descriptor type (1 is descriptor, 255 is descriptor terminator)
		char id[5];			/// Volume descriptor ID (always CD001)
		u_short version;	/// Volume descriptor version (always 0x01)
	} ISO_DESCRIPTOR_HEADER;

	/// Structure of a date stamp for ISO_DIR_ENTRY structure
	typedef struct {
		u_char year;	/// number of years since 1900
		u_char month;	/// month, where 1=January, 2=February, etc.
		u_char day;		/// day of month, in the range from 1 to 31
		u_char hour;	/// hour, in the range from 0 to 23
		u_char minute;	/// minute, in the range from 0 to 59
		u_char second;	/// Second, in the range from 0 to 59
		u_char GMToffs;	/// Greenwich Mean Time offset
	} ISO_DATESTAMP;

	/// Structure of an ISO path table entry (specifically for the cd::IsoReader class)
	typedef struct {
		u_char nameLength;	/// Name length (or 1 for the root directory)
		u_char extLength;	/// Number of sectors in extended attribute record
		u_int dirOffs;		/// Number of the first sector in the directory, as a double word
		short dirLevel;		/// Index of the directory record's parent directory
		char* name;			/// Name (0 for the root directory)
							/// If nameLength is odd numbered, a padding byte will be present after the identifier text.
	} ISO_PATHTABLE_ENTRY;

	typedef struct {
		// Directory entry length (variable, use for parsing through entries)
		u_char entryLength;
		// Extended entry data length (always 0)
		u_char extLength;
		// Points to the LBA of the file/directory entry
		ISO_UINT_PAIR entryOffs;
		// Size of the file/directory entry
		ISO_UINT_PAIR entrySize;
		// Date & time stamp of entry
		ISO_DATESTAMP entryDate;
		// File flags (0x02 for directories, 0x00 for files)
		u_char flags;
		// Unit size (usually 0 even with Form 2 files such as STR/XA)
		u_char fileUnitSize;
		// Interleave gap size (usually 0 even with Form 2 files such as STR/XA)
		u_char interleaveGapSize;
		// Volume sequence number (always 1)
		ISO_USHORT_PAIR volSeqNum;
		// Identifier (file/directory name) length in bytes
		u_char identifierLen;
		// Pointer to identifier (placed here for convenience)
		// If identifierLen is even numbered, a padding byte will be present after the identifier text.
		char* identifier;
		void* extData;
	} ISO_DIR_ENTRY;

	// XA attribute struct (located right after the identifier string)
	typedef struct {
		u_int pad;		// Nothing but null bytes
		u_short type;	// XA attribute (0x8800 for directories, 0x800 for regular files, 0x0 for XA Form 2 files)
		char id[2];		// Always XA
		u_char pad2[6];	// Nothing but null bytes
	} ISO_XA_ATTRIB;

	typedef struct {
		u_char entryLength;		// Always 34 bytes
		u_char extLength;		// Always 0
		ISO_UINT_PAIR entryOffs;	// Should point to LBA 22
		ISO_UINT_PAIR entrySize;	// Size of entry extent
		ISO_DATESTAMP entryDate;	// Record date and time
		u_char flags;				// File flags
		u_char fileUnitSize;
		u_char interleaveGapSize;
		ISO_USHORT_PAIR volSeqNum;
		u_char identifierLen;		// 0x01
		u_char identifier;			// 0x01
	} ISO_ROOTDIR_HEADER;

	// ISO descriptor structure
	typedef struct {

		// ISO descriptor header
		ISO_DESCRIPTOR_HEADER header;
		// System ID (always PLAYSTATION)
		char systemID[32];
		// Volume ID (or label, can be blank or anything)
		char volumeID[32];
		// Unused null bytes
		u_char pad2[8];
		// Size of volume in sector units
		ISO_UINT_PAIR volumeSize;
		// Unused null bytes
		u_char pad3[32];
		// Number of discs in this volume set (always 1 for single volume)
		ISO_USHORT_PAIR volumeSetSize;
		// Number of this disc in volume set (always 1 for single volume)
		ISO_USHORT_PAIR volumeSeqNumber;
		// Size of sector in bytes (always 2352 bytes)
		ISO_USHORT_PAIR sectorSize;
		// Path table size in bytes (applies to all the path tables)
		ISO_UINT_PAIR pathTableSize;
		// LBA to Type-L path table
		u_int	pathTable1Offs;
		// LBA to optional Type-L path table (usually a copy of the primary path table)
		u_int	pathTable2Offs;
		// LBA to Type-L path table but with MSB format values
		u_int	pathTable1MSBoffs;
		// LBA to optional Type-L path table but with MSB format values (usually a copy of the main path table)
		u_int	pathTable2MSBoffs;
		// Directory entry for the root directory (similar to a directory entry)
		ISO_ROOTDIR_HEADER	rootDirRecord;
		// Volume set identifier (can be blank or anything)
		char	volumeSetIdentifier[128];
		// Publisher identifier (can be blank or anything)
		char	publisherIdentifier[128];
		// Data preparer identifier (can be blank or anything)
		char	dataPreparerIdentifier[128];
		// Application identifier (always PLAYSTATION)
		char	applicationIdentifier[128];
		// Copyright file in the file system identifier (can be blank or anything)
		char	copyrightFileIdentifier[37];
		// Abstract file in the file system identifier (can be blank or anything)
		char	abstractFileIdentifier[37];
		// Bibliographical file identifier in the file system (can be blank or anything)
		char	bibliographicFilelIdentifier[37];
		// Volume create date (in text format YYYYMMDDHHMMSSMMGG)
		char	volumeCreateDate[17];
		// Volume modify date (in text format YYYYMMDDHHMMSSMMGG)
		char	volumeModifyDate[17];
		// Volume expiry date (in text format YYYYMMDDHHMMSSMMGG)
		char	volumeExpiryDate[17];
		// Volume effective date (in text format YYYYMMDDHHMMSSMMGG)
		char	volumeEffeciveDate[17];
		// File structure version (always 1)
		u_char	fileStructVersion;
		// Padding
		u_char	dummy0;
		// Application specific data (says CD-XA001 at [141], the rest are null bytes)
		u_char	appData[512];
		// Padding
		u_char	pad4[653];

	} ISO_DESCRIPTOR;

	// Leave non-aligned structure packing
	#pragma pack(pop)

}

#endif // _CD_H
