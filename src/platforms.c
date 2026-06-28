#include "cs_platforms.h"
#include "cs_catalog.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define CS_DISCOVERED_PLATFORM_MAX 128
#define CS_MODELED_PLATFORM_MAX 160
#define CS_MODELED_PATTERN_MAX 96
#define CS_MODELED_CORE_MAX 32
#define CS_MODELED_ROM_DIR_MAX 24
#define CS_CATALOG_ID_MAX 64
#define CS_SHORTCUT_MARKER ".shortcut"
#define CS_SHORTCUT_PREFIX "\xEF\xBB\xBF"
/* Keep prefix-only detection for legacy shortcuts that predate the .shortcut marker file. */
#define CS_LEGACY_SHORTCUT_PREFIX "\xE2\x98\x85 "

typedef struct cs_discovered_rom_dir {
    char system_name[128];
    char system_code[CS_PLATFORM_CODE_MAX];
    char dir_name[256];
} cs_discovered_rom_dir;

typedef struct cs_platform_identity {
    const char *catalog_id;
    const char *tag;
    const char *primary_code;
    const char *group;
    const char *icon;
    const char *fallback_name;
} cs_platform_identity;

typedef struct cs_platform_alias {
    const char *alias;
    const char *tag;
} cs_platform_alias;

typedef struct cs_canonical_alias {
    const char *alias_id;
    const char *canonical_id;
} cs_canonical_alias;

typedef struct cs_modeled_platform {
    cs_platform_info info;
    char catalog_id[CS_CATALOG_ID_MAX];
    char patterns[CS_MODELED_PATTERN_MAX][256];
    size_t pattern_count;
    char candidate_cores[CS_MODELED_CORE_MAX][CS_CATALOG_ID_MAX];
    size_t candidate_core_count;
    char rom_directories[CS_MODELED_ROM_DIR_MAX][256];
    size_t rom_directory_count;
} cs_modeled_platform;

static const cs_platform_identity g_platform_identities[] = {
    {.catalog_id = "32X", .tag = "32X", .primary_code = "32X", .group = "Sega", .icon = "32X", .fallback_name = "Sega 32X"},
    {.catalog_id = "ARCADE", .tag = "FBN", .primary_code = "FBN", .group = "Arcade", .icon = "FBN", .fallback_name = "Arcade"},
    {.catalog_id = "ATARI2600", .tag = "A2600", .primary_code = "A2600", .group = "Atari", .icon = "ATARI2600", .fallback_name = "Atari 2600"},
    {.catalog_id = "COLECO", .tag = "COLECO", .primary_code = "COLECO", .group = "Other", .icon = "COLECOVISION", .fallback_name = "Colecovision"},
    {.catalog_id = "DC", .tag = "DC", .primary_code = "DC", .group = "Sega", .icon = "DC", .fallback_name = "Dreamcast"},
    {.catalog_id = "DOS", .tag = "DOS", .primary_code = "DOS", .group = "Computer", .icon = "DOS", .fallback_name = "MS-DOS"},
    {.catalog_id = "EASYRPG", .tag = "EASYRPG", .primary_code = "EASYRPG", .group = "Computer", .icon = "RPGM", .fallback_name = "EasyRPG"},
    {.catalog_id = "FC", .tag = "FC", .primary_code = "FC", .group = "Nintendo", .icon = "NES", .fallback_name = "NES/Famicom"},
    {.catalog_id = "FDS", .tag = "FDS", .primary_code = "FDS", .group = "Nintendo", .icon = "FDS", .fallback_name = "Famicom Disk System"},
    {.catalog_id = "GB", .tag = "GB", .primary_code = "GB", .group = "Nintendo", .icon = "GB", .fallback_name = "Game Boy"},
    {.catalog_id = "GBA", .tag = "GBA", .primary_code = "GBA", .group = "Nintendo", .icon = "GBA", .fallback_name = "Game Boy Advance"},
    {.catalog_id = "GBC", .tag = "GBC", .primary_code = "GBC", .group = "Nintendo", .icon = "GBC", .fallback_name = "Game Boy Color"},
    {.catalog_id = "GG", .tag = "GG", .primary_code = "GG", .group = "Sega", .icon = "GG", .fallback_name = "Sega Game Gear"},
    {.catalog_id = "GW", .tag = "GW", .primary_code = "GW", .group = "Nintendo", .icon = "GW", .fallback_name = "Game & Watch"},
    {.catalog_id = "LYNX", .tag = "LYNX", .primary_code = "LYNX", .group = "Atari", .icon = "LYNX", .fallback_name = "Atari Lynx"},
    {.catalog_id = "MAME", .tag = "MAME", .primary_code = "MAME", .group = "Arcade", .icon = "MAME", .fallback_name = "Arcade (MAME)"},
    {.catalog_id = "MAME2003", .tag = "MAME2003", .primary_code = "MAME2003", .group = "Arcade", .icon = "MAME", .fallback_name = "Arcade (MAME 2003)"},
    {.catalog_id = "MAME2010", .tag = "MAME2010", .primary_code = "MAME2010", .group = "Arcade", .icon = "MAME", .fallback_name = "Arcade (MAME 2010)"},
    {.catalog_id = "MD", .tag = "MD", .primary_code = "MD", .group = "Sega", .icon = "MD", .fallback_name = "Sega Genesis"},
    {.catalog_id = "MD32X", .tag = "MD32X", .primary_code = "MD32X", .group = "Sega", .icon = "32X", .fallback_name = "Sega 32X"},
    {.catalog_id = "MS", .tag = "SMS", .primary_code = "SMS", .group = "Sega", .icon = "SMS", .fallback_name = "Sega Master System"},
    {.catalog_id = "N64", .tag = "N64", .primary_code = "N64", .group = "Nintendo", .icon = "N64", .fallback_name = "Nintendo 64"},
    {.catalog_id = "NDS", .tag = "NDS", .primary_code = "NDS", .group = "Nintendo", .icon = "NDS", .fallback_name = "Nintendo DS"},
    {.catalog_id = "NEOGEO", .tag = "NEOGEO", .primary_code = "NEOGEO", .group = "SNK", .icon = "NEOGEO", .fallback_name = "Neo Geo"},
    {.catalog_id = "NGP", .tag = "NGP", .primary_code = "NGP", .group = "SNK", .icon = "NGP", .fallback_name = "Neo Geo Pocket"},
    {.catalog_id = "NGPC", .tag = "NGPC", .primary_code = "NGPC", .group = "SNK", .icon = "NGPC", .fallback_name = "Neo Geo Pocket Color"},
    {.catalog_id = "PCE", .tag = "PCE", .primary_code = "PCE", .group = "NEC", .icon = "PCE", .fallback_name = "PC Engine"},
    {.catalog_id = "PCECD", .tag = "PCECD", .primary_code = "PCECD", .group = "NEC", .icon = "PCE", .fallback_name = "PC Engine CD"},
    {.catalog_id = "PICO8", .tag = "P8", .primary_code = "P8", .group = "Computer", .icon = "PICO8", .fallback_name = "Pico-8"},
    {.catalog_id = "PORTS", .tag = "PORTS", .primary_code = "PORTS", .group = "PortMaster", .icon = "PORTMASTER", .fallback_name = "Ports"},
    {.catalog_id = "PS", .tag = "PS", .primary_code = "PS", .group = "Sony", .icon = "PS", .fallback_name = "Sony PlayStation"},
    {.catalog_id = "PSP", .tag = "PSP", .primary_code = "PSP", .group = "Sony", .icon = "PSP", .fallback_name = "PlayStation Portable"},
    {.catalog_id = "SATURN", .tag = "SATURN", .primary_code = "SATURN", .group = "Sega", .icon = "SATURN", .fallback_name = "Sega Saturn"},
    {.catalog_id = "SEGACD", .tag = "SEGACD", .primary_code = "SEGACD", .group = "Sega", .icon = "SEGACD", .fallback_name = "Sega CD"},
    {.catalog_id = "SEVENTYEIGHTHUNDRED", .tag = "A7800", .primary_code = "A7800", .group = "Atari", .icon = "ATARI7800", .fallback_name = "Atari 7800"},
    {.catalog_id = "SFC", .tag = "SFC", .primary_code = "SFC", .group = "Nintendo", .icon = "SNES", .fallback_name = "SNES"},
    {.catalog_id = "SFC_JP", .tag = "SFC_JP", .primary_code = "SFC_JP", .group = "Nintendo", .icon = "SNES", .fallback_name = "Super Famicom"},
    {.catalog_id = "VB", .tag = "VB", .primary_code = "VB", .group = "Nintendo", .icon = "VIRTUALBOY", .fallback_name = "Virtual Boy"},
    {.catalog_id = "VECTREX", .tag = "VECTREX", .primary_code = "VECTREX", .group = "Other", .icon = "VECTREX", .fallback_name = "Vectrex"},
    {.catalog_id = "WS", .tag = "WS", .primary_code = "WS", .group = "Bandai", .icon = "WS", .fallback_name = "WonderSwan"},
    {.catalog_id = "WSC", .tag = "WSC", .primary_code = "WSC", .group = "Bandai", .icon = "WSC", .fallback_name = "WonderSwan Color"},
};

static const cs_platform_alias g_static_aliases[] = {
    {.alias = "ARCADE", .tag = "FBN"},
    {.alias = "ATARI2600", .tag = "A2600"},
    {.alias = "2600", .tag = "A2600"},
    {.alias = "A2600", .tag = "A2600"},
    {.alias = "COLECOVISION", .tag = "COLECO"},
    {.alias = "GEN", .tag = "MD"},
    {.alias = "GENESIS", .tag = "MD"},
    {.alias = "NES", .tag = "FC"},
    {.alias = "PICO8", .tag = "P8"},
    {.alias = "MS", .tag = "SMS"},
    {.alias = "PSX", .tag = "PS"},
    {.alias = "SNES", .tag = "SFC"},
    {.alias = "TG16", .tag = "PCE"},
    {.alias = "PROSYSTEM", .tag = "A7800"},
    {.alias = "SEVENTYEIGHTHUNDRED", .tag = "A7800"},
    {.alias = "ATARI7800", .tag = "A7800"},
    /* Emulator/compat variants fold into their base system's card (their core
       becomes a launch choice). Catalog patterns drive this when systems.json
       is present; these keep folder resolution working in fallback mode too. */
    {.alias = "MGBA", .tag = "GBA"},
    {.alias = "SUPA", .tag = "SFC"},
    {.alias = "SGB", .tag = "GB"},
};

static const cs_canonical_alias g_canonical_aliases[] = {
    {.alias_id = "NES", .canonical_id = "FC"},
    {.alias_id = "GEN", .canonical_id = "MD"},
    {.alias_id = "GENESIS", .canonical_id = "MD"},
    {.alias_id = "PSX", .canonical_id = "PS"},
    {.alias_id = "SNES", .canonical_id = "SFC"},
    {.alias_id = "TG16", .canonical_id = "PCE"},
    {.alias_id = "PROSYSTEM", .canonical_id = "SEVENTYEIGHTHUNDRED"},
};

static cs_platform_info g_fallback_platforms[sizeof(g_platform_identities) / sizeof(g_platform_identities[0])];
static int g_fallback_platforms_initialized = 0;

int cs_platform_is_shortcut_directory(const char *name, const char *absolute_path);
int cs_platform_parse_rom_directory(const char *dir_name,
                                    char *system_name,
                                    size_t system_name_size,
                                    char *system_code,
                                    size_t system_code_size);
static int cs_platform_view_matches_value(const cs_modeled_platform *view, const char *value);

static int cs_write_string(char *dst, size_t size, const char *value) {
    int written;

    if (!dst || size == 0 || !value) {
        return -1;
    }

    written = snprintf(dst, size, "%s", value);
    return (written < 0 || (size_t) written >= size) ? -1 : 0;
}

static int cs_platform_component_is_safe(const char *value) {
    size_t i;

    if (!value || value[0] == '\0') {
        return 0;
    }
    if (value[0] == '.') {
        return 0;
    }
    if ((value[0] == '.' && value[1] == '\0')
        || (value[0] == '.' && value[1] == '.' && value[2] == '\0')) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char) value[i];

        if (ch < 0x20 || ch == '/' || ch == '\\') {
            return 0;
        }
    }

    return 1;
}

static int cs_platform_is_directory(const char *path) {
    struct stat st;

    if (!path) {
        return 0;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }

    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int cs_platform_is_regular_file_not_symlink(const char *path) {
    struct stat st;

    if (!path) {
        return 0;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }

    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int cs_platform_name_has_prefix(const char *name, const char *prefix) {
    size_t prefix_len;

    if (!name || !prefix) {
        return 0;
    }

    prefix_len = strlen(prefix);
    return prefix_len > 0 && strncmp(name, prefix, prefix_len) == 0;
}

static const char *cs_platform_canonical_id(const char *id) {
    size_t i;

    if (!id) {
        return "";
    }
    for (i = 0; i < sizeof(g_canonical_aliases) / sizeof(g_canonical_aliases[0]); ++i) {
        if (strcasecmp(g_canonical_aliases[i].alias_id, id) == 0) {
            return g_canonical_aliases[i].canonical_id;
        }
    }
    return id;
}

static const cs_platform_identity *cs_platform_identity_for_catalog_id(const char *catalog_id) {
    size_t i;

    if (!catalog_id) {
        return NULL;
    }
    for (i = 0; i < sizeof(g_platform_identities) / sizeof(g_platform_identities[0]); ++i) {
        if (strcasecmp(g_platform_identities[i].catalog_id, catalog_id) == 0) {
            return &g_platform_identities[i];
        }
    }
    return NULL;
}

static int cs_platform_strip_roms_prefix(const char *rom_root, char *dst, size_t dst_size) {
    const char *source = rom_root ? rom_root : "";

    if (strncasecmp(source, "Roms/", 5) == 0) {
        source += 5;
    }
    return cs_write_string(dst, dst_size, source);
}

static int cs_platform_strip_images_prefix(const char *image_root, char *dst, size_t dst_size) {
    const char *source = image_root ? image_root : "";

    if (strncasecmp(source, "Images/", 7) == 0) {
        source += 7;
    }
    return cs_write_string(dst, dst_size, source);
}

static int cs_platform_add_unique_string(char items[][256], size_t *count, size_t capacity, const char *value) {
    size_t i;

    if (!items || !count || !value || value[0] == '\0') {
        return 0;
    }
    for (i = 0; i < *count; ++i) {
        if (strcasecmp(items[i], value) == 0) {
            return 0;
        }
    }
    if (*count >= capacity) {
        return -1;
    }
    if (cs_write_string(items[*count], 256, value) != 0) {
        return -1;
    }
    *count += 1;
    return 0;
}

static int cs_platform_add_unique_core(cs_modeled_platform *view, const char *value) {
    size_t i;

    if (!view || !value || value[0] == '\0') {
        return 0;
    }
    for (i = 0; i < view->candidate_core_count; ++i) {
        if (strcasecmp(view->candidate_cores[i], value) == 0) {
            return 0;
        }
    }
    if (view->candidate_core_count >= CS_MODELED_CORE_MAX) {
        return -1;
    }
    if (cs_write_string(view->candidate_cores[view->candidate_core_count],
                        sizeof(view->candidate_cores[view->candidate_core_count]),
                        value)
        != 0) {
        return -1;
    }
    view->candidate_core_count += 1;
    return 0;
}

static int cs_platform_add_pattern(cs_modeled_platform *view, const char *value) {
    return cs_platform_add_unique_string(view->patterns,
                                         &view->pattern_count,
                                         sizeof(view->patterns) / sizeof(view->patterns[0]),
                                         value);
}

static int cs_platform_add_rom_directory(cs_modeled_platform *view, const char *value) {
    return cs_platform_add_unique_string(view->rom_directories,
                                         &view->rom_directory_count,
                                         sizeof(view->rom_directories) / sizeof(view->rom_directories[0]),
                                         value);
}

static int cs_platform_init_view_from_catalog(cs_modeled_platform *view,
                                              const char *canonical_id,
                                              const cs_catalog_system *system) {
    const cs_platform_identity *identity = cs_platform_identity_for_catalog_id(canonical_id);
    char rom_directory[256];
    char image_directory[256];

    if (!view || !canonical_id || !system) {
        return -1;
    }
    memset(view, 0, sizeof(*view));
    if (cs_platform_strip_roms_prefix(system->rom_root, rom_directory, sizeof(rom_directory)) != 0) {
        return -1;
    }
    if (system->image_root && system->image_root[0] != '\0') {
        if (cs_platform_strip_images_prefix(system->image_root, image_directory, sizeof(image_directory)) != 0) {
            return -1;
        }
    } else if (cs_write_string(image_directory, sizeof(image_directory), rom_directory) != 0) {
        return -1;
    }

    if (cs_write_string(view->catalog_id, sizeof(view->catalog_id), canonical_id) != 0
        || cs_write_string(view->info.tag,
                           sizeof(view->info.tag),
                           identity ? identity->tag : canonical_id)
               != 0
        || cs_write_string(view->info.name,
                           sizeof(view->info.name),
                           identity ? identity->fallback_name
                                    : (system->name && system->name[0] ? system->name : canonical_id))
               != 0
        || cs_write_string(view->info.group,
                           sizeof(view->info.group),
                           identity ? identity->group : "Other")
               != 0
        || cs_write_string(view->info.icon,
                           sizeof(view->info.icon),
                           identity ? identity->icon : "UNKNOWN")
               != 0
        || cs_write_string(view->info.primary_code,
                           sizeof(view->info.primary_code),
                           identity ? identity->primary_code : canonical_id)
               != 0
        || cs_write_string(view->info.rom_directory, sizeof(view->info.rom_directory), rom_directory) != 0
        || cs_write_string(view->info.canonical_rom_directory,
                           sizeof(view->info.canonical_rom_directory), rom_directory) != 0
        || cs_write_string(view->info.canonical_image_directory,
                           sizeof(view->info.canonical_image_directory),
                           image_directory)
               != 0) {
        return -1;
    }
    return 0;
}

static int cs_platform_find_view_by_catalog_id(cs_modeled_platform *views, size_t count, const char *catalog_id) {
    size_t i;

    if (!views || !catalog_id) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (strcasecmp(views[i].catalog_id, catalog_id) == 0) {
            return (int) i;
        }
    }
    return -1;
}

static int cs_platform_fold_system_into_view(cs_modeled_platform *view, const cs_catalog_system *system) {
    char rom_directory[256];
    size_t i;

    if (!view || !system) {
        return -1;
    }
    if (cs_platform_strip_roms_prefix(system->rom_root, rom_directory, sizeof(rom_directory)) != 0) {
        return -1;
    }
    if (cs_platform_add_pattern(view, system->id) != 0
        || cs_platform_add_rom_directory(view, rom_directory) != 0
        || cs_platform_add_pattern(view, rom_directory) != 0
        || cs_platform_add_unique_core(view, system->default_core) != 0) {
        return -1;
    }
    for (i = 0; i < system->patterns.count; ++i) {
        if (cs_platform_add_pattern(view, system->patterns.items[i]) != 0) {
            return -1;
        }
    }
    for (i = 0; i < system->alternate_cores.count; ++i) {
        if (cs_platform_add_unique_core(view, system->alternate_cores.items[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int cs_platform_build_modeled_views(const cs_catalog *catalog,
                                           cs_modeled_platform *views,
                                           size_t capacity,
                                           size_t *count_out) {
    size_t count = 0;
    size_t i;

    if (count_out) {
        *count_out = 0;
    }
    if (!catalog || !views || capacity == 0) {
        return -1;
    }

    for (i = 0; i < catalog->system_count; ++i) {
        const cs_catalog_system *system = &catalog->systems[i];
        const char *canonical_id = cs_platform_canonical_id(system->id);

        if (strcasecmp(canonical_id, system->id) != 0) {
            continue;
        }
        if (cs_platform_find_view_by_catalog_id(views, count, canonical_id) >= 0) {
            continue;
        }
        if (count >= capacity || cs_platform_init_view_from_catalog(&views[count], canonical_id, system) != 0) {
            return -1;
        }
        count += 1;
    }

    for (i = 0; i < catalog->system_count; ++i) {
        const cs_catalog_system *system = &catalog->systems[i];
        const char *canonical_id = cs_platform_canonical_id(system->id);
        int view_index = cs_platform_find_view_by_catalog_id(views, count, canonical_id);

        if (view_index < 0) {
            if (count >= capacity || cs_platform_init_view_from_catalog(&views[count], canonical_id, system) != 0) {
                return -1;
            }
            view_index = (int) count;
            count += 1;
        }
        if (cs_platform_fold_system_into_view(&views[view_index], system) != 0) {
            return -1;
        }
    }

    if (count_out) {
        *count_out = count;
    }
    return 0;
}

static int cs_platform_join_path(char *dst, size_t dst_size, const char *left, const char *right) {
    int written;

    if (!dst || dst_size == 0 || !left || !right) {
        return -1;
    }
    written = snprintf(dst, dst_size, "%s/%s", left, right);
    return (written < 0 || (size_t) written >= dst_size) ? -1 : 0;
}

static int cs_platform_modeled_rom_directory_exists(const cs_paths *paths, const cs_modeled_platform *view) {
    size_t source_index;
    size_t dir_index;

    if (!paths || !view) {
        return 0;
    }
    for (source_index = 0; source_index < paths->source_count; ++source_index) {
        for (dir_index = 0; dir_index < view->rom_directory_count; ++dir_index) {
            char path[CS_PATH_MAX];

            if (cs_platform_join_path(path,
                                      sizeof(path),
                                      paths->sources[source_index].roms_root,
                                      view->rom_directories[dir_index])
                    == 0
                && cs_platform_is_directory(path)) {
                return 1;
            }
        }
        {
            DIR *dir = opendir(paths->sources[source_index].roms_root);
            struct dirent *entry;

            if (!dir) {
                continue;
            }
            while ((entry = readdir(dir)) != NULL) {
                char absolute_path[CS_PATH_MAX];
                char system_name[128];
                char system_code[CS_PLATFORM_CODE_MAX];

                if (entry->d_name[0] == '.') {
                    continue;
                }
                if (cs_platform_join_path(absolute_path,
                                          sizeof(absolute_path),
                                          paths->sources[source_index].roms_root,
                                          entry->d_name)
                        != 0
                    || !cs_platform_is_directory(absolute_path)
                    || cs_platform_is_shortcut_directory(entry->d_name, absolute_path)) {
                    continue;
                }
                if (cs_platform_view_matches_value(view, entry->d_name)) {
                    closedir(dir);
                    return 1;
                }
                if (cs_platform_parse_rom_directory(entry->d_name,
                                                    system_name,
                                                    sizeof(system_name),
                                                    system_code,
                                                    sizeof(system_code))
                        == 0
                    && cs_platform_view_matches_value(view, system_code)) {
                    closedir(dir);
                    return 1;
                }
            }
            closedir(dir);
        }
    }
    return 0;
}

static int cs_platform_core_present(const cs_paths *paths,
                                    const cs_catalog *catalog,
                                    const cs_modeled_platform *view,
                                    const char *core_id) {
    const cs_catalog_core *core;
    char path[CS_PATH_MAX];

    if (!paths || !catalog || !core_id || core_id[0] == '\0') {
        return 0;
    }
    core = cs_catalog_find_core(catalog, core_id);
    if (!core) {
        return 0;
    }
    if (strcasecmp(core->type, "path") == 0) {
        if (strcasecmp(core->id, "ports") == 0) {
            return cs_platform_modeled_rom_directory_exists(paths, view);
        }
        if (!core->path || core->path[0] == '\0') {
            return 0;
        }
        if (core->path[0] == '/') {
            return cs_platform_is_regular_file_not_symlink(core->path);
        }
        if (cs_platform_join_path(path, sizeof(path), paths->system_root, core->path) != 0) {
            return 0;
        }
        return cs_platform_is_regular_file_not_symlink(path);
    }
    if (!core->file_name || core->file_name[0] == '\0') {
        return 0;
    }
    if (cs_platform_join_path(path, sizeof(path), paths->cores_root, core->file_name) != 0) {
        return 0;
    }
    return cs_platform_is_regular_file_not_symlink(path);
}

static int cs_platform_view_visible(const cs_paths *paths, const cs_catalog *catalog, const cs_modeled_platform *view) {
    size_t i;

    if (!paths || !catalog || !view) {
        return 0;
    }
    for (i = 0; i < view->candidate_core_count; ++i) {
        if (cs_platform_core_present(paths, catalog, view, view->candidate_cores[i])) {
            return 1;
        }
    }
    return 0;
}

static int cs_platform_view_matches_value(const cs_modeled_platform *view, const char *value) {
    size_t i;

    if (!view || !value || value[0] == '\0') {
        return 0;
    }
    if (strcasecmp(view->catalog_id, value) == 0
        || strcasecmp(view->info.tag, value) == 0
        || strcasecmp(view->info.primary_code, value) == 0) {
        return 1;
    }
    for (i = 0; i < view->pattern_count; ++i) {
        if (strcasecmp(view->patterns[i], value) == 0) {
            return 1;
        }
    }
    for (i = 0; i < view->rom_directory_count; ++i) {
        if (strcasecmp(view->rom_directories[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cs_platform_find_view_for_discovered_dir(const cs_modeled_platform *views,
                                                    size_t view_count,
                                                    const cs_discovered_rom_dir *dir) {
    size_t i;

    if (!views || !dir) {
        return -1;
    }
    for (i = 0; i < view_count; ++i) {
        if (cs_platform_view_matches_value(&views[i], dir->system_code)
            || cs_platform_view_matches_value(&views[i], dir->dir_name)) {
            return (int) i;
        }
    }
    return -1;
}

static int cs_platform_build_visible_views(const cs_paths *paths,
                                           const cs_catalog *catalog,
                                           cs_modeled_platform *visible,
                                           size_t capacity,
                                           size_t *count_out) {
    cs_modeled_platform *modeled;
    size_t modeled_count = 0;
    size_t count = 0;
    size_t i;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !catalog || !visible || capacity == 0) {
        return -1;
    }
    modeled = (cs_modeled_platform *) calloc(CS_MODELED_PLATFORM_MAX, sizeof(modeled[0]));
    if (!modeled) {
        return -1;
    }
    if (cs_platform_build_modeled_views(catalog, modeled, CS_MODELED_PLATFORM_MAX, &modeled_count) != 0) {
        free(modeled);
        return -1;
    }
    for (i = 0; i < modeled_count && count < capacity; ++i) {
        if (!cs_platform_view_visible(paths, catalog, &modeled[i])) {
            continue;
        }
        visible[count++] = modeled[i];
    }
    free(modeled);
    if (count_out) {
        *count_out = count;
    }
    return 0;
}

static int cs_platform_is_ports(const cs_platform_info *platform) {
    return platform && strcmp(platform->tag, "PORTS") == 0;
}

static int cs_platform_build_is_leaf(void) {
    return 1;
}

static int cs_platform_find_discovered_by_code(const cs_discovered_rom_dir *dirs,
                                               size_t dir_count,
                                               const char *system_code) {
    size_t i;

    if (!dirs || !system_code) {
        return -1;
    }

    for (i = 0; i < dir_count; ++i) {
        if (strcasecmp(dirs[i].system_code, system_code) == 0) {
            return (int) i;
        }
    }

    return -1;
}

static int cs_platform_scan_rom_dirs(const cs_paths *paths,
                                     cs_discovered_rom_dir *dirs,
                                     size_t capacity,
                                     size_t *count_out) {
    size_t count = 0;
    size_t source_index;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !dirs || capacity == 0) {
        return -1;
    }

    for (source_index = 0; source_index < paths->source_count; ++source_index) {
        DIR *dir = opendir(paths->sources[source_index].roms_root);
        struct dirent *entry;

        if (!dir) {
            continue;
        }

        while ((entry = readdir(dir)) != NULL) {
            char system_name[sizeof(dirs[0].system_name)];
            char system_code[sizeof(dirs[0].system_code)];
            char absolute_path[CS_PATH_MAX];

            if (entry->d_name[0] == '.') {
                continue;
            }
            if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", paths->sources[source_index].roms_root, entry->d_name) < 0) {
                continue;
            }
            if (!cs_platform_is_directory(absolute_path)) {
                continue;
            }
            if (cs_platform_is_shortcut_directory(entry->d_name, absolute_path)) {
                continue;
            }
            if (cs_platform_parse_rom_directory(entry->d_name,
                                                system_name,
                                                sizeof(system_name),
                                                system_code,
                                                sizeof(system_code))
                != 0) {
                if (!cs_platform_build_is_leaf() || !cs_platform_component_is_safe(entry->d_name)) {
                    continue;
                }
                if (cs_write_string(system_name, sizeof(system_name), entry->d_name) != 0
                    || cs_write_string(system_code, sizeof(system_code), entry->d_name) != 0) {
                    closedir(dir);
                    return -1;
                }
            }
            if (cs_platform_find_discovered_by_code(dirs, count, system_code) >= 0) {
                continue;
            }
            if (count >= capacity) {
                break;
            }

            if (cs_write_string(dirs[count].system_name, sizeof(dirs[count].system_name), system_name) != 0
                || cs_write_string(dirs[count].system_code, sizeof(dirs[count].system_code), system_code) != 0
                || cs_write_string(dirs[count].dir_name, sizeof(dirs[count].dir_name), entry->d_name) != 0) {
                closedir(dir);
                return -1;
            }

            count += 1;
        }

        closedir(dir);
    }

    if (count_out) {
        *count_out = count;
    }
    return 0;
}

static int cs_platform_emulator_code_present(const char *code,
                                             const char codes[][CS_PLATFORM_CODE_MAX],
                                             size_t code_count) {
    size_t i;

    if (!code || !codes) {
        return 0;
    }
    for (i = 0; i < code_count; ++i) {
        if (strcasecmp(code, codes[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cs_platform_build_custom(const cs_discovered_rom_dir *dir, cs_platform_info *target) {
    if (!dir || !target) {
        return -1;
    }
    memset(target, 0, sizeof(*target));
    if (cs_write_string(target->tag, sizeof(target->tag), dir->system_code) != 0
        || cs_write_string(target->name, sizeof(target->name), dir->system_name) != 0
        || cs_write_string(target->group, sizeof(target->group), "Custom") != 0
        || cs_write_string(target->icon, sizeof(target->icon), dir->system_code) != 0
        || cs_write_string(target->primary_code, sizeof(target->primary_code), dir->system_code) != 0
        || cs_write_string(target->rom_directory, sizeof(target->rom_directory), dir->dir_name) != 0
        || cs_write_string(target->canonical_image_directory,
                           sizeof(target->canonical_image_directory),
                           dir->system_code)
               != 0) {
        return -1;
    }
    target->is_custom = 1;
    return 0;
}

static int cs_platform_compare_by_name(const void *a, const void *b) {
    const cs_platform_info *left = (const cs_platform_info *) a;
    const cs_platform_info *right = (const cs_platform_info *) b;

    return strcmp(left->name, right->name);
}

static void cs_platform_init_fallbacks(void) {
    size_t i;

    if (g_fallback_platforms_initialized) {
        return;
    }
    for (i = 0; i < sizeof(g_platform_identities) / sizeof(g_platform_identities[0]); ++i) {
        const cs_platform_identity *identity = &g_platform_identities[i];

        memset(&g_fallback_platforms[i], 0, sizeof(g_fallback_platforms[i]));
        (void) cs_write_string(g_fallback_platforms[i].tag, sizeof(g_fallback_platforms[i].tag), identity->tag);
        (void) cs_write_string(g_fallback_platforms[i].name, sizeof(g_fallback_platforms[i].name), identity->fallback_name);
        (void) cs_write_string(g_fallback_platforms[i].group, sizeof(g_fallback_platforms[i].group), identity->group);
        (void) cs_write_string(g_fallback_platforms[i].icon, sizeof(g_fallback_platforms[i].icon), identity->icon);
        (void) cs_write_string(g_fallback_platforms[i].primary_code,
                               sizeof(g_fallback_platforms[i].primary_code),
                               identity->primary_code);
        (void) cs_write_string(g_fallback_platforms[i].rom_directory,
                               sizeof(g_fallback_platforms[i].rom_directory),
                               identity->catalog_id);
        (void) cs_write_string(g_fallback_platforms[i].canonical_image_directory,
                               sizeof(g_fallback_platforms[i].canonical_image_directory),
                               identity->primary_code);
    }
    g_fallback_platforms_initialized = 1;
}

static const cs_platform_info *cs_platform_find_fallback_by_tag(const char *tag) {
    size_t i;

    if (!tag || tag[0] == '\0') {
        return NULL;
    }
    cs_platform_init_fallbacks();
    for (i = 0; i < sizeof(g_fallback_platforms) / sizeof(g_fallback_platforms[0]); ++i) {
        if (strcasecmp(g_fallback_platforms[i].tag, tag) == 0
            || strcasecmp(g_fallback_platforms[i].primary_code, tag) == 0
            || strcasecmp(g_platform_identities[i].catalog_id, tag) == 0) {
            return &g_fallback_platforms[i];
        }
    }
    for (i = 0; i < sizeof(g_static_aliases) / sizeof(g_static_aliases[0]); ++i) {
        if (strcasecmp(g_static_aliases[i].alias, tag) == 0) {
            return cs_platform_find_fallback_by_tag(g_static_aliases[i].tag);
        }
    }
    return NULL;
}

static int cs_platform_collect_core_codes_from_dir(const char *dir_path,
                                                   char codes[][CS_PLATFORM_CODE_MAX],
                                                   size_t *count,
                                                   size_t capacity) {
    DIR *dir;
    struct dirent *entry;

    if (!dir_path || !codes || !count) {
        return -1;
    }

    dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char code[CS_PLATFORM_CODE_MAX];
        char absolute_path[CS_PATH_MAX];
        size_t suffix_len = strlen("_libretro.so");
        size_t name_len;
        size_t code_len;
        size_t i;

        if (entry->d_name[0] == '.') {
            continue;
        }
        name_len = strlen(entry->d_name);
        if (name_len <= suffix_len
            || strcmp(entry->d_name + name_len - suffix_len, "_libretro.so") != 0) {
            continue;
        }
        code_len = name_len - suffix_len;
        if (code_len == 0 || code_len >= sizeof(code)) {
            continue;
        }
        if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", dir_path, entry->d_name) < 0
            || !cs_platform_is_regular_file_not_symlink(absolute_path)) {
            continue;
        }
        for (i = 0; i < code_len; ++i) {
            unsigned char ch = (unsigned char) entry->d_name[i];

            if (ch >= 'a' && ch <= 'z') {
                ch = (unsigned char) (ch - 'a' + 'A');
            }
            code[i] = (char) ch;
        }
        code[code_len] = '\0';
        if (!cs_platform_component_is_safe(code)) {
            continue;
        }
        if (cs_platform_emulator_code_present(code, (const char (*)[CS_PLATFORM_CODE_MAX]) codes, *count)) {
            continue;
        }
        if (*count >= capacity || cs_write_string(codes[*count], CS_PLATFORM_CODE_MAX, code) != 0) {
            closedir(dir);
            return -1;
        }
        *count += 1;
    }

    closedir(dir);
    return 0;
}

static int cs_platform_runtime_views(const cs_paths *paths,
                                     cs_modeled_platform *views,
                                     size_t capacity,
                                     size_t *count_out,
                                     cs_catalog_error *error_out) {
    cs_catalog catalog = {0};
    int status;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !views || capacity == 0) {
        return -1;
    }
    status = cs_catalog_load(paths->systems_catalog_path, paths->cores_catalog_path, &catalog, error_out);
    if (status != 0) {
        return -1;
    }
    status = cs_platform_build_visible_views(paths, &catalog, views, capacity, count_out);
    cs_catalog_free(&catalog);
    return status;
}

static int cs_platform_discover_internal(const cs_paths *paths,
                                         cs_platform_info *platforms,
                                         size_t capacity,
                                         size_t *count_out,
                                         cs_catalog_error *error_out) {
    cs_discovered_rom_dir *dirs = NULL;
    cs_modeled_platform *views = NULL;
    char (*emulator_codes)[CS_PLATFORM_CODE_MAX] = NULL;
    size_t dir_count = 0;
    size_t view_count = 0;
    size_t emulator_code_count = 0;
    size_t count = 0;
    size_t custom_start;
    size_t i;
    int status = -1;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !platforms || capacity == 0) {
        return -1;
    }
    dirs = (cs_discovered_rom_dir *) calloc(CS_DISCOVERED_PLATFORM_MAX, sizeof(dirs[0]));
    views = (cs_modeled_platform *) calloc(CS_MODELED_PLATFORM_MAX, sizeof(views[0]));
    emulator_codes = (char (*)[CS_PLATFORM_CODE_MAX]) calloc(CS_DISCOVERED_PLATFORM_MAX, sizeof(emulator_codes[0]));
    if (!dirs || !views || !emulator_codes) {
        goto cleanup;
    }
    if (cs_platform_runtime_views(paths, views, CS_MODELED_PLATFORM_MAX, &view_count, error_out) != 0) {
        goto cleanup;
    }
    if (cs_platform_scan_rom_dirs(paths, dirs, CS_DISCOVERED_PLATFORM_MAX, &dir_count) != 0) {
        goto cleanup;
    }

    for (i = 0; i < view_count && count < capacity; ++i) {
        int discovered_index = -1;

        platforms[count] = views[i].info;
        {
            size_t dir_index;

            for (dir_index = 0; dir_index < dir_count; ++dir_index) {
                if (cs_platform_view_matches_value(&views[i], dirs[dir_index].system_code)
                    || cs_platform_view_matches_value(&views[i], dirs[dir_index].dir_name)) {
                    discovered_index = (int) dir_index;
                    break;
                }
            }
        }
        if (discovered_index >= 0
            && cs_write_string(platforms[count].rom_directory,
                               sizeof(platforms[count].rom_directory),
                               dirs[discovered_index].dir_name)
                   != 0) {
            goto cleanup;
        }
        count += 1;
    }

    custom_start = count;
    if (cs_platform_collect_installed_emulators(paths,
                                                emulator_codes,
                                                CS_DISCOVERED_PLATFORM_MAX,
                                                &emulator_code_count)
        != 0) {
        emulator_code_count = 0;
    }

    for (i = 0; i < dir_count && count < capacity; ++i) {
        cs_platform_info entry;

        if (cs_platform_find_view_for_discovered_dir(views, view_count, &dirs[i]) >= 0) {
            continue;
        }
        if (!cs_platform_emulator_code_present(dirs[i].system_code,
                                               (const char (*)[CS_PLATFORM_CODE_MAX]) emulator_codes,
                                               emulator_code_count)) {
            continue;
        }
        if (cs_platform_build_custom(&dirs[i], &entry) != 0) {
            goto cleanup;
        }
        platforms[count++] = entry;
    }

    if (count > custom_start + 1) {
        qsort(platforms + custom_start,
              count - custom_start,
              sizeof(platforms[0]),
              cs_platform_compare_by_name);
    }

    if (count_out) {
        *count_out = count;
    }
    status = 0;

cleanup:
    free(emulator_codes);
    free(views);
    free(dirs);
    return status;
}

const cs_platform_info *cs_platform_find(const char *tag) {
    return cs_platform_find_fallback_by_tag(tag);
}

int cs_platform_copy(const cs_platform_info *source, cs_platform_info *target) {
    if (!source || !target) {
        return -1;
    }

    *target = *source;
    return 0;
}

int cs_platform_resolve(const cs_paths *paths, const char *tag, cs_platform_info *target) {
    cs_platform_info *platforms = NULL;
    const cs_platform_info *fallback = NULL;
    const char *resolved_tag = tag;
    size_t platform_count = 0;
    size_t i;
    int status = -1;

    if (!tag || !target) {
        return -1;
    }
    if (!paths) {
        fallback = cs_platform_find(tag);
        return fallback ? cs_platform_copy(fallback, target) : -1;
    }
    fallback = cs_platform_find(tag);
    if (fallback) {
        resolved_tag = fallback->tag;
    }
    platforms = (cs_platform_info *) calloc(CS_MODELED_PLATFORM_MAX, sizeof(platforms[0]));
    if (!platforms) {
        return -1;
    }

    if (cs_platform_discover_internal(paths,
                                      platforms,
                                      CS_MODELED_PLATFORM_MAX,
                                      &platform_count,
                                      NULL)
        != 0) {
        goto cleanup;
    }
    for (i = 0; i < platform_count; ++i) {
        if (strcasecmp(platforms[i].tag, resolved_tag) == 0
            || strcasecmp(platforms[i].tag, tag) == 0
            || strcasecmp(platforms[i].primary_code, tag) == 0
            || strcasecmp(platforms[i].rom_directory, tag) == 0) {
            *target = platforms[i];
            status = 0;
            goto cleanup;
        }
    }

cleanup:
    free(platforms);
    return status;
}

int cs_platform_discover_with_error(const cs_paths *paths,
                                    cs_platform_info *platforms,
                                    size_t capacity,
                                    size_t *count_out,
                                    cs_catalog_error *error_out) {
    return cs_platform_discover_internal(paths, platforms, capacity, count_out, error_out);
}

int cs_platform_discover(const cs_paths *paths,
                         cs_platform_info *platforms,
                         size_t capacity,
                         size_t *count_out) {
    return cs_platform_discover_internal(paths, platforms, capacity, count_out, NULL);
}

int cs_platform_parse_rom_directory(const char *dir_name,
                                    char *system_name,
                                    size_t system_name_size,
                                    char *system_code,
                                    size_t system_code_size) {
    const char *last_open;
    const char *last_close;
    size_t name_len;
    size_t code_len;

    if (!dir_name || !system_name || system_name_size == 0 || !system_code || system_code_size == 0) {
        return -1;
    }

    last_open = strrchr(dir_name, '(');
    if (!last_open || last_open == dir_name || last_open == dir_name + 1 || *(last_open - 1) != ' ') {
        return -1;
    }

    last_close = strrchr(dir_name, ')');
    if (!last_close || last_close < last_open || last_close[1] != '\0') {
        return -1;
    }

    name_len = (size_t) ((last_open - 1) - dir_name);
    code_len = (size_t) (last_close - (last_open + 1));
    if (name_len == 0 || code_len == 0 || name_len >= system_name_size || code_len >= system_code_size) {
        return -1;
    }

    memcpy(system_name, dir_name, name_len);
    system_name[name_len] = '\0';
    memcpy(system_code, last_open + 1, code_len);
    system_code[code_len] = '\0';
    if (!cs_platform_component_is_safe(system_code)) {
        return -1;
    }
    return 0;
}

int cs_platform_supports_resource(const cs_platform_info *platform, const char *resource) {
    if (!platform || !resource || resource[0] == '\0') {
        return 0;
    }

    if (strcmp(resource, "roms") == 0) {
        return 1;
    }
    if (cs_platform_is_ports(platform)) {
        return 0;
    }
    if (cs_platform_build_is_leaf() && strcmp(resource, "overlays") == 0) {
        return 0;
    }

    return strcmp(resource, "saves") == 0 || strcmp(resource, "states") == 0 || strcmp(resource, "bios") == 0
           || strcmp(resource, "overlays") == 0 || strcmp(resource, "cheats") == 0;
}

int cs_platform_allows_hidden_rom_entries(const cs_platform_info *platform) {
    return cs_platform_is_ports(platform);
}

int cs_platform_is_shortcut_directory(const char *name, const char *absolute_path) {
    char marker_path[CS_PATH_MAX];
    const char *basename = name;
    int written;

    if ((!basename || basename[0] == '\0') && absolute_path) {
        basename = strrchr(absolute_path, '/');
        basename = basename ? basename + 1 : absolute_path;
    }

    if (basename && (cs_platform_name_has_prefix(basename, CS_SHORTCUT_PREFIX)
                     || cs_platform_name_has_prefix(basename, CS_LEGACY_SHORTCUT_PREFIX))) {
        return 1;
    }
    if (!absolute_path || absolute_path[0] == '\0') {
        return 0;
    }

    written = snprintf(marker_path, sizeof(marker_path), "%s/%s", absolute_path, CS_SHORTCUT_MARKER);
    if (written < 0 || (size_t) written >= sizeof(marker_path)) {
        return 0;
    }

    return cs_platform_is_regular_file_not_symlink(marker_path);
}

int cs_platform_collect_installed_emulators(const cs_paths *paths,
                                            char codes[][CS_PLATFORM_CODE_MAX],
                                            size_t capacity,
                                            size_t *count_out) {
    size_t count = 0;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !codes || capacity == 0) {
        return -1;
    }

    if (cs_platform_collect_core_codes_from_dir(paths->cores_root, codes, &count, capacity) != 0) {
        return -1;
    }

    if (count_out) {
        *count_out = count;
    }
    return 0;
}
