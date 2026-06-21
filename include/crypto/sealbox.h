#ifndef YAPF_SEALBOX_H
#define YAPF_SEALBOX_H

#include <stdlib.h>

void yapf_box_stream(const unsigned char key[32], const unsigned char nonce[12],
    unsigned int counter, const unsigned char *in, unsigned char *out, size_t len);
int yapf_box_lock(const unsigned char key[32], const unsigned char nonce[12],
    const unsigned char *aad, size_t aad_len,
    const unsigned char *plaintext, size_t plaintext_len,
    unsigned char *ciphertext, unsigned char tag[16]);
int yapf_box_open(const unsigned char key[32], const unsigned char nonce[12],
    const unsigned char *aad, size_t aad_len,
    const unsigned char *ciphertext, size_t ciphertext_len,
    const unsigned char tag[16], unsigned char *plaintext);

#endif
