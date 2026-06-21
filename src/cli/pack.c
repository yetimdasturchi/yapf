#define _XOPEN_SOURCE 700
#include "cli/args.h"
#include "core/container.h"
#include <errno.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *source_dir;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} payload_buffer;

typedef struct {
    char **items;
    size_t count;
} string_list;

static payload_buffer payload;
static string_list custom_excludes;
static string_list allowed_exts;
static string_list allowed_files;

static void print_help(void)
{
    printf("Usage: yapf-pack --source DIR --output FILE --app-id ID [--entry FILE]\n\n");
    printf("Pack a PHP source directory into a sealed app container.\n\n");
    printf("Options:\n");
    printf("  --source DIR      PHP project directory to pack.\n");
    printf("  --output FILE     Output app container path, usually app.yapfc.\n");
    printf("  --app-id ID       Application identifier used by license/state files.\n");
    printf("  --entry FILE      Main PHP file inside the source tree. Default: public/index.php.\n");
    printf("  --exclude PATH    Exclude a path or path segment. Can be repeated.\n");
    printf("  --allow-ext LIST  Only include files with these comma-separated extensions.\n");
    printf("  --allow-file PATH Include an exact file path when --allow-ext is active. Can be repeated.\n");
    printf("  --help            Show this help.\n");
}

static char *copy_range(const char *start, size_t len)
{
    char *value = malloc(len + 1);

    if (!value) {
        return NULL;
    }
    memcpy(value, start, len);
    value[len] = '\0';
    return value;
}

static void trim_range(const char **start, size_t *len)
{
    while (*len > 0 && ((*start)[0] == ' ' || (*start)[0] == '\t' || (*start)[0] == '\n' || (*start)[0] == '\r')) {
        (*start)++;
        (*len)--;
    }
    while (*len > 0 && ((*start)[*len - 1] == ' ' || (*start)[*len - 1] == '\t' || (*start)[*len - 1] == '\n' || (*start)[*len - 1] == '\r')) {
        (*len)--;
    }
}

static int list_add_range(string_list *list, const char *start, size_t len)
{
    char **next;
    char *value;

    trim_range(&start, &len);
    while (len > 0 && *start == '/') {
        start++;
        len--;
    }
    if (len == 0) {
        return 0;
    }

    value = copy_range(start, len);
    if (!value) {
        return -1;
    }

    next = realloc(list->items, sizeof(char *) * (list->count + 1));
    if (!next) {
        free(value);
        return -1;
    }
    list->items = next;
    list->items[list->count++] = value;
    return 0;
}

static int list_add_value(string_list *list, const char *value)
{
    return list_add_range(list, value, strlen(value));
}

static int list_add_csv(string_list *list, const char *value)
{
    const char *start = value;
    const char *p = value;

    while (1) {
        if (*p == ',' || *p == '\0') {
            if (list_add_range(list, start, (size_t)(p - start)) != 0) {
                return -1;
            }
            if (*p == '\0') {
                return 0;
            }
            start = p + 1;
        }
        p++;
    }
}

static void list_free(string_list *list)
{
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static int parse_pack_options(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--exclude") == 0 || strcmp(argv[i], "--allow-ext") == 0 || strcmp(argv[i], "--allow-file") == 0) && i + 1 >= argc) {
            fprintf(stderr, "%s requires a value\n", argv[i]);
            return -1;
        }
        if (strcmp(argv[i], "--exclude") == 0) {
            if (list_add_value(&custom_excludes, argv[++i]) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--allow-ext") == 0) {
            if (list_add_csv(&allowed_exts, argv[++i]) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--allow-file") == 0) {
            if (list_add_value(&allowed_files, argv[++i]) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int payload_reserve(size_t extra)
{
    size_t needed;
    unsigned char *next;
    size_t cap = payload.cap ? payload.cap : 8192;

    if (extra > ((size_t)-1) - payload.len) {
        return -1;
    }
    needed = payload.len + extra;

    while (cap < needed) {
        if (cap > ((size_t)-1) / 2) {
            return -1;
        }
        cap *= 2;
    }
    if (cap == payload.cap) {
        return 0;
    }

    next = realloc(payload.data, cap);
    if (!next) {
        return -1;
    }
    payload.data = next;
    payload.cap = cap;
    return 0;
}

static int payload_append(const void *data, size_t len)
{
    if (payload_reserve(len) != 0) {
        return -1;
    }
    memcpy(payload.data + payload.len, data, len);
    payload.len += len;
    return 0;
}

static int payload_appendf(const char *format, ...)
{
    va_list args;
    va_list copy;
    int len;
    char *buffer;
    int result;

    va_start(args, format);
    va_copy(copy, args);
    len = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (len < 0) {
        va_end(args);
        return -1;
    }

    buffer = malloc((size_t)len + 1);
    if (!buffer) {
        va_end(args);
        return -1;
    }
    vsnprintf(buffer, (size_t)len + 1, format, args);
    va_end(args);

    result = payload_append(buffer, (size_t)len);
    free(buffer);
    return result;
}

static int path_has_segment(const char *path, const char *segment)
{
    size_t segment_len = strlen(segment);
    const char *p = path;

    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (strncmp(p, segment, segment_len) == 0 && (p[segment_len] == '/' || p[segment_len] == '\0')) {
            return 1;
        }
        while (*p && *p != '/') {
            p++;
        }
    }

    return 0;
}

static int path_matches_filter(const char *relative, const char *filter)
{
    size_t filter_len = strlen(filter);

    return strcmp(relative, filter) == 0 ||
        (filter_len > 0 && strncmp(relative, filter, filter_len) == 0 && relative[filter_len] == '/') ||
        path_has_segment(relative, filter);
}

static int is_custom_excluded(const char *relative)
{
    for (size_t i = 0; i < custom_excludes.count; i++) {
        if (path_matches_filter(relative, custom_excludes.items[i])) {
            return 1;
        }
    }

    return 0;
}

static int should_skip_relative(const char *relative)
{
    return strcmp(relative, ".env") == 0 ||
        path_has_segment(relative, ".git") ||
        path_has_segment(relative, ".svn") ||
        path_has_segment(relative, "node_modules") ||
        path_has_segment(relative, "storage") ||
        strcmp(relative, "var/cache") == 0 ||
        strncmp(relative, "var/cache/", 10) == 0 ||
        strcmp(relative, "bootstrap/cache") == 0 ||
        strncmp(relative, "bootstrap/cache/", 16) == 0;
}

static int extension_matches(const char *relative, const char *allowed)
{
    const char *slash = strrchr(relative, '/');
    const char *name = slash ? slash + 1 : relative;
    const char *dot = strrchr(name, '.');
    const char *ext = allowed;

    while (*ext == '.') {
        ext++;
    }
    return dot && dot[1] != '\0' && strcmp(dot + 1, ext) == 0;
}

static int is_allowed_file(const char *relative)
{
    for (size_t i = 0; i < allowed_files.count; i++) {
        if (strcmp(relative, allowed_files.items[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static int passes_file_whitelist(const char *relative)
{
    if (allowed_exts.count == 0) {
        return 1;
    }
    if (is_allowed_file(relative)) {
        return 1;
    }
    for (size_t i = 0; i < allowed_exts.count; i++) {
        if (extension_matches(relative, allowed_exts.items[i])) {
            return 1;
        }
    }

    return 0;
}

static int append_file_content(const char *path, long long size)
{
    FILE *fp = fopen(path, "rb");
    unsigned char buf[8192];
    size_t n;

    if (!fp) {
        return -1;
    }

    if (payload_appendf("DATA %lld\n", size) != 0) {
        fclose(fp);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (payload_append(buf, n) != 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    if (payload_append("\nEND\n", 5) != 0) {
        return -1;
    }
    return 0;
}

static int write_entry(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    const char *relative;

    (void)ftw;

    if (type == FTW_D) {
        relative = path + strlen(source_dir);
        if (*relative == '/') {
            relative++;
        }
        if (should_skip_relative(relative) || is_custom_excluded(relative)) {
            return 0;
        }
        if (*relative != '\0') {
            if (payload_appendf("DIR %s\n", relative) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (type != FTW_F) {
        return 0;
    }

    relative = path + strlen(source_dir);
    if (*relative == '/') {
        relative++;
    }
    if (should_skip_relative(relative) || is_custom_excluded(relative) || !passes_file_whitelist(relative)) {
        return 0;
    }

    if (payload_appendf("FILE %s\n", relative) != 0) {
        return -1;
    }
    if (append_file_content(path, (long long)st->st_size) != 0) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *output = yapf_arg_value(argc, argv, "--output");
    const char *app_id = yapf_arg_value(argc, argv, "--app-id");
    const char *entry = yapf_arg_value(argc, argv, "--entry");
    FILE *bundle;
    time_t now = time(NULL);

    if (yapf_arg_has(argc, argv, "--help") || yapf_arg_has(argc, argv, "-h")) {
        print_help();
        return 0;
    }

    source_dir = yapf_arg_value(argc, argv, "--source");

    if (!entry) {
        entry = "public/index.php";
    }

    if (!source_dir || !output || !app_id) {
        print_help();
        return 2;
    }
    if (parse_pack_options(argc, argv) != 0) {
        return 2;
    }

    if (payload_appendf("APP %s\nENTRY %s\nCREATED_AT %lld\n", app_id, entry, (long long)now) != 0) {
        fprintf(stderr, "cannot allocate payload\n");
        list_free(&custom_excludes);
        list_free(&allowed_exts);
        list_free(&allowed_files);
        return 1;
    }

    if (nftw(source_dir, write_entry, 32, FTW_PHYS) != 0) {
        fprintf(stderr, "cannot scan source: %s\n", strerror(errno));
        free(payload.data);
        list_free(&custom_excludes);
        list_free(&allowed_exts);
        list_free(&allowed_files);
        return 1;
    }

    bundle = fopen(output, "wb");
    if (!bundle) {
        fprintf(stderr, "cannot open output: %s\n", strerror(errno));
        free(payload.data);
        list_free(&custom_excludes);
        list_free(&allowed_exts);
        list_free(&allowed_files);
        return 1;
    }

    if (yapf_write_sealed(bundle, YAPF_KIND_APP, payload.data, payload.len) != 0) {
        fprintf(stderr, "cannot write sealed bundle\n");
        fclose(bundle);
        free(payload.data);
        list_free(&custom_excludes);
        list_free(&allowed_exts);
        list_free(&allowed_files);
        return 1;
    }

    fclose(bundle);
    free(payload.data);
    list_free(&custom_excludes);
    list_free(&allowed_exts);
    list_free(&allowed_files);
    return 0;
}
