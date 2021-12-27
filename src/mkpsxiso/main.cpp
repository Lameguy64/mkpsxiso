#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <string>
#include <filesystem>
#include "common.h"
#include "cdwriter.h"	// CD image writer module
#include "iso.h"		// ISO file system generator module
#include "xml.h"
#include "platform.h"

#define MA_NO_THREADING
#define MA_NO_DEVICE_IO
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "miniaudio_pcm.h"


namespace global
{
	time_t		BuildTime;
	int			QuietMode	= false;
	int			Overwrite	= false;

	int			NoLimit		= false;
	int			trackNum	= 1;
	int			noXA		= false;

	std::filesystem::path XMLscript;
	std::filesystem::path LBAfile;
	std::filesystem::path LBAheaderFile;
	std::filesystem::path ImageName;

	std::optional<std::filesystem::path> cuefile;
	int			NoIsoGen = false;
};


bool ParseDirectory(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* parentElement, const std::filesystem::path& xmlPath, const iso::EntryAttributes& parentAttribs, bool& found_da);
int ParseISOfileSystem(cd::IsoWriter* writer, FILE* cue_fp, const tinyxml2::XMLElement* trackElement, const std::filesystem::path& xmlPath);

int PackFileAsCDDA(cd::IsoWriter* writer, const std::filesystem::path& audioFile);

// Helper wrapper to simplify dealing with paths on Windows
ma_result ma_decoder_init_path(const std::filesystem::path& pFilePath, const ma_decoder_config* pConfig, ma_decoder* pDecoder)
{
#ifdef _WIN32
	return ma_decoder_init_file_w(pFilePath.c_str(), pConfig, pDecoder);
#else
	return ma_decoder_init_file(pFilePath.c_str(), pConfig, pDecoder);
#endif
}

int Main(int argc, char* argv[])
{
	static constexpr const char* HELP_TEXT =
		"mkpsxiso [-h|--help] [-y] [-q|--quiet] [-o|--output <file>] [-lba <file>] [-lbahead <file>] [-nolimit]\n"
		"  [-noisogen] <xml>\n\n"
		"  -y        - Always overwrite ISO image files.\n"
		"  -q|--quiet - Quiet mode (prints nothing but warnings and errors).\n"
		"  -o|--output - Specifies output file name (overrides XML but not cue_sheet).\n"
		"  <xml>     - File name of an ISO image project in XML document format.\n\n"
		"Special Options:\n\n"
		"  -lba      - Outputs a log of all files packed with LBA information.\n"
		"  -lbahead  - Outputs a C header of all the file's LBA addresses.\n"
		"  -nolimit  - Remove warning when a directory record exceeds a sector.\n"
		"  -noisogen - Do not generate ISO but calculates file LBAs only\n"
		"              (To be used with -lba or -lbahead without generating ISO).\n"
		"  -noxa     - Do not generate CD-XA file attributes\n"
		"              (XA data can still be included but not recommended).\n"
		"  -h|--help - Show this help text\n";

	static constexpr const char* VERSION_TEXT = 
		"MKPSXISO " VERSION " - PlayStation ISO Image Maker\n"
		"2017-2018 Meido-Tek Productions (Lameguy64)\n"
		"2021 Silent, Chromaryu, and G4Vi\n\n";

	bool OutputOverride = false;

	// Parse arguments
	for (char** args = argv+1; *args != nullptr; args++)
	{
		// Is it a switch?
		if ((*args)[0] == '-')
		{
			if (ParseArgument(args, "h", "help"))
			{
				printf(VERSION_TEXT);
				printf(HELP_TEXT);
				return EXIT_SUCCESS;
			}
			if (auto lbaHead = ParsePathArgument(args, "lbahead"); lbaHead.has_value())
			{
				global::LBAheaderFile = *lbaHead;
				continue;
			}
			if (ParseArgument(args, "nolimit"))
			{
				global::NoLimit = true;
				continue;
			}
			if (ParseArgument(args, "noisogen"))
			{
				global::NoIsoGen = true;
				continue;
			}
			if (ParseArgument(args, "q", "quiet"))
			{
				global::QuietMode = true;
				continue;
			}
			if (auto lbaFile = ParsePathArgument(args, "lba"); lbaFile.has_value())
			{
				global::LBAfile	= *lbaFile;
				continue;
			}
			if (auto output = ParsePathArgument(args, "o", "output"); output.has_value())
			{
				global::ImageName = *output;
				OutputOverride = true;
				continue;
			}
			if (ParseArgument(args, "y"))
			{
				global::Overwrite = true;
				continue;
			}
			if (ParseArgument(args, "noxa"))
			{
				global::noXA = true;
				continue;
			}
			
			// If we reach this point, an unknown parameter was passed
			printf("Unknown parameter: %s\n", *args);
			return EXIT_FAILURE;
		}
		else
		{
			if ( global::XMLscript.empty() )
			{
				global::XMLscript = std::filesystem::u8path(*args);
			}
		}

	}

	if ( (!global::QuietMode) || (argc == 1) )
	{
		printf(VERSION_TEXT);
	}

	if ( argc == 1 )
	{
		printf(HELP_TEXT);
		return EXIT_SUCCESS;
	}

	if ( global::XMLscript.empty() )
	{
		printf( "No XML script specified.\n" );
		return EXIT_FAILURE;
	}

	// Get current time to be used as date stamps for all directories
	time( &global::BuildTime );

	// Load XML file
	tinyxml2::XMLDocument xmlFile;
	{
		tinyxml2::XMLError error;
		if (FILE* file = OpenFile(global::XMLscript, "rb"); file != nullptr)
		{
			error = xmlFile.LoadFile(file);
			fclose(file);
		}
		else
		{
			error = tinyxml2::XML_ERROR_FILE_NOT_FOUND;
		}

		if ( error != tinyxml2::XML_SUCCESS )
		{
			printf("ERROR: ");
			if ( error == tinyxml2::XML_ERROR_FILE_NOT_FOUND )
			{
				printf("File not found.\n");
			}
			else if ( error == tinyxml2::XML_ERROR_FILE_COULD_NOT_BE_OPENED )
			{
				printf("File cannot be opened.\n");
			}
			else if ( error == tinyxml2::XML_ERROR_FILE_READ_ERROR )
			{
				printf("Error reading file.\n");
			}
			else
			{
				printf("%s on line %d\n", xmlFile.ErrorName(), xmlFile.ErrorLineNum());
			}
			return EXIT_FAILURE;
		}
    }

	// Check if there is an <iso_project> element
    const tinyxml2::XMLElement* projectElement =
		xmlFile.FirstChildElement(xml::elem::ISO_PROJECT);

    if ( projectElement == nullptr )
	{
		printf( "ERROR: Cannot find <iso_project> element in XML document.\n" );
		return EXIT_FAILURE;
    }

    int imagesCount = 0;

	// Build loop for XML scripts with multiple <iso_project> elements
	while ( projectElement != nullptr )
	{
		imagesCount++;
		if ( imagesCount > 1 && OutputOverride )
		{
			printf( "ERROR: -o switch cannot be used in multi-disc ISO "
				"project.\n" );
			return EXIT_FAILURE;
		}

		// Check if image_name attribute is specified
		if ( global::ImageName.empty() )
		{
			if ( const char* image_name = projectElement->Attribute(xml::attrib::IMAGE_NAME); image_name != nullptr )
			{
				global::ImageName = std::filesystem::u8path(image_name);
			}
			else
			{
				printf( "ERROR: %s attribute not specfied in "
					"<iso_project> element.\n", xml::attrib::IMAGE_NAME );
				return EXIT_FAILURE;
			}
		}

		if ( const char* cue_sheet = projectElement->Attribute(xml::attrib::CUE_SHEET); cue_sheet != nullptr )
		{
			global::cuefile = std::filesystem::u8path(cue_sheet);
		}

		if ( !global::QuietMode )
		{
			printf( "Building ISO Image: %" PRFILESYSTEM_PATH, global::ImageName.lexically_normal().c_str() );

			if ( global::cuefile )
			{
				printf( " + %" PRFILESYSTEM_PATH, global::cuefile->lexically_normal().c_str() );
			}

			printf( "\n" );
		}

		global::noXA = projectElement->IntAttribute( xml::attrib::NO_XA, 0 );

		if ( ( !global::Overwrite ) && ( !global::NoIsoGen ) )
		{
			if ( GetSize( global::ImageName ) >= 0 )
			{
				printf( "WARNING: ISO image already exists, overwrite? <y/n> " );
				char key;

				do {

					key = getchar();

					if ( std::tolower( key ) == 'n' )
					{
						return EXIT_FAILURE;
					}
				} while( tolower( key ) != 'y' );

				printf( "\n" );

			}
			else
			{
				printf( "\n" );
			}

		}


		// Check if there is a track element specified
		const tinyxml2::XMLElement* trackElement =
			projectElement->FirstChildElement(xml::elem::TRACK);

		if ( trackElement == nullptr )
		{
			printf( "ERROR: At least one <track> element must be specified.\n" );
			return EXIT_FAILURE;
		}


		// Check if cue_sheet attribute is specified
		FILE*	cuefp = nullptr;

		if ( !global::NoIsoGen )
		{
			if ( global::cuefile )
			{
				if ( global::cuefile->empty() )
				{
					if ( !global::QuietMode )
					{
						printf( "  " );
					}

					printf( "ERROR: %s attribute is blank.\n", xml::attrib::CUE_SHEET );

					return EXIT_FAILURE;
				}

				cuefp = OpenFile( global::cuefile.value(), "w" );

				if ( cuefp == nullptr )
				{
					if ( !global::QuietMode )
					{
						printf( "  " );
					}

					printf( "ERROR: Unable to create cue sheet.\n" );

					return EXIT_FAILURE;
				}

				fprintf(cuefp, "FILE \"%" PRFILESYSTEM_PATH "\" BINARY\n", global::ImageName.filename().c_str());
			}
		}


		// Create ISO image for writing
		cd::IsoWriter writer;

		if ( !global::NoIsoGen )
		{
			if ( !writer.Create( global::ImageName.c_str() ) ) {

				if ( !global::QuietMode )
				{
					printf( "  " );
				}

				printf( "ERROR: Cannot open or create output image file.\n" );

				if ( cuefp != nullptr )
				{
					fclose( cuefp );
				}

				return EXIT_FAILURE;

			}
		}

		global::trackNum = 1;

		// Parse tracks
		while ( trackElement != nullptr )
		{
			const char* track_type = trackElement->Attribute(xml::attrib::TRACK_TYPE);

			if ( track_type == nullptr )
			{
				if ( !global::QuietMode )
				{
					printf( "  " );
				}

				printf( "ERROR: %s attribute not specified in <track> "
					"element on line %d.\n", xml::attrib::TRACK_TYPE, trackElement->GetLineNum() );

				if ( !global::NoIsoGen )
				{
					writer.Close();
				}

				std::error_code ec;
				std::filesystem::remove(global::ImageName, ec);

				if ( cuefp != nullptr )
				{
					fclose(cuefp);
				}

				return EXIT_FAILURE;
			}

			if ( !global::QuietMode )
			{
				printf( "  Track #%d %s:\n", global::trackNum,
					track_type );
			}

			// Generate ISO file system for data track
			if ( CompareICase( "data", track_type ) )
			{
				if ( global::trackNum != 1 )
				{
					if ( !global::QuietMode )
					{
						printf( "  " );
					}

					printf( "ERROR: Only the first track can be set as a "
						"data track on line: %d\n", trackElement->GetLineNum() );

					if ( !global::NoIsoGen )
					{
						writer.Close();
					}

					if ( cuefp != nullptr )
					{
						fclose( cuefp );
					}

					return EXIT_FAILURE;
				}

				if ( !ParseISOfileSystem( &writer, cuefp, trackElement, global::XMLscript.parent_path() ) )
				{
					if ( !global::NoIsoGen )
					{
						writer.Close();
					}

					std::error_code ec;
					std::filesystem::remove(global::ImageName, ec);

					if ( cuefp != nullptr )
					{
						fclose( cuefp );
						std::filesystem::remove(std::filesystem::u8path(projectElement->Attribute(xml::attrib::CUE_SHEET)), ec);
					}

					return EXIT_FAILURE;
				}

				if ( global::NoIsoGen )
				{
					printf( "Skipped generating ISO image.\n" );
					break;
				}

				if ( !global::QuietMode )
				{
					printf("\n");
				}

			// Add audio track
			}
			else if ( CompareICase( "audio", track_type ) )
			{

				// Only allow audio tracks if the cue_sheet attribute is specified
				if ( cuefp == nullptr )
				{
					if ( !global::QuietMode )
					{
						printf( "    " );
					}

					printf( "ERROR: %s attribute must be specified "
						"when using audio tracks.\n", xml::attrib::CUE_SHEET );

					if ( !global::NoIsoGen )
					{
						writer.Close();
					}

					return EXIT_FAILURE;
				}

				// Write track information to the CUE sheet
				if ( const char* track_source = trackElement->Attribute(xml::attrib::TRACK_SOURCE); track_source == nullptr )
				{
					if ( !global::QuietMode )
					{
						printf("    ");
					}

					printf( "ERROR: %s attribute not specified "
						"for track on line %d.\n", xml::attrib::TRACK_SOURCE, trackElement->GetLineNum() );

					if ( !global::NoIsoGen )
					{
						writer.Close();
					}

					if ( cuefp != nullptr )
					{
						fclose(cuefp);
					}

					return EXIT_FAILURE;
				}
				else
				{
					fprintf( cuefp, "  TRACK %02d AUDIO\n", global::trackNum );

					if ( !global::NoIsoGen )
					{
						int trackLBA = writer.SeekToEnd();

                        // TODO configurable pregap
						fprintf( cuefp, "    INDEX 00 %02d:%02d:%02d\n",
								(trackLBA/75)/60, (trackLBA/75)%60,
								trackLBA%75 );

						writer.WriteBlankSectors(150);
						trackLBA += 150;

						fprintf( cuefp, "    INDEX 01 %02d:%02d:%02d\n",
							(trackLBA/75)/60, (trackLBA/75)%60, trackLBA%75 );

						// Pack the audio file
						if ( !global::QuietMode )
						{
							printf( "    Packing audio %s... ", track_source );
						}

						if ( PackFileAsCDDA( &writer, track_source ) )
						{
							if ( !global::QuietMode )
							{
								printf( "Done.\n" );
							}

						}
						else
						{
							writer.Close();
							fclose( cuefp );

							return EXIT_FAILURE;
						}

					}

				}

				if ( !global::QuietMode )
				{
					printf( "\n" );
				}

			// If an unknown track type is specified
			}
			else
			{
				if ( !global::QuietMode )
				{
					printf( "    " );
				}

				printf( "ERROR: Unknown track type on line %d.\n",
					trackElement->GetLineNum() );

				if ( !global::NoIsoGen )
				{
					writer.Close();
				}

				if ( cuefp != nullptr )
				{
					fclose(cuefp);
				}

				return EXIT_FAILURE;
			}

			trackElement = trackElement->NextSiblingElement(xml::elem::TRACK);
			global::trackNum++;
		}

		// Get the last LBA of the image to calculate total size
		if ( !global::NoIsoGen )
		{
			int totalImageSize = writer.SeekToEnd();

			// Close both ISO writer and CUE sheet
			writer.Close();

			if ( cuefp != nullptr )
			{
				fclose( cuefp );
			}

			if ( !global::QuietMode )
			{
				printf( "ISO image generated successfully.\n" );
				printf( "Total image size: %d bytes (%d sectors)\n",
					(CD_SECTOR_SIZE*totalImageSize), totalImageSize );
			}
		}

		// Check for next <iso_project> element
		projectElement = projectElement->NextSiblingElement(xml::elem::ISO_PROJECT);

	}

    return 0;
}

iso::EntryAttributes ReadEntryAttributes( const tinyxml2::XMLElement* dirElement )
{
	iso::EntryAttributes result;

	const char* GMToffs = dirElement->Attribute(xml::attrib::GMT_OFFSET);
	if ( GMToffs != nullptr )
	{
		result.GMTOffs = static_cast<signed char>(strtol(GMToffs, nullptr, 0));
	}

	const char* XAAttrib = dirElement->Attribute(xml::attrib::XA_ATTRIBUTES);
	if ( XAAttrib != nullptr )
	{
		result.XAAttrib = static_cast<unsigned char>(strtoul(XAAttrib, nullptr, 0));
	}

	const char* XAPerm = dirElement->Attribute(xml::attrib::XA_PERMISSIONS);
	if ( XAPerm != nullptr )
	{
		result.XAPerm = static_cast<unsigned short>(strtoul(XAPerm, nullptr, 0));
	}

	const char* GID = dirElement->Attribute(xml::attrib::XA_GID);
	if ( GID != nullptr )
	{
		result.GID = static_cast<unsigned short>(strtoul(GID, nullptr, 0));
	}

	const char* UID = dirElement->Attribute(xml::attrib::XA_UID);
	if ( UID != nullptr )
	{
		result.UID = static_cast<unsigned short>(strtoul(UID, nullptr, 0));
	}

	return result;
};

int ParseISOfileSystem(cd::IsoWriter* writer, FILE* cue_fp, const tinyxml2::XMLElement* trackElement, const std::filesystem::path& xmlPath)
{
	const tinyxml2::XMLElement* identifierElement =
		trackElement->FirstChildElement(xml::elem::IDENTIFIERS);
	const tinyxml2::XMLElement* licenseElement =
		trackElement->FirstChildElement(xml::elem::LICENSE);

	// Set file system identifiers
	iso::IDENTIFIERS isoIdentifiers {};

	if ( identifierElement != nullptr )
	{
		isoIdentifiers.SystemID		= identifierElement->Attribute(xml::attrib::SYSTEM_ID);
		isoIdentifiers.VolumeID		= identifierElement->Attribute(xml::attrib::VOLUME_ID);
		isoIdentifiers.VolumeSet	= identifierElement->Attribute(xml::attrib::VOLUME_SET);
		isoIdentifiers.Publisher	= identifierElement->Attribute(xml::attrib::PUBLISHER);
		isoIdentifiers.Application	= identifierElement->Attribute(xml::attrib::APPLICATION);
		isoIdentifiers.DataPreparer	= identifierElement->Attribute(xml::attrib::DATA_PREPARER);
		isoIdentifiers.Copyright	= identifierElement->Attribute(xml::attrib::COPYRIGHT);
		isoIdentifiers.CreationDate = identifierElement->Attribute(xml::attrib::CREATION_DATE);
		isoIdentifiers.ModificationDate = identifierElement->Attribute(xml::attrib::MODIFICATION_DATE);

		bool hasSystemID = true;
		if ( isoIdentifiers.SystemID == nullptr )
		{
			hasSystemID = false;
			isoIdentifiers.SystemID = "PLAYSTATION";
		}

		bool hasApplication = true;
		if ( isoIdentifiers.Application == nullptr )
		{
			hasApplication = false;
			isoIdentifiers.Application = "PLAYSTATION";
		}

		// Print out identifiers if present
		if ( !global::QuietMode )
		{
			printf("    Identifiers:\n");
			printf( "      System       : %s%s\n",
				isoIdentifiers.SystemID,
				hasSystemID ? "" : " (default)" );

			printf( "      Application  : %s%s\n",
				isoIdentifiers.Application,
				hasApplication ? "" : " (default)" );

			if ( isoIdentifiers.VolumeID != nullptr )
			{
				printf( "      Volume       : %s\n",
					isoIdentifiers.VolumeID );
			}
			if ( isoIdentifiers.VolumeSet != nullptr )
			{
				printf( "      Volume Set   : %s\n",
					isoIdentifiers.VolumeSet );
			}
			if ( isoIdentifiers.Publisher != nullptr )
			{
				printf( "      Publisher    : %s\n",
					isoIdentifiers.Publisher );
			}
			if ( isoIdentifiers.DataPreparer != nullptr )
			{
				printf( "      Data Preparer: %s\n",
					isoIdentifiers.DataPreparer );
			}
			if ( isoIdentifiers.Copyright != nullptr )
			{
				printf( "      Copyright    : %s\n",
					isoIdentifiers.Copyright );
			}
			if ( isoIdentifiers.CreationDate != nullptr )
			{
				printf( "      Creation Date : %s\n",
					isoIdentifiers.CreationDate );
			}
			if ( isoIdentifiers.ModificationDate != nullptr )
			{
				printf( "      Modification Date : %s\n",
					isoIdentifiers.ModificationDate );
			}
			printf( "\n" );
		}
	}

	if ( licenseElement != nullptr )
	{
		if ( const char* license_file_attrib = licenseElement->Attribute(xml::attrib::LICENSE_FILE); license_file_attrib != nullptr )
		{
			const std::filesystem::path license_file{xmlPath / std::filesystem::u8path(license_file_attrib)};
			if ( license_file.empty() )
			{
				if ( !global::QuietMode )
				{
					printf( "    " );
				}

				printf("ERROR: file attribute of <license> element is missing "
					"or blank on line %d\n.", licenseElement->GetLineNum() );

				return false;
			}

			if ( !global::QuietMode )
			{
				printf( "    License file: %" PRFILESYSTEM_PATH "\n\n", license_file.lexically_normal().c_str() );
			}

			int64_t licenseSize = GetSize( license_file );

            if ( licenseSize < 0 )
			{
				if ( !global::QuietMode )
				{
					printf( "    " );
				}

				printf( "ERROR: Specified license file not found on line %d.\n",
					licenseElement->GetLineNum() );

				return false;

            }
			else if ( licenseSize != 28032 )
			{
            	if ( !global::QuietMode )
				{
					printf("    ");
				}
				printf( "WARNING: Specified license file may not be of "
					"correct format.\n" );
            }


		}
		else
		{
			if ( !global::QuietMode )
			{
				printf( "    " );
			}

			printf( "ERROR: <license> element has no file attribute on line %d.\n",
				licenseElement->GetLineNum() );

			return false;
		}


	}

	// Establish the volume timestamp to either the current local time or isoIdentifiers.CreationDate (if specified)
	cd::ISO_DATESTAMP volumeDate;
	bool gotDateFromXML = false;
	if ( isoIdentifiers.CreationDate != nullptr )
	{
		// Try to use time from XML. If it's malformed, fall back to local time.
		volumeDate = GetDateFromString(isoIdentifiers.CreationDate, &gotDateFromXML);
	}

	if ( !gotDateFromXML )
	{
		// Use local time
		const tm imageTime = *gmtime( &global::BuildTime );

		volumeDate.year = imageTime.tm_year;
		volumeDate.month = imageTime.tm_mon + 1;
		volumeDate.day = imageTime.tm_mday;
		volumeDate.hour = imageTime.tm_hour;
		volumeDate.minute = imageTime.tm_min;
		volumeDate.second = imageTime.tm_sec;

		// Calculate the GMT offset. It doesn't have to be perfect, but this should cover most/all normal cases.
		const time_t timeUTC = global::BuildTime;

		tm localTime = imageTime;
		const time_t timeLocal = mktime(&localTime);

		const double diff = difftime(timeUTC, timeLocal);
		volumeDate.GMToffs = static_cast<signed char>(diff / 60.0 / 15.0); // Seconds to 15-minute units
	}

	// Parse directory entries in the directory_tree element
	if ( !global::QuietMode )
	{
		printf( "    Parsing directory tree...\n" );
	}

	iso::EntryList entries;
	iso::DIRENTRY& root = iso::DirTreeClass::CreateRootDirectory(entries, volumeDate);
	iso::DirTreeClass* dirTree = root.subdir.get();

	const tinyxml2::XMLElement* directoryTree = trackElement->FirstChildElement(xml::elem::DIRECTORY_TREE);
	if ( directoryTree == nullptr )
	{
		if ( !global::QuietMode )
		{
			printf( "      " );
		}
		printf( "ERROR: No %s element specified for data track "
			"on line %d.\n", xml::elem::DIRECTORY_TREE, trackElement->GetLineNum() );
		return false;
	}

	bool found_da = false;	
	const iso::EntryAttributes rootAttributes = iso::EntryAttributes::Overlay(iso::EntryAttributes::MakeDefault(), ReadEntryAttributes(directoryTree));
	if ( !ParseDirectory(dirTree, directoryTree, xmlPath, rootAttributes, found_da) )
	{
		return false;
	}

	// Calculate directory tree LBAs and retrieve size of image
	int pathTableLen = dirTree->CalculatePathTableLen(root);

	const int rootLBA = 17+(GetSizeInSectors(pathTableLen)*4);
	int totalLen = dirTree->CalculateTreeLBA(rootLBA);

	if ( !global::QuietMode )
	{
		printf( "      Files Total: %d\n", dirTree->GetFileCountTotal() );
		printf( "      Directories: %d\n", dirTree->GetDirCountTotal() );
		printf( "      Total file system size: %d bytes (%d sectors)\n\n",
			2352*totalLen, totalLen);
	}

	if ( !global::LBAfile.empty() )
	{
		FILE* fp = OpenFile( global::LBAfile, "w" );
		if (fp != nullptr)
		{
			fprintf( fp, "File LBA log generated by MKPSXISO v" VERSION "\n\n" );
			fprintf( fp, "Image bin file: %" PRFILESYSTEM_PATH "\n", global::ImageName.lexically_normal().c_str() );

			if ( global::cuefile )
			{
				fprintf( fp, "Image cue file: %" PRFILESYSTEM_PATH "\n", global::cuefile->lexically_normal().c_str() );
			}

			fprintf( fp, "\nFile System:\n\n" );
			fprintf( fp, "    Type  Name             Length    LBA       "
				"Timecode    Bytes     Source File\n\n" );

			dirTree->OutputLBAlisting( fp, 0 );

			fclose( fp );

			if ( !global::QuietMode )
			{
				printf( "    Wrote file LBA log %" PRFILESYSTEM_PATH ".\n\n",
					global::LBAfile.lexically_normal().c_str() );
			}
		}
		else
		{
			if ( !global::QuietMode )
			{
				printf( "    Failed to write LBA log %" PRFILESYSTEM_PATH "!\n\n",
					global::LBAfile.lexically_normal().c_str() );
			}
		}
	}

	if ( !global::LBAheaderFile.empty() )
	{
		FILE* fp = OpenFile( global::LBAheaderFile, "w" );
		if (fp != nullptr)
		{
			dirTree->OutputHeaderListing( fp, 0 );

			fclose( fp );

			if ( !global::QuietMode )
			{
				printf( "    Wrote file LBA listing header %" PRFILESYSTEM_PATH ".\n\n",
					global::LBAheaderFile.lexically_normal().c_str() );
			}
		}
		else
		{
			if ( !global::QuietMode )
			{
				printf( "    Failed to write LBA listing header %" PRFILESYSTEM_PATH ".\n\n",
					global::LBAheaderFile.lexically_normal().c_str() );
			}
		}
	}

	if ( cue_fp != nullptr )
	{
		fprintf( cue_fp, "  TRACK 01 MODE2/2352\n" );
		fprintf( cue_fp, "    INDEX 01 00:00:00\n" );

		dirTree->WriteCueEntries( cue_fp, &global::trackNum );
	}

	if ( global::NoIsoGen )
	{
		return true;
	}

	// Write the file system
	if ( !global::QuietMode )
	{
		printf( "    Building filesystem... " );
	}

	//unsigned char subHead[] = { 0x00, 0x00, 0x08, 0x00 };
	writer->SetSubheader( cd::IsoWriter::SubData );

	if ( (global::NoLimit == false) &&
		(dirTree->CalculatePathTableLen(root) > 2048) )
	{
		if ( !global::QuietMode )
		{
			printf( "      " );
		}
		printf( "WARNING: Path table exceeds 2048 bytes.\n" );
	}

	if ( !global::QuietMode )
	{
		printf( "\n" );
	}

	// Write padding which will be written with proper data later on
	for ( int i=0; i<18+(GetSizeInSectors(dirTree->CalculatePathTableLen(root))*4); i++ )
	{
		char buff[2048];
		memset( buff, 0x00, 2048 );
		writer->WriteBytes( buff, 2048, cd::IsoWriter::EdcEccForm1 );
	}

	// Copy the files into the disc image
	dirTree->WriteFiles( writer );

	// Write file system
	if ( !global::QuietMode )
	{
		printf( "      Writing filesystem... " );
	}

	// Sort directory entries and write it
	dirTree->SortDirectoryEntries();
	dirTree->WriteDirectoryRecords( writer, root, root );

	// Write file system descriptors to finish the image
	iso::WriteDescriptor( writer, isoIdentifiers, root, totalLen );

	if ( !global::QuietMode )
	{
		printf( "Ok.\n" );
	}

	// Write license data
	if ( licenseElement != nullptr )
	{
		FILE* fp = OpenFile( xmlPath / std::filesystem::u8path(licenseElement->Attribute(xml::attrib::LICENSE_FILE)), "rb" );
		if (fp != nullptr)
		{
			auto license = std::make_unique<cd::ISO_LICENSE>();
			if (fread( license->data, sizeof(license->data), 1, fp ) == 1)
			{
				if ( !global::QuietMode )
				{
					printf( "      Writing license data..." );
				}

				iso::WriteLicenseData( writer, license->data );

				if ( !global::QuietMode )
				{
					printf( "Ok.\n" );
				}
			}
			fclose( fp );
		}
	}

	return true;
}

static bool ParseFileEntry(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* dirElement, const std::filesystem::path& xmlPath, const iso::EntryAttributes& parentAttribs, bool& found_da)
{
	const char* nameElement = dirElement->Attribute(xml::attrib::ENTRY_NAME);
	const char* sourceElement = dirElement->Attribute(xml::attrib::ENTRY_SOURCE);

	if ( nameElement == nullptr && sourceElement == nullptr )
	{
		if ( !global::QuietMode )
		{
			printf("      ");
		}

		printf( "ERROR: Missing name and source attributes on "
			"line %d.\n", dirElement->GetLineNum() );

		return false;
	}

	std::filesystem::path srcFile;
	if ( sourceElement != nullptr )
	{
		srcFile = std::filesystem::u8path(sourceElement);
	}

	std::string name;
	if ( nameElement != nullptr )
	{
		name = nameElement;
	}
	else
	{
		name = srcFile.filename().u8string();
	}

	if ( srcFile.empty() )
	{
		srcFile = name;
	}

	if ( name.find_first_of( "\\/" ) != std::string::npos )
	{
		if ( !global::QuietMode )
		{
			printf("      ");
		}

		printf( "ERROR: Name attribute for file entry '%s' cannot be "
			"a path on line %d.\n", name.c_str(),
			dirElement->GetLineNum() );

		return false;
	}

	if ( name.size() > 12 )
	{
		if ( !global::QuietMode )
		{
			printf( "      " );
		}

		printf( "ERROR: Name entry for file '%s' is more than 12 "
			"characters long on line %d.\n", name.c_str(),
			dirElement->GetLineNum() );

		return false;
	}

	EntryType entry = EntryType::EntryFile;

	const char* typeElement = dirElement->Attribute(xml::attrib::ENTRY_TYPE);
	if ( typeElement != nullptr )
	{
		if ( CompareICase( "data", typeElement ) )
		{
			entry = EntryType::EntryFile;
		} else if ( CompareICase( "mixed", typeElement ) ||
                    CompareICase( "xa", typeElement ) || //alias xa and str to mixed
                    CompareICase( "str", typeElement ) )
		{
			entry = EntryType::EntryXA;
		}
		else if ( CompareICase( "da", typeElement ) )
		{
			entry = EntryType::EntryDA;
			if ( !global::cuefile )
			{
				if ( !global::QuietMode )
				{
					printf( "      " );
				}
				printf( "ERROR: DA audio file(s) specified but no CUE sheet specified.\n" );
				return false;
			}
			found_da = true;
		}
		else
		{
			if ( !global::QuietMode )
			{
				printf( "      " );
			}

			printf( "ERROR: Unknown type %s on line %d\n",
				dirElement->Attribute(xml::attrib::ENTRY_TYPE),
				dirElement->GetLineNum() );

			return false;
		}

		if ( found_da && entry != EntryType::EntryDA )
		{
			if ( !global::QuietMode )
			{
				printf( "      " );
			}

			printf( "ERROR: Cannot place file past a DA audio file on line %d.\n",
				dirElement->GetLineNum() );

			return false;
		}

	}

	return dirTree->AddFileEntry(name.c_str(), entry, xmlPath / srcFile, iso::EntryAttributes::Overlay(parentAttribs, ReadEntryAttributes(dirElement)));
}

static bool ParseDummyEntry(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* dirElement, const bool found_da)
{
	if ( found_da )
	{
		if ( !global::QuietMode )
		{
			printf( "      " );
		}

		printf( "ERROR: Cannot place dummy past a DA audio file on line %d.\n",
			dirElement->GetLineNum() );

		return false;
	}

	// TODO: For now this is a hack, unify this code again with the file type in the future
	// so it isn't as awkward
	int dummyType = 0; // Data
	const char* type = dirElement->Attribute(xml::attrib::ENTRY_TYPE);
	if ( type != nullptr )
	{
		// TODO: Make reasonable
		if ( CompareICase(type, "2336") )
		{
			dummyType = 1; // XA
		}
	}


	dirTree->AddDummyEntry( atoi( dirElement->Attribute(xml::attrib::NUM_DUMMY_SECTORS) ), dummyType );
	return true;
}

static bool ParseDirEntry(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* dirElement, const std::filesystem::path& xmlPath, const iso::EntryAttributes& parentAttribs, bool& found_da)
{
	const char* nameElement = dirElement->Attribute(xml::attrib::ENTRY_NAME);
	if ( strlen( nameElement ) > 12 )
	{
		printf( "ERROR: Directory name %s on line %d is more than 12 "
			"characters long.\n", nameElement,
				dirElement->GetLineNum() );
		return false;
	}

	const iso::EntryAttributes attribs = iso::EntryAttributes::Overlay(parentAttribs, ReadEntryAttributes(dirElement));

	std::filesystem::path srcDir;
	if (const char* sourceElement = dirElement->Attribute(xml::attrib::ENTRY_SOURCE); sourceElement != nullptr)
	{
		srcDir = xmlPath / std::filesystem::u8path(sourceElement);
	}

	bool alreadyExists = false;
	iso::DirTreeClass* subdir = dirTree->AddSubDirEntry(
		nameElement, srcDir, attribs, alreadyExists );

	if ( subdir == nullptr )
	{
		return false;
	}

	if ( found_da && !alreadyExists )
	{
		if ( !global::QuietMode )
		{
			printf( "      " );
		}

		printf( "ERROR: Cannot place directory past a DA audio file on line %d\n",
			dirElement->GetLineNum() );

		return false;
	}

	return ParseDirectory(subdir, dirElement, xmlPath, attribs, found_da);
}

bool ParseDirectory(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* parentElement, const std::filesystem::path& xmlPath, const iso::EntryAttributes& parentAttribs, bool& found_da)
{
	for ( const tinyxml2::XMLElement* dirElement = parentElement->FirstChildElement(); dirElement != nullptr; dirElement = dirElement->NextSiblingElement() )
	{
        if ( CompareICase( "file", dirElement->Name() ))
		{
			if (!ParseFileEntry(dirTree, dirElement, xmlPath, parentAttribs, found_da))
			{
				return false;
			}
		}
		else if ( CompareICase( "dummy", dirElement->Name() ))
		{
			if (!ParseDummyEntry(dirTree, dirElement, found_da))
			{
				return false;
			}
        }
		else if ( CompareICase( "dir", dirElement->Name() ))
		{
			if (!ParseDirEntry(dirTree, dirElement, xmlPath, parentAttribs, found_da))
			{
				return false;
			}
        }
	}

	return true;
}

typedef enum {
	DAF_WAV,
	DAF_FLAC,
	DAF_MP3,
	DAF_PCM
} DecoderAudioFormats;

int PackFileAsCDDA(cd::IsoWriter* writer, const std::filesystem::path& audioFile)
{
	// open the decoder
    ma_decoder decoder;	
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 2, 44100);	
	bool isLossy = false;
	long pcmBytesLeft;
	// determine which format to try based on file extension
    DecoderAudioFormats tryorder[4] = {DAF_WAV, DAF_FLAC, DAF_MP3, DAF_PCM};
	const auto& extension = audioFile.extension().u8string();
	if(extension.size() >= 4)
	{
		//nothing to change if wav
		if(CompareICase(extension.c_str(), ".flac") )
		{
			tryorder[0] = DAF_FLAC;
			tryorder[1] = DAF_WAV;
		}
		else if(CompareICase(extension.c_str(), ".mp3") )
		{
			tryorder[0] = DAF_MP3;
			tryorder[1] = DAF_WAV;
			tryorder[2] = DAF_FLAC;
		}
		else if((CompareICase(extension.c_str(), ".pcm") ) || (CompareICase(extension.c_str(), ".raw") ))
		{
			tryorder[0] = DAF_PCM;
			tryorder[1] = DAF_WAV;
			tryorder[2] = DAF_FLAC;
			tryorder[3] = DAF_MP3;
		}
	}
	VirtualWavEx vw;
	const int num_tries = std::size(tryorder);
	int i;
	for(i = 0; i < num_tries; i++)
	{
		if(tryorder[i] == DAF_WAV)
		{
	        decoderConfig.encodingFormat = ma_encoding_format_wav;
	        if(MA_SUCCESS == ma_decoder_init_path(audioFile, &decoderConfig, &decoder)) break;				
		}
        else if(tryorder[i] == DAF_FLAC)
		{
	        decoderConfig.encodingFormat = ma_encoding_format_flac;
	        if(MA_SUCCESS == ma_decoder_init_path(audioFile, &decoderConfig, &decoder)) break;
		}
		else if(tryorder[i] == DAF_MP3)
		{
	        decoderConfig.encodingFormat = ma_encoding_format_mp3;
	        if(MA_SUCCESS == ma_decoder_init_path(audioFile, &decoderConfig, &decoder))
	        {
	        	isLossy = true;
	        	break;
	        }
		}
		else if(tryorder[i] == DAF_PCM)
		{
			printf("\n    WARN: Guessing it's just signed 16 bit stereo @ 44100 kHz pcm audio\n");
			if(MA_SUCCESS == ma_decoder_init_path_pcm(audioFile, &decoderConfig, &decoder, &vw)) break;
		}
	}
	if(i == num_tries)
	{
		// no more formats to try, return false
	    printf("    ERROR: No valid format found\n");
	    return false;	
	}

    //  note if there's some data converting going on
    ma_format internalFormat;
	ma_uint32 internalChannels;
	ma_uint32 internalSampleRate; 
	if(MA_SUCCESS != ma_data_source_get_data_format(decoder.pBackend, &internalFormat, &internalChannels, &internalSampleRate))
	{
		printf("\n    ERROR: unable to get internal metadata for %" PRFILESYSTEM_PATH "\n", audioFile.c_str());
		ma_decoder_uninit(&decoder);
	    return false;
	} 
	if((internalFormat != ma_format_s16) || (internalChannels != 2) || (internalSampleRate != 44100) || isLossy)
	{
		printf("\n    WARN: This is not Redbook audio, converting.\n    ");			
	}

	// get expected pcm frame count (if your file isn't redbook this can vary from the input file's amount)
	// unfortunately it needs to decode the whole file to determine this for mp3
	const ma_uint64 expectedPCMFrames = ma_decoder_get_length_in_pcm_frames(&decoder);
    if(expectedPCMFrames == 0)
	{
		printf("\n    ERROR: corrupt file? unable to get_length_in_pcm_frames\n");
		ma_decoder_uninit(&decoder);
        return false;
	}
	
	// read a sector and write a sector until done
	const ma_uint64 framesToRead = (CD_SECTOR_SIZE/(2 * sizeof(int16_t))); // 2 channels 2 bytes per single channel sample (588 intrachannel pcm frames)
	ma_uint64 framesRead;
	ma_uint64 totalFramesProcessed = 0;
	unsigned char buff[CD_SECTOR_SIZE];
	do {
		memset(buff, 0x00, CD_SECTOR_SIZE);
		framesRead = ma_decoder_read_pcm_frames(&decoder, &buff, framesToRead);
		writer->WriteBytesRaw( buff, CD_SECTOR_SIZE );
		totalFramesProcessed += framesRead;
	} while(framesToRead == framesRead);
	ma_decoder_uninit(&decoder);
	if(totalFramesProcessed != expectedPCMFrames)
	{
		printf("\n    ERROR: corrupt file? (totalFramesProcessed != expectedPCMFrames)\n");
		return false;
	}
    return true;   
}
