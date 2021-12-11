#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

#else
#include <unistd.h>
#endif


#include <string>
#include <filesystem>
#include <memory>

#include "platform.h"
#include "common.h"
#include "cd.h"
#include "xa.h"
#include "cdreader.h"
#include "xml.h"

#include <time.h>


namespace param {

    std::filesystem::path isoFile;
    std::filesystem::path outPath;
    std::filesystem::path xmlFile;

    int		printOnly=false;

}

template<size_t N>
void PrintId(char (&id)[N])
{
	std::string_view view;

	const std::string_view id_view(id, N);
	const size_t last_char = id_view.find_last_not_of(' ');
	if (last_char != std::string_view::npos)
	{
		view = id_view.substr(0, last_char+1);
	}

	if (!view.empty())
	{
		printf("%.*s", static_cast<int>(view.length()), view.data());
	}
	printf("\n");
}

void PrintDate(const cd::ISO_LONG_DATESTAMP& date) {
    printf("%s\n", LongDateToString(date).c_str());
}

template<size_t N>
static std::string_view CleanDescElement(char (&id)[N])
{
	std::string_view result;

	const std::string_view view(id, N);
	const size_t last_char = view.find_last_not_of(' ');
	if (last_char != std::string_view::npos)
	{
		result = view.substr(0, last_char+1);
	}

    return result;
}

std::string_view CleanIdentifier(std::string_view id)
{
	std::string_view result(id.substr(0, id.find_last_of(';')));
	return result;
}

void prepareRIFFHeader(cd::RIFF_HEADER* header, int dataSize) {
	memcpy(header->chunkID,"RIFF",4);
	header->chunkSize = 36 + dataSize;
	memcpy(header->format, "WAVE", 4);

	memcpy(header->subchunk1ID, "fmt ", 4);
	header->subchunk1Size = 16;
	header->audioFormat = 1;
	header->numChannels = 2;
	header->sampleRate = 44100;
	header->bitsPerSample = 16;
	header->byteRate = (header->sampleRate * header->numChannels * header->bitsPerSample) / 8;
	header->blockAlign = (header->numChannels * header->bitsPerSample) / 8;

	memcpy(header->subchunk2ID, "data", 4);
	header->subchunk2Size = dataSize;
}
std::unique_ptr<cd::ISO_LICENSE> ReadLicense(cd::IsoReader& reader) {
	auto license = std::make_unique<cd::ISO_LICENSE>();

	reader.SeekToSector(0);
	reader.ReadBytesXA(license->data, sizeof(license->data));

	return license;
}

void SaveLicense(const cd::ISO_LICENSE& license) {
    const std::filesystem::path outputPath = param::outPath / "license_data.dat";

	FILE* outFile = OpenFile(outputPath, "wb");

    if (outFile == NULL) {
		printf("ERROR: Cannot create license file %" PRFILESYSTEM_PATH "...", outputPath.lexically_normal().c_str());
        return;
    }

    fwrite(license.data, 1, sizeof(license.data), outFile);
    fclose(outFile);
}

static void WriteOptionalXMLAttribs(tinyxml2::XMLElement* element, const cd::IsoDirEntries::Entry& entry, EntryType type)
{
	element->SetAttribute(xml::attrib::GMT_OFFSET, entry.entry.entryDate.GMToffs);

	// Don't output XA attributes on directories yet
	// TODO: Might need something to allow those to propagate upwards with a file/directory distinction
	if (type != EntryType::EntryDir)
	{
		// xa_attrib only makes sense on XA files
		if (type == EntryType::EntryXA)
		{
			element->SetAttribute(xml::attrib::XA_ATTRIBUTES, (entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8);
		}
		element->SetAttribute(xml::attrib::XA_PERMISSIONS, entry.extData.attributes & cdxa::XA_PERMISSIONS_MASK);

		element->SetAttribute(xml::attrib::XA_GID, entry.extData.ownergroupid);
		element->SetAttribute(xml::attrib::XA_UID, entry.extData.owneruserid);
	}
}

static EntryType GetXAEntryType(unsigned short xa_attr)
{
	// we try to guess the file type. Usually, the xa_attr should tell this, but there are many games
	// that do not follow the standard, and sometime leave some or all the attributes unset.
	if (xa_attr & 0x40)
	{
		// if the cddata flag is set, we assume this is the case
		return EntryType::EntryDA;
	}
	if (xa_attr & 0x80)
	{
		// if the directory flag is set, we assume this is the case
		return EntryType::EntryDir;
	}
	if ( (xa_attr & 0x08) && !(xa_attr & 0x10) )
	{
		// if the mode 2 form 1 flag is set, and form 2 is not, we assume this is a regular file.
		return EntryType::EntryFile;
	}
	if ( (xa_attr & 0x10) && !(xa_attr & 0x08) )
	{
		// if the mode 2 form 2 flag is set, and form 1 is not, we assume this is a pure audio xa file.
		return EntryType::EntryXA;
	}

	// here all flags are set to the same value. From what I could see until now, when both flags are the same,
	// this is interpreted in the following two ways, which both lead us to choose str/xa type.
	// 1. Both values are 1, which means there is an indication by the mode 2 form 2 flag that the data is not
	//    regular mode 2 form 1 data (i.e., it is either mixed or just xa). 
	// 2. Both values are 0. The fact that the mode 2 form 2 flag is 0 simply means that the data might not
	//    be *pure* mode 2 form 2 data (i.e., xa), so, we do not conclude it is regular mode 2 form 1 data.
	//    We thus give priority to the mode 2 form 1 flag, which is also zero,
	//	  and conclude that the data is not regular mode 2 form 1 data, and thus can be either mode 2 form 2 or mixed.

	// Remark: Some games (Legend of Mana), use a very strange STR+XA format that is stored in plain mode 2 form 1.
	// This is properly marked in the xa_attr, and there is nothing wrong in extracting them as data.
	return EntryType::EntryXA;
}

std::unique_ptr<cd::IsoDirEntries> ParseSubdirectory(cd::IsoReader& reader, ListView<cd::IsoDirEntries::Entry> view, int offs, int sectors,
	const std::filesystem::path& path)
{
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));
    dirEntries->ReadDirEntries(&reader, offs, sectors);

    for (auto& e : dirEntries->dirEntryList.GetView())
	{
		auto& entry = e.get();

		entry.virtualPath = path;
        if (entry.entry.flags & 0x2)
		{
            entry.subdir = ParseSubdirectory(reader, dirEntries->dirEntryList.NewView(), entry.entry.entryOffs.lsb, GetSizeInSectors(entry.entry.entrySize.lsb), 
				path / CleanIdentifier(entry.identifier));
        }
    }
	
	return dirEntries;
}

std::unique_ptr<cd::IsoDirEntries> ParseRoot(cd::IsoReader& reader, ListView<cd::IsoDirEntries::Entry> view, int offs)
{
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));
    dirEntries->ReadRootDir(&reader, offs);

	auto& entry = dirEntries->dirEntryList.GetView().front().get();
    entry.subdir = ParseSubdirectory(reader, dirEntries->dirEntryList.NewView(), entry.entry.entryOffs.lsb, GetSizeInSectors(entry.entry.entrySize.lsb), 
		CleanIdentifier(entry.identifier));
	
	return dirEntries;
}

void ExtractFiles(cd::IsoReader& reader, const std::list<cd::IsoDirEntries::Entry>& files, const std::filesystem::path& rootPath)
{
    for (const auto& entry : files)
	{
		const std::filesystem::path outputPath = rootPath / entry.virtualPath / CleanIdentifier(entry.identifier);
        if (entry.subdir == nullptr) // Do not extract directories, they're already prepared
		{
			printf("   Extracting %s...\n%" PRFILESYSTEM_PATH "\n", entry.identifier.c_str(), outputPath.lexically_normal().c_str());

			const EntryType type = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8);
			if (type == EntryType::EntryXA)
			{
				// Extract XA or STR file.
				// For both XA and STR files, we need to extract the data 2336 bytes per sector.
				// When rebuilding the bin using mkpsxiso, either we mark the file with str or with xa
				// the source file will anyway be stored on our hard drive in raw form.

				// this is the data to be read 2336 bytes per sector, both if the file is an STR or XA,
				// because the STR contains audio.
				size_t sectorsToRead = GetSizeInSectors(entry.entry.entrySize.lsb);

				int bytesLeft = 2336*sectorsToRead;

				reader.SeekToSector(entry.entry.entryOffs.lsb);

				FILE* outFile = OpenFile(outputPath, "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %" PRFILESYSTEM_PATH "...", outputPath.lexically_normal().c_str());
					return;
				}

				// Copy loop
				while(bytesLeft > 0) {

					u_char copyBuff[2336];

					int bytesToRead = bytesLeft;

					if (bytesToRead > 2336)
						bytesToRead = 2336;

					reader.ReadBytesXA(copyBuff, 2336);

					fwrite(copyBuff, 1, 2336, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);

			}
			else if (type == EntryType::EntryDA)
			{
				// Extract CDDA file
				int result = reader.SeekToSector(entry.entry.entryOffs.lsb);

				if (result) {
					printf("WARNING: The CDDA file %" PRFILESYSTEM_PATH " is out of the iso file bounds.\n", outputPath.lexically_normal().c_str());
					printf("This usually means that the game has audio tracks, and they are on separate files.\n");
					printf("As DUMPSXISO does not support dumping from a cue file, you should use an iso file containing all tracks.\n\n");
					printf("DUMPSXISO will write the file as a dummy (silent) cdda file.\n");
					printf("This is generally fine, when the real CDDA file is also a dummy file.\n");
					printf("If it is not dummy, you WILL lose this audio data in the rebuilt iso.\n");
				}

				FILE* outFile = OpenFile(outputPath, "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %" PRFILESYSTEM_PATH "...", outputPath.lexically_normal().c_str());
					return;
				}

				size_t sectorsToRead = GetSizeInSectors(entry.entry.entrySize.lsb);

				size_t cddaSize = 2352 * sectorsToRead;
				size_t bytesLeft = cddaSize;

				cd::RIFF_HEADER riffHeader;

				prepareRIFFHeader(&riffHeader, cddaSize);
				fwrite((void*)&riffHeader, sizeof(riffHeader), 1, outFile);


				while (bytesLeft > 0) {

					u_char copyBuff[2352] {};

					size_t bytesToRead = bytesLeft;

					if (bytesToRead > 2352)
						bytesToRead = 2352;

					if (!result)
						reader.ReadBytesDA(copyBuff, bytesToRead);
					
					fwrite(copyBuff, 1, bytesToRead, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);

			}
			else if (type == EntryType::EntryFile)
			{
				// Extract regular file

				reader.SeekToSector(entry.entry.entryOffs.lsb);

				FILE* outFile = OpenFile(outputPath, "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %" PRFILESYSTEM_PATH "...", outputPath.lexically_normal().c_str());
					return;
				}

				size_t bytesLeft = entry.entry.entrySize.lsb;
				while(bytesLeft > 0) {

					u_char copyBuff[2048];
					size_t bytesToRead = bytesLeft;

					if (bytesToRead > 2048)
						bytesToRead = 2048;

					reader.ReadBytes(copyBuff, bytesToRead);
					fwrite(copyBuff, 1, bytesToRead, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);

			}
			else
			{
				printf("ERROR: File %s is of invalid type", entry.identifier.c_str());
				continue;
			}
        }	
    }

	// Update timestamps AFTER all files have been extracted
	// else directories will have their timestamps discarded when files are being unpacked into them!
	for (const auto& entry : files)
	{
		UpdateTimestamps(rootPath / entry.virtualPath / CleanIdentifier(entry.identifier), entry.entry.entryDate);
	}

}

tinyxml2::XMLElement* WriteXMLEntry(const cd::IsoDirEntries::Entry& entry, tinyxml2::XMLElement* dirElement, std::filesystem::path* currentVirtualPath,
	const std::filesystem::path& sourcePath)
{
	tinyxml2::XMLElement* newelement;

	const std::filesystem::path outputPath = sourcePath / entry.virtualPath / CleanIdentifier(entry.identifier);
	const EntryType entryType = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8);
	if (entryType == EntryType::EntryDir)
	{
		if (!entry.identifier.empty())
		{
			newelement = dirElement->InsertNewChildElement("dir");
			newelement->SetAttribute(xml::attrib::ENTRY_NAME, entry.identifier.c_str());
			newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.generic_u8string().c_str());
		}
		else
		{
			// Root directory
			newelement = dirElement->InsertNewChildElement(xml::elem::DIRECTORY_TREE);
		}

		dirElement = newelement;
		if (currentVirtualPath != nullptr)
		{
			*currentVirtualPath /= entry.identifier;
		}
    }
	else
	{
        newelement = dirElement->InsertNewChildElement("file");
        newelement->SetAttribute(xml::attrib::ENTRY_NAME, std::string(CleanIdentifier(entry.identifier)).c_str());
        newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.generic_u8string().c_str());		

		if (entryType == EntryType::EntryXA)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "mixed");
		}
		else if (entryType == EntryType::EntryDA)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "da");
		}
		else if (entryType == EntryType::EntryFile)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "data");
		}
	}
	WriteOptionalXMLAttribs(newelement, entry, entryType);
	return dirElement;
}

void WriteXMLGap(unsigned int numSectors, tinyxml2::XMLElement* dirElement)
{
	// TODO: Detect if gap needs checksums and reflect it accordingly here
	tinyxml2::XMLElement* newelement = dirElement->InsertNewChildElement("dummy");
	newelement->SetAttribute(xml::attrib::NUM_DUMMY_SECTORS, numSectors);
}

void WriteXMLByLBA(const std::list<cd::IsoDirEntries::Entry>& files, tinyxml2::XMLElement* dirElement, const std::filesystem::path& sourcePath,
	const unsigned int startLBA, const unsigned int sizeInSectors, const bool onlyDA)
{
	std::filesystem::path currentVirtualPath; // Used to find out whether to traverse 'dir' up or down the chain
	unsigned int expectedLBA = startLBA;

	for (const auto& entry : files)
	{
		const bool isDA = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8) == EntryType::EntryDA;
		if (onlyDA)
		{
			if (!isDA)
			{
				continue;
			}
		}
		else
		{
			// Insert gaps if needed
			// TODO: Tidy it up when audio pregaps are sorted, for now hack it around like this
			if (isDA)
			{
				// Ignore pregap
				expectedLBA += 150;
			}
			if (entry.entry.entryOffs.lsb > expectedLBA)
			{
				WriteXMLGap(entry.entry.entryOffs.lsb - expectedLBA, dirElement);
			}
			expectedLBA = entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb);
		}

		// Work out the relative position between the current directory and the element to create
		const std::filesystem::path relative = entry.virtualPath.lexically_relative(currentVirtualPath);
		for (const std::filesystem::path& part : relative)
		{
			if (part == "..")
			{
				// Go up in XML
				dirElement = dirElement->Parent()->ToElement();
				currentVirtualPath = currentVirtualPath.parent_path();
				continue;
			}
			if (part == ".")
			{
				// Do nothing
				continue;
			}

			// "Enter" the directory
			dirElement = dirElement->InsertNewChildElement("dir");
			dirElement->SetAttribute(xml::attrib::ENTRY_NAME, part.generic_u8string().c_str());

			currentVirtualPath /= part;
		}

		dirElement = WriteXMLEntry(entry, dirElement, &currentVirtualPath, sourcePath);
	}

	// If there is a gap at the end, add it too
	if (!onlyDA)
	{
		if (sizeInSectors > expectedLBA)
		{
			WriteXMLGap(sizeInSectors - expectedLBA, dirElement);
		}
	}
}

// Writes all entries BUT DA! Those must not be "pretty printed"
void WriteXMLByDirectories(const cd::IsoDirEntries* directory, tinyxml2::XMLElement* dirElement, const std::filesystem::path& sourcePath)
{
	for (const auto& e : directory->dirEntryList.GetView())
	{
		const auto& entry = e.get();
		if (GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8) == EntryType::EntryDA) continue;
		
		tinyxml2::XMLElement* child = WriteXMLEntry(entry, dirElement, nullptr, sourcePath);
		// Recursively write children if there are any
		if (const cd::IsoDirEntries* subdir = entry.subdir.get(); subdir != nullptr)
		{
			WriteXMLByDirectories(subdir, child, sourcePath);
		}
	}
}

void ParseISO(cd::IsoReader& reader) {

    cd::ISO_DESCRIPTOR descriptor;
	auto license = ReadLicense(reader);

    reader.SeekToSector(16);
    reader.ReadBytes(&descriptor, 2048);


    printf("ISO descriptor:\n\n");

    printf("   System ID      : ");
    PrintId(descriptor.systemID);
    printf("   Volume ID      : ");
    PrintId(descriptor.volumeID);
    printf("   Volume Set ID  : ");
    PrintId(descriptor.volumeSetIdentifier);
    printf("   Publisher ID   : ");
    PrintId(descriptor.publisherIdentifier);
    printf("   Data Prep. ID  : ");
    PrintId(descriptor.dataPreparerIdentifier);
    printf("   Application ID : ");
    PrintId(descriptor.applicationIdentifier);
    printf("\n");

    printf("   Volume Create Date : ");
    PrintDate(descriptor.volumeCreateDate);
    printf("   Volume Modify Date : ");
    PrintDate(descriptor.volumeModifyDate);
    printf("   Volume Expire Date : ");
    PrintDate(descriptor.volumeExpiryDate);
    printf("\n");

    cd::IsoPathTable pathTable;

    size_t numEntries = pathTable.ReadPathTable(&reader, descriptor.pathTable1Offs);

    if (numEntries == 0) {
        printf("   No files to find.\n");
        return;
    }

	// Prepare output directories
	for(size_t i=0; i<numEntries; i++)
	{
		const std::filesystem::path dirPath = param::outPath / pathTable.GetFullDirPath(i);

		std::error_code ec;
		std::filesystem::create_directories(dirPath, ec);
	}

    printf("ISO contents:\n\n");


	std::list<cd::IsoDirEntries::Entry> entries;
	std::unique_ptr<cd::IsoDirEntries> rootDir = ParseRoot(reader,
					ListView(entries),
					descriptor.rootDirRecord.entryOffs.lsb);

	// Sort files by LBA for "strict" output
	entries.sort([](const auto& left, const auto& right)
		{
			return left.entry.entryOffs.lsb < right.entry.entryOffs.lsb;
		});

	ExtractFiles(reader, entries, param::outPath);
    SaveLicense(*license);

	if (!param::xmlFile.empty())
	{
		if (FILE* file = OpenFile(param::xmlFile, "w"); file != nullptr)
		{
			tinyxml2::XMLDocument xmldoc;    

			tinyxml2::XMLElement *baseElement = static_cast<tinyxml2::XMLElement*>(xmldoc.InsertFirstChild(xmldoc.NewElement(xml::elem::ISO_PROJECT)));
			baseElement->SetAttribute(xml::attrib::IMAGE_NAME, "mkpsxiso.bin");
			baseElement->SetAttribute(xml::attrib::CUE_SHEET, "mkpsxiso.cue");

			tinyxml2::XMLElement *trackElement = baseElement->InsertNewChildElement(xml::elem::TRACK);
			trackElement->SetAttribute(xml::attrib::TRACK_TYPE, "data");

			{
				tinyxml2::XMLElement *newElement = trackElement->InsertNewChildElement(xml::elem::IDENTIFIERS);
				auto setAttributeIfNotEmpty = [newElement](const char* name, std::string_view value)
				{
					if (!value.empty())
					{
						newElement->SetAttribute(name, std::string(value).c_str());
					}
				};

				setAttributeIfNotEmpty(xml::attrib::SYSTEM_ID, CleanDescElement(descriptor.systemID));
				setAttributeIfNotEmpty(xml::attrib::APPLICATION, CleanDescElement(descriptor.applicationIdentifier));
				setAttributeIfNotEmpty(xml::attrib::VOLUME_ID, CleanDescElement(descriptor.volumeID));
				setAttributeIfNotEmpty(xml::attrib::VOLUME_SET, CleanDescElement(descriptor.volumeSetIdentifier));
				setAttributeIfNotEmpty(xml::attrib::PUBLISHER, CleanDescElement(descriptor.publisherIdentifier));
				setAttributeIfNotEmpty(xml::attrib::DATA_PREPARER, CleanDescElement(descriptor.dataPreparerIdentifier));
				newElement->SetAttribute(xml::attrib::CREATION_DATE, LongDateToString(descriptor.volumeCreateDate).c_str());
			}

			{
				tinyxml2::XMLElement *newElement = trackElement->InsertNewChildElement(xml::elem::LICENSE);
				newElement->SetAttribute(xml::attrib::LICENSE_FILE,
					(param::outPath / "license_data.dat").lexically_proximate(param::xmlFile.parent_path()).generic_u8string().c_str());
			}

			const std::filesystem::path sourcePath = param::outPath.lexically_proximate(param::xmlFile.parent_path());

			// TODO: Commandline switch
			bool preserveLBA = true;
			if (preserveLBA)
			{
				WriteXMLByLBA(entries, trackElement, sourcePath, descriptor.rootDirRecord.entryOffs.lsb, descriptor.volumeSize.lsb, false);
			}
			else
			{
				// All this to get contents of a "root dir"...
				WriteXMLByDirectories(rootDir->dirEntryList.GetView().front().get().subdir.get(), trackElement, sourcePath);

				// Write DAs by LBA
				WriteXMLByLBA(entries, trackElement, sourcePath, descriptor.rootDirRecord.entryOffs.lsb, descriptor.volumeSize.lsb, true);
			}

			xmldoc.SaveFile(file);
			fclose(file);
		}
	}
}

int Main(int argc, char *argv[])
{
    printf("DUMPSXISO " VERSION " - PlayStation ISO dumping tool\n");
    printf("2017 Meido-Tek Productions (Lameguy64)\n");
    printf("2020 Phoenix (SadNES cITy)\n");
    printf("2021 Silent and Chromaryu\n\n");

	if (argc == 1) {

		printf("Usage:\n\n");
		printf("   dumpsxiso <isofile> [-x <path>]\n\n");
		printf("   <isofile>   - File name of ISO file (supports any 2352 byte/sector images).\n");
		printf("   [-x <path>] - Specified destination directory of extracted files.\n");
		printf("   [-s <path>] - Outputs an MKPSXISO compatible XML script for later rebuilding.\n");

		return EXIT_SUCCESS;
	}


    for(int i=1; i<argc; i++) {

		// Is it a switch?
        if (argv[i][0] == '-') {

			// Directory path
            if (strcmp("x", &argv[i][1]) == 0) {

                param::outPath = std::filesystem::u8path(argv[i+1]).lexically_normal();

                i++;

            } else if (strcmp("s", &argv[i][1]) == 0) {

                param::xmlFile = std::filesystem::u8path(argv[i+1]);
                i++;

            } else if (strcmp("p", &argv[i][1]) == 0) {

            	param::printOnly = true;

            } else {

            	printf("Unknown parameter: %s\n", argv[i]);
            	return EXIT_FAILURE;
            }

        } else {

			if (param::isoFile.empty()) {

				param::isoFile = std::filesystem::u8path(argv[i]);

			} else {

				printf("Only one iso file is supported.\n");
				return EXIT_FAILURE;
			}

        }

    }

	if (param::isoFile.empty()) {

		printf("No iso file specified.\n");
		return EXIT_FAILURE;
	}


	cd::IsoReader reader;

	if (!reader.Open(param::isoFile)) {

		printf("ERROR: Cannot open file %" PRFILESYSTEM_PATH "...\n", param::isoFile.lexically_normal().c_str());
		return EXIT_FAILURE;

	}

	if (!param::outPath.empty())
	{
		printf("Output directory : %" PRFILESYSTEM_PATH "\n", param::outPath.lexically_normal().c_str());
	}

    ParseISO(reader);
	return EXIT_SUCCESS;
}
