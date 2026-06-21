#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "runtime/loader.h"
#include "runtime/license.h"
#include "core/container.h"
#include "core/payload.h"
#include "runtime/stream.h"
#include "php.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_stream.h"
#include "ext/standard/info.h"

ZEND_DECLARE_MODULE_GLOBALS(yapf_loader)

const char *yapf_loader_bundle_path(void)
{
    return YAPF_LOADER_G(bundle);
}

void php_yapf_loader_init_globals(zend_yapf_loader_globals *globals)
{
    globals->bundle = NULL;
    globals->license = NULL;
    globals->state = NULL;
    globals->env = NULL;
    globals->license_checked = 0;
    globals->app_id = NULL;
    globals->machine_code = NULL;
    globals->expires_at = NULL;
    globals->limit_type = NULL;
    globals->limit_value = NULL;
    globals->license_counter = NULL;
    globals->last_seen_at = NULL;
}

PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("yapf.bundle", "", PHP_INI_SYSTEM, OnUpdateString, bundle, zend_yapf_loader_globals, yapf_loader_globals)
    STD_PHP_INI_ENTRY("yapf.license", "", PHP_INI_SYSTEM, OnUpdateString, license, zend_yapf_loader_globals, yapf_loader_globals)
    STD_PHP_INI_ENTRY("yapf.state", "", PHP_INI_SYSTEM, OnUpdateString, state, zend_yapf_loader_globals, yapf_loader_globals)
    STD_PHP_INI_ENTRY("yapf.env", "", PHP_INI_SYSTEM, OnUpdateString, env, zend_yapf_loader_globals, yapf_loader_globals)
PHP_INI_END()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_yapf_start, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_yapf_info, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

static void yapf_free_value(char *value)
{
    if (value) {
        efree(value);
    }
}

static zend_string *yapf_php_string_literal(const char *value)
{
    size_t len = strlen(value);
    zend_string *literal = zend_string_alloc((len * 2) + 2, 0);
    char *out = ZSTR_VAL(literal);

    *out++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\\' || value[i] == '\'') {
            *out++ = '\\';
        }
        *out++ = value[i];
    }
    *out++ = '\'';
    *out = '\0';
    ZSTR_LEN(literal) = (size_t)(out - ZSTR_VAL(literal));

    return literal;
}

static void yapf_runtime_error(const char *message)
{
    php_printf("%s\n", message);
    EG(exit_status) = 1;
}

PHP_FUNCTION(yapf_start)
{
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    char *entry;
    zend_string *entry_uri;
    zend_string *entry_literal;
    zend_file_handle file_handle;
    char error[256];

    if (yapf_license_validate(error, sizeof(error)) != 0) {
        yapf_runtime_error(error);
        return;
    }

    if (!YAPF_LOADER_G(bundle) || yapf_read_sealed_file(YAPF_LOADER_G(bundle), YAPF_KIND_APP, &payload, &payload_len) != 0) {
        yapf_runtime_error("YAPF app container is invalid");
        return;
    }

    entry = yapf_payload_find_value(payload, payload_len, "ENTRY");
    free(payload);
    if (!entry) {
        yapf_runtime_error("YAPF entry is missing");
        return;
    }

    entry_uri = strpprintf(0, "yapf://%s", entry);
    efree(entry);
    entry_literal = yapf_php_string_literal(ZSTR_VAL(entry_uri));

    zend_string *context_code = strpprintf(
        0,
        "$GLOBALS['argv'][0]=%s;"
        "$_SERVER['argv'][0]=%s;"
        "$_SERVER['SCRIPT_FILENAME']=%s;"
        "$_SERVER['SCRIPT_NAME']=%s;"
        "$_SERVER['PHP_SELF']=%s;",
        ZSTR_VAL(entry_literal),
        ZSTR_VAL(entry_literal),
        ZSTR_VAL(entry_literal),
        ZSTR_VAL(entry_literal),
        ZSTR_VAL(entry_literal)
    );

    if (zend_eval_stringl_ex(ZSTR_VAL(context_code), ZSTR_LEN(context_code), NULL, "yapf://entry-context", false) != SUCCESS) {
        zend_string_release(context_code);
        zend_string_release(entry_literal);
        zend_string_release(entry_uri);
        yapf_runtime_error("YAPF entry context failed");
        return;
    }
    zend_string_release(context_code);

    zend_stream_init_filename(&file_handle, ZSTR_VAL(entry_uri));
    (void)zend_execute_scripts(ZEND_REQUIRE, NULL, 1, &file_handle);

    zend_string_release(entry_literal);
    zend_string_release(entry_uri);
}

PHP_FUNCTION(yapf_info)
{
    char error[256];
    int license_ok;

    array_init(return_value);
    license_ok = yapf_license_validate(error, sizeof(error)) == 0;
    add_assoc_string(return_value, "status", "loaded");
    add_assoc_bool(return_value, "license_ok", license_ok);
    if (!license_ok) {
        add_assoc_string(return_value, "license_error", error);
    }
    add_assoc_string(return_value, "app_id", YAPF_LOADER_G(app_id) ? YAPF_LOADER_G(app_id) : "");
    add_assoc_string(return_value, "machine_code", YAPF_LOADER_G(machine_code) ? YAPF_LOADER_G(machine_code) : "");
    add_assoc_string(return_value, "expires_at", YAPF_LOADER_G(expires_at) ? YAPF_LOADER_G(expires_at) : "");
    add_assoc_string(return_value, "limit_type", YAPF_LOADER_G(limit_type) ? YAPF_LOADER_G(limit_type) : "");
    add_assoc_string(return_value, "limit_value", YAPF_LOADER_G(limit_value) ? YAPF_LOADER_G(limit_value) : "");
    add_assoc_string(return_value, "license_counter", YAPF_LOADER_G(license_counter) ? YAPF_LOADER_G(license_counter) : "");
    add_assoc_string(return_value, "last_seen_at", YAPF_LOADER_G(last_seen_at) ? YAPF_LOADER_G(last_seen_at) : "");
    add_assoc_string(return_value, "storage_path", getenv("APP_STORAGE") ? getenv("APP_STORAGE") : "");
}

static const zend_function_entry yapf_loader_functions[] = {
    PHP_FE(yapf_start, arginfo_yapf_start)
    PHP_FE(yapf_info, arginfo_yapf_info)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(yapf_loader)
{
    ZEND_INIT_MODULE_GLOBALS(yapf_loader, php_yapf_loader_init_globals, NULL);
    REGISTER_INI_ENTRIES();
    if (yapf_stream_register_wrapper() != SUCCESS) {
        return FAILURE;
    }
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(yapf_loader)
{
    yapf_stream_unregister_wrapper();
    yapf_stream_clear_cache();
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}

PHP_MINFO_FUNCTION(yapf_loader)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "YAPF loader", "enabled");
    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();
}

zend_module_entry yapf_loader_module_entry = {
    STANDARD_MODULE_HEADER,
    "yapf_loader",
    yapf_loader_functions,
    PHP_MINIT(yapf_loader),
    PHP_MSHUTDOWN(yapf_loader),
    NULL,
    NULL,
    PHP_MINFO(yapf_loader),
    "0.1.0",
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_YAPF_LOADER
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
__attribute__((visibility("default"))) zend_module_entry *get_module(void)
{
    return &yapf_loader_module_entry;
}
#endif
