#include "xmlwriter.h"
#include "xml.h"
#include "platform.h"
#include <map>

namespace param
{
	extern bool dir;
	extern bool lba;
	extern fs::path outPath;
	extern fs::path xmlFile;
	extern bool outputSortedByDir;
}

namespace global
{
	extern bool ps2;
	extern bool xa_edc;
	extern std::string licenseFile;
}

extern fs::path GetEncodedDAPath(const fs::path& inputPath);

namespace
{
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
}

template<size_t N>
static std::string_view CleanDescElement(const char (&id)[N])
{
	const std::string_view view(id, N);
	const size_t last_char = view.find_last_not_of(' ');
	if (last_char == std::string_view::npos)
	{
		return {};
	}

	return view.substr(0, last_char + 1);
}

static void WriteOptionalXMLAttribs(tinyxml2::XMLElement* element, const cd::IsoDirEntries::Entry& entry, EntryAttributeCounters& attributeCounters)
{
	element->SetAttribute(xml::attrib::GMT_OFFSET, entry.entry.entryDate.GMToffs);
	++attributeCounters.GMTOffs[entry.entry.entryDate.GMToffs];

	// xa_attrib only makes sense on XA files
	if (entry.type == EntryType::EntryXA)
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

	const auto HFLAG = entry.entry.flags & 0x20 ? entry.entry.flags & 0x03 : entry.entry.flags & 0x01; // 5th bit simulates obfuscation
	element->SetAttribute(xml::attrib::HIDDEN_FLAG, HFLAG);
	++attributeCounters.HFLAG[HFLAG];

	if (entry.order.has_value())
	{
		element->SetAttribute(xml::attrib::ORDER, *entry.order);
	}
	if (param::lba)
	{
		element->SetAttribute(xml::attrib::OFFSET, entry.entry.entryOffs.lsb);
	}
}

static EntryAttributes EstablishXMLAttributeDefaults(tinyxml2::XMLElement* defaultAttributesElement, const EntryAttributeCounters& attributeCounters)
{
	// First establish "defaults" - that is, the most commonly occurring attributes
	auto findMaxElement = [](const auto& map, const int& defaultAttr)
	{
		if (map.empty())
		{
			return defaultAttr;
		}
		return std::max_element(map.begin(), map.end(), [](const auto& left, const auto& right) { return left.second < right.second; })->first;
	};

	EntryAttributes defaultAttributes;
	defaultAttributes.GMTOffs	= static_cast<signed char>(findMaxElement(attributeCounters.GMTOffs, defaultAttributes.GMTOffs));
	defaultAttributes.HFLAG		= static_cast<unsigned char>(findMaxElement(attributeCounters.HFLAG, defaultAttributes.HFLAG));
	defaultAttributes.XAAttrib	= static_cast<unsigned char>(findMaxElement(attributeCounters.XAAttrib, defaultAttributes.XAAttrib));
	defaultAttributes.XAPerm	= static_cast<unsigned short>(findMaxElement(attributeCounters.XAPerm, defaultAttributes.XAPerm));
	defaultAttributes.GID		= static_cast<unsigned short>(findMaxElement(attributeCounters.GID, defaultAttributes.GID));
	defaultAttributes.UID		= static_cast<unsigned short>(findMaxElement(attributeCounters.UID, defaultAttributes.UID));

	// Write them out to the XML
	defaultAttributesElement->SetAttribute(xml::attrib::GMT_OFFSET, defaultAttributes.GMTOffs);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_ATTRIBUTES, defaultAttributes.XAAttrib);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_PERMISSIONS, defaultAttributes.XAPerm);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_GID, defaultAttributes.GID);
	defaultAttributesElement->SetAttribute(xml::attrib::XA_UID, defaultAttributes.UID);
	if (defaultAttributes.HFLAG) // Set only if not zero
	{
		defaultAttributesElement->SetAttribute(xml::attrib::HIDDEN_FLAG, defaultAttributes.HFLAG);
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

static tinyxml2::XMLElement* WriteXMLEntry(const cd::IsoDirEntries::Entry& entry, tinyxml2::XMLElement* dirElement, fs::path* currentVirtualPath,
	const fs::path& sourcePath, EntryAttributeCounters& attributeCounters)
{
	tinyxml2::XMLElement* newelement;

	if (entry.type == EntryType::EntryDir)
	{
		if (!entry.identifier.empty())
		{
			newelement = dirElement->InsertNewChildElement("dir");
			newelement->SetAttribute(xml::attrib::ENTRY_NAME, entry.identifier.c_str());
			if (param::lba)
			{
				const fs::path outputPath = sourcePath / entry.virtualPath / entry.identifier;
				newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, reinterpret_cast<const char*>(outputPath.generic_u8string().c_str()));
			}
			if (!param::dir)
			{
				newelement->SetAttribute(xml::attrib::ENTRY_DATE, DateToString(entry.entry.entryDate, false).c_str());
			}
		}
		else
		{
			// Root directory
			newelement = dirElement->InsertNewChildElement(xml::elem::DIRECTORY_TREE);
			if (!param::lba)
			{
				newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, reinterpret_cast<const char*>(sourcePath.generic_u8string().c_str()));
			}
			if (!param::dir)
			{
				newelement->SetAttribute(xml::attrib::ENTRY_DATE, DateToString(entry.entry.entryDate, false).c_str());
			}
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
		newelement->SetAttribute(xml::attrib::ENTRY_NAME, entry.identifier.c_str());
		if(entry.type != EntryType::EntryDA)
		{
			if (param::lba)
			{
				const fs::path outputPath = sourcePath / entry.virtualPath / entry.identifier;
				newelement->SetAttribute(xml::attrib::ENTRY_SOURCE, reinterpret_cast<const char*>(outputPath.generic_u8string().c_str()));
			}
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, entry.type == EntryType::EntryFile ? "data" : "mixed");
		}
		else
		{
			newelement->SetAttribute(xml::attrib::TRACK_ID, entry.trackid.c_str());
			newelement->SetAttribute(xml::attrib::ENTRY_TYPE, "da");
		}
		if (!param::dir)
		{
			newelement->SetAttribute(xml::attrib::ENTRY_DATE, DateToString(entry.entry.entryDate, false).c_str());
		}
	}
	if (!param::dir)
	{
		WriteOptionalXMLAttribs(newelement, entry, attributeCounters);
	}
	return dirElement;
}

static void WriteXMLGap(const unsigned int numSectors, tinyxml2::XMLElement* dirElement, const unsigned int startSector)
{
	if (numSectors < 1)
	{
		return;
	}

	cd::SECTOR_M2F1 sector;
	cd::reader->SeekToSector(startSector);
	cd::reader->ReadBytesXA(sector.subHead, XA_DATA_SIZE, true);
	tinyxml2::XMLElement* newelement = dirElement->InsertNewChildElement("dummy");
	newelement->SetAttribute(xml::attrib::NUM_DUMMY_SECTORS, numSectors);
	newelement->SetAttribute(xml::attrib::ENTRY_TYPE, sector.subHead[2]);
	if (param::lba)
	{
		newelement->SetAttribute(xml::attrib::OFFSET, startSector);
	}
}

static void WriteXMLPostGap(const unsigned int postGap, tinyxml2::XMLElement* dirTree, unsigned int& currentLBA)
{
	if (!postGap)
	{
		return;
	}

	// There are some CD-DA games that have a non-zero adrress ECC calculation in the last postgap sector. So, we are checking it.
	// Idk if this behavior could happen in other sectors, but apparently it's only related to CD-DA games postgaps (maybe a bug).
	cd::SECTOR_M2F1 sector;
	cd::reader->SeekToSector(currentLBA + postGap - 1);
	cd::reader->ReadBytesXA(sector.subHead, XA_DATA_SIZE, true);
	if (memcmp(sector.ecc, "\0\0\0\0", sizeof(sector.edc)))
	{
		WriteXMLGap(postGap - 1, dirTree, currentLBA);
		WriteXMLGap(1, dirTree, currentLBA + postGap - 1);
		dirTree->LastChildElement()->SetAttribute(xml::attrib::ECC_ADDRESS, true);
	}
	else
	{
		WriteXMLGap(postGap, dirTree, currentLBA);
	}
	currentLBA += postGap;
}

static void WriteXMLByLBA(const std::list<cd::IsoDirEntries::Entry>& files, tinyxml2::XMLElement* dirElement, const fs::path& sourcePath, unsigned int& expectedLBA,
	EntryAttributeCounters& attributeCounters, const unsigned int postGap)
{
	fs::path currentVirtualPath; // Used to find out whether to traverse 'dir' up or down the chain
	bool writedPostGap = false;
	for (const auto& entry : files)
	{
		// if this is a DA file we are at the end of filesystem
		if (entry.type != EntryType::EntryDA)
		{
			// only check for gaps, update LBA if it's inside the iso filesystem
			if (entry.entry.entryOffs.lsb > expectedLBA)
			{
				WriteXMLGap(entry.entry.entryOffs.lsb - expectedLBA, dirElement, expectedLBA);
			}
			expectedLBA = entry.entry.entryOffs.lsb + GetSizeInSectors(entry.entry.entrySize.lsb);
		}
		else if (entry.trackid.empty())
		{
			continue; // Skip if it's an unreferenced DA file
		}
		else if (!writedPostGap)
		{
			WriteXMLPostGap(postGap, dirElement, expectedLBA);
			writedPostGap = true;
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
			dirElement->SetAttribute(xml::attrib::ENTRY_NAME, part.string().c_str());

			currentVirtualPath /= part;
		}

		dirElement = WriteXMLEntry(entry, dirElement, &currentVirtualPath, sourcePath, attributeCounters);
	}

	if (!writedPostGap)
	{
		WriteXMLPostGap(postGap, dirElement, expectedLBA);
	}
}

static void WriteXMLByDirectories(const cd::IsoDirEntries* directory, tinyxml2::XMLElement* dirElement, const fs::path& sourcePath, unsigned int& expectedLBA,
	EntryAttributeCounters& attributeCounters)
{
	for (const auto& e : directory->dirEntryList.GetView())
	{
		const auto& entry = e.get();

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

unsigned xml::WriteXML(const cd::ISO_DESCRIPTOR& descriptor, const std::unique_ptr<cd::IsoDirEntries>& rootDir, const std::list<cd::IsoDirEntries::Entry*>& DAfiles,
	const unsigned postGap)
{
	unique_file file = OpenScopedFile(param::xmlFile, "wb");
	if (file == nullptr)
	{
		printf("\nERROR: Cannot create xml file \"%" PRFILESYSTEM_PATH "\". %s\n", param::xmlFile.c_str(), strerror(errno));
		exit(EXIT_FAILURE);
	}

	tinyxml2::XMLDocument xmldoc;

	tinyxml2::XMLElement *baseElement = static_cast<tinyxml2::XMLElement*>(xmldoc.InsertFirstChild(xmldoc.NewElement(xml::elem::ISO_PROJECT)));
	baseElement->SetAttribute(xml::attrib::IMAGE_NAME, "mkpsxiso.bin");
	baseElement->SetAttribute(xml::attrib::CUE_SHEET, "mkpsxiso.cue");

	tinyxml2::XMLElement *trackElement = baseElement->InsertNewChildElement(xml::elem::TRACK);
	trackElement->SetAttribute(xml::attrib::TRACK_TYPE, "data");
	trackElement->SetAttribute(xml::attrib::CDVD_STYLE, *global::cdvd_style);
	if (global::ps2)
	{
		trackElement->SetAttribute(xml::attrib::PS2, global::ps2);
	}
	if (!global::xa_edc)
	{
		trackElement->SetAttribute(xml::attrib::XA_EDC, global::xa_edc);
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
		setAttributeIfNotEmpty(xml::attrib::ABSTRACT_FILE, CleanDescElement(descriptor.abstractFileIdentifier));
		setAttributeIfNotEmpty(xml::attrib::BIBLIOGRAPHIC_FILE, CleanDescElement(descriptor.bibliographicFilelIdentifier));
		setAttributeIfNotEmpty(xml::attrib::CREATION_DATE, LongDateToString(descriptor.volumeCreateDate).c_str());
		if (auto ZERO_DATE = GetUnspecifiedLongDate(); memcmp(&descriptor.volumeModifyDate, &ZERO_DATE, sizeof(descriptor.volumeModifyDate)) != 0)
		{
			// Set only if not zero
			setAttributeIfNotEmpty(xml::attrib::MODIFICATION_DATE, LongDateToString(descriptor.volumeModifyDate).c_str());
		}
	}

	const fs::path outPath = fs::absolute(param::outPath).lexically_normal();
	const fs::path xmlPath = fs::absolute(param::xmlFile).lexically_normal().parent_path();
	const fs::path srcPath = param::xmlFile.is_absolute() ? outPath : outPath.lexically_proximate(xmlPath);

	// Add license element to the xml
	{
		if (!global::licenseFile.empty())
		{
			global::licenseFile = reinterpret_cast<const char*>((srcPath / global::licenseFile).generic_u8string().c_str());
		}
		tinyxml2::XMLElement *newElement = trackElement->InsertNewChildElement(xml::elem::LICENSE);
		newElement->SetAttribute(xml::attrib::LICENSE_FILE, global::licenseFile.c_str());
	}

	// Create <default_attributes> now so it lands before the directory tree
	tinyxml2::XMLElement* defaultAttributesElement = !param::dir ? trackElement->InsertNewChildElement(xml::elem::DEFAULT_ATTRIBUTES) : nullptr;

	EntryAttributeCounters attributeCounters;
	unsigned currentLBA = descriptor.rootDirRecord.entryOffs.lsb;
	if (param::outputSortedByDir)
	{
		WriteXMLByDirectories(rootDir.get(), trackElement, srcPath, currentLBA, attributeCounters);
		WriteXMLPostGap(postGap, trackElement->LastChildElement(), currentLBA);
	}
	else
	{
		WriteXMLByLBA(rootDir->dirEntryList.GetUnderlyingList(), trackElement, srcPath, currentLBA, attributeCounters, postGap);
	}

	if (defaultAttributesElement != nullptr)
	{
		tinyxml2::XMLElement *dirtree = trackElement->FirstChildElement(xml::elem::DIRECTORY_TREE);
		SimplifyDefaultXMLAttributes(dirtree, EstablishXMLAttributeDefaults(defaultAttributesElement, attributeCounters));
	}

	// Write CD-DA tracks
	tinyxml2::XMLNode *modifyProject = trackElement->Parent();
	tinyxml2::XMLElement *addAfter = trackElement;
	for(const auto& dafile : DAfiles)
	{
		// SYSTEM DESCRIPTION CD-ROM XA Ch.II 2.3, pause should be always >= 150 sectors.
		unsigned pregap_sectors = 150;
		dafile->virtualPath = GetEncodedDAPath(srcPath / dafile->virtualPath / dafile->identifier);
		if(dafile->entry.entryOffs.lsb != currentLBA)
		{
			pregap_sectors = dafile->entry.entryOffs.lsb - currentLBA;
			currentLBA += pregap_sectors;
		}
		currentLBA += GetSizeInSectors(dafile->entry.entrySize.lsb);
		tinyxml2::XMLElement *newtrack = xmldoc.NewElement(xml::elem::TRACK);
		newtrack->SetAttribute(xml::attrib::TRACK_TYPE, "audio");
		if (!dafile->trackid.empty())
		{
			newtrack->SetAttribute(xml::attrib::TRACK_ID, dafile->trackid.c_str());
		}
		newtrack->SetAttribute(xml::attrib::TRACK_SOURCE, reinterpret_cast<const char*>(dafile->virtualPath.generic_u8string().c_str()));
		// only write the pregap element if it's non default
		if(pregap_sectors != 150)
		{
			tinyxml2::XMLElement *pregap = newtrack->InsertNewChildElement(xml::elem::TRACK_PREGAP);
			pregap->SetAttribute(xml::attrib::PREGAP_DURATION, SectorsToTimecode(pregap_sectors).c_str());
		}

		modifyProject->InsertAfterChild(addAfter, newtrack);
		addAfter = newtrack;
	}

	xmldoc.SaveFile(file.get());
	return currentLBA;
}
