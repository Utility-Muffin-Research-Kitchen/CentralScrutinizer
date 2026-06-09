#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cs_keep_awake.h"
#include "cs_paths.h"

static void remove_tree(const char *path) {
    DIR *dir;
    struct dirent *entry;

    assert(path != NULL);

    dir = opendir(path);
    assert(dir != NULL);

    while ((entry = readdir(dir)) != NULL) {
        char child_path[CS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        assert(snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name) < (int) sizeof(child_path));
        assert(lstat(child_path, &st) == 0);
        if (S_ISDIR(st.st_mode)) {
            remove_tree(child_path);
            continue;
        }
        assert(unlink(child_path) == 0);
    }

    assert(closedir(dir) == 0);
    assert(rmdir(path) == 0);
}

static void make_keep_awake_state_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    assert(paths != NULL);
    assert(buffer != NULL);
    assert(snprintf(buffer, buffer_len, "%s/keep-awake-state.txt", paths->shared_state_root) < (int) buffer_len);
}

static void assert_file_absent(const char *path) {
    assert(path != NULL);
    assert(access(path, F_OK) != 0);
}

int main(void) {
    char template[] = "/tmp/cs-keep-awake-XXXXXX";
    char *sdcard_root;
    char keep_awake_state_path[CS_PATH_MAX];
    cs_paths paths;

    sdcard_root = mkdtemp(template);
    assert(sdcard_root != NULL);

    assert(setenv("SDCARD_PATH", sdcard_root, 1) == 0);
    assert(setenv("CS_PLATFORM_NAME_OVERRIDE", "mlp1", 1) == 0);
    assert(cs_paths_init(&paths) == 0);
    make_keep_awake_state_path(&paths, keep_awake_state_path, sizeof(keep_awake_state_path));

    assert(strcmp(cs_keep_awake_platform_name(), "mlp1") == 0);
    assert(cs_keep_awake_current_platform_uses_settings_override() == 0);
    assert(cs_keep_awake_enable(NULL) == -1);
    assert(cs_keep_awake_disable(NULL) == -1);
    assert(cs_keep_awake_enable(&paths) == 0);
    assert(cs_keep_awake_enable(&paths) == 0);
    assert_file_absent(keep_awake_state_path);
    assert(cs_keep_awake_disable(&paths) == 0);
    assert_file_absent(keep_awake_state_path);

    remove_tree(sdcard_root);
    return 0;
}
