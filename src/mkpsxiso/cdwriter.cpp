#include "cdwriter.h"
#include "common.h"
#include "edcecc.h"
#include "platform.h"

#include <algorithm>
#include <cstring>

using namespace cd;

static const EDCECC EDC_ECC_GEN;

ISO_USHORT_PAIR cd::SetPair16(unsigned short val) {
    return { val, SwapBytes16(val) };
}

ISO_UINT_PAIR cd::SetPair32(unsigned int val) {
	return { val, SwapBytes32(val) };
}

bool IsoWriter::Create(const fs::path& fileName, unsigned int sizeLBA)
{
	const uint64_t sizeBytes = static_cast<uint64_t>(sizeLBA) * CD_SECTOR_SIZE;

	m_threadPool = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
	
	m_mmap = std::make_unique<MMappedFile>();
	return m_mmap->Create(fileName, sizeBytes);
}

void IsoWriter::Close()
{
	m_mmap.reset();
}

// ======================================================

IsoWriter::SectorView::SectorView(ThreadPool* threadPool, MMappedFile* mappedFile, unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm)
	: m_threadPool(threadPool) 
	, m_view(mappedFile->GetView(static_cast<uint64_t>(offsetLBA) * CD_SECTOR_SIZE, static_cast<size_t>(sizeLBA) * CD_SECTOR_SIZE))
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

	m_checksumJobs.emplace_front(m_threadPool->enqueue([](SECTOR_M2F1* sector)
		{
			// Encode EDC data
			EDC_ECC_GEN.ComputeEdcBlock(sector->subHead, sizeof(sector->subHead) + sizeof(sector->data), sector->edc);

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
	m_checksumJobs.emplace_front(m_threadPool->enqueue(&EDCECC::ComputeEdcBlock, &EDC_ECC_GEN,
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
	return std::make_unique<SectorViewM2F1>(m_threadPool.get(), m_mmap.get(), offsetLBA, sizeLBA, edcEccForm);
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
	return std::make_unique<SectorViewM2F2>(m_threadPool.get(), m_mmap.get(), offsetLBA, sizeLBA, edcEccForm);
}

// ======================================================

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

auto IsoWriter::GetRawSectorView(unsigned int offsetLBA, unsigned int sizeLBA) const -> std::unique_ptr<RawSectorView>
{
	return std::make_unique<RawSectorView>(m_mmap.get(), offsetLBA, sizeLBA);
}
