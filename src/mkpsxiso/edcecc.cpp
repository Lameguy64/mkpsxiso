/*	EDC and ECC calculation routines from ecmtools by Neill Corlett
 *
 *	Its the only program where I can find routines for proper EDC/ECC calculation.
 */

#include "edcecc.h"

EDCECC::EDCECC() {

	unsigned int i,j,edc;

	for(i=0; i<256; i++) {

		j = (i<<1)^(i&0x80?0x11D:0);
		ecc_f_lut[i] = j;
		ecc_b_lut[i^j] = i;
		edc = i;

		for(j=0; j<8; j++)
			edc=(edc>>1)^(edc&1?0xD8018001:0);

		edc_lut[i] = edc;

	}

}

unsigned int EDCECC::ComputeEdcBlockPartial(unsigned int edc, const unsigned char *src, size_t len) const {

	while(len--)
		edc = (edc>>8)^edc_lut[(edc^(*src++))&0xFF];

	return edc;

}

void EDCECC::ComputeEdcBlock(const unsigned char *src, size_t len, unsigned char *dest) const {

	unsigned int edc = ComputeEdcBlockPartial(0, src, len);

	dest[0] = (edc>>0)&0xFF;
	dest[1] = (edc>>8)&0xFF;
	dest[2] = (edc>>16)&0xFF;
	dest[3] = (edc>>24)&0xFF;

}

void EDCECC::ComputeEccBlock(const unsigned char *address, const unsigned char *src, unsigned int major_count, unsigned int minor_count, unsigned int major_mult, unsigned int minor_inc, unsigned char *dest) const {

	unsigned int len = major_count*minor_count;
	unsigned int major,minor;

	for(major = 0; major < major_count; major++) {

		unsigned int	index = (major >> 1) * major_mult + (major & 1);
		unsigned char	ecc_a = 0;
		unsigned char	ecc_b = 0;

		for(minor = 0; minor < minor_count; minor++) {

			unsigned char temp;
			if (index < 4) {
				temp = address[index];
			} else {
				temp = src[index - 4];
			}

			index += minor_inc;

			if(index >= len)
				index -= len;

			ecc_a ^= temp;
			ecc_b ^= temp;
			ecc_a = ecc_f_lut[ecc_a];

		}

		ecc_a = ecc_b_lut[ecc_f_lut[ecc_a]^ecc_b];
		dest[major] = ecc_a;
		dest[major+major_count] = ecc_a^ecc_b;

	}

}
