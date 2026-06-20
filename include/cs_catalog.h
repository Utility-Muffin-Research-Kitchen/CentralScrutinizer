#ifndef CS_CATALOG_H
#define CS_CATALOG_H

#include <stddef.h>

#include "cs_paths.h"

typedef struct cs_catalog_string_list {
    char **items;
    size_t count;
} cs_catalog_string_list;

typedef struct cs_catalog_system {
    char *id;
    char *name;
    char *default_core;
    cs_catalog_string_list alternate_cores;
    char *rom_root;
    cs_catalog_string_list patterns;
    cs_catalog_string_list extensions;
} cs_catalog_system;

typedef struct cs_catalog_core {
    char *id;
    char *type;
    char *file_name;
    char *info_name;
    char *path;
    char *display_name;
} cs_catalog_core;

typedef struct cs_catalog {
    cs_catalog_system *systems;
    size_t system_count;
    cs_catalog_core *cores;
    size_t core_count;
} cs_catalog;

typedef enum cs_catalog_error_kind {
    CS_CATALOG_ERROR_NONE = 0,
    CS_CATALOG_ERROR_MISSING,
    CS_CATALOG_ERROR_PARSE,
    CS_CATALOG_ERROR_VERSION,
    CS_CATALOG_ERROR_MEMORY
} cs_catalog_error_kind;

typedef struct cs_catalog_error {
    cs_catalog_error_kind kind;
    char path[CS_PATH_MAX];
    char message[256];
} cs_catalog_error;

int cs_catalog_load(const char *systems_path,
                    const char *cores_path,
                    cs_catalog *out,
                    cs_catalog_error *error_out);
void cs_catalog_free(cs_catalog *catalog);

const cs_catalog_system *cs_catalog_find_system(const cs_catalog *catalog, const char *id);
const cs_catalog_core *cs_catalog_find_core(const cs_catalog *catalog, const char *id);
const char *cs_catalog_error_kind_name(cs_catalog_error_kind kind);

#endif
