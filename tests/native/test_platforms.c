#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_catalog.h"
#include "cs_library.h"
#include "cs_paths.h"
#include "cs_platforms.h"

static void make_dir(const char *path) {
    char tmp[PATH_MAX];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    assert(mkdir(path, 0700) == 0 || access(path, F_OK) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void path_join(char *dst, size_t dst_size, const char *left, const char *right) {
    assert(snprintf(dst, dst_size, "%s/%s", left, right) > 0);
}

static void set_sdcard_root_realpath(const char *root) {
    char resolved[PATH_MAX];

    assert(realpath(root, resolved) != NULL);
    assert(setenv("SDCARD_PATH", resolved, 1) == 0);
    unsetenv("SDCARD_PATHS");
    unsetenv("SYSTEMS_CATALOG_PATH");
    unsetenv("CORES_CATALOG_PATH");
    unsetenv("CORES_PATH");
    unsetenv("INFO_PATH");
    unsetenv("CS_WEB_ROOT");
}

static void set_sdcard_roots_realpath(const char *first, const char *second) {
    char first_resolved[PATH_MAX];
    char second_resolved[PATH_MAX];
    char joined[(PATH_MAX * 2) + 2];

    assert(realpath(first, first_resolved) != NULL);
    assert(realpath(second, second_resolved) != NULL);
    assert(snprintf(joined, sizeof(joined), "%s:%s", first_resolved, second_resolved) > 0);
    assert(setenv("SDCARD_PATHS", joined, 1) == 0);
    unsetenv("SDCARD_PATH");
    unsetenv("SYSTEMS_CATALOG_PATH");
    unsetenv("CORES_CATALOG_PATH");
    unsetenv("CORES_PATH");
    unsetenv("INFO_PATH");
    unsetenv("CS_WEB_ROOT");
}

static const cs_platform_info *find_platform_entry(const cs_platform_info *platforms,
                                                   size_t count,
                                                   const char *tag) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (strcmp(platforms[i].tag, tag) == 0) {
            return &platforms[i];
        }
    }
    return NULL;
}

static void write_catalogs(const char *root) {
    char defaults_dir[PATH_MAX];
    char systems_path[PATH_MAX];
    char cores_path[PATH_MAX];
    char cores_dir[PATH_MAX];
    char info_dir[PATH_MAX];
    char roms_dir[PATH_MAX];

    path_join(defaults_dir, sizeof(defaults_dir), root, ".system/leaf/platforms/mlp1/defaults");
    path_join(cores_dir, sizeof(cores_dir), root, ".system/leaf/platforms/mlp1/cores");
    path_join(info_dir, sizeof(info_dir), root, ".system/leaf/platforms/mlp1/info");
    path_join(roms_dir, sizeof(roms_dir), root, "Roms");
    make_dir(defaults_dir);
    make_dir(cores_dir);
    make_dir(info_dir);
    make_dir(roms_dir);
    path_join(systems_path, sizeof(systems_path), defaults_dir, "systems.json");
    path_join(cores_path, sizeof(cores_path), defaults_dir, "cores.json");

    write_file(systems_path,
               "{"
               "\"version\":1,"
               "\"systems\":["
               "{\"id\":\"ARCADE\",\"name\":\"Arcade\",\"patterns\":[\"ARCADE\"],\"extensions\":[],\"default_core\":\"fbneo\",\"alternate_cores\":[],\"rom_root\":\"Roms/ARCADE\"},"
               "{\"id\":\"FC\",\"name\":\"NES\",\"patterns\":[\"FC\",\"NES\",\"FAMICOM\"],\"extensions\":[],\"default_core\":\"fceumm\",\"alternate_cores\":[],\"rom_root\":\"Roms/FC\"},"
               "{\"id\":\"NES\",\"name\":\"NES\",\"patterns\":[\"NES\",\"FC\"],\"extensions\":[],\"default_core\":\"fceumm\",\"alternate_cores\":[],\"rom_root\":\"Roms/NES\"},"
               "{\"id\":\"GB\",\"name\":\"GB\",\"patterns\":[\"GB\",\"DMG\",\"SGB\"],\"extensions\":[],\"default_core\":\"gambatte\",\"alternate_cores\":[\"mgba\"],\"rom_root\":\"Roms/GB\"},"
               "{\"id\":\"GBA\",\"name\":\"GBA\",\"patterns\":[\"GBA\",\"GAMEBOYADVANCE\",\"MGBA\"],\"extensions\":[],\"default_core\":\"mgba\",\"alternate_cores\":[\"gpsp\"],\"rom_root\":\"Roms/GBA\"},"
               "{\"id\":\"GBC\",\"name\":\"GBC\",\"patterns\":[\"GBC\",\"CGB\"],\"extensions\":[],\"default_core\":\"gambatte\",\"alternate_cores\":[\"mgba\"],\"rom_root\":\"Roms/GBC\"},"
               "{\"id\":\"MD\",\"name\":\"Genesis\",\"patterns\":[\"MD\",\"GENESIS\"],\"extensions\":[],\"default_core\":\"genesis_plus_gx\",\"alternate_cores\":[],\"rom_root\":\"Roms/MD\"},"
               "{\"id\":\"GEN\",\"name\":\"GEN\",\"patterns\":[\"GEN\"],\"extensions\":[],\"default_core\":\"genesis_plus_gx\",\"alternate_cores\":[],\"rom_root\":\"Roms/GEN\"},"
               "{\"id\":\"GENESIS\",\"name\":\"Genesis\",\"patterns\":[\"GENESIS\",\"MEGADRIVE\"],\"extensions\":[],\"default_core\":\"genesis_plus_gx\",\"alternate_cores\":[],\"rom_root\":\"Roms/GENESIS\"},"
               "{\"id\":\"MS\",\"name\":\"Sega MS\",\"patterns\":[\"MS\",\"SMS\"],\"extensions\":[],\"default_core\":\"genesis_plus_gx\",\"alternate_cores\":[],\"rom_root\":\"Roms/MS\"},"
               "{\"id\":\"N64\",\"name\":\"Nintendo 64\",\"patterns\":[\"N64\"],\"extensions\":[\"n64\",\"v64\",\"z64\"],\"default_core\":\"mupen64plus_standalone\",\"alternate_cores\":[\"mupen64plus_next\"],\"rom_root\":\"Roms/N64\",\"image_root\":\"Images/N64\"},"
               "{\"id\":\"NDS\",\"name\":\"Nintendo DS\",\"patterns\":[\"NDS\"],\"extensions\":[],\"default_core\":\"drastic\",\"alternate_cores\":[],\"rom_root\":\"Roms/NDS\"},"
               "{\"id\":\"PICO8\",\"name\":\"Pico-8\",\"patterns\":[\"PICO8\",\"P8\"],\"extensions\":[],\"default_core\":\"fake08\",\"alternate_cores\":[],\"rom_root\":\"Roms/PICO8\"},"
               "{\"id\":\"PORTS\",\"name\":\"Ports\",\"patterns\":[\"PORTS\"],\"extensions\":[],\"default_core\":\"ports\",\"alternate_cores\":[],\"rom_root\":\"Roms/PORTS\"},"
               "{\"id\":\"PS\",\"name\":\"PSX\",\"patterns\":[\"PS\",\"PSX\"],\"extensions\":[],\"default_core\":\"pcsx_rearmed\",\"alternate_cores\":[],\"rom_root\":\"Roms/PS\"},"
               "{\"id\":\"PSX\",\"name\":\"PSX\",\"patterns\":[\"PSX\",\"PS\"],\"extensions\":[],\"default_core\":\"pcsx_rearmed\",\"alternate_cores\":[],\"rom_root\":\"Roms/PSX\"},"
               "{\"id\":\"PSP\",\"name\":\"PSP\",\"patterns\":[\"PSP\"],\"extensions\":[],\"default_core\":\"ppsspp\",\"alternate_cores\":[],\"rom_root\":\"Roms/PSP\"},"
               "{\"id\":\"SEVENTYEIGHTHUNDRED\",\"name\":\"Atari 7800\",\"patterns\":[\"SEVENTYEIGHTHUNDRED\",\"A7800\"],\"extensions\":[],\"default_core\":\"prosystem\",\"alternate_cores\":[],\"rom_root\":\"Roms/SEVENTYEIGHTHUNDRED\"},"
               "{\"id\":\"PROSYSTEM\",\"name\":\"PROSYSTEM\",\"patterns\":[\"PROSYSTEM\"],\"extensions\":[],\"default_core\":\"prosystem\",\"alternate_cores\":[],\"rom_root\":\"Roms/PROSYSTEM\"},"
               "{\"id\":\"SFC\",\"name\":\"SNES\",\"patterns\":[\"SFC\",\"SNES\",\"SUPA\"],\"extensions\":[],\"default_core\":\"snes9x\",\"alternate_cores\":[],\"rom_root\":\"Roms/SFC\"},"
               "{\"id\":\"SNES\",\"name\":\"SNES\",\"patterns\":[\"SNES\",\"SFC\"],\"extensions\":[],\"default_core\":\"snes9x\",\"alternate_cores\":[],\"rom_root\":\"Roms/SNES\"}"
               "]"
               "}");

    write_file(cores_path,
               "{"
               "\"version\":2,"
               "\"cores\":["
               "{\"id\":\"fbneo\",\"display_name\":\"FBNeo\",\"type\":\"retroarch\",\"file_name\":\"fbneo_libretro.so\",\"info_name\":\"fbneo_libretro.info\",\"path\":null},"
               "{\"id\":\"fceumm\",\"display_name\":\"FCEUmm\",\"type\":\"retroarch\",\"file_name\":\"fceumm_libretro.so\",\"info_name\":\"fceumm_libretro.info\",\"path\":null},"
               "{\"id\":\"fake08\",\"display_name\":\"FAKE-08\",\"type\":\"retroarch\",\"file_name\":\"fake08_libretro.so\",\"info_name\":\"fake08_libretro.info\",\"path\":null},"
               "{\"id\":\"gambatte\",\"display_name\":\"Gambatte\",\"type\":\"retroarch\",\"file_name\":\"gambatte_libretro.so\",\"info_name\":\"gambatte_libretro.info\",\"path\":null},"
               "{\"id\":\"genesis_plus_gx\",\"display_name\":\"Genesis Plus GX\",\"type\":\"retroarch\",\"file_name\":\"genesis_plus_gx_libretro.so\",\"info_name\":\"genesis_plus_gx_libretro.info\",\"path\":null},"
               "{\"id\":\"gpsp\",\"display_name\":\"gpSP\",\"type\":\"retroarch\",\"file_name\":\"gpsp_libretro.so\",\"info_name\":\"gpsp_libretro.info\",\"path\":null},"
               "{\"id\":\"mgba\",\"display_name\":\"mGBA\",\"type\":\"retroarch\",\"file_name\":\"mgba_libretro.so\",\"info_name\":\"mgba_libretro.info\",\"path\":null},"
               "{\"id\":\"pcsx_rearmed\",\"display_name\":\"PCSX ReARMed\",\"type\":\"retroarch\",\"file_name\":\"pcsx_rearmed_libretro.so\",\"info_name\":\"pcsx_rearmed_libretro.info\",\"path\":null},"
               "{\"id\":\"prosystem\",\"display_name\":\"ProSystem\",\"type\":\"retroarch\",\"file_name\":\"prosystem_libretro.so\",\"info_name\":\"prosystem_libretro.info\",\"path\":null},"
               "{\"id\":\"snes9x\",\"display_name\":\"Snes9x\",\"type\":\"retroarch\",\"file_name\":\"snes9x_libretro.so\",\"info_name\":\"snes9x_libretro.info\",\"path\":null},"
               "{\"id\":\"mednafen_supafaust\",\"display_name\":\"Supafaust\",\"type\":\"retroarch\",\"file_name\":\"mednafen_supafaust_libretro.so\",\"info_name\":\"mednafen_supafaust_libretro.info\",\"path\":null},"
               "{\"id\":\"snes9x2010\",\"display_name\":\"Snes9x 2010\",\"type\":\"retroarch\",\"file_name\":\"snes9x2010_libretro.so\",\"info_name\":\"snes9x2010_libretro.info\",\"path\":null},"
               "{\"id\":\"snes9x2005\",\"display_name\":\"Snes9x 2005\",\"type\":\"retroarch\",\"file_name\":\"snes9x2005_libretro.so\",\"info_name\":\"snes9x2005_libretro.info\",\"path\":null},"
               "{\"id\":\"snes9x2005_plus\",\"display_name\":\"Snes9x 2005 Plus\",\"type\":\"retroarch\",\"file_name\":\"snes9x2005_plus_libretro.so\",\"info_name\":\"snes9x2005_plus_libretro.info\",\"path\":null},"
               "{\"id\":\"snes9x2002\",\"display_name\":\"Snes9x 2002\",\"type\":\"retroarch\",\"file_name\":\"snes9x2002_libretro.so\",\"info_name\":\"snes9x2002_libretro.info\",\"path\":null},"
               "{\"id\":\"chimerasnes\",\"display_name\":\"ChimeraSNES\",\"type\":\"retroarch\",\"file_name\":\"chimerasnes_libretro.so\",\"info_name\":\"chimerasnes_libretro.info\",\"path\":null},"
               "{\"id\":\"drastic\",\"display_name\":\"DraStic\",\"type\":\"path\",\"file_name\":null,\"info_name\":null,\"path\":\"emulators/drastic/launch.sh\"},"
               "{\"id\":\"mupen64plus_standalone\",\"display_name\":\"Mupen64Plus Standalone\",\"type\":\"path\",\"file_name\":null,\"info_name\":null,\"path\":\"emulators/mupen64plus/launch.sh\"},"
               "{\"id\":\"ppsspp\",\"display_name\":\"PPSSPP\",\"type\":\"path\",\"file_name\":null,\"info_name\":null,\"path\":\"emulators/ppsspp/launch.sh\"},"
               "{\"id\":\"ports\",\"display_name\":\"Ports\",\"type\":\"path\",\"file_name\":null,\"info_name\":null,\"path\":\"/mnt/SDCARD/Roms/PORTS\"}"
               "]"
               "}");
}

static void write_core(const char *root, const char *file_name) {
    char path[PATH_MAX];

    assert(snprintf(path, sizeof(path), "%s/.system/leaf/platforms/mlp1/cores/%s", root, file_name) > 0);
    write_file(path, "core");
}

static void write_info(const char *root, const char *file_name) {
    char path[PATH_MAX];

    assert(snprintf(path, sizeof(path), "%s/.system/leaf/platforms/mlp1/info/%s", root, file_name) > 0);
    write_file(path, "info");
}

static void write_launcher_file(const char *root, const char *relative) {
    char path[PATH_MAX];
    char dir[PATH_MAX];
    char *slash;

    assert(snprintf(path, sizeof(path), "%s/.system/leaf/platforms/mlp1/%s", root, relative) > 0);
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    assert(slash != NULL);
    *slash = '\0';
    make_dir(dir);
    write_file(path, "#!/bin/sh\n");
}

static void test_path_defaults(void) {
    char template[] = "/tmp/cs-paths-catalog-XXXXXX";
    char *root = mkdtemp(template);
    cs_paths paths = {0};

    assert(root != NULL);
    write_catalogs(root);
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(strstr(paths.systems_catalog_path, ".system/leaf/platforms/mlp1/defaults/systems.json") != NULL);
    assert(strstr(paths.cores_catalog_path, ".system/leaf/platforms/mlp1/defaults/cores.json") != NULL);
}

static void test_static_identity_helpers(void) {
    const cs_platform_info *info;

    info = cs_platform_find("PICO8");
    assert(info != NULL);
    assert(strcmp(info->tag, "P8") == 0);
    info = cs_platform_find("ARCADE");
    assert(info != NULL);
    assert(strcmp(info->tag, "FBN") == 0);
    info = cs_platform_find("SEVENTYEIGHTHUNDRED");
    assert(info != NULL);
    assert(strcmp(info->tag, "A7800") == 0);
}

static void test_visibility_requires_present_libretro_core(void) {
    char template[] = "/tmp/cs-platforms-visible-XXXXXX";
    char *root = mkdtemp(template);
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    size_t count = 0;

    assert(root != NULL);
    write_catalogs(root);
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "GBA") == NULL);

    write_info(root, "mgba_libretro.info");
    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "GBA") == NULL);

    write_core(root, "mgba_libretro.so");
    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "GBA") != NULL);
    /* The mGBA variant folds into the GBA card (its core is a launch choice),
       so it is never advertised as a separate platform. */
    assert(find_platform_entry(platforms, count, "MGBA") == NULL);
}

static void test_canonical_alias_rows_collapse_and_match_folders(void) {
    char template[] = "/tmp/cs-platforms-canonical-XXXXXX";
    char *root = mkdtemp(template);
    char nes_dir[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    cs_platform_info resolved = {0};
    size_t count = 0;
    const cs_platform_info *fc;

    assert(root != NULL);
    write_catalogs(root);
    assert(snprintf(nes_dir, sizeof(nes_dir), "%s/Roms/Nintendo Entertainment System (NES)", root) > 0);
    make_dir(nes_dir);
    write_core(root, "fceumm_libretro.so");
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    fc = find_platform_entry(platforms, count, "FC");
    assert(fc != NULL);
    assert(find_platform_entry(platforms, count, "NES") == NULL);
    assert(strcmp(fc->rom_directory, "Nintendo Entertainment System (NES)") == 0);
    assert(cs_platform_resolve(&paths, "NES", &resolved) == 0);
    assert(strcmp(resolved.tag, "FC") == 0);
    assert(strcmp(resolved.rom_directory, "Nintendo Entertainment System (NES)") == 0);
}

static int path_ends_with(const char *path, const char *suffix) {
    size_t pl = strlen(path);
    size_t sl = strlen(suffix);
    return pl >= sl && strcmp(path + (pl - sl), suffix) == 0;
}

static void write_collapsed_fc_catalog(const char *root) {
    char defaults_dir[PATH_MAX];
    char cores_dir[PATH_MAX];
    char info_dir[PATH_MAX];
    char roms_dir[PATH_MAX];
    char systems_path[PATH_MAX];
    char cores_path[PATH_MAX];

    path_join(defaults_dir, sizeof(defaults_dir), root, ".system/leaf/platforms/mlp1/defaults");
    path_join(cores_dir, sizeof(cores_dir), root, ".system/leaf/platforms/mlp1/cores");
    path_join(info_dir, sizeof(info_dir), root, ".system/leaf/platforms/mlp1/info");
    path_join(roms_dir, sizeof(roms_dir), root, "Roms");
    make_dir(defaults_dir);
    make_dir(cores_dir);
    make_dir(info_dir);
    make_dir(roms_dir);
    path_join(systems_path, sizeof(systems_path), defaults_dir, "systems.json");
    path_join(cores_path, sizeof(cores_path), defaults_dir, "cores.json");

    /* One collapsed FC row whose canonical public folder is Roms/NES, matching
       the real generated catalog. */
    write_file(systems_path,
               "{\"version\":1,\"systems\":["
	       "{\"id\":\"FC\",\"name\":\"Nintendo Entertainment System / Famicom\","
	       "\"patterns\":[\"FC\",\"NES\",\"FAMICOM\"],\"extensions\":[],"
	       "\"default_core\":\"fceumm\",\"alternate_cores\":[],"
	       "\"rom_root\":\"Roms/NES\",\"image_root\":\"Images/NES\"}"
	       "]}");
    write_file(cores_path,
               "{\"version\":2,\"cores\":["
               "{\"id\":\"fceumm\",\"display_name\":\"FCEUmm\",\"type\":\"retroarch\","
               "\"file_name\":\"fceumm_libretro.so\",\"info_name\":\"fceumm_libretro.info\",\"path\":null}"
               "]}");
}

static void test_uploads_target_canonical_folder(void) {
    char template[] = "/tmp/cs-platforms-upload-XXXXXX";
    char *root = mkdtemp(template);
    char fc_dir[PATH_MAX];
    char write_root[PATH_MAX];
    char browse_root[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    size_t count = 0;
    const cs_platform_info *fc;

    assert(root != NULL);
    write_collapsed_fc_catalog(root);
    /* Only a legacy alias folder exists on disk; canonical is Roms/NES. */
    assert(snprintf(fc_dir, sizeof(fc_dir), "%s/Roms/FC", root) > 0);
    make_dir(fc_dir);
    write_core(root, "fceumm_libretro.so");
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    fc = find_platform_entry(platforms, count, "FC");
    assert(fc != NULL);
    /* Browse/display follows the discovered legacy folder... */
    assert(strcmp(fc->rom_directory, "FC") == 0);
    /* ...while the canonical public folder is recorded for new content. */
    assert(strcmp(fc->canonical_rom_directory, "NES") == 0);
    assert(strcmp(fc->canonical_image_directory, "NES") == 0);

    /* Uploads/extraction target the canonical Roms/NES, not the alias Roms/FC. */
    assert(cs_browser_write_root_for_scope(&paths, CS_SCOPE_ROMS, fc, write_root, sizeof(write_root)) == 0);
    assert(path_ends_with(write_root, "/Roms/NES"));
    /* Browsing still resolves to the legacy folder where the games actually are. */
    assert(cs_browser_root_for_scope(&paths, CS_SCOPE_ROMS, fc, browse_root, sizeof(browse_root)) == 0);
    assert(path_ends_with(browse_root, "/Roms/FC"));
}

static void test_uploads_target_primary_source_when_folder_exists_on_secondary(void) {
    char template[] = "/tmp/cs-platforms-upload-sources-XXXXXX";
    char *sandbox = mkdtemp(template);
    char primary_root[PATH_MAX];
    char secondary_root[PATH_MAX];
    char secondary_n64_dir[PATH_MAX];
    char secondary_rom_path[PATH_MAX];
    char primary_suffix[PATH_MAX];
    char secondary_suffix[PATH_MAX];
    char browse_root[PATH_MAX];
    char write_root[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    size_t count = 0;
    const cs_platform_info *n64;

    assert(sandbox != NULL);
    path_join(primary_root, sizeof(primary_root), sandbox, "card-a");
    path_join(secondary_root, sizeof(secondary_root), sandbox, "card-b");
    assert(mkdir(primary_root, 0775) == 0);
    assert(mkdir(secondary_root, 0775) == 0);
    write_catalogs(primary_root);
    write_launcher_file(primary_root, "emulators/mupen64plus/launch.sh");
    path_join(secondary_n64_dir, sizeof(secondary_n64_dir), secondary_root, "Roms/N64");
    make_dir(secondary_n64_dir);
    path_join(secondary_rom_path, sizeof(secondary_rom_path), secondary_n64_dir, "existing.z64");
    write_file(secondary_rom_path, "rom");

    set_sdcard_roots_realpath(primary_root, secondary_root);
    assert(cs_paths_init(&paths) == 0);
    assert(paths.source_count == 2);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    n64 = find_platform_entry(platforms, count, "N64");
    assert(n64 != NULL);
    assert(strcmp(n64->rom_directory, "N64") == 0);
    assert(strcmp(n64->canonical_rom_directory, "N64") == 0);

    assert(cs_browser_root_for_scope(&paths, CS_SCOPE_ROMS, n64, browse_root, sizeof(browse_root)) == 0);
    assert(snprintf(secondary_suffix, sizeof(secondary_suffix), "%s/Roms/N64", paths.sources[1].root) > 0);
    assert(path_ends_with(browse_root, secondary_suffix));

    assert(cs_browser_write_root_for_scope(&paths, CS_SCOPE_ROMS, n64, write_root, sizeof(write_root)) == 0);
    assert(snprintf(primary_suffix, sizeof(primary_suffix), "%s/Roms/N64", paths.sources[0].root) > 0);
    assert(path_ends_with(write_root, primary_suffix));
}

static void test_emulator_variants_fold_into_base_system(void) {
    char template[] = "/tmp/cs-platforms-variants-XXXXXX";
    char *root = mkdtemp(template);
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    cs_platform_info resolved = {0};
    size_t count = 0;

    assert(root != NULL);
    write_catalogs(root);
    write_core(root, "mgba_libretro.so");
    write_core(root, "snes9x_libretro.so");
    write_core(root, "gambatte_libretro.so");
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    /* Emulator/compat variants are not advertised as separate platform cards;
       their behavior is a core/launch choice under the base system. */
    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "GBA") != NULL);
    assert(find_platform_entry(platforms, count, "SFC") != NULL);
    assert(find_platform_entry(platforms, count, "GB") != NULL);
    assert(find_platform_entry(platforms, count, "MGBA") == NULL);
    assert(find_platform_entry(platforms, count, "SUPA") == NULL);
    assert(find_platform_entry(platforms, count, "SGB") == NULL);

    /* A legacy variant folder still resolves to its canonical base card. */
    assert(cs_platform_resolve(&paths, "MGBA", &resolved) == 0);
    assert(strcmp(resolved.tag, "GBA") == 0);
    assert(cs_platform_resolve(&paths, "SUPA", &resolved) == 0);
    assert(strcmp(resolved.tag, "SFC") == 0);
    assert(cs_platform_resolve(&paths, "SGB", &resolved) == 0);
    assert(strcmp(resolved.tag, "GB") == 0);
}

static void test_path_cores_and_ports_visibility(void) {
    char template[] = "/tmp/cs-platforms-path-XXXXXX";
    char *root = mkdtemp(template);
    char ports_dir[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    size_t count = 0;
    char browse_root[PATH_MAX];
    char write_root[PATH_MAX];
    char saves_root[PATH_MAX];
    const cs_platform_info *n64;

    assert(root != NULL);
    write_catalogs(root);
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "N64") == NULL);
    assert(find_platform_entry(platforms, count, "NDS") == NULL);
    assert(find_platform_entry(platforms, count, "PSP") == NULL);
    assert(find_platform_entry(platforms, count, "PORTS") == NULL);

    write_launcher_file(root, "emulators/drastic/launch.sh");
    write_launcher_file(root, "emulators/mupen64plus/launch.sh");
    write_launcher_file(root, "emulators/ppsspp/launch.sh");
    assert(snprintf(ports_dir, sizeof(ports_dir), "%s/Roms/Ports (PORTS)", root) > 0);
    make_dir(ports_dir);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    n64 = find_platform_entry(platforms, count, "N64");
    assert(n64 != NULL);
    assert(strcmp(n64->rom_directory, "N64") == 0);
    assert(strcmp(n64->canonical_rom_directory, "N64") == 0);
    assert(cs_browser_root_for_scope(&paths, CS_SCOPE_ROMS, n64, browse_root, sizeof(browse_root)) == 0);
    assert(path_ends_with(browse_root, "/Roms/N64"));
    assert(cs_browser_write_root_for_scope(&paths, CS_SCOPE_ROMS, n64, write_root, sizeof(write_root)) == 0);
    assert(path_ends_with(write_root, "/Roms/N64"));
    assert(cs_browser_root_for_scope(&paths, CS_SCOPE_SAVES, n64, saves_root, sizeof(saves_root)) == 0);
    assert(path_ends_with(saves_root, "/Saves/N64"));
    assert(find_platform_entry(platforms, count, "NDS") != NULL);
    assert(find_platform_entry(platforms, count, "PSP") != NULL);
    assert(find_platform_entry(platforms, count, "PORTS") != NULL);
}

static void test_identity_map_and_rom_root_stripping(void) {
    char template[] = "/tmp/cs-platforms-identity-XXXXXX";
    char *root = mkdtemp(template);
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    size_t count = 0;
    const cs_platform_info *arcade;
    const cs_platform_info *sms;
    const cs_platform_info *p8;
    const cs_platform_info *a7800;

    assert(root != NULL);
    write_catalogs(root);
    write_core(root, "fbneo_libretro.so");
    write_core(root, "fake08_libretro.so");
    write_core(root, "genesis_plus_gx_libretro.so");
    write_core(root, "prosystem_libretro.so");
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    arcade = find_platform_entry(platforms, count, "FBN");
    sms = find_platform_entry(platforms, count, "SMS");
    p8 = find_platform_entry(platforms, count, "P8");
    a7800 = find_platform_entry(platforms, count, "A7800");
    assert(arcade != NULL);
    assert(sms != NULL);
    assert(p8 != NULL);
    assert(a7800 != NULL);
    assert(strcmp(arcade->primary_code, "FBN") == 0);
    assert(strcmp(arcade->rom_directory, "ARCADE") == 0);
    assert(strcmp(sms->rom_directory, "MS") == 0);
    assert(strcmp(p8->rom_directory, "PICO8") == 0);
    assert(strcmp(a7800->rom_directory, "SEVENTYEIGHTHUNDRED") == 0);
}

static void test_custom_rom_directories_are_exposed_when_core_present(void) {
    char template[] = "/tmp/cs-platforms-custom-XXXXXX";
    char *root = mkdtemp(template);
    char foo_dir[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    cs_platform_info resolved = {0};
    size_t count = 0;
    const cs_platform_info *foo;

    assert(root != NULL);
    write_catalogs(root);
    assert(snprintf(foo_dir, sizeof(foo_dir), "%s/Roms/Awesome System (FOO)", root) > 0);
    make_dir(foo_dir);
    write_info(root, "FOO_libretro.info");
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "FOO") == NULL);

    write_core(root, "FOO_libretro.so");
    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    foo = find_platform_entry(platforms, count, "FOO");
    assert(foo != NULL);
    assert(foo->is_custom == 1);
    assert(strcmp(foo->rom_directory, "Awesome System (FOO)") == 0);
    assert(cs_platform_resolve(&paths, "FOO", &resolved) == 0);
    assert(resolved.is_custom == 1);
}

static void test_load_errors_are_typed(void) {
    char template[] = "/tmp/cs-platforms-errors-XXXXXX";
    char *root = mkdtemp(template);
    char defaults_dir[PATH_MAX];
    char systems_path[PATH_MAX];
    char cores_path[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[16];
    size_t count = 0;
    cs_catalog_error error = {0};

    assert(root != NULL);
    write_catalogs(root);
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(remove(paths.systems_catalog_path) == 0);
    assert(cs_platform_discover_with_error(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count, &error) != 0);
    assert(error.kind == CS_CATALOG_ERROR_MISSING);
    assert(count == 0);

    path_join(defaults_dir, sizeof(defaults_dir), root, ".system/leaf/platforms/mlp1/defaults");
    path_join(systems_path, sizeof(systems_path), defaults_dir, "systems.json");
    path_join(cores_path, sizeof(cores_path), defaults_dir, "cores.json");
    write_file(systems_path, "{\"version\":2,\"systems\":[]}");
    error.kind = CS_CATALOG_ERROR_NONE;
    assert(cs_platform_discover_with_error(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count, &error) != 0);
    assert(error.kind == CS_CATALOG_ERROR_VERSION);

    write_file(systems_path, "{not json");
    error.kind = CS_CATALOG_ERROR_NONE;
    assert(cs_platform_discover_with_error(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count, &error) != 0);
    assert(error.kind == CS_CATALOG_ERROR_PARSE);

    write_file(systems_path, "{\"version\":1,\"systems\":[]}");
    write_file(cores_path, "{\"version\":3,\"cores\":[]}");
    error.kind = CS_CATALOG_ERROR_NONE;
    assert(cs_platform_discover_with_error(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count, &error) != 0);
    assert(error.kind == CS_CATALOG_ERROR_VERSION);
}

static void test_shortcut_directories_are_excluded(void) {
    char template[] = "/tmp/cs-platforms-shortcut-XXXXXX";
    char *root = mkdtemp(template);
    char shortcut_dir[PATH_MAX];
    char marker[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info platforms[128];
    size_t count = 0;

    assert(root != NULL);
    write_catalogs(root);
    write_core(root, "genesis_plus_gx_libretro.so");
    assert(snprintf(shortcut_dir, sizeof(shortcut_dir), "%s/Roms/0) Sonic - Spindash (MD)", root) > 0);
    make_dir(shortcut_dir);
    assert(snprintf(marker, sizeof(marker), "%s/.shortcut", shortcut_dir) > 0);
    write_file(marker, "shortcut");
    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &count) == 0);
    assert(find_platform_entry(platforms, count, "MD") != NULL);
    assert(strcmp(find_platform_entry(platforms, count, "MD")->rom_directory, "MD") == 0);
    assert(cs_platform_is_shortcut_directory("0) Sonic - Spindash (MD)", shortcut_dir) == 1);
}

int main(void) {
    test_path_defaults();
    test_static_identity_helpers();
    test_visibility_requires_present_libretro_core();
    test_canonical_alias_rows_collapse_and_match_folders();
    test_uploads_target_canonical_folder();
    test_uploads_target_primary_source_when_folder_exists_on_secondary();
    test_emulator_variants_fold_into_base_system();
    test_path_cores_and_ports_visibility();
    test_identity_map_and_rom_root_stripping();
    test_custom_rom_directories_are_exposed_when_core_present();
    test_load_errors_are_typed();
    test_shortcut_directories_are_excluded();
    return 0;
}
