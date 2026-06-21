#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "runtime/loader.h"
#include "runtime/license.h"
#include "core/container.h"
#include "core/payload.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void yapf_free_value(char *value)
{
    if (value) {
        efree(value);
    }
}

static void yapf_replace_global_string(char **target, const char *value)
{
    if (*target) {
        efree(*target);
    }
    *target = estrdup(value ? value : "");
}

static void yapf_register_string_constant_once(const char *name, const char *value)
{
    if (!zend_get_constant_str(name, strlen(name))) {
        zend_register_string_constant(name, strlen(name), value ? value : "", CONST_CS, yapf_loader_module_entry.module_number);
    }
}

static void yapf_register_bool_constant_once(const char *name, zend_bool value)
{
    if (!zend_get_constant_str(name, strlen(name))) {
        zend_register_bool_constant(name, strlen(name), value, CONST_CS, yapf_loader_module_entry.module_number);
    }
}

static void yapf_publish_runtime_info(const char *app_id, const char *machine_code, const char *expires_at, const char *limit_type, uint64_t limit_value, uint64_t counter, time_t last_seen_at)
{
    char counter_text[32];
    char limit_value_text[32];
    char last_seen_text[32];
    const char *storage = getenv("APP_STORAGE");

    snprintf(counter_text, sizeof(counter_text), "%llu", (unsigned long long)counter);
    snprintf(limit_value_text, sizeof(limit_value_text), "%llu", (unsigned long long)limit_value);
    snprintf(last_seen_text, sizeof(last_seen_text), "%llu", (unsigned long long)last_seen_at);

    yapf_replace_global_string(&YAPF_LOADER_G(app_id), app_id);
    yapf_replace_global_string(&YAPF_LOADER_G(machine_code), machine_code);
    yapf_replace_global_string(&YAPF_LOADER_G(expires_at), expires_at);
    yapf_replace_global_string(&YAPF_LOADER_G(limit_type), limit_type);
    yapf_replace_global_string(&YAPF_LOADER_G(limit_value), limit_value_text);
    yapf_replace_global_string(&YAPF_LOADER_G(license_counter), counter_text);
    yapf_replace_global_string(&YAPF_LOADER_G(last_seen_at), last_seen_text);

    yapf_register_bool_constant_once("YAPF_LICENSE_VALID", 1);
    yapf_register_string_constant_once("YAPF_APP_ID", app_id);
    yapf_register_string_constant_once("YAPF_MACHINE_CODE", machine_code);
    yapf_register_string_constant_once("YAPF_LICENSE_EXPIRES_AT", expires_at);
    yapf_register_string_constant_once("YAPF_LICENSE_LIMIT_TYPE", limit_type);
    yapf_register_string_constant_once("YAPF_LICENSE_LIMIT_VALUE", limit_value_text);
    yapf_register_string_constant_once("YAPF_LICENSE_COUNTER", counter_text);
    yapf_register_string_constant_once("YAPF_LICENSE_LAST_SEEN_AT", last_seen_text);
    yapf_register_string_constant_once("YAPF_STORAGE_PATH", storage ? storage : "");
}

static int yapf_same_text(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static int yapf_is_null_value(const char *value)
{
    return !value || strcmp(value, "null") == 0 || *value == '\0';
}

static uint64_t yapf_parse_u64_value(const char *value, int *ok)
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

static time_t yapf_parse_time_value(const char *value, int *ok)
{
    int numeric_ok = 0;
    uint64_t numeric;
    int year;
    int month;
    int day;
    struct tm tm_value;

    *ok = 0;
    if (yapf_is_null_value(value)) {
        *ok = 1;
        return 0;
    }

    numeric = yapf_parse_u64_value(value, &numeric_ok);
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

static int yapf_write_state_payload(const char *path, const unsigned char *payload, size_t payload_len)
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

int yapf_license_validate(char *error, size_t error_size)
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

    if (YAPF_LOADER_G(license_checked)) {
        return 0;
    }

    if (!YAPF_LOADER_G(bundle) || !YAPF_LOADER_G(license) || !YAPF_LOADER_G(state)) {
        snprintf(error, error_size, "YAPF runtime paths are missing");
        return -1;
    }

    if (yapf_read_sealed_file(YAPF_LOADER_G(bundle), YAPF_KIND_APP, &app_payload, &app_len) != 0 ||
        yapf_read_sealed_file(YAPF_LOADER_G(license), YAPF_KIND_LICENSE, &license_payload, &license_len) != 0 ||
        yapf_read_sealed_file(YAPF_LOADER_G(state), YAPF_KIND_STATE, &state_payload, &state_len) != 0) {
        snprintf(error, error_size, "YAPF license files are invalid");
        goto cleanup;
    }

    app_id = yapf_payload_find_value(app_payload, app_len, "APP");
    license_app = yapf_payload_find_value(license_payload, license_len, "APP");
    license_machine = yapf_payload_find_value(license_payload, license_len, "MACHINE");
    expires_at = yapf_payload_find_value(license_payload, license_len, "EXPIRES_AT");
    limit_type = yapf_payload_find_value(license_payload, license_len, "LIMIT_TYPE");
    limit_value_text = yapf_payload_find_value(license_payload, license_len, "LIMIT_VALUE");
    license_state_id = yapf_payload_find_value(license_payload, license_len, "STATE_ID");
    license_seed_hash = yapf_payload_find_value(license_payload, license_len, "STATE_SEED_HASH");
    state_app = yapf_payload_find_value(state_payload, state_len, "APP");
    state_machine = yapf_payload_find_value(state_payload, state_len, "MACHINE");
    state_id = yapf_payload_find_value(state_payload, state_len, "STATE_ID");
    state_seed = yapf_payload_find_value(state_payload, state_len, "STATE_SEED");
    first_seen_at = yapf_payload_find_value(state_payload, state_len, "FIRST_SEEN_AT");
    runs_used_text = yapf_payload_find_value(state_payload, state_len, "RUNS_USED");
    last_seen_at = yapf_payload_find_value(state_payload, state_len, "LAST_SEEN_AT");

    if (!limit_type) {
        limit_type = estrdup("none");
    }
    if (!limit_value_text) {
        limit_value_text = estrdup("0");
    }
    if (!first_seen_at) {
        first_seen_at = estrdup("null");
    }
    if (!runs_used_text) {
        runs_used_text = estrdup("0");
    }

    if (!app_id || !license_app || !license_machine || !expires_at || !license_state_id ||
        !license_seed_hash || !state_app || !state_machine || !state_id || !state_seed ||
        !runs_used_text || !first_seen_at || !last_seen_at) {
        snprintf(error, error_size, "YAPF license metadata is incomplete");
        goto cleanup;
    }

    if (!yapf_same_text(app_id, license_app) || !yapf_same_text(app_id, state_app)) {
        snprintf(error, error_size, "YAPF license does not belong to this app");
        goto cleanup;
    }

    yapf_machine_code(machine_code, sizeof(machine_code));
    if (!yapf_same_text(machine_code, license_machine) || !yapf_same_text(machine_code, state_machine)) {
        snprintf(error, error_size, "YAPF license does not belong to this machine");
        goto cleanup;
    }

    if (!yapf_same_text(license_state_id, state_id)) {
        snprintf(error, error_size, "YAPF license and state do not match");
        goto cleanup;
    }

    seed_hash = yapf_mix_text(1469598103934665603ULL, state_seed);
    snprintf(seed_hash_text, sizeof(seed_hash_text), "%llu", (unsigned long long)seed_hash);
    if (!yapf_same_text(seed_hash_text, license_seed_hash)) {
        snprintf(error, error_size, "YAPF state was not issued for this license");
        goto cleanup;
    }

    expires_time = yapf_parse_time_value(expires_at, &time_ok);
    if (!time_ok) {
        snprintf(error, error_size, "YAPF license expiry is invalid");
        goto cleanup;
    }
    if (expires_time > 0 && now > expires_time) {
        snprintf(error, error_size, "YAPF license has expired");
        goto cleanup;
    }

    last_seen_time = yapf_parse_time_value(last_seen_at, &time_ok);
    if (!time_ok) {
        snprintf(error, error_size, "YAPF state time is invalid");
        goto cleanup;
    }
    if (last_seen_time > 0 && now + 300 < last_seen_time) {
        snprintf(error, error_size, "YAPF system clock moved backwards");
        goto cleanup;
    }

    limit_value = yapf_parse_u64_value(limit_value_text, &limit_ok);
    if (!limit_ok) {
        snprintf(error, error_size, "YAPF license limit value is invalid");
        goto cleanup;
    }

    runs_used = yapf_parse_u64_value(runs_used_text, &runs_ok);
    if (!runs_ok) {
        snprintf(error, error_size, "YAPF state runs counter is invalid");
        goto cleanup;
    }

    first_seen_time = yapf_parse_time_value(first_seen_at, &time_ok);
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
        runs_used++;
        policy_counter = runs_used;
        if (policy_counter > limit_value) {
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

    if (yapf_write_state_payload(YAPF_LOADER_G(state), (const unsigned char *)new_payload, (size_t)new_payload_len) != 0) {
        snprintf(error, error_size, "YAPF state cannot be updated");
        goto cleanup;
    }

    yapf_publish_runtime_info(app_id, machine_code, expires_at, limit_type, limit_value, policy_counter, now);
    YAPF_LOADER_G(license_checked) = 1;
    result = 0;

cleanup:
    if (app_payload) {
        free(app_payload);
    }
    if (license_payload) {
        free(license_payload);
    }
    if (state_payload) {
        free(state_payload);
    }
    yapf_free_value(app_id);
    yapf_free_value(license_app);
    yapf_free_value(license_machine);
    yapf_free_value(expires_at);
    yapf_free_value(limit_type);
    yapf_free_value(limit_value_text);
    yapf_free_value(license_state_id);
    yapf_free_value(license_seed_hash);
    yapf_free_value(state_app);
    yapf_free_value(state_machine);
    yapf_free_value(state_id);
    yapf_free_value(state_seed);
    yapf_free_value(first_seen_at);
    yapf_free_value(runs_used_text);
    yapf_free_value(last_seen_at);
    return result;
}
