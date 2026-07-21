#include "cs_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *env_first(const char *first_name, const char *second_name) {
    const char *value;

    if (first_name) {
        value = getenv(first_name);
        if (value && value[0] != '\0') {
            return value;
        }
    }
    if (second_name) {
        value = getenv(second_name);
        if (value && value[0] != '\0') {
            return value;
        }
    }
    return NULL;
}

static int write_value(char *dst, size_t size, const char *value, const char *fallback) {
    const char *source = (value != NULL && value[0] != '\0') ? value : fallback;
    int written = snprintf(dst, size, "%s", source);

    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

static int write_sdcard_root(char *dst, size_t size, const char *value, const char *fallback) {
    const char *source = (value != NULL && value[0] != '\0') ? value : fallback;

    if (!dst || size == 0 || !source) {
        return -1;
    }

    if (source[0] == '/') {
        char resolved[CS_PATH_MAX];

        if (realpath(source, resolved) != NULL) {
            source = resolved;
        }
    }

    return write_value(dst, size, source, fallback);
}

static int write_joined(char *dst, size_t size, const char *prefix, const char *suffix) {
    int written = snprintf(dst, size, "%s%s", prefix, suffix);

    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

static int write_joined_component(char *dst, size_t size, const char *left, const char *right) {
    int written;

    if (!left || !right) {
        return -1;
    }
    if (right[0] == '\0') {
        written = snprintf(dst, size, "%s", left);
    } else {
        written = snprintf(dst, size, "%s/%s", left, right);
    }

    return (written < 0 || (size_t) written >= size) ? -1 : 0;
}

static int write_env_or_joined(char *dst,
                               size_t size,
                               const char *env_name,
                               const char *fallback_root,
                               const char *fallback_suffix) {
    const char *value = env_name ? getenv(env_name) : NULL;

    if (value && value[0] != '\0') {
        return write_value(dst, size, value, NULL);
    }

    return write_joined(dst, size, fallback_root, fallback_suffix);
}

static int split_nth_colon_value(const char *list, size_t index, char *dst, size_t dst_size) {
    const char *start;
    size_t current = 0;

    if (!dst || dst_size == 0) {
        return -1;
    }
    dst[0] = '\0';
    if (!list || list[0] == '\0') {
        return 0;
    }

    start = list;
    while (1) {
        const char *end = strchr(start, ':');
        size_t len = end ? (size_t) (end - start) : strlen(start);

        if (current == index) {
            if (len >= dst_size) {
                return -1;
            }
            memcpy(dst, start, len);
            dst[len] = '\0';
            return 0;
        }
        if (!end) {
            return 0;
        }
        start = end + 1;
        current += 1;
    }
}

static int derive_source_alias(const char *root, size_t index, char *dst, size_t dst_size) {
    const char *base;
    int written;

    if (!dst || dst_size == 0) {
        return -1;
    }

    base = root ? strrchr(root, '/') : NULL;
    base = base ? base + 1 : root;
    if (!base || base[0] == '\0') {
        base = "source";
    }

    written = snprintf(dst, dst_size, "%s", base);
    if (written < 0 || (size_t) written >= dst_size) {
        return -1;
    }
    if (index > 0 && strcmp(dst, "sdcard") != 0 && strcmp(dst, "SDCARD") != 0) {
        return 0;
    }
    if (index == 0) {
        return 0;
    }

    written = snprintf(dst, dst_size, "%s%zu", base, index);
    return (written < 0 || (size_t) written >= dst_size) ? -1 : 0;
}

static int source_alias_exists(const cs_paths *paths, const char *alias, size_t count) {
    size_t i;

    if (!paths || !alias) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(paths->sources[i].alias, alias) == 0) {
            return 1;
        }
    }
    return 0;
}

static int make_source_alias_unique(cs_paths *paths, size_t index) {
    char base[sizeof(paths->sources[index].alias)];
    size_t suffix = 2;

    if (!paths || index >= CS_PATH_SOURCE_MAX) {
        return -1;
    }
    if (!source_alias_exists(paths, paths->sources[index].alias, index)) {
        return 0;
    }
    if (write_value(base, sizeof(base), paths->sources[index].alias, "source") != 0) {
        return -1;
    }

    do {
        int written = snprintf(paths->sources[index].alias,
                               sizeof(paths->sources[index].alias),
                               "%s-%zu",
                               base,
                               suffix++);

        if (written < 0 || (size_t) written >= sizeof(paths->sources[index].alias)) {
            return -1;
        }
    } while (source_alias_exists(paths, paths->sources[index].alias, index));

    return 0;
}

static int populate_source_path(char *dst,
                                size_t dst_size,
                                const char *plural_env,
                                const char *singular_env,
                                size_t index,
                                const char *source_root,
                                const char *suffix) {
    char plural_value[CS_PATH_MAX];
    const char *singular_value;

    if (split_nth_colon_value(plural_env, index, plural_value, sizeof(plural_value)) != 0) {
        return -1;
    }
    if (plural_value[0] != '\0') {
        return write_value(dst, dst_size, plural_value, NULL);
    }
    singular_value = (index == 0 && singular_env) ? getenv(singular_env) : NULL;
    if (singular_value && singular_value[0] != '\0') {
        return write_value(dst, dst_size, singular_value, NULL);
    }
    return write_joined(dst, dst_size, source_root, suffix);
}

static int source_fixture_available(size_t index) {
    const char *fixture = getenv("CS_SOURCE_TEST_AVAILABLE");
    return fixture && fixture[0]
           && (strcmp(fixture, "all") == 0
               || (index == 0 && strstr(fixture, "primary") != NULL)
               || (index == 1 && strstr(fixture, "secondary_sd") != NULL));
}

#if defined(__linux__)
static int unescape_mountpoint(char *out, size_t out_size, const char *raw) {
    size_t used = 0;
    size_t i;
    for (i = 0; raw && raw[i]; ++i) {
        unsigned char value = (unsigned char) raw[i];
        if (raw[i] == '\\'
            && raw[i + 1] >= '0' && raw[i + 1] <= '7'
            && raw[i + 2] >= '0' && raw[i + 2] <= '7'
            && raw[i + 3] >= '0' && raw[i + 3] <= '7') {
            value = (unsigned char) (((raw[i + 1] - '0') << 6)
                                     | ((raw[i + 2] - '0') << 3)
                                     | (raw[i + 3] - '0'));
            i += 3;
        }
        if (used + 1 >= out_size) {
            return 0;
        }
        out[used++] = (char) value;
    }
    out[used] = '\0';
    return 1;
}

static int mountinfo_has_exact_root(const char *root) {
    const char *fixture = getenv("CS_SOURCE_TEST_MOUNTINFO");
    FILE *fp = fopen(fixture && fixture[0] ? fixture : "/proc/self/mountinfo", "r");
    char *line = NULL;
    size_t capacity = 0;
    int found = 0;
    if (!fp) {
        return 0;
    }
    while (getline(&line, &capacity, fp) >= 0) {
        char *save = NULL;
        char *field = strtok_r(line, " \n", &save);
        int field_index;
        for (field_index = 0; field && field_index < 4; ++field_index) {
            field = strtok_r(NULL, " \n", &save);
        }
        if (field) {
            char mountpoint[CS_PATH_MAX];
            if (unescape_mountpoint(mountpoint, sizeof(mountpoint), field)
                && strcmp(mountpoint, root) == 0) {
                found = 1;
                break;
            }
        }
    }
    free(line);
    fclose(fp);
    return found;
}
#endif

static int source_root_available(const char *root, size_t index) {
    struct stat st;
    char normalized[CS_PATH_MAX];
    if (source_fixture_available(index)) {
        return 1;
    }
    if (write_sdcard_root(normalized, sizeof(normalized), root, root) != 0
        || stat(normalized, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 0;
    }
    if (index == 0) {
        return 1;
    }
#if defined(__linux__)
    return mountinfo_has_exact_root(normalized);
#else
    return 1;
#endif
}

static int add_source(cs_paths *paths,
                      const char *root,
                      size_t storage_index,
                      size_t content_index) {
    cs_path_source *source;

    if (!paths || !root || root[0] == '\0' || storage_index >= CS_PATH_SOURCE_MAX) {
        return -1;
    }

    source = &paths->sources[storage_index];
    if (write_sdcard_root(source->root, sizeof(source->root), root, root) != 0
        || derive_source_alias(source->root, storage_index,
                               source->alias, sizeof(source->alias)) != 0
        || make_source_alias_unique(paths, storage_index) != 0
        || populate_source_path(source->roms_root,
                                sizeof(source->roms_root),
                                getenv("ROMS_PATHS"),
                                "ROMS_PATH",
                                content_index,
                                source->root,
                                "/Roms")
               != 0
        || populate_source_path(source->images_root,
                                sizeof(source->images_root),
                                getenv("IMAGES_PATHS"),
                                "IMAGES_PATH",
                                content_index,
                                source->root,
                                "/Images")
               != 0
        || populate_source_path(source->apps_root,
                                sizeof(source->apps_root),
                                getenv("APPS_PATHS"),
                                "APPS_PATH",
                                content_index,
                                source->root,
                                "/Apps")
               != 0
        || populate_source_path(source->bios_root,
                                sizeof(source->bios_root),
                                getenv("BIOS_PATHS"),
                                "BIOS_PATH",
                                content_index,
                                source->root,
                                "/BIOS")
               != 0
        || populate_source_path(source->saves_root,
                                sizeof(source->saves_root),
                                getenv("SAVES_PATHS"),
                                "SAVES_PATH",
                                content_index,
                                source->root,
                                "/Saves")
               != 0
        || populate_source_path(source->states_root,
                                sizeof(source->states_root),
                                getenv("STATES_PATHS"),
                                "STATES_PATH",
                                content_index,
                                source->root,
                                "/States")
               != 0
        || populate_source_path(source->cheats_root,
                                sizeof(source->cheats_root),
                                getenv("CHEATS_PATHS"),
                                "CHEATS_PATH",
                                content_index,
                                source->root,
                                "/Cheats")
               != 0) {
        return -1;
    }

    return 0;
}

static int populate_sources(cs_paths *paths, const char *sd) {
    const char *source_list = getenv("SDCARD_PATHS");
    char value[CS_PATH_MAX];
    size_t index;

    if (!paths) {
        return -1;
    }

    if (source_list && source_list[0] != '\0') {
        for (index = 0; index < CS_PATH_SOURCE_MAX; ++index) {
            if (split_nth_colon_value(source_list, index, value, sizeof(value)) != 0) {
                return -1;
            }
            if (value[0] == '\0') {
                break;
            }
            if (!source_root_available(value, index)) {
                continue;
            }
            if (add_source(paths, value, paths->source_count, index) != 0) {
                return -1;
            }
            paths->source_count += 1;
        }
    }

    if (paths->source_count == 0) {
        if (add_source(paths, sd && sd[0] != '\0' ? sd : "/mnt/sdcard",
                       0, 0) != 0) {
            return -1;
        }
        paths->source_count = 1;
    }

    return 0;
}

int cs_paths_init(cs_paths *paths) {
    const char *sd;
    const char *web;
    const char *userdata;
    const char *shared_userdata;
    const char *platform;
    const char *default_web_root = "web/out";
    char default_system_root[CS_PATH_MAX];
    char default_userdata_root[CS_PATH_MAX];
    char default_cores_root[CS_PATH_MAX];
    char default_info_root[CS_PATH_MAX];
    char default_systems_catalog_path[CS_PATH_MAX];
    char default_cores_catalog_path[CS_PATH_MAX];
    cs_paths temp = {0};

    if (!paths) {
        return -1;
    }

    sd = getenv("SDCARD_PATH");
    web = getenv("CS_WEB_ROOT");
    platform = getenv("PLATFORM");
    if (!platform || platform[0] == '\0') {
        platform = "mlp1";
    }

    if ((!web || web[0] == '\0') && access("resources/web", R_OK | X_OK) == 0) {
        default_web_root = "resources/web";
    }

    if (populate_sources(&temp, sd) != 0) {
        return -1;
    }
    if (write_value(temp.sdcard_root, sizeof(temp.sdcard_root), temp.sources[0].root, "/mnt/sdcard") != 0) {
        return -1;
    }
    {
        int written = snprintf(default_system_root,
                               sizeof(default_system_root),
                               "%s/.system/leaf/platforms/%s",
                               temp.sdcard_root,
                               platform);

        if (written < 0 || (size_t) written >= sizeof(default_system_root)) {
            return -1;
        }
    }
    if (write_value(temp.system_root,
                    sizeof(temp.system_root),
                    env_first("UMRK_PLATFORM_PATH", "SYSTEM_PATH"),
                    default_system_root)
        != 0) {
        return -1;
    }
    {
        /* Durable app data lives at the SD root's .userdata/<platform>, separate
           from the release-managed .system payload. Env (USERDATA_PATH) wins on a
           normal launch; this is the native/test fallback. */
        int written = snprintf(default_userdata_root, sizeof(default_userdata_root),
                               "%s/.userdata/%s", temp.sdcard_root, platform);
        if (written < 0 || (size_t) written >= sizeof(default_userdata_root)) {
            return -1;
        }
    }
    shared_userdata = getenv("SHARED_USERDATA_PATH");
    if (write_joined(temp.shared_userdata_root,
                     sizeof(temp.shared_userdata_root),
                     shared_userdata && shared_userdata[0] != '\0' ? shared_userdata : temp.sdcard_root,
                     shared_userdata && shared_userdata[0] != '\0' ? "" : "/.userdata/shared")
        != 0) {
        return -1;
    }
    userdata = getenv("USERDATA_PATH");
    if (write_joined(temp.shared_state_root,
                     sizeof(temp.shared_state_root),
                     userdata && userdata[0] != '\0' ? userdata : default_userdata_root,
                     "/CentralScrutinizer")
        != 0) {
        return -1;
    }
    if (write_value(temp.roms_root, sizeof(temp.roms_root), temp.sources[0].roms_root, NULL) != 0) {
        return -1;
    }
    if (write_value(temp.images_root, sizeof(temp.images_root), temp.sources[0].images_root, NULL) != 0) {
        return -1;
    }
    if (write_value(temp.apps_root, sizeof(temp.apps_root), temp.sources[0].apps_root, NULL) != 0) {
        return -1;
    }
    if (write_value(temp.saves_root, sizeof(temp.saves_root), temp.sources[0].saves_root, NULL) != 0) {
        return -1;
    }
    if (write_value(temp.states_root, sizeof(temp.states_root), temp.sources[0].states_root, NULL) != 0) {
        return -1;
    }
    if (write_value(temp.bios_root, sizeof(temp.bios_root), temp.sources[0].bios_root, NULL) != 0) {
        return -1;
    }
    if (write_value(temp.cheats_root, sizeof(temp.cheats_root), temp.sources[0].cheats_root, NULL) != 0) {
        return -1;
    }
    if (write_joined(temp.temp_upload_root, sizeof(temp.temp_upload_root), temp.shared_state_root, "/uploads/tmp") != 0) {
        return -1;
    }
    if (write_joined_component(default_cores_root, sizeof(default_cores_root), temp.system_root, "cores") != 0
        || write_joined_component(default_info_root, sizeof(default_info_root), temp.system_root, "info") != 0
        || write_joined_component(default_systems_catalog_path,
                                  sizeof(default_systems_catalog_path),
                                  temp.system_root,
                                  "defaults/systems.json")
               != 0
        || write_joined_component(default_cores_catalog_path,
                                  sizeof(default_cores_catalog_path),
                                  temp.system_root,
                                  "defaults/cores.json")
               != 0) {
        return -1;
    }
    if (write_value(temp.cores_root, sizeof(temp.cores_root), getenv("CORES_PATH"), default_cores_root) != 0) {
        return -1;
    }
    if (write_value(temp.info_root, sizeof(temp.info_root), getenv("INFO_PATH"), default_info_root) != 0) {
        return -1;
    }
    if (write_value(temp.systems_catalog_path,
                    sizeof(temp.systems_catalog_path),
                    getenv("SYSTEMS_CATALOG_PATH"),
                    default_systems_catalog_path)
        != 0) {
        return -1;
    }
    if (write_value(temp.cores_catalog_path,
                    sizeof(temp.cores_catalog_path),
                    getenv("CORES_CATALOG_PATH"),
                    default_cores_catalog_path)
        != 0) {
        return -1;
    }
    if (write_env_or_joined(temp.logs_root, sizeof(temp.logs_root), "LOGS_PATH", default_userdata_root, "/logs") != 0) {
        return -1;
    }
    if (write_value(temp.internal_data_root,
                    sizeof(temp.internal_data_root),
                    env_first("UMRK_INTERNAL_DATA_PATH", NULL),
                    temp.shared_state_root)
        != 0) {
        return -1;
    }
    if (write_value(temp.web_root, sizeof(temp.web_root), web, default_web_root) != 0) {
        return -1;
    }

    *paths = temp;
    return 0;
}

int cs_paths_source_index_for_alias(const cs_paths *paths, const char *alias) {
    size_t i;

    if (!paths || !alias || alias[0] == '\0') {
        return -1;
    }
    for (i = 0; i < paths->source_count; ++i) {
        if (strcmp(paths->sources[i].alias, alias) == 0) {
            return (int) i;
        }
    }
    return -1;
}

int cs_paths_resolve_files_path(const cs_paths *paths,
                                const char *virtual_path,
                                char *root,
                                size_t root_size,
                                char *relative,
                                size_t relative_size,
                                const cs_path_source **source_out) {
    const char *input = virtual_path ? virtual_path : "";
    const char *slash;
    char alias[sizeof(paths->sources[0].alias)];
    size_t alias_len;
    int source_index;

    if (source_out) {
        *source_out = NULL;
    }
    if (!paths || paths->source_count == 0 || !root || root_size == 0 || !relative || relative_size == 0) {
        return -1;
    }

    if (paths->source_count == 1) {
        if (write_value(root, root_size, paths->sources[0].root, NULL) != 0
            || write_value(relative, relative_size, input, "") != 0) {
            return -1;
        }
        if (source_out) {
            *source_out = &paths->sources[0];
        }
        return 0;
    }

    slash = strchr(input, '/');
    alias_len = slash ? (size_t) (slash - input) : strlen(input);
    if (alias_len == 0 || alias_len >= sizeof(alias)) {
        return -1;
    }
    memcpy(alias, input, alias_len);
    alias[alias_len] = '\0';

    source_index = cs_paths_source_index_for_alias(paths, alias);
    if (source_index < 0) {
        return -1;
    }

    if (write_value(root, root_size, paths->sources[source_index].root, NULL) != 0
        || write_value(relative, relative_size, slash ? slash + 1 : "", "") != 0) {
        return -1;
    }
    if (source_out) {
        *source_out = &paths->sources[source_index];
    }
    return 0;
}
