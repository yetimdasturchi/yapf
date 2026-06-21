#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "core/payload.h"

#include <stdlib.h>
#include <string.h>

unsigned char *yapf_payload_find_line_end(unsigned char *pos, unsigned char *end)
{
    while (pos < end && *pos != '\n') {
        pos++;
    }
    return pos < end ? pos : NULL;
}

char *yapf_payload_find_file(unsigned char *payload, size_t payload_len, const char *target, size_t *code_len)
{
    unsigned char *pos = payload;
    unsigned char *end = payload + payload_len;
    size_t target_len = strlen(target);

    while (pos < end) {
        unsigned char *line_end = yapf_payload_find_line_end(pos, end);
        if (!line_end) {
            return NULL;
        }

        if ((size_t)(line_end - pos) > 5 && memcmp(pos, "FILE ", 5) == 0) {
            unsigned char *name = pos + 5;
            size_t name_len = (size_t)(line_end - name);
            pos = line_end + 1;

            line_end = yapf_payload_find_line_end(pos, end);
            if (!line_end || (size_t)(line_end - pos) < 5 || memcmp(pos, "DATA ", 5) != 0) {
                return NULL;
            }

            long data_len = strtol((const char *)(pos + 5), NULL, 10);
            if (data_len < 0) {
                return NULL;
            }

            pos = line_end + 1;
            if (pos + data_len > end) {
                return NULL;
            }

            if (name_len == target_len && memcmp(name, target, target_len) == 0) {
                char *code = emalloc((size_t)data_len + 1);
                memcpy(code, pos, (size_t)data_len);
                code[data_len] = '\0';
                *code_len = (size_t)data_len;
                return code;
            }

            pos += data_len;
            if (pos < end && *pos == '\n') {
                pos++;
            }

            line_end = yapf_payload_find_line_end(pos, end);
            if (!line_end || (size_t)(line_end - pos) != 3 || memcmp(pos, "END", 3) != 0) {
                return NULL;
            }
            pos = line_end + 1;
            continue;
        }

        pos = line_end + 1;
    }

    return NULL;
}

char *yapf_payload_find_value(unsigned char *payload, size_t payload_len, const char *key)
{
    unsigned char *pos = payload;
    unsigned char *end = payload + payload_len;
    size_t key_len = strlen(key);

    while (pos < end) {
        unsigned char *line_end = yapf_payload_find_line_end(pos, end);
        if (!line_end) {
            return NULL;
        }

        if ((size_t)(line_end - pos) > key_len + 1 && memcmp(pos, key, key_len) == 0 && pos[key_len] == ' ') {
            size_t value_len = (size_t)(line_end - (pos + key_len + 1));
            char *value = emalloc(value_len + 1);
            memcpy(value, pos + key_len + 1, value_len);
            value[value_len] = '\0';
            return value;
        }

        pos = line_end + 1;
    }

    return NULL;
}

int yapf_payload_has_dir(unsigned char *payload, size_t payload_len, const char *target)
{
    unsigned char *pos = payload;
    unsigned char *end = payload + payload_len;
    size_t target_len = strlen(target);

    if (*target == '\0') {
        return 1;
    }

    while (pos < end) {
        unsigned char *line_end = yapf_payload_find_line_end(pos, end);
        if (!line_end) {
            return 0;
        }

        if ((size_t)(line_end - pos) > 4 && memcmp(pos, "DIR ", 4) == 0) {
            unsigned char *name = pos + 4;
            size_t name_len = (size_t)(line_end - name);
            if (name_len == target_len && memcmp(name, target, target_len) == 0) {
                return 1;
            }
        }

        pos = line_end + 1;
    }

    return 0;
}
