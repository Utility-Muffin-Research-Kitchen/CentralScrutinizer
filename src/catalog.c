#include "cs_catalog.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CS_CATALOG_MAX_BYTES (1024 * 1024)

static void cs_catalog_set_error(cs_catalog_error *error,
                                 cs_catalog_error_kind kind,
                                 const char *path,
                                 const char *message) {
    if (!error) {
        return;
    }

    error->kind = kind;
    snprintf(error->path, sizeof(error->path), "%s", path ? path : "");
    snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
}

static char *cs_catalog_strdup(const char *value) {
    size_t len;
    char *copy;

    if (!value) {
        value = "";
    }
    len = strlen(value);
    copy = (char *) malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, len + 1u);
    return copy;
}

static char *cs_catalog_json_string(cJSON *object, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);

    if (!cJSON_IsString(item) || !item->valuestring) {
        return cs_catalog_strdup("");
    }

    return cs_catalog_strdup(item->valuestring);
}

static char *cs_catalog_read_file(const char *path, cs_catalog_error *error_out) {
    FILE *file;
    long file_size;
    char *content;
    size_t read_count;

    file = fopen(path, "rb");
    if (!file) {
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_MISSING, path, "catalog file is missing");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "catalog file cannot be measured");
        return NULL;
    }
    file_size = ftell(file);
    if (file_size < 0 || file_size > CS_CATALOG_MAX_BYTES) {
        fclose(file);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "catalog file is too large");
        return NULL;
    }
    rewind(file);

    content = (char *) malloc((size_t) file_size + 1u);
    if (!content) {
        fclose(file);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_MEMORY, path, "out of memory");
        return NULL;
    }
    read_count = fread(content, 1, (size_t) file_size, file);
    fclose(file);
    if (read_count != (size_t) file_size) {
        free(content);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "catalog file cannot be read");
        return NULL;
    }
    content[read_count] = '\0';
    return content;
}

static int cs_catalog_load_string_list(cJSON *object,
                                       const char *key,
                                       cs_catalog_string_list *out) {
    cJSON *array;
    cJSON *item;
    int array_count;

    memset(out, 0, sizeof(*out));
    array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!array) {
        return 0;
    }
    if (!cJSON_IsArray(array)) {
        return -1;
    }

    array_count = cJSON_GetArraySize(array);
    if (array_count <= 0) {
        return 0;
    }

    out->items = (char **) calloc((size_t) array_count, sizeof(out->items[0]));
    if (!out->items) {
        return -1;
    }

    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item) || !item->valuestring) {
            return -1;
        }
        out->items[out->count] = cs_catalog_strdup(item->valuestring);
        if (!out->items[out->count]) {
            return -1;
        }
        out->count += 1;
    }
    return 0;
}

static int cs_catalog_load_system(cJSON *row, cs_catalog_system *out) {
    memset(out, 0, sizeof(*out));
    out->id = cs_catalog_json_string(row, "id");
    out->name = cs_catalog_json_string(row, "name");
    out->default_core = cs_catalog_json_string(row, "default_core");
    out->rom_root = cs_catalog_json_string(row, "rom_root");
    if (!out->id || !out->name || !out->default_core || !out->rom_root
        || cs_catalog_load_string_list(row, "alternate_cores", &out->alternate_cores) != 0
        || cs_catalog_load_string_list(row, "patterns", &out->patterns) != 0
        || cs_catalog_load_string_list(row, "extensions", &out->extensions) != 0) {
        return -1;
    }
    if (out->id[0] == '\0' || out->name[0] == '\0') {
        return -1;
    }
    return 0;
}

static int cs_catalog_load_core(cJSON *row, cs_catalog_core *out) {
    memset(out, 0, sizeof(*out));
    out->id = cs_catalog_json_string(row, "id");
    out->type = cs_catalog_json_string(row, "type");
    out->file_name = cs_catalog_json_string(row, "file_name");
    out->info_name = cs_catalog_json_string(row, "info_name");
    out->path = cs_catalog_json_string(row, "path");
    out->display_name = cs_catalog_json_string(row, "display_name");
    if (!out->id || !out->type || !out->file_name || !out->info_name || !out->path || !out->display_name) {
        return -1;
    }
    if (out->id[0] == '\0' || out->type[0] == '\0') {
        return -1;
    }
    return 0;
}

static void cs_catalog_string_list_free(cs_catalog_string_list *list) {
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void cs_catalog_system_free(cs_catalog_system *system) {
    if (!system) {
        return;
    }
    free(system->id);
    free(system->name);
    free(system->default_core);
    cs_catalog_string_list_free(&system->alternate_cores);
    free(system->rom_root);
    cs_catalog_string_list_free(&system->patterns);
    cs_catalog_string_list_free(&system->extensions);
}

static void cs_catalog_core_free(cs_catalog_core *core) {
    if (!core) {
        return;
    }
    free(core->id);
    free(core->type);
    free(core->file_name);
    free(core->info_name);
    free(core->path);
    free(core->display_name);
}

void cs_catalog_free(cs_catalog *catalog) {
    size_t i;

    if (!catalog) {
        return;
    }
    for (i = 0; i < catalog->system_count; ++i) {
        cs_catalog_system_free(&catalog->systems[i]);
    }
    for (i = 0; i < catalog->core_count; ++i) {
        cs_catalog_core_free(&catalog->cores[i]);
    }
    free(catalog->systems);
    free(catalog->cores);
    memset(catalog, 0, sizeof(*catalog));
}

static int cs_catalog_parse_systems(const char *path,
                                    const char *content,
                                    cs_catalog *out,
                                    cs_catalog_error *error_out) {
    cJSON *root;
    cJSON *version;
    cJSON *systems;
    cJSON *row;
    int system_count;

    root = cJSON_Parse(content);
    if (!root) {
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "systems catalog is not valid JSON");
        return -1;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != 1) {
        cJSON_Delete(root);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_VERSION, path, "unsupported systems catalog version");
        return -1;
    }
    systems = cJSON_GetObjectItemCaseSensitive(root, "systems");
    if (!cJSON_IsArray(systems)) {
        cJSON_Delete(root);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "systems catalog has no systems array");
        return -1;
    }

    system_count = cJSON_GetArraySize(systems);
    if (system_count > 0) {
        out->systems = (cs_catalog_system *) calloc((size_t) system_count, sizeof(out->systems[0]));
        if (!out->systems) {
            cJSON_Delete(root);
            cs_catalog_set_error(error_out, CS_CATALOG_ERROR_MEMORY, path, "out of memory");
            return -1;
        }
    }

    cJSON_ArrayForEach(row, systems) {
        if (!cJSON_IsObject(row) || cs_catalog_load_system(row, &out->systems[out->system_count]) != 0) {
            cJSON_Delete(root);
            cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "systems catalog row is invalid");
            return -1;
        }
        out->system_count += 1;
    }

    cJSON_Delete(root);
    return 0;
}

static int cs_catalog_parse_cores(const char *path,
                                  const char *content,
                                  cs_catalog *out,
                                  cs_catalog_error *error_out) {
    cJSON *root;
    cJSON *version;
    cJSON *cores;
    cJSON *row;
    int core_count;

    root = cJSON_Parse(content);
    if (!root) {
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "cores catalog is not valid JSON");
        return -1;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != 2) {
        cJSON_Delete(root);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_VERSION, path, "unsupported cores catalog version");
        return -1;
    }
    cores = cJSON_GetObjectItemCaseSensitive(root, "cores");
    if (!cJSON_IsArray(cores)) {
        cJSON_Delete(root);
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "cores catalog has no cores array");
        return -1;
    }

    core_count = cJSON_GetArraySize(cores);
    if (core_count > 0) {
        out->cores = (cs_catalog_core *) calloc((size_t) core_count, sizeof(out->cores[0]));
        if (!out->cores) {
            cJSON_Delete(root);
            cs_catalog_set_error(error_out, CS_CATALOG_ERROR_MEMORY, path, "out of memory");
            return -1;
        }
    }

    cJSON_ArrayForEach(row, cores) {
        if (!cJSON_IsObject(row) || cs_catalog_load_core(row, &out->cores[out->core_count]) != 0) {
            cJSON_Delete(root);
            cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, path, "cores catalog row is invalid");
            return -1;
        }
        out->core_count += 1;
    }

    cJSON_Delete(root);
    return 0;
}

int cs_catalog_load(const char *systems_path,
                    const char *cores_path,
                    cs_catalog *out,
                    cs_catalog_error *error_out) {
    char *systems_content;
    char *cores_content;
    cs_catalog temp = {0};

    if (error_out) {
        memset(error_out, 0, sizeof(*error_out));
    }
    if (!systems_path || !cores_path || !out) {
        cs_catalog_set_error(error_out, CS_CATALOG_ERROR_PARSE, "", "invalid catalog load arguments");
        return -1;
    }

    systems_content = cs_catalog_read_file(systems_path, error_out);
    if (!systems_content) {
        return -1;
    }
    cores_content = cs_catalog_read_file(cores_path, error_out);
    if (!cores_content) {
        free(systems_content);
        return -1;
    }

    if (cs_catalog_parse_systems(systems_path, systems_content, &temp, error_out) != 0
        || cs_catalog_parse_cores(cores_path, cores_content, &temp, error_out) != 0) {
        free(systems_content);
        free(cores_content);
        cs_catalog_free(&temp);
        return -1;
    }

    free(systems_content);
    free(cores_content);
    *out = temp;
    return 0;
}

const cs_catalog_system *cs_catalog_find_system(const cs_catalog *catalog, const char *id) {
    size_t i;

    if (!catalog || !id || id[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < catalog->system_count; ++i) {
        if (strcasecmp(catalog->systems[i].id, id) == 0) {
            return &catalog->systems[i];
        }
    }
    return NULL;
}

const cs_catalog_core *cs_catalog_find_core(const cs_catalog *catalog, const char *id) {
    size_t i;

    if (!catalog || !id || id[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < catalog->core_count; ++i) {
        if (strcasecmp(catalog->cores[i].id, id) == 0) {
            return &catalog->cores[i];
        }
    }
    return NULL;
}

const char *cs_catalog_error_kind_name(cs_catalog_error_kind kind) {
    switch (kind) {
        case CS_CATALOG_ERROR_NONE:
            return "none";
        case CS_CATALOG_ERROR_MISSING:
            return "missing";
        case CS_CATALOG_ERROR_PARSE:
            return "parse";
        case CS_CATALOG_ERROR_VERSION:
            return "version";
        case CS_CATALOG_ERROR_MEMORY:
            return "memory";
    }
    return "parse";
}
