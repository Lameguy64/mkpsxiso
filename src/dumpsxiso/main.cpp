#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

#else
#include <unistd.h>
#endif


#include <string>

#include "cd.h"
#include "xa.h"
#include "cdreader.h"
#include "xml.h"

#include <time.h>

#if defined(_WIN32)
    #include <direct.h>
#else
	#include <sys/stat.h>
	int _mkdir(const char* dirname){ return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); }
#endif

namespace param {

    char*	isoFile=NULL;
    std::string	outPath;
    std::string xmlFile;

    int		printOnly=false;

}

namespace global {

    std::string isoPath;

}

void PrintId(char* text) {

    int i=0;

    while(text[i] != 32) {
        printf("%c", text[i]);
        i++;
    }
    printf("\n");

}

void PrintDate(const cd::ISO_LONG_DATESTAMP& date) {
    printf("%s\n", LongDateToString(date).c_str());
}

void BackDir(std::string& path) {

    path.resize(path.rfind("/"));

}

const char* CleanVolumeId(const char* id) {

    static char buff[38];
    int i;

    for(i=0; (i<37)&&(id[i]!=0x20); i++) {
        buff[i] = id[i];
    }
    buff[i] = 0x00;

    return buff;

}

const char* CleanIdentifier(const char* id) {

    static char buff[16];
    int i;

    for(i=0; (id[i]!=0x00)&&(id[i]!=';'); i++) {
        buff[i] = id[i];
    }
    buff[i] = 0x00;

    return buff;

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
void ReadLicense(cd::IsoReader& reader, cd::ISO_LICENSE* license) {
	reader.SeekToSector(0);
	reader.ReadBytesXA(license->data, 28032);
}

#if defined(_WIN32)
static FILETIME TimetToFileTime(time_t t)
{
	FILETIME ft;
    LARGE_INTEGER ll;
	ll.QuadPart = t * 10000000ll + 116444736000000000ll;
    ft.dwLowDateTime = ll.LowPart;
    ft.dwHighDateTime = ll.HighPart;
	return ft;
}

// TODO: Move to a header shared with mkpsxiso
time_t timegm(struct tm* tm)
{
	return _mkgmtime(tm);
}
#endif

void UpdateTimestamps(const std::string& path, const cd::ISO_DATESTAMP* entryDate)
{
// utime can't update timestamps of directories, so a platform-specific approach is needed
#if defined(_WIN32)
	HANDLE file = CreateFileA(path.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (file != INVALID_HANDLE_VALUE)
	{
		tm timeBuf {};
		timeBuf.tm_year = entryDate->year;
		timeBuf.tm_mon = entryDate->month - 1;
		timeBuf.tm_mday = entryDate->day;
		timeBuf.tm_hour = entryDate->hour;
		timeBuf.tm_min = entryDate->minute - (15 * entryDate->GMToffs);
		timeBuf.tm_sec = entryDate->second;

		const FILETIME ft = TimetToFileTime(timegm(&timeBuf));
		SetFileTime(file, &ft, nullptr, &ft);

		CloseHandle(file);
	}
#else
	// TODO: Do
#endif
}

void SaveLicense(cd::ISO_LICENSE& license) {
    std::string outputPath = param::outPath;

    outputPath = outputPath + "license_data.dat";
    FILE* outFile = fopen(outputPath.c_str(), "wb");

    if (outFile == NULL) {
        printf("ERROR: Cannot create license file %s...", outputPath.c_str());
        return;
    }

    fwrite(license.data, 1, 28032, outFile);
    fclose(outFile);
}

void ParseDirectories(cd::IsoReader& reader, int offs, tinyxml2::XMLDocument* doc, tinyxml2::XMLElement* element, int sectors=1) {

    cd::IsoDirEntries dirEntries;
    tinyxml2::XMLElement* newelement = NULL;
    FILE *outFile;

    int entriesFound = dirEntries.ReadDirEntries(&reader, offs, sectors);
    dirEntries.SortByLBA();

    for(int e=2; e<entriesFound; e++) {

		std::string outputPath = global::isoPath;

		if (!outputPath.empty()) {
			outputPath.erase(0, 1);
			outputPath += "/";
		}

		outputPath = param::outPath + outputPath + CleanIdentifier(dirEntries.dirEntryList[e].identifier);

        if (dirEntries.dirEntryList[e].flags & 0x2) {

            global::isoPath += "/";
            global::isoPath += dirEntries.dirEntryList[e].identifier;

            if (element != NULL) {

                newelement = doc->NewElement("dir");
                newelement->SetAttribute(xml::attrib::ENTRY_NAME, dirEntries.dirEntryList[e].identifier);
                element->InsertEndChild(newelement);

            }

            ParseDirectories(reader, dirEntries.dirEntryList[e].entryOffs.lsb, doc, newelement, dirEntries.dirEntryList[e].entrySize.lsb/2048);

            BackDir(global::isoPath);

        } else {

			printf("   Extracting %s...\n", dirEntries.dirEntryList[e].identifier);

			printf("%s\n",outputPath.c_str());

            if (element != NULL) {

                newelement = doc->NewElement("file");
                newelement->SetAttribute(xml::attrib::ENTRY_NAME, CleanIdentifier(dirEntries.dirEntryList[e].identifier));
                newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.c_str());

            }

			unsigned short xa_attr = ((cdxa::ISO_XA_ATTRIB*)dirEntries.dirEntryList[e].extData)->attributes;

			char type = -1;

			// we try to guess the file type. Usually, the xa_attr should tell this, but there are many games
			// that do not follow the standard, and sometime leave some or all the attributes unset.

			if ((xa_attr & 0xff) & 0x40) {
				// if the cddata flag is set, we assume this is the case
				type = 1;
			} else if ( ((xa_attr & 0xff) & 0x08) && !((xa_attr & 0xff) & 0x10) ){
				// if the mode 2 form 1 flag is set, and form 2 is not, we assume this is a regular file.
				type = 0;
			} else if ( ((xa_attr & 0xff) & 0x10) && !((xa_attr & 0xff) & 0x08) ) {
				// if the mode 2 form 2 flag is set, and form 1 is not, we assume this is a pure audio xa file.
				type = 2;
			}  else {
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
				type = 2;
			}

			if (type == 2) {

				// Extract XA or STR file.
				// For both XA and STR files, we need to extract the data 2336 bytes per sector.
				// When rebuilding the bin using mkpsxiso, either we mark the file with str or with xa
				// the source file will anyway be stored on our hard drive in raw form.
				// Here we mark the file as str, so that each sector will have the correct error codes regenerated by mkpsxiso,
				// depending on the value of the sub-header (video or audio data).

				if(element != NULL)
					newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "mixed");

				// this is the data to be read 2336 bytes per sector, both if the file is an STR or XA,
				// because the STR contains audio.
				size_t sectorsToRead = (dirEntries.dirEntryList[e].entrySize.lsb+2047)/2048;

				int bytesLeft = 2336*sectorsToRead;

				reader.SeekToSector(dirEntries.dirEntryList[e].entryOffs.lsb);

				outFile = fopen(outputPath.c_str(), "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %s...", outputPath.c_str());
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
			else if (type == 1) {

				// Extract CDDA file

				if (element != NULL)
					newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "da");

				int result = reader.SeekToSector(dirEntries.dirEntryList[e].entryOffs.lsb);

				if (result) {
					printf("WARNING: The CDDA file %s is out of the iso file bounds.\n", outputPath.c_str());
					printf("This usually means that the game has audio tracks, and they are on separate files.\n");
					printf("As DUMPSXISO does not support dumping from a cue file, you should use an iso file containing all tracks.\n\n");
					printf("DUMPSXISO will write the file as a dummy (silent) cdda file.\n");
					printf("This is generally fine, when the real CDDA file is also a dummy file.\n");
					printf("If it is not dummy, you WILL lose this audio data in the rebuilt iso.\n");
				}

				outFile = fopen(outputPath.c_str(), "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %s...", outputPath.c_str());
					return;
				}

				size_t sectorsToRead = (dirEntries.dirEntryList[e].entrySize.lsb + 2047) / 2048;

				int cddaSize = 2352 * sectorsToRead;
				int bytesLeft = cddaSize;

				cd::RIFF_HEADER riffHeader;

				prepareRIFFHeader(&riffHeader, cddaSize);
				fwrite((void*)&riffHeader, 1, sizeof(cd::RIFF_HEADER), outFile);


				while (bytesLeft > 0) {

					u_char copyBuff[2352];
					memset(copyBuff, 0, 2352);

					int bytesToRead = bytesLeft;

					if (bytesToRead > 2352)
						bytesToRead = 2352;

					if (!result)
						reader.ReadBytesDA(copyBuff, bytesToRead);
					
					fwrite(copyBuff, 1, bytesToRead, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);

			} else {

				// Extract regular file

				if (element != NULL)
                    newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "data");

				reader.SeekToSector(dirEntries.dirEntryList[e].entryOffs.lsb);

				outFile = fopen(outputPath.c_str(), "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %s...", outputPath.c_str());
					return;
				}

				int bytesLeft = dirEntries.dirEntryList[e].entrySize.lsb;
				while(bytesLeft > 0) {

					u_char copyBuff[2048];
					int bytesToRead = bytesLeft;

					if (bytesToRead > 2048)
						bytesToRead = 2048;

					reader.ReadBytes(copyBuff, bytesToRead);
					fwrite(copyBuff, 1, bytesToRead, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);

			}

			if (element != NULL)
				element->InsertEndChild(newelement);

        }

		UpdateTimestamps(outputPath, &dirEntries.dirEntryList[e].entryDate);
    }
}

void ParseISO(cd::IsoReader& reader) {

    cd::ISO_DESCRIPTOR	descriptor;
	cd::ISO_LICENSE license;

	ReadLicense(reader, &license);

    reader.SeekToSector(16);
    reader.ReadBytes(&descriptor, 2048);


    printf("ISO decriptor:\n\n");

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

    int numEntries = pathTable.ReadPathTable(&reader, descriptor.pathTable1Offs);

    if (numEntries == 0) {
        printf("   No files to find.\n");
        return;
    }

    if (!param::outPath.empty()) {

        if (param::outPath.rfind("/") != param::outPath.length()-1)
            param::outPath += "/";

    }

	// Prepare output directories
	for(int i=1; i<numEntries; i++) {

		char pathBuff[256];
		pathTable.GetFullDirPath(i, pathBuff, 256);

		std::string dirPath = param::outPath;

		dirPath += pathBuff;
		_mkdir(dirPath.c_str());

	}

    printf("ISO contents:\n\n");

    tinyxml2::XMLDocument xmldoc;

    if (!param::xmlFile.empty()) {

		tinyxml2::XMLElement *baseElement = xmldoc.NewElement(xml::elem::ISO_PROJECT);
		baseElement->SetAttribute(xml::attrib::IMAGE_NAME, "mkpsxiso.bin");
		baseElement->SetAttribute(xml::attrib::CUE_SHEET, "mkpsxiso.cue");

		tinyxml2::XMLElement *trackElement = xmldoc.NewElement(xml::elem::TRACK);
		trackElement->SetAttribute(xml::attrib::TRACK_TYPE, "data");

		tinyxml2::XMLElement *newElement = xmldoc.NewElement(xml::elem::IDENTIFIERS);

		if (descriptor.systemID[0] != 0x20)
			newElement->SetAttribute(xml::attrib::SYSTEM_ID, CleanVolumeId(descriptor.systemID));
		if (descriptor.applicationIdentifier[0] != 0x20)
			newElement->SetAttribute(xml::attrib::APPLICATION, CleanVolumeId(descriptor.applicationIdentifier));
		if (descriptor.volumeID[0] != 0x20)
			newElement->SetAttribute(xml::attrib::VOLUME_ID, CleanVolumeId(descriptor.volumeID));
		if (descriptor.volumeSetIdentifier[0] != 0x20)
			newElement->SetAttribute(xml::attrib::VOLUME_SET, CleanVolumeId(descriptor.volumeSetIdentifier));
		if (descriptor.publisherIdentifier[0] != 0x20)
			newElement->SetAttribute(xml::attrib::PUBLISHER, CleanVolumeId(descriptor.publisherIdentifier));
		if (descriptor.dataPreparerIdentifier[0] != 0x20)
			newElement->SetAttribute(xml::attrib::DATA_PREPARER, CleanVolumeId(descriptor.dataPreparerIdentifier));

		newElement->SetAttribute(xml::attrib::CREATION_DATE, LongDateToString(descriptor.volumeCreateDate).c_str());

		trackElement->InsertEndChild(newElement);

		newElement = xmldoc.NewElement(xml::elem::LICENSE);
		newElement->SetAttribute(xml::attrib::LICENSE_FILE, (param::outPath+"license_data.dat").c_str());

		trackElement->InsertEndChild(newElement);

		newElement = xmldoc.NewElement(xml::elem::DIRECTORY_TREE);

		ParseDirectories(reader,
						descriptor.rootDirRecord.entryOffs.lsb,
						&xmldoc,
						newElement,
						descriptor.rootDirRecord.entrySize.lsb/2048);

		trackElement->InsertEndChild(newElement);
		baseElement->InsertEndChild(trackElement);
		xmldoc.InsertEndChild(baseElement);
		xmldoc.SaveFile(param::xmlFile.c_str());

    } else {

    	ParseDirectories(reader,
						descriptor.rootDirRecord.entryOffs.lsb,
						&xmldoc,
						NULL,
						descriptor.rootDirRecord.entrySize.lsb/2048);

    }

    //Save license file anyway.
    SaveLicense(license);
}

int main(int argc, char *argv[]) {


    printf("DUMPSXISO " VERSION " - PlayStation ISO dumping tool\n");
    printf("2017 Meido-Tek Productions (Lameguy64), 2020 Phoenix (SadNES cITy).\n\n");

	if (argc == 1) {

		printf("Usage:\n\n");
		printf("   dumpsxiso <isofile> [-x <path>]\n\n");
		printf("   <isofile>   - File name of ISO file (supports any 2352 byte/sector images).\n");
		printf("   [-x <path>] - Specified destination directory of extracted files.\n");
		printf("   [-s <path>] - Outputs an MKPSXISO compatible XML script for later rebuilding.\n");

		return(EXIT_SUCCESS);

	}


    for(int i=1; i<argc; i++) {

		// Is it a switch?
        if (argv[i][0] == '-') {

			// Directory path
            if (strcmp("x", &argv[i][1]) == 0) {

                param::outPath = argv[i+1];

                i++;

            } else if (strcmp("s", &argv[i][1]) == 0) {

                param::xmlFile = argv[i+1];
                i++;

            } else if (strcmp("p", &argv[i][1]) == 0) {

            	param::printOnly = true;

            } else {

            	printf("Unknown parameter: %s\n", argv[i]);
            	return(EXIT_FAILURE);

            }

        } else {

			if (param::isoFile == NULL) {

				param::isoFile = argv[i];

			} else {

				printf("Only one iso file is supported.\n");
				return(EXIT_FAILURE);

			}

        }

    }

	if (param::isoFile == NULL) {

		printf("No iso file specified.\n");
		return(EXIT_FAILURE);

	}


	cd::IsoReader reader;

	if (!reader.Open(param::isoFile)) {

		printf("ERROR: Cannot open file %s...\n", param::isoFile);
		return(EXIT_FAILURE);

	}

	if (!param::outPath.empty())
		printf("Output directory : %s\n", param::outPath.c_str());

    ParseISO(reader);

    reader.Close();

    exit(EXIT_SUCCESS);

}
