#ifndef YAPF_SUPERVISOR_H
#define YAPF_SUPERVISOR_H

#include <stddef.h>

int yapf_supervisor_check(const char *bundle_path, const char *license_path, const char *state_path, char *error, size_t error_size);

#endif
