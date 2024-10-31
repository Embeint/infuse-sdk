/*
The eXtended Keccak Code Package (XKCP)
https://github.com/XKCP/XKCP

Xoodyak, designed by Joan Daemen, Seth Hoffert, Michaël Peeters, Gilles Van Assche and Ronny Van
Keer.

Implementation by Ronny Van Keer, hereby denoted as "the implementer".

For more information, feedback or questions, please refer to the Keccak Team website:
https://keccak.team/

To the extent possible under law, the implementer has waived all copyright
and related or neighboring rights to the source code in this file.
http://creativecommons.org/publicdomain/zero/1.0/
*/

#ifdef XoodooReference
#include "displayIntermediateValues.h"
#endif

#if DEBUG
#include <assert.h>
#endif
#include <string.h>
#include "Xoodyak.h"

#ifdef OUTPUT
#include <stdlib.h>
#include <string.h>

static void displayByteString(FILE *f, const char *synopsis, const uint8_t *data,
			      unsigned int length);
static void displayByteString(FILE *f, const char *synopsis, const uint8_t *data,
			      unsigned int length)
{
	unsigned int i;

	fprintf(f, "%s:", synopsis);
	for (i = 0; i < length; i++) {
		fprintf(f, " %02x", (unsigned int)data[i]);
	}
	fprintf(f, "\n");
}
#endif

#define MyMin(a, b) (((a) < (b)) ? (a) : (b))

#ifdef XKCP_has_Xoodoo
#include "Xoodoo-SnP.h"

#define SnP         Xoodoo
#define SnP_Permute Xoodoo_Permute_12rounds
#define prefix      Xoodyak
#include "Cyclist.inc"
#undef prefix
#undef SnP
#undef SnP_Permute
#endif

#define TAGLEN           16
#define CRYPTO_KEYBYTES  16
#define CRYPTO_NPUBBYTES 16

int xoodyak_aead_encrypt(unsigned char *c, unsigned long long *clen, const unsigned char *m,
			 unsigned long long mlen, const unsigned char *ad, unsigned long long adlen,
			 unsigned char *tag, const unsigned char *npub, const unsigned char *k)
{
	Xoodyak_Instance instance;

	Xoodyak_Initialize(&instance, k, CRYPTO_KEYBYTES, NULL, 0, NULL, 0);
	Xoodyak_Absorb(&instance, npub, CRYPTO_NPUBBYTES);
	Xoodyak_Absorb(&instance, ad, (size_t)adlen);
	Xoodyak_Encrypt(&instance, m, c, (size_t)mlen);
	Xoodyak_Squeeze(&instance, tag, TAGLEN);
	*clen = mlen;
	return 0;
}

int xoodyak_aead_decrypt(unsigned char *m, unsigned long long *mlen, unsigned char *tag,
			 const unsigned char *c, unsigned long long clen, const unsigned char *ad,
			 unsigned long long adlen, const unsigned char *npub,
			 const unsigned char *k)
{
	Xoodyak_Instance instance;
	unsigned char tag_out[TAGLEN];
	unsigned long long mlen_;

	*mlen = 0;
	mlen_ = clen;
	Xoodyak_Initialize(&instance, k, CRYPTO_KEYBYTES, NULL, 0, NULL, 0);
	Xoodyak_Absorb(&instance, npub, CRYPTO_NPUBBYTES);
	Xoodyak_Absorb(&instance, ad, (size_t)adlen);
	Xoodyak_Decrypt(&instance, c, m, (size_t)mlen_);
	Xoodyak_Squeeze(&instance, tag_out, TAGLEN);
	if (memcmp(tag_out, tag, TAGLEN) != 0) {
		memset(m, 0, (size_t)mlen_);
		return -1;
	}
	*mlen = mlen_;
	return 0;
}
