#include <stdio.h>
#include <tinyxml2.h>
#include <unistd.h>
#include "cdwriter.h"	// CD image reader/writer module
#include "iso.h"		// ISO file system generator module

#define VERSION "1.10"


namespace global {

	time_t		BuildTime;
	int			QuietMode	= false;
	int			Overwrite	= false;

	int			NoLimit		= false;

	const char*	XMLscript		= NULL;
	const char*	LBAfile			= NULL;
	const char*	LBAheaderFile	= NULL;
	const char*	ImageName		= NULL;

	int			OutputOverride = false;
	int			NoIsoGen = false;

};


int ParseDirectory(iso::DirTreeClass* dirTree, tinyxml2::XMLElement* dirElement);
int ParseISOfileSystem(cd::IsoWriter* writer, tinyxml2::XMLElement* trackElement);

int PackWaveFile(cd::IsoWriter* writer, const char* wavFile);
int GetSize(const char* fileName);


int main(int argc, const char* argv[]) {

	// Parse arguments
	for(int i=1; i<argc; i++) {

		if (argv[i][0] == '-') {

			if (strcasecmp("-lbahead", argv[i]) == 0) {

				i++;
				global::LBAheaderFile = argv[i];

			} else if (strcasecmp("-nolimit", argv[i]) == 0) {

				global::NoLimit = true;

			} else if (strcasecmp("-noisogen", argv[i]) == 0) {

				global::NoIsoGen = true;

			} else if (strcasecmp("-q", argv[i]) == 0) {

				global::QuietMode = true;

			} else if (strcasecmp("-lba", argv[i]) == 0) {

				i++;
				global::LBAfile = argv[i];

			} else if (strcasecmp("-o", argv[i]) == 0) {

				i++;
				global::ImageName = argv[i];
				global::OutputOverride = true;

			} else if (strcasecmp("-y", argv[i]) == 0) {

				global::Overwrite = true;

			} else {

				printf("Unknown parameter: %s\n", argv[i]);

				return EXIT_FAILURE;

			}

		} else {

			if (global::XMLscript == NULL)
				global::XMLscript = argv[i];

		}

	}

	if ((!global::QuietMode) || (argc == 1)) {

		printf("MKPSXISO " VERSION " - PlayStation ISO Image Maker\n");
		printf("2017 Meido-Tek Productions (Lameguy64)\n\n");

	}

	if (argc == 1) {

		printf("mkpsxiso [-y] [-q] [-o <file>] [-lba <file>] [-lbaheader <file>] [-nolimit]\n");
		printf(" [-noisogen] <xml>\n\n");
		printf("  -y        - Always overwrite.\n");
		printf("  -q        - Quiet mode (prints nothing but warnings and errors).\n");
		printf("  -o        - Specifies output file name (overrides XML but not cue_sheet).\n");
		printf("  <xml>     - File name of an ISO image project in XML format to generate.\n\n");
		printf("Special Options:\n\n");
		printf("  -lbalist  - Outputs an LBA listing of all files within the ISO file system.\n");
		printf("  -lbahead  - Outputs an LBA listing of all files in C header format.\n");
		printf("  -nolimit  - Don't warn when a directory record or path table exceeds a sector.\n");
		printf("  -noisogen - Do not generate ISO image but calculate file LBA addresses.\n");
		printf("              (ideal for -lba and -lbaheader without generating an image)\n");

		return EXIT_SUCCESS;

	}

	if (global::XMLscript == NULL) {

		printf("No XML script specified.\n");
		return EXIT_FAILURE;

	}

	// Get current time to be used as date stamps for all directories
	time(&global::BuildTime);

	// Load XML file
	tinyxml2::XMLDocument xmlFile;

    if (xmlFile.LoadFile(global::XMLscript) != tinyxml2::XML_SUCCESS) {

		printf("ERROR: Cannot load XML file.\n\n");
		printf("Make sure the format of the XML document is correct and that the file exists.\n");
		return EXIT_FAILURE;

    }

	// Check if there is an <iso_project> element
    tinyxml2::XMLElement* projectElement = xmlFile.FirstChildElement("iso_project");

    if (projectElement == NULL) {

		printf("ERROR: Cannot find <iso_project> element in XML document.\n");
		return EXIT_FAILURE;

    }


	// Build loop for XML scripts with multiple <iso_project> elements
	while(projectElement != NULL) {

		// Check if image_name attribute is specified
		if (global::ImageName == NULL) {

			if (projectElement->Attribute("image_name") == NULL) {

				printf("ERROR: image_name attribute not specfied in <iso_project> element.\n");
				return EXIT_FAILURE;

			}

			global::ImageName = projectElement->Attribute("image_name");

		}

		if (!global::QuietMode) {

			printf("Building ISO Image: %s", global::ImageName);

			if (projectElement->Attribute("cue_sheet") != NULL)
				printf(" + %s", projectElement->Attribute("cue_sheet"));

			printf("\n");

		}

		if (!global::Overwrite) {

			if (GetSize(global::ImageName) >= 0) {

				printf("WARNING: ISO image already exists, overwrite? <y/n> ");

				char key;

				do {

					key = getchar();

					if (tolower(key) == 'n') {

						return EXIT_FAILURE;

					}

				} while(tolower(key) != 'y');

				printf("\n");

			} else {

				printf("\n");

			}

		}


		// Check if there is a track element specified
		tinyxml2::XMLElement* trackElement = projectElement->FirstChildElement("track");

		if (trackElement == NULL) {

			printf("ERROR: At least one <track> element must be specified.\n");
			return EXIT_FAILURE;

		}


		// Check if cue_sheet attribute is specified
		FILE*	cuefp = NULL;

		if (!global::NoIsoGen) {

			if (projectElement->Attribute("cue_sheet") != NULL) {

				if (strlen(projectElement->Attribute("cue_sheet")) == 0) {

					if (!global::QuietMode)
						printf("  ");

					printf("ERROR: cue_sheet attribute is blank.\n");

					return EXIT_FAILURE;

				}

				cuefp = fopen(projectElement->Attribute("cue_sheet"), "w");

				if (cuefp == NULL) {

					if (!global::QuietMode)
						printf("  ");

					printf("ERROR: Unable to create cue sheet.\n");

					return EXIT_FAILURE;

				}

				fprintf(cuefp, "FILE \"%s\" BINARY\n", global::ImageName);

			}

		}


		// Create ISO image for writing
		cd::IsoWriter writer;

		if (!global::NoIsoGen)
			writer.Create(global::ImageName);


		int trackNum = 1;
		int firstCDDAdone = false;

		// Parse tracks
		while(trackElement != NULL) {

			if (!global::QuietMode)
				printf("  Track #%d %s:\n", trackNum, trackElement->Attribute("type"));

			if (trackElement->Attribute("type") == NULL) {

				if (!global::QuietMode)
					printf("  ");

				printf("ERROR: type attribute not specified in <track> element.\n");

				if (!global::NoIsoGen)
					writer.Close();

				unlink(global::ImageName);

				if (cuefp != NULL)
					fclose(cuefp);

				return EXIT_FAILURE;

			}

			// Generate ISO file system for data track
			if (strcasecmp("data", trackElement->Attribute("type")) == 0) {

				if (trackNum != 1) {

					if (!global::QuietMode)
						printf("  ");

					printf("ERROR: Only the first track can be data.\n");

					if (!global::NoIsoGen)
						writer.Close();

					if (cuefp != NULL)
						fclose(cuefp);

					return EXIT_FAILURE;

				}

				if (!ParseISOfileSystem(&writer, trackElement)) {

					if (!global::NoIsoGen)
						writer.Close();

					unlink(global::ImageName);

					if (cuefp != NULL) {

						fclose(cuefp);
						unlink(projectElement->Attribute("cue_sheet"));

					}

					return EXIT_FAILURE;


				}

				// Write track information to the CUE sheet
				if (cuefp != NULL) {

					fprintf(cuefp, "  TRACK %02d MODE2/2352\n", trackNum);
					fprintf(cuefp, "    INDEX 01 00:00:00\n");

				}

			// Add audio track
			} else if (strcasecmp("audio", trackElement->Attribute("type")) == 0) {

				// Only allow audio tracks if the cue_sheet attribute is specified
				if (cuefp == NULL) {

					if (!global::QuietMode)
						printf("    ");

					printf("ERROR: cue_sheet attribute must be specified when using audio tracks.\n");

					if (!global::NoIsoGen)
						writer.Close();

					return EXIT_FAILURE;

				}

				// Write track information to the CUE sheet

				if (trackElement->Attribute("source") == NULL) {

					if (!global::QuietMode)
						printf("    ");

					printf("ERROR: source attribute not specified for track.\n");

					if (!global::NoIsoGen)
						writer.Close();

					if (cuefp != NULL)
						fclose(cuefp);

					return EXIT_FAILURE;

				} else {

					fprintf(cuefp, "  TRACK %02d AUDIO\n", trackNum);

					if (!global::NoIsoGen) {

						int trackLBA = writer.SeekToEnd();

						// Add PREGAP of 2 seconds on first audio track only
						if (!firstCDDAdone) {

							fprintf(cuefp, "    PREGAP 00:02:00\n");
							firstCDDAdone = true;

						} else {

							fprintf(cuefp, "    INDEX 00 %02d:%02d:%02d\n",
								(trackLBA/75)/60,
								(trackLBA/75)%60,
								trackLBA%75
							);

							char blank[CD_SECTOR_SIZE];
							memset(blank, 0x00, CD_SECTOR_SIZE);

							for(int sp=0; sp<150; sp++)
								writer.WriteBytesRaw(blank, CD_SECTOR_SIZE);

							trackLBA += 150;

						}

						fprintf(cuefp, "    INDEX 01 %02d:%02d:%02d\n",
							(trackLBA/75)/60,
							(trackLBA/75)%60,
							trackLBA%75
						);

						// Pack the audio file
						if (!global::QuietMode)
							printf("    Packing audio %s... ", trackElement->Attribute("source"));

						if (PackWaveFile(&writer, trackElement->Attribute("source"))) {

							if (!global::QuietMode)
								printf("Done.\n");

						} else {

							writer.Close();
							fclose(cuefp);

							return EXIT_FAILURE;

						}

					}

				}


			// If an unknown track type is specified
			} else {

				if (!global::QuietMode)
					printf("    ");

				printf("ERROR: Unknown track type.\n");

				if (!global::NoIsoGen)
					writer.Close();

				if (cuefp != NULL)
					fclose(cuefp);

				return EXIT_FAILURE;

			}

			if (!global::QuietMode)
				printf("\n");

			trackElement = trackElement->NextSiblingElement("track");
			trackNum++;

		}

		// Get the last LBA of the image to calculate total size
		if (!global::NoIsoGen) {

			int totalImageSize = writer.SeekToEnd();

			// Close both ISO writer and CUE sheet
			writer.Close();

			if (cuefp != NULL)
				fclose(cuefp);

			if (!global::QuietMode) {

				printf("ISO image generated successfully.\n");
				printf("Total image size: %d bytes (%d sectors)\n\n", (CD_SECTOR_SIZE*totalImageSize), totalImageSize);

			}

		}

		// Check for next <iso_project> element
		projectElement = projectElement->NextSiblingElement("iso_project");

		if (global::OutputOverride) {

			printf("ERROR: -o switch cannot be used in multi-disc ISO projects.\n");
			return EXIT_FAILURE;

		}

	}

    return(0);

}


int ParseISOfileSystem(cd::IsoWriter* writer, tinyxml2::XMLElement* trackElement) {

	tinyxml2::XMLElement* identifierElement = trackElement->FirstChildElement("identifiers");
	tinyxml2::XMLElement* licenseElement = trackElement->FirstChildElement("license");

	// Print out identifiers if present
	if (!global::QuietMode) {

		if (identifierElement != NULL) {

			printf("    Identifiers:\n");
			if (identifierElement->Attribute("system") != NULL)
				printf("      System       : %s\n", identifierElement->Attribute("system"));
			if (identifierElement->Attribute("application") != NULL)
				printf("      Application  : %s\n", identifierElement->Attribute("application"));
			if (identifierElement->Attribute("volume") != NULL)
				printf("      Volume       : %s\n", identifierElement->Attribute("volume"));
			if (identifierElement->Attribute("volumeset") != NULL)
				printf("      Volume Set   : %s\n", identifierElement->Attribute("volumeset"));
			if (identifierElement->Attribute("publisher") != NULL)
				printf("      Publisher    : %s\n", identifierElement->Attribute("publisher"));
			if (identifierElement->Attribute("datapreparer") != NULL)
				printf("      Data Preparer: %s\n", identifierElement->Attribute("datapreparer"));
			printf("\n");

		}

	}

	if (licenseElement != NULL) {

		if (licenseElement->Attribute("file") != NULL) {

			if (strlen(licenseElement->Attribute("file")) == 0) {

				if (!global::QuietMode)
					printf("    ");

				printf("ERROR: file attribute of <license> element is blank.");

				return false;

			}

			if (!global::QuietMode)
				printf("    License file: %s\n\n", licenseElement->Attribute("file"));

			int licenseSize = GetSize(licenseElement->Attribute("file"));

            if (licenseSize < 0) {

				if (!global::QuietMode)
					printf("    ");

				printf("ERROR: Specified license file not found.\n");

				return false;

            } else if (licenseSize != 28032) {

            	if (!global::QuietMode)
					printf("    ");

				printf("WARNING: Specified license file may not be of correct format.\n");

            }


		} else {

			if (!global::QuietMode)
				printf("    ");

			printf("ERROR: <license> element has no file attribute.");

			return false;

		}


	}

	// Parse directory entries in the directory_tree element
	if (!global::QuietMode)
		printf("    Parsing directory tree...\n");

	iso::DirTreeClass dirTree;

	if (trackElement->FirstChildElement("directory_tree") == NULL) {

		if (!global::QuietMode)
			printf("      ");

		printf("ERROR: No directory_tree element specified.\n");
		return false;

	}


	if (!ParseDirectory(&dirTree, trackElement->FirstChildElement("directory_tree")))
		return false;


	// Calculate directory tree LBAs and retrieve size of image
	int imageLen = dirTree.CalculateTreeLBA(18+(((dirTree.CalculatePathTableLen()+2047)/2048)*4));

	if (!global::QuietMode) {

		printf("      Files Total: %d\n", dirTree.GetFileCountTotal());
		printf("      Directories: %d\n", dirTree.GetDirCountTotal());
		printf("      Estimated filesystem size: %d bytes (%d sectors)\n\n", 2352*imageLen, imageLen);

	}


	if (global::LBAfile != NULL) {

		FILE* fp = fopen(global::LBAfile, "w");

		dirTree.OutputLBAlisting(fp, 0);

		fclose(fp);

		if (!global::QuietMode)
			printf("    Wrote LBA file listing %s.\n\n", global::LBAfile);

	}

	if (global::LBAheaderFile != NULL) {

		FILE* fp = fopen(global::LBAheaderFile, "w");

		dirTree.OutputHeaderListing(fp, 0);

		fclose(fp);

		if (!global::QuietMode)
			printf("    Wrote LBA file listing header %s.\n\n", global::LBAheaderFile);

	}


	if (global::NoIsoGen)
		return true;


	// Write the file system
	if (!global::QuietMode)
		printf("    Building filesystem... ");

	{
		unsigned char subHead[] = { 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00 };
		writer->SetSubheader(subHead);
	}

	if ((global::NoLimit == false) && (dirTree.CalculatePathTableLen() > 2048)) {

		if (!global::QuietMode)
			printf("      ");

		printf("WARNING: Path table exceeds 2048 bytes.\n");

	}

	if (!global::QuietMode)
		printf("\n");


	// Write padding which will be written with proper data later on
	for(int i=0; i<18+(((dirTree.CalculatePathTableLen()+2047)/2048)*4); i++) {

		char buff[2048];
		memset(buff, 0x00, 2048);
		writer->WriteBytes(buff, 2048, cd::IsoWriter::EdcEccForm1);

	}


	// Copy the files into the disc image
	dirTree.WriteFiles(writer);


	// Write file system
	if (!global::QuietMode)
		printf("      Writing filesystem... ");

	dirTree.SortDirEntries();

	dirTree.WriteDirectoryRecords(writer, 0);


	// Set file system identifiers
	iso::IDENTIFIERS isoIdentifiers;
	memset(&isoIdentifiers, 0x00, sizeof(iso::IDENTIFIERS));

	if (identifierElement != NULL) {

		isoIdentifiers.SystemID		= identifierElement->Attribute("system", "");
		isoIdentifiers.VolumeID		= identifierElement->Attribute("volume", "");
		isoIdentifiers.VolumeSet	= identifierElement->Attribute("volumeset", "");
		isoIdentifiers.Publisher	= identifierElement->Attribute("publisher", "");
		isoIdentifiers.Application	= identifierElement->Attribute("application", "");
		isoIdentifiers.DataPreparer	= identifierElement->Attribute("datapreparer");

	}

	// Write file system descriptor to finish the image
	iso::WriteDescriptor(writer, isoIdentifiers, &dirTree, imageLen);

	if (!global::QuietMode)
		printf("Ok.\n");

	// Write license data
	if (licenseElement != NULL) {

		char buff[28032];

		FILE* fp = fopen(licenseElement->Attribute("file"), "rb");
		fread(buff, 1, 28032, fp);
		fclose(fp);

		if (!global::QuietMode)
			printf("      Writing license data...");

		iso::WriteLicenseData(writer, (void*)buff);

		if (!global::QuietMode)
			printf("Ok.\n");

	}

	return true;

}

int ParseDirectory(iso::DirTreeClass* dirTree, tinyxml2::XMLElement* dirElement) {

	const char *srcDir = dirElement->Attribute("srcdir");

	if (srcDir != NULL) {

		while(strchr(srcDir, '\\') != NULL)
			*strchr(srcDir, '\\') = '/';

	}

	dirElement = dirElement->FirstChildElement();

	while(dirElement != NULL) {

        if (strcasecmp("file", dirElement->Name()) == 0) {


			if ((strchr(dirElement->Attribute("name"), '\\') != NULL) || (strchr(dirElement->Attribute("name"), '/') != NULL)) {

                if (!global::QuietMode)
					printf("      ");

				printf("ERROR: Filename '%s' cannot be a path.\n", dirElement->Attribute("name"));
				return false;

			}

			if (strlen(dirElement->Attribute("name")) > 12) {

				if (!global::QuietMode)
					printf("      ");

				printf("ERROR: Filename '%s' is more than 12 characters long.\n", dirElement->Attribute("name"));
				return false;

			}

			int entry = iso::EntryFile;

			if (dirElement->Attribute("type") != NULL) {

				if (strcasecmp("data", dirElement->Attribute("type")) == 0) {

					entry = iso::EntryFile;

				} else if (strcasecmp("xa", dirElement->Attribute("type")) == 0) {

					entry = iso::EntryXA;

				} else if (strcasecmp("str", dirElement->Attribute("type")) == 0) {

					entry = iso::EntrySTR;

				} else {

					if (!global::QuietMode)
						printf("      ");

					printf("ERROR: Unknown type: %s\n", dirElement->Attribute("type"));
					return false;

				}

			}

			char *srcFilePath = NULL;
			const char *srcFile = dirElement->Attribute("source");

			if (srcFile == NULL)
				srcFile = dirElement->Attribute("name");

			if (srcDir != NULL) {

				srcFilePath = (char*)malloc(strlen(srcDir)+strlen(srcFile)+4);

				strcpy(srcFilePath, srcDir);
				strcat(srcFilePath, "/");
				strcat(srcFilePath, srcFile);

				srcFile = srcFilePath;

			}

			if (!dirTree->AddFileEntry(dirElement->Attribute("name"), entry, srcFile)) {

				if (srcFilePath != NULL)
					free(srcFilePath);

				return false;

			}

			if (srcFilePath != NULL)
				free(srcFilePath);

		} else if (strcasecmp("dummy", dirElement->Name()) == 0) {

			dirTree->AddDummyEntry(atoi(dirElement->Attribute("sectors")));

        } else if (strcasecmp("dir", dirElement->Name()) == 0) {

			if (strlen(dirElement->Attribute("name")) > 12) {

				printf("ERROR: Directory name %s is more than 12 characters long.\n", dirElement->Attribute("source"));

				return false;

			}

			iso::DirTreeClass* subdir = dirTree->AddSubDirEntry(dirElement->Attribute("name"));

			if (subdir == NULL)
				return false;

            if (!ParseDirectory(subdir, dirElement))
				return false;

        }

		dirElement = dirElement->NextSiblingElement();

	}

	return true;

}

int PackWaveFile(cd::IsoWriter* writer, const char* wavFile) {

	FILE *fp;

	if (!(fp = fopen(wavFile, "rb"))) {
		printf("ERROR: File not found.\n");
		return false;
	}

	// Get header chunk
	struct {
		char	id[4];
		int		size;
		char	format[4];
	} HeaderChunk;

	fread(&HeaderChunk, 1, sizeof(HeaderChunk), fp);

	if (memcmp(&HeaderChunk.id, "RIFF", 4) || memcmp(&HeaderChunk.format, "WAVE", 4)) {

		if (!global::QuietMode)
			printf("\n    ");

		printf("ERROR: File is not a Microsoft format WAV file.\n");

		fclose(fp);
		return false;

	}


	// Get header chunk
	struct {
		char	id[4];
		int		size;
		short	format;
		short	chan;
		int		freq;
		int		brate;
		short	balign;
		short	bps;
	} WAV_Subchunk1;

	fread(&WAV_Subchunk1, 1, sizeof(WAV_Subchunk1), fp);


	// Check if its a valid WAVE file
	if (memcmp(&WAV_Subchunk1.id, "fmt ", 4)) {

		if (!global::QuietMode)
			printf("\n    ");

		printf("ERROR: Unsupported WAV format.\n");

		fclose(fp);
		return false;

	}


    if ((WAV_Subchunk1.chan != 2) || (WAV_Subchunk1.freq != 44100) || (WAV_Subchunk1.bps != 16)) {

		if (!global::QuietMode)
			printf("\n    ");

		printf("ERROR: Only 44.1KHz, 16-bit Stereo WAV files are supported.\n");

		fclose(fp);
		return false;
    }


	// Search for the data chunk
	struct {
		char	id[4];
		int		len;
	} Subchunk2;

	while(1) {

		fread(&Subchunk2, 1, sizeof(Subchunk2), fp);

		if (memcmp(&Subchunk2.id, "data", 4)) {

			fseek(fp, Subchunk2.len, SEEK_CUR);

		} else {

			break;

		}

	}

    int waveLen = Subchunk2.len;

	while(waveLen > 0) {

		unsigned char buff[CD_SECTOR_SIZE];
		memset(buff, 0x00, CD_SECTOR_SIZE);

        int readLen = waveLen;

        if (readLen > 2352)
			readLen = 2352;

		fread(buff, 1, readLen, fp);

        writer->WriteBytesRaw(buff, 2352);

        waveLen -= readLen;

	}

	fclose(fp);

	return true;

}

int GetSize(const char* fileName) {

	struct stat fileAttrib;

    if (stat(fileName, &fileAttrib) != 0)
		return -1;

	return fileAttrib.st_size;

}
