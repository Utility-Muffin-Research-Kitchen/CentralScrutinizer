#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include "cs_library.h"
#include "cs_paths.h"
#include "cs_platforms.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void make_dir_p(const char *path) {
    char buffer[PATH_MAX];
    size_t i;

    assert(path != NULL);
    assert(strlen(path) < sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", path);

    for (i = 1; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '/') {
            continue;
        }
        buffer[i] = '\0';
        assert(mkdir(buffer, 0700) == 0 || access(buffer, F_OK) == 0);
        buffer[i] = '/';
    }

    assert(mkdir(buffer, 0700) == 0 || access(buffer, F_OK) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void write_sized_file(const char *path, size_t size) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fclose(file) == 0);
    assert(truncate(path, (off_t) size) == 0);
}

static void set_sdcard_root_realpath(const char *root) {
    char resolved[PATH_MAX];

    assert(realpath(root, resolved) != NULL);
    setenv("SDCARD_PATH", resolved, 1);
    unsetenv("CS_WEB_ROOT");
    unsetenv("UMRK_INTERNAL_DATA_PATH");
}

static const cs_browser_entry *find_entry(const cs_browser_result *result, const char *name) {
    size_t i;

    assert(result != NULL);
    assert(name != NULL);
    for (i = 0; i < result->count; ++i) {
        if (strcmp(result->entries[i].name, name) == 0) {
            return &result->entries[i];
        }
    }

    return NULL;
}

static const cs_browser_entry *find_entry_by_type(const cs_browser_result *result, const char *type) {
    size_t i;

    assert(result != NULL);
    assert(type != NULL);
    for (i = 0; i < result->count; ++i) {
        if (strcmp(result->entries[i].type, type) == 0) {
            return &result->entries[i];
        }
    }

    return NULL;
}

static void test_fixture_browser_scopes_and_rejection(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    cs_platform_info gba_resolved = {0};
    cs_platform_info ps_resolved = {0};
    const cs_platform_info *gba;
    const cs_platform_info *ps;
    const cs_browser_entry *entry;

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    unsetenv("CS_WEB_ROOT");

    assert(cs_paths_init(&paths) == 0);
    /* Resolve like the routes do so legacy "Name (CODE)" fixture folders are
       picked up; the static table now defaults to the Jawaka short names. */
    assert(cs_platform_resolve(&paths, "GBA", &gba_resolved) == 0);
    assert(cs_platform_resolve(&paths, "PS", &ps_resolved) == 0);
    gba = &gba_resolved;
    ps = &ps_resolved;

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.scope, "roms") == 0);
    assert(strcmp(result.title, "ROMs - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)") == 0);
    assert(result.count == 1);
    assert(result.truncated == 0);
    entry = find_entry_by_type(&result, "rom");
    assert(entry != NULL);
    assert(strcmp(entry->type, "rom") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_SAVES, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.title, "Saves - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Saves/GBA") == 0);
    assert(result.count == 1);
    entry = find_entry(&result, "Pokemon Emerald.sav");
    assert(entry != NULL);
    assert(strcmp(entry->type, "save") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_BIOS, ps, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.title, "BIOS - Sony PlayStation") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/BIOS/PS") == 0);
    assert(result.count == 1);
    entry = find_entry(&result, "scph1001.bin");
    assert(entry != NULL);
    assert(strcmp(entry->type, "bios") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_CHEATS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.scope, "cheats") == 0);
    assert(strcmp(result.title, "Cheats - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Cheats/GBA") == 0);
    assert(result.count == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_FILES, NULL, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.title, "File Browser") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard") == 0);
    assert(find_entry(&result, ".system") != NULL);
    assert(find_entry(&result, "BIOS") != NULL);
    assert(find_entry(&result, "Roms") != NULL);
    assert(find_entry(&result, "Saves") != NULL);

    assert(cs_browser_list(&paths,
                           CS_SCOPE_FILES,
                           NULL,
                           ".system/leaf/platforms/mlp1/userdata/CentralScrutinizer",
                           0,
                           NULL,
                           &result)
           == CS_BROWSER_LIST_OK);
    assert(result.breadcrumb_count == 6);
    assert(strcmp(result.breadcrumbs[0].label, ".system") == 0);
    assert(strcmp(result.breadcrumbs[1].label, "leaf") == 0);
    assert(strcmp(result.breadcrumbs[2].label, "platforms") == 0);
    assert(strcmp(result.breadcrumbs[3].label, "mlp1") == 0);
    assert(strcmp(result.breadcrumbs[4].label, "userdata") == 0);
    assert(strcmp(result.breadcrumbs[5].label, "CentralScrutinizer") == 0);
    assert(find_entry(&result, ".keep") != NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, NULL, "", 0, NULL, &result) == CS_BROWSER_LIST_INTERNAL);
    assert(cs_browser_list(&paths, CS_SCOPE_FILES, NULL, "../outside", 0, NULL, &result)
           == CS_BROWSER_LIST_NOT_FOUND);
}

static void test_rom_thumbnail_resolution_is_png_only(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-thumb-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char images_dir[PATH_MAX];
    char image_system_dir[PATH_MAX];
    char rom_file[PATH_MAX];
    char png_art[PATH_MAX];
    char jpg_art[PATH_MAX];
    cs_platform_info gba_resolved = {0};
    const cs_platform_info *gba = &gba_resolved;
    const cs_browser_entry *entry;

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    assert(snprintf(images_dir, sizeof(images_dir), "%s/Images", root) > 0);
    assert(snprintf(image_system_dir, sizeof(image_system_dir), "%s/Images/GBA", root) > 0);
    assert(snprintf(rom_file, sizeof(rom_file), "%s/Box Art Test.gba", system_dir) > 0);
    assert(snprintf(png_art, sizeof(png_art), "%s/Box Art Test.png", image_system_dir) > 0);
    assert(snprintf(jpg_art, sizeof(jpg_art), "%s/Box Art Test.jpg", image_system_dir) > 0);

    make_dir(roms_dir);
    make_dir(system_dir);
    make_dir(images_dir);
    make_dir(image_system_dir);
    write_file(rom_file, "rom");
    write_file(png_art, "png");
    write_file(jpg_art, "jpg");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_platform_resolve(&paths, "GBA", &gba_resolved) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    entry = find_entry(&result, "Box Art Test.gba");
    assert(entry != NULL);
    assert(strcmp(entry->thumbnail_path, "Images/GBA/Box Art Test.png") == 0);

    assert(unlink(png_art) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    entry = find_entry(&result, "Box Art Test.gba");
    assert(entry != NULL);
    assert(entry->thumbnail_path[0] == '\0');

    assert(unlink(jpg_art) == 0);
    assert(unlink(rom_file) == 0);
    assert(rmdir(image_system_dir) == 0);
    assert(rmdir(images_dir) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_library_db_populates_root_rom_listing(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    cs_browser_sort_options sort = {CS_BROWSER_SORT_SIZE, CS_BROWSER_SORT_DESC};
    cs_platform_info gba;
    char template[] = "/tmp/cs-library-db-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char images_dir[PATH_MAX];
    char image_system_dir[PATH_MAX];
    char system_state_root[PATH_MAX];
    char leaf_dir[PATH_MAX];
    char platforms_dir[PATH_MAX];
    char mlp1_dir[PATH_MAX];
    char state_dir[PATH_MAX];
    char db_path[PATH_MAX];
    char zelda_rom[PATH_MAX];
    char metroid_rom[PATH_MAX];
    char fs_only_rom[PATH_MAX];
    char zelda_art[PATH_MAX];
    sqlite3 *db = NULL;
    char *err = NULL;
    const cs_browser_entry *entry;
    int db_count = 0;

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    assert(snprintf(images_dir, sizeof(images_dir), "%s/Images", root) > 0);
    assert(snprintf(image_system_dir, sizeof(image_system_dir), "%s/Images/GBA", root) > 0);
    assert(snprintf(system_state_root, sizeof(system_state_root), "%s/.system", root) > 0);
    assert(snprintf(leaf_dir, sizeof(leaf_dir), "%s/.system/leaf", root) > 0);
    assert(snprintf(platforms_dir, sizeof(platforms_dir), "%s/.system/leaf/platforms", root) > 0);
    assert(snprintf(mlp1_dir, sizeof(mlp1_dir), "%s/.system/leaf/platforms/mlp1", root) > 0);
    assert(snprintf(state_dir, sizeof(state_dir), "%s/.system/leaf/platforms/mlp1/state", root) > 0);
    assert(snprintf(db_path, sizeof(db_path), "%s/library.db", state_dir) > 0);
    assert(snprintf(zelda_rom, sizeof(zelda_rom), "%s/Zelda Minish Cap.gba", system_dir) > 0);
    assert(snprintf(metroid_rom, sizeof(metroid_rom), "%s/Metroid Fusion.gba", system_dir) > 0);
    assert(snprintf(fs_only_rom, sizeof(fs_only_rom), "%s/Filesystem Only.gba", system_dir) > 0);
    assert(snprintf(zelda_art, sizeof(zelda_art), "%s/Zelda Minish Cap.png", image_system_dir) > 0);

    make_dir(roms_dir);
    make_dir(system_dir);
    make_dir(images_dir);
    make_dir(image_system_dir);
    make_dir_p(state_dir);
    write_sized_file(zelda_rom, 7);
    write_sized_file(metroid_rom, 11);
    write_sized_file(fs_only_rom, 13);
    write_file(zelda_art, "png");

    assert(sqlite3_open(db_path, &db) == SQLITE_OK);
    assert(sqlite3_exec(db,
                        "CREATE TABLE games ("
                        "id INTEGER PRIMARY KEY,"
                        "system TEXT NOT NULL,"
                        "name TEXT NOT NULL,"
                        "rom_path TEXT NOT NULL UNIQUE,"
                        "image_path TEXT,"
                        "last_played INTEGER,"
                        "playtime_s INTEGER NOT NULL DEFAULT 0"
                        ");"
                        "CREATE TABLE favorites ("
                        "kind TEXT NOT NULL CHECK (kind IN ('game','app')),"
                        "target_id INTEGER NOT NULL,"
                        "added_at INTEGER NOT NULL,"
                        "PRIMARY KEY (kind, target_id)"
                        ");"
                        "INSERT INTO games (system, name, rom_path, image_path) VALUES "
                        "('GBA', 'Database Zelda', 'Roms/GBA/Zelda Minish Cap.gba', 'Images/GBA/Zelda Minish Cap.png'),"
                        "('GBA', 'Database Metroid', 'Roms/GBA/Metroid Fusion.gba', NULL);",
                        NULL,
                        NULL,
                        &err)
           == SQLITE_OK);
    assert(err == NULL);
    assert(sqlite3_close(db) == SQLITE_OK);
    db = NULL;

    set_sdcard_root_realpath(root);
    assert(setenv("UMRK_INTERNAL_DATA_PATH", state_dir, 1) == 0);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_platform_resolve(&paths, "GBA", &gba) == 0);
    assert(strcmp(gba.rom_directory, "GBA") == 0);

    assert(cs_library_db_count_roms_for_platform(&paths, &gba, &db_count) == 0);
    assert(db_count == 2);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, &gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 2);
    assert(result.total_count == 2);
    assert(find_entry(&result, "Filesystem Only.gba") == NULL);
    entry = find_entry(&result, "Zelda Minish Cap.gba");
    assert(entry != NULL);
    assert(strcmp(entry->path, "Zelda Minish Cap.gba") == 0);
    assert(strcmp(entry->type, "rom") == 0);
    assert(entry->size == 7);
    assert(strcmp(entry->thumbnail_path, "Images/GBA/Zelda Minish Cap.png") == 0);
    assert(entry->favorite_supported == 1);
    assert(entry->favorite == 0);

    assert(cs_library_db_set_game_favorite(&paths, &gba, "Zelda Minish Cap.gba", 1) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, &gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    entry = find_entry(&result, "Zelda Minish Cap.gba");
    assert(entry != NULL);
    assert(entry->favorite_supported == 1);
    assert(entry->favorite == 1);
    entry = find_entry(&result, "Metroid Fusion.gba");
    assert(entry != NULL);
    assert(entry->favorite_supported == 1);
    assert(entry->favorite == 0);

    assert(cs_library_db_set_game_favorite(&paths, &gba, "Zelda Minish Cap.gba", 0) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, &gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    entry = find_entry(&result, "Zelda Minish Cap.gba");
    assert(entry != NULL);
    assert(entry->favorite_supported == 1);
    assert(entry->favorite == 0);
    assert(cs_library_db_set_game_favorite(&paths, &gba, "Filesystem Only.gba", 1) != 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, &gba, "", 0, "database zelda", &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 1);
    assert(strcmp(result.entries[0].name, "Zelda Minish Cap.gba") == 0);

    assert(cs_browser_list_with_sort(&paths, CS_SCOPE_ROMS, &gba, "", 0, NULL, &sort, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 2);
    assert(strcmp(result.entries[0].name, "Metroid Fusion.gba") == 0);
    assert(strcmp(result.entries[1].name, "Zelda Minish Cap.gba") == 0);

    assert(unlink(db_path) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, &gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 3);
    assert(find_entry(&result, "Filesystem Only.gba") != NULL);

    assert(unlink(zelda_art) == 0);
    assert(unlink(fs_only_rom) == 0);
    assert(unlink(metroid_rom) == 0);
    assert(unlink(zelda_rom) == 0);
    assert(rmdir(state_dir) == 0);
    assert(rmdir(mlp1_dir) == 0);
    assert(rmdir(platforms_dir) == 0);
    assert(rmdir(leaf_dir) == 0);
    assert(rmdir(system_state_root) == 0);
    assert(rmdir(image_system_dir) == 0);
    assert(rmdir(images_dir) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlink_entries_are_skipped(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char real_rom[PATH_MAX];
    char outside_file[PATH_MAX];
    char link_path[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    assert(snprintf(real_rom, sizeof(real_rom), "%s/Pokemon Emerald.gba", system_dir) > 0);
    assert(snprintf(outside_file, sizeof(outside_file), "%s/not-a-rom.bin", root) > 0);
    assert(snprintf(link_path, sizeof(link_path), "%s/Outside Link.gba", system_dir) > 0);

    make_dir(roms_dir);
    make_dir(system_dir);
    write_file(real_rom, "rom");
    write_file(outside_file, "outside");
    assert(symlink(outside_file, link_path) == 0);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 1);
    assert(result.truncated == 0);
    assert(strcmp(result.entries[0].name, "Pokemon Emerald.gba") == 0);

    assert(unlink(link_path) == 0);
    assert(unlink(real_rom) == 0);
    assert(unlink(outside_file) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlinked_scope_root_is_rejected(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-rootlink-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char outside_dir[PATH_MAX];
    char real_rom[PATH_MAX];
    char system_link[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(outside_dir, sizeof(outside_dir), "%s/outside-system", root) > 0);
    assert(snprintf(real_rom, sizeof(real_rom), "%s/Pokemon Emerald.gba", outside_dir) > 0);
    assert(snprintf(system_link, sizeof(system_link), "%s/Roms/GBA", root) > 0);

    make_dir(roms_dir);
    make_dir(outside_dir);
    write_file(real_rom, "rom");
    assert(symlink(outside_dir, system_link) == 0);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_NOT_FOUND);

    assert(unlink(system_link) == 0);
    assert(unlink(real_rom) == 0);
    assert(rmdir(outside_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlinked_absolute_sdcard_root_is_canonicalized_for_files_scope(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-sdlink-XXXXXX";
    char *root;
    char actual_sdcard[PATH_MAX];
    char linked_sdcard[PATH_MAX];
    char roms_dir[PATH_MAX];
    char bios_dir[PATH_MAX];
    char saves_dir[PATH_MAX];
    char expected_root[PATH_MAX];

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(actual_sdcard, sizeof(actual_sdcard), "%s/sdcard-real", root) > 0);
    assert(snprintf(linked_sdcard, sizeof(linked_sdcard), "%s/SDCARD", root) > 0);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", actual_sdcard) > 0);
    assert(snprintf(bios_dir, sizeof(bios_dir), "%s/BIOS", actual_sdcard) > 0);
    assert(snprintf(saves_dir, sizeof(saves_dir), "%s/Saves", actual_sdcard) > 0);

    make_dir(actual_sdcard);
    make_dir(roms_dir);
    make_dir(bios_dir);
    make_dir(saves_dir);
    assert(symlink(actual_sdcard, linked_sdcard) == 0);
    assert(realpath(actual_sdcard, expected_root) != NULL);

    setenv("SDCARD_PATH", linked_sdcard, 1);
    unsetenv("CS_WEB_ROOT");

    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, expected_root) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_FILES, NULL, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.root_path, expected_root) == 0);
    assert(find_entry(&result, "Roms") != NULL);
    assert(find_entry(&result, "BIOS") != NULL);
    assert(find_entry(&result, "Saves") != NULL);

    assert(unlink(linked_sdcard) == 0);
    assert(rmdir(saves_dir) == 0);
    assert(rmdir(bios_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(actual_sdcard) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlinked_roms_parent_is_rejected(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-parentlink-XXXXXX";
    char outside_template[] = "/tmp/cs-library-parentoutside-XXXXXX";
    char *root;
    char *outside_root;
    char real_roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char rom_file[PATH_MAX];
    char roms_link[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    outside_root = mkdtemp(outside_template);
    assert(root != NULL);
    assert(outside_root != NULL);

    assert(snprintf(real_roms_dir, sizeof(real_roms_dir), "%s/real-roms", outside_root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/real-roms/GBA", outside_root) > 0);
    assert(snprintf(rom_file, sizeof(rom_file), "%s/Pokemon Emerald.gba", system_dir) > 0);
    assert(snprintf(roms_link, sizeof(roms_link), "%s/Roms", root) > 0);

    make_dir(real_roms_dir);
    make_dir(system_dir);
    write_file(rom_file, "rom");
    assert(symlink(real_roms_dir, roms_link) == 0);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_NOT_FOUND);

    assert(unlink(roms_link) == 0);
    assert(unlink(rom_file) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(real_roms_dir) == 0);
    assert(rmdir(outside_root) == 0);
    assert(rmdir(root) == 0);
}

static void test_pagination_window_and_total_count(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-paging-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char expected_name[64];
    const size_t total_files = CS_BROWSER_PAGE_SIZE + 50;
    size_t i;
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    make_dir(roms_dir);
    make_dir(system_dir);

    for (i = 0; i < total_files; ++i) {
        char file_path[PATH_MAX];

        assert(snprintf(file_path, sizeof(file_path), "%s/Game %03zu.gba", system_dir, i) > 0);
        write_file(file_path, "rom");
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == CS_BROWSER_PAGE_SIZE);
    assert(result.total_count == total_files);
    assert(result.offset == 0);
    assert(result.truncated == 0);
    assert(strcmp(result.entries[0].name, "Game 000.gba") == 0);
    assert(snprintf(expected_name, sizeof(expected_name), "Game %03zu.gba", (size_t) CS_BROWSER_PAGE_SIZE - 1u) > 0);
    assert(strcmp(result.entries[CS_BROWSER_PAGE_SIZE - 1].name, expected_name) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", CS_BROWSER_PAGE_SIZE, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == total_files - CS_BROWSER_PAGE_SIZE);
    assert(result.total_count == total_files);
    assert(result.offset == CS_BROWSER_PAGE_SIZE);
    assert(snprintf(expected_name, sizeof(expected_name), "Game %03zu.gba", (size_t) CS_BROWSER_PAGE_SIZE) > 0);
    assert(strcmp(result.entries[0].name, expected_name) == 0);
    assert(snprintf(expected_name, sizeof(expected_name), "Game %03zu.gba", total_files - 1) > 0);
    assert(strcmp(result.entries[result.count - 1].name, expected_name) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", total_files, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 0);
    assert(result.total_count == total_files);
    assert(result.offset == total_files);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", total_files + 10, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 0);
    assert(result.total_count == total_files);

    for (i = 0; i < total_files; ++i) {
        char file_path[PATH_MAX];

        assert(snprintf(file_path, sizeof(file_path), "%s/Game %03zu.gba", system_dir, i) > 0);
        assert(unlink(file_path) == 0);
    }
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_sorted_pagination_window_uses_requested_order(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    cs_browser_sort_options sort = {CS_BROWSER_SORT_SIZE, CS_BROWSER_SORT_DESC};
    char template[] = "/tmp/cs-library-sort-page-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char expected_name[64];
    const size_t total_files = CS_BROWSER_PAGE_SIZE + 3;
    size_t i;
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    make_dir(roms_dir);
    make_dir(system_dir);

    for (i = 0; i < total_files; ++i) {
        char file_path[PATH_MAX];
        struct utimbuf times;

        assert(snprintf(file_path, sizeof(file_path), "%s/Game %03zu.gba", system_dir, i) > 0);
        write_sized_file(file_path, i + 1u);
        times.actime = 1700000000 + (long) i;
        times.modtime = 1700000000 + (long) (total_files - i);
        assert(utime(file_path, &times) == 0);
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_browser_list_with_sort(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &sort, &result) == CS_BROWSER_LIST_OK);
    assert(result.count == CS_BROWSER_PAGE_SIZE);
    assert(result.total_count == total_files);
    assert(result.offset == 0);
    assert(snprintf(expected_name, sizeof(expected_name), "Game %03zu.gba", total_files - 1u) > 0);
    assert(strcmp(result.entries[0].name, expected_name) == 0);
    assert(strcmp(result.entries[CS_BROWSER_PAGE_SIZE - 1].name, "Game 003.gba") == 0);

    assert(cs_browser_list_with_sort(&paths, CS_SCOPE_ROMS, gba, "", CS_BROWSER_PAGE_SIZE, NULL, &sort, &result)
           == CS_BROWSER_LIST_OK);
    assert(result.count == 3);
    assert(result.total_count == total_files);
    assert(result.offset == CS_BROWSER_PAGE_SIZE);
    assert(strcmp(result.entries[0].name, "Game 002.gba") == 0);
    assert(strcmp(result.entries[1].name, "Game 001.gba") == 0);
    assert(strcmp(result.entries[2].name, "Game 000.gba") == 0);

    sort.column = CS_BROWSER_SORT_MODIFIED;
    sort.direction = CS_BROWSER_SORT_ASC;
    assert(cs_browser_list_with_sort(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &sort, &result) == CS_BROWSER_LIST_OK);
    assert(snprintf(expected_name, sizeof(expected_name), "Game %03zu.gba", total_files - 1u) > 0);
    assert(strcmp(result.entries[0].name, expected_name) == 0);

    sort.direction = CS_BROWSER_SORT_DESC;
    assert(cs_browser_list_with_sort(&paths, CS_SCOPE_ROMS, gba, "", 0, NULL, &sort, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.entries[0].name, "Game 000.gba") == 0);

    for (i = 0; i < total_files; ++i) {
        char file_path[PATH_MAX];

        assert(snprintf(file_path, sizeof(file_path), "%s/Game %03zu.gba", system_dir, i) > 0);
        assert(unlink(file_path) == 0);
    }
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_query_filters_results_case_insensitively(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-query-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char pokemon_emerald[PATH_MAX];
    char pokemon_ruby[PATH_MAX];
    char metroid[PATH_MAX];
    char zelda[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    assert(snprintf(pokemon_emerald, sizeof(pokemon_emerald), "%s/Pokemon Emerald.gba", system_dir) > 0);
    assert(snprintf(pokemon_ruby, sizeof(pokemon_ruby), "%s/Pokemon Ruby.gba", system_dir) > 0);
    assert(snprintf(metroid, sizeof(metroid), "%s/Metroid Fusion.gba", system_dir) > 0);
    assert(snprintf(zelda, sizeof(zelda), "%s/Zelda Minish Cap.gba", system_dir) > 0);

    make_dir(roms_dir);
    make_dir(system_dir);
    write_file(pokemon_emerald, "rom");
    write_file(pokemon_ruby, "rom");
    write_file(metroid, "rom");
    write_file(zelda, "rom");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, "pokemon", &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 2);
    assert(result.total_count == 2);
    assert(find_entry(&result, "Pokemon Emerald.gba") != NULL);
    assert(find_entry(&result, "Pokemon Ruby.gba") != NULL);
    assert(find_entry(&result, "Metroid Fusion.gba") == NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, "POKEMON", &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 2);
    assert(result.total_count == 2);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, "EmErAlD", &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 1);
    assert(strcmp(result.entries[0].name, "Pokemon Emerald.gba") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, "nothingmatchesthis", &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 0);
    assert(result.total_count == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", 0, "", &result) == CS_BROWSER_LIST_OK);
    assert(result.count == 4);
    assert(result.total_count == 4);

    assert(unlink(pokemon_emerald) == 0);
    assert(unlink(pokemon_ruby) == 0);
    assert(unlink(metroid) == 0);
    assert(unlink(zelda) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_ports_browser_supports_hidden_ports_and_rejects_other_resources(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-ports-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char ports_dir[PATH_MAX];
    char hidden_ports_dir[PATH_MAX];
    char shortcut_dir[PATH_MAX];
    char shortcut_marker[PATH_MAX];
    char root_script[PATH_MAX];
    char port_manifest[PATH_MAX];
    const cs_platform_info *ports = cs_platform_find("PORTS");

    assert(ports != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(ports_dir, sizeof(ports_dir), "%s/Roms/PORTS", root) > 0);
    assert(snprintf(hidden_ports_dir, sizeof(hidden_ports_dir), "%s/Roms/PORTS/.ports", root) > 0);
    assert(snprintf(shortcut_dir, sizeof(shortcut_dir), "%s/Roms/PORTS/0) Search (SHORTCUT)", root) > 0);
    assert(snprintf(shortcut_marker, sizeof(shortcut_marker), "%s/.shortcut", shortcut_dir) > 0);
    assert(snprintf(root_script, sizeof(root_script), "%s/PokeMMO.sh", ports_dir) > 0);
    assert(snprintf(port_manifest, sizeof(port_manifest), "%s/port.json", hidden_ports_dir) > 0);

    make_dir(roms_dir);
    make_dir(ports_dir);
    make_dir(hidden_ports_dir);
    make_dir(shortcut_dir);
    write_file(shortcut_marker, "Search");
    write_file(root_script, "launch");
    write_file(port_manifest, "{}");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, ports, "", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(find_entry(&result, ".ports") != NULL);
    assert(find_entry(&result, "PokeMMO.sh") != NULL);
    assert(find_entry(&result, "0) Search (SHORTCUT)") == NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, ports, ".ports", 0, NULL, &result) == CS_BROWSER_LIST_OK);
    assert(strcmp(result.path, ".ports") == 0);
    assert(find_entry(&result, "port.json") != NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_SAVES, ports, "", 0, NULL, &result) == CS_BROWSER_LIST_INTERNAL);
    assert(cs_browser_list(&paths, CS_SCOPE_BIOS, ports, "", 0, NULL, &result) == CS_BROWSER_LIST_INTERNAL);
    assert(cs_browser_list(&paths, CS_SCOPE_OVERLAYS, ports, "", 0, NULL, &result) == CS_BROWSER_LIST_INTERNAL);
    assert(cs_browser_list(&paths, CS_SCOPE_CHEATS, ports, "", 0, NULL, &result) == CS_BROWSER_LIST_INTERNAL);

    assert(remove(port_manifest) == 0);
    assert(remove(root_script) == 0);
    assert(remove(shortcut_marker) == 0);
    assert(rmdir(shortcut_dir) == 0);
    assert(rmdir(hidden_ports_dir) == 0);
    assert(rmdir(ports_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    test_fixture_browser_scopes_and_rejection();
    test_rom_thumbnail_resolution_is_png_only();
    test_library_db_populates_root_rom_listing();
    test_symlink_entries_are_skipped();
    test_symlinked_scope_root_is_rejected();
    test_symlinked_absolute_sdcard_root_is_canonicalized_for_files_scope();
    test_symlinked_roms_parent_is_rejected();
    test_pagination_window_and_total_count();
    test_sorted_pagination_window_uses_requested_order();
    test_query_filters_results_case_insensitively();
    test_ports_browser_supports_hidden_ports_and_rejects_other_resources();
    return 0;
}
