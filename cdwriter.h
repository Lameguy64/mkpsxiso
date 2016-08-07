#ifndef _CDWRITER_H
#define _CDWRITER_H

#include "cd.h"
#include "edcecc.h"

namespace cd {

class IsoWriter {

		FILE*			filePtr;
		u_char			subHeadBuff[12];
		u_char			sectorBuff[CD_SECTOR_SIZE];
		SECTOR_M2F1*	sectorM2F1;
		SECTOR_M2F2*	sectorM2F2;

		EDCECC			edcEccGen;

		int				currentSector;
		int				currentByte;
		int				bytesWritten;
		int				lastSectorType;

		void			PrepSector(int edcEccMode);

	public:

		enum {
			EdcEccNone = 0,
			EdcEccForm1,
			EdcEccForm2,
		};

		IsoWriter();
		virtual	~IsoWriter();

		bool	Create(const char* fileName);

		int		SeekToSector(int sector);

		int		SeekToEnd();

		int		CurrentSector();

		void	SetSubheader(u_char* data);

		size_t	WriteBytes(void* data, size_t bytes, int edcEccEncode);
		size_t	WriteBytesXA(void* data, size_t bytes, int edcEccEncode);
		size_t	WriteBytesRaw(void* data, size_t bytes);

		void	Close();

	};

	void SwapBytes(void *var, int size);
	void SetPair16(cd::ISO_USHORT_PAIR* pair, u_short val);
	void SetPair32(cd::ISO_UINT_PAIR* pair, u_int val);

};

#endif // _CDWRITER_H
