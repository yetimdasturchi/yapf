#include "cli/args.h"
#include "core/container.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t kind_value(const char *kind)
{
    if (strcmp(kind, "license") == 0) {
        return YAPF_KIND_LICENSE;
    }
    if (strcmp(kind, "state") == 0) {
        return YAPF_KIND_STATE;
    }
    return 0;
}

static void print_help(void)
{
    printf("Usage: yapf-seal --kind license|state --output FILE --app-id ID --machine-code CODE --state-id ID --state-seed SEED [options]\n\n");
    printf("Create sealed license or mutable state files for a packed app.\n\n");
    printf("Required:\n");
    printf("  --kind license|state  File type to create.\n");
    printf("  --output FILE         Output path, usually license.yapfl or license.yapfs.\n");
    printf("  --app-id ID           Must match the app container id.\n");
    printf("  --machine-code CODE   Machine id from yapf-client.\n");
    printf("  --state-id ID         Shared id binding license and state together.\n");
    printf("  --state-seed SEED     Shared secret binding license and state together.\n\n");
    printf("License options:\n");
    printf("  --expires-at VALUE    null, YYYY-MM-DD, or epoch timestamp. Default: null.\n");
    printf("  --limit-type TYPE     none, days, or runs. Default: none.\n");
    printf("  --limit-value N       Limit value. Default: 0.\n");
    printf("  --help                Show this help.\n");
}

static int valid_limit_type(const char *limit_type)
{
    return strcmp(limit_type, "none") == 0 || strcmp(limit_type, "days") == 0 || strcmp(limit_type, "runs") == 0;
}

static int valid_limit_value(const char *limit_value)
{
    if (!limit_value || !*limit_value) {
        return 0;
    }
    for (const char *p = limit_value; *p; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *kind_name = yapf_arg_value(argc, argv, "--kind");
    const char *output = yapf_arg_value(argc, argv, "--output");
    const char *app_id = yapf_arg_value(argc, argv, "--app-id");
    const char *machine_code = yapf_arg_value(argc, argv, "--machine-code");
    const char *state_id = yapf_arg_value(argc, argv, "--state-id");
    const char *state_seed = yapf_arg_value(argc, argv, "--state-seed");
    const char *expires_at = yapf_arg_value(argc, argv, "--expires-at");
    const char *limit_type = yapf_arg_value(argc, argv, "--limit-type");
    const char *limit_value = yapf_arg_value(argc, argv, "--limit-value");
    uint32_t kind;
    FILE *fp;
    char payload[2048];
    int n;

    if (yapf_arg_has(argc, argv, "--help") || yapf_arg_has(argc, argv, "-h")) {
        print_help();
        return 0;
    }

    if (!kind_name || !output || !app_id || !machine_code || !state_id || !state_seed) {
        print_help();
        return 2;
    }

    kind = kind_value(kind_name);
    if (!kind) {
        fprintf(stderr, "invalid kind: %s\n", kind_name);
        return 2;
    }

    if (kind == YAPF_KIND_LICENSE) {
        uint64_t seed_hash = yapf_mix_text(1469598103934665603ULL, state_seed);
        if (!expires_at || !*expires_at) {
            expires_at = "null";
        }
        if (!limit_type || !*limit_type) {
            limit_type = "none";
        }
        if (!limit_value || !*limit_value) {
            limit_value = "0";
        }
        if (!valid_limit_type(limit_type) || !valid_limit_value(limit_value)) {
            fprintf(stderr, "invalid license limit policy\n");
            return 2;
        }
        if (strcmp(limit_type, "none") != 0 && strcmp(limit_value, "0") == 0) {
            fprintf(stderr, "license limit value must be greater than zero\n");
            return 2;
        }
        n = snprintf(payload, sizeof(payload),
            "APP %s\nMACHINE %s\nEXPIRES_AT %s\nLIMIT_TYPE %s\nLIMIT_VALUE %s\nSTATE_ID %s\nSTATE_SEED_HASH %llu\n",
            app_id, machine_code, expires_at, limit_type, limit_value, state_id, (unsigned long long)seed_hash);
    } else {
        n = snprintf(payload, sizeof(payload),
            "APP %s\nMACHINE %s\nSTATE_ID %s\nSTATE_SEED %s\nFIRST_SEEN_AT null\nRUNS_USED 0\nLAST_SEEN_AT null\n",
            app_id, machine_code, state_id, state_seed);
    }

    if (n < 0 || (size_t)n >= sizeof(payload)) {
        fprintf(stderr, "payload is too large\n");
        return 1;
    }

    fp = fopen(output, "wb");
    if (!fp) {
        fprintf(stderr, "cannot open output: %s\n", strerror(errno));
        return 1;
    }

    if (yapf_write_sealed(fp, kind, (const unsigned char *)payload, (size_t)n) != 0) {
        fprintf(stderr, "cannot write sealed file\n");
        fclose(fp);
        return 1;
    }

    fclose(fp);
    return 0;
}
