#include "core/container.h"
#include "crypto/crypto.h"
#include <dirent.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("machine.h")
#include "machine.h"
#endif
#endif

#ifndef YAPF_MACHINE_SALT_DATA_LEN
#error "YAPF machine config must be provided by machine.h"
#endif

#define YAPF_SEAL_HEAD_LEN 64U
#define YAPF_SEAL_AUTH_LEN 26U

uint64_t yapf_mix_bytes(uint64_t hash, const unsigned char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t yapf_mix_text(uint64_t hash, const char *data)
{
    return yapf_mix_bytes(hash, (const unsigned char *)data, strlen(data));
}

static int yapf_read_exact(FILE *fp, unsigned char *buf, size_t len)
{
    return fread(buf, 1, len, fp) == len ? 0 : -1;
}

static uint16_t yapf_read_u16_value(const unsigned char *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint64_t yapf_read_u64_value(const unsigned char *buf)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= ((uint64_t)buf[i]) << (i * 8);
    }
    return value;
}

static void yapf_put_u16(unsigned char *buf, uint16_t value)
{
    buf[0] = (unsigned char)(value & 0xffU);
    buf[1] = (unsigned char)((value >> 8) & 0xffU);
}

static void yapf_put_u64(unsigned char *buf, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        buf[i] = (unsigned char)((value >> (i * 8)) & 0xffU);
    }
}

int yapf_read_sealed_file(const char *path, uint32_t expected_kind, unsigned char **payload, size_t *payload_len)
{
    FILE *fp = fopen(path, "rb");
    unsigned char header[YAPF_SEAL_HEAD_LEN];
    uint16_t kind;
    uint64_t ciphertext_len;
    unsigned char *ciphertext;

    *payload = NULL;
    *payload_len = 0;

    if (!fp) {
        return -1;
    }

    if (yapf_read_exact(fp, header, sizeof(header)) != 0) {
        fclose(fp);
        return -1;
    }

    if (memcmp(header, "YAPF", 4) != 0) {
        fclose(fp);
        return -1;
    }

    kind = yapf_read_u16_value(header + 4);
    ciphertext_len = yapf_read_u64_value(header + 6);

    if (kind != expected_kind) {
        fclose(fp);
        return -1;
    }

    ciphertext = malloc((size_t)ciphertext_len ? (size_t)ciphertext_len : 1);
    if (!ciphertext) {
        fclose(fp);
        return -1;
    }

    if (ciphertext_len && yapf_read_exact(fp, ciphertext, (size_t)ciphertext_len) != 0) {
        free(ciphertext);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (yapf_crypto_decrypt(kind, header, YAPF_SEAL_AUTH_LEN,
        ciphertext, (size_t)ciphertext_len,
        header + 14, header + 26,
        payload, payload_len) != 0) {
        free(ciphertext);
        return -1;
    }

    free(ciphertext);
    return 0;
}

int yapf_write_sealed(FILE *fp, uint32_t kind, const unsigned char *payload, size_t payload_len)
{
    unsigned char header[YAPF_SEAL_HEAD_LEN] = {0};
    unsigned char *ciphertext = NULL;
    size_t ciphertext_len = 0;

    memcpy(header, "YAPF", 4);
    yapf_put_u16(header + 4, (uint16_t)kind);
    yapf_put_u64(header + 6, (uint64_t)payload_len);

    if (yapf_crypto_nonce(header + 14) != 0 ||
        yapf_crypto_encrypt(kind, header, YAPF_SEAL_AUTH_LEN,
            payload, payload_len, header + 14,
            &ciphertext, &ciphertext_len, header + 26) != 0) {
        return -1;
    }

    if (fwrite(header, 1, sizeof(header), fp) != sizeof(header) ||
        fwrite(ciphertext, 1, ciphertext_len, fp) != ciphertext_len) {
        free(ciphertext);
        return -1;
    }

    free(ciphertext);
    return 0;
}

static uint64_t yapf_hash_text_value(uint64_t hash, const char *label, const char *value)
{
    hash = yapf_mix_text(hash, label);
    hash = yapf_mix_text(hash, ":present:");
    return yapf_mix_text(hash, value);
}

static uint64_t yapf_hash_file_line(uint64_t hash, const char *label, const char *path)
{
    FILE *fp = fopen(path, "r");
    char buf[512];

    if (!fp) {
        return yapf_mix_text(yapf_mix_text(hash, label), ":missing");
    }

    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return yapf_mix_text(yapf_mix_text(hash, label), ":empty");
    }

    fclose(fp);
    buf[strcspn(buf, "\r\n")] = '\0';
    return yapf_hash_text_value(hash, label, buf);
}

static uint64_t yapf_hash_disk_serial(uint64_t hash)
{
    DIR *dir = opendir("/sys/block");
    struct dirent *entry;
    char path[512];
    int found = 0;

    if (!dir) {
        return yapf_mix_text(hash, "disk_serial:missing");
    }

    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (strncmp(entry->d_name, "loop", 4) == 0 || strncmp(entry->d_name, "ram", 3) == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/block/%s/device/serial", entry->d_name);
        hash = yapf_hash_file_line(hash, "disk_serial", path);
        found = 1;
        break;
    }

    closedir(dir);
    return found ? hash : yapf_mix_text(hash, "disk_serial:missing");
}

static void yapf_format_machine_code(uint64_t a, uint64_t b, char *out, size_t out_size)
{
    const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    char raw[26];
    size_t pos = 0;
    size_t out_pos = 0;

    if (out_size < 36) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return;
    }

    for (int i = 0; i < 13; i++) {
        raw[pos++] = alphabet[a & 31U];
        a >>= 5U;
    }
    for (int i = 0; i < 12; i++) {
        raw[pos++] = alphabet[b & 31U];
        b >>= 5U;
    }
    raw[pos] = '\0';

    memcpy(out, "YAPF-", 5);
    out_pos = 5;
    for (size_t i = 0; i < 25; i++) {
        if (i > 0 && i % 5 == 0) {
            out[out_pos++] = '-';
        }
        out[out_pos++] = raw[i];
    }
    out[out_pos] = '\0';
}

void yapf_machine_code(char *out, size_t out_size)
{
    uint64_t a = 1469598103934665603ULL;
    uint64_t b = 1099511628211ULL;
    unsigned char *machine_salt = NULL;

    a = yapf_hash_file_line(a, "machine_id", "/etc/machine-id");
    a = yapf_hash_file_line(a, "product_uuid", "/sys/class/dmi/id/product_uuid");
    a = yapf_hash_file_line(a, "board_serial", "/sys/class/dmi/id/board_serial");
    a = yapf_hash_disk_serial(a);

    machine_salt = malloc(YAPF_MACHINE_SALT_DATA_LEN ? YAPF_MACHINE_SALT_DATA_LEN : 1);
    if (!machine_salt) {
        out[0] = '\0';
        return;
    }
    yapf_unmask_config(YAPF_MACHINE_SALT_DATA, YAPF_MACHINE_SALT_DATA_LEN, machine_salt);
    b = yapf_mix_bytes(a ^ b, machine_salt, YAPF_MACHINE_SALT_DATA_LEN);
    memset(machine_salt, 0, YAPF_MACHINE_SALT_DATA_LEN);
    free(machine_salt);

    yapf_format_machine_code(a, b, out, out_size);
}