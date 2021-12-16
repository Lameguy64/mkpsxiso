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
#include <filesystem>
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

		std::filesystem::path srcfile;	/// Filename with path to source file (empty if directory or dummy)
		EntryType	  type;		/// File type (0 - file, 1 - directory)
		unsigned char attribs;	/// XA attributes, 0xFF is not set
		unsigned short perms;	/// XA permissions
		unsigned short GID;		/// Owner group ID
		unsigned short UID;		/// Owner user ID
		std::unique_ptr<class DirTreeClass> subdir;

		cd::ISO_DATESTAMP date;

	};

	// EntryList must have stable references!
	using EntryList = std::list<DIRENTRY>;


	// Inheritable attributes of files and/or directories
	// They are applied in the following order:
	// 1. mkpsxiso defaults
	// 2. directory_tree attributes
	// 3. dir attributes
	// 4. file attributes
	class EntryAttributes
	{
	private:
		static constexpr signed char DEFAULT_GMFOFFS = 0;
		static constexpr unsigned char DEFAULT_XAATRIB = 0xFF;
		static constexpr unsigned short DEFAULT_XAPERM = 0x555; // rx
		static constexpr unsigned short	DEFAULT_OWNER_ID = 0;

	public:
		std::optional<signed char> GMTOffs;
		std::optional<unsigned char> XAAttrib;
		std::optional<unsigned short> XAPerm;
		std::optional<unsigned short> GID;
		std::optional<unsigned short> UID;

	public:
		// Default attributes, specified above
		static EntryAttributes MakeDefault();

		// "Overlay" the derived attributes (if they exist) on the base ones
		static EntryAttributes Overlay(EntryAttributes base, const EntryAttributes& derived);
	};
	
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
		int	WriteDirEntries(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir) const;

		/// Internal function for recursive path table generation
		std::unique_ptr<PathTableClass> GenPathTableSub(unsigned short& index, unsigned short parentIndex) const;

		int GetWavSize(const std::filesystem::path& wavFile);
		int PackWaveFile(cd::IsoWriter* writer, const std::filesystem::path& wavFile);
		
	public:

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
		 * 
		 * * *passedSector - Flag to indicate if the directory record has exceeded a sector
		 */
		int	CalculateDirEntryLen(bool* passedSector = nullptr) const;

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
		bool AddFileEntry(const char* id, EntryType type, const std::filesystem::path& srcfile, const EntryAttributes& attributes);

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
		DirTreeClass* AddSubDirEntry(const char* id, const std::filesystem::path& srcDir, const EntryAttributes& attributes, bool& alreadyExists);

		/**	Writes the source files assigned to the directory entries to a CD image. Its recommended to execute
		 *	this first before writing the actual file system.
		 *
		 *	*writer	- Pointer to a cd::IsoWriter class that is ready for writing.
		 */
		int	WriteFiles(cd::IsoWriter* writer);

		/**	Writes the file system of the directory records to a CD image. Execute this after the source files
		 *	have been written to the CD image.
		 *
		 *	*writer		   - Pointer to a cd::IsoWriter class that is ready for writing.
		 *	LBA			   - Current directory LBA
		 *  parentLBA	   - Parent directory LBA
		 *  currentDirDate - Timestamp to use for . and .. directories.
		 */
		int	WriteDirectoryRecords(cd::IsoWriter* writer, const DIRENTRY& dir, const DIRENTRY& parentDir);

		void SortDirectoryEntries();

		int CalculatePathTableLen(const DIRENTRY& dirEntry) const;

		int GetFileCountTotal() const;
		int GetDirCountTotal() const;

		void OutputLBAlisting(FILE* fp, int level) const;
		int WriteCueEntries(FILE* fp, int* trackNum) const;
	};

	void WriteLicenseData(cd::IsoWriter* writer, void* data);

	void WriteDescriptor(cd::IsoWriter* writer, const IDENTIFIERS& id, const DIRENTRY& root, int imageLen);

};

#endif
