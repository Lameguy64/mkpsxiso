#ifndef _EDC_ECC_H
#define _EDC_ECC_H

#ifdef __WIN32__
#include <windows.h>
#else
#include <unistd.h>
#endif

class EDCECC {

	// Tables for EDC and ECC calculation
	unsigned char ecc_f_lut[256];
	unsigned char ecc_b_lut[256];
	unsigned int edc_lut[256];

public:

	// Initializer
	EDCECC();

	// Computes the EDC of *src and returns the result
	unsigned int	ComputeEdcBlockPartial(unsigned int edc, const unsigned char *src, int len);

	// Computes the EDC of *src and stores the result to an unsigned char array *dest
	void	ComputeEdcBlock(const unsigned char *src, int len, unsigned char *dest);

	// Computes the ECC data of *src and stores the result to an unsigned char array *dest
	void	ComputeEccBlock(unsigned char *src, unsigned int major_count, unsigned int minor_count, unsigned int major_mult, unsigned int minor_inc, unsigned char *dest);

};

#endif // _EDC_ECC_H
