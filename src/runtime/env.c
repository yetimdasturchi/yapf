#define _GNU_SOURCE
#include "runtime/env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *yapf_trim(char *value)
{
    char *end;

    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }

    end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }

    return value;
}

static char yapf_env_escape_char(char value)
{
    switch (value) {
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        default:
            return value;
    }
}

static char *yapf_unquote_env_value(char *value)
{
    size_t len = strlen(value);
    char quote;
    char *src;
    char *dst;

    if (len < 2) {
        return value;
    }

    quote = value[0];
    if ((quote != '"' && quote != '\'') || value[len - 1] != quote) {
        return value;
    }

    value[len - 1] = '\0';
    src = value + 1;
    dst = value;

    while (*src) {
        if (*src == '\\' && src[1] != '\0') {
            src++;
            *dst++ = yapf_env_escape_char(*src++);
            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';

    return value;
}

static int yapf_valid_env_key(const char *key)
{
    if (!((*key >= 'A' && *key <= 'Z') || (*key >= 'a' && *key <= 'z') || *key == '_')) {
        return 0;
    }

    for (const char *p = key + 1; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
            return 0;
        }
    }

    return 1;
}

void yapf_env_load_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[4096];

    if (!fp) {
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *key = yapf_trim(line);
        char *eq;
        char *value;

        if (*key == '\0' || *key == '#') {
            continue;
        }

        eq = strchr(key, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        value = yapf_unquote_env_value(yapf_trim(eq + 1));
        key = yapf_trim(key);

        if (!yapf_valid_env_key(key) || getenv(key)) {
            continue;
        }

        setenv(key, value, 0);
    }

    fclose(fp);
}