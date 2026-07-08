#include "cs_library.h"
#include "cs_util.h"

#include "cs_file_ops.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct cs_browser_sort_entry {
    char name[256];
    int is_dir;
    unsigned long long size;
    long long modified;
    cs_browser_sort_column sort_column;
    cs_browser_sort_direction sort_direction;
} cs_browser_sort_entry;

typedef struct cs_browser_db_entry {
    cs_browser_entry entry;
    int is_dir;
    cs_browser_sort_column sort_column;
    cs_browser_sort_direction sort_direction;
} cs_browser_db_entry;

typedef enum cs_library_db_status {
    CS_LIBRARY_DB_OK = 0,
    CS_LIBRARY_DB_UNAVAILABLE = 1,
    CS_LIBRARY_DB_INTERNAL = 2,
} cs_library_db_status;

#define CS_LIBRARY_DB_PLATFORM_SYSTEM_BINDING_COUNT 6

static const cs_browser_sort_options CS_BROWSER_DEFAULT_SORT_OPTIONS = {
    CS_BROWSER_SORT_NAME,
    CS_BROWSER_SORT_ASC,
};

static int cs_browser_name_matches_query(const char *name, const char *query) {
    size_t name_len;
    size_t query_len;
    size_t i;

    if (!query || query[0] == '\0') {
        return 1;
    }
    if (!name) {
        return 0;
    }
    name_len = strlen(name);
    query_len = strlen(query);
    if (query_len > name_len) {
        return 0;
    }
    for (i = 0; i + query_len <= name_len; ++i) {
        size_t j;

        for (j = 0; j < query_len; ++j) {
            unsigned char nc = (unsigned char) name[i + j];
            unsigned char qc = (unsigned char) query[j];

            if (tolower(nc) != tolower(qc)) {
                break;
            }
        }
        if (j == query_len) {
            return 1;
        }
    }
    return 0;
}

static int cs_is_regular_file_not_symlink(const char *path) {
    struct stat st;

    if (!path) {
        return 0;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }

    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int cs_write_relative_path(const char *root,
                                  const char *path,
                                  char *relative,
                                  size_t relative_size) {
    const char *suffix;
    size_t root_len;

    if (!root || !path || !relative || relative_size == 0) {
        return -1;
    }

    root_len = strlen(root);
    if (root_len == 0 || strncmp(path, root, root_len) != 0) {
        return -1;
    }

    suffix = path + root_len;
    if (*suffix == '/') {
        suffix += 1;
    } else if (*suffix != '\0') {
        return -1;
    }

    return CS_SAFE_SNPRINTF(relative, relative_size, "%s", suffix);
}

static int cs_open_root_directory(const char *path) {
    if (!path) {
        return -1;
    }

    return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
}

static int cs_open_directory_from_fd(int start_fd, const char *relative_path) {
    int current_fd = start_fd;

    if (current_fd < 0 || !relative_path) {
        errno = EINVAL;
        return -1;
    }
    if (relative_path[0] == '\0') {
        return current_fd;
    }

    while (*relative_path != '\0') {
        const char *slash = strchr(relative_path, '/');
        size_t length = slash ? (size_t) (slash - relative_path) : strlen(relative_path);
        char component[CS_PATH_MAX];
        int next_fd;

        if (length == 0 || length >= sizeof(component)) {
            close(current_fd);
            errno = EINVAL;
            return -1;
        }

        memcpy(component, relative_path, length);
        component[length] = '\0';
        next_fd = openat(current_fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(current_fd);
        if (next_fd < 0) {
            return -1;
        }

        current_fd = next_fd;
        relative_path = slash ? slash + 1 : relative_path + length;
    }

    return current_fd;
}

static int cs_open_guarded_directory(const char *guard_root, const char *path) {
    char relative[CS_PATH_MAX];
    int guard_fd;

    if (cs_write_relative_path(guard_root, path, relative, sizeof(relative)) != 0) {
        return -1;
    }

    guard_fd = cs_open_root_directory(guard_root);
    if (guard_fd < 0) {
        return -1;
    }

    return cs_open_directory_from_fd(guard_fd, relative);
}

static int cs_join_path(char *dst, size_t size, const char *left, const char *right) {
    int written;

    if (!dst || size == 0 || !left || !right) {
        return -1;
    }
    if (right[0] == '\0') {
        written = CS_SAFE_SNPRINTF(dst, size, "%s", left);
    } else {
        written = CS_SAFE_SNPRINTF(dst, size, "%s/%s", left, right);
    }

    return written;
}

static const char *cs_source_root_for_scope(const cs_path_source *source, cs_browser_scope scope) {
    if (!source) {
        return NULL;
    }

    switch (scope) {
        case CS_SCOPE_ROMS:
            return source->roms_root;
        case CS_SCOPE_SAVES:
            return source->saves_root;
        case CS_SCOPE_BIOS:
            return source->bios_root;
        case CS_SCOPE_CHEATS:
            return source->cheats_root;
        case CS_SCOPE_FILES:
            return source->root;
        case CS_SCOPE_OVERLAYS:
        default:
            return NULL;
    }
}

static int cs_browser_write_platform_relative_root(cs_browser_scope scope,
                                                   const cs_platform_info *platform,
                                                   int prefer_canonical,
                                                   char *relative,
                                                   size_t relative_size) {
    if (!relative || relative_size == 0 || !platform) {
        return -1;
    }

    switch (scope) {
        case CS_SCOPE_ROMS: {
            /* New content targets the canonical public folder; browse/read uses
               the discovered (possibly legacy alias) folder. Fall back to
               rom_directory when no canonical is recorded (fallback/custom). */
            const char *dir = (prefer_canonical && platform->canonical_rom_directory[0])
                                  ? platform->canonical_rom_directory
                                  : platform->rom_directory;
            return CS_SAFE_SNPRINTF(relative, relative_size, "%s", dir);
        }
        case CS_SCOPE_SAVES:
        case CS_SCOPE_BIOS:
        case CS_SCOPE_CHEATS:
        case CS_SCOPE_OVERLAYS:
            return CS_SAFE_SNPRINTF(relative, relative_size, "%s", platform->primary_code);
        default:
            return -1;
    }
}

static int cs_path_is_directory(const char *path) {
    struct stat st;

    return path && lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const cs_path_source *cs_browser_select_source_for_scope(const cs_paths *paths,
                                                               cs_browser_scope scope,
                                                               const cs_platform_info *platform) {
    char relative[CS_PATH_MAX];
    size_t i;

    if (!paths || paths->source_count == 0) {
        return NULL;
    }
    if (scope == CS_SCOPE_FILES || scope == CS_SCOPE_OVERLAYS
        || !platform
        || cs_browser_write_platform_relative_root(scope, platform, 0, relative, sizeof(relative)) != 0) {
        return &paths->sources[0];
    }

    for (i = 0; i < paths->source_count; ++i) {
        const char *base = cs_source_root_for_scope(&paths->sources[i], scope);
        char candidate[CS_PATH_MAX];

        if (!base || cs_join_path(candidate, sizeof(candidate), base, relative) != 0) {
            continue;
        }
        if (cs_path_is_directory(candidate)) {
            return &paths->sources[i];
        }
    }

    return &paths->sources[0];
}

static int cs_browser_should_include_hidden_rom_entry(const cs_platform_info *platform, const char *name) {
    if (!platform || !name) {
        return 0;
    }

    return cs_platform_allows_hidden_rom_entries(platform) && strcmp(name, ".ports") == 0;
}

static int cs_browser_should_include_entry(cs_browser_scope scope,
                                           const cs_platform_info *platform,
                                           const char *name,
                                           const char *absolute_path) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (scope == CS_SCOPE_ROMS && absolute_path && cs_platform_is_shortcut_directory(name, absolute_path)) {
        return 0;
    }
    if (name[0] != '.') {
        return 1;
    }
    if (scope == CS_SCOPE_FILES) {
        return 1;
    }

    return scope == CS_SCOPE_ROMS && cs_browser_should_include_hidden_rom_entry(platform, name);
}

static const char *cs_scope_label(cs_browser_scope scope) {
    switch (scope) {
        case CS_SCOPE_ROMS:
            return "ROMs";
        case CS_SCOPE_SAVES:
            return "Saves";
        case CS_SCOPE_BIOS:
            return "BIOS";
        case CS_SCOPE_OVERLAYS:
            return "Overlays";
        case CS_SCOPE_CHEATS:
            return "Cheats";
        case CS_SCOPE_FILES:
            return "File Browser";
        default:
            return "Browser";
    }
}

static int cs_browser_write_title(char *dst,
                                  size_t size,
                                  cs_browser_scope scope,
                                  const cs_platform_info *platform) {
    int written;

    if (!dst || size == 0) {
        return -1;
    }

    if (platform && scope != CS_SCOPE_FILES) {
        written = CS_SAFE_SNPRINTF(dst, size, "%s - %s", cs_scope_label(scope), platform->name);
    } else {
        written = CS_SAFE_SNPRINTF(dst, size, "%s", cs_scope_label(scope));
    }
    return written;
}

static int cs_browser_write_breadcrumbs(cs_browser_result *result, const char *relative_path) {
    const char *cursor = relative_path;
    size_t count = 0;

    if (!result || !relative_path || relative_path[0] == '\0') {
        if (result) {
            result->breadcrumb_count = 0;
        }
        return 0;
    }

    while (*cursor != '\0' && count < CS_BROWSER_MAX_BREADCRUMBS) {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t) (slash - cursor) : strlen(cursor);
        size_t prefix_len;

        if (slash) {
            prefix_len = (size_t) (slash - relative_path);
        } else {
            prefix_len = strlen(relative_path);
        }

        if (length == 0 || prefix_len >= sizeof(result->breadcrumbs[count].path)) {
            return -1;
        }

        memcpy(result->breadcrumbs[count].label, cursor, length);
        result->breadcrumbs[count].label[length] = '\0';
        memcpy(result->breadcrumbs[count].path, relative_path, prefix_len);
        result->breadcrumbs[count].path[prefix_len] = '\0';
        count += 1;

        cursor = slash ? slash + 1 : cursor + length;
    }

    result->breadcrumb_count = count;
    return 0;
}

static int cs_browser_write_thumbnail(const char *root,
                                      const char *images_root,
                                      const cs_platform_info *platform,
                                      const char *entry_relative_path,
                                      char *thumbnail_path,
                                      size_t thumbnail_path_size) {
    char candidate_relative[CS_PATH_MAX];
    char candidate_absolute[CS_PATH_MAX];
    char basename[CS_PATH_MAX];
    const char *ext;
    size_t name_len;

    if (!root || !entry_relative_path || !thumbnail_path || thumbnail_path_size == 0) {
        return -1;
    }
    ext = strrchr(entry_relative_path, '.');
    name_len = ext && ext != entry_relative_path ? (size_t) (ext - entry_relative_path) : strlen(entry_relative_path);
    if (name_len == 0 || name_len >= sizeof(basename)) {
        thumbnail_path[0] = '\0';
        return 0;
    }

    memcpy(basename, entry_relative_path, name_len);
    basename[name_len] = '\0';

    if (images_root && images_root[0] != '\0' && platform) {
        const char *image_dir = platform->canonical_image_directory[0] ? platform->canonical_image_directory
                                                                       : platform->primary_code;

        if (CS_SAFE_SNPRINTF(candidate_relative,
                             sizeof(candidate_relative),
                             "%s/%s.png",
                             image_dir,
                             basename)
                != 0
            || cs_join_path(candidate_absolute, sizeof(candidate_absolute), images_root, candidate_relative) != 0) {
            return -1;
        }
        if (cs_is_regular_file_not_symlink(candidate_absolute)) {
            if (CS_SAFE_SNPRINTF(thumbnail_path, thumbnail_path_size, "Images/%s", candidate_relative) != 0) {
                return -1;
            }
            return 0;
        }
    }

    thumbnail_path[0] = '\0';
    return 0;
}

/* qsort has no context pointer, so the active sort column/direction are duplicated onto every
 * entry by the caller. Every entry in a given sort carries identical values, so reading them
 * off `a` is safe. */
static int cs_browser_sort_compare(const void *left, const void *right) {
    const cs_browser_sort_entry *a = (const cs_browser_sort_entry *) left;
    const cs_browser_sort_entry *b = (const cs_browser_sort_entry *) right;
    cs_browser_sort_direction direction = a->sort_direction;
    int cmp = 0;

    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }

    switch (a->sort_column) {
        case CS_BROWSER_SORT_SIZE:
            cmp = (a->size > b->size) - (a->size < b->size);
            break;
        case CS_BROWSER_SORT_MODIFIED:
            cmp = (a->modified > b->modified) - (a->modified < b->modified);
            break;
        case CS_BROWSER_SORT_NAME:
        default:
            cmp = strcmp(a->name, b->name);
            break;
    }

    if (direction == CS_BROWSER_SORT_DESC) {
        cmp = -cmp;
    }
    if (cmp != 0) {
        return cmp;
    }

    return strcmp(a->name, b->name);
}

static int cs_browser_db_sort_compare(const void *left, const void *right) {
    const cs_browser_db_entry *a = (const cs_browser_db_entry *) left;
    const cs_browser_db_entry *b = (const cs_browser_db_entry *) right;
    cs_browser_sort_direction direction = a->sort_direction;
    int cmp = 0;

    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }

    switch (a->sort_column) {
        case CS_BROWSER_SORT_SIZE:
            cmp = (a->entry.size > b->entry.size) - (a->entry.size < b->entry.size);
            break;
        case CS_BROWSER_SORT_MODIFIED:
            cmp = (a->entry.modified > b->entry.modified) - (a->entry.modified < b->entry.modified);
            break;
        case CS_BROWSER_SORT_NAME:
        default:
            cmp = strcmp(a->entry.name, b->entry.name);
            break;
    }

    if (direction == CS_BROWSER_SORT_DESC) {
        cmp = -cmp;
    }
    if (cmp != 0) {
        return cmp;
    }

    return strcmp(a->entry.path, b->entry.path);
}

static cs_browser_sort_options cs_browser_normalize_sort_options(const cs_browser_sort_options *sort_options) {
    cs_browser_sort_options normalized = sort_options ? *sort_options : CS_BROWSER_DEFAULT_SORT_OPTIONS;

    if (normalized.column != CS_BROWSER_SORT_NAME && normalized.column != CS_BROWSER_SORT_SIZE
        && normalized.column != CS_BROWSER_SORT_MODIFIED) {
        normalized.column = CS_BROWSER_SORT_NAME;
    }
    if (normalized.direction != CS_BROWSER_SORT_ASC && normalized.direction != CS_BROWSER_SORT_DESC) {
        normalized.direction = CS_BROWSER_SORT_ASC;
    }
    return normalized;
}

static const char *cs_path_basename(const char *path) {
    const char *slash;

    if (!path) {
        return "";
    }

    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int cs_path_strip_prefix_component(const char *path,
                                          const char *prefix,
                                          char *relative,
                                          size_t relative_size) {
    size_t prefix_len;
    const char *suffix;

    if (!path || !prefix || !relative || relative_size == 0) {
        return -1;
    }

    prefix_len = strlen(prefix);
    if (prefix_len == 0 || strncmp(path, prefix, prefix_len) != 0) {
        return -1;
    }

    suffix = path + prefix_len;
    if (*suffix == '\0') {
        relative[0] = '\0';
        return 0;
    }
    if (*suffix != '/') {
        return -1;
    }
    suffix += 1;

    return CS_SAFE_SNPRINTF(relative, relative_size, "%s", suffix);
}

static int cs_library_db_make_path(const cs_paths *paths, char *db_path, size_t db_path_size) {
    if (!paths || !db_path || db_path_size == 0 || paths->internal_data_root[0] == '\0') {
        return -1;
    }

    return CS_SAFE_SNPRINTF(db_path, db_path_size, "%s/library.db", paths->internal_data_root);
}

static cs_library_db_status cs_library_db_open_readonly(const cs_paths *paths, sqlite3 **db_out) {
    char db_path[CS_PATH_MAX];
    sqlite3 *db = NULL;

    if (!db_out) {
        return CS_LIBRARY_DB_INTERNAL;
    }
    *db_out = NULL;
    if (cs_library_db_make_path(paths, db_path, sizeof(db_path)) != 0) {
        return CS_LIBRARY_DB_INTERNAL;
    }
    if (access(db_path, R_OK) != 0) {
        return CS_LIBRARY_DB_UNAVAILABLE;
    }

    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return CS_LIBRARY_DB_UNAVAILABLE;
    }
    sqlite3_busy_timeout(db, 1000);
    *db_out = db;
    return CS_LIBRARY_DB_OK;
}

static cs_library_db_status cs_library_db_open_writable(const cs_paths *paths, sqlite3 **db_out) {
    char db_path[CS_PATH_MAX];
    sqlite3 *db = NULL;

    if (!db_out) {
        return CS_LIBRARY_DB_INTERNAL;
    }
    *db_out = NULL;
    if (cs_library_db_make_path(paths, db_path, sizeof(db_path)) != 0) {
        return CS_LIBRARY_DB_INTERNAL;
    }
    if (access(db_path, R_OK | W_OK) != 0) {
        return CS_LIBRARY_DB_UNAVAILABLE;
    }

    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return CS_LIBRARY_DB_UNAVAILABLE;
    }
    sqlite3_busy_timeout(db, 1000);
    *db_out = db;
    return CS_LIBRARY_DB_OK;
}

static const char *cs_library_db_nonempty_or_blank(const char *value) {
    return value && value[0] != '\0' ? value : "";
}

static void cs_library_db_collect_platform_systems(const cs_platform_info *platform,
                                                   const char *systems[CS_LIBRARY_DB_PLATFORM_SYSTEM_BINDING_COUNT]) {
    const cs_platform_info *fallback = platform ? cs_platform_find(platform->tag) : NULL;

    systems[0] = cs_library_db_nonempty_or_blank(platform ? platform->primary_code : NULL);
    systems[1] = cs_library_db_nonempty_or_blank(platform ? platform->tag : NULL);
    systems[2] = cs_library_db_nonempty_or_blank(platform ? platform->rom_directory : NULL);
    systems[3] = cs_library_db_nonempty_or_blank(platform ? platform->canonical_rom_directory : NULL);
    systems[4] = cs_library_db_nonempty_or_blank(fallback ? fallback->primary_code : NULL);
    systems[5] = cs_library_db_nonempty_or_blank(fallback ? fallback->rom_directory : NULL);
}

static void cs_library_db_bind_platform_systems(sqlite3_stmt *stmt, const cs_platform_info *platform) {
    const char *systems[CS_LIBRARY_DB_PLATFORM_SYSTEM_BINDING_COUNT];
    size_t i;

    cs_library_db_collect_platform_systems(platform, systems);
    for (i = 0; i < CS_LIBRARY_DB_PLATFORM_SYSTEM_BINDING_COUNT; ++i) {
        sqlite3_bind_text(stmt, (int) i + 1, systems[i], -1, SQLITE_TRANSIENT);
    }
}

static int cs_library_db_platform_system_matches(const cs_platform_info *platform, const char *value) {
    const char *systems[CS_LIBRARY_DB_PLATFORM_SYSTEM_BINDING_COUNT];
    size_t i;

    if (!value || value[0] == '\0') {
        return 0;
    }
    cs_library_db_collect_platform_systems(platform, systems);
    for (i = 0; i < CS_LIBRARY_DB_PLATFORM_SYSTEM_BINDING_COUNT; ++i) {
        if (systems[i][0] != '\0' && strcmp(systems[i], value) == 0) {
            return 1;
        }
    }

    return 0;
}

static int cs_library_db_path_component_matches_platform(const char *component,
                                                         size_t component_len,
                                                         const cs_platform_info *platform) {
    char component_buf[256];
    char parsed_name[128];
    char parsed_code[CS_PLATFORM_CODE_MAX];

    if (!component || component_len == 0 || component_len >= sizeof(component_buf) || !platform) {
        return 0;
    }

    memcpy(component_buf, component, component_len);
    component_buf[component_len] = '\0';

    if (cs_library_db_platform_system_matches(platform, component_buf)) {
        return 1;
    }

    if (cs_platform_parse_rom_directory(component_buf,
                                        parsed_name,
                                        sizeof(parsed_name),
                                        parsed_code,
                                        sizeof(parsed_code))
            == 0
        && cs_library_db_platform_system_matches(platform, parsed_code)) {
        return 1;
    }

    return 0;
}

static int cs_library_db_strip_platform_component(const char *roms_relative,
                                                  const cs_platform_info *platform,
                                                  char *platform_relative,
                                                  size_t platform_relative_size) {
    const char *slash;
    size_t component_len;

    if (!roms_relative || !platform || !platform_relative || platform_relative_size == 0) {
        return -1;
    }

    slash = strchr(roms_relative, '/');
    if (!slash || slash == roms_relative || slash[1] == '\0') {
        return -1;
    }

    component_len = (size_t) (slash - roms_relative);
    if (!cs_library_db_path_component_matches_platform(roms_relative, component_len, platform)) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(platform_relative, platform_relative_size, "%s", slash + 1);
}

static int cs_library_db_resolve_rom_path(const cs_paths *paths,
                                          const cs_platform_info *platform,
                                          const char *rom_path,
                                          const cs_path_source **source_out,
                                          char *platform_relative,
                                          size_t platform_relative_size,
                                          char *absolute_path,
                                          size_t absolute_path_size) {
    char roms_relative[CS_PATH_MAX];
    size_t i;

    if (source_out) {
        *source_out = NULL;
    }
    if (!paths || !platform || !rom_path || rom_path[0] == '\0' || !platform_relative || platform_relative_size == 0
        || !absolute_path || absolute_path_size == 0) {
        return -1;
    }

    if (rom_path[0] == '/') {
        for (i = 0; i < paths->source_count; ++i) {
            const cs_path_source *source = &paths->sources[i];

            if (cs_path_strip_prefix_component(rom_path, source->roms_root, roms_relative, sizeof(roms_relative)) != 0) {
                char source_relative[CS_PATH_MAX];

                if (cs_path_strip_prefix_component(rom_path, source->root, source_relative, sizeof(source_relative)) != 0
                    || cs_path_strip_prefix_component(source_relative, "Roms", roms_relative, sizeof(roms_relative)) != 0) {
                    continue;
                }
            }

            if (cs_library_db_strip_platform_component(roms_relative,
                                                       platform,
                                                       platform_relative,
                                                       platform_relative_size)
                != 0) {
                continue;
            }
            if (CS_SAFE_SNPRINTF(absolute_path, absolute_path_size, "%s", rom_path) != 0) {
                return -1;
            }
            if (source_out) {
                *source_out = source;
            }
            return 0;
        }
        return -1;
    }

    if (cs_path_strip_prefix_component(rom_path, "Roms", roms_relative, sizeof(roms_relative)) != 0) {
        return -1;
    }
    if (cs_library_db_strip_platform_component(roms_relative,
                                               platform,
                                               platform_relative,
                                               platform_relative_size)
        != 0) {
        return -1;
    }
    if (paths->source_count == 0
        || cs_join_path(absolute_path, absolute_path_size, paths->sources[0].roms_root, roms_relative) != 0) {
        return -1;
    }
    if (source_out) {
        *source_out = &paths->sources[0];
    }
    return 0;
}

static int cs_library_db_virtual_file_path_for_source(const cs_paths *paths,
                                                      const cs_path_source *source,
                                                      const char *source_relative,
                                                      char *virtual_path,
                                                      size_t virtual_path_size) {
    if (!paths || !source || !source_relative || source_relative[0] == '\0' || !virtual_path || virtual_path_size == 0) {
        return -1;
    }

    if (paths->source_count > 1) {
        return CS_SAFE_SNPRINTF(virtual_path, virtual_path_size, "%s/%s", source->alias, source_relative);
    }
    return CS_SAFE_SNPRINTF(virtual_path, virtual_path_size, "%s", source_relative);
}

static int cs_library_db_resolve_image_path(const cs_paths *paths,
                                            const cs_path_source *rom_source,
                                            const char *image_path,
                                            char *virtual_path,
                                            size_t virtual_path_size) {
    char source_relative[CS_PATH_MAX];
    size_t i;

    if (!paths || !rom_source || !image_path || image_path[0] == '\0' || !virtual_path || virtual_path_size == 0) {
        return -1;
    }

    if (image_path[0] == '/') {
        for (i = 0; i < paths->source_count; ++i) {
            const cs_path_source *source = &paths->sources[i];

            if (cs_path_strip_prefix_component(image_path, source->root, source_relative, sizeof(source_relative)) != 0) {
                continue;
            }
            if (!cs_is_regular_file_not_symlink(image_path)) {
                return -1;
            }
            return cs_library_db_virtual_file_path_for_source(paths,
                                                              source,
                                                              source_relative,
                                                              virtual_path,
                                                              virtual_path_size);
        }
        return -1;
    }

    if (cs_path_strip_prefix_component(image_path, "Images", source_relative, sizeof(source_relative)) != 0) {
        return -1;
    }

    {
        char absolute_path[CS_PATH_MAX];

        if (cs_join_path(absolute_path, sizeof(absolute_path), rom_source->images_root, source_relative) != 0
            || !cs_is_regular_file_not_symlink(absolute_path)) {
            return -1;
        }
    }

    {
        char relative_with_root[CS_PATH_MAX];

        if (CS_SAFE_SNPRINTF(relative_with_root, sizeof(relative_with_root), "Images/%s", source_relative) != 0) {
            return -1;
        }
        return cs_library_db_virtual_file_path_for_source(paths,
                                                          rom_source,
                                                          relative_with_root,
                                                          virtual_path,
                                                          virtual_path_size);
    }
}

static int cs_browser_rom_entry_path_for_source(const cs_paths *paths,
                                                const cs_path_source *source,
                                                const char *platform_relative,
                                                char *entry_path,
                                                size_t entry_path_size) {
    if (!paths || !source || !platform_relative || platform_relative[0] == '\0' || !entry_path || entry_path_size == 0) {
        return -1;
    }

    /* ROM browser rows use paths as opaque operation handles: primary-source
       rows stay plain, while secondary-source rows carry an "<alias>/" prefix. */
    if (paths->source_count > 1 && source != &paths->sources[0]) {
        return CS_SAFE_SNPRINTF(entry_path, entry_path_size, "%s/%s", source->alias, platform_relative);
    }
    return CS_SAFE_SNPRINTF(entry_path, entry_path_size, "%s", platform_relative);
}

static int cs_browser_rom_platform_root_for_source(const cs_path_source *source,
                                                   const cs_platform_info *platform,
                                                   char *root,
                                                   size_t root_size) {
    char relative[CS_PATH_MAX];

    if (!source || !platform || !root || root_size == 0) {
        return -1;
    }
    if (cs_browser_write_platform_relative_root(CS_SCOPE_ROMS, platform, 0, relative, sizeof(relative)) != 0) {
        return -1;
    }
    return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", source->roms_root, relative);
}

static int cs_browser_split_rom_source_prefix(const cs_paths *paths,
                                              const char *entry_path,
                                              const cs_path_source **source_out,
                                              char *relative,
                                              size_t relative_size) {
    const char *slash;
    char alias[sizeof(paths->sources[0].alias)];
    size_t alias_len;
    int source_index;

    if (source_out) {
        *source_out = NULL;
    }
    if (!paths || paths->source_count <= 1 || !entry_path || !relative || relative_size == 0) {
        return -1;
    }

    slash = strchr(entry_path, '/');
    if (!slash || slash == entry_path || slash[1] == '\0') {
        return -1;
    }
    alias_len = (size_t) (slash - entry_path);
    if (alias_len >= sizeof(alias)) {
        return -1;
    }
    memcpy(alias, entry_path, alias_len);
    alias[alias_len] = '\0';

    source_index = cs_paths_source_index_for_alias(paths, alias);
    if (source_index < 0) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(relative, relative_size, "%s", slash + 1) != 0) {
        return -1;
    }
    if (source_out) {
        *source_out = &paths->sources[source_index];
    }
    return 0;
}

int cs_browser_resolve_rom_entry_path(const cs_paths *paths,
                                      const cs_platform_info *platform,
                                      const char *entry_path,
                                      char *root,
                                      size_t root_size,
                                      char *relative,
                                      size_t relative_size,
                                      const cs_path_source **source_out) {
    const cs_path_source *source = NULL;
    char stripped_relative[CS_PATH_MAX];
    size_t i;

    if (source_out) {
        *source_out = NULL;
    }
    if (!paths || !platform || !entry_path || entry_path[0] == '\0' || !root || root_size == 0
        || !relative || relative_size == 0) {
        return -1;
    }

    if (cs_browser_split_rom_source_prefix(paths,
                                           entry_path,
                                           &source,
                                           stripped_relative,
                                           sizeof(stripped_relative))
        == 0) {
        if (cs_browser_rom_platform_root_for_source(source, platform, root, root_size) != 0
            || CS_SAFE_SNPRINTF(relative, relative_size, "%s", stripped_relative) != 0) {
            return -1;
        }
        if (source_out) {
            *source_out = source;
        }
        return 0;
    }

    for (i = 0; i < paths->source_count; ++i) {
        char candidate_root[CS_PATH_MAX];
        char candidate_path[CS_PATH_MAX];
        struct stat st;

        source = &paths->sources[i];
        if (cs_browser_rom_platform_root_for_source(source, platform, candidate_root, sizeof(candidate_root)) != 0
            || cs_join_path(candidate_path, sizeof(candidate_path), candidate_root, entry_path) != 0) {
            continue;
        }
        if (lstat(candidate_path, &st) == 0 && !S_ISLNK(st.st_mode)) {
            if (CS_SAFE_SNPRINTF(root, root_size, "%s", candidate_root) != 0
                || CS_SAFE_SNPRINTF(relative, relative_size, "%s", entry_path) != 0) {
                return -1;
            }
            if (source_out) {
                *source_out = source;
            }
            return 0;
        }
    }

    source = cs_browser_select_source_for_scope(paths, CS_SCOPE_ROMS, platform);
    if (!source
        || cs_browser_rom_platform_root_for_source(source, platform, root, root_size) != 0
        || CS_SAFE_SNPRINTF(relative, relative_size, "%s", entry_path) != 0) {
        return -1;
    }
    if (source_out) {
        *source_out = source;
    }
    return 0;
}

static cs_browser_list_status cs_browser_path_failure_status(int error_code) {
    return (error_code == ENOENT || error_code == ENOTDIR) ? CS_BROWSER_LIST_NOT_FOUND : CS_BROWSER_LIST_INTERNAL;
}

static const char *cs_browser_entry_type_for_scope(cs_browser_scope scope, int is_dir, const char *entry_relative) {
    if (is_dir) {
        return "directory";
    }
    switch (scope) {
        case CS_SCOPE_ROMS:
            (void) entry_relative;
            return "rom";
        case CS_SCOPE_SAVES:
            return "save";
        case CS_SCOPE_BIOS:
            return "bios";
        case CS_SCOPE_OVERLAYS:
            return "overlay";
        case CS_SCOPE_CHEATS:
            return "cheat";
        default:
            return "file";
    }
}

static int cs_browser_guard_root_for_scope(const cs_paths *paths,
                                           cs_browser_scope scope,
                                           const cs_path_source *selected_source,
                                           char *guard_root,
                                           size_t guard_root_size) {
    const char *source = NULL;

    if (!paths || !guard_root || guard_root_size == 0) {
        return -1;
    }

    switch (scope) {
        case CS_SCOPE_ROMS:
            source = selected_source ? selected_source->roms_root : paths->roms_root;
            break;
        case CS_SCOPE_SAVES:
            source = selected_source ? selected_source->saves_root : paths->saves_root;
            break;
        case CS_SCOPE_BIOS:
            source = selected_source ? selected_source->bios_root : paths->bios_root;
            break;
        case CS_SCOPE_OVERLAYS:
            source = paths->overlays_root;
            break;
        case CS_SCOPE_CHEATS:
            source = selected_source ? selected_source->cheats_root : paths->cheats_root;
            break;
        case CS_SCOPE_FILES:
            source = selected_source ? selected_source->root : paths->sdcard_root;
            break;
        default:
            return -1;
    }

    return CS_SAFE_SNPRINTF(guard_root, guard_root_size, "%s", source);
}

const char *cs_browser_scope_name(cs_browser_scope scope) {
    switch (scope) {
        case CS_SCOPE_ROMS:
            return "roms";
        case CS_SCOPE_SAVES:
            return "saves";
        case CS_SCOPE_BIOS:
            return "bios";
        case CS_SCOPE_OVERLAYS:
            return "overlays";
        case CS_SCOPE_CHEATS:
            return "cheats";
        case CS_SCOPE_FILES:
            return "files";
        default:
            return NULL;
    }
}

cs_browser_scope cs_browser_scope_parse(const char *value) {
    if (!value || value[0] == '\0') {
        return CS_SCOPE_INVALID;
    }
    if (strcmp(value, "roms") == 0) {
        return CS_SCOPE_ROMS;
    }
    if (strcmp(value, "saves") == 0) {
        return CS_SCOPE_SAVES;
    }
    if (strcmp(value, "bios") == 0) {
        return CS_SCOPE_BIOS;
    }
    if (strcmp(value, "overlays") == 0) {
        return CS_SCOPE_OVERLAYS;
    }
    if (strcmp(value, "cheats") == 0) {
        return CS_SCOPE_CHEATS;
    }
    if (strcmp(value, "files") == 0) {
        return CS_SCOPE_FILES;
    }

    return CS_SCOPE_INVALID;
}

int cs_browser_scope_requires_platform(cs_browser_scope scope) {
    return scope == CS_SCOPE_ROMS || scope == CS_SCOPE_SAVES || scope == CS_SCOPE_BIOS
           || scope == CS_SCOPE_OVERLAYS || scope == CS_SCOPE_CHEATS;
}

int cs_browser_scope_allows_hidden(cs_browser_scope scope) {
    return scope == CS_SCOPE_FILES;
}

int cs_browser_scope_supported_for_platform(const cs_platform_info *platform, cs_browser_scope scope) {
    const char *scope_name;

    if (scope == CS_SCOPE_FILES) {
        return 1;
    }

    scope_name = cs_browser_scope_name(scope);
    return scope_name ? cs_platform_supports_resource(platform, scope_name) : 0;
}

int cs_browser_scope_allows_hidden_for_platform(cs_browser_scope scope, const cs_platform_info *platform) {
    return cs_browser_scope_allows_hidden(scope)
           || (scope == CS_SCOPE_ROMS && cs_platform_allows_hidden_rom_entries(platform));
}

int cs_browser_root_for_scope_ex(const cs_paths *paths,
                                 cs_browser_scope scope,
                                 const cs_platform_info *platform,
                                 int prefer_canonical,
                                 char *root,
                                 size_t root_size) {
    const cs_path_source *selected_source;
    char relative[CS_PATH_MAX];
    const char *base;

    if (!paths || !root || root_size == 0) {
        return -1;
    }
    if (scope != CS_SCOPE_FILES && !cs_browser_scope_supported_for_platform(platform, scope)) {
        return -1;
    }

    selected_source = prefer_canonical && paths->source_count > 0
                      ? &paths->sources[0]
                      : cs_browser_select_source_for_scope(paths, scope, platform);
    switch (scope) {
        case CS_SCOPE_ROMS:
        case CS_SCOPE_SAVES:
        case CS_SCOPE_BIOS:
        case CS_SCOPE_CHEATS:
            if (!platform || !selected_source) {
                return -1;
            }
            base = cs_source_root_for_scope(selected_source, scope);
            if (!base || cs_browser_write_platform_relative_root(scope, platform, prefer_canonical, relative, sizeof(relative)) != 0) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", base, relative);
        case CS_SCOPE_OVERLAYS:
            if (!platform) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", paths->overlays_root, platform->primary_code);
        case CS_SCOPE_FILES:
            return CS_SAFE_SNPRINTF(root, root_size, "%s", selected_source ? selected_source->root : paths->sdcard_root);
        default:
            return -1;
    }
}

int cs_browser_root_for_scope(const cs_paths *paths,
                              cs_browser_scope scope,
                              const cs_platform_info *platform,
                              char *root,
                              size_t root_size) {
    return cs_browser_root_for_scope_ex(paths, scope, platform, 0, root, root_size);
}

int cs_browser_write_root_for_scope(const cs_paths *paths,
                                    cs_browser_scope scope,
                                    const cs_platform_info *platform,
                                    char *root,
                                    size_t root_size) {
    return cs_browser_root_for_scope_ex(paths, scope, platform, 1, root, root_size);
}

static cs_browser_list_status cs_browser_list_source_virtual_root(const cs_paths *paths,
                                                                  cs_browser_scope scope,
                                                                  const cs_platform_info *platform,
                                                                  size_t offset,
                                                                  const char *query,
                                                                  cs_browser_result *result) {
    size_t i;
    size_t total = 0;
    size_t out_count = 0;

    if (!paths || !result) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    memset(result, 0, sizeof(*result));
    result->offset = offset;
    if (CS_SAFE_SNPRINTF(result->scope, sizeof(result->scope), "%s", cs_browser_scope_name(scope)) != 0
        || CS_SAFE_SNPRINTF(result->root_path, sizeof(result->root_path), "%s", "sources") != 0
        || CS_SAFE_SNPRINTF(result->path, sizeof(result->path), "%s", "") != 0
        || cs_browser_write_title(result->title, sizeof(result->title), scope, platform) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    for (i = 0; i < paths->source_count; ++i) {
        cs_browser_entry *entry;
        struct stat st;

        if (!cs_browser_name_matches_query(paths->sources[i].alias, query)) {
            continue;
        }
        if (total++ < offset) {
            continue;
        }
        if (out_count >= CS_BROWSER_PAGE_SIZE) {
            result->truncated = 1;
            continue;
        }

        entry = &result->entries[out_count++];
        if (CS_SAFE_SNPRINTF(entry->name, sizeof(entry->name), "%s", paths->sources[i].alias) != 0
            || CS_SAFE_SNPRINTF(entry->path, sizeof(entry->path), "%s", paths->sources[i].alias) != 0
            || CS_SAFE_SNPRINTF(entry->type, sizeof(entry->type), "%s", "directory") != 0) {
            return CS_BROWSER_LIST_INTERNAL;
        }
        if (lstat(paths->sources[i].root, &st) == 0) {
            entry->modified = (long long) st.st_mtime;
        }
    }

    result->count = out_count;
    result->total_count = total;
    return CS_BROWSER_LIST_OK;
}

int cs_library_db_count_roms_for_platform(const cs_paths *paths, const cs_platform_info *platform, int *count_out) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    cs_library_db_status open_status;
    static const char *sql =
        "SELECT COUNT(*) FROM games WHERE system IN (?, ?, ?, ?, ?, ?);";

    if (!paths || !platform || !count_out) {
        return -1;
    }
    *count_out = 0;

    open_status = cs_library_db_open_readonly(paths, &db);
    if (open_status != CS_LIBRARY_DB_OK) {
        return -1;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    cs_library_db_bind_platform_systems(stmt, platform);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *count_out = sqlite3_column_int(stmt, 0);
    } else {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

int cs_library_db_set_game_favorite(const cs_paths *paths,
                                    const cs_platform_info *platform,
                                    const char *rom_relative_path,
                                    int favorite) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *write_stmt = NULL;
    cs_library_db_status open_status;
    const cs_path_source *target_source = NULL;
    char target_root[CS_PATH_MAX];
    char target_relative[CS_PATH_MAX];
    int found_id = 0;
    int step_rc;
    static const char *select_sql =
        "SELECT id, rom_path FROM games WHERE system IN (?, ?, ?, ?, ?, ?);";
    static const char *add_sql =
        "INSERT OR IGNORE INTO favorites (kind, target_id, added_at) "
        "VALUES ('game', ?, strftime('%s','now'));";
    static const char *remove_sql =
        "DELETE FROM favorites WHERE kind = 'game' AND target_id = ?;";

    if (!paths || !platform || !rom_relative_path || rom_relative_path[0] == '\0') {
        return -1;
    }
    if (cs_browser_resolve_rom_entry_path(paths,
                                          platform,
                                          rom_relative_path,
                                          target_root,
                                          sizeof(target_root),
                                          target_relative,
                                          sizeof(target_relative),
                                          &target_source)
            != 0
        || !target_source) {
        return -1;
    }

    open_status = cs_library_db_open_writable(paths, &db);
    if (open_status != CS_LIBRARY_DB_OK) {
        return -1;
    }
    if (sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    cs_library_db_bind_platform_systems(stmt, platform);

    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int row_id = sqlite3_column_int(stmt, 0);
        const char *rom_path = (const char *) sqlite3_column_text(stmt, 1);
        const cs_path_source *row_source = NULL;
        char platform_relative[CS_PATH_MAX];
        char absolute_path[CS_PATH_MAX];

        if (cs_library_db_resolve_rom_path(paths,
                                           platform,
                                           rom_path,
                                           &row_source,
                                           platform_relative,
                                           sizeof(platform_relative),
                                           absolute_path,
                                           sizeof(absolute_path))
                == 0
            && row_source
            && strcmp(row_source->root, target_source->root) == 0
            && strcmp(platform_relative, target_relative) == 0) {
            found_id = row_id;
            break;
        }
    }
    if (step_rc != SQLITE_ROW && step_rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    if (found_id <= 0) {
        sqlite3_close(db);
        return -1;
    }
    if (sqlite3_prepare_v2(db, favorite ? add_sql : remove_sql, -1, &write_stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    sqlite3_bind_int(write_stmt, 1, found_id);
    if (sqlite3_step(write_stmt) != SQLITE_DONE) {
        sqlite3_finalize(write_stmt);
        sqlite3_close(db);
        return -1;
    }

    sqlite3_finalize(write_stmt);
    sqlite3_close(db);
    return 0;
}

static cs_library_db_status cs_browser_list_db_roms(const cs_paths *paths,
                                                    const cs_platform_info *platform,
                                                    const char *relative_path,
                                                    size_t offset,
                                                    const char *query,
                                                    const cs_browser_sort_options *sort_options,
                                                    const char *root,
                                                    cs_browser_result *result) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    cs_browser_db_entry *entries = NULL;
    cs_browser_sort_options normalized_sort = cs_browser_normalize_sort_options(sort_options);
    cs_library_db_status open_status;
    size_t total = 0;
    size_t out_count = 0;
    size_t i;
    int saw_valid_row = 0;
    int scan_truncated = 0;
    int step_rc;
    static const char *sql =
        "SELECT name, rom_path, COALESCE(image_path, ''), "
        "EXISTS(SELECT 1 FROM favorites f WHERE f.kind = 'game' AND f.target_id = games.id) "
        "FROM games WHERE system IN (?, ?, ?, ?, ?, ?) ORDER BY name;";

    if (!paths || !platform || !root || !result) {
        return CS_LIBRARY_DB_INTERNAL;
    }
    if (relative_path && relative_path[0] != '\0') {
        return CS_LIBRARY_DB_UNAVAILABLE;
    }

    open_status = cs_library_db_open_readonly(paths, &db);
    if (open_status != CS_LIBRARY_DB_OK) {
        return open_status;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return CS_LIBRARY_DB_UNAVAILABLE;
    }
    cs_library_db_bind_platform_systems(stmt, platform);

    entries = (cs_browser_db_entry *) calloc(CS_BROWSER_SCAN_CAP, sizeof(*entries));
    if (!entries) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return CS_LIBRARY_DB_INTERNAL;
    }

    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *title = (const char *) sqlite3_column_text(stmt, 0);
        const char *rom_path = (const char *) sqlite3_column_text(stmt, 1);
        const char *image_path = (const char *) sqlite3_column_text(stmt, 2);
        int favorite = sqlite3_column_int(stmt, 3);
        const cs_path_source *row_source = NULL;
        char platform_relative[CS_PATH_MAX];
        char absolute_path[CS_PATH_MAX];
        char entry_path[CS_PATH_MAX];
        struct stat st;
        const char *name;
        cs_browser_db_entry *entry;

        if (cs_library_db_resolve_rom_path(paths,
                                           platform,
                                           rom_path,
                                           &row_source,
                                           platform_relative,
                                           sizeof(platform_relative),
                                           absolute_path,
                                           sizeof(absolute_path))
            != 0) {
            continue;
        }
        if (!row_source) {
            continue;
        }
        if (lstat(absolute_path, &st) != 0 || !S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            continue;
        }

        saw_valid_row = 1;
        name = cs_path_basename(platform_relative);
        if (!cs_browser_name_matches_query(name, query)
            && !cs_browser_name_matches_query(platform_relative, query)
            && !cs_browser_name_matches_query(title, query)) {
            continue;
        }

        if (total >= CS_BROWSER_SCAN_CAP) {
            scan_truncated = 1;
            continue;
        }

        entry = &entries[total++];
        if (cs_browser_rom_entry_path_for_source(paths, row_source, platform_relative, entry_path, sizeof(entry_path))
                != 0
            || CS_SAFE_SNPRINTF(entry->entry.name, sizeof(entry->entry.name), "%s", name) != 0
            || CS_SAFE_SNPRINTF(entry->entry.path, sizeof(entry->entry.path), "%s", entry_path) != 0
            || CS_SAFE_SNPRINTF(entry->entry.type, sizeof(entry->entry.type), "%s", "rom") != 0) {
            free(entries);
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return CS_LIBRARY_DB_INTERNAL;
        }
        entry->is_dir = 0;
        entry->entry.size = (unsigned long long) st.st_size;
        entry->entry.modified = (long long) st.st_mtime;
        entry->entry.favorite = favorite ? 1 : 0;
        entry->entry.favorite_supported = 1;
        entry->sort_column = normalized_sort.column;
        entry->sort_direction = normalized_sort.direction;

        if (cs_library_db_resolve_image_path(paths,
                                             row_source,
                                             image_path,
                                             entry->entry.thumbnail_path,
                                             sizeof(entry->entry.thumbnail_path))
            != 0) {
            (void) cs_browser_write_thumbnail(root,
                                              row_source->images_root,
                                              platform,
                                              platform_relative,
                                              entry->entry.thumbnail_path,
                                              sizeof(entry->entry.thumbnail_path));
        }
    }

    if (step_rc != SQLITE_DONE) {
        free(entries);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return CS_LIBRARY_DB_UNAVAILABLE;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (!saw_valid_row) {
        free(entries);
        return CS_LIBRARY_DB_UNAVAILABLE;
    }

    qsort(entries, total, sizeof(*entries), cs_browser_db_sort_compare);

    memset(result, 0, sizeof(*result));
    result->offset = offset;
    result->total_count = total;
    result->truncated = scan_truncated;
    if (CS_SAFE_SNPRINTF(result->scope, sizeof(result->scope), "%s", cs_browser_scope_name(CS_SCOPE_ROMS)) != 0
        || CS_SAFE_SNPRINTF(result->root_path, sizeof(result->root_path), "%s", root) != 0
        || CS_SAFE_SNPRINTF(result->path, sizeof(result->path), "%s", "") != 0
        || cs_browser_write_title(result->title, sizeof(result->title), CS_SCOPE_ROMS, platform) != 0
        || cs_browser_write_breadcrumbs(result, "") != 0) {
        free(entries);
        return CS_LIBRARY_DB_INTERNAL;
    }

    for (i = offset; i < total && out_count < CS_BROWSER_PAGE_SIZE; ++i) {
        result->entries[out_count++] = entries[i].entry;
    }
    result->count = out_count;

    free(entries);
    return CS_LIBRARY_DB_OK;
}

static int cs_browser_result_path_seen(const cs_browser_db_entry *entries, size_t count, const char *path) {
    size_t i;

    if (!entries || !path) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(entries[i].entry.path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static cs_browser_list_status cs_browser_list_merged_rom_filesystem(const cs_paths *paths,
                                                                    const cs_platform_info *platform,
                                                                    const char *relative_path,
                                                                    size_t offset,
                                                                    const char *query,
                                                                    const cs_browser_sort_options *sort_options,
                                                                    cs_browser_result *result) {
    cs_browser_db_entry *entries = NULL;
    cs_browser_sort_options normalized_sort = cs_browser_normalize_sort_options(sort_options);
    unsigned int path_flags = CS_PATH_FLAG_ALLOW_EMPTY;
    size_t total = 0;
    size_t out_count = 0;
    size_t i;
    int scan_truncated = 0;
    int opened_any = 0;
    const char *request_relative = relative_path ? relative_path : "";
    char display_root[CS_PATH_MAX];

    if (!paths || !platform || !result) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_scope_allows_hidden_for_platform(CS_SCOPE_ROMS, platform)) {
        path_flags |= CS_PATH_FLAG_ALLOW_HIDDEN;
    }
    if (cs_browser_write_root_for_scope(paths, CS_SCOPE_ROMS, platform, display_root, sizeof(display_root)) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    entries = (cs_browser_db_entry *) calloc(CS_BROWSER_SCAN_CAP, sizeof(*entries));
    if (!entries) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    for (i = 0; i < paths->source_count; ++i) {
        const cs_path_source *source = &paths->sources[i];
        char source_root[CS_PATH_MAX];
        char target_path[CS_PATH_MAX];
        DIR *dir;
        struct dirent *entry;

        if (cs_browser_rom_platform_root_for_source(source, platform, source_root, sizeof(source_root)) != 0
            || cs_resolve_path_under_root_with_flags(source_root,
                                                     request_relative,
                                                     path_flags,
                                                     target_path,
                                                     sizeof(target_path))
                   != 0) {
            continue;
        }

        dir = opendir(target_path);
        if (!dir) {
            continue;
        }
        opened_any = 1;

        while ((entry = readdir(dir)) != NULL) {
            char entry_absolute[CS_PATH_MAX];
            char platform_relative[CS_PATH_MAX];
            char entry_path[CS_PATH_MAX];
            char thumbnail_path[CS_PATH_MAX];
            struct stat st;
            int is_dir;
            cs_browser_db_entry *out;

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (cs_join_path(entry_absolute, sizeof(entry_absolute), target_path, entry->d_name) != 0) {
                continue;
            }
            if (!cs_browser_should_include_entry(CS_SCOPE_ROMS, platform, entry->d_name, entry_absolute)) {
                continue;
            }
            if (!cs_browser_name_matches_query(entry->d_name, query)) {
                continue;
            }
            if (lstat(entry_absolute, &st) != 0 || S_ISLNK(st.st_mode)
                || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
                continue;
            }

            is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            if (request_relative[0] != '\0') {
                if (CS_SAFE_SNPRINTF(platform_relative,
                                     sizeof(platform_relative),
                                     "%s/%s",
                                     request_relative,
                                     entry->d_name)
                    != 0) {
                    continue;
                }
            } else if (CS_SAFE_SNPRINTF(platform_relative, sizeof(platform_relative), "%s", entry->d_name) != 0) {
                continue;
            }

            if (is_dir) {
                /* Directories are merged by platform-relative path, so entering
                   one opens and merges that directory from every source. */
                if (CS_SAFE_SNPRINTF(entry_path, sizeof(entry_path), "%s", platform_relative) != 0) {
                    continue;
                }
                if (cs_browser_result_path_seen(entries, total, entry_path)) {
                    continue;
                }
            } else {
                /* Files keep source identity in their path handle so operations
                   can distinguish same-name ROMs that live on different cards. */
                if (cs_browser_rom_entry_path_for_source(paths,
                                                         source,
                                                         platform_relative,
                                                         entry_path,
                                                         sizeof(entry_path))
                    != 0) {
                    continue;
                }
            }

            if (total >= CS_BROWSER_SCAN_CAP) {
                scan_truncated = 1;
                continue;
            }

            out = &entries[total++];
            if (CS_SAFE_SNPRINTF(out->entry.name, sizeof(out->entry.name), "%s", entry->d_name) != 0
                || CS_SAFE_SNPRINTF(out->entry.path, sizeof(out->entry.path), "%s", entry_path) != 0
                || CS_SAFE_SNPRINTF(out->entry.type,
                                    sizeof(out->entry.type),
                                    "%s",
                                    cs_browser_entry_type_for_scope(CS_SCOPE_ROMS, is_dir, platform_relative))
                       != 0) {
                (void) closedir(dir);
                free(entries);
                return CS_BROWSER_LIST_INTERNAL;
            }
            out->entry.size = is_dir ? 0 : (unsigned long long) st.st_size;
            out->entry.modified = (long long) st.st_mtime;
            out->is_dir = is_dir;
            out->sort_column = normalized_sort.column;
            out->sort_direction = normalized_sort.direction;

            if (!is_dir
                && cs_browser_write_thumbnail(source_root,
                                              source->images_root,
                                              platform,
                                              platform_relative,
                                              thumbnail_path,
                                              sizeof(thumbnail_path))
                       == 0
                && thumbnail_path[0] != '\0') {
                if (paths->source_count > 1 && source != &paths->sources[0]) {
                    (void) cs_library_db_virtual_file_path_for_source(paths,
                                                                      source,
                                                                      thumbnail_path,
                                                                      out->entry.thumbnail_path,
                                                                      sizeof(out->entry.thumbnail_path));
                } else {
                    (void) CS_SAFE_SNPRINTF(out->entry.thumbnail_path,
                                            sizeof(out->entry.thumbnail_path),
                                            "%s",
                                            thumbnail_path);
                }
            }
        }

        (void) closedir(dir);
    }

    if (!opened_any && request_relative[0] != '\0') {
        free(entries);
        return CS_BROWSER_LIST_NOT_FOUND;
    }

    qsort(entries, total, sizeof(*entries), cs_browser_db_sort_compare);

    memset(result, 0, sizeof(*result));
    result->offset = offset;
    result->total_count = total;
    result->truncated = scan_truncated;
    if (CS_SAFE_SNPRINTF(result->scope, sizeof(result->scope), "%s", cs_browser_scope_name(CS_SCOPE_ROMS)) != 0
        || CS_SAFE_SNPRINTF(result->root_path, sizeof(result->root_path), "%s", display_root)
               != 0
        || CS_SAFE_SNPRINTF(result->path, sizeof(result->path), "%s", request_relative) != 0
        || cs_browser_write_title(result->title, sizeof(result->title), CS_SCOPE_ROMS, platform) != 0
        || cs_browser_write_breadcrumbs(result, request_relative) != 0) {
        free(entries);
        return CS_BROWSER_LIST_INTERNAL;
    }

    for (i = offset; i < total && out_count < CS_BROWSER_PAGE_SIZE; ++i) {
        result->entries[out_count++] = entries[i].entry;
    }
    result->count = out_count;

    free(entries);
    return CS_BROWSER_LIST_OK;
}

cs_browser_list_status cs_browser_list(const cs_paths *paths,
                                       cs_browser_scope scope,
                                       const cs_platform_info *platform,
                                       const char *relative_path,
                                       size_t offset,
                                       const char *query,
                                       cs_browser_result *result) {
    return cs_browser_list_with_sort(paths, scope, platform, relative_path, offset, query, NULL, result);
}

cs_browser_list_status cs_browser_list_with_sort(const cs_paths *paths,
                                                 cs_browser_scope scope,
                                                 const cs_platform_info *platform,
                                                 const char *relative_path,
                                                 size_t offset,
                                                 const char *query,
                                                 const cs_browser_sort_options *sort_options,
                                                 cs_browser_result *result) {
    char root[CS_PATH_MAX];
    char target_path[CS_PATH_MAX];
    char guard_root[CS_PATH_MAX];
    char effective_relative[CS_PATH_MAX];
    char virtual_prefix[CS_PATH_MAX];
    cs_browser_sort_options normalized_sort = cs_browser_normalize_sort_options(sort_options);
    unsigned int path_flags = CS_PATH_FLAG_ALLOW_EMPTY;
    const cs_path_source *selected_source = NULL;
    int root_fd = -1;
    int dir_fd = -1;
    DIR *dir;
    struct dirent *entry;
    cs_browser_sort_entry *sort_buf = NULL;
    size_t sort_count = 0;
    int scan_truncated = 0;
    size_t out_count = 0;
    size_t i;

    if (!paths || !result || scope == CS_SCOPE_INVALID) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_scope_requires_platform(scope) && !platform) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (scope == CS_SCOPE_FILES && paths->source_count > 1) {
        const char *slash;
        size_t prefix_len;

        if (!relative_path || relative_path[0] == '\0') {
            return cs_browser_list_source_virtual_root(paths, scope, platform, offset, query, result);
        }
        if (cs_paths_resolve_files_path(paths,
                                        relative_path,
                                        root,
                                        sizeof(root),
                                        effective_relative,
                                        sizeof(effective_relative),
                                        &selected_source)
            != 0) {
            return CS_BROWSER_LIST_NOT_FOUND;
        }
        slash = strchr(relative_path, '/');
        prefix_len = slash ? (size_t) (slash - relative_path) : strlen(relative_path);
        if (prefix_len >= sizeof(virtual_prefix)) {
            return CS_BROWSER_LIST_INTERNAL;
        }
        memcpy(virtual_prefix, relative_path, prefix_len);
        virtual_prefix[prefix_len] = '\0';
    } else {
        selected_source = cs_browser_select_source_for_scope(paths, scope, platform);
        if (cs_browser_root_for_scope(paths, scope, platform, root, sizeof(root)) != 0) {
            return CS_BROWSER_LIST_INTERNAL;
        }
        if (CS_SAFE_SNPRINTF(effective_relative, sizeof(effective_relative), "%s", relative_path ? relative_path : "") != 0) {
            return CS_BROWSER_LIST_INTERNAL;
        }
        virtual_prefix[0] = '\0';
    }
    if (cs_browser_guard_root_for_scope(paths, scope, selected_source, guard_root, sizeof(guard_root)) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    if (scope == CS_SCOPE_ROMS && (!relative_path || relative_path[0] == '\0')) {
        cs_library_db_status db_status = cs_browser_list_db_roms(paths,
                                                                 platform,
                                                                 relative_path,
                                                                 offset,
                                                                 query,
                                                                 &normalized_sort,
                                                                 root,
                                                                 result);

        if (db_status == CS_LIBRARY_DB_OK) {
            return CS_BROWSER_LIST_OK;
        }
        if (db_status == CS_LIBRARY_DB_INTERNAL) {
            return CS_BROWSER_LIST_INTERNAL;
        }
    }

    if (scope == CS_SCOPE_ROMS && paths->source_count > 1) {
        return cs_browser_list_merged_rom_filesystem(paths,
                                                     platform,
                                                     relative_path,
                                                     offset,
                                                     query,
                                                     &normalized_sort,
                                                     result);
    }

    memset(result, 0, sizeof(*result));
    /* Echo the requested offset even when it is past the end; the page is then
     * intentionally empty while total_count still reports the matched entries.
     */
    result->offset = offset;
    if (CS_SAFE_SNPRINTF(result->scope, sizeof(result->scope), "%s", cs_browser_scope_name(scope)) != 0
        || CS_SAFE_SNPRINTF(result->root_path, sizeof(result->root_path), "%s", root) != 0
        || CS_SAFE_SNPRINTF(result->path, sizeof(result->path), "%s", relative_path ? relative_path : "") != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_write_title(result->title, sizeof(result->title), scope, platform) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_write_breadcrumbs(result, relative_path ? relative_path : "") != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    if (cs_browser_scope_allows_hidden_for_platform(scope, platform)) {
        path_flags |= CS_PATH_FLAG_ALLOW_HIDDEN;
    }
    if (cs_resolve_path_under_root_with_flags(root,
                                              effective_relative,
                                              path_flags,
                                              target_path,
                                              sizeof(target_path))
        != 0) {
        return CS_BROWSER_LIST_NOT_FOUND;
    }

    root_fd = cs_open_guarded_directory(guard_root, root);
    if (root_fd < 0) {
        if (scope != CS_SCOPE_FILES && errno == ENOENT) {
            return CS_BROWSER_LIST_OK;
        }
        return cs_browser_path_failure_status(errno);
    }

    if (effective_relative[0] != '\0') {
        dir_fd = cs_open_directory_from_fd(root_fd, effective_relative);
        root_fd = -1;
    } else {
        dir_fd = root_fd;
    }
    if (dir_fd < 0) {
        return cs_browser_path_failure_status(errno);
    }

    dir = fdopendir(dir_fd);
    if (!dir) {
        int saved_errno = errno;

        close(dir_fd);
        return cs_browser_path_failure_status(saved_errno);
    }
    if (dir_fd == root_fd) {
        root_fd = -1;
    }

    /* Keep the capped sort buffer off the stack; CS_BROWSER_SCAN_CAP entries
     * are roughly 1 MiB on the target builds.
     */
    sort_buf = (cs_browser_sort_entry *) calloc(CS_BROWSER_SCAN_CAP, sizeof(*sort_buf));
    if (!sort_buf) {
        (void) closedir(dir);
        if (root_fd >= 0) {
            close(root_fd);
        }
        return CS_BROWSER_LIST_INTERNAL;
    }

    while ((entry = readdir(dir)) != NULL) {
        char entry_absolute[CS_PATH_MAX];
        struct stat st;
        cs_browser_sort_entry *sort_entry;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (cs_join_path(entry_absolute, sizeof(entry_absolute), target_path, entry->d_name) != 0) {
            continue;
        }
        if (!cs_browser_should_include_entry(scope, platform, entry->d_name, entry_absolute)) {
            continue;
        }
        if (!cs_browser_name_matches_query(entry->d_name, query)) {
            continue;
        }
        if (fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            continue;
        }
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            continue;
        }

        if (sort_count >= CS_BROWSER_SCAN_CAP) {
            scan_truncated = 1;
            continue;
        }

        sort_entry = &sort_buf[sort_count];
        if (CS_SAFE_SNPRINTF(sort_entry->name, sizeof(sort_entry->name), "%s", entry->d_name) != 0) {
            continue;
        }
        sort_entry->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        sort_entry->size = S_ISREG(st.st_mode) ? (unsigned long long) st.st_size : 0;
        sort_entry->modified = (long long) st.st_mtime;
        sort_entry->sort_column = normalized_sort.column;
        sort_entry->sort_direction = normalized_sort.direction;
        sort_count += 1;
    }

    (void) closedir(dir);
    if (root_fd >= 0) {
        close(root_fd);
    }

    qsort(sort_buf, sort_count, sizeof(*sort_buf), cs_browser_sort_compare);

    result->total_count = sort_count;
    result->truncated = scan_truncated;

    for (i = offset; i < sort_count && out_count < CS_BROWSER_PAGE_SIZE; ++i) {
        const cs_browser_sort_entry *src = &sort_buf[i];
        cs_browser_entry *dst = &result->entries[out_count];
        char entry_relative[CS_PATH_MAX];
        const char *type_str;

        if (virtual_prefix[0] != '\0') {
            if (effective_relative[0] != '\0') {
                if (CS_SAFE_SNPRINTF(entry_relative,
                                     sizeof(entry_relative),
                                     "%s/%s/%s",
                                     virtual_prefix,
                                     effective_relative,
                                     src->name)
                    != 0) {
                    continue;
                }
            } else if (CS_SAFE_SNPRINTF(entry_relative, sizeof(entry_relative), "%s/%s", virtual_prefix, src->name)
                       != 0) {
                continue;
            }
        } else if (relative_path && relative_path[0] != '\0') {
            if (CS_SAFE_SNPRINTF(entry_relative, sizeof(entry_relative), "%s/%s", relative_path, src->name) != 0) {
                continue;
            }
        } else if (CS_SAFE_SNPRINTF(entry_relative, sizeof(entry_relative), "%s", src->name) != 0) {
            continue;
        }

        memset(dst, 0, sizeof(*dst));
        if (CS_SAFE_SNPRINTF(dst->name, sizeof(dst->name), "%s", src->name) != 0
            || CS_SAFE_SNPRINTF(dst->path, sizeof(dst->path), "%s", entry_relative) != 0) {
            free(sort_buf);
            return CS_BROWSER_LIST_INTERNAL;
        }
        dst->size = src->size;
        dst->modified = src->modified;

        type_str = cs_browser_entry_type_for_scope(scope, src->is_dir, entry_relative);
        if (CS_SAFE_SNPRINTF(dst->type, sizeof(dst->type), "%s", type_str) != 0) {
            free(sort_buf);
            return CS_BROWSER_LIST_INTERNAL;
        }

        if (scope == CS_SCOPE_ROMS && !src->is_dir) {
            (void) cs_browser_write_thumbnail(root,
                                              selected_source ? selected_source->images_root : paths->images_root,
                                              platform,
                                              entry_relative,
                                              dst->thumbnail_path,
                                              sizeof(dst->thumbnail_path));
        }

        out_count += 1;
    }

    result->count = out_count;
    free(sort_buf);
    return CS_BROWSER_LIST_OK;
}
