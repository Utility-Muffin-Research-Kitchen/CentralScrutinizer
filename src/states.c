#include "cs_states.h"
#include "cs_util.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum cs_state_family {
    CS_STATE_FAMILY_UNKNOWN = 0,
    CS_STATE_FAMILY_RETROARCH = 1,
    CS_STATE_FAMILY_AUTO = 2,
} cs_state_family;

typedef struct cs_state_group {
    cs_state_entry entry;
    char stem[256];
    unsigned int family_mask;
} cs_state_group;

typedef struct cs_state_file_match {
    char stem[256];
    int slot;
    int is_preview;
    cs_state_family family;
} cs_state_file_match;

typedef struct cs_state_rom_stems {
    char (*items)[256];
    size_t count;
    size_t capacity;
} cs_state_rom_stems;

static int cs_state_path_is_regular_file(const char *path, struct stat *st_out) {
    struct stat st;

    if (!path || lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    if (st_out) {
        *st_out = st;
    }
    return 1;
}

static int cs_state_path_is_directory(const char *path) {
    struct stat st;

    return path && lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int cs_state_add_path(char paths[][CS_PATH_MAX],
                             size_t *count,
                             size_t capacity,
                             const char *value) {
    size_t i;

    if (!paths || !count || !value || value[0] == '\0') {
        return -1;
    }

    for (i = 0; i < *count; ++i) {
        if (strcmp(paths[i], value) == 0) {
            return 0;
        }
    }
    if (*count >= capacity) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(paths[*count], CS_PATH_MAX, "%s", value) != 0) {
        return -1;
    }
    *count += 1;
    return 0;
}

static int cs_state_note_file(cs_state_entry *entry,
                              const char *relative_path,
                              const struct stat *st,
                              int is_preview) {
    if (!entry || !relative_path || !st) {
        return -1;
    }

    if (cs_state_add_path(entry->download_paths,
                          &entry->download_path_count,
                          CS_STATE_MAX_PATHS,
                          relative_path)
            != 0
        || cs_state_add_path(entry->delete_paths, &entry->delete_path_count, CS_STATE_MAX_PATHS, relative_path)
               != 0) {
        return -1;
    }

    if (is_preview
        && CS_SAFE_SNPRINTF(entry->preview_path, sizeof(entry->preview_path), "%s", relative_path) != 0) {
        return -1;
    }

    entry->size += (unsigned long long) st->st_size;
    if ((long long) st->st_mtime > entry->modified) {
        entry->modified = (long long) st->st_mtime;
    }
    return 0;
}

static int cs_state_copy_rom_stem(char *dst, size_t dst_size, const char *name) {
    char *dot;

    if (!dst || dst_size == 0 || !name || name[0] == '\0' || name[0] == '.') {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(dst, dst_size, "%s", name) != 0) {
        return -1;
    }
    dot = strrchr(dst, '.');
    if (dot && dot != dst) {
        *dot = '\0';
    }
    return dst[0] == '\0' ? -1 : 0;
}

static int cs_state_rom_stems_add(cs_state_rom_stems *stems, const char *value) {
    size_t i;
    char (*resized)[256];
    size_t new_capacity;

    if (!stems || !value || value[0] == '\0') {
        return -1;
    }
    for (i = 0; i < stems->count; ++i) {
        if (strcmp(stems->items[i], value) == 0) {
            return 0;
        }
    }
    if (stems->count >= stems->capacity) {
        new_capacity = stems->capacity > 0 ? stems->capacity * 2 : 32;
        if (new_capacity > SIZE_MAX / sizeof(*stems->items)) {
            return -1;
        }
        resized = (char (*)[256]) realloc(stems->items, new_capacity * sizeof(*stems->items));
        if (!resized) {
            return -1;
        }
        stems->items = resized;
        stems->capacity = new_capacity;
    }
    if (CS_SAFE_SNPRINTF(stems->items[stems->count], sizeof(stems->items[stems->count]), "%s", value) != 0) {
        return -1;
    }
    stems->count += 1;
    return 0;
}

static int cs_state_rom_stems_contains(const cs_state_rom_stems *stems, const char *value) {
    size_t i;

    if (!stems || !value || value[0] == '\0') {
        return 0;
    }
    for (i = 0; i < stems->count; ++i) {
        if (strcmp(stems->items[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cs_state_collect_rom_stems_from_dir(const char *dir_path, cs_state_rom_stems *stems) {
    DIR *dir;
    struct dirent *entry;

    if (!dir_path || !stems) {
        return -1;
    }

    dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char absolute_path[CS_PATH_MAX];
        char stem[256];

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (CS_SAFE_SNPRINTF(absolute_path, sizeof(absolute_path), "%s/%s", dir_path, entry->d_name) != 0
            || !cs_state_path_is_regular_file(absolute_path, NULL)) {
            continue;
        }
        if (cs_state_copy_rom_stem(stem, sizeof(stem), entry->d_name) == 0
            && cs_state_rom_stems_add(stems, stem) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

static int cs_state_collect_rom_stems(const cs_paths *paths,
                                      const cs_platform_info *platform,
                                      cs_state_rom_stems *stems) {
    size_t i;

    if (!paths || !platform || !stems) {
        return -1;
    }

    for (i = 0; i < paths->source_count; ++i) {
        char rom_dir[CS_PATH_MAX];

        if (platform->rom_directory[0] != '\0'
            && CS_SAFE_SNPRINTF(rom_dir,
                                sizeof(rom_dir),
                                "%s/%s",
                                paths->sources[i].roms_root,
                                platform->rom_directory)
                   == 0
            && cs_state_collect_rom_stems_from_dir(rom_dir, stems) != 0) {
            return -1;
        }
        if (platform->primary_code[0] != '\0' && strcmp(platform->primary_code, platform->rom_directory) != 0
            && CS_SAFE_SNPRINTF(rom_dir,
                                sizeof(rom_dir),
                                "%s/%s",
                                paths->sources[i].roms_root,
                                platform->primary_code)
                   == 0
            && cs_state_collect_rom_stems_from_dir(rom_dir, stems) != 0) {
            return -1;
        }
    }
    return 0;
}

static int cs_state_parse_digits(const char *value, size_t len, int *slot_out) {
    size_t i;
    long slot = 0;

    if (!value || len == 0 || !slot_out) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char) value[i];

        if (ch < '0' || ch > '9') {
            return 0;
        }
        slot = slot * 10 + (long) (ch - '0');
        if (slot > CS_STATE_SLOT_MAX) {
            return 0;
        }
    }
    *slot_out = (int) slot;
    return 1;
}

static int cs_state_parse_filename(const char *name, cs_state_file_match *match) {
    const char *state_suffix = NULL;
    const char *cursor;
    const char *tail;
    size_t stem_len;
    size_t tail_len;
    int slot = 0;
    int is_preview = 0;

    if (!name || !match) {
        return -1;
    }

    memset(match, 0, sizeof(*match));
    cursor = name;
    while ((cursor = strstr(cursor, ".state")) != NULL) {
        state_suffix = cursor;
        cursor += 1;
    }
    if (!state_suffix || state_suffix == name) {
        return -1;
    }

    stem_len = (size_t) (state_suffix - name);
    if (stem_len == 0 || stem_len >= sizeof(match->stem)) {
        return -1;
    }
    tail = state_suffix + strlen(".state");
    tail_len = strlen(tail);

    if (strcmp(tail, ".auto") == 0 || strcmp(tail, ".auto.png") == 0) {
        slot = 9;
        is_preview = strcmp(tail, ".auto.png") == 0;
        match->family = CS_STATE_FAMILY_AUTO;
    } else {
        const char *digits = tail;
        size_t digit_len = tail_len;

        match->family = CS_STATE_FAMILY_RETROARCH;
        if (strcmp(tail, "") == 0) {
            slot = 0;
        } else if (strcmp(tail, ".png") == 0) {
            slot = 0;
            is_preview = 1;
        } else {
            if (digits[0] == '.') {
                digits += 1;
                digit_len -= 1;
            }
            if (digit_len > strlen(".png") && strcmp(digits + digit_len - strlen(".png"), ".png") == 0) {
                is_preview = 1;
                digit_len -= strlen(".png");
            }
            if (!cs_state_parse_digits(digits, digit_len, &slot)) {
                return -1;
            }
        }
    }

    memcpy(match->stem, name, stem_len);
    match->stem[stem_len] = '\0';
    match->slot = slot;
    match->is_preview = is_preview;
    return 0;
}

static void cs_state_write_slot_label(cs_state_group *group) {
    if (!group) {
        return;
    }

    if ((group->family_mask & CS_STATE_FAMILY_AUTO) != 0) {
        (void) CS_SAFE_SNPRINTF(group->entry.slot_label,
                                sizeof(group->entry.slot_label),
                                "%s",
                                "Auto Resume");
        (void) CS_SAFE_SNPRINTF(group->entry.kind, sizeof(group->entry.kind), "%s", "auto-resume");
        return;
    }

    (void) CS_SAFE_SNPRINTF(group->entry.slot_label, sizeof(group->entry.slot_label), "Slot %d", group->entry.slot + 1);
    (void) CS_SAFE_SNPRINTF(group->entry.kind, sizeof(group->entry.kind), "%s", "slot");
}

static void cs_state_write_format(cs_state_group *group) {
    if (!group) {
        return;
    }

    if (strstr(group->entry.core_dir, "Mupen64Plus") != NULL) {
        (void) CS_SAFE_SNPRINTF(group->entry.format, sizeof(group->entry.format), "%s", "Mupen64Plus");
        return;
    }
    (void) CS_SAFE_SNPRINTF(group->entry.format, sizeof(group->entry.format), "%s", "RetroArch");
}

static int cs_state_find_group(cs_state_group *groups,
                               size_t count,
                               const char *core_dir,
                               const char *stem,
                               int slot) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (groups[i].entry.slot == slot && strcmp(groups[i].entry.core_dir, core_dir) == 0
            && strcmp(groups[i].stem, stem) == 0) {
            return (int) i;
        }
    }

    return -1;
}

static int cs_state_ensure_group_capacity(cs_state_group **groups, size_t *capacity, size_t required) {
    size_t new_capacity;
    cs_state_group *resized;

    if (!groups || !capacity) {
        return -1;
    }
    if (required <= *capacity) {
        return 0;
    }

    new_capacity = *capacity > 0 ? *capacity : 32;
    while (new_capacity < required) {
        if (new_capacity > (SIZE_MAX / 2)) {
            return -1;
        }
        new_capacity *= 2;
    }
    if (new_capacity > SIZE_MAX / sizeof(**groups)) {
        return -1;
    }

    resized = (cs_state_group *) realloc(*groups, new_capacity * sizeof(**groups));
    if (!resized) {
        return -1;
    }

    *groups = resized;
    *capacity = new_capacity;
    return 0;
}

static int cs_state_add_payload(cs_state_group **groups,
                                size_t *count,
                                size_t *capacity,
                                const char *core_dir,
                                const char *relative_path,
                                const struct stat *st,
                                const cs_state_file_match *match) {
    int index;
    cs_state_group *group;

    if (!groups || !count || !capacity || !core_dir || !relative_path || !st || !match) {
        return -1;
    }

    index = cs_state_find_group(*groups, *count, core_dir, match->stem, match->slot);
    if (index < 0) {
        if (cs_state_ensure_group_capacity(groups, capacity, *count + 1) != 0) {
            return -1;
        }

        index = (int) *count;
        group = &(*groups)[index];
        memset(group, 0, sizeof(*group));
        if (CS_SAFE_SNPRINTF(group->entry.id,
                             sizeof(group->entry.id),
                             "%s:%s:%d",
                             core_dir,
                             match->stem,
                             match->slot)
                != 0
            || CS_SAFE_SNPRINTF(group->entry.title, sizeof(group->entry.title), "%s", match->stem) != 0
            || CS_SAFE_SNPRINTF(group->entry.core_dir, sizeof(group->entry.core_dir), "%s", core_dir) != 0
            || CS_SAFE_SNPRINTF(group->stem, sizeof(group->stem), "%s", match->stem) != 0) {
            return -1;
        }

        group->entry.slot = match->slot;
        *count += 1;
    }

    group = &(*groups)[index];
    group->family_mask |= (unsigned int) match->family;
    cs_state_write_slot_label(group);
    if (cs_state_note_file(&group->entry, relative_path, st, match->is_preview) != 0) {
        return -1;
    }
    cs_state_write_format(group);
    return 0;
}

static int cs_state_relative_for_source(const cs_paths *paths,
                                        const cs_path_source *source,
                                        const char *state_relative,
                                        char *relative_path,
                                        size_t relative_path_size) {
    const char *base_relative = "States";
    size_t root_len;

    if (!paths || !source || !state_relative || !relative_path || relative_path_size == 0) {
        return -1;
    }

    root_len = strlen(source->root);
    if (strncmp(source->states_root, source->root, root_len) == 0 && source->states_root[root_len] == '/') {
        base_relative = source->states_root + root_len + 1;
    }

    if (paths->source_count > 1) {
        return CS_SAFE_SNPRINTF(relative_path,
                                relative_path_size,
                                "%s/%s/%s",
                                source->alias,
                                base_relative,
                                state_relative);
    }
    return CS_SAFE_SNPRINTF(relative_path, relative_path_size, "%s/%s", base_relative, state_relative);
}

static int cs_state_scan_dir(const cs_paths *paths,
                             const cs_path_source *source,
                             const cs_state_rom_stems *rom_stems,
                             const char *absolute_dir,
                             const char *relative_prefix,
                             const char *core_dir,
                             cs_state_group **groups,
                             size_t *count,
                             size_t *group_capacity) {
    DIR *dir;
    struct dirent *entry;

    if (!paths || !source || !rom_stems || !absolute_dir || !relative_prefix || !core_dir || !groups || !count
        || !group_capacity) {
        return -1;
    }

    dir = opendir(absolute_dir);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char absolute_path[CS_PATH_MAX];
        char state_relative[CS_PATH_MAX];
        char relative_path[CS_PATH_MAX];
        struct stat st;
        cs_state_file_match match;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (cs_state_parse_filename(entry->d_name, &match) != 0) {
            continue;
        }
        if (!cs_state_rom_stems_contains(rom_stems, match.stem)) {
            continue;
        }
        if (CS_SAFE_SNPRINTF(absolute_path, sizeof(absolute_path), "%s/%s", absolute_dir, entry->d_name) != 0
            || !cs_state_path_is_regular_file(absolute_path, &st)) {
            continue;
        }
        if (relative_prefix[0] != '\0') {
            if (CS_SAFE_SNPRINTF(state_relative, sizeof(state_relative), "%s/%s", relative_prefix, entry->d_name) != 0) {
                closedir(dir);
                return -1;
            }
        } else if (CS_SAFE_SNPRINTF(state_relative, sizeof(state_relative), "%s", entry->d_name) != 0) {
            closedir(dir);
            return -1;
        }
        if (cs_state_relative_for_source(paths, source, state_relative, relative_path, sizeof(relative_path)) != 0
            || cs_state_add_payload(groups, count, group_capacity, core_dir, relative_path, &st, &match) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

static int cs_state_scan_source(const cs_paths *paths,
                                const cs_path_source *source,
                                const cs_state_rom_stems *rom_stems,
                                cs_state_group **groups,
                                size_t *count,
                                size_t *group_capacity) {
    DIR *dir;
    struct dirent *entry;
    char flat_label[128];

    if (!paths || !source || !rom_stems || !groups || !count || !group_capacity) {
        return -1;
    }

    if (!cs_state_path_is_directory(source->states_root)) {
        return 0;
    }

    if (paths->source_count > 1) {
        if (CS_SAFE_SNPRINTF(flat_label, sizeof(flat_label), "%s/States", source->alias) != 0) {
            return -1;
        }
    } else if (CS_SAFE_SNPRINTF(flat_label, sizeof(flat_label), "%s", "States") != 0) {
        return -1;
    }
    if (cs_state_scan_dir(paths, source, rom_stems, source->states_root, "", flat_label, groups, count, group_capacity)
        != 0) {
        return -1;
    }

    dir = opendir(source->states_root);
    if (!dir) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        char absolute_dir[CS_PATH_MAX];
        char core_dir[128];

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (CS_SAFE_SNPRINTF(absolute_dir, sizeof(absolute_dir), "%s/%s", source->states_root, entry->d_name) != 0
            || !cs_state_path_is_directory(absolute_dir)) {
            continue;
        }
        if (paths->source_count > 1) {
            if (CS_SAFE_SNPRINTF(core_dir, sizeof(core_dir), "%s/%s", source->alias, entry->d_name) != 0) {
                closedir(dir);
                return -1;
            }
        } else if (CS_SAFE_SNPRINTF(core_dir, sizeof(core_dir), "%s", entry->d_name) != 0) {
            closedir(dir);
            return -1;
        }
        if (cs_state_scan_dir(paths,
                              source,
                              rom_stems,
                              absolute_dir,
                              entry->d_name,
                              core_dir,
                              groups,
                              count,
                              group_capacity)
            != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static int cs_state_compare_descending(const void *left, const void *right) {
    const cs_state_group *a = (const cs_state_group *) left;
    const cs_state_group *b = (const cs_state_group *) right;

    if (a->entry.modified != b->entry.modified) {
        return a->entry.modified > b->entry.modified ? -1 : 1;
    }
    if (a->entry.slot != b->entry.slot) {
        return a->entry.slot - b->entry.slot;
    }

    return strcmp(a->entry.id, b->entry.id);
}

int cs_states_collect(const cs_paths *paths,
                      const cs_platform_info *platform,
                      cs_state_entry *entries,
                      size_t entry_capacity,
                      size_t *entry_count_out,
                      int *truncated_out) {
    cs_state_group *groups = NULL;
    cs_state_rom_stems rom_stems = {0};
    size_t count = 0;
    size_t group_capacity = 0;
    size_t source_index;

    if (entry_count_out) {
        *entry_count_out = 0;
    }
    if (truncated_out) {
        *truncated_out = 0;
    }
    if (!paths || !platform) {
        return -1;
    }
    if (cs_state_collect_rom_stems(paths, platform, &rom_stems) != 0) {
        free(rom_stems.items);
        return -1;
    }
    if (rom_stems.count == 0) {
        free(rom_stems.items);
        return 0;
    }

    for (source_index = 0; source_index < paths->source_count; ++source_index) {
        if (cs_state_scan_source(paths,
                                 &paths->sources[source_index],
                                 &rom_stems,
                                 &groups,
                                 &count,
                                 &group_capacity)
            != 0) {
            free(groups);
            free(rom_stems.items);
            return -1;
        }
    }

    qsort(groups, count, sizeof(groups[0]), cs_state_compare_descending);
    if (entries) {
        size_t i;
        size_t limit = count < entry_capacity ? count : entry_capacity;

        for (i = 0; i < limit; ++i) {
            entries[i] = groups[i].entry;
        }
        if (entry_count_out) {
            *entry_count_out = count;
        }
    } else if (entry_count_out) {
        *entry_count_out = count;
    }
    if (truncated_out && entries && count > entry_capacity) {
        *truncated_out = 1;
    }
    free(groups);
    free(rom_stems.items);
    return 0;
}
