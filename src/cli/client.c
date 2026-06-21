#include "core/container.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int file_exists(const char *path)
{
    return access(path, R_OK) == 0;
}

static void path_join(char *out, size_t out_size, const char *dir, const char *file)
{
    size_t len = strlen(dir);
    snprintf(out, out_size, "%s%s%s", dir, (len > 0 && dir[len - 1] == '/') ? "" : "/", file);
}

static char *copy_value(const unsigned char *payload, size_t len, const char *key)
{
    size_t key_len = strlen(key);
    size_t pos = 0;

    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && payload[pos] != '\n') {
            pos++;
        }

        size_t line_len = pos - line_start;
        if (line_len > key_len && memcmp(payload + line_start, key, key_len) == 0 && payload[line_start + key_len] == ' ') {
            size_t value_start = line_start + key_len + 1;
            size_t value_len = line_start + line_len - value_start;
            char *value = malloc(value_len + 1);
            if (!value) {
                return NULL;
            }
            memcpy(value, payload + value_start, value_len);
            value[value_len] = '\0';
            return value;
        }

        if (pos < len && payload[pos] == '\n') {
            pos++;
        }
    }

    return NULL;
}

static unsigned char *read_sealed_or_null(const char *path, unsigned int kind, size_t *len)
{
    unsigned char *payload = NULL;
    if (!file_exists(path)) {
        return NULL;
    }
    if (yapf_read_sealed_file(path, kind, &payload, len) != 0) {
        return NULL;
    }
    return payload;
}

static void print_field(const char *label, const char *value)
{
    printf("%-18s %s\n", label, (value && value[0]) ? value : "-");
}

static int is_expired(const char *expires_at)
{
    if (!expires_at || strcmp(expires_at, "null") == 0 || strcmp(expires_at, "never") == 0) {
        return 0;
    }

    char *end = NULL;
    long long epoch = strtoll(expires_at, &end, 10);
    if (end && *end == '\0' && epoch > 0) {
        return time(NULL) > (time_t)epoch;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(expires_at, "%d-%d-%d", &year, &month, &day) == 3) {
        struct tm tm_value;
        memset(&tm_value, 0, sizeof(tm_value));
        tm_value.tm_year = year - 1900;
        tm_value.tm_mon = month - 1;
        tm_value.tm_mday = day + 1;
        return time(NULL) >= mktime(&tm_value);
    }

    return 0;
}

static int print_app_info(const char *app_dir, int explicit_app)
{
    char app_path[PATH_MAX];
    char license_path[PATH_MAX];
    char state_path[PATH_MAX];
    char current_machine[64];
    size_t app_len = 0;
    size_t license_len = 0;
    size_t state_len = 0;
    unsigned char *app_payload = NULL;
    unsigned char *license_payload = NULL;
    unsigned char *state_payload = NULL;
    char *app_id = NULL;
    char *entry = NULL;
    char *license_app = NULL;
    char *license_machine = NULL;
    char *expires_at = NULL;
    char *limit_type = NULL;
    char *limit_value = NULL;
    char *license_state_id = NULL;
    char *state_app = NULL;
    char *state_machine = NULL;
    char *state_id = NULL;
    char *first_seen_at = NULL;
    char *last_seen_at = NULL;
    char *runs_used = NULL;
    int status = 0;

    path_join(app_path, sizeof(app_path), app_dir, "app.yapfc");
    path_join(license_path, sizeof(license_path), app_dir, "license.yapfl");
    path_join(state_path, sizeof(state_path), app_dir, "license.yapfs");

    if (!file_exists(license_path) && !file_exists(state_path) && !explicit_app) {
        return 1;
    }

    app_payload = read_sealed_or_null(app_path, YAPF_KIND_APP, &app_len);
    license_payload = read_sealed_or_null(license_path, YAPF_KIND_LICENSE, &license_len);
    state_payload = read_sealed_or_null(state_path, YAPF_KIND_STATE, &state_len);

    if (!license_payload || !state_payload) {
        fprintf(stderr, "license files are missing or invalid in %s\n", app_dir);
        status = 2;
        goto cleanup;
    }

    if (app_payload) {
        app_id = copy_value(app_payload, app_len, "APP");
        entry = copy_value(app_payload, app_len, "ENTRY");
    }
    license_app = copy_value(license_payload, license_len, "APP");
    license_machine = copy_value(license_payload, license_len, "MACHINE");
    expires_at = copy_value(license_payload, license_len, "EXPIRES_AT");
    limit_type = copy_value(license_payload, license_len, "LIMIT_TYPE");
    limit_value = copy_value(license_payload, license_len, "LIMIT_VALUE");
    license_state_id = copy_value(license_payload, license_len, "STATE_ID");
    state_app = copy_value(state_payload, state_len, "APP");
    state_machine = copy_value(state_payload, state_len, "MACHINE");
    state_id = copy_value(state_payload, state_len, "STATE_ID");
    first_seen_at = copy_value(state_payload, state_len, "FIRST_SEEN_AT");
    last_seen_at = copy_value(state_payload, state_len, "LAST_SEEN_AT");
    runs_used = copy_value(state_payload, state_len, "RUNS_USED");

    yapf_machine_code(current_machine, sizeof(current_machine));

    int machine_ok = license_machine && state_machine &&
        strcmp(license_machine, current_machine) == 0 &&
        strcmp(state_machine, current_machine) == 0;
    int app_ok = license_app && state_app && strcmp(license_app, state_app) == 0;
    int state_ok = license_state_id && state_id && strcmp(license_state_id, state_id) == 0;
    int expired = is_expired(expires_at);

    printf("YAPF license\n");
    print_field("App", app_id ? app_id : license_app);
    print_field("Entry", entry);
    print_field("Machine", machine_ok ? "valid" : "mismatch");
    print_field("License", (machine_ok && app_ok && state_ok && !expired) ? "valid" : "invalid");
    print_field("Expires", expires_at);
    print_field("Limit type", limit_type);
    print_field("Limit value", limit_value);
    print_field("Runs used", runs_used);
    print_field("First seen", first_seen_at);
    print_field("Last seen", last_seen_at);
    print_field("Current machine", current_machine);

cleanup:
    free(app_payload);
    free(license_payload);
    free(state_payload);
    free(app_id);
    free(entry);
    free(license_app);
    free(license_machine);
    free(expires_at);
    free(limit_type);
    free(limit_value);
    free(license_state_id);
    free(state_app);
    free(state_machine);
    free(state_id);
    free(first_seen_at);
    free(last_seen_at);
    free(runs_used);
    return status;
}

static void print_help(void)
{
    printf("Usage: client [--raw] [--app DIR]\n\n");
    printf("Print the machine id or YAPF license information for an app folder.\n\n");
    printf("Options:\n");
    printf("  --raw       Print only the machine id value.\n");
    printf("  --app DIR   Print license information for DIR.\n");
    printf("  --help      Show this help.\n");
}

int main(int argc, char **argv)
{
    char code[64];
    int raw = 0;
    const char *app_dir = ".";
    int explicit_app = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "--raw") == 0) {
            raw = 1;
            continue;
        }
        if (strcmp(argv[i], "--app") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--app requires a value\n");
                return 2;
            }
            app_dir = argv[++i];
            explicit_app = 1;
            continue;
        }
        fprintf(stderr, "unknown option: %s\n", argv[i]);
        print_help();
        return 2;
    }

    yapf_machine_code(code, sizeof(code));
    if (raw) {
        printf("%s\n", code);
    } else {
        int app_result = print_app_info(app_dir, explicit_app);
        if (app_result == 1) {
            printf("Your machine id: %s\n", code);
        } else {
            return app_result;
        }
    }
    return 0;
}
