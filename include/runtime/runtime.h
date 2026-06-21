#ifndef YAPF_RUNTIME_H
#define YAPF_RUNTIME_H

#include <stddef.h>

int yapf_path_exists(const char *path);
void yapf_join_path(char *out, size_t out_size, const char *dir, const char *name);
void yapf_app_dir(char *out, size_t out_size);

#endif
