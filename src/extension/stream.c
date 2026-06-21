#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "runtime/license.h"
#include "runtime/loader.h"
#include "core/payload.h"
#include "runtime/stream.h"
#include "core/container.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "main/php_memory_streams.h"
#include "main/php_streams.h"

typedef struct {
    char **names;
    unsigned char *types;
    size_t count;
    size_t index;
} yapf_dir_listing;

static unsigned char *cached_payload;
static size_t cached_payload_len;
static char *cached_bundle;

static int yapf_load_app_payload(const unsigned char **payload, size_t *payload_len)
{
    char error[256];
    const char *bundle = yapf_loader_bundle_path();
    unsigned char *next_payload = NULL;
    size_t next_payload_len = 0;

    *payload = NULL;
    *payload_len = 0;

    if (yapf_license_validate(error, sizeof(error)) != 0 || !bundle) {
        return -1;
    }

    if (cached_payload && cached_bundle && strcmp(cached_bundle, bundle) == 0) {
        *payload = cached_payload;
        *payload_len = cached_payload_len;
        return 0;
    }

    if (yapf_read_sealed_file(bundle, YAPF_KIND_APP, &next_payload, &next_payload_len) != 0) {
        return -1;
    }

    free(cached_payload);
    free(cached_bundle);
    cached_payload = next_payload;
    cached_payload_len = next_payload_len;
    cached_bundle = strdup(bundle);
    if (!cached_bundle) {
        free(cached_payload);
        cached_payload = NULL;
        cached_payload_len = 0;
        return -1;
    }

    *payload = cached_payload;
    *payload_len = cached_payload_len;
    return 0;
}

static void yapf_dir_listing_add(yapf_dir_listing *listing, const char *name, unsigned char type)
{
    size_t name_len = strlen(name);

    if (name_len == 0) {
        return;
    }

    for (size_t i = 0; i < listing->count; i++) {
        if (strcmp(listing->names[i], name) == 0) {
            if (type == DT_DIR) {
                listing->types[i] = DT_DIR;
            }
            return;
        }
    }

    listing->names = erealloc(listing->names, sizeof(char *) * (listing->count + 1));
    listing->types = erealloc(listing->types, sizeof(unsigned char) * (listing->count + 1));
    listing->names[listing->count] = estrdup(name);
    listing->types[listing->count] = type;
    listing->count++;
}

static int yapf_direct_child(const char *path, const char *dir, char *child, size_t child_size, unsigned char *type)
{
    const char *rest = path;
    const char *slash;
    size_t dir_len = strlen(dir);
    size_t child_len;

    if (dir_len > 0) {
        if (strncmp(path, dir, dir_len) != 0 || path[dir_len] != '/') {
            return 0;
        }
        rest = path + dir_len + 1;
    }

    if (*rest == '\0') {
        return 0;
    }

    slash = strchr(rest, '/');
    if (slash) {
        child_len = (size_t)(slash - rest);
        *type = DT_DIR;
    } else {
        child_len = strlen(rest);
    }

    if (child_len == 0 || child_len >= child_size) {
        return 0;
    }

    memcpy(child, rest, child_len);
    child[child_len] = '\0';
    return 1;
}

static yapf_dir_listing *yapf_build_dir_listing(unsigned char *payload, size_t payload_len, const char *target)
{
    unsigned char *pos = payload;
    unsigned char *end = payload + payload_len;
    yapf_dir_listing *listing = ecalloc(1, sizeof(*listing));

    yapf_dir_listing_add(listing, ".", DT_DIR);
    yapf_dir_listing_add(listing, "..", DT_DIR);

    while (pos < end) {
        unsigned char *line_end = yapf_payload_find_line_end(pos, end);
        unsigned char *name = NULL;
        size_t name_len = 0;
        unsigned char type = DT_UNKNOWN;
        char path[4096];
        char child[NAME_MAX + 1];

        if (!line_end) {
            break;
        }

        if ((size_t)(line_end - pos) > 4 && memcmp(pos, "DIR ", 4) == 0) {
            name = pos + 4;
            name_len = (size_t)(line_end - name);
            type = DT_DIR;
        } else if ((size_t)(line_end - pos) > 5 && memcmp(pos, "FILE ", 5) == 0) {
            name = pos + 5;
            name_len = (size_t)(line_end - name);
            type = DT_REG;
        }

        if (name && name_len < sizeof(path)) {
            memcpy(path, name, name_len);
            path[name_len] = '\0';
            if (yapf_direct_child(path, target, child, sizeof(child), &type)) {
                yapf_dir_listing_add(listing, child, type);
            }
        }

        pos = line_end + 1;
    }

    return listing;
}

static void yapf_dir_listing_free(yapf_dir_listing *listing)
{
    if (!listing) {
        return;
    }

    for (size_t i = 0; i < listing->count; i++) {
        efree(listing->names[i]);
    }
    efree(listing->names);
    efree(listing->types);
    efree(listing);
}

static char *normalize_yapf_path(const char *filename)
{
    const char *path = filename;
    char *copy;
    char *token;
    char *save = NULL;
    char *parts[256];
    size_t count = 0;
    size_t total = 1;
    char *normalized;
    char *out;

    if (strncmp(path, "yapf://", 7) == 0) {
        path += 7;
    }

    while (*path == '/') {
        path++;
    }

    copy = estrdup(path);
    for (token = php_strtok_r(copy, "/", &save); token; token = php_strtok_r(NULL, "/", &save)) {
        if (strcmp(token, ".") == 0 || *token == '\0') {
            continue;
        }
        if (strcmp(token, "..") == 0) {
            if (count > 0) {
                count--;
            }
            continue;
        }
        if (count >= sizeof(parts) / sizeof(parts[0])) {
            efree(copy);
            return NULL;
        }
        parts[count++] = token;
        total += strlen(token) + 1;
    }

    normalized = emalloc(total);
    out = normalized;
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(parts[i]);
        if (i > 0) {
            *out++ = '/';
        }
        memcpy(out, parts[i], len);
        out += len;
    }
    *out = '\0';

    efree(copy);
    return normalized;
}

static php_stream *yapf_stream_opener(php_stream_wrapper *wrapper, const char *filename, const char *mode, int options, zend_string **opened_path, php_stream_context *context STREAMS_DC)
{
    const unsigned char *payload = NULL;
    size_t payload_len = 0;
    char *file_payload;
    size_t file_len = 0;
    zend_string *buffer;
    php_stream *stream;
    char *path = normalize_yapf_path(filename);

    (void)wrapper;
    (void)options;
    (void)context;

    if (!path || !mode || mode[0] != 'r' || yapf_load_app_payload(&payload, &payload_len) != 0) {
        if (path) {
            efree(path);
        }
        return NULL;
    }

    file_payload = yapf_payload_find_file((unsigned char *)payload, payload_len, path, &file_len);
    efree(path);
    if (!file_payload) {
        return NULL;
    }

    buffer = zend_string_init(file_payload, file_len, 0);
    efree(file_payload);
    stream = php_stream_memory_open(TEMP_STREAM_READONLY, buffer);
    zend_string_release(buffer);

    if (opened_path) {
        *opened_path = zend_string_init(filename, strlen(filename), 0);
    }

    return stream;
}

static int yapf_url_stat(php_stream_wrapper *wrapper, const char *url, int flags, php_stream_statbuf *ssb, php_stream_context *context)
{
    const unsigned char *payload = NULL;
    size_t payload_len = 0;
    char *file_payload;
    size_t file_len = 0;
    char *path = normalize_yapf_path(url);
    int is_dir = 0;

    (void)wrapper;
    (void)flags;
    (void)context;

    memset(ssb, 0, sizeof(*ssb));

    if (!path || yapf_load_app_payload(&payload, &payload_len) != 0) {
        if (path) {
            efree(path);
        }
        return -1;
    }

    file_payload = yapf_payload_find_file((unsigned char *)payload, payload_len, path, &file_len);
    if (!file_payload) {
        is_dir = yapf_payload_has_dir((unsigned char *)payload, payload_len, path);
    }
    efree(path);
    if (!file_payload && !is_dir) {
        return -1;
    }

    if (file_payload) {
        efree(file_payload);
        ssb->sb.st_mode = S_IFREG | 0444;
        ssb->sb.st_size = (zend_off_t)file_len;
    } else {
        ssb->sb.st_mode = S_IFDIR | 0555;
        ssb->sb.st_size = 0;
    }
    return 0;
}

static ssize_t yapf_dir_read(php_stream *stream, char *buf, size_t count)
{
    yapf_dir_listing *listing = (yapf_dir_listing *)stream->abstract;
    php_stream_dirent *entry = (php_stream_dirent *)buf;

    if (!listing || count < sizeof(php_stream_dirent) || listing->index >= listing->count) {
        return 0;
    }

    memset(entry, 0, sizeof(*entry));
    snprintf(entry->d_name, sizeof(entry->d_name), "%s", listing->names[listing->index]);
    entry->d_type = listing->types[listing->index];
    listing->index++;
    return sizeof(php_stream_dirent);
}

static int yapf_dir_close(php_stream *stream, int close_handle)
{
    (void)close_handle;
    yapf_dir_listing_free((yapf_dir_listing *)stream->abstract);
    return 0;
}

static int yapf_dir_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffset)
{
    yapf_dir_listing *listing = (yapf_dir_listing *)stream->abstract;

    if (!listing || whence != SEEK_SET || offset < 0 || (size_t)offset > listing->count) {
        return -1;
    }

    listing->index = (size_t)offset;
    if (newoffset) {
        *newoffset = (zend_off_t)listing->index;
    }
    return 0;
}

static const php_stream_ops yapf_dir_stream_ops = {
    NULL,
    yapf_dir_read,
    yapf_dir_close,
    NULL,
    "YAPF directory",
    yapf_dir_seek,
    NULL,
    NULL,
    NULL
};

static php_stream *yapf_dir_opener(php_stream_wrapper *wrapper, const char *filename, const char *mode, int options, zend_string **opened_path, php_stream_context *context STREAMS_DC)
{
    const unsigned char *payload = NULL;
    size_t payload_len = 0;
    char *path = normalize_yapf_path(filename);
    yapf_dir_listing *listing;
    php_stream *stream;

    (void)wrapper;
    (void)options;
    (void)context;

    if (!path || !mode || mode[0] != 'r' || yapf_load_app_payload(&payload, &payload_len) != 0) {
        if (path) {
            efree(path);
        }
        return NULL;
    }

    if (!yapf_payload_has_dir((unsigned char *)payload, payload_len, path)) {
        efree(path);
        return NULL;
    }

    listing = yapf_build_dir_listing((unsigned char *)payload, payload_len, path);
    efree(path);

    stream = php_stream_alloc(&yapf_dir_stream_ops, listing, NULL, "r");
    if (opened_path) {
        *opened_path = zend_string_init(filename, strlen(filename), 0);
    }
    return stream;
}

static const php_stream_wrapper_ops yapf_stream_wrapper_ops = {
    yapf_stream_opener,
    NULL,
    NULL,
    yapf_url_stat,
    yapf_dir_opener,
    "YAPF",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static const php_stream_wrapper yapf_stream_wrapper = {
    &yapf_stream_wrapper_ops,
    NULL,
    0
};

int yapf_stream_register_wrapper(void)
{
    return php_register_url_stream_wrapper("yapf", &yapf_stream_wrapper);
}

void yapf_stream_unregister_wrapper(void)
{
    php_unregister_url_stream_wrapper("yapf");
}

void yapf_stream_clear_cache(void)
{
    free(cached_payload);
    free(cached_bundle);
    cached_payload = NULL;
    cached_payload_len = 0;
    cached_bundle = NULL;
}
