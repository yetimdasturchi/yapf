#ifndef YAPF_CRYPTO_H
#define YAPF_CRYPTO_H

#include <stdint.h>
#include <stdlib.h>

#define YAPF_CRYPTO_NONCE_LEN 12U
#define YAPF_CRYPTO_TAG_LEN 16U

int yapf_crypto_nonce(unsigned char nonce[YAPF_CRYPTO_NONCE_LEN]);
int yapf_crypto_encrypt(uint32_t kind, const unsigned char *aad, size_t aad_len,
    const unsigned char *plaintext, size_t plaintext_len,
    const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN],
    unsigned char **ciphertext, size_t *ciphertext_len,
    unsigned char tag[YAPF_CRYPTO_TAG_LEN]);
int yapf_crypto_decrypt(uint32_t kind, const unsigned char *aad, size_t aad_len,
    const unsigned char *ciphertext, size_t ciphertext_len,
    const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN],
    const unsigned char tag[YAPF_CRYPTO_TAG_LEN],
    unsigned char **plaintext, size_t *plaintext_len);

#endif
