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
	constexpr const char* XA_EDC = "xa_edc"; // Simulates the Form2 EDC absence bug, used for 1:1 rebuilds.
	constexpr const char* CDVD_STYLE = "cdvd_style";
	constexpr const char* PS2 = "ps2";
	constexpr const char* TRACK_SOURCE = "source";
	constexpr const char* TRACK_ID = "trackid";
	constexpr const char* PREGAP_DURATION = "duration";

	constexpr const char* ENTRY_NAME = "name";
	constexpr const char* ENTRY_SOURCE = "source";
	constexpr const char* ENTRY_TYPE = "type";
	constexpr const char* ENTRY_DATE = "date";
	constexpr const char* ORDER = "order"; // Explicit Directory Record order to match CDVDGEN mastered dumps, used for 1:1 rebuilds.
	constexpr const char* OFFSET = "offs"; // Forces a specific LBA. Mostly used for debugging, as offsets are auto-calculated based on XML layout.

	constexpr const char* LICENSE_FILE = "file";

	constexpr const char* GMT_OFFSET = "gmt_offs"; // Explicit timezone offset, used for 1:1 rebuilds.
	constexpr const char* HIDDEN_FLAG = "hidden";
	constexpr const char* XA_ATTRIBUTES = "xa_attrib"; // Explicit CD-XA attributes, used for 1:1 rebuilds.
	constexpr const char* XA_PERMISSIONS = "xa_perm"; // Explicit CD-XA permissions, used for 1:1 rebuilds.
	constexpr const char* XA_GID = "xa_gid"; // Explicit CD-XA Group ID, used for 1:1 rebuilds.
	constexpr const char* XA_UID = "xa_uid"; // Explicit CD-XA User ID, used for 1:1 rebuilds.

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
	constexpr const char* ABSTRACT_FILE = "abstract_file";
	constexpr const char* BIBLIOGRAPHIC_FILE = "bibliographic_file";

	constexpr const char* NUM_DUMMY_SECTORS = "sectors";
	constexpr const char* ECC_ADDRESS = "ecc_addr"; // Simulates the real address ECC calculation bug, used for 1:1 rebuilds.
}

}
