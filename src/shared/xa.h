#pragma once

// CD-XA extension namespace
// As per "System Description CD-ROM XA" by NV Philips and Sony Corporation.
namespace cdxa {

	// XA attribute struct (located right after the identifier string)
	// Fields are big endian
	struct ISO_XA_ATTRIB {
		unsigned short	ownergroupid;	// Usually 0x0000
		unsigned short	owneruserid;	// Usually 0x0000
		unsigned short	attributes;
		char id[2];
		unsigned char	filenum;		// Usually 0x00
		unsigned char	reserved[5];
	};

	// Masks for ISO_XA_ATTRIB.attributes
	constexpr unsigned short XA_PERMISSIONS_MASK = 0x7FF;
	constexpr unsigned short XA_ATTRIBUTES_MASK = ~XA_PERMISSIONS_MASK;
}