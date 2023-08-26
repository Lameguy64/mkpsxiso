#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <string>
#include <queue>

#include "common.h"
#include "fs.h"
#include "cdwriter.h"	// CD image writer module
#include "iso.h"		// ISO file system generator module
#include "xml.h"
#include "platform.h"

#define MA_NO_THREADING
#define MA_NO_DEVICE_IO
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "miniaudio_pcm.h"
#include "miniaudio_helpers.h"


namespace global
{
	time_t		BuildTime;
	int			QuietMode	= false;
	int			Overwrite	= false;

	int			trackNum	= 1;
	int			noXA		= false;

	std::optional<std::string> volid_override;
	fs::path XMLscript;
	fs::path LBAfile;
	fs::path LBAheaderFile;
	fs::path ImageName;

	std::optional<fs::path> cuefile;
	int			NoIsoGen = false;
	fs::path RebuildXMLScript;

	tinyxml2::XMLDocument xmlIdFile;
};


bool ParseDirectory(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* parentElement, const fs::path& xmlPath, const EntryAttributes& parentAttribs, bool& found_da);
int ParseISOfileSystem(const tinyxml2::XMLElement* trackElement, const fs::path& xmlPath, iso::EntryList& entries, iso::IDENTIFIERS& isoIdentifiers, int& totalLen);

int PackFileAsCDDA(void* buffer, size_t bufSize, const fs::path& audioFile);

bool UpdateDAFilesWithLBA(iso::EntryList& entries, const char *trackid, const unsigned lba)
{
	for(auto& entry : entries)
	{
		if(entry.trackid != trackid) continue;
		if(entry.lba != iso::DA_FILE_PLACEHOLDER_LBA)
		{
			printf( "ERROR: Cannot replace entry.lba when it is not 0x%X\n ", iso::DA_FILE_PLACEHOLDER_LBA);
			return false;
		}
		entry.lba = lba;
		if ( !global::QuietMode )
		{
			std::string_view id(entry.id);
			printf("    DA File %s\n", std::string(id.substr(0, id.find_last_of(';'))).c_str());
		}
		return true;
	}

	printf( "ERROR: Did not find entry with trackid %s\n",  trackid);
	return false;
}

int Main(int argc, char* argv[])
{
	static constexpr const char* HELP_TEXT =
		"mkpsxiso [-h|--help] [-y] [-q|--quiet] [-o|--output <file>] [-lba <file>]\n"
		"  [-lbahead <file>] [-rebuildxml <file>] [-noisogen] <xml>\n\n"
		"  -y\t\tAlways overwrite ISO image files\n"
		"  -q|--quiet\tQuiet mode (suppress all but warnings and errors)\n"
		"  -o|--output\tSpecify output file (overrides image_name attribute)\n"
		"  -c|--cuefile\tSpecify cue sheet file (overrides cue_sheet attribute)\n"
		"  -l|--label\tSpecify volume ID (overrides volume element)\n"
		"  <xml>\t\tFile name of disc image project in XML document format\n\n"
		"  -lba\t\tGenerate a log of file LBA locations in disc image\n"
		"  -lbahead\tGenerate a C header of file LBA locations in disc image\n"
		"  -noisogen\tDo not generate ISO, but calculate file LBA locations only\n"
		"\t\t(for use with -lba or -lbahead)\n"
		"  -noxa\t\tDo not generate CD-XA extended file attributes (plain ISO9660)\n"
		"\t\t(XA data can still be included but not recommended)\n"
		"  -rebuildxml\tRebuild the XML using our newest schema\n"
		"  -h|--help\tShow this help text\n";

	static constexpr const char* VERSION_TEXT =
		"MKPSXISO " VERSION " - PlayStation ISO Image Maker\n"
		"2017-2022 Meido-Tek Productions (John \"Lameguy\" Wilbert Villamor/Lameguy64)\n"
		"2021-2022 Silent, Chromaryu, G4Vi, and spicyjpeg\n\n";

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
			if (auto output = ParsePathArgument(args, "c", "cuefile"); output.has_value())
			{
				global::cuefile = *output;
				OutputOverride = true;
				continue;
			}
			if (auto label = ParseStringArgument(args, "l", "label"); label.has_value())
			{
				global::volid_override = label;
				continue;
			}
			if (auto newxmlfile = ParsePathArgument(args, "rebuildxml"); newxmlfile.has_value())
			{
				global::RebuildXMLScript = *newxmlfile;
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
				global::XMLscript = fs::u8path(*args);
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

	// Fix XML tree to our current spec
	// convert DA file source syntax to DA file trackid syntax
	unsigned trackindex = 2;
	for(tinyxml2::XMLElement *modifyProject = xmlFile.FirstChildElement(xml::elem::ISO_PROJECT);
		modifyProject != nullptr;
		modifyProject = modifyProject->NextSiblingElement(xml::elem::ISO_PROJECT))
	{
		tinyxml2::XMLElement *modifyTrack = modifyProject->FirstChildElement(xml::elem::TRACK);
		if((modifyTrack == nullptr) || (!modifyTrack->Attribute(xml::attrib::TRACK_TYPE, "data")))
		{
			continue;
		}
		tinyxml2::XMLElement *dt =  modifyTrack->FirstChildElement(xml::elem::DIRECTORY_TREE);
		if(dt == nullptr)
		{
			continue;
		}
		std::queue<tinyxml2::XMLElement *> toscan({dt});
		while(!toscan.empty())
		{
			tinyxml2::XMLElement *scanElm = toscan.front();
			toscan.pop();
			if(CompareICase(scanElm->Name(), xml::elem::FILE))
			{
				if(scanElm->Attribute(xml::attrib::ENTRY_TYPE, "da"))
				{
					const char *trackid = scanElm->Attribute(xml::attrib::TRACK_ID);
					const char *source = scanElm->Attribute(xml::attrib::ENTRY_SOURCE);
					if((trackid != nullptr) && (source != nullptr))
					{
						printf( "ERROR: Cannot specify trackid and source at the same time\n ");
		                return EXIT_FAILURE;
					}
					if(source != nullptr)
					{
						char tid[3];
						snprintf(tid, sizeof(tid), "%02u", trackindex);
						std::string trackid = tid;
						trackindex++;

                        // add a new track
						tinyxml2::XMLElement *newtrack = xmlFile.NewElement(xml::elem::TRACK);
						newtrack->SetAttribute(xml::attrib::TRACK_TYPE, "audio");
						newtrack->SetAttribute(xml::attrib::TRACK_ID, trackid.c_str());
						newtrack->SetAttribute(xml::attrib::TRACK_SOURCE, source);
						// a 2 second pregap is assumed, don't write it
						/*tinyxml2::XMLElement *pregap = newtrack->InsertNewChildElement(xml::elem::TRACK_PREGAP);
						pregap->SetAttribute(xml::attrib::PREGAP_DURATION, "00:02:00");*/
						modifyProject->InsertAfterChild(modifyTrack, newtrack);
						modifyTrack = newtrack;

						// update the file to point to the track
						scanElm->DeleteAttribute(xml::attrib::ENTRY_SOURCE);
						scanElm->SetAttribute(xml::attrib::TRACK_ID, trackid.c_str());
					}
				}
				continue;
			}

			// add children to scan
			scanElm = scanElm->FirstChildElement();
			while(scanElm != nullptr)
			{
				toscan.push(scanElm);
				scanElm = scanElm->NextSiblingElement();
			}
		}
	}

	if(!global::RebuildXMLScript.empty())
	{
		if ( !global::QuietMode )
		{
			printf( "      Writing new XML ... " );
		}
		if (FILE* file = OpenFile(global::RebuildXMLScript, "w"); file != nullptr)
	    {
	    	xmlFile.SaveFile(file);
	    	fclose(file);
	    }
		else
		{
			printf( "ERROR: Cannot open %s for writing\n", 
				global::RebuildXMLScript.generic_u8string().c_str());
			return EXIT_FAILURE;
		}
		if ( !global::QuietMode )
		{
			printf("Ok.\n");
		}
	    return EXIT_SUCCESS;
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
			printf( "ERROR: -o or -c switch cannot be used in multi-disc ISO "
				"project.\n" );
			return EXIT_FAILURE;
		}

		// Check if image_name attribute is specified
		if ( global::ImageName.empty() )
		{
			if ( const char* image_name = projectElement->Attribute(xml::attrib::IMAGE_NAME); image_name != nullptr )
			{
				global::ImageName = fs::u8path(image_name);
			}
			else
			{
				// Use file name of XML project as the image file name
				global::ImageName = global::XMLscript.stem();
				global::ImageName += ".iso";
			}
		}

		if ( !global::cuefile )
		{
			if ( const char* cue_sheet = projectElement->Attribute(xml::attrib::CUE_SHEET); cue_sheet != nullptr )
			{
				global::cuefile = fs::u8path(cue_sheet);
			}
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
		if ( projectElement->FirstChildElement(xml::elem::TRACK) == nullptr )
		{
			printf( "ERROR: At least one <track> element must be specified.\n" );
			return EXIT_FAILURE;
		}

		// Check if cue_sheet attribute is specified
		unique_file cuefp;

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

				cuefp = OpenScopedFile( global::cuefile.value(), "w" );

				if ( cuefp == nullptr )
				{
					if ( !global::QuietMode )
					{
						printf( "  " );
					}

					printf( "ERROR: Unable to create cue sheet.\n" );

					return EXIT_FAILURE;
				}

				fprintf(cuefp.get(), "FILE \"%" PRFILESYSTEM_PATH "\" BINARY\n", global::ImageName.filename().c_str());
			}
		}

		global::trackNum = 1;
		iso::EntryList entries;
		iso::IDENTIFIERS isoIdentifiers {};
		int totalLenLBA = 0;

		std::vector<cdtrack> audioTracks;

		const tinyxml2::XMLElement* dataTrack = nullptr;

		// Parse tracks
		if ( !global::QuietMode )
		{
			printf("Scanning tracks...\n\n");
		}
		for ( const tinyxml2::XMLElement* trackElement = projectElement->FirstChildElement(xml::elem::TRACK);
			trackElement != nullptr; trackElement = trackElement->NextSiblingElement(xml::elem::TRACK) )
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
				dataTrack = trackElement;
				if ( global::trackNum != 1 )
				{
					if ( !global::QuietMode )
					{
						printf( "  " );
					}

					printf( "ERROR: Only the first track can be set as a "
						"data track on line: %d\n", trackElement->GetLineNum() );

					return EXIT_FAILURE;
				}

				if ( !ParseISOfileSystem( trackElement, global::XMLscript.parent_path(), entries, isoIdentifiers, totalLenLBA ) )
				{
					return EXIT_FAILURE;
				}

				if ( cuefp )
				{
					fprintf( cuefp.get(), "  TRACK %02d MODE2/2352\n", global::trackNum );
					fprintf( cuefp.get(), "    INDEX 01 00:00:00\n" );
				}

				if ( global::NoIsoGen )
				{
					printf( "Skipped generating ISO image.\n" );
					break;
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

					printf( "ERROR: %s attribute or -c parameter must be specified "
						"when using audio tracks.\n", xml::attrib::CUE_SHEET );

					return EXIT_FAILURE;
				}

				// Write track information to the CUE sheet
				if ( const char* trackRelativeSource = trackElement->Attribute(xml::attrib::TRACK_SOURCE); trackRelativeSource == nullptr )
				{
					if ( !global::QuietMode )
					{
						printf("    ");
					}

					printf( "ERROR: %s attribute not specified "
						"for track on line %d.\n", xml::attrib::TRACK_SOURCE, trackElement->GetLineNum() );

					return EXIT_FAILURE;
				}
				else
				{
					fs::path trackSource = (global::XMLscript.parent_path() / trackRelativeSource);
					fprintf( cuefp.get(), "  TRACK %02d AUDIO\n", global::trackNum );

					// pregap
					int pregapSectors = 150; // by default 2 seconds
					const tinyxml2::XMLElement *pregapElement = trackElement->FirstChildElement(xml::elem::TRACK_PREGAP);
					if(pregapElement != nullptr)
					{
						const char *duration = pregapElement->Attribute(xml::attrib::PREGAP_DURATION);
						if(duration != nullptr)
						{
							unsigned minutes, seconds, frames;
							if(
								(sscanf(duration, "%u:%u:%u", &minutes, &seconds, &frames) != 3) ||
								(seconds > 59) || (frames > 74)
							)
							{
								printf( "ERROR: %s duration is invalid MM:SS:FF"
									"for track on line %d.\n", xml::attrib::TRACK_SOURCE, pregapElement->GetLineNum() );
								return EXIT_FAILURE;
							}

							if(((((minutes * 60) + seconds) * 75) + frames) > (80 * 60 * 75))
							{
								printf( "WARNING: duration > 80 minutes\n");
							}
							pregapSectors = (((minutes * 60)+seconds)*75)+frames;
						}
					}
					if(pregapSectors > 0)
					{
						fprintf( cuefp.get(), "    INDEX 00 %s\n", SectorsToTimecode(totalLenLBA).c_str());

						audioTracks.emplace_back(totalLenLBA, pregapSectors * CD_SECTOR_SIZE);
						totalLenLBA += pregapSectors;
					}

					fprintf( cuefp.get(), "    INDEX 01 %s\n", SectorsToTimecode(totalLenLBA).c_str());

					const char *trackid = trackElement->Attribute(xml::attrib::TRACK_ID);
					if(trackid != nullptr)
					{
						if(!UpdateDAFilesWithLBA(entries, trackid, totalLenLBA))
						{
							return EXIT_FAILURE;
						}
					}

					const unsigned int audioSize = iso::DirTreeClass::GetAudioSize(trackSource);
					audioTracks.emplace_back(totalLenLBA, audioSize, trackSource.generic_u8string());

					totalLenLBA += audioSize/CD_SECTOR_SIZE;
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

				return EXIT_FAILURE;
			}

			global::trackNum++;
		}

		iso::DIRENTRY& root = entries.front();
	    iso::DirTreeClass* dirTree = root.subdir.get();

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
					printf( "Wrote file LBA log %" PRFILESYSTEM_PATH ".\n\n",
						global::LBAfile.lexically_normal().c_str() );
				}
			}
			else
			{
				if ( !global::QuietMode )
				{
					printf( "Failed to write LBA log %" PRFILESYSTEM_PATH "!\n\n",
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
					printf( "Wrote file LBA listing header %" PRFILESYSTEM_PATH ".\n\n",
						global::LBAheaderFile.lexically_normal().c_str() );
				}
			}
			else
			{
				if ( !global::QuietMode )
				{
					printf( "Failed to write LBA listing header %" PRFILESYSTEM_PATH ".\n\n",
						global::LBAheaderFile.lexically_normal().c_str() );
				}
			}
		}

		if ( !global::NoIsoGen )
		{
			// Create ISO image for writing
			cd::IsoWriter writer;

			if ( !writer.Create(global::ImageName, totalLenLBA ) ) {

				if ( !global::QuietMode )
				{
					printf( "  " );
				}

				printf( "ERROR: Cannot open or create output image file.\n" );
				return EXIT_FAILURE;

			}


			// Write the file system
			if ( !global::QuietMode )
			{
				printf("Writing ISO\n");
				printf( "    Writing files...\n" );
			}

			// Copy the files into the disc image
			iso::DIRENTRY& root = entries.front();
			iso::DirTreeClass* dirTree = root.subdir.get();
			dirTree->WriteFiles( &writer );

			if ( !global::QuietMode )
			{
				printf("\n");
				printf("    Writing CDDA tracks...\n");
			}

			// Write out the audio tracks
			for (const cdtrack& track : audioTracks)
			{
				const uint32_t sizeInSectors = GetSizeInSectors(track.size, CD_SECTOR_SIZE);
				auto sectorView = writer.GetRawSectorView(track.lba, sizeInSectors);

				if (!track.source.empty())
				{
					// Pack the audio file
					if ( !global::QuietMode )
					{
						printf( "      Packing audio %s... ", track.source.c_str() );
					}

					if ( PackFileAsCDDA( sectorView->GetRawBuffer(), track.size, fs::u8path(track.source) ) )
					{
						if ( !global::QuietMode )
						{
							printf( "Done.\n" );
						}
					}
				}
				else
				{
					// Write pregap
					sectorView->WriteBlankSectors();
				}
			}

			if ( !global::QuietMode )
			{
				printf( "\n" );
			}
						

			// Write license data
			const tinyxml2::XMLElement* licenseElement = dataTrack->FirstChildElement(xml::elem::LICENSE);
			if ( licenseElement != nullptr )
			{
				FILE* fp = OpenFile( global::XMLscript.parent_path() / fs::u8path(licenseElement->Attribute(xml::attrib::LICENSE_FILE)), "rb" );
				if (fp != nullptr)
				{
					auto license = std::make_unique<cd::ISO_LICENSE>();
					if (fread( license->data, sizeof(license->data), 1, fp ) == 1)
					{
						if ( !global::QuietMode )
						{
							printf( "    Writing license data..." );
						}

						iso::WriteLicenseData( &writer, license->data );

						if ( !global::QuietMode )
						{
							printf( "Ok.\n" );
						}
					}
					fclose( fp );
				}
			}
			else
			{
				// Write blank sectors if no license data is to be injected
				auto appBlankSectors = 
					writer.GetSectorViewM2F1(0, 16, cd::IsoWriter::EdcEccForm::Form2);
				appBlankSectors->WriteBlankSectors(16);
			}

			// Write file system
			if ( !global::QuietMode )
			{
				printf( "    Writing directories... " );
			}

			// Sort directory entries and write it
			dirTree->SortDirectoryEntries();
			dirTree->WriteDirectoryRecords( &writer, root, root );

			// Write file system descriptors to finish the image
	        iso::WriteDescriptor( &writer, isoIdentifiers, root, totalLenLBA );

			if ( !global::QuietMode )
			{
				printf( "Ok.\n" );
			}

			// Close both ISO writer and CUE sheet
			writer.Close();
			cuefp.reset();

			if ( !global::QuietMode )
			{
				printf( "ISO image generated successfully.\n" );
				printf( "Total image size: %d bytes (%d sectors)\n",
					(CD_SECTOR_SIZE*totalLenLBA), totalLenLBA );
			}
		}

		// Check for next <iso_project> element
		projectElement = projectElement->NextSiblingElement(xml::elem::ISO_PROJECT);

	}

    return 0;
}

EntryAttributes ReadEntryAttributes(EntryAttributes current, const tinyxml2::XMLElement* dirElement)
{
	if (dirElement != nullptr)
	{
		auto getAttributeIfExists = [dirElement](auto& value, const char* name)
		{
			using type = std::decay_t<decltype(value)>;
			if constexpr (std::is_unsigned_v<type>)
			{
				value = static_cast<type>(dirElement->UnsignedAttribute(name, value));
			}
			else
			{
				value = static_cast<type>(dirElement->IntAttribute(name, value));
			}
		};

		getAttributeIfExists(current.GMTOffs, xml::attrib::GMT_OFFSET);
		getAttributeIfExists(current.XAAttrib, xml::attrib::XA_ATTRIBUTES);
		getAttributeIfExists(current.XAPerm, xml::attrib::XA_PERMISSIONS);
		getAttributeIfExists(current.GID, xml::attrib::XA_GID);
		getAttributeIfExists(current.UID, xml::attrib::XA_UID);
		getAttributeIfExists(current.FLBA, xml::attrib::OFFSET);
	}

	return current;
};

int ParseISOfileSystem(const tinyxml2::XMLElement* trackElement, const fs::path& xmlPath, iso::EntryList& entries, iso::IDENTIFIERS& isoIdentifiers, int& totalLen)
{
	const tinyxml2::XMLElement* identifierElement =
		trackElement->FirstChildElement(xml::elem::IDENTIFIERS);
	const tinyxml2::XMLElement* licenseElement =
		trackElement->FirstChildElement(xml::elem::LICENSE);

	// Set file system identifiers

	if ( identifierElement != nullptr )
	{
		const char* identifierFile;
		
		// Otherwise use individual elements defined by each attribute
		isoIdentifiers.SystemID		= identifierElement->Attribute(xml::attrib::SYSTEM_ID);
		isoIdentifiers.VolumeID		= identifierElement->Attribute(xml::attrib::VOLUME_ID);
		isoIdentifiers.VolumeSet	= identifierElement->Attribute(xml::attrib::VOLUME_SET);
		isoIdentifiers.Publisher	= identifierElement->Attribute(xml::attrib::PUBLISHER);
		isoIdentifiers.Application	= identifierElement->Attribute(xml::attrib::APPLICATION);
		isoIdentifiers.DataPreparer	= identifierElement->Attribute(xml::attrib::DATA_PREPARER);
		isoIdentifiers.Copyright	= identifierElement->Attribute(xml::attrib::COPYRIGHT);
		isoIdentifiers.CreationDate	= identifierElement->Attribute(xml::attrib::CREATION_DATE);
		isoIdentifiers.ModificationDate = identifierElement->Attribute(xml::attrib::MODIFICATION_DATE);

		// Is an ID file specified?
		if( identifierFile = identifierElement->Attribute(xml::attrib::ID_FILE) )
		{
			// Load the file as an XML document
			{
				tinyxml2::XMLError error;
				if (FILE* file = OpenFile(identifierFile, "rb"); file != nullptr)
				{
					error = global::xmlIdFile.LoadFile(file);
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
						printf("%s on line %d\n", global::xmlIdFile.ErrorName(), global::xmlIdFile.ErrorLineNum());
					}
					return false;
				}
			}
			
			// Get the identifier element, if there is one
			if( identifierElement = global::xmlIdFile.FirstChildElement(xml::elem::IDENTIFIERS) )
			{
				const char *str;
				// Use strings defined in file, otherwise leave ones already defined alone
				if( str = identifierElement->Attribute(xml::attrib::SYSTEM_ID) )
					isoIdentifiers.SystemID			= str;
				if( str = identifierElement->Attribute(xml::attrib::VOLUME_ID) )
					isoIdentifiers.VolumeID			= str;
				if( str = identifierElement->Attribute(xml::attrib::VOLUME_SET) )
					isoIdentifiers.VolumeSet		= str;
				if( str = identifierElement->Attribute(xml::attrib::PUBLISHER) )
					isoIdentifiers.Publisher		= str;
				if( str = identifierElement->Attribute(xml::attrib::APPLICATION) )
					isoIdentifiers.Application		= str;
				if( str = identifierElement->Attribute(xml::attrib::DATA_PREPARER) )
					isoIdentifiers.DataPreparer		= str;
				if( str = identifierElement->Attribute(xml::attrib::COPYRIGHT) )
					isoIdentifiers.Copyright		= str;
				if( str = identifierElement->Attribute(xml::attrib::CREATION_DATE) )
					isoIdentifiers.CreationDate		= str;
				if( str = identifierElement->Attribute(xml::attrib::MODIFICATION_DATE) )
					isoIdentifiers.ModificationDate = str;
			}
		}

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

		if( global::volid_override )
		{
			isoIdentifiers.VolumeID = global::volid_override->c_str();
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
			const fs::path license_file{xmlPath / fs::u8path(license_file_attrib)};
			if ( license_file.empty() )
			{
				if ( !global::QuietMode )
				{
					printf( "    " );
				}

				printf("ERROR: File attribute of <license> element is missing "
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
	const EntryAttributes defaultAttributes = ReadEntryAttributes(EntryAttributes{}, trackElement->FirstChildElement(xml::elem::DEFAULT_ATTRIBUTES));
	if ( !ParseDirectory(dirTree, directoryTree, xmlPath, defaultAttributes, found_da) )
	{
		return false;
	}

	// Calculate directory tree LBAs and retrieve size of image
	int pathTableLen = dirTree->CalculatePathTableLen(root);

	// 16 license sectors + 2 header sectors
	const int rootLBA = 18+(GetSizeInSectors(pathTableLen)*4);
	totalLen = dirTree->CalculateTreeLBA(rootLBA);

	if ( !global::QuietMode )
	{
		printf( "      Files Total: %d\n", dirTree->GetFileCountTotal() );
		printf( "      Directories: %d\n", dirTree->GetDirCountTotal() );
		printf( "      Total file system size: %d bytes (%d sectors)\n\n",
			CD_SECTOR_SIZE*totalLen, totalLen);
	}

	return true;
}

static bool ParseFileEntry(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* dirElement, const fs::path& xmlPath, const EntryAttributes& defaultAttributes, bool& found_da)
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

	fs::path srcFile;
	if ( sourceElement != nullptr )
	{
		srcFile = fs::u8path(sourceElement);
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
	const char *trackid = nullptr;

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
			trackid = dirElement->Attribute(xml::attrib::TRACK_ID);
			if ( trackid == nullptr )
			{
				printf( "ERROR: DA audio file(s) does not have an associated CDDA track [trackid]\n" );
				return false;
			}
			// locate the node containing the tracks
			const tinyxml2::XMLElement *isoElement;
			for( const tinyxml2::XMLElement *parent = (tinyxml2::XMLElement *)dirElement->Parent(); ; parent = (tinyxml2::XMLElement *)parent->Parent())
			{
				if(parent == nullptr)
				{
					printf( "ERROR: locating <%s> elem, necessary for locating corresponding track to da file\n", xml::elem::ISO_PROJECT );
					return false;
				}
				if(CompareICase(parent->Name(), xml::elem::ISO_PROJECT))
				{
					isoElement = parent;
					break;
				}
			}
			// locate the track with trackid
			const tinyxml2::XMLElement *trackElement;
			for(trackElement = isoElement->FirstChildElement(xml::elem::TRACK); ; trackElement = trackElement->NextSiblingElement(xml::elem::TRACK))
			{
				if(trackElement == nullptr)
				{
					printf( "ERROR: locating <%s %s=\"audio\" %s=\"%s\"> for da file\n", xml::elem::TRACK, xml::attrib::TRACK_TYPE, xml::attrib::TRACK_ID, trackid);
					return false;
				}
				if(trackElement->Attribute(xml::attrib::TRACK_TYPE, "audio") && trackElement->Attribute(xml::attrib::TRACK_ID, trackid))
				{
					break;
				}
			}
			// set the src file to the trackid source
			sourceElement = trackElement->Attribute(xml::attrib::TRACK_SOURCE);
			if(sourceElement == nullptr)
			{
				printf( "ERROR: <%s %s=\"audio\" %s=\"%s\"> must have source\n", xml::elem::TRACK, xml::attrib::TRACK_TYPE, xml::attrib::TRACK_ID, trackid);
				return false;
			}
			srcFile = fs::u8path(sourceElement);
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

		//if ( found_da && entry != EntryType::EntryDA )
		//{
		//	if ( !global::QuietMode )
		//	{
		//		printf( "      " );
		//	}
//
		//	printf( "ERROR: Cannot place file past a DA audio file on line %d.\n",
		//		dirElement->GetLineNum() );
//
		//	return false;
		//}

	}

	return dirTree->AddFileEntry(name.c_str(), entry, xmlPath / srcFile, ReadEntryAttributes(defaultAttributes, dirElement), trackid);
}

static bool ParseDummyEntry(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* dirElement, const bool found_da)
{
	//if ( found_da )
	//{
	//	if ( !global::QuietMode )
	//	{
	//		printf( "      " );
	//	}
//
	//	printf( "ERROR: Cannot place dummy past a DA audio file on line %d.\n",
	//		dirElement->GetLineNum() );
//
	//	return false;
	//}
//
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

static bool ParseDirEntry(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* dirElement, const fs::path& xmlPath, const EntryAttributes& defaultAttributes, bool& found_da)
{
	const char* nameElement = dirElement->Attribute(xml::attrib::ENTRY_NAME);
	if ( strlen( nameElement ) > 12 )
	{
		printf( "ERROR: Directory name %s on line %d is more than 12 "
			"characters long.\n", nameElement,
				dirElement->GetLineNum() );
		return false;
	}

	fs::path srcDir;
	if (const char* sourceElement = dirElement->Attribute(xml::attrib::ENTRY_SOURCE); sourceElement != nullptr)
	{
		srcDir = xmlPath / fs::u8path(sourceElement);
	}

	bool alreadyExists = false;
	iso::DirTreeClass* subdir = dirTree->AddSubDirEntry(
		nameElement, srcDir, ReadEntryAttributes(defaultAttributes, dirElement), alreadyExists );

	if ( subdir == nullptr )
	{
		return false;
	}

	//if ( found_da && !alreadyExists )
	//{
	//	if ( !global::QuietMode )
	//	{
	//		printf( "      " );
	//	}
//
	//	printf( "ERROR: Cannot place directory past a DA audio file on line %d\n",
	//		dirElement->GetLineNum() );
//
	//	return false;
	//}

	return ParseDirectory(subdir, dirElement, xmlPath, defaultAttributes, found_da);
}

bool ParseDirectory(iso::DirTreeClass* dirTree, const tinyxml2::XMLElement* parentElement, const fs::path& xmlPath, const EntryAttributes& defaultAttributes, bool& found_da)
{
	for ( const tinyxml2::XMLElement* dirElement = parentElement->FirstChildElement(); dirElement != nullptr; dirElement = dirElement->NextSiblingElement() )
	{
		
        if ( CompareICase( "file", dirElement->Name() ))
		{
			if (!ParseFileEntry(dirTree, dirElement, xmlPath, defaultAttributes, found_da))
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
			if (!ParseDirEntry(dirTree, dirElement, xmlPath, defaultAttributes, found_da))
			{
				return false;
			}
        }
	}

	return true;
}

int PackFileAsCDDA(void* buffer, size_t bufSize, const fs::path& audioFile)
{
	// open the decoder
    ma_decoder decoder;
	VirtualWavEx vw;
	bool isLossy;
	if(ma_redbook_decoder_init_path_by_ext(audioFile, &decoder, &vw, isLossy) != MA_SUCCESS)
	{
		return 0;
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

	ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder, buffer, expectedPCMFrames);
	ma_decoder_uninit(&decoder);

	if(framesRead != expectedPCMFrames)
	{
		printf("\n    ERROR: corrupt file? (framesRead != expectedPCMFrames)\n");
		return false;
	}
    return true;
}
