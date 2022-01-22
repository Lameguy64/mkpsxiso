#ifndef _CDWRITER_H
#define _CDWRITER_H

#include "cd.h"
#include "edcecc.h"
#include "mmappedfile.h"
#include <filesystem>
#include <forward_list>
#include <future>
#include <memory>

namespace cd {

class IsoWriter {

		std::unique_ptr<MMappedFile> m_mmap;

		FILE*			filePtr;
		unsigned char	subHeadBuff[12];
		unsigned char	sectorBuff[CD_SECTOR_SIZE];
		SECTOR_M2F1*	sectorM2F1;
		SECTOR_M2F2*	sectorM2F2;

		EDCECC			edcEccGen;

		int				currentSector;
		int				currentByte;
		int				bytesWritten;
		int				lastSectorType;

		void			PrepSector(int edcEccMode);
		size_t			WriteSectorToDisc();

	public:
		enum class EdcEccForm
		{
			None = 0,
			Form1,
			Form2,
			Autodetect,
		};
		
		enum {
			SubData	= 0x00080000,
			SubSTR	= 0x00480100,
			SubEOL	= 0x00090000,
			SubEOF	= 0x00890000,
		};

		class SectorView
		{
		public:
			SectorView(MMappedFile* mappedFile, unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm);
			virtual ~SectorView();

			virtual void WriteFile(FILE* file) = 0;
			virtual void WriteMemory(const void* memory, size_t size) = 0;
			virtual void WriteBlankSectors(unsigned int count) = 0;
			virtual size_t GetSpaceInCurrentSector() const = 0;
			virtual void NextSector() = 0;
			virtual void SetSubheader(unsigned int subHead) = 0;

			void WaitForChecksumJobs();

		protected:		
			void CalculateForm1();
			void CalculateForm2();

		protected:
			void* m_currentSector = nullptr;
			size_t m_offsetInSector = 0;
			unsigned int m_endLBA = 0;
			unsigned int m_currentLBA = 0;
			EdcEccForm m_edcEccForm;

		private:
			std::forward_list<std::future<void>> m_checksumJobs;
			MMappedFile::View m_view;
		};

		class RawSectorView
		{
		public:
			RawSectorView(MMappedFile* mappedFile, unsigned int offsetLBA, unsigned int sizeLBA);

			void* GetRawBuffer() const;
			void WriteBlankSectors();

		private:
			MMappedFile::View m_view;
			unsigned int m_endLBA;
		};

		IsoWriter();
		~IsoWriter();

		bool	Create(const std::filesystem::path& fileName, unsigned int sizeLBA);
		std::unique_ptr<SectorView> GetSectorViewM2F1(unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm) const;
		std::unique_ptr<SectorView> GetSectorViewM2F2(unsigned int offsetLBA, unsigned int sizeLBA, EdcEccForm edcEccForm) const;
		std::unique_ptr<RawSectorView> GetRawSectorView(unsigned int offsetLBA, unsigned int sizeLBA) const;

		int		SeekToSector(int sector);

		int		SeekToEnd();

		int		CurrentSector();

		void	SetSubheader(unsigned char* data);
		void	SetSubheader(unsigned int data);

		size_t	WriteBytes(void* data, size_t bytes, int edcEccEncode);
		size_t	WriteBytesXA(void* data, size_t bytes, int edcEccEncode);
		size_t	WriteBytesRaw(const void* data, size_t bytes);
		size_t  WriteBlankSectors(const size_t count);

		void	Close();

	};

	ISO_USHORT_PAIR SetPair16(unsigned short val);
	ISO_UINT_PAIR SetPair32(unsigned int val);

};

#endif // _CDWRITER_H
