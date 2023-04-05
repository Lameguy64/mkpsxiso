#ifndef _ISO_H
#define _ISO_H

#include <ctime>
#include <stdlib.h>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "fs.h"
#include "cdwriter.h"
#include "common.h"

namespace iso
{
	typedef struct
	{
		const char*	SystemID;
		const char*	VolumeID;
		const char*	VolumeSet;
		const char*	Publisher;
		const char*	DataPreparer;
		const char*	Application;
		const char* Copyright;
		const char* CreationDate;
		const char* ModificationDate;
	} IDENTIFIERS;

	struct DIRENTRY
	{
		std::string	id;		/// Entry identifier (empty if invisible dummy)
		int64_t length;		/// Length of file in bytes
		int		lba;		/// File LBA (in sectors)

		fs::path srcfile;	/// Filename with path to source file (empty if directory or dummy)
		EntryType	  type;		/// File type (0 - file, 1 - directory)
		unsigned char attribs;	/// XA attributes, 0xFF is not set
		unsigned short perms;	/// XA permissions
		unsigned short GID;		/// Owner group ID
		unsigned short UID;		/// Owner user ID
		std::unique_ptr<class DirTreeClass> subdir;

		cd::ISO_DATESTAMP date;
		std::string trackid; /// only used for DA files

	};

	// EntryList must have stable references!
	using EntryList = std::list<DIRENTRY>;
	
	class PathEntryClass {
	public:
		std::string dir_id;
		unsigned short dir_index = 0;
		unsigned short dir_parent_index = 0;
		int dir_lba = 0;
		
		std::unique_ptr<class PathTableClass> sub;
	};
	
	class PathTableClass {
	public:
		unsigned char* GenTableData(unsigned char* buff, bool msb);
		
		std::vector<PathEntryClass> entries;
	};

	class DirTreeClass
	{
	private:
		// TODO: Once DirTreeClass stores a reference to its own entry, this will be pointless
		// Same for all 'dir' arguments to methods of this class
		std::string name;

		DirTreeClass* parent = nullptr; // Non-owning
		
		/// Internal function for generating and writing directory records
		bool WriteDirEntries(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir) const;

		/// Internal function for recursive path table generation
		std::unique_ptr<PathTableClass> GenPathTableSub(unsigned short& index, unsigned short parentIndex) const;

	public:
        static int GetAudioSize(const fs::path& audioFile);
		EntryList& entries; // List of all entries on the disc
		std::vector<std::reference_wrapper<iso::DIRENTRY>> entriesInDir; // References to entries in this directory

		DirTreeClass(EntryList& entries, DirTreeClass* parent = nullptr);
		~DirTreeClass();

		static DIRENTRY& CreateRootDirectory(EntryList& entries, const cd::ISO_DATESTAMP& volumeDate);

		void PrintRecordPath();

		void OutputHeaderListing(FILE* fp, int level) const;
		
		/** Calculates the length of the directory record to be produced by this class in bytes.
		 *
		 *  Returns: Length of directory record in bytes.
		 */
		int	CalculateDirEntryLen() const;

		/** Calculates the LBA of all file and directory entries in the directory record and returns the next LBA
		 *	address.
		 *
		 *	lba			- LBA address where the first directory record begins.
		 */
		int CalculateTreeLBA(int lba);

		/** Adds a file entry to the directory record.
		 *
		 *	*id			- The name of the file entry. It will be converted to uppercase and adds the file version
		 *				identifier (;1) automatically.
		 *	type		- The type of file to add, EntryFile is for standard files, EntryXA is for XA streams and
		 *				EntryStr is for STR streams. To add directories, use AddDirEntry().
		 *	*srcfile	- Path and filename to the source file.
		 *  attributes  - GMT offset/XA permissions for the file, if applicable.
		 */
		bool AddFileEntry(const char* id, EntryType type, const fs::path& srcfile, const EntryAttributes& attributes, const char *trackid = nullptr);

		/** Adds an invisible dummy file entry to the directory record. Its invisible because its file entry
		 *	is not actually added to the directory record.
		 *
		 *	sectors	- The size of the dummy file in sector units (1 = 2048 bytes, 1024 = 2MB).
		 *  type	- 0 for form1 (data) dummy, 1 for form2 (XA) dummy
		 */
		void AddDummyEntry(int sectors, int type);

		/** Generates a path table of all directories and subdirectories within this class' directory record.
		 *
		 *  root	- Directory entry of this path
		 *	*buff	- Pointer to a 2048 byte buffer to generate the path table to.
		 *	msb		- If true, generates a path table encoded in big-endian format, little-endian otherwise.
		 *
		 *	Returns: Length of path table in bytes.
		 */
		int GeneratePathTable(const DIRENTRY& root, unsigned char* buff, bool msb) const;

		/** Adds a subdirectory to the directory record.
		 *
		 *	*id		- The name of the subdirectory to add. It will be converted to uppercase automatically.
		 *  attributes  - GMT offset/XA permissions for the file, if applicable.
		 *  alreadyExists - set to true if a returned DirTreeClass already existed
		 *
		 *	Returns: Pointer to another DirTreeClass for accessing the directory record of the subdirectory.
		 */
		DirTreeClass* AddSubDirEntry(const char* id, const fs::path& srcDir, const EntryAttributes& attributes, bool& alreadyExists);

		/**	Writes the source files assigned to the directory entries to a CD image. Its recommended to execute
		 *	this first before writing the actual file system.
		 *
		 *	*writer	- Pointer to a cd::IsoWriter class that is ready for writing.
		 */
		bool WriteFiles(cd::IsoWriter* writer) const;

		/**	Writes the file system of the directory records to a CD image. Execute this after the source files
		 *	have been written to the CD image.
		 *
		 *	*writer		   - Pointer to a cd::IsoWriter class that is ready for writing.
		 *	LBA			   - Current directory LBA
		 *  parentLBA	   - Parent directory LBA
		 *  currentDirDate - Timestamp to use for . and .. directories.
		 */
		bool WriteDirectoryRecords(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir);

		void SortDirectoryEntries();

		int CalculatePathTableLen(const DIRENTRY& dirEntry) const;

		int GetFileCountTotal() const;
		int GetDirCountTotal() const;

		void OutputLBAlisting(FILE* fp, int level) const;
	};

	void WriteLicenseData(cd::IsoWriter* writer, void* data);

	void WriteDescriptor(cd::IsoWriter* writer, const IDENTIFIERS& id, const DIRENTRY& root, int imageLen);

	const int DA_FILE_PLACEHOLDER_LBA = 0xDEADBEEF;

};

#endif
