#pragma once

#include <tinyxml2.h>

// Shared XML element and attribute names
namespace xml
{

namespace elem
{
	constexpr char* ISO_PROJECT = "iso_project";
	constexpr char* IDENTIFIERS = "identifiers";
	constexpr char* LICENSE = "license";
	constexpr char* TRACK = "track";
	constexpr char* DIRECTORY_TREE = "directory_tree";
}

namespace attrib
{
	constexpr char* IMAGE_NAME = "image_name";
	constexpr char* CUE_SHEET = "cue_sheet";
	constexpr char* NO_XA = "no_xa";

	constexpr char* TRACK_TYPE = "type";
	constexpr char* TRACK_SOURCE = "source";

	constexpr char* ENTRY_NAME = "name";
	constexpr char* ENTRY_SOURCE = "source";
	constexpr char* ENTRY_TYPE = "type";

	constexpr char* LICENSE_FILE = "file";

	constexpr char* GMT_OFFSET = "gmt_offs";
	constexpr char* XA_ATTRIBUTES = "xa_attrib";
	constexpr char* XA_PERMISSIONS = "xa_perm";
	constexpr char* XA_GID = "xa_gid";
	constexpr char* XA_UID = "xa_uid";

	constexpr char* SYSTEM_ID = "system";
	constexpr char* VOLUME_ID = "volume";
	constexpr char* APPLICATION = "application";
	constexpr char* VOLUME_SET = "volume_set";
	constexpr char* PUBLISHER = "publisher";
	constexpr char* DATA_PREPARER = "data_preparer";
	constexpr char* COPYRIGHT = "copyright";
	constexpr char* CREATION_DATE = "creation_date";
	constexpr char* MODIFICATION_DATE = "modification_date";
}

}