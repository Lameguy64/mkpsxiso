#pragma once

#include <tinyxml2.h>

// Shared XML element and attribute names
namespace xml
{

namespace elem
{
	constexpr const char* ISO_PROJECT = "iso_project";
	constexpr const char* IDENTIFIERS = "identifiers";
	constexpr const char* LICENSE = "license";
	constexpr const char* DEFAULT_ATTRIBUTES = "default_attributes";
	constexpr const char* TRACK = "track";
	constexpr const char* DIRECTORY_TREE = "directory_tree";
	constexpr const char* FILE = "file";
	constexpr const char* TRACK_PREGAP = "pregap";
}

namespace attrib
{
	constexpr const char* IMAGE_NAME = "image_name";
	constexpr const char* CUE_SHEET = "cue_sheet";
	constexpr const char* NO_XA = "no_xa";

	constexpr const char* TRACK_TYPE = "type";
	constexpr const char* TRACK_SOURCE = "source";
	constexpr const char* TRACK_ID = "trackid";
	constexpr const char* PREGAP_DURATION = "duration";

	constexpr const char* ENTRY_NAME = "name";
	constexpr const char* ENTRY_SOURCE = "source";
	constexpr const char* ENTRY_TYPE = "type";

	constexpr const char* LICENSE_FILE = "file";

	constexpr const char* GMT_OFFSET = "gmt_offs";
	constexpr const char* XA_ATTRIBUTES = "xa_attrib";
	constexpr const char* XA_PERMISSIONS = "xa_perm";
	constexpr const char* XA_GID = "xa_gid";
	constexpr const char* XA_UID = "xa_uid";

	constexpr const char* ID_FILE = "id_file";
	constexpr const char* SYSTEM_ID = "system";
	constexpr const char* VOLUME_ID = "volume";
	constexpr const char* APPLICATION = "application";
	constexpr const char* VOLUME_SET = "volume_set";
	constexpr const char* PUBLISHER = "publisher";
	constexpr const char* DATA_PREPARER = "data_preparer";
	constexpr const char* COPYRIGHT = "copyright";
	constexpr const char* CREATION_DATE = "creation_date";
	constexpr const char* MODIFICATION_DATE = "modification_date";

	constexpr const char* NUM_DUMMY_SECTORS = "sectors";
}

}
