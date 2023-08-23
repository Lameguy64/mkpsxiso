#include <stdio.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#else
#include <unistd.h>
#endif


#include <string>
#include <vector>
#include <map>
#include <memory>

#include "platform.h"
#include "common.h"
#include "fs.h"
#include "cd.h"
#include "xa.h"
#include "cdreader.h"
#include "xml.h"

#ifndef MKPSXISO_NO_LIBFLAC
#include "FLAC/stream_encoder.h"
#endif

#include <time.h>

typedef enum {
	EAF_WAV  = 1 << 0,
	EAF_FLAC = 1 << 1,
	EAF_PCM  = 1 << 2
} EncoderAudioFormats;

typedef struct {
	const char *codec;
	EncoderAudioFormats eaf;
	const char *notcompiledmessage;
} EncodingCodec;

static const EncodingCodec EncodingCodecs[] = {
	{"wave", EAF_WAV, ""},
	{"flac", EAF_FLAC, "ERROR: dumpsxiso was NOT built with libFLAC support!\n"},
	{"pcm",  EAF_PCM, ""}
};

const unsigned BUILTIN_CODECS = (EAF_WAV | EAF_PCM);
#define BUILTIN_CODEC_TEXT "wave, pcm"
#ifndef MKPSXISO_NO_LIBFLAC
	#define LIBFLAC_SUPPORTED EAF_FLAC
	#define LIBFLAC_CODEC_TEXT ", flac"
#else
    #define LIBFLAC_SUPPORTED 0
	#define LIBFLAC_CODEC_TEXT ""
#endif
const unsigned SUPPORTED_CODECS  = (BUILTIN_CODECS | LIBFLAC_SUPPORTED);
#define SUPPORTED_CODEC_TEXT BUILTIN_CODEC_TEXT LIBFLAC_CODEC_TEXT

namespace param {

    fs::path isoFile;
    fs::path outPath;
    fs::path xmlFile;
    bool outputSortedByDir = false;
		bool pathTable = false;
	EncoderAudioFormats encodingFormat = EAF_WAV;
}

fs::path GetRealDAFilePath(const fs::path& inputPath)
{
	fs::path outputPath(inputPath); 
	if(param::encodingFormat == EAF_WAV)
	{
		outputPath.replace_extension(".WAV");
	}
	else if(param::encodingFormat == EAF_FLAC)
	{
		outputPath.replace_extension(".FLAC");
	}
	else if(param::encodingFormat == EAF_PCM)
	{
		outputPath.replace_extension(".PCM");
	}
	else
	{
		printf("ERROR: support for encoding format is not implemented, not changing name\n");
		return inputPath;
	}
	return outputPath;
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
    const fs::path outputPath = param::outPath / "license_data.dat";

	FILE* outFile = OpenFile(outputPath, "wb");

    if (outFile == NULL) {
		printf("ERROR: Cannot create license file %" PRFILESYSTEM_PATH "...", outputPath.lexically_normal().c_str());
        return;
    }

    fwrite(license.data, 1, sizeof(license.data), outFile);
    fclose(outFile);
}

void writePCMFile(FILE *outFile, cd::IsoReader& reader, const size_t cddaSize, const int isInvalid)
{
	int bytesLeft = cddaSize;
	while (bytesLeft > 0) {

		u_char copyBuff[2352]{};

    	int bytesToRead = bytesLeft;

    	if (bytesToRead > 2352)
    		bytesToRead = 2352;

    	if (!isInvalid)
    		reader.ReadBytesDA(copyBuff, bytesToRead);

    	fwrite(copyBuff, 1, bytesToRead, outFile);

    	bytesLeft -= bytesToRead;
    }
}

void writeWaveFile(FILE *outFile, cd::IsoReader& reader, const size_t cddaSize, const int isInvalid)
{
    cd::RIFF_HEADER riffHeader;
    prepareRIFFHeader(&riffHeader, cddaSize);
    fwrite((void*)&riffHeader, 1, sizeof(cd::RIFF_HEADER), outFile);

    writePCMFile(outFile, reader, cddaSize, isInvalid);
}

#ifndef MKPSXISO_NO_LIBFLAC
void writeFLACFile(FILE *outFile, cd::IsoReader& reader, const int cddaSize, const int isInvalid)
{
	FLAC__bool ok = true;
	FLAC__StreamEncoder *encoder = 0;
	FLAC__StreamEncoderInitStatus init_status;
	if((encoder = FLAC__stream_encoder_new()) == NULL)
	{
		fprintf(stderr, "ERROR: allocating encoder\n");
		return;
	}
	unsigned sample_rate = 44100;
	unsigned channels = 2;
	unsigned bps = 16;
	unsigned total_samples = cddaSize / (channels * (bps/8));

	ok &= FLAC__stream_encoder_set_verify(encoder, true);
	ok &= FLAC__stream_encoder_set_compression_level(encoder, 5);
	ok &= FLAC__stream_encoder_set_channels(encoder, channels);
	ok &= FLAC__stream_encoder_set_bits_per_sample(encoder, bps);
	ok &= FLAC__stream_encoder_set_sample_rate(encoder, sample_rate);
	ok &= FLAC__stream_encoder_set_total_samples_estimate(encoder, total_samples);
	if(!ok)
	{
		fprintf(stderr, "ERROR: setting encoder settings\n");
		goto writeFLACFile_cleanup;
	}

	init_status = FLAC__stream_encoder_init_FILE(encoder, outFile, /*progress_callback=*/NULL, /*client_data=*/NULL);
	if(init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
	{
		fprintf(stderr, "ERROR: initializing encoder: %s\n", FLAC__StreamEncoderInitStatusString[init_status]);
		goto writeFLACFile_cleanup;
	}

    {
    size_t left = (size_t)total_samples;
	size_t max_pcmframe_read = 2352 / (channels * (bps/8));

    std::unique_ptr<int32_t[]> pcm(new int32_t[channels * max_pcmframe_read]);
	while (left && ok) {

		u_char copyBuff[2352]{};

		size_t need = (left > max_pcmframe_read ? max_pcmframe_read : left);
		size_t needBytes = need * (channels * (bps/8));
		if(!isInvalid)
			reader.ReadBytesDA(copyBuff, needBytes);

    	/* convert the packed little-endian 16-bit PCM samples from WAVE into an interleaved FLAC__int32 buffer for libFLAC */
		for(size_t i = 0; i < need*channels; i++)
		{
			/* inefficient but simple and works on big- or little-endian machines */
			pcm[i] = (FLAC__int32)(((FLAC__int16)(FLAC__int8)copyBuff[2*i+1] << 8) | (FLAC__int16)copyBuff[2*i]);
		}
		/* feed samples to encoder */
		ok = FLAC__stream_encoder_process_interleaved(encoder, pcm.get(), need);
		left -= need;
    }
    }
	ok &= FLAC__stream_encoder_finish(encoder); // closes outFile
	if(!ok)
	{
		fprintf(stderr, "encoding: %s\n", ok? "succeeded" : "FAILED");
		fprintf(stderr, "   state: %s\n", FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(encoder)]);
	}

writeFLACFile_cleanup:
	FLAC__stream_encoder_delete(encoder);
}
#endif

// XML attribute stuff
struct EntryAttributeCounters
{
	std::map<int, unsigned int> GMTOffs;
	std::map<int, unsigned int> XAAttrib;
	std::map<int, unsigned int> XAPerm;
	std::map<int, unsigned int> GID;
	std::map<int, unsigned int> UID;
};

static void WriteOptionalXMLAttribs(tinyxml2::XMLElement* element, const cd::IsoDirEntries::Entry& entry, EntryType type, EntryAttributeCounters& attributeCounters)
{
	element->SetAttribute(xml::attrib::GMT_OFFSET, entry.entry.entryDate.GMToffs);
	++attributeCounters.GMTOffs[entry.entry.entryDate.GMToffs];

	// xa_attrib only makes sense on XA files
	if (type == EntryType::EntryXA)
	{
		const auto XAAtrib = (entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8;
		element->SetAttribute(xml::attrib::XA_ATTRIBUTES, XAAtrib);
		++attributeCounters.XAAttrib[XAAtrib];
	}
	const auto XAPerm = entry.extData.attributes & cdxa::XA_PERMISSIONS_MASK;
	element->SetAttribute(xml::attrib::XA_PERMISSIONS, XAPerm);
	++attributeCounters.XAPerm[XAPerm];

	element->SetAttribute(xml::attrib::XA_GID, entry.extData.ownergroupid);
	element->SetAttribute(xml::attrib::XA_UID, entry.extData.owneruserid);
	++attributeCounters.GID[entry.extData.ownergroupid];
	++attributeCounters.UID[entry.extData.owneruserid];
}

static EntryAttributes EstablishXMLAttributeDefaults(tinyxml2::XMLElement* defaultAttributesElement, const EntryAttributeCounters& attributeCounters)
{
	// First establish "defaults" - that is, the most commonly occurring attributes
	auto findMaxElement = [](const auto& map)
	{
		return std::max_element(map.begin(), map.end(), [](const auto& left, const auto& right) { return left.second < right.second; })->first;
	};

	EntryAttributes defaultAttributes;
	defaultAttributes.GMTOffs = static_cast<signed char>(findMaxElement(attributeCounters.GMTOffs));
	defaultAttributes.XAAttrib = static_cast<unsigned char>(findMaxElement(attributeCounters.XAAttrib));
	defaultAttributes.XAPerm = static_cast<unsigned short>(findMaxElement(attributeCounters.XAPerm));
	defaultAttributes.GID = static_cast<unsigned short>(findMaxElement(attributeCounters.GID));
	defaultAttributes.UID = static_cast<unsigned short>(findMaxElement(attributeCounters.UID));

	// Write them out to the XML
	defaultAttributesElement->SetAttribute(xml::attrib::GMT_OFFSET, defaultAttributes.GMTOffs);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_ATTRIBUTES, defaultAttributes.XAAttrib);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_PERMISSIONS, defaultAttributes.XAPerm);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_GID, defaultAttributes.GID);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_UID, defaultAttributes.UID);

	return defaultAttributes;
}

static void SimplifyDefaultXMLAttributes(tinyxml2::XMLElement* element, const EntryAttributes& defaults)
{
	// DeleteAttribute can be safely called even if that attribute doesn't exist, so treating failure and default values
	// as equal simplifies logic
	auto deleteAttribute = [element](const char* name, auto defaultValue)
	{
		bool deleteAttribute = false;
		if constexpr (std::is_unsigned_v<decltype(defaultValue)>)
		{
			deleteAttribute = element->UnsignedAttribute(name, defaultValue) == defaultValue;
		}
		else
		{
			deleteAttribute = element->IntAttribute(name, defaultValue) == defaultValue;
		}
		if (deleteAttribute)
		{
			element->DeleteAttribute(name);
		}
	};

	deleteAttribute(xml::attrib::GMT_OFFSET, defaults.GMTOffs);
	deleteAttribute(xml::attrib::XA_ATTRIBUTES, defaults.XAAttrib);
	deleteAttribute(xml::attrib::XA_PERMISSIONS, defaults.XAPerm);
	deleteAttribute(xml::attrib::XA_GID, defaults.GID);
	deleteAttribute(xml::attrib::XA_UID, defaults.UID);

	for (tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr; child = child->NextSiblingElement())
	{
		SimplifyDefaultXMLAttributes(child, defaults);
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
	const fs::path& path)
{
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));
    dirEntries->ReadDirEntries(&reader, offs, sectors, false);

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

std::unique_ptr<cd::IsoDirEntries> ParsePathTable(cd::IsoReader& reader, ListView<cd::IsoDirEntries::Entry> view, std::vector<cd::IsoPathTable::Entry>& pathTableList, std::vector<cd::IsoPathTable::Entry>& sorted, int index,
   const fs::path& path) {
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));

		int sec = std::max(1, (int)pathTableList[index].entry.extLength);

		int sindex = -1;
		for (int i = 1; i < pathTableList.size(); i++) {
				if (sorted[i].name == pathTableList[index].name && sorted[i].entry.parentDirIndex == pathTableList[index].entry.parentDirIndex) {
						sindex = i;
						break;
				}
		}
	
		do {
			  dirEntries->ReadDirEntries(&reader, pathTableList[index].entry.dirOffs, sec, true);
				if (dirEntries->dirEntryList.GetView().size() == 0) {
					break;
				}
		
				auto& entry = dirEntries->dirEntryList.GetView().back().get();							

				const bool isDA = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8) == EntryType::EntryDA;

				if(!isDA && sindex >= 0 && sindex + 1 < pathTableList.size() && pathTableList[index].entry.extLength == 0)
				{
					if (sorted[sindex + 1].entry.dirOffs > entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb))
					{
							sec++;
							dirEntries->dirEntryList.ClearView();
							continue;
					}
				}

				break;
		} while (true);
  
    for (int i = 1; i < pathTableList.size(); i++) {
        auto& e = pathTableList[i];
        if (e.entry.parentDirIndex - 1 == index) {
            dirEntries->ReadRootDir(&reader, e.entry.dirOffs);
        }
    } 

    for (auto& e : dirEntries->dirEntryList.GetView()) {
    		auto& entry = e.get();
										
    		entry.virtualPath = path;

        if (entry.entry.flags & 0x2) {
						int index = -1;
						std::string s;
						for (int i = 1; i < pathTableList.size(); i++) {
								auto& ee = pathTableList[i];
								if (ee.entry.dirOffs == entry.entry.entryOffs.lsb) {
										index = i;
										s = ee.name;
										break;
								}
						}

						if (index < 0) continue;
						entry.identifier = s;
				
						entry.subdir = ParsePathTable(reader, dirEntries->dirEntryList.NewView(), pathTableList, sorted, index, path / s);				
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

std::unique_ptr<cd::IsoDirEntries> ParseRootPathTable(cd::IsoReader& reader, ListView<cd::IsoDirEntries::Entry> view, std::vector<cd::IsoPathTable::Entry>& pathTableList, std::vector<cd::IsoPathTable::Entry>& sorted)
{ 
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));
    dirEntries->ReadRootDir(&reader, pathTableList[0].entry.dirOffs);

  	auto& entry = dirEntries->dirEntryList.GetView().front().get();

    entry.subdir = ParsePathTable(reader, dirEntries->dirEntryList.NewView(), pathTableList, sorted, 0,
    CleanIdentifier(entry.identifier));
    
  	return dirEntries;
}

void ExtractFiles(cd::IsoReader& reader, const std::list<cd::IsoDirEntries::Entry>& files, const fs::path& rootPath)
{
    for (const auto& entry : files)
	{
		const fs::path outputPath = rootPath / entry.virtualPath / CleanIdentifier(entry.identifier);
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

                auto daOutPath = GetRealDAFilePath(outputPath);
				auto outFile = OpenScopedFile(daOutPath, "wb");

				if (!outFile) {
					printf("ERROR: Cannot create file %" PRFILESYSTEM_PATH "...", daOutPath.lexically_normal().c_str());
					return;
				}

				size_t sectorsToRead = GetSizeInSectors(entry.entry.entrySize.lsb);
				size_t cddaSize = 2352 * sectorsToRead;

				if(param::encodingFormat == EAF_WAV)
				{
					writeWaveFile(outFile.get(), reader, cddaSize, result);
				}
#ifndef MKPSXISO_NO_LIBFLAC
				else if(param::encodingFormat == EAF_FLAC)
				{
					// libflac closes outFile
					writeFLACFile(outFile.release(), reader, cddaSize, result);
				}
#endif
				else if(param::encodingFormat == EAF_PCM)
				{
					writePCMFile(outFile.get(), reader, cddaSize, result);
				}
				else
				{
					printf("ERROR: support for encoding format is not implemented\n");
					return;
				}
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
		fs::path toChange(rootPath / entry.virtualPath / CleanIdentifier(entry.identifier));
		const EntryType type = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8);
		if(type == EntryType::EntryDA)
		{
			toChange = GetRealDAFilePath(toChange);
		}
		UpdateTimestamps(toChange, entry.entry.entryDate);
	}
}

tinyxml2::XMLElement* WriteXMLEntry(const cd::IsoDirEntries::Entry& entry, tinyxml2::XMLElement* dirElement, fs::path* currentVirtualPath,
	const fs::path& sourcePath, const std::string& trackid, EntryAttributeCounters& attributeCounters)
{
	tinyxml2::XMLElement* newelement;

	const fs::path outputPath = sourcePath / entry.virtualPath / CleanIdentifier(entry.identifier);
	const EntryType entryType = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8);
	if (entryType == EntryType::EntryDir)
	{
		if (!entry.identifier.empty())
		{
			newelement = dirElement->InsertNewChildElement("dir");
			newelement->SetAttribute(xml::attrib::ENTRY_NAME, entry.identifier.c_str());
			newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.lexically_normal().generic_u8string().c_str());
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
		if(entryType != EntryType::EntryDA)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.lexically_normal().generic_u8string().c_str());
		}
		else
		{
			newelement->SetAttribute(xml::attrib::TRACK_ID, trackid.c_str());
		}

		if (entryType == EntryType::EntryXA)
		{
			if (param::pathTable) {
				 newelement->SetAttribute(xml::attrib::OFFSET, entry.entry.entryOffs.lsb);
			}

			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "mixed");
		}
		else if (entryType == EntryType::EntryDA)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "da");
		}
		else if (entryType == EntryType::EntryFile)
		{
			if (param::pathTable) {
				 newelement->SetAttribute(xml::attrib::OFFSET, entry.entry.entryOffs.lsb);
			}

			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "data");
		}
	}
	WriteOptionalXMLAttribs(newelement, entry, entryType, attributeCounters);
	return dirElement;
}

void WriteXMLGap(unsigned int numSectors, tinyxml2::XMLElement* dirElement)
{
	// TODO: Detect if gap needs checksums and reflect it accordingly here
	tinyxml2::XMLElement* newelement = dirElement->InsertNewChildElement("dummy");
	newelement->SetAttribute(xml::attrib::NUM_DUMMY_SECTORS, numSectors);
}

void WriteXMLByLBA(const std::list<cd::IsoDirEntries::Entry>& files, tinyxml2::XMLElement* dirElement, const fs::path& sourcePath, unsigned int& expectedLBA,
	EntryAttributeCounters& attributeCounters)
{
	fs::path currentVirtualPath; // Used to find out whether to traverse 'dir' up or down the chain
	unsigned tracknum = 2;
	for (const auto& entry : files)
	{
		const bool isDA = GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8) == EntryType::EntryDA;

		// only check for gaps, update LBA if it's inside the iso filesystem
		if(!isDA)
		{
			if (entry.entry.entryOffs.lsb > expectedLBA)
			{
				// if this is a DA file we are at the end of filesystem, flag to write the gap after DA files
				WriteXMLGap(entry.entry.entryOffs.lsb - expectedLBA, dirElement);
			}
			expectedLBA = entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb);
		}

		// add a trackid to DA tracks
		std::string trackid;
		if (isDA)
		{
			char tidbuf[3];
			snprintf(tidbuf, sizeof(tidbuf), "%02u", tracknum++);
			trackid = tidbuf;
		}

		// Work out the relative position between the current directory and the element to create
		const fs::path relative = entry.virtualPath.lexically_relative(currentVirtualPath);
		for (const fs::path& part : relative)
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

		dirElement = WriteXMLEntry(entry, dirElement, &currentVirtualPath, sourcePath, trackid, attributeCounters);
	}
}

void WriteXMLByDirectories(const cd::IsoDirEntries* directory, tinyxml2::XMLElement* dirElement, const fs::path& sourcePath, unsigned int& expectedLBA,
	EntryAttributeCounters& attributeCounters)
{
	unsigned tracknum = 2;
	for (const auto& e : directory->dirEntryList.GetView())
	{
		const auto& entry = e.get();

		std::string trackid;
		if (GetXAEntryType((entry.extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8) == EntryType::EntryDA)
		{
			char tidbuf[3];
			snprintf(tidbuf, sizeof(tidbuf), "%02u", tracknum++);
			trackid = tidbuf;
		}
		else
		{
			// Update the LBA to the max encountered value
			expectedLBA = std::max(expectedLBA, entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb));
		}

		tinyxml2::XMLElement* child = WriteXMLEntry(entry, dirElement, nullptr, sourcePath, trackid, attributeCounters);
		// Recursively write children if there are any
		if (const cd::IsoDirEntries* subdir = entry.subdir.get(); subdir != nullptr)
		{
			WriteXMLByDirectories(subdir, child, sourcePath, expectedLBA, attributeCounters);
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

		std::vector<cd::IsoPathTable::Entry> sorted(pathTable.pathTableList);
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
				return a.entry.dirOffs < b.entry.dirOffs;	
		});

    if (numEntries == 0) {
        printf("   No files to find.\n");
        return;
    }

	// Prepare output directories
	for(size_t i=0; i<numEntries; i++)
	{
		const fs::path dirPath = param::outPath / pathTable.GetFullDirPath(i);

		std::error_code ec;
		fs::create_directories(dirPath, ec);
	}

    printf("ISO contents:\n\n");


	std::list<cd::IsoDirEntries::Entry> entries;
	std::unique_ptr<cd::IsoDirEntries> rootDir = (param::pathTable
		?	ParseRootPathTable(reader, ListView(entries), pathTable.pathTableList, sorted)
		: ParseRoot(reader,
					ListView(entries),
					descriptor.rootDirRecord.entryOffs.lsb));

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
				setAttributeIfNotEmpty(xml::attrib::COPYRIGHT, CleanDescElement(descriptor.copyrightFileIdentifier));
				newElement->SetAttribute(xml::attrib::CREATION_DATE, LongDateToString(descriptor.volumeCreateDate).c_str());
				if (auto ZERO_DATE = GetUnspecifiedLongDate(); memcmp(&descriptor.volumeModifyDate, &ZERO_DATE, sizeof(descriptor.volumeModifyDate)) != 0)
				{
					// Set only if not zero
					newElement->SetAttribute(xml::attrib::MODIFICATION_DATE, LongDateToString(descriptor.volumeModifyDate).c_str());
				}
			}

			const fs::path xmlPath = param::xmlFile.parent_path().lexically_normal();

			{
				tinyxml2::XMLElement *newElement = trackElement->InsertNewChildElement(xml::elem::LICENSE);
				newElement->SetAttribute(xml::attrib::LICENSE_FILE,
					(param::outPath / "license_data.dat").lexically_proximate(xmlPath).generic_u8string().c_str());
			}

			// Create <default_attributes> now so it lands before the directory tree
			tinyxml2::XMLElement* defaultAttributesElement = trackElement->InsertNewChildElement(xml::elem::DEFAULT_ATTRIBUTES);

			const fs::path sourcePath = param::outPath.lexically_proximate(xmlPath);

			// process DA "files" to tracks and add to the dirs so the XML looks nicer
			std::vector<std::list<cd::IsoDirEntries::Entry>::const_iterator> dafiles;
			for(auto it = entries.begin(); it != entries.end(); ++it)
			{
				if(GetXAEntryType(((*it).extData.attributes & cdxa::XA_ATTRIBUTES_MASK) >> 8) == EntryType::EntryDA)
				{
					dafiles.emplace_back(it);
				}
			}
			std::vector<cdtrack> tracks;
			for(const auto& dafile : dafiles)
			{
				auto tracksource = GetRealDAFilePath(sourcePath / dafile->virtualPath / CleanIdentifier(dafile->identifier)).lexically_normal();

				// add to make track element later
				tracks.emplace_back(
					dafile->entry.entryOffs.lsb,
					dafile->entry.entrySize.lsb,
					tracksource.generic_u8string()
				);

				// add back in to the rest of the files
				for(auto it = entries.begin(); it != entries.end(); ++it)
				{
				    fs::path vpath = (*it).virtualPath / CleanIdentifier((*it).identifier);
					if(dafile->virtualPath == vpath)
					{
						do {
							++it;
						} while((it != entries.end()) && ((*it).virtualPath == dafile->virtualPath));
						entries.splice(it, entries, dafile);
						break;
					}
				}
			}

			EntryAttributeCounters attributeCounters;
			unsigned currentLBA = descriptor.rootDirRecord.entryOffs.lsb;
			if (param::outputSortedByDir)
			{
				WriteXMLByDirectories(rootDir.get(), trackElement, sourcePath, currentLBA, attributeCounters);		
			}
			else
			{
				WriteXMLByLBA(entries, trackElement, sourcePath, currentLBA, attributeCounters);
			}

			tinyxml2::XMLElement *dirtree = trackElement->FirstChildElement(xml::elem::DIRECTORY_TREE);
			SimplifyDefaultXMLAttributes(dirtree, EstablishXMLAttributeDefaults(defaultAttributesElement, attributeCounters));

			// write CDDA tracks
			tinyxml2::XMLNode *modifyProject = trackElement->Parent();
			tinyxml2::XMLElement *addAfter = trackElement;
			unsigned tracknum = 2;
			for(const auto& track : tracks)
			{
				unsigned pregap_sectors = 0;
				if(track.lba != currentLBA)
				{
					unsigned delta = (track.lba - currentLBA);
					// HACK add dummy sectors, assumes a 150 sector pregap if this a gap of more sectors
					if((delta > 150) && (tracknum == 2))
					{
						WriteXMLGap(delta-150, dirtree);
						currentLBA += (delta-150);
						delta = 150;
					}
					currentLBA += delta;
					pregap_sectors = delta;
				}
				currentLBA += GetSizeInSectors(track.size);
				char tidbuf[3];
				snprintf(tidbuf, sizeof(tidbuf), "%02u", tracknum);
				tinyxml2::XMLElement *newtrack = xmldoc.NewElement(xml::elem::TRACK);
				newtrack->SetAttribute(xml::attrib::TRACK_TYPE, "audio");
				newtrack->SetAttribute(xml::attrib::TRACK_ID, tidbuf);
				newtrack->SetAttribute(xml::attrib::TRACK_SOURCE, track.source.c_str());
				// only write the pregap element if it's non default
	            if(pregap_sectors != 150)
	            {
                    tinyxml2::XMLElement *pregap = newtrack->InsertNewChildElement(xml::elem::TRACK_PREGAP);
	                pregap->SetAttribute(xml::attrib::PREGAP_DURATION, SectorsToTimecode(pregap_sectors).c_str());
                }

                modifyProject->InsertAfterChild(addAfter, newtrack);
	            addAfter = newtrack;
				tracknum++;
			}

			xmldoc.SaveFile(file);
			fclose(file);
		}
	}
}

int Main(int argc, char *argv[])
{
	static constexpr const char* HELP_TEXT =
		"dumpsxiso [-h|--help] [-x <path>] [-s <path>] <isofile>\n\n"
		"  <isofile>  - File name of ISO file (supports any 2352 byte/sector images).\n"
		"  -x <path>  - Specified destination directory of extracted files.\n"
		"  -s <path>  - Outputs an MKPSXISO compatible XML script for later rebuilding.\n"
		"  -S|--sort-by-dir - Outputs a \"pretty\" XML script where entries are grouped in directories, instead of strictly following their original order on the disc.\n"
		"  -e|--encode <codec> - Codec to encode CDDA/DA audio. wave is default. Supported codecs: " SUPPORTED_CODEC_TEXT "\n"
		"  -h|--help  - Show this help text\n"
		"  -pt|--path-table - instead of going through the file system, go to every known directory in order; helps with deobfuscating\n";

    printf( "DUMPSXISO " VERSION " - PlayStation ISO dumping tool\n"
			"2017 Meido-Tek Productions (John \"Lameguy\" Wilbert Villamor/Lameguy64)\n"
			"2020 Phoenix (SadNES cITy)\n"
			"2021-2022 Silent, Chromaryu, G4Vi, and spicyjpeg\n\n" );

	if (argc == 1)
	{
		printf(HELP_TEXT);
		return EXIT_SUCCESS;
	}

	for (char** args = argv+1; *args != nullptr; args++)
	{
		// Is it a switch?
		if ((*args)[0] == '-')
		{
			if (ParseArgument(args, "pt", "path-table"))
			{
        param::pathTable = true;
        continue;
			}
			if (ParseArgument(args, "h", "help"))
			{
				printf(HELP_TEXT);
				return EXIT_SUCCESS;
			}
			if (auto outPath = ParsePathArgument(args, "x"); outPath.has_value())
			{
				param::outPath = outPath->lexically_normal();
				continue;
			}
			if (auto xmlPath = ParsePathArgument(args, "s"); xmlPath.has_value())
			{
				param::xmlFile = *xmlPath;
				continue;
			}
			if (ParseArgument(args, "S", "sort-by-dir"))
			{
				param::outputSortedByDir = true;
				continue;
			}
			if(auto encodingStr = ParseStringArgument(args, "e", "encode"); encodingStr.has_value())
			{
				unsigned i;
				for(i = 0; i < std::size(EncodingCodecs); i++)
				{
					if(CompareICase(*encodingStr, EncodingCodecs[i].codec))
					{
						break;
					}
				}
				if(i == std::size(EncodingCodecs))
				{
					printf("Unknown codec: %s\n", (*encodingStr).c_str());
					printf("Supported codecs: %s\n", SUPPORTED_CODEC_TEXT);
				    return EXIT_FAILURE;
				}
				if(EncodingCodecs[i].eaf & SUPPORTED_CODECS)
				{
					param::encodingFormat = EncodingCodecs[i].eaf;
					continue;
				}
				printf("%s", EncodingCodecs[i].notcompiledmessage);
				return EXIT_FAILURE;
			}

			// If we reach this point, an unknown parameter was passed
			printf("Unknown parameter: %s\n", *args);
			return EXIT_FAILURE;
		}

		if (param::isoFile.empty())
		{
			param::isoFile = fs::u8path(*args);
		}
		else
		{
			printf("Only one ISO file is supported.\n");
			return EXIT_FAILURE;
		}
	}

	if (param::isoFile.empty())
	{
		printf("No iso file specified.\n");
		return EXIT_FAILURE;
	}


	cd::IsoReader reader;

	if (!reader.Open(param::isoFile)) {

		printf("ERROR: Cannot open file %" PRFILESYSTEM_PATH "...\n", param::isoFile.lexically_normal().c_str());
		return EXIT_FAILURE;

	}
	
	// Check if file has a valid ISO9660 header
	{
		char sectbuff[2048];
		//char descid[] = { 0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01 };
		reader.SeekToSector(16);
		reader.ReadBytes(&sectbuff, 2048);
		if( memcmp(sectbuff, "\1CD001\1", 7) )
		{
			printf("ERROR: File does not contain a valid ISO9660 file system.\n");
			return EXIT_FAILURE;
		}
	}

	if (!param::outPath.empty())
	{
		printf("Output directory : %" PRFILESYSTEM_PATH "\n", param::outPath.lexically_normal().c_str());
	}

    ParseISO(reader);
	return EXIT_SUCCESS;
}
