#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "crypto/crypto.h"
#include "core/container.h"
#include "crypto/sealbox.h"
#include "crypto/sha256.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include("machine.h")
#include "machine.h"
#endif
#endif

#ifndef YAPF_MACHINE_SALT
#error "YAPF_MACHINE_SALT must be provided by machine.h"
#endif

#ifndef YAPF_CRYPTO_SECRET
#error "YAPF_CRYPTO_SECRET must be provided by machine.h"
#endif

static void yapf_kdf(uint32_t kind, const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN],
    const char *label, unsigned char *out, size_t out_len)
{
    unsigned char salt[128];
    unsigned char info[96];
    size_t salt_len = 0;
    size_t info_len = 0;

    memcpy(salt + salt_len, YAPF_MACHINE_SALT, strlen(YAPF_MACHINE_SALT));
    salt_len += strlen(YAPF_MACHINE_SALT);
    memcpy(salt + salt_len, nonce, YAPF_CRYPTO_NONCE_LEN);
    salt_len += YAPF_CRYPTO_NONCE_LEN;

    memcpy(info + info_len, label, strlen(label));
    info_len += strlen(label);
    memcpy(info + info_len, &kind, sizeof(kind));
    info_len += sizeof(kind);

    yapf_hkdf_sha256((const unsigned char *)YAPF_CRYPTO_SECRET, strlen(YAPF_CRYPTO_SECRET),
        salt, salt_len, info, info_len, out, out_len);

    memset(salt, 0, sizeof(salt));
    memset(info, 0, sizeof(info));
}

static uint64_t yapf_inner_seed(uint32_t kind, const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN])
{
    unsigned char digest[32];
    uint64_t seed = 0;

    yapf_kdf(kind, nonce, "inner", digest, sizeof(digest));
    for (size_t i = 0; i < sizeof(seed); i++) {
        seed |= ((uint64_t)digest[i]) << (i * 8);
    }
    memset(digest, 0, sizeof(digest));

    return seed ? seed : 1469598103934665603ULL;
}

static uint64_t yapf_inner_next(uint64_t *state)
{
    uint64_t x = *state;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;

    return x;
}

static void yapf_inner_layer(uint32_t kind, const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN],
    unsigned char *data, size_t len)
{
    uint64_t key = yapf_inner_seed(kind, nonce);
    uint64_t block = 0;

    for (size_t i = 0; i < len; i++) {
        if (i % sizeof(block) == 0) {
            block = yapf_inner_next(&key);
        }
        data[i] ^= (unsigned char)(block >> ((i % sizeof(block)) * 8));
    }
}

static int yapf_read_random(unsigned char *buf, size_t len)
{
    size_t done = 0;

#ifdef SYS_getrandom
    while (done < len) {
        ssize_t n = getrandom(buf + done, len - done, 0);
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        break;
    }
    if (done == len) {
        return 0;
    }
#endif

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        done = 0;
        while (done < len) {
            ssize_t n = read(fd, buf + done, len - done);
            if (n <= 0) {
                break;
            }
            done += (size_t)n;
        }
        close(fd);
        if (done == len) {
            return 0;
        }
    }

    return -1;
}

int yapf_crypto_nonce(unsigned char nonce[YAPF_CRYPTO_NONCE_LEN])
{
    if (yapf_read_random(nonce, YAPF_CRYPTO_NONCE_LEN) == 0) {
        return 0;
    }

    memset(nonce, 0, YAPF_CRYPTO_NONCE_LEN);
    return -1;
}

int yapf_crypto_encrypt(uint32_t kind, const unsigned char *aad, size_t aad_len,
    const unsigned char *plaintext, size_t plaintext_len,
    const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN],
    unsigned char **ciphertext, size_t *ciphertext_len,
    unsigned char tag[YAPF_CRYPTO_TAG_LEN])
{
    unsigned char key[32];
    unsigned char *work = NULL;

    *ciphertext = NULL;
    *ciphertext_len = 0;

    work = malloc(plaintext_len ? plaintext_len : 1);
    if (!work) {
        return -1;
    }
    if (plaintext_len) {
        memcpy(work, plaintext, plaintext_len);
        yapf_inner_layer(kind, nonce, work, plaintext_len);
    }

    *ciphertext = malloc(plaintext_len ? plaintext_len : 1);
    if (!*ciphertext) {
        free(work);
        return -1;
    }

    yapf_kdf(kind, nonce, "outer", key, sizeof(key));
    if (yapf_box_lock(key, nonce, aad, aad_len, work, plaintext_len, *ciphertext, tag) != 0) {
        free(*ciphertext);
        *ciphertext = NULL;
        free(work);
        memset(key, 0, sizeof(key));
        return -1;
    }

    *ciphertext_len = plaintext_len;
    memset(work, 0, plaintext_len);
    free(work);
    memset(key, 0, sizeof(key));

    return 0;
}

int yapf_crypto_decrypt(uint32_t kind, const unsigned char *aad, size_t aad_len,
    const unsigned char *ciphertext, size_t ciphertext_len,
    const unsigned char nonce[YAPF_CRYPTO_NONCE_LEN],
    const unsigned char tag[YAPF_CRYPTO_TAG_LEN],
    unsigned char **plaintext, size_t *plaintext_len)
{
    unsigned char key[32];

    *plaintext = NULL;
    *plaintext_len = 0;

    *plaintext = malloc(ciphertext_len ? ciphertext_len : 1);
    if (!*plaintext) {
        return -1;
    }

    yapf_kdf(kind, nonce, "outer", key, sizeof(key));
    if (yapf_box_open(key, nonce, aad, aad_len, ciphertext, ciphertext_len, tag, *plaintext) != 0) {
        free(*plaintext);
        *plaintext = NULL;
        memset(key, 0, sizeof(key));
        return -1;
    }

    if (ciphertext_len) {
        yapf_inner_layer(kind, nonce, *plaintext, ciphertext_len);
    }
    *plaintext_len = ciphertext_len;
    memset(key, 0, sizeof(key));

    return 0;
}
