#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <string.h>
#include <cassert>
#include "common.h"
#include "cdwriter.h"
#include "edcecc.h"
#include "platform.h"

using namespace cd;



static const EDCECC EDC_ECC_GEN;

ISO_USHORT_PAIR cd::SetPair16(unsigned short val) {
    return { val, SwapBytes16(val) };
}

ISO_UINT_PAIR cd::SetPair32(unsigned int val) {
	return { val, SwapBytes32(val) };
}


IsoWriter::IsoWriter() {

	IsoWriter::filePtr			= nullptr;
	IsoWriter::sectorM2F1		= nullptr;
	IsoWriter::sectorM2F2		= nullptr;
	IsoWriter::currentByte		= 0;
	IsoWriter::currentSector	= 0;
	IsoWriter::bytesWritten		= 0;
	IsoWriter::lastSectorType	= 0;

	memset(IsoWriter::sectorBuff, 0, CD_SECTOR_SIZE);

}

IsoWriter::~IsoWriter() {

	Close();
}

void IsoWriter::PrepSector(int edcEccMode) {

    memset(IsoWriter::sectorM2F1->sync, 0xff, 12);
    IsoWriter::sectorM2F1->sync[0]	= 0x00;
    IsoWriter::sectorM2F1->sync[11]	= 0x00;

	int taddr,addr = 150+IsoWriter::currentSector;	// Sector addresses always start at LBA 150

	taddr = addr%75;
	IsoWriter::sectorM2F1->addr[2] = (16*(taddr/10))+(taddr%10);

	taddr = (addr/75)%60;
	IsoWriter::sectorM2F1->addr[1] = (16*(taddr/10))+(taddr%10);

	taddr = (addr/75)/60;
	IsoWriter::sectorM2F1->addr[0] = (16*(taddr/10))+(taddr%10);

	IsoWriter::sectorM2F1->mode = 0x02;

	/*if (edcEccMode == IsoWriter::EdcEccForm1) {

		// Encode EDC data
		edcEccGen.ComputeEdcBlock(IsoWriter::sectorM2F1->subHead, 0x808, IsoWriter::sectorM2F1->edc);

		// Encode ECC data

		// Compute ECC P code
		static const unsigned char zeroaddress[4] = { 0, 0, 0, 0 };
		edcEccGen.ComputeEccBlock(zeroaddress, IsoWriter::sectorBuff+0x10, 86, 24, 2, 86, IsoWriter::sectorBuff+0x81C);
		// Compute ECC Q code
		edcEccGen.ComputeEccBlock(zeroaddress, IsoWriter::sectorBuff+0x10, 52, 43, 86, 88, IsoWriter::sectorBuff+0x8C8);

	} else if (edcEccMode == IsoWriter::EdcEccForm2) {

		edcEccGen.ComputeEdcBlock(IsoWriter::sectorM2F1->subHead, 0x91C, &IsoWriter::sectorM2F2->data[2332]);

	}*/

}

size_t IsoWriter::WriteSectorToDisc()
{
	const size_t bytesRead = fwrite(sectorBuff, CD_SECTOR_SIZE, 1, filePtr);
	currentByte = 0;
	memset(sectorBuff, 0, CD_SECTOR_SIZE);
	return bytesRead;
}

bool IsoWriter::Create(const std::filesystem::path& fileName, unsigned int sizeLBA)
{
	const uint64_t sizeBytes = static_cast<uint64_t>(sizeLBA) * CD_SECTOR_SIZE;

	m_mmap = std::make_unique<MMappedFile>();
	return m_mmap->Create(fileName, sizeBytes);
}

int IsoWriter::SeekToSector(int sector) {

	assert(!"Dead code");

	return 0;

}

int IsoWriter::SeekToEnd() {

	assert(!"Dead code");

	return 0;

}

size_t IsoWriter::WriteBytes(void* data, size_t bytes, int edcEccEncode) {

    size_t writeBytes = 0;

	char*  	dataPtr = (char*)data;
    int		toWrite;

	memcpy(IsoWriter::sectorM2F1->subHead, IsoWriter::subHeadBuff, 8);

	IsoWriter::lastSectorType = edcEccEncode;

    while(bytes > 0) {

        if (bytes > 2048)
			toWrite = 2048;
		else
			toWrite = bytes;

		memcpy(&IsoWriter::sectorM2F1->data[IsoWriter::currentByte], dataPtr, toWrite);

		IsoWriter::currentByte += toWrite;

		dataPtr += toWrite;
		bytes -= toWrite;

		if (IsoWriter::currentByte >= 2048) {

			IsoWriter::PrepSector(edcEccEncode);

            if (WriteSectorToDisc() == 0) {

				IsoWriter::currentByte = 0;
				return(writeBytes);

            }

            IsoWriter::currentByte = 0;
            IsoWriter::currentSector++;

            IsoWriter::sectorM2F1 = (SECTOR_M2F1*)sectorBuff;
			IsoWriter::sectorM2F2 = (SECTOR_M2F2*)sectorBuff;

		}

		writeBytes += toWrite;

    }

	return(writeBytes);

}

size_t IsoWriter::WriteBytesXA(void* data, size_t bytes, int edcEccEncode) {

    size_t writeBytes = 0;

	char*  	dataPtr = (char*)data;
    int		toWrite;

	IsoWriter::lastSectorType = edcEccEncode;

    while(bytes > 0) {

        if (bytes > 2336)
			toWrite = 2336;
		else
			toWrite = bytes;

		memcpy(&IsoWriter::sectorM2F2->data[IsoWriter::currentByte], dataPtr, toWrite);

		IsoWriter::currentByte += toWrite;

		dataPtr += toWrite;
		bytes -= toWrite;

		if (IsoWriter::currentByte >= 2336) {

			IsoWriter::PrepSector(edcEccEncode);

            if (WriteSectorToDisc() == 0) {

				IsoWriter::currentByte = 0;
				return(writeBytes);

            }

            IsoWriter::currentByte = 0;
            IsoWriter::currentSector++;

            IsoWriter::sectorM2F1 = (SECTOR_M2F1*)sectorBuff;
			IsoWriter::sectorM2F2 = (SECTOR_M2F2*)sectorBuff;

		}

		writeBytes += toWrite;

    }

	return(writeBytes);

}

size_t IsoWriter::WriteBytesRaw(const void* data, size_t bytes) {

    size_t writeBytes = 0;

	char*  	dataPtr = (char*)data;
    int		toWrite;

	//IsoWriter::lastSectorType = EdcEccNone;

    while(bytes > 0) {

        if (bytes > 2352)
			toWrite = 2352;
		else
			toWrite = bytes;

		memcpy(&IsoWriter::sectorBuff[IsoWriter::currentByte], dataPtr, toWrite);

		IsoWriter::currentByte += toWrite;

		dataPtr += toWrite;
		bytes -= toWrite;

		if (IsoWriter::currentByte >= 2352) {

            if (WriteSectorToDisc() == 0) {

				IsoWriter::currentByte = 0;
				return(writeBytes);

            }

            IsoWriter::currentByte = 0;
            IsoWriter::currentSector++;

            IsoWriter::sectorM2F1 = (SECTOR_M2F1*)sectorBuff;
			IsoWriter::sectorM2F2 = (SECTOR_M2F2*)sectorBuff;

		}

		writeBytes += toWrite;

    }

	return(writeBytes);

}

size_t  IsoWriter::WriteBlankSectors(const size_t count)
{
	const char blank[CD_SECTOR_SIZE] {};
	
	size_t bytesWritten = 0;
	for(size_t i = 0; i < count; i++)
	{
		bytesWritten += WriteBytesRaw( blank, CD_SECTOR_SIZE );
	}
	return bytesWritten;
}

int IsoWriter::CurrentSector() {

	return currentSector;

}

void IsoWriter::SetSubheader(unsigned char* data) {

	assert(!"Dead code");

}

void IsoWriter::SetSubheader(unsigned int data) {

	assert(!"Dead code");

}

void IsoWriter::Close() {

	if (IsoWriter::filePtr != nullptr) {

		if (IsoWriter::currentByte > 0) {
			IsoWriter::PrepSector(IsoWriter::lastSectorType);
			WriteSectorToDisc();
		}

		fclose(IsoWriter::filePtr);

	}

	IsoWriter::filePtr = nullptr;

}

// ======================================================

IsoWriter::SectorView::SectorView(MMappedFile* mappedFile, unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm)
	: m_view(mappedFile->GetView(static_cast<uint64_t>(offsetLBA) * CD_SECTOR_SIZE, static_cast<size_t>(sizeLBA) * CD_SECTOR_SIZE))
	, m_currentLBA(offsetLBA)
	, m_endLBA(offsetLBA + sizeLBA)
	, m_edcEccForm(edcEccForm)
{
	m_currentSector = m_view.GetBuffer();
}

IsoWriter::SectorView::~SectorView()
{
	WaitForChecksumJobs();
}

static uint8_t ToBCD8(uint8_t num)
{
	return ((num / 10) << 4) | (num % 10);
}

static void WriteSectorAddress(uint8_t* output, unsigned int lsn)
{
	unsigned int lba = lsn + 150;

	const uint8_t frame = static_cast<uint8_t>(lba % 75);
	lba /= 75;

	const uint8_t second = static_cast<uint8_t>(lba % 60);
	lba /= 60;

	const uint8_t minute = static_cast<uint8_t>(lba);

	output[0] = ToBCD8(minute);
	output[1] = ToBCD8(second);
	output[2] = ToBCD8(frame);
}

void IsoWriter::SectorView::PrepareSectorHeader() const
{
	SECTOR_M2F1* sector = static_cast<SECTOR_M2F1*>(m_currentSector);

	static constexpr uint8_t SYNC_PATTERN[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	std::copy(std::begin(SYNC_PATTERN), std::end(SYNC_PATTERN), sector->sync);

	WriteSectorAddress(sector->addr, m_currentLBA);

	sector->mode = 2; // Mode 2
}

void IsoWriter::SectorView::CalculateForm1()
{
	SECTOR_M2F1* sector = static_cast<SECTOR_M2F1*>(m_currentSector);

	// Encode EDC data
	m_checksumJobs.emplace_front(std::async(std::launch::async, &EDCECC::ComputeEdcBlock, &EDC_ECC_GEN,
		sector->subHead, sizeof(sector->subHead) + sizeof(sector->data), sector->edc));

	// Encode ECC data
	m_checksumJobs.emplace_front(std::async(std::launch::async, [](SECTOR_M2F1* sector)
		{
			// Compute ECC P code
			static const unsigned char zeroaddress[4] = { 0, 0, 0, 0 };
			EDC_ECC_GEN.ComputeEccBlock(zeroaddress, sector->subHead, 86, 24, 2, 86, sector->ecc);
			// Compute ECC Q code
			EDC_ECC_GEN.ComputeEccBlock(zeroaddress, sector->subHead, 52, 43, 86, 88, sector->ecc+172);
		}, sector));
}

void IsoWriter::SectorView::CalculateForm2()
{
	SECTOR_M2F2* sector = static_cast<SECTOR_M2F2*>(m_currentSector);
	m_checksumJobs.emplace_front(std::async(std::launch::async, &EDCECC::ComputeEdcBlock, &EDC_ECC_GEN,
		sector->data, sizeof(sector->data) - 4, sector->data + sizeof(sector->data) - 4));
}

void IsoWriter::SectorView::WaitForChecksumJobs()
{
	for (auto& job : m_checksumJobs)
	{
		job.get();
	}
	m_checksumJobs.clear();
}

// ======================================================

class SectorViewM2F1 final : public IsoWriter::SectorView
{
private:
	using SectorType = SECTOR_M2F1;

public:
	using IsoWriter::SectorView::SectorView;

	~SectorViewM2F1() override
	{
		if (m_offsetInSector != 0)
		{
			NextSector();
		}
	}

	void WriteFile(FILE* file) override
	{
		SectorType* sector = static_cast<SectorType*>(m_currentSector);
		const unsigned int lastLBA = m_endLBA - 1;

		while (m_currentLBA < m_endLBA)
		{
			PrepareSectorHeader();
			SetSubHeader(sector->subHead, m_currentLBA != lastLBA ? m_subHeader : IsoWriter::SubEOF);

			const size_t bytesRead = fread(sector->data, 1, sizeof(sector->data), file);
			// Fill the remainder of the sector with zeroes if applicable
			std::fill(std::begin(sector->data) + bytesRead, std::end(sector->data), 0);
		
			if (m_edcEccForm == IsoWriter::EdcEccForm::Form1)
			{
				CalculateForm1();
			}
			else if (m_edcEccForm == IsoWriter::EdcEccForm::Form2)
			{
				CalculateForm2();
			}

			m_currentLBA++;
			m_currentSector = ++sector;
		}
	}

	void WriteMemory(const void* memory, size_t size) override
	{
		const char* buf = static_cast<const char*>(memory);
		const unsigned int lastLBA = m_endLBA - 1;

		while (m_currentLBA < m_endLBA && size > 0)
		{
			SectorType* sector = static_cast<SectorType*>(m_currentSector);

			if (m_offsetInSector == 0)
			{
				PrepareSectorHeader();
				SetSubHeader(sector->subHead, m_currentLBA != lastLBA ? m_subHeader : IsoWriter::SubEOF);
			}

			const size_t memToCopy = std::min(GetSpaceInCurrentSector(), size);
			std::copy_n(buf, memToCopy, sector->data + m_offsetInSector);
			
			size -= memToCopy;
			buf += memToCopy;
			m_offsetInSector += memToCopy;

			if (m_offsetInSector >= sizeof(sector->data))
			{
				NextSector();
			}
		}
	}

	void WriteBlankSectors(unsigned int count) override
	{
		SectorType* sector = static_cast<SectorType*>(m_currentSector);
		const bool isForm2 = m_edcEccForm == IsoWriter::EdcEccForm::Form2;

		while (m_currentLBA < m_endLBA && count > 0)
		{
			PrepareSectorHeader();
			SetSubHeader(sector->subHead, isForm2 ? 0x00200000 : 0);

			std::fill(std::begin(sector->data), std::end(sector->data), 0);
			if (m_edcEccForm == IsoWriter::EdcEccForm::Form1)
			{
				CalculateForm1();
			}
			else if (m_edcEccForm == IsoWriter::EdcEccForm::Form2)
			{
				CalculateForm2();
			}

			count--;
			m_currentLBA++;
			m_currentSector = ++sector;
		}
	}

	size_t GetSpaceInCurrentSector() const override
	{
		return sizeof(SectorType::data) - m_offsetInSector;
	}

	void NextSector() override
	{
		// Fill the remainder of the sector with zeroes if applicable
		SectorType* sector = static_cast<SectorType*>(m_currentSector);
		std::fill(std::begin(sector->data) + m_offsetInSector, std::end(sector->data), 0);
		
		if (m_edcEccForm == IsoWriter::EdcEccForm::Form1)
		{
			CalculateForm1();
		}
		else if (m_edcEccForm == IsoWriter::EdcEccForm::Form2)
		{
			CalculateForm2();
		}

		m_offsetInSector = 0;
		m_currentLBA++;
		m_currentSector = sector + 1;
	}

	void SetSubheader(unsigned int subHead) override
	{
		m_subHeader = subHead;
	}

private:
	void SetSubHeader(unsigned char* subHead, unsigned int data) const
	{
		memcpy(subHead, &data, sizeof(data));
		memcpy(subHead+4, &data, sizeof(data));
	}

private:
	unsigned int m_subHeader = IsoWriter::SubData;
};

auto IsoWriter::GetSectorViewM2F1(unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm) const -> std::unique_ptr<SectorView>
{
	return std::make_unique<SectorViewM2F1>(m_mmap.get(), offsetLBA, sizeLBA, edcEccForm);
}

class SectorViewM2F2 final : public IsoWriter::SectorView
{
private:
	using SectorType = SECTOR_M2F2;

public:
	using IsoWriter::SectorView::SectorView;

	~SectorViewM2F2() override
	{
		if (m_offsetInSector != 0)
		{
			NextSector();
		}
	}

	void WriteFile(FILE* file) override
	{
		SectorType* sector = static_cast<SectorType*>(m_currentSector);

		while (m_currentLBA < m_endLBA)
		{
			PrepareSectorHeader();

			const size_t bytesRead = fread(sector->data, 1, sizeof(sector->data), file);
			// Fill the remainder of the sector with zeroes if applicable
			std::fill(std::begin(sector->data) + bytesRead, std::end(sector->data), 0);
		
			if (m_edcEccForm != IsoWriter::EdcEccForm::Autodetect)
			{
				if (m_edcEccForm == IsoWriter::EdcEccForm::Form1)
				{
					CalculateForm1();
				}
				else if (m_edcEccForm == IsoWriter::EdcEccForm::Form2)
				{
					CalculateForm2();
				}
			}
			else
			{
				// Check submode if sector is mode 2 form 2
				if ( sector->data[2] & 0x20 )
				{
					// If so, write it as an XA sector
					CalculateForm2();
				}
				else
				{
					// Otherwise, write it as Mode 2 Form 1
					CalculateForm1();
				}
			}

			m_currentLBA++;
			m_currentSector = ++sector;
		}
	}

	void WriteMemory(const void* memory, size_t size) override
	{
		const char* buf = static_cast<const char*>(memory);
		const unsigned int lastLBA = m_endLBA - 1;

		while (m_currentLBA < m_endLBA && size > 0)
		{
			if (m_offsetInSector == 0)
			{
				PrepareSectorHeader();
			}

			SectorType* sector = static_cast<SectorType*>(m_currentSector);

			const size_t memToCopy = std::min(GetSpaceInCurrentSector(), size);
			std::copy_n(buf, memToCopy, sector->data + m_offsetInSector);
			
			size -= memToCopy;
			buf += memToCopy;
			m_offsetInSector += memToCopy;

			if (m_offsetInSector >= sizeof(sector->data))
			{
				NextSector();
			}
		}
	}

	void WriteBlankSectors(unsigned int count) override
	{
		SectorType* sector = static_cast<SectorType*>(m_currentSector);
		const bool isForm2 = m_edcEccForm == IsoWriter::EdcEccForm::Form2;

		while (m_currentLBA < m_endLBA && count > 0)
		{
			PrepareSectorHeader();

			std::fill(std::begin(sector->data), std::end(sector->data), 0);
			if (m_edcEccForm == IsoWriter::EdcEccForm::Form1)
			{
				CalculateForm1();
			}
			else if (m_edcEccForm == IsoWriter::EdcEccForm::Form2)
			{
				CalculateForm2();
			}

			count--;
			m_currentLBA++;
			m_currentSector = ++sector;
		}
	}

	size_t GetSpaceInCurrentSector() const override
	{
		return sizeof(SectorType::data) - m_offsetInSector;
	}

	void NextSector() override
	{
		// Fill the remainder of the sector with zeroes if applicable
		SectorType* sector = static_cast<SectorType*>(m_currentSector);
		std::fill(std::begin(sector->data) + m_offsetInSector, std::end(sector->data), 0);
		
		if (m_edcEccForm != IsoWriter::EdcEccForm::Autodetect)
		{
			if (m_edcEccForm == IsoWriter::EdcEccForm::Form1)
			{
				CalculateForm1();
			}
			else if (m_edcEccForm == IsoWriter::EdcEccForm::Form2)
			{
				CalculateForm2();
			}
		}
		else
		{
			// Check submode if sector is mode 2 form 2
			if ( sector->data[2] & 0x20 )
			{
				// If so, write it as an XA sector
				CalculateForm2();
			}
			else
			{
				// Otherwise, write it as Mode 2 Form 1
				CalculateForm1();
			}
		}

		m_offsetInSector = 0;
		m_currentLBA++;
		m_currentSector = sector + 1;
	}

	void SetSubheader(unsigned int subHead) override
	{
		// Not applicable to M2F2 sectors
	}
};

auto IsoWriter::GetSectorViewM2F2(unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm) const -> std::unique_ptr<SectorView>
{
	return std::make_unique<SectorViewM2F2>(m_mmap.get(), offsetLBA, sizeLBA, edcEccForm);
}

// ======================================================

auto IsoWriter::GetRawSectorView(unsigned int offsetLBA, unsigned int sizeLBA) const -> std::unique_ptr<RawSectorView>
{
	return std::make_unique<RawSectorView>(m_mmap.get(), offsetLBA, sizeLBA);
}

IsoWriter::RawSectorView::RawSectorView(MMappedFile* mappedFile, unsigned int offsetLBA, unsigned int sizeLBA)
	: m_view(mappedFile->GetView(static_cast<uint64_t>(offsetLBA) * CD_SECTOR_SIZE, static_cast<size_t>(sizeLBA) * CD_SECTOR_SIZE))
	, m_endLBA(sizeLBA)
{
}

void* IsoWriter::RawSectorView::GetRawBuffer() const
{
	return m_view.GetBuffer();
}

void IsoWriter::RawSectorView::WriteBlankSectors()
{
	char* buf = static_cast<char*>(m_view.GetBuffer());
	std::fill_n(buf, static_cast<size_t>(m_endLBA) * CD_SECTOR_SIZE, 0);
}