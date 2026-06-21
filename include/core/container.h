#ifndef YAPF_CONTAINER_H
#define YAPF_CONTAINER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define YAPF_KIND_APP 1U
#define YAPF_KIND_LICENSE 2U
#define YAPF_KIND_STATE 3U

#if defined(__GNUC__)
#define YAPF_UNUSED __attribute__((unused))
#else
#define YAPF_UNUSED
#endif

uint64_t yapf_mix_bytes(uint64_t hash, const unsigned char *data, size_t len);
uint64_t yapf_mix_text(uint64_t hash, const char *data);

int yapf_read_sealed_file(const char *path, uint32_t expected_kind, unsigned char **payload, size_t *payload_len);
int yapf_write_sealed(FILE *fp, uint32_t kind, const unsigned char *payload, size_t payload_len);

void yapf_machine_code(char *out, size_t out_size);

#endif
