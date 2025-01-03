#include "platform.h"
#include "common.h"
#include "xml.h"
#include "cue.h"
#include <map>

#ifndef MKPSXISO_NO_LIBFLAC
#include "FLAC/stream_encoder.h"
#endif

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
	bool force = false;
	bool noxml = false;
	bool noWarns = false;
	bool QuietMode = false;
	bool pathTable = false;
    bool outputSortedByDir = false;
	EncoderAudioFormats encodingFormat = EAF_WAV;
}

namespace global {
	CueFile cueFile;
	std::optional<bool> new_type;
}

fs::path GetRealDAFilePath(const fs::path& inputPath)
{
	fs::path outputPath(inputPath); 
	if(param::encodingFormat == EAF_WAV)
	{
		outputPath.replace_extension(".WAV");
	}
#ifndef MKPSXISO_NO_LIBFLAC
	else if(param::encodingFormat == EAF_FLAC)
	{
		outputPath.replace_extension(".FLAC");
	}
#endif
	else
	{
		outputPath.replace_extension(".PCM");
	}
	return outputPath;
}

template<size_t N>
void PrintId(const char* label, char (&id)[N])
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
		printf("%s%.*s\n", label, static_cast<int>(view.length()), view.data());
	}
}

void PrintDate(const char* label, const cd::ISO_LONG_DATESTAMP& date)
{
	auto ZERO_DATE = GetUnspecifiedLongDate();
	if (memcmp(&date, &ZERO_DATE, sizeof(date)))
	{
		printf("%s%s\n", label, LongDateToString(date).c_str());
	}
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

// This will ensure that the EDC remains the same as in the original file. Games built with an old, buggy Sony's mastering tool version
// don't have EDC Form2 data (this can be checked at redump.org) and some games rely on this to do anti-piracy checks like DDR.
const bool CheckEDCXA(cd::IsoReader &reader) {
	cd::SECTOR_M2F2 sector;
	while (reader.ReadBytesXA(sector.data, 2336, true)) {
		if (sector.data[2] & 0x20) {
			if (sector.data[2332] == 0 && sector.data[2333] == 0 && sector.data[2334] == 0 && sector.data[2335] == 0) {
				return false;
			}
			return true;
		}
	}
	return true;
}

// Games from 2003 and onwards apparenly has built with a newer Sony's mastering tool.
// These has different submode in the descriptor sectors, a correct root year value and files are sorted by LBA instead by name.
const bool CheckISOver(cd::IsoReader &reader, bool& ps2) {
	cd::SECTOR_M2F2 sector;
	reader.SeekToSector(15);
	reader.ReadBytesXA(sector.data, 2336);
	if (sector.data[2] & 0x08) {
		ps2 = true;
	}
	reader.ReadBytesXA(sector.data, 2336, true);
	if (sector.data[2] & 0x01) {
		return false;
	}
	return true;
}

std::unique_ptr<cd::ISO_LICENSE> ReadLicense(cd::IsoReader& reader) {
	auto license = std::make_unique<cd::ISO_LICENSE>();

	reader.SeekToSector(0);
	reader.ReadBytesXA(license->data, sizeof(license->data));

	return license;
}

void SaveLicense(const cd::ISO_LICENSE& license) {
	if (!param::QuietMode) {
		printf("\n  Creating license data...");
	}

	FILE* outFile = OpenFile(param::outPath / "license_data.dat", "wb");

    if (outFile == NULL) {
		printf("\nERROR: Cannot create license file.\n");
        exit(EXIT_FAILURE);
    }
	else if (!param::QuietMode) {
		printf(" Ok.\n");
	}

    fwrite(license.data, 1, sizeof(license.data), outFile);
    fclose(outFile);
}

void writePCMFile(FILE *outFile, cd::IsoReader& reader, const size_t cddaSize, const bool isInvalid)
{
	int bytesLeft = cddaSize;
	while (bytesLeft > 0) {

		unsigned char copyBuff[2352]{};

    	int bytesToRead = bytesLeft;

    	if (bytesToRead > 2352)
    		bytesToRead = 2352;

    	if (!isInvalid)
    		reader.ReadBytesDA(copyBuff, bytesToRead);

    	fwrite(copyBuff, 1, bytesToRead, outFile);

    	bytesLeft -= bytesToRead;
    }
}

void writeWaveFile(FILE *outFile, cd::IsoReader& reader, const size_t cddaSize, const bool isInvalid)
{
    cd::RIFF_HEADER riffHeader;
    prepareRIFFHeader(&riffHeader, cddaSize);
    fwrite((void*)&riffHeader, 1, sizeof(cd::RIFF_HEADER), outFile);

    writePCMFile(outFile, reader, cddaSize, isInvalid);
}

#ifndef MKPSXISO_NO_LIBFLAC
void writeFLACFile(FILE *outFile, cd::IsoReader& reader, const int cddaSize, const bool isInvalid)
{
	FLAC__bool ok = true;
	FLAC__StreamEncoder *encoder = 0;
	FLAC__StreamEncoderInitStatus init_status;
	if((encoder = FLAC__stream_encoder_new()) == NULL)
	{
		fprintf(stderr, "\nERROR: allocating encoder.\n");
		exit(EXIT_FAILURE);
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
		fprintf(stderr, "\nERROR: setting encoder settings.\n");
		goto writeFLACFile_cleanup;
	}

	init_status = FLAC__stream_encoder_init_FILE(encoder, outFile, /*progress_callback=*/NULL, /*client_data=*/NULL);
	if(init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
	{
		fprintf(stderr, "\nERROR: initializing encoder: %s.\n", FLAC__StreamEncoderInitStatusString[init_status]);
		ok = false;
		goto writeFLACFile_cleanup;
	}

    {
    size_t left = (size_t)total_samples;
	size_t max_pcmframe_read = 2352 / (channels * (bps/8));

    std::unique_ptr<int32_t[]> pcm(new int32_t[channels * max_pcmframe_read]);
	while (left && ok) {

		unsigned char copyBuff[2352]{};

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
		fprintf(stderr, "\nencoding: FAILED\n");
		fprintf(stderr, "   state: %s\n", FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(encoder)]);
	}

writeFLACFile_cleanup:
	FLAC__stream_encoder_delete(encoder);
	if(!ok) {
		exit(EXIT_FAILURE);
	}
}
#endif

// XML attribute stuff
struct EntryAttributeCounters
{
	std::map<int, unsigned int> GMTOffs;
	std::map<int, unsigned int> HFLAG;
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

	element->SetAttribute(xml::attrib::HIDDEN_FLAG, entry.entry.flags & 0x01);
	++attributeCounters.HFLAG[entry.entry.flags & 0x01];
}

static EntryAttributes EstablishXMLAttributeDefaults(tinyxml2::XMLElement* defaultAttributesElement, const EntryAttributeCounters& attributeCounters)
{
	// First establish "defaults" - that is, the most commonly occurring attributes
	auto findMaxElement = [](const auto& map)
	{
		if (!map.empty()) {
			return std::max_element(map.begin(), map.end(), [](const auto& left, const auto& right) { return left.second < right.second; })->first;
		}
		return 0;
	};

	EntryAttributes defaultAttributes;
	defaultAttributes.GMTOffs = static_cast<signed char>(findMaxElement(attributeCounters.GMTOffs));
	defaultAttributes.HFLAG = static_cast<unsigned char>(findMaxElement(attributeCounters.HFLAG));
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
	if (defaultAttributes.HFLAG) { // Set only if not zero
		defaultAttributesElement->SetAttribute(xml::attrib::HIDDEN_FLAG, 0x01);
	}

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
	deleteAttribute(xml::attrib::HIDDEN_FLAG, defaults.HFLAG);
	deleteAttribute(xml::attrib::XA_ATTRIBUTES, defaults.XAAttrib);
	deleteAttribute(xml::attrib::XA_PERMISSIONS, defaults.XAPerm);
	deleteAttribute(xml::attrib::XA_GID, defaults.GID);
	deleteAttribute(xml::attrib::XA_UID, defaults.UID);

	for (tinyxml2::XMLElement* child = element->FirstChildElement(); child != nullptr; child = child->NextSiblingElement())
	{
		SimplifyDefaultXMLAttributes(child, defaults);
	}
}

EntryType GetXAEntryType(unsigned short xa_attr)
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

std::unique_ptr<cd::IsoDirEntries> ParsePathTable(cd::IsoReader& reader, ListView<cd::IsoDirEntries::Entry> view, std::vector<cd::IsoPathTable::Entry>& pathTableList, int index,
   const fs::path& path) {
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));

	// Calculate Directory Record sector size
	int dirRecordSectors = 0;
	if (*global::new_type && pathTableList.size() != index + 1) {
		dirRecordSectors = pathTableList[index + 1].entry.dirOffs - pathTableList[index].entry.dirOffs;
	}
	else {
		reader.SeekToSector(pathTableList[index].entry.dirOffs);
		while (true) {
			cd::SECTOR_M2F1 sector;
			dirRecordSectors++;
			reader.ReadBytesXA(sector.subHead, 2336);
			if (sector.subHead[2] == 0x89) { // Directory records always ends with submode 0x89
				break;
			}
		}
	}

	dirEntries->ReadDirEntries(&reader, pathTableList[index].entry.dirOffs, dirRecordSectors);

	// Only add the missing directories to the list
    for (int i = 1; i < pathTableList.size(); i++) {
        auto& e = pathTableList[i];
        if (e.entry.parentDirIndex - 1 == index) {
			if (!std::any_of(dirEntries->dirEntryList.GetView().begin(), dirEntries->dirEntryList.GetView().end(), [&e](const auto& entry)
					{
						return entry.get().identifier == e.name;
					})) {
				dirEntries->ReadRootDir(&reader, e.entry.dirOffs);
			}
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
				
						entry.subdir = ParsePathTable(reader, dirEntries->dirEntryList.NewView(), pathTableList, index, path / s);				
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

std::unique_ptr<cd::IsoDirEntries> ParseRootPathTable(cd::IsoReader& reader, ListView<cd::IsoDirEntries::Entry> view, std::vector<cd::IsoPathTable::Entry>& pathTableList)
{ 
    auto dirEntries = std::make_unique<cd::IsoDirEntries>(std::move(view));
    dirEntries->ReadRootDir(&reader, pathTableList[0].entry.dirOffs);

  	auto& entry = dirEntries->dirEntryList.GetView().front().get();

	entry.subdir = ParsePathTable(reader, dirEntries->dirEntryList.NewView(), pathTableList, 0, CleanIdentifier(entry.identifier));

  	return dirEntries;
}

std::vector<std::list<cd::IsoDirEntries::Entry>::iterator> processDAfiles(cd::IsoReader &reader, std::list<cd::IsoDirEntries::Entry>& entries)
{
	std::vector<std::list<cd::IsoDirEntries::Entry>::iterator> DAfiles;
	unsigned tracknum = 2;

	// Get referenced DA files and assign them an ID number
	for(auto it = entries.begin(); it != entries.end(); it++) {
		if(it->type == EntryType::EntryDA) {
			it->trackid = (tracknum < 10 ? "0" : "") + std::to_string(tracknum);
			tracknum++;
			DAfiles.push_back(it);
		}
	}

	if (tracknum <= global::cueFile.tracks.size()) {
		std::vector<cd::IsoDirEntries::Entry> unrefDAbuff;
		// Create a buffer of unreferenced DA tracks
		for(const auto& track : global::cueFile.tracks) {
			// Skip non audio tracks
			if (track.type != "AUDIO") {
				continue;
			}
			// Skip referenced DA tracks
			if (tracknum > 2) {
				if (std::any_of(DAfiles.begin(), DAfiles.end(), [&track](const auto& entry)
						{
							return entry->entry.entryOffs.lsb == track.startSector;
						})) {
					continue;
				}
			}

			// Add the unreferenced DA track to the buffer
			auto& entry = unrefDAbuff.emplace_back();
			entry.entry.entryOffs.lsb = track.startSector;
			entry.entry.entrySize.lsb = track.sizeInSectors * 2048;
			entry.identifier = GetRealDAFilePath("TRACK-" + track.number).string() + ";1";
			entry.type = EntryType::EntryDA;

			// Additional safety check in case the .cue file had a wrong pause size
			// For ex, Mega Man X3 track 30 had 149 sectors pause, but at redump.org says it was a 150 standard one
			unsigned char sectorBuff[CD_SECTOR_SIZE];
			unsigned char emptyBuff[CD_SECTOR_SIZE] {};
			while (true) {
				if (!reader.SeekToSector(entry.entry.entryOffs.lsb - 1) && !multiBinSeeker(entry.entry.entryOffs.lsb - 1, entry, reader, global::cueFile)) {
					break;
				}
				reader.ReadBytesDA(sectorBuff, CD_SECTOR_SIZE, true);
				if (std::memcmp(sectorBuff, emptyBuff, CD_SECTOR_SIZE)) {
					entry.entry.entryOffs.lsb--;
					entry.entry.entrySize.lsb += 2048;
				}
				else {
					break;
				}
			}
		}

		// Add unreferenced DA tracks to entries for further extraction
		for (auto& entry : unrefDAbuff) {
			entries.emplace_back(std::move(entry));
			DAfiles.push_back(std::prev(entries.end()));
		}

		// Sort DA files by LBA
		std::sort(DAfiles.begin(), DAfiles.end(),[](const auto& left, const auto& right)
			{
				return left->entry.entryOffs.lsb < right->entry.entryOffs.lsb;
			});

		// Only recalculate the track id's if there were unreferenced tracks among the referenced ones
		// This is just for a prettier XML sort, because unsorted track id's have no impact at build time
		if (tracknum > 2) {
			tracknum = 2;
			for(const auto& entry : DAfiles) {
				if(!entry->trackid.empty()) {
					entry->trackid = (tracknum < 10 ? "0" : "") + std::to_string(tracknum);
				}
				tracknum++;
			}
		}

		// Reopen the first file for safety
		reader.Open(global::cueFile.tracks[0].filePath);
	}

	return DAfiles;
}

void ExtractFiles(cd::IsoReader& reader, const std::list<cd::IsoDirEntries::Entry>& files, const fs::path& rootPath)
{
	bool firstDA = true;
    for (const auto& entry : files)
	{
		const fs::path outputPath = rootPath / entry.virtualPath / CleanIdentifier(entry.identifier);
        if (entry.subdir == nullptr) // Do not extract directories, they're already prepared
		{
			if (entry.type == EntryType::EntryXA)
			{
				// Extract XA or STR file.
				if (!param::QuietMode) {
					printf("    Extracting XA \"%" PRFILESYSTEM_PATH "\"... ", outputPath.lexically_normal().c_str());
				}
				fflush(stdout);
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
					printf("\nERROR: Cannot create file \"%" PRFILESYSTEM_PATH "\"...\n", outputPath.filename().c_str());
					exit(EXIT_FAILURE);
				}

				// Copy loop
				while(bytesLeft > 0) {

					unsigned char copyBuff[2336];

					int bytesToRead = bytesLeft;

					if (bytesToRead > 2336)
						bytesToRead = 2336;

					reader.ReadBytesXA(copyBuff, bytesToRead);

					fwrite(copyBuff, 1, bytesToRead, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);
			}
			else if (entry.type == EntryType::EntryDA)
			{
				// Extract CDDA file
				if (firstDA && !param::QuietMode) {
					printf("\n  Creating CDDA files...\n");
					firstDA = false;
				}
				bool isInvalid = !global::cueFile.multiBIN ? !reader.SeekToSector(entry.entry.entryOffs.lsb) : !multiBinSeeker(entry.entry.entryOffs.lsb, entry, reader, global::cueFile);
                auto daOutPath = GetRealDAFilePath(outputPath);
				auto outFile = OpenScopedFile(daOutPath, "wb");

				if (isInvalid && !param::noWarns) {
					printf( "\nWARNING: The CDDA file \"%" PRFILESYSTEM_PATH "\" is out of the iso file bounds.\n"
							"\t This usually means that the game has audio tracks, and they are on separate files.\n", daOutPath.filename().c_str() );
					if (global::cueFile.tracks.empty()) {
						printf("\t Try using a .cue file, instead of an ISO image, to be able to access those files.\n");
					}
					printf( "\t DUMPSXISO will write the file as a dummy (silent) cdda file.\n"
							"\t This is generally fine, when the real CDDA file is also a dummy file.\n"
							"\t If it is not dummy, you WILL lose this audio data in the rebuilt iso... " );
					if (param::QuietMode) {
						printf("\n");
					}
				}
				else if (!param::QuietMode) {
					printf("    Extracting audio \"%" PRFILESYSTEM_PATH "\"... ", daOutPath.lexically_normal().c_str());
				}
				fflush(stdout);

				if (!outFile) {
					printf("\nERROR: Cannot create file \"%" PRFILESYSTEM_PATH "\"...\n", daOutPath.filename().c_str());
					exit(EXIT_FAILURE);
				}

				size_t sectorsToRead = GetSizeInSectors(entry.entry.entrySize.lsb);
				size_t cddaSize = 2352 * sectorsToRead;

				if(param::encodingFormat == EAF_WAV)
				{
					writeWaveFile(outFile.get(), reader, cddaSize, isInvalid);
				}
#ifndef MKPSXISO_NO_LIBFLAC
				else if(param::encodingFormat == EAF_FLAC)
				{
					// libflac closes outFile
					writeFLACFile(outFile.release(), reader, cddaSize, isInvalid);
				}
#endif
				else
				{
					writePCMFile(outFile.get(), reader, cddaSize, isInvalid);
				}

				if (global::cueFile.multiBIN) {
					reader.Open(global::cueFile.tracks[0].filePath);
				}
			}
			else if (entry.type == EntryType::EntryFile)
			{
				// Extract regular file
				if (!param::QuietMode) {
					printf("    Extracting \"%" PRFILESYSTEM_PATH "\"... ", outputPath.lexically_normal().c_str());
					fflush(stdout);
				}

				reader.SeekToSector(entry.entry.entryOffs.lsb);

				FILE* outFile = OpenFile(outputPath, "wb");

				if (outFile == NULL) {
					printf("\nERROR: Cannot create file \"%" PRFILESYSTEM_PATH "\"...\n", outputPath.filename().c_str());
					exit(EXIT_FAILURE);
				}

				size_t bytesLeft = entry.entry.entrySize.lsb;
				while(bytesLeft > 0) {

					unsigned char copyBuff[2048];
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
				if (!param::noWarns) {
					printf("WARNING: File %s is of invalid type.\n", entry.identifier.c_str());
				}
				continue;
			}
			if (!param::QuietMode) {
				printf("Done.\n");
			}
        }
    }

	// Update timestamps AFTER all files have been extracted
	// else directories will have their timestamps discarded when files are being unpacked into them!
	for (const auto& entry : files)
	{
		fs::path toChange(rootPath / entry.virtualPath / CleanIdentifier(entry.identifier));
		if(entry.type == EntryType::EntryDA)
		{
			if (entry.trackid.empty()) {
				continue; // Skip if it's an unreferenced DA file
			}
			toChange = GetRealDAFilePath(toChange);
		}
		UpdateTimestamps(toChange, entry.entry.entryDate);
	}
}

tinyxml2::XMLElement* WriteXMLEntry(const cd::IsoDirEntries::Entry& entry, tinyxml2::XMLElement* dirElement, fs::path* currentVirtualPath,
	const fs::path& sourcePath, EntryAttributeCounters& attributeCounters)
{
	tinyxml2::XMLElement* newelement;

	const fs::path outputPath = sourcePath / entry.virtualPath / CleanIdentifier(entry.identifier);
	if (entry.type == EntryType::EntryDir)
	{
		if (!entry.identifier.empty())
		{
			newelement = dirElement->InsertNewChildElement("dir");
			newelement->SetAttribute(xml::attrib::ENTRY_NAME, entry.identifier.c_str());
			newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.lexically_normal().generic_string().c_str());
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
		newelement->SetAttribute(xml::attrib::ENTRY_NAME, CleanIdentifier(entry.identifier).c_str());
		if(entry.type != EntryType::EntryDA)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.lexically_normal().generic_string().c_str());
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, entry.type == EntryType::EntryFile ? "data" : "mixed");	
		}
		else
		{
			newelement->SetAttribute(xml::attrib::TRACK_ID, entry.trackid.c_str());
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "da");
		}
	}
	if (!entry.identifier.empty())
	{
		if (param::force)
		{
			newelement->SetAttribute(xml::attrib::OFFSET, entry.entry.entryOffs.lsb);
		}
		if (entry.order.has_value())
		{
			newelement->SetAttribute(xml::attrib::ORDER, *entry.order);
		}
	}
	WriteOptionalXMLAttribs(newelement, entry, entry.type, attributeCounters);
	return dirElement;
}

void WriteXMLGap(const unsigned int numSectors, tinyxml2::XMLElement* dirElement, const unsigned int startSector, cd::IsoReader &reader)
{
	if (numSectors < 1) {
		return;
	}
	cd::SECTOR_M2F1 sector;
	reader.SeekToSector(startSector);
	reader.ReadBytesXA(sector.subHead, 2336);
	tinyxml2::XMLElement* newelement = dirElement->InsertNewChildElement("dummy");
	newelement->SetAttribute(xml::attrib::NUM_DUMMY_SECTORS, numSectors);
	newelement->SetAttribute(xml::attrib::ENTRY_TYPE, sector.subHead[2]);
	if (param::force) {
		newelement->SetAttribute(xml::attrib::OFFSET, startSector);
	}
}

void WriteXMLByLBA(std::list<cd::IsoDirEntries::Entry>& files, tinyxml2::XMLElement* dirElement, const fs::path& sourcePath, unsigned int& expectedLBA,
	EntryAttributeCounters& attributeCounters, cd::IsoReader &reader)
{
	fs::path currentVirtualPath; // Used to find out whether to traverse 'dir' up or down the chain

	for (auto& entry : files)
	{
		// if this is a DA file we are at the end of filesystem
		if (entry.type != EntryType::EntryDA)
		{
			// only check for gaps, update LBA if it's inside the iso filesystem
			if (entry.entry.entryOffs.lsb > expectedLBA)
			{
				WriteXMLGap(entry.entry.entryOffs.lsb - expectedLBA, dirElement, expectedLBA, reader);
			}
			expectedLBA = entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb);
		}
		else if (entry.trackid.empty())
		{
			continue; // Skip if it's an unreferenced DA file
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
			dirElement->SetAttribute(xml::attrib::ENTRY_NAME, part.generic_string().c_str());

			currentVirtualPath /= part;
		}

		dirElement = WriteXMLEntry(entry, dirElement, &currentVirtualPath, sourcePath, attributeCounters);
	}
}

void WriteXMLByDirectories(const cd::IsoDirEntries* directory, tinyxml2::XMLElement* dirElement, const fs::path& sourcePath, unsigned int& expectedLBA,
	EntryAttributeCounters& attributeCounters)
{
	for (const auto& e : directory->dirEntryList.GetView())
	{
		auto& entry = e.get();

		if (entry.type != EntryType::EntryDA)
		{
			// Update the LBA to the max encountered value
			expectedLBA = std::max(expectedLBA, entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb));
		}

		tinyxml2::XMLElement* child = WriteXMLEntry(entry, dirElement, nullptr, sourcePath, attributeCounters);
		// Recursively write children if there are any
		if (const cd::IsoDirEntries* subdir = entry.subdir.get(); subdir != nullptr)
		{
			WriteXMLByDirectories(subdir, child, sourcePath, expectedLBA, attributeCounters);
		}
	}
}

void ParseISO(cd::IsoReader& reader) {

    cd::ISO_DESCRIPTOR descriptor;
	bool ps2 = false;
	auto license = ReadLicense(reader);
	const bool xa_edc = CheckEDCXA(reader);
	global::new_type = CheckISOver(reader, ps2);

    reader.SeekToSector(16);
    reader.ReadBytes(&descriptor, 2048);


	if (!param::QuietMode) {
		printf( "Scanning tracks...\n\n"
				"  Track #1 data:\n"
				"    Identifiers:\n" );
		PrintId("      System ID         : ", descriptor.systemID);
		PrintId("      Volume ID         : ", descriptor.volumeID);
		PrintId("      Volume Set ID     : ", descriptor.volumeSetIdentifier);
		PrintId("      Publisher ID      : ", descriptor.publisherIdentifier);
		PrintId("      Data Preparer ID  : ", descriptor.dataPreparerIdentifier);
		PrintId("      Application ID    : ", descriptor.applicationIdentifier);
		PrintId("      Copyright ID      : ", descriptor.copyrightFileIdentifier);

		PrintDate("      Creation Date     : ", descriptor.volumeCreateDate);
		PrintDate("      Modification Date : ", descriptor.volumeModifyDate);
		PrintDate("      Expiration Date   : ", descriptor.volumeExpiryDate);
	}

    cd::IsoPathTable pathTable;

    size_t numEntries = pathTable.ReadPathTable(&reader, descriptor.pathTable1Offs);

    if (numEntries == 0) {
		printf("\nNo files to find.\n");
        return;
    }

	// Prepare output directories
	for(size_t i=0; i<numEntries; i++)
	{
		const fs::path dirPath = param::outPath / pathTable.GetFullDirPath(i);

		std::error_code ec;
		fs::create_directories(dirPath, ec);
		if (ec) {
			printf("\nERROR: Cannot create directory \"%" PRFILESYSTEM_PATH "\"... %s\n", dirPath.parent_path().lexically_normal().c_str(), ec.message().c_str());
			exit(EXIT_FAILURE);
		}
	}

	if (!param::QuietMode) {
		if (!param::noxml) {
			printf("\n    License file: \"%" PRFILESYSTEM_PATH "\"\n", (param::outPath.lexically_normal() / "license_data.dat").c_str());
		}
		printf("\n    Parsing directory tree...\n");
	}

	std::list<cd::IsoDirEntries::Entry> entries;
	std::unique_ptr<cd::IsoDirEntries> rootDir = (param::pathTable
		? ParseRootPathTable(reader, ListView(entries), pathTable.pathTableList)
		: ParseRoot(reader,	ListView(entries), descriptor.rootDirRecord.entryOffs.lsb));

	// Sort files by LBA for "strict" output
	entries.sort([](const auto& left, const auto& right)
		{
			return left.entry.entryOffs.lsb < right.entry.entryOffs.lsb;
		});

	unsigned totalLenLBA = descriptor.volumeSize.lsb;
	if (!param::QuietMode) {
		printf("      Files Total: %zu\n", entries.size() - numEntries);
		printf("      Directories: %zu\n", numEntries - 1);
		for (auto it = entries.rbegin(); it != entries.rend(); it++) {
			if (it->type != EntryType::EntryDA) {
				unsigned endFS = it->entry.entryOffs.lsb + GetSizeInSectors(it->entry.entrySize.lsb);
				endFS += totalLenLBA - endFS < 150 ? totalLenLBA - endFS : 150;
				printf("      Total file system size: %u bytes (%u sectors)\n", endFS * CD_SECTOR_SIZE, endFS);
				break;
			}
		}
	}

	// Process DA tracks and add them to the entries list
	auto DAfiles = processDAfiles(reader, entries);

	if (!param::QuietMode) {
		for(size_t i = 0; i < DAfiles.size(); i++) {
			printf("\n  Track #%zu audio:\n", i + 2);
			printf("    DA File \"%s\"\n", CleanIdentifier(DAfiles[i]->identifier).c_str());
		}
		printf( "\nExtracting ISO...\n"
				"  Creating files...\n" );
	}

	ExtractFiles(reader, entries, param::outPath);

	if (!param::noxml)
	{
		SaveLicense(*license);
		if (!param::QuietMode) {
			printf("  Creating XML document...");
		}
		if (FILE* file = OpenFile(param::xmlFile, "wb"); file != nullptr)
		{
			if (!param::QuietMode) {
				printf(" Ok.\n\n");
			}
			tinyxml2::XMLDocument xmldoc;

			tinyxml2::XMLElement *baseElement = static_cast<tinyxml2::XMLElement*>(xmldoc.InsertFirstChild(xmldoc.NewElement(xml::elem::ISO_PROJECT)));
			baseElement->SetAttribute(xml::attrib::IMAGE_NAME, "mkpsxiso.bin");
			baseElement->SetAttribute(xml::attrib::CUE_SHEET, "mkpsxiso.cue");

			tinyxml2::XMLElement *trackElement = baseElement->InsertNewChildElement(xml::elem::TRACK);
			trackElement->SetAttribute(xml::attrib::TRACK_TYPE, "data");
			trackElement->SetAttribute(xml::attrib::XA_EDC, xa_edc);
			trackElement->SetAttribute(xml::attrib::NEW_TYPE, *global::new_type);
			if (ps2) {
				trackElement->SetAttribute(xml::attrib::PS2, ps2);
			}

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

			const fs::path xmlPath = param::xmlFile.parent_path();
			const fs::path sourcePath = xmlPath.is_absolute() ? fs::absolute(param::outPath) : param::outPath.lexically_proximate(xmlPath);

			// Add license element to the xml
			tinyxml2::XMLElement *newElement = trackElement->InsertNewChildElement(xml::elem::LICENSE);
			newElement->SetAttribute(xml::attrib::LICENSE_FILE,	(sourcePath / "license_data.dat").generic_string().c_str());

			// Create <default_attributes> now so it lands before the directory tree
			tinyxml2::XMLElement* defaultAttributesElement = trackElement->InsertNewChildElement(xml::elem::DEFAULT_ATTRIBUTES);

			EntryAttributeCounters attributeCounters;
			unsigned currentLBA = descriptor.rootDirRecord.entryOffs.lsb;
			if (param::outputSortedByDir)
			{
				WriteXMLByDirectories(rootDir.get(), trackElement, sourcePath, currentLBA, attributeCounters);		
			}
			else
			{
				WriteXMLByLBA(entries, trackElement, sourcePath, currentLBA, attributeCounters, reader);
			}

			tinyxml2::XMLElement *dirtree = trackElement->FirstChildElement(xml::elem::DIRECTORY_TREE);
			SimplifyDefaultXMLAttributes(dirtree, EstablishXMLAttributeDefaults(defaultAttributesElement, attributeCounters));

			// Write the DATA track postgap
			// SYSTEM DESCRIPTION CD-ROM XA Ch.II 2.3, postgap should be always >= 150 sectors for CD-DA discs and optionally for non CD-DA.
			unsigned postGap = 150;
			if (!global::cueFile.tracks.empty() && global::cueFile.tracks[0].endSector <= totalLenLBA) {
				postGap = global::cueFile.tracks[0].endSector - currentLBA;
			}
			else if (totalLenLBA - currentLBA < postGap) {
				postGap = totalLenLBA - currentLBA;
				if (postGap && !param::noWarns) {
					printf("WARNING: Size of DATA track postgap is of %u sectors instead of 150.\n", postGap);
				}
			}
			else if (!DAfiles.empty() && DAfiles[0]->entry.entryOffs.lsb - postGap == currentLBA) {
				postGap = 0;
			}
			// There are some CD-DA games that have a non-zero adrress ECC calculation in the last postgap sector. So, we are checking it.
			// Idk if this behavior could happen in other sectors, but apparently it's only related to CD-DA games postgaps (maybe a bug).
			if (postGap) {
				cd::SECTOR_M2F1 sector;
				reader.SeekToSector(currentLBA + postGap - 1);
				reader.ReadBytesXA(sector.subHead, 2336);
				if (sector.ecc[0] != 0 || sector.ecc[1] != 0 || sector.ecc[2] != 0 || sector.ecc[3] != 0) {
					WriteXMLGap(postGap - 1, dirtree, currentLBA, reader);
					WriteXMLGap(1, dirtree, currentLBA + postGap - 1, reader);
					dirtree->LastChildElement("dummy")->SetAttribute(xml::attrib::ECC_ADDRES, true);
				}
				else {
					WriteXMLGap(postGap, dirtree, currentLBA, reader);
				}
			}
			currentLBA += postGap;

			// Write CD-DA tracks
			tinyxml2::XMLNode *modifyProject = trackElement->Parent();
			tinyxml2::XMLElement *addAfter = trackElement;
			for(const auto& dafile : DAfiles)
			{
				// SYSTEM DESCRIPTION CD-ROM XA Ch.II 2.3, pause should be always >= 150 sectors.
				unsigned pregap_sectors = 150;
				dafile->virtualPath = GetRealDAFilePath(sourcePath / dafile->virtualPath / CleanIdentifier(dafile->identifier)).lexically_normal();
				if(dafile->entry.entryOffs.lsb != currentLBA)
				{
					pregap_sectors = dafile->entry.entryOffs.lsb - currentLBA;
					currentLBA += pregap_sectors;
				}
				currentLBA += GetSizeInSectors(dafile->entry.entrySize.lsb);
				tinyxml2::XMLElement *newtrack = xmldoc.NewElement(xml::elem::TRACK);
				newtrack->SetAttribute(xml::attrib::TRACK_TYPE, "audio");
				if (!dafile->trackid.empty()) {
					newtrack->SetAttribute(xml::attrib::TRACK_ID, dafile->trackid.c_str());
				}
				newtrack->SetAttribute(xml::attrib::TRACK_SOURCE, dafile->virtualPath.generic_string().c_str());
				// only write the pregap element if it's non default
				if(pregap_sectors != 150)
				{
					tinyxml2::XMLElement *pregap = newtrack->InsertNewChildElement(xml::elem::TRACK_PREGAP);
					pregap->SetAttribute(xml::attrib::PREGAP_DURATION, SectorsToTimecode(pregap_sectors).c_str());
				}

				modifyProject->InsertAfterChild(addAfter, newtrack);
				addAfter = newtrack;
			}

			// Check if there is still an EoF gap
			if (currentLBA < totalLenLBA && !param::noWarns) {
				printf( "WARNING: There is still a gap of %u sectors at the end.\n"
						"\t This could mean that there are missing files or tracks.\n", totalLenLBA - currentLBA);
				if (global::cueFile.tracks.empty()) {
					printf("\t Try using a .cue file instead of an ISO image.\n");
				}
				else {
					printf("\t Try using the -pt command, it could help if the game has an obfuscated file system.\n");
				}
			}

			xmldoc.SaveFile(file);
			fclose(file);
		}
		else
		{
			printf("\nERROR: Cannot create xml file \"%" PRFILESYSTEM_PATH "\"... %s\n", param::xmlFile.lexically_normal().c_str(), strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
}

int Main(int argc, char *argv[])
{
	static constexpr const char* HELP_TEXT =
		"Usage: dumpsxiso [options <file>] <isofile>\n\n"
		"  <isofile>\t\tFile name of the bin/cue file (supports any 2352 byte/sector images)\n\n"
		"Options:\n"
		"  -h|--help\t\tShows this help text\n"
		"  -q|--quiet\t\tQuiet mode (suppress all but warnings and errors)\n"
		"  -w|--warns\t\tSuppress all warnings (can be used along with -q)\n"
		"  -x <path>\t\tOptional destination directory for extracted files (defaults to working dir)\n"
		"  -s <file>\t\tOptional XML name/destination for MKPSXISO script (defaults to working dir)\n"
		"  -pt|--path-table\tGo through every known directory in order; helps to deobfuscate some games (like DMW3)\n"
		"  -e|--encode <codec>\tCodec to encode CDDA/DA audio; supports " SUPPORTED_CODEC_TEXT " (defaults to wave)\n"
		"  -f|--force\t\tWrites all lba offsets in the xml to force them at build\n"
		"  -n|--noxml\t\tDo not generate an XML file and license file\n"
		"  -S|--sort-by-dir\tOutputs a \"pretty\" XML script where entries are grouped in directories\n"
		"\t\t\t(instead of strictly following their original order on the disc)\n";

	static constexpr const char* VERSION_TEXT =
		"DUMPSXISO " VERSION " - PlayStation ISO dumping tool\n"
		"Get the latest version at https://github.com/Lameguy64/mkpsxiso\n"
		"Original work: Meido-Tek Productions (John \"Lameguy\" Wilbert Villamor/Lameguy64)\n"
		"Maintained by: Silent (CookiePLMonster) and spicyjpeg\n"
		"Contributions: marco-calautti, G4Vi, Nagtan and all the ones from github\n\n";

	if (argc == 1)
	{
		printf(VERSION_TEXT);
		printf(HELP_TEXT);
		return EXIT_SUCCESS;
	}

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
			if (ParseArgument(args, "f", "force"))
			{
				param::force = true;
				continue;
			}
			if (ParseArgument(args, "n", "noxml"))
			{
				param::noxml = true;
				continue;
			}
			if (ParseArgument(args, "pt", "path-table"))
			{
				param::pathTable = true;
				continue;
			}
			if (ParseArgument(args, "q", "quiet"))
			{
				param::QuietMode = true;
				continue;
			}
			if (ParseArgument(args, "w", "warns"))
			{
				param::noWarns = true;
				continue;
			}
			if (ParseArgument(args, "S", "sort-by-dir"))
			{
				param::outputSortedByDir = true;
				continue;
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
			param::isoFile = *args;
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

	if (!param::QuietMode)
	{
		printf(VERSION_TEXT);
	}

	if (param::outPath.empty())
	{
		param::outPath = param::isoFile.stem();
	}

	if (param::xmlFile.empty())
	{
		param::xmlFile = param::isoFile.stem() += ".xml";
	}

	if (CompareICase(param::isoFile.extension().string(), ".cue"))
	{
		global::cueFile = parseCueFile(param::isoFile);
	}

	cd::IsoReader reader;

	if (!reader.Open(param::isoFile)) {

		printf("ERROR: Cannot open file \"%" PRFILESYSTEM_PATH "\"...\n", param::isoFile.lexically_normal().c_str());
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

	if (!param::QuietMode) {
		printf("Output directory : \"%" PRFILESYSTEM_PATH "\"\n\n", param::outPath.lexically_normal().c_str());
	}

	tzset(); // Initializes the time-related environment variables
    ParseISO(reader);
	if (!param::QuietMode) {
		printf("ISO image dumped successfully.\n");
	}
	return EXIT_SUCCESS;
}
