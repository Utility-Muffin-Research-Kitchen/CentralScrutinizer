#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_paths.h"

static void assert_default_paths(const cs_paths *paths) {
    assert(strcmp(paths->sdcard_root, "/mnt/sdcard") == 0);
    assert(strcmp(paths->system_root, "/mnt/sdcard/.system/leaf/platforms/mlp1") == 0);
    assert(strcmp(paths->shared_userdata_root, "/mnt/sdcard/.userdata/shared") == 0);
    assert(strcmp(paths->shared_state_root, "/mnt/sdcard/.userdata/mlp1/CentralScrutinizer") == 0);
    assert(strcmp(paths->logs_root, "/mnt/sdcard/.userdata/mlp1/logs") == 0);
    assert(strcmp(paths->web_root, "web/out") == 0);
    assert(strcmp(paths->roms_root, "/mnt/sdcard/Roms") == 0);
    assert(strcmp(paths->images_root, "/mnt/sdcard/Images") == 0);
    assert(strcmp(paths->saves_root, "/mnt/sdcard/Saves") == 0);
    assert(strcmp(paths->states_root, "/mnt/sdcard/States") == 0);
    assert(strcmp(paths->bios_root, "/mnt/sdcard/BIOS") == 0);
    assert(paths->overlays_root[0] == '\0');
    assert(strcmp(paths->temp_upload_root, "/mnt/sdcard/.userdata/mlp1/CentralScrutinizer/uploads/tmp") == 0);
    assert(paths->source_count == 1);
}

static void assert_fixture_paths(const cs_paths *paths) {
    assert(strcmp(paths->sdcard_root, "fixtures/mock_sdcard") == 0);
    assert(strcmp(paths->system_root, "fixtures/mock_sdcard/.system/leaf/platforms/mlp1") == 0);
    assert(strcmp(paths->shared_userdata_root, "fixtures/mock_sdcard/.userdata/shared") == 0);
    assert(strcmp(paths->shared_state_root, "fixtures/mock_sdcard/.userdata/mlp1/CentralScrutinizer") == 0);
    assert(strcmp(paths->web_root, "custom/web/root") == 0);
    assert(strcmp(paths->roms_root, "fixtures/mock_sdcard/Roms") == 0);
    assert(strcmp(paths->images_root, "fixtures/mock_sdcard/Images") == 0);
    assert(strcmp(paths->saves_root, "fixtures/mock_sdcard/Saves") == 0);
    assert(strcmp(paths->states_root, "fixtures/mock_sdcard/States") == 0);
    assert(strcmp(paths->bios_root, "fixtures/mock_sdcard/BIOS") == 0);
    assert(paths->overlays_root[0] == '\0');
    assert(strcmp(paths->temp_upload_root, "fixtures/mock_sdcard/.userdata/mlp1/CentralScrutinizer/uploads/tmp") == 0);
    assert(paths->source_count == 1);
}

static void fill_sentinel(cs_paths *paths) {
    memset(paths, 0xA5, sizeof(*paths));
}

static void assert_unchanged(const cs_paths *actual, const cs_paths *expected) {
    assert(memcmp(actual, expected, sizeof(*actual)) == 0);
}

static void assert_fixture_file(const char *path) {
    assert(access(path, F_OK) == 0);
}

static void make_dir(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0700); *p = '/'; }
    }
    assert(mkdir(path, 0700) == 0);
}

static void test_absolute_sdcard_path_is_canonicalized(void) {
    cs_paths paths;
    char template[] = "/tmp/cs-paths-XXXXXX";
    char *root;
    char actual_sdcard[PATH_MAX];
    char link_sdcard[PATH_MAX];
    char expected_root[PATH_MAX];

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(actual_sdcard, sizeof(actual_sdcard), "%s/sdcard-real", root) > 0);
    assert(snprintf(link_sdcard, sizeof(link_sdcard), "%s/SDCARD", root) > 0);

    make_dir(actual_sdcard);
    assert(symlink(actual_sdcard, link_sdcard) == 0);
    assert(realpath(actual_sdcard, expected_root) != NULL);

    setenv("SDCARD_PATH", link_sdcard, 1);
    unsetenv("CS_WEB_ROOT");

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, expected_root) == 0);

    assert(unlink(link_sdcard) == 0);
    assert(rmdir(actual_sdcard) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    cs_paths paths;
    cs_paths expected;
    char oversized[CS_PATH_MAX * 2];
    char boundary_sdcard[CS_PATH_MAX];
    char resolved_root[CS_PATH_MAX];
    char resolved_relative[CS_PATH_MAX];
    const cs_path_source *resolved_source = NULL;

    unsetenv("SDCARD_PATH");
    unsetenv("SDCARD_PATHS");
    unsetenv("CS_WEB_ROOT");
    unsetenv("SYSTEM_PATH");
    unsetenv("UMRK_PLATFORM_PATH");
    unsetenv("USERDATA_PATH");
    unsetenv("SHARED_USERDATA_PATH");

    assert(cs_paths_init(NULL) == -1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert_default_paths(&paths);

    setenv("SDCARD_PATH", "/mnt/sdcard", 1);
    unsetenv("CS_WEB_ROOT");

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, "/mnt/sdcard") == 0);
    assert(strcmp(paths.shared_userdata_root, "/mnt/sdcard/.userdata/shared") == 0);
    assert(strcmp(paths.shared_state_root, "/mnt/sdcard/.userdata/mlp1/CentralScrutinizer") == 0);

    setenv("SDCARD_PATH", "/definitely/missing/sdcard", 1);
    unsetenv("CS_WEB_ROOT");

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, "/definitely/missing/sdcard") == 0);

    setenv("SDCARD_PATH", "/mnt/sdcard", 1);
    setenv("SYSTEM_PATH", "/compat/system-path", 1);
    setenv("UMRK_PLATFORM_PATH", "/public/umrk-platform-path", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.system_root, "/public/umrk-platform-path") == 0);
    unsetenv("SYSTEM_PATH");
    unsetenv("UMRK_PLATFORM_PATH");

    test_absolute_sdcard_path_is_canonicalized();
    assert_fixture_file("fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)");
    assert_fixture_file("fixtures/mock_sdcard/Images/GBA/Pokemon Emerald Renamed.png");
    assert_fixture_file("fixtures/mock_sdcard/Roms/PlayStation (PS)/Castlevania - Symphony of the Night.chd");
    assert_fixture_file("fixtures/mock_sdcard/BIOS/PS/scph1001.bin");
    assert_fixture_file("fixtures/mock_sdcard/.userdata/mlp1/CentralScrutinizer/.keep");

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert_fixture_paths(&paths);
    assert(cs_paths_resolve_files_path(&paths,
                                       "Roms/GBA/Pokemon Emerald.gba",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       &resolved_source)
           == 0);
    assert(strcmp(resolved_root, "fixtures/mock_sdcard") == 0);
    assert(strcmp(resolved_relative, "Roms/GBA/Pokemon Emerald.gba") == 0);
    assert(resolved_source == &paths.sources[0]);

    setenv("SDCARD_PATHS", "/mnt/sdcard:/media/sdcard1", 1);
    setenv("CS_SOURCE_TEST_AVAILABLE", "all", 1);
    setenv("ROMS_PATHS", "/mnt/sdcard/Roms:/media/sdcard1/Roms", 1);
    setenv("IMAGES_PATHS", "/mnt/sdcard/Images:/media/sdcard1/Images", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(paths.source_count == 2);
    assert(strcmp(paths.sources[0].alias, "sdcard") == 0);
    assert(strcmp(paths.sources[1].alias, "sdcard1") == 0);
    assert(strcmp(paths.sources[1].roms_root, "/media/sdcard1/Roms") == 0);
    assert(strcmp(paths.sources[1].images_root, "/media/sdcard1/Images") == 0);
    resolved_source = NULL;
    assert(cs_paths_resolve_files_path(&paths,
                                       "sdcard/Roms/GBA/Pokemon Emerald.gba",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       &resolved_source)
           == 0);
    assert(strcmp(resolved_root, "/mnt/sdcard") == 0);
    assert(strcmp(resolved_relative, "Roms/GBA/Pokemon Emerald.gba") == 0);
    assert(resolved_source == &paths.sources[0]);
    resolved_source = NULL;
    assert(cs_paths_resolve_files_path(&paths,
                                       "sdcard1/Apps/mlp1/CentralScrutinizer.pak/pak.json",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       &resolved_source)
           == 0);
    assert(strcmp(resolved_root, "/media/sdcard1") == 0);
    assert(strcmp(resolved_relative, "Apps/mlp1/CentralScrutinizer.pak/pak.json") == 0);
    assert(resolved_source == &paths.sources[1]);
    assert(cs_paths_resolve_files_path(&paths,
                                       "Roms/GBA/Pokemon Emerald.gba",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       NULL)
           == -1);
    assert(cs_paths_resolve_files_path(&paths,
                                       "unknown/Roms/GBA/Pokemon Emerald.gba",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       NULL)
           == -1);
    assert(cs_paths_resolve_files_path(&paths,
                                       "/mnt/sdcard/Roms/GBA/Pokemon Emerald.gba",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       NULL)
           == -1);
    assert(cs_paths_resolve_files_path(&paths,
                                       "",
                                       resolved_root,
                                       sizeof(resolved_root),
                                       resolved_relative,
                                       sizeof(resolved_relative),
                                       NULL)
           == -1);

    /* A configured but unavailable Secondary card is skipped without making
       the available primary source or its logical roots unusable. */
    setenv("CS_SOURCE_TEST_AVAILABLE", "primary", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(paths.source_count == 1);
    assert(strcmp(paths.sources[0].alias, "sdcard") == 0);
    assert(strcmp(paths.sources[0].root, "/mnt/sdcard") == 0);
    assert(strcmp(paths.sources[0].roms_root, "/mnt/sdcard/Roms") == 0);
    assert(strcmp(paths.sources[0].images_root, "/mnt/sdcard/Images") == 0);

    unsetenv("SDCARD_PATHS");
    unsetenv("ROMS_PATHS");
    unsetenv("IMAGES_PATHS");
    unsetenv("CS_SOURCE_TEST_AVAILABLE");

    setenv("SDCARD_PATH", "", 1);
    setenv("CS_WEB_ROOT", "", 1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert_default_paths(&paths);

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    expected = paths;

    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    setenv("SDCARD_PATH", oversized, 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    assert(cs_paths_init(&paths) == -1);
    assert_unchanged(&paths, &expected);

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    expected = paths;

    memset(oversized, 'y', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", oversized, 1);

    assert(cs_paths_init(&paths) == -1);
    assert_unchanged(&paths, &expected);

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    expected = paths;

    memset(boundary_sdcard, 'z', sizeof(boundary_sdcard) - 1);
    boundary_sdcard[sizeof(boundary_sdcard) - 1] = '\0';
    setenv("SDCARD_PATH", boundary_sdcard, 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    assert(cs_paths_init(&paths) == -1);
    assert_unchanged(&paths, &expected);

    return 0;
}
