#ifndef _EDC_ECC_H
#define _EDC_ECC_H

#ifdef __WIN32__
#include <windows.h>
#else
#include <unistd.h>
#endif

class EDCECC {

	// Tables for EDC and ECC calculation
	u_char ecc_f_lut[256];
	u_char ecc_b_lut[256];
	u_int edc_lut[256];

public:

	// Initializer
	EDCECC();

	// Computes the EDC of *src and returns the result
	u_int	ComputeEdcBlockPartial(u_int edc, const u_char *src, int len);

	// Computes the EDC of *src and stores the result to an unsigned char array *dest
	void	ComputeEdcBlock(const u_char *src, int len, u_char *dest);

	// Computes the ECC data of *src and stores the result to an unsigned char array *dest
	void	ComputeEccBlock(u_char *src, u_int major_count, u_int minor_count, u_int major_mult, u_int minor_inc, u_char *dest);

};

#endif // _EDC_ECC_H
