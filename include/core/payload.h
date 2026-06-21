#ifndef YAPF_PAYLOAD_H
#define YAPF_PAYLOAD_H

#include <stddef.h>

char *yapf_payload_find_file(unsigned char *payload, size_t payload_len, const char *target, size_t *code_len);
char *yapf_payload_find_value(unsigned char *payload, size_t payload_len, const char *key);
int yapf_payload_has_dir(unsigned char *payload, size_t payload_len, const char *target);
unsigned char *yapf_payload_find_line_end(unsigned char *pos, unsigned char *end);

#endif
