#ifndef _ISO_H
#define _ISO_H

#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <malloc.h>
#include <tinyxml2.h>

#include "cdwriter.h"

namespace iso {

	enum EntryType {
		EntryFile = 0,
		EntryXA,
		EntrySTR,
		EntryDir,
	};

	typedef struct {
		const char*	SystemID;
		const char*	VolumeID;
		const char*	VolumeSet;
		const char*	Publisher;
		const char*	DataPreparer;
		const char*	Application;
	} IDENTIFIERS;

	typedef struct {

		char*	id;			/// Entry identifier (NULL if invisible dummy)
		int		length;		/// Length of file in bytes
		int		lba;		/// File LBA (in sectors)

		char*	srcfile;	/// Filename with path to source file (NULL if directory or dummy)
		int		type;		/// File type (0 - file, 1 - directory)
		void*	subdir;

		cd::ISO_DATESTAMP date;

	} DIRENTRY;

	class DirTreeClass {

		int dirIndex;

		/// Internal function for generating and writing directory records
		int	WriteDirEntries(cd::IsoWriter* writer, int lastLBA);

		/// Internal function for recursive path table length calculation
		int CalculatePathTableLenSub(DIRENTRY* dirEntry);

		/// Internal function for recursive path table generation
		unsigned char* GenPathTableSub(unsigned char* buff, DIRENTRY* dirEntry, int parentIndex, int msb);

		int			recordLBA;

	public:

		int			numentries;
		DIRENTRY*	entries;


		DirTreeClass();
		virtual ~DirTreeClass();

		/** Calculates the length of the directory record to be produced by this class in bytes.
		 *
		 *  Returns: Length of directory record in bytes.
		 */
		int	CalculateDirEntryLen();

		/** Calculates the LBA of all file and directory entries in the directory record and returns the next LBA
		 *	address.
		 *
		 *	lba			- LBA address where the first directory record begins (always 22).
		 */
		int CalculateTreeLBA(int lba = 22);

		/** Adds a file entry to the directory record.
		 *
		 *	*id			- The name of the file entry. It will be converted to uppercase and adds the file version
		 *				identifier (;1) automatically.
		 *	type		- The type of file to add, EntryFile is for standard files, EntryXA is for XA streams and
		 *				EntryStr is for STR streams. To add directories, use AddDirEntry().
		 *	*srcfile	- Path and filename to the source file.
		 */
		int	AddFileEntry(const char* id, int type, const char* srcfile);

		/** Adds an invisible dummy file entry to the directory record. Its invisible because its file entry
		 *	is not actually added to the directory record.
		 *
		 *	sectors	- The size of the dummy file in sector units (1 = 2048 bytes, 1024 = 2MB).
		 */
		void AddDummyEntry(int sectors);

		/** Generates a path table of all directories and subdirectories within this class' directory record.
		 *
		 *	*buff	- Pointer to a 2048 byte buffer to generate the path table to.
		 *	msb		- If true, generates a path table encoded in big-endian format, little-endian otherwise.
		 *
		 *	Returns: Length of path table in bytes.
		 */
		int GeneratePathTable(unsigned char* buff, int msb);

		/** Adds a subdirectory to the directory record.
		 *
		 *	*id		- The name of the subdirectory to add. It will be converted to uppercase automatically.
		 *
		 *	Returns: Pointer to another DirTreeClass for accessing the directory record of the subdirectory.
		 */
		DirTreeClass* AddSubDirEntry(const char* id);

		/**	Writes the source files assigned to the directory entries to a CD image. Its recommended to execute
		 *	this first before writing the actual file system.
		 *
		 *	*writer	- Pointer to a cd::IsoWriter class that is ready for writing.
		 */
		int	WriteFiles(cd::IsoWriter* writer);

		/**	Writes the file system of the directory records to a CD image. Execute this after the source files
		 *	have been written to the CD image.
		 *
		 *	*writer		- Pointer to a cd::IsoWriter class that is ready for writing.
		 *	lastDirLBA	- Used for recursive calls, always set to 0.
		 */
		int	WriteDirectoryRecords(cd::IsoWriter* writer, int lastDirLBA);

		void SortDirEntries();

		int CalculatePathTableLen();

		int GetFileCountTotal();
		int GetDirCountTotal();

		void PrintEntries();

	};

	void WriteLicenseData(cd::IsoWriter* writer, void* data);

	void WriteDescriptor(cd::IsoWriter* writer, IDENTIFIERS id, DirTreeClass* dirTree, int imageLen);

};

#endif
