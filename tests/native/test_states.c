#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_paths.h"
#include "cs_platforms.h"
#include "cs_states.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void reset_path_env(void) {
    unsetenv("SDCARD_PATHS");
    unsetenv("ROMS_PATHS");
    unsetenv("STATES_PATHS");
    unsetenv("ROMS_PATH");
    unsetenv("STATES_PATH");
    unsetenv("CS_WEB_ROOT");
}

static void set_sdcard_root_realpath(const char *root) {
    char resolved[PATH_MAX];

    reset_path_env();
    assert(realpath(root, resolved) != NULL);
    assert(setenv("SDCARD_PATH", resolved, 1) == 0);
}

static const cs_state_entry *find_entry(const cs_state_entry *entries,
                                        size_t count,
                                        const char *title,
                                        const char *core_dir,
                                        int slot) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (entries[i].slot == slot && strcmp(entries[i].title, title) == 0
            && strcmp(entries[i].core_dir, core_dir) == 0) {
            return &entries[i];
        }
    }

    return NULL;
}

static int has_path(const char paths[][CS_PATH_MAX], size_t count, const char *path) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (strcmp(paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void make_leaf_platform_dirs(const char *root, char *rom_dir, size_t rom_dir_size, char *states_dir, size_t states_dir_size) {
    char roms_root[PATH_MAX];

    assert(snprintf(roms_root, sizeof(roms_root), "%s/Roms", root) > 0);
    assert(snprintf(rom_dir, rom_dir_size, "%s/Roms/GBA", root) > 0);
    assert(snprintf(states_dir, states_dir_size, "%s/States", root) > 0);
    make_dir(roms_root);
    make_dir(rom_dir);
    make_dir(states_dir);
}

static void test_leaf_states_are_grouped_with_thumbnails(void) {
    char template[] = "/tmp/cs-states-leaf-XXXXXX";
    char *root;
    char rom_dir[PATH_MAX];
    char states_dir[PATH_MAX];
    char mgba_dir[PATH_MAX];
    char rom_path[PATH_MAX];
    char slot_zero[PATH_MAX];
    char slot_zero_thumb[PATH_MAX];
    char auto_state[PATH_MAX];
    char slot_one[PATH_MAX];
    char slot_one_thumb[PATH_MAX];
    char unrelated_state[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    size_t count_only = 0;
    int truncated = 1;
    int count_only_truncated = 1;
    const cs_state_entry *manual;
    const cs_state_entry *auto_resume;
    const cs_state_entry *core_slot;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);
    make_leaf_platform_dirs(root, rom_dir, sizeof(rom_dir), states_dir, sizeof(states_dir));
    assert(snprintf(mgba_dir, sizeof(mgba_dir), "%s/mGBA", states_dir) > 0);
    make_dir(mgba_dir);

    assert(snprintf(rom_path, sizeof(rom_path), "%s/Pokemon Emerald.gba", rom_dir) > 0);
    assert(snprintf(slot_zero, sizeof(slot_zero), "%s/Pokemon Emerald.state", states_dir) > 0);
    assert(snprintf(slot_zero_thumb, sizeof(slot_zero_thumb), "%s/Pokemon Emerald.state.png", states_dir) > 0);
    assert(snprintf(auto_state, sizeof(auto_state), "%s/Pokemon Emerald.state.auto", states_dir) > 0);
    assert(snprintf(slot_one, sizeof(slot_one), "%s/Pokemon Emerald.state1", mgba_dir) > 0);
    assert(snprintf(slot_one_thumb, sizeof(slot_one_thumb), "%s/Pokemon Emerald.state1.png", mgba_dir) > 0);
    assert(snprintf(unrelated_state, sizeof(unrelated_state), "%s/Other Game.state", states_dir) > 0);

    write_file(rom_path, "rom");
    write_file(slot_zero, "state0");
    write_file(slot_zero_thumb, "png0");
    write_file(auto_state, "auto");
    write_file(slot_one, "state1");
    write_file(slot_one_thumb, "png1");
    write_file(unrelated_state, "orphan");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, NULL, 0, &count_only, &count_only_truncated) == 0);
    assert(count_only == 3);
    assert(count_only_truncated == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 3);
    assert(truncated == 0);

    manual = find_entry(entries, count, "Pokemon Emerald", "States", 0);
    assert(manual != NULL);
    assert(strcmp(manual->slot_label, "Slot 1") == 0);
    assert(strcmp(manual->kind, "slot") == 0);
    assert(strcmp(manual->format, "RetroArch") == 0);
    assert(strcmp(manual->preview_path, "States/Pokemon Emerald.state.png") == 0);
    assert(has_path(manual->download_paths, manual->download_path_count, "States/Pokemon Emerald.state") == 1);
    assert(has_path(manual->download_paths, manual->download_path_count, "States/Pokemon Emerald.state.png") == 1);
    assert(manual->warning_count == 0);

    auto_resume = find_entry(entries, count, "Pokemon Emerald", "States", 9);
    assert(auto_resume != NULL);
    assert(strcmp(auto_resume->slot_label, "Auto Resume") == 0);
    assert(strcmp(auto_resume->kind, "auto-resume") == 0);

    core_slot = find_entry(entries, count, "Pokemon Emerald", "mGBA", 1);
    assert(core_slot != NULL);
    assert(strcmp(core_slot->preview_path, "States/mGBA/Pokemon Emerald.state1.png") == 0);
}

static void test_numbered_retroarch_slots_stay_regular_slots(void) {
    char template[] = "/tmp/cs-states-slots-XXXXXX";
    char *root;
    char rom_dir[PATH_MAX];
    char states_dir[PATH_MAX];
    char rom_path[PATH_MAX];
    char state_eight[PATH_MAX];
    char state_nine[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    int truncated = 1;
    const cs_state_entry *slot_eight;
    const cs_state_entry *slot_nine;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);
    make_leaf_platform_dirs(root, rom_dir, sizeof(rom_dir), states_dir, sizeof(states_dir));
    assert(snprintf(rom_path, sizeof(rom_path), "%s/Manual Slot.gba", rom_dir) > 0);
    assert(snprintf(state_eight, sizeof(state_eight), "%s/Manual Slot.state8", states_dir) > 0);
    assert(snprintf(state_nine, sizeof(state_nine), "%s/Manual Slot.state.9", states_dir) > 0);

    write_file(rom_path, "rom");
    write_file(state_eight, "slot8");
    write_file(state_nine, "slot9");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 2);
    assert(truncated == 0);

    slot_eight = find_entry(entries, count, "Manual Slot", "States", 8);
    assert(slot_eight != NULL);
    assert(strcmp(slot_eight->slot_label, "Slot 9") == 0);
    assert(strcmp(slot_eight->kind, "slot") == 0);

    slot_nine = find_entry(entries, count, "Manual Slot", "States", 9);
    assert(slot_nine != NULL);
    assert(strcmp(slot_nine->slot_label, "Slot 10") == 0);
    assert(strcmp(slot_nine->kind, "slot") == 0);
}

static void test_state_collection_reports_truncation_without_failing(void) {
    char template[] = "/tmp/cs-states-limit-XXXXXX";
    char *root;
    char rom_dir[PATH_MAX];
    char states_dir[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    size_t count_only = 0;
    int truncated = 0;
    int count_only_truncated = 1;
    size_t i;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);
    make_leaf_platform_dirs(root, rom_dir, sizeof(rom_dir), states_dir, sizeof(states_dir));

    for (i = 0; i < CS_STATE_MAX_ENTRIES + 1; ++i) {
        char rom_path[PATH_MAX];
        char state_path[PATH_MAX];

        assert(snprintf(rom_path, sizeof(rom_path), "%s/Game %03zu.gba", rom_dir, i) > 0);
        assert(snprintf(state_path, sizeof(state_path), "%s/Game %03zu.state", states_dir, i) > 0);
        write_file(rom_path, "rom");
        write_file(state_path, "state");
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, NULL, 0, &count_only, &count_only_truncated) == 0);
    assert(count_only == CS_STATE_MAX_ENTRIES + 1);
    assert(count_only_truncated == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == CS_STATE_MAX_ENTRIES + 1);
    assert(truncated == 1);
    assert(entries[0].title[0] != '\0');
}

static void test_multi_source_state_paths_use_file_browser_aliases(void) {
    char template[] = "/tmp/cs-states-sources-XXXXXX";
    char *root;
    char primary_root[PATH_MAX];
    char secondary_root[PATH_MAX];
    char primary_resolved[PATH_MAX];
    char secondary_resolved[PATH_MAX];
    char source_list[PATH_MAX * 2 + 2];
    char roms_root[PATH_MAX];
    char rom_dir[PATH_MAX];
    char states_dir[PATH_MAX];
    char rom_path[PATH_MAX];
    char state_path[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    int truncated = 1;
    const cs_state_entry *entry;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(primary_root, sizeof(primary_root), "%s/primary", root) > 0);
    assert(snprintf(secondary_root, sizeof(secondary_root), "%s/secondary", root) > 0);
    make_dir(primary_root);
    make_dir(secondary_root);

    assert(snprintf(roms_root, sizeof(roms_root), "%s/Roms", secondary_root) > 0);
    assert(snprintf(rom_dir, sizeof(rom_dir), "%s/Roms/GBA", secondary_root) > 0);
    assert(snprintf(states_dir, sizeof(states_dir), "%s/States", secondary_root) > 0);
    make_dir(roms_root);
    make_dir(rom_dir);
    make_dir(states_dir);
    assert(snprintf(rom_path, sizeof(rom_path), "%s/Second Card.gba", rom_dir) > 0);
    assert(snprintf(state_path, sizeof(state_path), "%s/Second Card.state", states_dir) > 0);
    write_file(rom_path, "rom");
    write_file(state_path, "state");

    reset_path_env();
    assert(realpath(primary_root, primary_resolved) != NULL);
    assert(realpath(secondary_root, secondary_resolved) != NULL);
    assert(snprintf(source_list, sizeof(source_list), "%s:%s", primary_resolved, secondary_resolved) > 0);
    assert(setenv("SDCARD_PATHS", source_list, 1) == 0);
    assert(setenv("SDCARD_PATH", primary_resolved, 1) == 0);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 1);
    assert(truncated == 0);

    entry = find_entry(entries, count, "Second Card", "secondary/States", 0);
    assert(entry != NULL);
    assert(has_path(entry->download_paths, entry->download_path_count, "secondary/States/Second Card.state") == 1);
    assert(has_path(entry->delete_paths, entry->delete_path_count, "secondary/States/Second Card.state") == 1);
}

int main(void) {
    test_leaf_states_are_grouped_with_thumbnails();
    test_numbered_retroarch_slots_stay_regular_slots();
    test_state_collection_reports_truncation_without_failing();
    test_multi_source_state_paths_use_file_browser_aliases();
    return 0;
}
