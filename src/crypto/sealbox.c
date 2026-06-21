#include "crypto/sealbox.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t load32_le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32_le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void store64_le(unsigned char *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (i * 8));
    }
}

static uint32_t rotl32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

#define QR(a, b, c, d) \
    do { \
        a += b; d ^= a; d = rotl32(d, 16); \
        c += d; b ^= c; b = rotl32(b, 12); \
        a += b; d ^= a; d = rotl32(d, 8); \
        c += d; b ^= c; b = rotl32(b, 7); \
    } while (0)

static void sealbox_block(const unsigned char key[32], const unsigned char nonce[12],
    uint32_t counter, unsigned char out[64])
{
    uint32_t x[16];
    uint32_t initial[16];

    initial[0] = 0x61707865U;
    initial[1] = 0x3320646eU;
    initial[2] = 0x79622d32U;
    initial[3] = 0x6b206574U;
    for (int i = 0; i < 8; i++) {
        initial[4 + i] = load32_le(key + (i * 4));
    }
    initial[12] = counter;
    initial[13] = load32_le(nonce + 0);
    initial[14] = load32_le(nonce + 4);
    initial[15] = load32_le(nonce + 8);

    memcpy(x, initial, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }

    for (int i = 0; i < 16; i++) {
        store32_le(out + (i * 4), x[i] + initial[i]);
    }
}

void yapf_box_stream(const unsigned char key[32], const unsigned char nonce[12],
    unsigned int counter, const unsigned char *in, unsigned char *out, size_t len)
{
    unsigned char block[64];
    size_t offset = 0;

    while (offset < len) {
        size_t take = len - offset < sizeof(block) ? len - offset : sizeof(block);
        sealbox_block(key, nonce, counter++, block);
        for (size_t i = 0; i < take; i++) {
            out[offset + i] = in[offset + i] ^ block[i];
        }
        offset += take;
    }
    memset(block, 0, sizeof(block));
}

static int sealbox_equal(const unsigned char *a, const unsigned char *b, size_t len)
{
    unsigned char diff = 0;

    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

static void sealbox_auth(const unsigned char *msg, size_t msg_len,
    const unsigned char key[32], unsigned char mac[16])
{
    uint32_t t0, t1, t2, t3;
    uint64_t r0, r1, r2, r3, r4;
    uint64_t s1, s2, s3, s4;
    uint64_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    uint64_t pad0, pad1, pad2, pad3;
    unsigned char block[16];
    int final = 0;

    t0 = load32_le(key + 0);
    t1 = load32_le(key + 4);
    t2 = load32_le(key + 8);
    t3 = load32_le(key + 12);

    r0 = t0 & 0x3ffffffULL;
    r1 = ((t0 >> 26) | ((uint64_t)t1 << 6)) & 0x3ffff03ULL;
    r2 = ((t1 >> 20) | ((uint64_t)t2 << 12)) & 0x3ffc0ffULL;
    r3 = ((t2 >> 14) | ((uint64_t)t3 << 18)) & 0x3f03fffULL;
    r4 = (t3 >> 8) & 0x00fffffULL;

    s1 = r1 * 5;
    s2 = r2 * 5;
    s3 = r3 * 5;
    s4 = r4 * 5;

    while (msg_len > 0) {
        size_t take = msg_len < 16 ? msg_len : 16;
        uint64_t hibit = 1ULL << 24;

        if (take < 16) {
            memset(block, 0, sizeof(block));
            memcpy(block, msg, take);
            block[take] = 1;
            msg = block;
            final = 1;
            hibit = 0;
        }

        t0 = load32_le(msg + 0);
        t1 = load32_le(msg + 4);
        t2 = load32_le(msg + 8);
        t3 = load32_le(msg + 12);

        h0 += t0 & 0x3ffffffULL;
        h1 += ((t0 >> 26) | ((uint64_t)t1 << 6)) & 0x3ffffffULL;
        h2 += ((t1 >> 20) | ((uint64_t)t2 << 12)) & 0x3ffffffULL;
        h3 += ((t2 >> 14) | ((uint64_t)t3 << 18)) & 0x3ffffffULL;
        h4 += ((t3 >> 8) & 0x00ffffffULL) | hibit;

        uint64_t d0 = (h0 * r0) + (h1 * s4) + (h2 * s3) + (h3 * s2) + (h4 * s1);
        uint64_t d1 = (h0 * r1) + (h1 * r0) + (h2 * s4) + (h3 * s3) + (h4 * s2);
        uint64_t d2 = (h0 * r2) + (h1 * r1) + (h2 * r0) + (h3 * s4) + (h4 * s3);
        uint64_t d3 = (h0 * r3) + (h1 * r2) + (h2 * r1) + (h3 * r0) + (h4 * s4);
        uint64_t d4 = (h0 * r4) + (h1 * r3) + (h2 * r2) + (h3 * r1) + (h4 * r0);

        uint64_t c = d0 >> 26; h0 = d0 & 0x3ffffffULL; d1 += c;
        c = d1 >> 26; h1 = d1 & 0x3ffffffULL; d2 += c;
        c = d2 >> 26; h2 = d2 & 0x3ffffffULL; d3 += c;
        c = d3 >> 26; h3 = d3 & 0x3ffffffULL; d4 += c;
        c = d4 >> 26; h4 = d4 & 0x3ffffffULL; h0 += c * 5;
        c = h0 >> 26; h0 &= 0x3ffffffULL; h1 += c;

        if (!final) {
            msg += take;
        }
        msg_len -= take;
    }

    uint64_t c = h1 >> 26; h1 &= 0x3ffffffULL; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffffULL; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffffULL; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffffULL; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffffULL; h1 += c;

    uint64_t g0 = h0 + 5;
    c = g0 >> 26; g0 &= 0x3ffffffULL;
    uint64_t g1 = h1 + c;
    c = g1 >> 26; g1 &= 0x3ffffffULL;
    uint64_t g2 = h2 + c;
    c = g2 >> 26; g2 &= 0x3ffffffULL;
    uint64_t g3 = h3 + c;
    c = g3 >> 26; g3 &= 0x3ffffffULL;
    uint64_t g4 = h4 + c - (1ULL << 26);

    uint64_t mask = (g4 >> 63) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    pad0 = load32_le(key + 16);
    pad1 = load32_le(key + 20);
    pad2 = load32_le(key + 24);
    pad3 = load32_le(key + 28);

    uint64_t f0 = (((h0) | (h1 << 26)) & 0xffffffffULL) + pad0;
    uint64_t f1 = (((h1 >> 6) | (h2 << 20)) & 0xffffffffULL) + pad1 + (f0 >> 32);
    uint64_t f2 = (((h2 >> 12) | (h3 << 14)) & 0xffffffffULL) + pad2 + (f1 >> 32);
    uint64_t f3 = (((h3 >> 18) | (h4 << 8)) & 0xffffffffULL) + pad3 + (f2 >> 32);

    store32_le(mac + 0, (uint32_t)f0);
    store32_le(mac + 4, (uint32_t)f1);
    store32_le(mac + 8, (uint32_t)f2);
    store32_le(mac + 12, (uint32_t)f3);
}

static void sealbox_append(unsigned char *buf, size_t *len, const unsigned char *data, size_t data_len,
    const unsigned char key[32], unsigned char tag[16])
{
    (void)key;
    (void)tag;
    memcpy(buf + *len, data, data_len);
    *len += data_len;
}

static size_t sealbox_padded_len(size_t len)
{
    return len + ((16 - (len % 16)) % 16);
}

static void sealbox_digest(const unsigned char box_key[32],
    const unsigned char *aad, size_t aad_len,
    const unsigned char *ciphertext, size_t ciphertext_len,
    unsigned char tag[16])
{
    size_t total = sealbox_padded_len(aad_len) + sealbox_padded_len(ciphertext_len) + 16;
    unsigned char *buf = malloc(total ? total : 1);
    unsigned char zeros[16] = {0};
    unsigned char len_block[16];
    size_t len = 0;

    if (!buf) {
        memset(tag, 0, 16);
        return;
    }

    sealbox_append(buf, &len, aad, aad_len, box_key, tag);
    if (sealbox_padded_len(aad_len) > aad_len) {
        sealbox_append(buf, &len, zeros, sealbox_padded_len(aad_len) - aad_len, box_key, tag);
    }
    sealbox_append(buf, &len, ciphertext, ciphertext_len, box_key, tag);
    if (sealbox_padded_len(ciphertext_len) > ciphertext_len) {
        sealbox_append(buf, &len, zeros, sealbox_padded_len(ciphertext_len) - ciphertext_len, box_key, tag);
    }
    store64_le(len_block + 0, (uint64_t)aad_len);
    store64_le(len_block + 8, (uint64_t)ciphertext_len);
    sealbox_append(buf, &len, len_block, sizeof(len_block), box_key, tag);

    sealbox_auth(buf, len, box_key, tag);
    memset(buf, 0, total);
    free(buf);
}

int yapf_box_lock(const unsigned char key[32], const unsigned char nonce[12],
    const unsigned char *aad, size_t aad_len,
    const unsigned char *plaintext, size_t plaintext_len,
    unsigned char *ciphertext, unsigned char tag[16])
{
    unsigned char block0[64];
    unsigned char box_key[32];

    sealbox_block(key, nonce, 0, block0);
    memcpy(box_key, block0, sizeof(box_key));
    yapf_box_stream(key, nonce, 1, plaintext, ciphertext, plaintext_len);
    sealbox_digest(box_key, aad, aad_len, ciphertext, plaintext_len, tag);
    memset(block0, 0, sizeof(block0));
    memset(box_key, 0, sizeof(box_key));

    return 0;
}

int yapf_box_open(const unsigned char key[32], const unsigned char nonce[12],
    const unsigned char *aad, size_t aad_len,
    const unsigned char *ciphertext, size_t ciphertext_len,
    const unsigned char tag[16], unsigned char *plaintext)
{
    unsigned char block0[64];
    unsigned char box_key[32];
    unsigned char actual[16];

    sealbox_block(key, nonce, 0, block0);
    memcpy(box_key, block0, sizeof(box_key));
    sealbox_digest(box_key, aad, aad_len, ciphertext, ciphertext_len, actual);

    if (!sealbox_equal(actual, tag, sizeof(actual))) {
        memset(block0, 0, sizeof(block0));
        memset(box_key, 0, sizeof(box_key));
        memset(actual, 0, sizeof(actual));
        return -1;
    }

    yapf_box_stream(key, nonce, 1, ciphertext, plaintext, ciphertext_len);
    memset(block0, 0, sizeof(block0));
    memset(box_key, 0, sizeof(box_key));
    memset(actual, 0, sizeof(actual));

    return 0;
}
