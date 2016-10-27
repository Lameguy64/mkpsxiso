/*	EDC and ECC calculation routines from ecmtools by Neill Corlett
 *
 *	Its the only program where I can find routines for proper EDC/ECC calculation.
 */

#include "edcecc.h"

EDCECC::EDCECC() {

	unsigned int i,j,edc;

	for(i=0; i<256; i++) {

		j = (i<<1)^(i&0x80?0x11D:0);
		EDCECC::ecc_f_lut[i] = j;
		EDCECC::ecc_b_lut[i^j] = i;
		edc = i;

		for(j=0; j<8; j++)
			edc=(edc>>1)^(edc&1?0xD8018001:0);

		EDCECC::edc_lut[i] = edc;

	}

}

unsigned int EDCECC::ComputeEdcBlockPartial(unsigned int edc, const unsigned char *src, int len) {

	while(len--)
		edc = (edc>>8)^EDCECC::edc_lut[(edc^(*src++))&0xFF];

	return edc;

}

void EDCECC::ComputeEdcBlock(const unsigned char *src, int len, unsigned char *dest) {

	unsigned int edc = EDCECC::ComputeEdcBlockPartial(0, src, len);

	dest[0] = (edc>>0)&0xFF;
	dest[1] = (edc>>8)&0xFF;
	dest[2] = (edc>>16)&0xFF;
	dest[3] = (edc>>24)&0xFF;

}

void EDCECC::ComputeEccBlock(unsigned char *src, unsigned int major_count, unsigned int minor_count, unsigned int major_mult, unsigned int minor_inc, unsigned char *dest) {

	unsigned int len = major_count*minor_count;
	unsigned int major,minor;

	for(major = 0; major < major_count; major++) {

		unsigned int	index = (major >> 1) * major_mult + (major & 1);
		unsigned char	ecc_a = 0;
		unsigned char	ecc_b = 0;

		for(minor = 0; minor < minor_count; minor++) {

			unsigned char temp = src[index];

			index += minor_inc;

			if(index >= len)
				index -= len;

			ecc_a ^= temp;
			ecc_b ^= temp;
			ecc_a = EDCECC::ecc_f_lut[ecc_a];

		}

		ecc_a = EDCECC::ecc_b_lut[EDCECC::ecc_f_lut[ecc_a]^ecc_b];
		dest[major] = ecc_a;
		dest[major+major_count] = ecc_a^ecc_b;

	}

}
