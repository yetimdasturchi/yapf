#include "crypto/sha256.h"

#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR((x), 2) ^ ROTR((x), 13) ^ ROTR((x), 22))
#define EP1(x) (ROTR((x), 6) ^ ROTR((x), 11) ^ ROTR((x), 25))
#define SIG0(x) (ROTR((x), 7) ^ ROTR((x), 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR((x), 17) ^ ROTR((x), 19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t load_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store_be32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static void yapf_sha256_transform(yapf_sha256_ctx *ctx, const unsigned char data[64])
{
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++) {
        m[i] = load_be32(data + (i * 4));
    }
    for (int i = 16; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
        uint32_t t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void yapf_sha256_init(yapf_sha256_ctx *ctx)
{
    ctx->data_len = 0;
    ctx->bit_len = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

void yapf_sha256_update(yapf_sha256_ctx *ctx, const unsigned char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 64) {
            yapf_sha256_transform(ctx, ctx->data);
            ctx->bit_len += 512;
            ctx->data_len = 0;
        }
    }
}

void yapf_sha256_final(yapf_sha256_ctx *ctx, unsigned char hash[32])
{
    size_t i = ctx->data_len;

    ctx->data[i++] = 0x80;
    if (i > 56) {
        while (i < 64) {
            ctx->data[i++] = 0x00;
        }
        yapf_sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) {
        ctx->data[i++] = 0x00;
    }

    ctx->bit_len += ctx->data_len * 8;
    for (int j = 0; j < 8; j++) {
        ctx->data[63 - j] = (unsigned char)(ctx->bit_len >> (j * 8));
    }
    yapf_sha256_transform(ctx, ctx->data);

    for (int j = 0; j < 8; j++) {
        store_be32(hash + (j * 4), ctx->state[j]);
    }
}

void yapf_hmac_sha256(const unsigned char *key, size_t key_len,
    const unsigned char *data, size_t data_len, unsigned char out[32])
{
    unsigned char key_block[64];
    unsigned char inner_key[64];
    unsigned char outer_key[64];
    unsigned char inner_hash[32];
    yapf_sha256_ctx ctx;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block)) {
        yapf_sha256_init(&ctx);
        yapf_sha256_update(&ctx, key, key_len);
        yapf_sha256_final(&ctx, key_block);
    } else if (key_len) {
        memcpy(key_block, key, key_len);
    }

    for (size_t i = 0; i < sizeof(key_block); i++) {
        inner_key[i] = key_block[i] ^ 0x36U;
        outer_key[i] = key_block[i] ^ 0x5cU;
    }

    yapf_sha256_init(&ctx);
    yapf_sha256_update(&ctx, inner_key, sizeof(inner_key));
    yapf_sha256_update(&ctx, data, data_len);
    yapf_sha256_final(&ctx, inner_hash);

    yapf_sha256_init(&ctx);
    yapf_sha256_update(&ctx, outer_key, sizeof(outer_key));
    yapf_sha256_update(&ctx, inner_hash, sizeof(inner_hash));
    yapf_sha256_final(&ctx, out);

    memset(key_block, 0, sizeof(key_block));
    memset(inner_key, 0, sizeof(inner_key));
    memset(outer_key, 0, sizeof(outer_key));
    memset(inner_hash, 0, sizeof(inner_hash));
}

void yapf_hkdf_sha256(const unsigned char *secret, size_t secret_len,
    const unsigned char *salt, size_t salt_len,
    const unsigned char *info, size_t info_len,
    unsigned char *out, size_t out_len)
{
    unsigned char zero_salt[32] = {0};
    unsigned char prk[32];
    unsigned char t[32];
    unsigned char block[32 + 128 + 1];
    size_t done = 0;
    size_t t_len = 0;
    unsigned char counter = 1;

    if (!salt || salt_len == 0) {
        salt = zero_salt;
        salt_len = sizeof(zero_salt);
    }

    yapf_hmac_sha256(salt, salt_len, secret, secret_len, prk);

    while (done < out_len) {
        size_t block_len = 0;
        if (t_len) {
            memcpy(block, t, t_len);
            block_len += t_len;
        }
        if (info_len) {
            memcpy(block + block_len, info, info_len);
            block_len += info_len;
        }
        block[block_len++] = counter++;
        yapf_hmac_sha256(prk, sizeof(prk), block, block_len, t);
        t_len = sizeof(t);

        size_t take = out_len - done < t_len ? out_len - done : t_len;
        memcpy(out + done, t, take);
        done += take;
    }

    memset(prk, 0, sizeof(prk));
    memset(t, 0, sizeof(t));
    memset(block, 0, sizeof(block));
}
