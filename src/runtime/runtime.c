#define _GNU_SOURCE
#include "runtime/runtime.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int yapf_path_exists(const char *path)
{
    return access(path, R_OK) == 0;
}

void yapf_join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);

    if (dir_len + 1 + name_len + 1 > out_size) {
        fprintf(stderr, "path is too long\n");
        exit(1);
    }

    memcpy(out, dir, dir_len);
    out[dir_len] = '/';
    memcpy(out + dir_len + 1, name, name_len + 1);
}

void yapf_app_dir(char *out, size_t out_size)
{
    char exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    char *slash;

    if (len <= 0) {
        fprintf(stderr, "cannot resolve executable path: %s\n", strerror(errno));
        exit(1);
    }

    exe[len] = '\0';
    slash = strrchr(exe, '/');
    if (!slash) {
        fprintf(stderr, "cannot resolve application directory\n");
        exit(1);
    }
    *slash = '\0';

    snprintf(out, out_size, "%s", exe);
}
