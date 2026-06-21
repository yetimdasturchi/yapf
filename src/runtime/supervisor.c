#define _GNU_SOURCE
#include "runtime/supervisor.h"
#include "core/container.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static int same_text(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static int is_null_value(const char *value)
{
    return !value || strcmp(value, "null") == 0 || *value == '\0';
}

static uint64_t parse_u64_value(const char *value, int *ok)
{
    uint64_t result = 0;

    *ok = 0;
    if (!value || !*value) {
        return 0;
    }

    for (const char *p = value; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        result = result * 10ULL + (uint64_t)(*p - '0');
    }

    *ok = 1;
    return result;
}

static time_t parse_time_value(const char *value, int *ok)
{
    int numeric_ok = 0;
    uint64_t numeric;
    int year;
    int month;
    int day;
    struct tm tm_value;

    *ok = 0;
    if (is_null_value(value)) {
        *ok = 1;
        return 0;
    }

    numeric = parse_u64_value(value, &numeric_ok);
    if (numeric_ok) {
        *ok = 1;
        return (time_t)numeric;
    }

    if (sscanf(value, "%d-%d-%d", &year, &month, &day) != 3) {
        return 0;
    }

    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = 23;
    tm_value.tm_min = 59;
    tm_value.tm_sec = 59;
    tm_value.tm_isdst = -1;

    *ok = 1;
    return mktime(&tm_value);
}

static int write_state_payload(const char *path, const unsigned char *payload, size_t payload_len)
{
    char tmp_path[4096];
    FILE *fp;
    int result = -1;

    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp_path)) {
        return -1;
    }

    fp = fopen(tmp_path, "wb");
    if (!fp) {
        return -1;
    }

    if (yapf_write_sealed(fp, YAPF_KIND_STATE, payload, payload_len) == 0 &&
        fflush(fp) == 0 &&
        fsync(fileno(fp)) == 0) {
        result = 0;
    }
    if (fclose(fp) != 0) {
        result = -1;
    }
    if (result == 0 && rename(tmp_path, path) != 0) {
        result = -1;
    }
    if (result != 0) {
        unlink(tmp_path);
    }
    return result;
}

int yapf_supervisor_check(const char *bundle_path, const char *license_path, const char *state_path, char *error, size_t error_size)
{
    unsigned char *app_payload = NULL;
    unsigned char *license_payload = NULL;
    unsigned char *state_payload = NULL;
    size_t app_len = 0;
    size_t license_len = 0;
    size_t state_len = 0;
    char machine_code[64];
    char *app_id = NULL;
    char *license_app = NULL;
    char *license_machine = NULL;
    char *expires_at = NULL;
    char *limit_type = NULL;
    char *limit_value_text = NULL;
    char *license_state_id = NULL;
    char *license_seed_hash = NULL;
    char *state_app = NULL;
    char *state_machine = NULL;
    char *state_id = NULL;
    char *state_seed = NULL;
    char *first_seen_at = NULL;
    char *runs_used_text = NULL;
    char *last_seen_at = NULL;
    time_t now = time(NULL);
    time_t expires_time = 0;
    time_t first_seen_time = 0;
    time_t last_seen_time = 0;
    int time_ok = 0;
    int limit_ok = 0;
    int runs_ok = 0;
    uint64_t limit_value = 0;
    uint64_t runs_used = 0;
    uint64_t policy_counter = 0;
    uint64_t seed_hash = 0;
    char seed_hash_text[64];
    char new_payload[2048];
    int new_payload_len;
    int result = -1;

    if (yapf_read_sealed_file(bundle_path, YAPF_KIND_APP, &app_payload, &app_len) != 0 ||
        yapf_read_sealed_file(license_path, YAPF_KIND_LICENSE, &license_payload, &license_len) != 0 ||
        yapf_read_sealed_file(state_path, YAPF_KIND_STATE, &state_payload, &state_len) != 0) {
        snprintf(error, error_size, "YAPF license files are invalid");
        goto cleanup;
    }

    app_id = copy_value(app_payload, app_len, "APP");
    license_app = copy_value(license_payload, license_len, "APP");
    license_machine = copy_value(license_payload, license_len, "MACHINE");
    expires_at = copy_value(license_payload, license_len, "EXPIRES_AT");
    limit_type = copy_value(license_payload, license_len, "LIMIT_TYPE");
    limit_value_text = copy_value(license_payload, license_len, "LIMIT_VALUE");
    license_state_id = copy_value(license_payload, license_len, "STATE_ID");
    license_seed_hash = copy_value(license_payload, license_len, "STATE_SEED_HASH");
    state_app = copy_value(state_payload, state_len, "APP");
    state_machine = copy_value(state_payload, state_len, "MACHINE");
    state_id = copy_value(state_payload, state_len, "STATE_ID");
    state_seed = copy_value(state_payload, state_len, "STATE_SEED");
    first_seen_at = copy_value(state_payload, state_len, "FIRST_SEEN_AT");
    runs_used_text = copy_value(state_payload, state_len, "RUNS_USED");
    last_seen_at = copy_value(state_payload, state_len, "LAST_SEEN_AT");

    if (!limit_type) {
        limit_type = strdup("none");
    }
    if (!limit_value_text) {
        limit_value_text = strdup("0");
    }
    if (!first_seen_at) {
        first_seen_at = strdup("null");
    }
    if (!runs_used_text) {
        runs_used_text = strdup("0");
    }

    if (!app_id || !license_app || !license_machine || !expires_at || !license_state_id ||
        !license_seed_hash || !state_app || !state_machine || !state_id || !state_seed ||
        !runs_used_text || !first_seen_at || !last_seen_at) {
        snprintf(error, error_size, "YAPF license metadata is incomplete");
        goto cleanup;
    }

    if (!same_text(app_id, license_app) || !same_text(app_id, state_app)) {
        snprintf(error, error_size, "YAPF license does not belong to this app");
        goto cleanup;
    }

    yapf_machine_code(machine_code, sizeof(machine_code));
    if (!same_text(machine_code, license_machine) || !same_text(machine_code, state_machine)) {
        snprintf(error, error_size, "YAPF license does not belong to this machine");
        goto cleanup;
    }

    if (!same_text(license_state_id, state_id)) {
        snprintf(error, error_size, "YAPF license and state do not match");
        goto cleanup;
    }

    seed_hash = yapf_mix_text(1469598103934665603ULL, state_seed);
    snprintf(seed_hash_text, sizeof(seed_hash_text), "%llu", (unsigned long long)seed_hash);
    if (!same_text(seed_hash_text, license_seed_hash)) {
        snprintf(error, error_size, "YAPF state was not issued for this license");
        goto cleanup;
    }

    expires_time = parse_time_value(expires_at, &time_ok);
    if (!time_ok) {
        snprintf(error, error_size, "YAPF license expiry is invalid");
        goto cleanup;
    }
    if (expires_time > 0 && now > expires_time) {
        snprintf(error, error_size, "YAPF license has expired");
        goto cleanup;
    }

    last_seen_time = parse_time_value(last_seen_at, &time_ok);
    if (!time_ok) {
        snprintf(error, error_size, "YAPF state time is invalid");
        goto cleanup;
    }
    if (last_seen_time > 0 && now + 300 < last_seen_time) {
        snprintf(error, error_size, "YAPF system clock moved backwards");
        goto cleanup;
    }

    limit_value = parse_u64_value(limit_value_text, &limit_ok);
    if (!limit_ok) {
        snprintf(error, error_size, "YAPF license limit value is invalid");
        goto cleanup;
    }

    runs_used = parse_u64_value(runs_used_text, &runs_ok);
    if (!runs_ok) {
        snprintf(error, error_size, "YAPF state runs counter is invalid");
        goto cleanup;
    }

    first_seen_time = parse_time_value(first_seen_at, &time_ok);
    if (!time_ok) {
        snprintf(error, error_size, "YAPF first seen time is invalid");
        goto cleanup;
    }
    if (first_seen_time <= 0) {
        first_seen_time = now;
    }
    if (first_seen_time > 0 && now + 300 < first_seen_time) {
        snprintf(error, error_size, "YAPF first seen time is in the future");
        goto cleanup;
    }

    if (strcmp(limit_type, "none") == 0) {
        policy_counter = 0;
    } else if (strcmp(limit_type, "days") == 0) {
        if (limit_value == 0) {
            snprintf(error, error_size, "YAPF day limit is invalid");
            goto cleanup;
        }
        policy_counter = (uint64_t)(((now - first_seen_time) / 86400) + 1);
        if (policy_counter > limit_value) {
            snprintf(error, error_size, "YAPF day limit has been reached");
            goto cleanup;
        }
    } else if (strcmp(limit_type, "runs") == 0) {
        if (limit_value == 0) {
            snprintf(error, error_size, "YAPF run limit is invalid");
            goto cleanup;
        }
        if (runs_used > limit_value) {
            snprintf(error, error_size, "YAPF run limit has been reached");
            goto cleanup;
        }
    } else {
        snprintf(error, error_size, "YAPF license limit type is invalid");
        goto cleanup;
    }

    new_payload_len = snprintf(new_payload, sizeof(new_payload),
        "APP %s\nMACHINE %s\nSTATE_ID %s\nSTATE_SEED %s\nFIRST_SEEN_AT %llu\nRUNS_USED %llu\nLAST_SEEN_AT %llu\n",
        app_id,
        machine_code,
        state_id,
        state_seed,
        (unsigned long long)first_seen_time,
        (unsigned long long)runs_used,
        (unsigned long long)now);
    if (new_payload_len < 0 || (size_t)new_payload_len >= sizeof(new_payload)) {
        snprintf(error, error_size, "YAPF state payload is too large");
        goto cleanup;
    }

    if (write_state_payload(state_path, (const unsigned char *)new_payload, (size_t)new_payload_len) != 0) {
        snprintf(error, error_size, "YAPF state cannot be updated");
        goto cleanup;
    }

    result = 0;

cleanup:
    free(app_payload);
    free(license_payload);
    free(state_payload);
    free(app_id);
    free(license_app);
    free(license_machine);
    free(expires_at);
    free(limit_type);
    free(limit_value_text);
    free(license_state_id);
    free(license_seed_hash);
    free(state_app);
    free(state_machine);
    free(state_id);
    free(state_seed);
    free(first_seen_at);
    free(runs_used_text);
    free(last_seen_at);
    return result;
}
