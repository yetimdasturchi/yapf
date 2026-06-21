#ifndef YAPF_LOADER_H
#define YAPF_LOADER_H

#include "php.h"
#include <stddef.h>

extern zend_module_entry yapf_loader_module_entry;

ZEND_BEGIN_MODULE_GLOBALS(yapf_loader)
    char *bundle;
    char *license;
    char *state;
    char *env;
    zend_bool license_checked;
    char *app_id;
    char *machine_code;
    char *expires_at;
    char *limit_type;
    char *limit_value;
    char *license_counter;
    char *last_seen_at;
ZEND_END_MODULE_GLOBALS(yapf_loader)

ZEND_EXTERN_MODULE_GLOBALS(yapf_loader)

#define YAPF_LOADER_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(yapf_loader, v)

const char *yapf_loader_bundle_path(void);
void php_yapf_loader_init_globals(zend_yapf_loader_globals *globals);

#endif
