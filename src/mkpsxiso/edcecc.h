#ifndef _EDC_ECC_H
#define _EDC_ECC_H

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
	unsigned int	ComputeEdcBlockPartial(unsigned int edc, const unsigned char *src, size_t len) const;

	// Computes the EDC of *src and stores the result to an unsigned char array *dest
	void	ComputeEdcBlock(const unsigned char *src, size_t len, unsigned char *dest) const;

	// Computes the ECC data of *src and stores the result to an unsigned char array *dest
	void	ComputeEccBlock(const unsigned char *address, const unsigned char *src, unsigned int major_count, unsigned int minor_count, unsigned int major_mult, unsigned int minor_inc, unsigned char *dest) const;

};

#endif // _EDC_ECC_H
