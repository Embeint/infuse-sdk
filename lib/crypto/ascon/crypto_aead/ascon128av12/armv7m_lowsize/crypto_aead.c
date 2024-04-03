#include "crypto_aead.h"

#include <string.h>

#include "api.h"
#include "ascon.h"
#include "permutations.h"
#include "printstate.h"

#ifdef ASCON_AEAD_RATE

int crypto_aead_encrypt(unsigned char* c, unsigned long long* clen,
                        const unsigned char* m, unsigned long long mlen,
                        const unsigned char* ad, unsigned long long adlen,
                        unsigned char* tag, const unsigned char* npub,
                        const unsigned char* k) {
  /* set ciphertext size */
  *clen = mlen;
  /* ascon encryption */
  ascon_aead(tag, c, m, mlen, ad, adlen, npub, k, ASCON_ENCRYPT);
  return 0;
}

int crypto_aead_decrypt(unsigned char* m, unsigned long long* mlen,
                        const unsigned char* tag, const unsigned char* c,
                        unsigned long long clen, const unsigned char* ad,
                        unsigned long long adlen, const unsigned char* npub,
                        const unsigned char* k) {
  int i;
  uint8_t t[16];
  int result = 0;
  /* set plaintext size */
  *mlen = clen;
  /* ascon decryption */
  ascon_aead(t, m, c, *mlen, ad, adlen, npub, k, ASCON_DECRYPT);
  /* verify tag (should be constant time, check compiler output) */
  for (i = 0; i < CRYPTO_ABYTES; ++i) result |= t[i] ^ tag[i];
  return (((result - 1) >> 8) & 1) - 1;
}

#endif
