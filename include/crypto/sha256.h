#ifndef YAPF_SHA256_H
#define YAPF_SHA256_H

#include <stdlib.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    unsigned char data[64];
    size_t data_len;
} yapf_sha256_ctx;

void yapf_sha256_init(yapf_sha256_ctx *ctx);
void yapf_sha256_update(yapf_sha256_ctx *ctx, const unsigned char *data, size_t len);
void yapf_sha256_final(yapf_sha256_ctx *ctx, unsigned char hash[32]);
void yapf_hmac_sha256(const unsigned char *key, size_t key_len,
    const unsigned char *data, size_t data_len, unsigned char out[32]);
void yapf_hkdf_sha256(const unsigned char *secret, size_t secret_len,
    const unsigned char *salt, size_t salt_len,
    const unsigned char *info, size_t info_len,
    unsigned char *out, size_t out_len);

#endif
