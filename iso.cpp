#include "global.h"
#include "iso.h"

iso::DirTreeClass::DirTreeClass() {

	entries = (DIRENTRY*)malloc(sizeof(DIRENTRY));
	numentries = 0;
	recordLBA = 0;

}

iso::DirTreeClass::~DirTreeClass() {

	for(int i=0; i<numentries; i++) {

		if (entries[i].id != NULL)
			free(entries[i].id);

		if (entries[i].srcfile != NULL)
			free(entries[i].srcfile);

		if (entries[i].subdir != NULL)
			delete((DirTreeClass*)entries[i].subdir);

	}

	free(entries);

}

int	iso::DirTreeClass::AddFileEntry(const char* id, int type, const char* srcfile) {

	struct stat fileAttrib;

    if (stat(srcfile, &fileAttrib) != 0)
		return false;

	if (((type == EntryXA) || (type == EntrySTR)) && ((fileAttrib.st_size % 2336) != 0)) {
		printf("      WARNING: %s is not a multiple of 2336 bytes for XA/STR.\n", srcfile);
	}


	if (numentries > 0)
		entries = (DIRENTRY*)realloc(entries, sizeof(DIRENTRY)*(numentries+1));


	char tempName[strlen(id)+2];

	strcpy(tempName, id);

	for(int i=0; tempName[i] != 0x00; i++)
		tempName[i] = toupper(tempName[i]);

	strcat(tempName, ";1");

	memset(&entries[numentries], 0x00, sizeof(DIRENTRY));

	entries[numentries].id		= strdup(tempName);
	entries[numentries].type	= type;
	entries[numentries].subdir	= NULL;

	if (srcfile != NULL)
		entries[numentries].srcfile = strdup(srcfile);

	if (type != EntryDir)
		entries[numentries].length	= fileAttrib.st_size;

    tm* fileTime = gmtime(&fileAttrib.st_mtime);

    entries[numentries].date.hour	= fileTime->tm_hour;
    entries[numentries].date.minute	= fileTime->tm_min;
    entries[numentries].date.second	= fileTime->tm_sec;
    entries[numentries].date.day	= fileTime->tm_mday;
    entries[numentries].date.month	= fileTime->tm_mon+1;
    entries[numentries].date.year	= fileTime->tm_year;
    entries[numentries].date.GMToffs = 0;

	numentries++;

	return true;

}

void iso::DirTreeClass::AddDummyEntry(int sectors) {

	if (numentries > 0)
		entries = (DIRENTRY*)realloc(entries, sizeof(DIRENTRY)*(numentries+1));


	entries[numentries].id		= NULL;
	entries[numentries].srcfile	= NULL;
	entries[numentries].subdir	= NULL;
	entries[numentries].type	= EntryFile;
	entries[numentries].length	= 2048*sectors;

	numentries++;

}

iso::DirTreeClass* iso::DirTreeClass::AddSubDirEntry(const char* id) {

	if (numentries > 0) {

		entries = (DIRENTRY*)realloc(entries, sizeof(DIRENTRY) * (numentries + 1));

	}

	memset(&entries[numentries], 0x00, sizeof(DIRENTRY));

	entries[numentries].id		= strdup(id);
	entries[numentries].type	= EntryDir;
	entries[numentries].length	= 2048;
	entries[numentries].subdir	= (void*) new DirTreeClass;

	tm*	dirTime = gmtime(&global::BuildTime);

	entries[numentries].date.hour	= dirTime->tm_hour;
	entries[numentries].date.minute	= dirTime->tm_min;
	entries[numentries].date.second	= dirTime->tm_sec;
	entries[numentries].date.month	= dirTime->tm_mon+1;
	entries[numentries].date.day	= dirTime->tm_mday;
	entries[numentries].date.year	= dirTime->tm_year;
	entries[numentries].date.GMToffs = 0;

	for(int i=0; entries[numentries].id[i] != 0x00; i++)
		entries[numentries].id[i] = toupper(entries[numentries].id[i]);

	numentries++;

	return (DirTreeClass*)entries[numentries-1].subdir;

}

int iso::DirTreeClass::CalculateTreeLBA(int lba) {

	// Set LBA of directory record of this class
	recordLBA = lba;

	lba += 1;

	for(int i=0; i<numentries; i++) {

		// Set current LBA to directory record entry
		entries[i].lba = lba;

		// If it is a subdir
		if (entries[i].subdir != NULL) {

			// Recursively calculate the LBA of subdirectories
			lba = ((DirTreeClass*)entries[i].subdir)->CalculateTreeLBA(lba);

		} else {

			// Increment LBA by the size of file
			if (entries[i].type == EntryFile)
				lba += (entries[i].length+2047)/2048;
			else if ((entries[i].type == EntryXA) || (entries[i].type == EntrySTR))
				lba += (entries[i].length+2335)/2336;

		}

	}


	return lba;

}

int iso::DirTreeClass::CalculateDirEntryLen() {

	int dirEntryLen = 96;

	for(int i=0; i<numentries; i++) {

		if (entries[i].id == NULL)
			continue;

		int dataLen = 33;

		dataLen += 2*((strlen(entries[i].id)+1)/2);

		if ((strlen(entries[i].id)%2) == 0)
			dataLen++;

		dataLen += sizeof(cd::ISO_XA_ATTRIB);

		dirEntryLen += dataLen;

	}

	return dirEntryLen;

}

void iso::DirTreeClass::SortDirEntries() {

	iso::DIRENTRY temp;
	int numdummies=0;

	if (numentries < 2)
		return;

	// Search for directories
	for(int i=0; i<numentries; i++) {

		if (entries[i].type == EntryDir) {

			// Perform recursive call
            if (entries[i].subdir != NULL)
				((iso::DirTreeClass*)entries[i].subdir)->SortDirEntries();

		} else {

			if (entries[i].id == NULL)
				numdummies++;

		}


	}


	// Sort dummies to end of list
	for(int i=1; i<numentries; i++) {
		for(int j=1; j<numentries; j++) {

			if ((entries[j-1].id == NULL) && (entries[j].id != NULL)) {

				temp = entries[j];
				entries[j] = entries[j-1];
				entries[j-1] = temp;

			}

		}
	}

	// Now sort the entries
	for(int i=1; i<numentries-numdummies; i++) {
		for(int j=1; j<numentries-numdummies; j++) {

			if (strcmp(entries[j-1].id, entries[j].id) > 0) {

				temp = entries[j-1];
				entries[j-1] = entries[j];
				entries[j] = temp;

			}

		}
	}


	/*
	int numdirs=0,numfiles=0;
	// Sort directories to be first in list
	for(int i=1; i<numentries; i++) {
		for(int j=1; j<numentries; j++) {

			if ((entries[j-1].type != EntryDir) && (entries[j].type == EntryDir)) {

				temp = entries[j-1];
				entries[j-1] = entries[j];
				entries[j] = temp;

			}

		}
	}

	if (numdirs > 1) {

		// Sort the directory entries
		for(int i=1; i<numdirs; i++) {
			for(int j=1; j<numdirs; j++) {

				if (strcmp(entries[j-1].id, entries[j].id) > 0) {

					temp = entries[j-1];
					entries[j-1] = entries[j];
					entries[j] = temp;

				}

			}
		}

	}


	// Count number of files
	for(int i=0; i<numentries; i++) {

		if ((entries[i].type != EntryDir) && (entries[i].id != NULL))
			numfiles++;

	}


	// Now sort the files
	if (numfiles > 1) {

		for(int i=numdirs+1; i<(numdirs+numfiles); i++) {
			for(int j=numdirs+1; j<(numdirs+numfiles); j++) {

				if (strcmp(entries[j-1].id, entries[j].id) > 0) {

					temp = entries[j-1];
					entries[j-1] = entries[j];
					entries[j] = temp;

				}

			}
		}

	}
	*/

}

int iso::DirTreeClass::WriteDirEntries(cd::IsoWriter* writer, int lastLBA) {

	char	dataBuff[2048];
	char*	dataBuffPtr=dataBuff;

	cd::ISO_DIR_ENTRY*	entry;
	cd::ISO_XA_ATTRIB*	xa;

	memset(dataBuff, 0x00, 2048);

	for(int i=0; i<2; i++) {

		entry = (cd::ISO_DIR_ENTRY*)dataBuffPtr;

		SetPair32(&entry->entrySize, 2048);
		SetPair16(&entry->volSeqNum, 1);
		entry->identifierLen = 1;

		int dataLen = 32;

		if (i == 0) {

			SetPair32(&entry->entryOffs, recordLBA);

		} else {

			SetPair32(&entry->entryOffs, lastLBA);
			dataBuffPtr[dataLen+1] = 0x01;

		}

		dataLen += 2;

		xa = (cd::ISO_XA_ATTRIB*)(dataBuffPtr+dataLen);

		xa->id[0] = 'X';
		xa->id[1] = 'A';
		xa->type  = 0x8800;

		dataLen += sizeof(cd::ISO_XA_ATTRIB);

		entry->flags = 0x02;
		entry->entryLength = dataLen;

		tm* dirTime = gmtime(&global::BuildTime);

		entry->entryDate.year	= dirTime->tm_year;
		entry->entryDate.month	= dirTime->tm_mon+1;
		entry->entryDate.day	= dirTime->tm_mday;
		entry->entryDate.hour	= dirTime->tm_hour;
		entry->entryDate.minute	= dirTime->tm_min;
		entry->entryDate.second	= dirTime->tm_sec;

		dataBuffPtr += dataLen;

	}

	for(int i=0; i<numentries; i++) {

		if (entries[i].id == NULL)
			continue;

		entry = (cd::ISO_DIR_ENTRY*)dataBuffPtr;

		if (entries[i].type == EntryDir) {

			entries[i].length = 0x800; //((DirTreeClass*)entries[i].subdir)->CalculateDirEntryLen();
			entry->flags = 0x02;

		} else {

			entry->flags = 0x00;

		}

		SetPair32(&entry->entryOffs, entries[i].lba);
		SetPair32(&entry->entrySize, entries[i].length);
		SetPair16(&entry->volSeqNum, 1);

		entry->identifierLen = strlen(entries[i].id);
		entry->entryDate = entries[i].date;

		int dataLen = 33;

		strncpy(&dataBuffPtr[dataLen], entries[i].id, entry->identifierLen);
		dataLen += 2*((entry->identifierLen+1)/2);

		if ((entry->identifierLen%2) == 0)
			dataLen++;

		xa = (cd::ISO_XA_ATTRIB*)(dataBuffPtr+dataLen);

		xa->id[0] = 'X';
		xa->id[1] = 'A';

		if (entries[i].type == EntryFile)
			xa->type  = 0x0800;
		else if (entries[i].type == EntryDir)
			xa->type  = 0x8800;

		dataLen += sizeof(cd::ISO_XA_ATTRIB);
		entry->entryLength = dataLen;

		dataBuffPtr += dataLen;

	}

	if ((int)(dataBuffPtr-dataBuff) > 2048)
		return 0;

	writer->SeekToSector(recordLBA);
	writer->WriteBytes(dataBuff, 2048, cd::IsoWriter::EdcEccForm1);

	return 1;

}

int iso::DirTreeClass::WriteDirectoryRecords(cd::IsoWriter* writer, int lastDirLBA) {

	if (lastDirLBA == 0)
		lastDirLBA = recordLBA;

	WriteDirEntries(writer, lastDirLBA);

	for(int i=0; i<numentries; i++) {

		if (entries[i].type == EntryDir) {

			if (!((DirTreeClass*)entries[i].subdir)->WriteDirectoryRecords(writer, recordLBA))
				return 0;

		}

	}

	return 1;

}

int iso::DirTreeClass::WriteFiles(cd::IsoWriter* writer) {

	for(int i=0; i<numentries; i++) {

		writer->SeekToSector(entries[i].lba);

		// Write files and dummies as regular data sectors
		if (entries[i].type == EntryFile) {

			char buff[2048];

			if (entries[i].srcfile != NULL) {

				if (!global::QuietMode)
					printf("      Packing %s... ", entries[i].srcfile);

				FILE *fp = fopen(entries[i].srcfile, "rb");

				while(!feof(fp)) {

					memset(buff, 0x00, 2048);

					fread(buff, 1, 2048, fp);
					writer->WriteBytes(buff, 2048, cd::IsoWriter::EdcEccForm1);

				}

				fclose(fp);

				if (!global::QuietMode)
					printf("Done.\n");

			} else {

				memset(buff, 0x00, 2048);

				for(int c=0; c<(entries[i].length/2048); c++) {

					writer->WriteBytes(buff, 2048, cd::IsoWriter::EdcEccForm1);

				}

			}

		// Write XA audio streams as Mode 2 Form 2 sectors without ECC
		} else if (entries[i].type == EntryXA) {

			char buff[2336];

			if (!global::QuietMode)
				printf("      Packing XA %s... ", entries[i].srcfile);

			FILE *fp = fopen(entries[i].srcfile, "rb");

			while(!feof(fp)) {

				memset(buff, 0x00, 2336);

				fread(buff, 1, 2336, fp);
				writer->WriteBytesXA(buff, 2336, cd::IsoWriter::EdcEccNone);

			}

			fclose(fp);

			if (!global::QuietMode)
				printf("Done.\n");

		// Write STR video streams as Mode 2 Form 1 (video sectors) and Mode 2 Form 2 (XA audio sectors)
		// Video sectors have EDC/ECC while XA does not
		} else if (entries[i].type == EntrySTR) {

			char buff[2336];

			if (!global::QuietMode)
				printf("      Packing STR %s... ", entries[i].srcfile);

			FILE *fp = fopen(entries[i].srcfile, "rb");

			while(!feof(fp)) {

				memset(buff, 0x00, 2336);
				fread(buff, 1, 2336, fp);

				// Check if sector is a video sector
				if (buff[2] == 0x48)
					writer->WriteBytesXA(buff, 2336, cd::IsoWriter::EdcEccForm1);	// If so, write it as Mode 2 Form 1
				else
					writer->WriteBytesXA(buff, 2336, cd::IsoWriter::EdcEccNone);	// Otherwise, write it as an XA track

			}

			fclose(fp);

			if (!global::QuietMode)
				printf("Done.\n");

		} else if (entries[i].type == EntryDir) {

			((DirTreeClass*)entries[i].subdir)->WriteFiles(writer);

		}

	}

	return 1;

}

void iso::DirTreeClass::PrintEntries() {

	printf("RECORD LBA : %d\n", recordLBA);
	for(int i=0; i<numentries; i++) {

		if (entries[i].id != NULL)
			printf("N:%s ", entries[i].id);
		else
			printf("N:<DUMMY> ");

		printf("L:%d LBA:%d T:%d S:%s\n", entries[i].length, entries[i].lba, entries[i].type, entries[i].srcfile);

		if (entries[i].subdir != NULL) {

			((DirTreeClass*)entries[i].subdir)->PrintEntries();

		}

	}

}


int iso::DirTreeClass::CalculatePathTableLenSub(DIRENTRY* dirEntry) {

	int len = 8;

	// Put identifier (NULL if first entry)
	len += 2*((strlen(dirEntry->id)+1)/2);

	for(int i=0; i<((DirTreeClass*)dirEntry->subdir)->numentries; i++) {

		if (((DirTreeClass*)dirEntry->subdir)->entries[i].type == EntryDir) {

			len += CalculatePathTableLenSub(&((DirTreeClass*)dirEntry->subdir)->entries[i]);

		}

	}

	return len;

}

int iso::DirTreeClass::CalculatePathTableLen() {

	int len = 10;

	for(int i=0; i<numentries; i++) {

		if (entries[i].type == EntryDir) {

			len += CalculatePathTableLenSub(&entries[i]);

		}

	}

	return len;

}

u_char* iso::DirTreeClass::GenPathTableSub(u_char* buff, DIRENTRY* dirEntry, int parentIndex, int msb) {

	*buff = strlen(dirEntry->id);	// Directory identifier length
	buff++;
	*buff = 0;						// Extended attribute record length (unused)
	buff++;

	// Write LBA and directory number index
	dirIndex++;
	*((int*)buff) = ((DirTreeClass*)dirEntry->subdir)->recordLBA;
	memcpy(buff+4, &parentIndex, 2);

	if (msb) {

		cd::SwapBytes(buff, 4);
		cd::SwapBytes(buff+4, 2);

	}

	buff += 6;

	// Put identifier (NULL if first entry)
	strncpy((char*)buff, dirEntry->id, strlen(dirEntry->id));
	buff += 2*((strlen(dirEntry->id)+1)/2);

	parentIndex = dirIndex;

	if (dirEntry->subdir != NULL) {

		for(int i=0; i<((DirTreeClass*)dirEntry->subdir)->numentries; i++) {

			if (((DirTreeClass*)dirEntry->subdir)->entries[i].type == EntryDir) {

				buff = GenPathTableSub(buff, &((DirTreeClass*)dirEntry->subdir)->entries[i], parentIndex, msb);

			}

		}

	}

	return buff;

}

int iso::DirTreeClass::GeneratePathTable(u_char* buff, int msb) {

	u_char* oldBuffPtr = buff;

	*buff = 1;	// Directory identifier length
	buff++;
	*buff = 0;	// Extended attribute record length (unused)
	buff++;

	int lba = recordLBA;

	// Write LBA and directory number index
	dirIndex = 1;
	memcpy(buff, &lba, 4);
	memcpy(buff+4, &dirIndex, 2);

	if (msb) {
		cd::SwapBytes(buff, 4);
		cd::SwapBytes(buff+4, 2);
	}

	buff += 6;

	// Put identifier (NULL if first entry)
	memset(buff, 0x00, 2);
	buff += 2;

	for(int i=0; i<numentries; i++) {

		if (entries[i].type == EntryDir) {

			buff = GenPathTableSub(buff, &entries[i], dirIndex, msb);

		}

		if ((int)(buff-oldBuffPtr) > 2048)
			break;

	}

	return (int)(buff-oldBuffPtr);

}

int iso::DirTreeClass::GetFileCountTotal() {

    int numfiles = 0;

    for(int i=0; i<numentries; i++) {

        if ((entries[i].type == EntryFile) || (entries[i].type == EntryXA) || (entries[i].type == EntrySTR)) {

			if (entries[i].id != NULL)
				numfiles++;

		} else {

            numfiles += ((DirTreeClass*)entries[i].subdir)->GetFileCountTotal();

		}

    }

    return numfiles;

}

int iso::DirTreeClass::GetDirCountTotal() {

	int numdirs = 0;

    for(int i=0; i<numentries; i++) {

        if (entries[i].type == EntryDir) {

			numdirs++;
            numdirs += ((DirTreeClass*)entries[i].subdir)->GetDirCountTotal();

		}

    }

    return numdirs;

}

void iso::WriteLicenseData(cd::IsoWriter* writer, void* data) {

	writer->SeekToSector(0);
	writer->WriteBytesXA(data, 2336*12, cd::IsoWriter::EdcEccForm1);

}

void iso::WriteDescriptor(cd::IsoWriter* writer, iso::IDENTIFIERS id, iso::DirTreeClass* dirTree, int imageLen) {


	cd::ISO_DESCRIPTOR	isoDescriptor;

	memset(&isoDescriptor, 0x00, sizeof(cd::ISO_DESCRIPTOR));

	isoDescriptor.header.type = 1;
	isoDescriptor.header.version = 1;
	strncpy((char*)isoDescriptor.header.id, "CD001", 5);

	// Set System identifier
	memset(isoDescriptor.systemID, 0x20, 32);
	if (id.SystemID != NULL) {

		for(int i=0; i<(int)strlen(id.SystemID); i++)
			isoDescriptor.systemID[i] = toupper(id.SystemID[i]);

	}

	// Set Volume identifier
	memset(isoDescriptor.volumeID, 0x20, 32);
	if (id.VolumeID != NULL) {

		for(int i=0; i<(int)strlen(id.VolumeID); i++)
			isoDescriptor.volumeID[i] = toupper(id.VolumeID[i]);

	}

	// Set Application identifier
	memset(isoDescriptor.applicationIdentifier, 0x20, 128);
	if (id.Application != NULL) {

		for(int i=0; i<(int)strlen(id.Application); i++)
			isoDescriptor.applicationIdentifier[i] = toupper(id.Application[i]);

	}

	// Volume Set identifier
	memset(isoDescriptor.volumeSetIdentifier, 0x20, 128);
	if (id.VolumeSet != NULL) {

		for(int i=0; i<(int)strlen(id.VolumeSet); i++)
			isoDescriptor.volumeSetIdentifier[i] = toupper(id.VolumeSet[i]);

	}

	// Publisher identifier
	memset(isoDescriptor.publisherIdentifier, 0x20, 128);
	if (id.Publisher != NULL) {

		for(int i=0; i<(int)strlen(id.Publisher); i++)
			isoDescriptor.publisherIdentifier[i] = toupper(id.Publisher[i]);

	}

	// Data preparer identifier
	memset(isoDescriptor.dataPreparerIdentifier, 0x20, 128);
	strcpy(isoDescriptor.dataPreparerIdentifier, "THIS DISC IMAGE WAS CREATED USING MKPSXISO BY LAMEGUY64 OF MEIDO-TEK PRODUCTIONS HTTPS://GITHUB.COM/LAMEGUY64/MKPSXISO");
	*strchr(isoDescriptor.dataPreparerIdentifier, 0x00) = 0x20;
	if (id.DataPreparer != NULL) {

		for(int i=0; i<(int)strlen(id.DataPreparer); i++)
			isoDescriptor.dataPreparerIdentifier[i] = toupper(id.DataPreparer[i]);

	}

	// Unneeded identifiers
	memset(isoDescriptor.copyrightFileIdentifier, 0x20, 37);
	memset(isoDescriptor.abstractFileIdentifier, 0x20, 37);
	memset(isoDescriptor.bibliographicFilelIdentifier, 0x20, 37);


	tm* imageTime = localtime(&global::BuildTime);

	sprintf(isoDescriptor.volumeCreateDate, "%04d%02d%02d%02d%02d%02d00",
		imageTime->tm_year+1900,
		imageTime->tm_mon,
		imageTime->tm_mday,
		imageTime->tm_hour,
		imageTime->tm_min,
		imageTime->tm_sec
	);

	sprintf(isoDescriptor.volumeModifyDate, "%04d%02d%02d%02d%02d%02d00",
		imageTime->tm_year+1900,
		imageTime->tm_mon,
		imageTime->tm_mday,
		imageTime->tm_hour,
		imageTime->tm_min,
		imageTime->tm_sec
	);

	strcpy(isoDescriptor.volumeEffeciveDate, "0000000000000000");
	strcpy(isoDescriptor.volumeExpiryDate, "0000000000000000");

	isoDescriptor.fileStructVersion = 1;
	strncpy((char*)&isoDescriptor.appData[141], "CD-XA001", 8);

	cd::SetPair16(&isoDescriptor.volumeSetSize, 1);
	cd::SetPair16(&isoDescriptor.volumeSeqNumber, 1);
	cd::SetPair16(&isoDescriptor.sectorSize, 2048);
	cd::SetPair32(&isoDescriptor.pathTableSize, dirTree->CalculatePathTableLen());

	// Setup the root directory record
	isoDescriptor.rootDirRecord.entryLength = 34;
	isoDescriptor.rootDirRecord.extLength	= 0;
	cd::SetPair32(&isoDescriptor.rootDirRecord.entryOffs, 22);
	cd::SetPair32(&isoDescriptor.rootDirRecord.entrySize, 0x800);
	isoDescriptor.rootDirRecord.flags = 0x02;
	cd::SetPair16(&isoDescriptor.rootDirRecord.volSeqNum, 1);
	isoDescriptor.rootDirRecord.identifierLen = 1;
	isoDescriptor.rootDirRecord.identifier = 0x01;

	isoDescriptor.pathTable1Offs = 18;
	isoDescriptor.pathTable2Offs = 19;
	isoDescriptor.pathTable1MSBoffs = 20;
	cd::SwapBytes(&isoDescriptor.pathTable1MSBoffs, 4);
	isoDescriptor.pathTable2MSBoffs = 21;
	cd::SwapBytes(&isoDescriptor.pathTable2MSBoffs, 4);

	SetPair32(&isoDescriptor.volumeSize, imageLen);

	// Write the descriptor
	writer->SeekToSector(16);
	writer->WriteBytes(&isoDescriptor, sizeof(cd::ISO_DESCRIPTOR), cd::IsoWriter::EdcEccForm1);


	// Write descriptor terminator;
	memset(&isoDescriptor, 0x00, sizeof(cd::ISO_DESCRIPTOR));
	isoDescriptor.header.type = 255;
	isoDescriptor.header.version = 1;
	strncpy(isoDescriptor.header.id, "CD001", 5);

	writer->WriteBytes(&isoDescriptor, sizeof(cd::ISO_DESCRIPTOR), cd::IsoWriter::EdcEccForm1);


	// Write path table
	u_char sectorBuff[2048];
	memset(sectorBuff, 0x00, 2048);

	dirTree->GeneratePathTable(sectorBuff, false);
	writer->WriteBytes(sectorBuff, 2048, cd::IsoWriter::EdcEccForm1);
	writer->WriteBytes(sectorBuff, 2048, cd::IsoWriter::EdcEccForm1);

	dirTree->GeneratePathTable(sectorBuff, true);
	writer->WriteBytes(sectorBuff, 2048, cd::IsoWriter::EdcEccForm1);
	writer->WriteBytes(sectorBuff, 2048, cd::IsoWriter::EdcEccForm1);

}
