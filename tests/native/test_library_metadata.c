#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include "cjson/cJSON.h"
#include "cs_library_metadata.h"
#include "cs_paths.h"

#define LIVE_CARD "card-key-live"
#define ALLOW_ALL (CS_METADATA_ALLOW_DESCRIPTION | CS_METADATA_ALLOW_ORDER)

typedef struct {
    char primary[PATH_MAX];
    char secondary[PATH_MAX];
    char state[PATH_MAX];
    char db_path[PATH_MAX];
    cs_paths paths;
    cs_platform_info gba;
} fixture;

static void make_dir_p(const char *path) {
    char buffer[PATH_MAX];

    assert(snprintf(buffer, sizeof(buffer), "%s", path) < (int) sizeof(buffer));
    for (char *cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            assert(mkdir(buffer, 0700) == 0 || access(buffer, F_OK) == 0);
            *cursor = '/';
        }
    }
    assert(mkdir(buffer, 0700) == 0 || access(buffer, F_OK) == 0);
}

static void exec_sql(const char *db_path, const char *sql) {
    sqlite3 *db = NULL;
    char *err = NULL;

    assert(sqlite3_open(db_path, &db) == SQLITE_OK);
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sql failed: %s\n", err ? err : "?");
        assert(0);
    }
    assert(sqlite3_close(db) == SQLITE_OK);
}

/* The tables as Jawaka creates them, with a primary game, one on the card in the slot,
 * one on a parked card, one hidden and one from another system. */
static const char *const kLibrarySql =
    "CREATE TABLE games (id INTEGER PRIMARY KEY, system TEXT NOT NULL, name TEXT NOT NULL,"
    " source_id TEXT NOT NULL, rom_relpath TEXT NOT NULL, rom_path TEXT NOT NULL,"
    " UNIQUE (source_id, rom_relpath));"
    "CREATE TABLE game_settings (game_id INTEGER NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,"
    " updated_at INTEGER NOT NULL, PRIMARY KEY (game_id, key));"
    "CREATE TABLE hidden_roms (source_id TEXT NOT NULL, rom_relpath TEXT NOT NULL,"
    " member TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE library_cards (id INTEGER PRIMARY KEY, card_key TEXT NOT NULL UNIQUE,"
    " is_live INTEGER NOT NULL DEFAULT 0, live_digest TEXT, label TEXT NOT NULL DEFAULT '',"
    " last_seen INTEGER NOT NULL);"
    "CREATE TABLE system_settings (system TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,"
    " updated_at INTEGER NOT NULL, PRIMARY KEY (system, key));"
    "INSERT INTO library_cards (card_key, is_live, last_seen) VALUES ('" LIVE_CARD "', 1, 1);"
    "INSERT INTO games (id, system, name, source_id, rom_relpath, rom_path) VALUES"
    " (1, 'GBA', 'Zelda', 'primary', 'GBA/Zelda.gba', 'Roms/GBA/Zelda.gba'),"
    " (2, 'GBA', 'Zelda Second', 'secondary_sd', 'GBA/Zelda.gba', '/second/Roms/GBA/Zelda.gba'),"
    " (3, 'GBA', 'Parked', 'card-1', 'GBA/Parked.gba', '/gone/Roms/GBA/Parked.gba'),"
    " (4, 'GBA', 'Hidden', 'primary', 'GBA/Hidden.gba', 'Roms/GBA/Hidden.gba'),"
    " (5, 'SFC', 'Other System', 'primary', 'SFC/Other.sfc', 'Roms/SFC/Other.sfc'),"
    " (6, 'GBA', 'Imported File', 'primary', 'GBA/imported.gba', 'Roms/GBA/imported.gba');"
    "INSERT INTO hidden_roms (source_id, rom_relpath) VALUES ('primary', 'GBA/Hidden.gba');"
    "INSERT INTO game_settings VALUES (6, 'imported_display_name', 'Imported Title', 1);"
    "INSERT INTO game_settings VALUES (1, 'ss.synopsis', 'Scraped text', 1);";

static void fixture_init(fixture *fx, const char *sql) {
    char template[] = "/tmp/cs-library-metadata-XXXXXX";
    char *made = mkdtemp(template);
    char root[PATH_MAX];
    char sources[PATH_MAX * 2 + 2];
    char second_roms[PATH_MAX];

    assert(made != NULL);
    assert(realpath(made, root) != NULL);
    memset(fx, 0, sizeof(*fx));
    assert(snprintf(fx->primary, sizeof(fx->primary), "%s/primary", root) > 0);
    assert(snprintf(fx->secondary, sizeof(fx->secondary), "%s/secondary", root) > 0);
    assert(snprintf(fx->state, sizeof(fx->state), "%s/state", root) > 0);
    assert(snprintf(fx->db_path, sizeof(fx->db_path), "%s/library.db", fx->state) > 0);
    assert(snprintf(second_roms, sizeof(second_roms), "%s/Roms", fx->secondary) > 0);
    make_dir_p(fx->primary);
    make_dir_p(second_roms);
    make_dir_p(fx->state);
    exec_sql(fx->db_path, sql);

    assert(snprintf(sources, sizeof(sources), "%s:%s", fx->primary, fx->secondary) > 0);
    assert(setenv("SDCARD_PATH", fx->primary, 1) == 0);
    assert(setenv("SDCARD_PATHS", sources, 1) == 0);
    assert(setenv("CS_SOURCE_TEST_AVAILABLE", "all", 1) == 0);
    assert(setenv("UMRK_INTERNAL_DATA_PATH", fx->state, 1) == 0);
    assert(cs_paths_init(&fx->paths) == 0);
    assert(fx->paths.source_count == 2);
    snprintf(fx->gba.tag, sizeof(fx->gba.tag), "GBA");
    snprintf(fx->gba.primary_code, sizeof(fx->gba.primary_code), "GBA");
    snprintf(fx->gba.rom_directory, sizeof(fx->gba.rom_directory), "GBA");
}

static cJSON *make_change(const char *source,
                          const char *path,
                          const char *card_key,
                          const char *field,
                          const char *value,
                          const char *expected) {
    cJSON *change = cJSON_CreateObject();

    cJSON_AddStringToObject(change, "source", source);
    cJSON_AddStringToObject(change, "path", path);
    if (card_key) {
        cJSON_AddStringToObject(change, "cardKey", card_key);
    } else {
        cJSON_AddNullToObject(change, "cardKey");
    }
    cJSON_AddStringToObject(change, "field", field);
    if (value) {
        cJSON_AddStringToObject(change, "value", value);
    } else {
        cJSON_AddNullToObject(change, "value");
    }
    if (expected) {
        cJSON_AddStringToObject(change, "expected", expected);
    } else {
        cJSON_AddNullToObject(change, "expected");
    }
    return change;
}

/* Applies one change. A conflict's reason is copied out, and "" means it saved. */
static cs_metadata_status apply_one(fixture *fx, cJSON *change, unsigned allowed, char *reason, size_t size) {
    cJSON *changes = cJSON_CreateArray();
    cJSON *conflicts = NULL;
    const char *problem = NULL;
    int saved = -1;
    cs_metadata_status status;

    cJSON_AddItemToArray(changes, change);
    status = cs_metadata_apply(&fx->paths, &fx->gba, changes, allowed, &saved, &conflicts, &problem);
    snprintf(reason, size, "%s", "");
    if (status == CS_METADATA_OK) {
        assert(saved == 1 && !conflicts);
    } else {
        assert(saved == 0);
    }
    if (conflicts) {
        const cJSON *first = cJSON_GetArrayItem(conflicts, 0);

        snprintf(reason, size, "%s", cJSON_GetObjectItemCaseSensitive(first, "reason")->valuestring);
        cJSON_Delete(conflicts);
    } else if (problem) {
        snprintf(reason, size, "%s", problem);
    }
    cJSON_Delete(changes);
    return status;
}

/* The raw row: returns 0 when it doesn't exist, as distinct from an empty value. */
static int read_setting(const fixture *fx, int game_id, const char *key, char *out, size_t size) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    assert(sqlite3_open(fx->db_path, &db) == SQLITE_OK);
    assert(sqlite3_prepare_v2(db, "SELECT value FROM game_settings WHERE game_id=? AND key=?;", -1, &stmt, NULL)
           == SQLITE_OK);
    sqlite3_bind_int(stmt, 1, game_id);
    sqlite3_bind_text(stmt, 2, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(out, size, "%s", (const char *) sqlite3_column_text(stmt, 0));
        found = 1;
    }
    sqlite3_finalize(stmt);
    assert(sqlite3_close(db) == SQLITE_OK);
    return found;
}

static void expect_setting(const fixture *fx, int game_id, const char *key, const char *want) {
    char value[2048];
    int found = read_setting(fx, game_id, key, value, sizeof(value));

    if (!want) {
        assert(!found);
    } else {
        assert(found && strcmp(value, want) == 0);
    }
}

static const cJSON *find_game(const cJSON *games, const char *source, const char *path) {
    const cJSON *game;

    cJSON_ArrayForEach(game, games) {
        if (strcmp(cJSON_GetObjectItemCaseSensitive(game, "source")->valuestring, source) == 0
            && strcmp(cJSON_GetObjectItemCaseSensitive(game, "path")->valuestring, path) == 0) {
            return game;
        }
    }
    return NULL;
}

static void test_list_covers_listed_games(void) {
    fixture fx;
    cJSON *out = cJSON_CreateObject();
    const cJSON *games;
    const cJSON *game;

    fixture_init(&fx, kLibrarySql);
    exec_sql(fx.db_path, "INSERT INTO game_settings VALUES (1, 'display_name', 'My Zelda', 1);");
    assert(cs_metadata_list_games(&fx.paths, &fx.gba, 0, out) == CS_METADATA_OK);
    games = cJSON_GetObjectItemCaseSensitive(out, "games");

    /* Not the parked card's game, the hidden game or the other system's. */
    assert(cJSON_GetArraySize(games) == 3);

    /* The same path on two cards is two games. */
    game = find_game(games, "primary", "GBA/Zelda.gba");
    assert(game);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "title")->valuestring, "My Zelda") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "defaultTitle")->valuestring, "Zelda") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "titleOverride")->valuestring, "My Zelda") == 0);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(game, "cardKey")));

    game = find_game(games, "secondary_sd", "GBA/Zelda.gba");
    assert(game);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "title")->valuestring, "Zelda Second") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "cardKey")->valuestring, LIVE_CARD) == 0);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(game, "titleOverride")));

    /* An imported title is the default, not an override. */
    game = find_game(games, "primary", "GBA/imported.gba");
    assert(game);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "title")->valuestring, "Imported Title") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "defaultTitle")->valuestring, "Imported Title") == 0);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(game, "titleOverride")));
    cJSON_Delete(out);
}

static void test_save_and_reset_title(void) {
    fixture fx;
    char reason[64];

    fixture_init(&fx, kLibrarySql);
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", "  My \n Zelda\t", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
    /* What the launcher can draw on one wrapped line. */
    expect_setting(&fx, 1, "display_name", "My Zelda");
    expect_setting(&fx, 2, "display_name", NULL);

    /* Reset deletes the row, so later imported or scanned names show through. */
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", NULL, "My Zelda"), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 1, "display_name", NULL);

    /* A value that normalizes to nothing is a reset too, not an empty override. */
    exec_sql(fx.db_path, "INSERT INTO game_settings VALUES (1, 'display_name', 'Again', 1);");
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", " \n ", "Again"), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 1, "display_name", NULL);
}

static void test_stale_edit_is_rejected(void) {
    fixture fx;
    char reason[64];
    cJSON *changes = cJSON_CreateArray();
    cJSON *conflicts = NULL;
    const char *problem = NULL;
    int saved = -1;
    const cJSON *conflict;

    fixture_init(&fx, kLibrarySql);
    exec_sql(fx.db_path, "INSERT INTO game_settings VALUES (1, 'display_name', 'From handheld', 1);");

    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", "From browser", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(strcmp(reason, "stale") == 0);
    expect_setting(&fx, 1, "display_name", "From handheld");

    /* A clear is checked as well. */
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", NULL, "Old"), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    expect_setting(&fx, 1, "display_name", "From handheld");

    /* An empty override and no override aren't the same state. */
    exec_sql(fx.db_path, "INSERT INTO game_settings VALUES (6, 'display_name', '', 1);");
    assert(apply_one(&fx, make_change("primary", "GBA/imported.gba", NULL, "title", "New", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(apply_one(&fx, make_change("primary", "GBA/imported.gba", NULL, "title", "New", ""), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);

    /* One stale change stops the whole batch, and says what's there now. */
    cJSON_AddItemToArray(changes, make_change("secondary_sd", "GBA/Zelda.gba", LIVE_CARD, "title", "Fine", NULL));
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/Zelda.gba", NULL, "title", "Stale", "Old"));
    assert(cs_metadata_apply(&fx.paths, &fx.gba, changes, 0, &saved, &conflicts, &problem) == CS_METADATA_CONFLICT);
    assert(saved == 0);
    assert(cJSON_GetArraySize(conflicts) == 1);
    conflict = cJSON_GetArrayItem(conflicts, 0);
    assert(cJSON_GetObjectItemCaseSensitive(conflict, "index")->valueint == 1);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(conflict, "current")->valuestring, "From handheld") == 0);
    expect_setting(&fx, 2, "display_name", NULL);
    cJSON_Delete(conflicts);
    cJSON_Delete(changes);
}

static void test_card_identity(void) {
    fixture fx;
    char reason[64];
    char second_roms[PATH_MAX];
    cJSON *out;

    fixture_init(&fx, kLibrarySql);

    /* Same path, same source, another card since this browser loaded the list. */
    assert(apply_one(&fx, make_change("secondary_sd", "GBA/Zelda.gba", "card-key-old", "title", "Mine", NULL), 0,
                     reason, sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(strcmp(reason, "card_changed") == 0);
    expect_setting(&fx, 2, "display_name", NULL);

    assert(apply_one(&fx, make_change("secondary_sd", "GBA/Zelda.gba", LIVE_CARD, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 2, "display_name", "Mine");
    expect_setting(&fx, 1, "display_name", NULL);

    /* A card with no Roms folder in the slot lists nothing and accepts nothing. */
    assert(snprintf(second_roms, sizeof(second_roms), "%s/Roms", fx.secondary) > 0);
    assert(rmdir(second_roms) == 0);
    out = cJSON_CreateObject();
    assert(cs_metadata_list_games(&fx.paths, &fx.gba, 0, out) == CS_METADATA_OK);
    assert(!find_game(cJSON_GetObjectItemCaseSensitive(out, "games"), "secondary_sd", "GBA/Zelda.gba"));
    cJSON_Delete(out);
    assert(apply_one(&fx, make_change("secondary_sd", "GBA/Zelda.gba", LIVE_CARD, "title", "Other", "Mine"), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(strcmp(reason, "card_changed") == 0);
    expect_setting(&fx, 2, "display_name", "Mine");
}

/* A Leaf from before library_cards can't say which card is in the slot. */
static void test_no_card_registry_means_primary_only(void) {
    fixture fx;
    char reason[64];
    cJSON *out = cJSON_CreateObject();

    fixture_init(&fx, kLibrarySql);
    exec_sql(fx.db_path, "DROP TABLE library_cards; DROP TABLE hidden_roms;");
    assert(cs_metadata_list_games(&fx.paths, &fx.gba, 0, out) == CS_METADATA_OK);
    assert(!find_game(cJSON_GetObjectItemCaseSensitive(out, "games"), "secondary_sd", "GBA/Zelda.gba"));
    assert(find_game(cJSON_GetObjectItemCaseSensitive(out, "games"), "primary", "GBA/Zelda.gba"));
    cJSON_Delete(out);

    assert(apply_one(&fx, make_change("secondary_sd", "GBA/Zelda.gba", "any", "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
}

static void test_unlisted_and_unknown_rows_are_left_alone(void) {
    fixture fx;
    char reason[64];

    fixture_init(&fx, kLibrarySql);
    assert(apply_one(&fx, make_change("primary", "GBA/Hidden.gba", NULL, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(strcmp(reason, "not_listed") == 0);
    expect_setting(&fx, 4, "display_name", NULL);

    assert(apply_one(&fx, make_change("primary", "GBA/Nope.gba", NULL, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(strcmp(reason, "not_found") == 0);

    /* Another platform's game can't be reached through this one. */
    assert(apply_one(&fx, make_change("primary", "SFC/Other.sfc", NULL, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_CONFLICT);
    expect_setting(&fx, 5, "display_name", NULL);
}

static void test_malformed_changes(void) {
    fixture fx;
    char reason[64];
    char too_long[CS_METADATA_DESCRIPTION_MAX + 2];
    char long_title[CS_METADATA_TITLE_MAX + 2];
    cJSON *change;

    fixture_init(&fx, kLibrarySql);
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "rating", "5", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_INVALID);
    assert(strcmp(reason, "invalid_field") == 0);
    assert(apply_one(&fx, make_change("card-1", "GBA/Parked.gba", NULL, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_INVALID);
    assert(strcmp(reason, "invalid_source") == 0);

    memset(long_title, 'x', sizeof(long_title) - 1);
    long_title[sizeof(long_title) - 1] = '\0';
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", long_title, NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_INVALID);
    assert(strcmp(reason, "value_too_long") == 0);
    long_title[sizeof(long_title) - 2] = '\0';
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", long_title, NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_OK);

    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "description", too_long, NULL), CS_METADATA_ALLOW_DESCRIPTION, reason,
                     sizeof(reason))
           == CS_METADATA_INVALID);

    change = make_change("primary", "GBA/Zelda.gba", NULL, "title", "Mine", NULL);
    cJSON_DeleteItemFromObject(change, "expected");
    assert(apply_one(&fx, change, 0, reason, sizeof(reason)) == CS_METADATA_INVALID);
}

static void test_description_override(void) {
    fixture fx;
    char reason[64];
    cJSON *out;

    fixture_init(&fx, kLibrarySql);

    /* A Leaf that wouldn't show it doesn't get it. */
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "description", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_INVALID);
    assert(strcmp(reason, "description_unsupported") == 0);
    expect_setting(&fx, 1, "user_synopsis", NULL);

    out = cJSON_CreateObject();
    assert(cs_metadata_get_game(&fx.paths, &fx.gba, "primary", "GBA/Zelda.gba", out) == CS_METADATA_OK);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(out, "descriptionOverride")));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(out, "defaultDescription")->valuestring, "Scraped text") == 0);
    cJSON_Delete(out);

    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "description", "Mine\n\nToo", NULL), CS_METADATA_ALLOW_DESCRIPTION, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 1, "user_synopsis", "Mine Too");
    expect_setting(&fx, 1, "ss.synopsis", "Scraped text");

    out = cJSON_CreateObject();
    assert(cs_metadata_get_game(&fx.paths, &fx.gba, "primary", "GBA/Zelda.gba", out) == CS_METADATA_OK);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(out, "descriptionOverride")->valuestring, "Mine Too") == 0);
    cJSON_Delete(out);

    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "description", NULL, "Mine Too"), CS_METADATA_ALLOW_DESCRIPTION, reason,
                     sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 1, "user_synopsis", NULL);
    expect_setting(&fx, 1, "ss.synopsis", "Scraped text");

    out = cJSON_CreateObject();
    assert(cs_metadata_get_game(&fx.paths, &fx.gba, "primary", "GBA/Nope.gba", out) == CS_METADATA_CONFLICT);
    cJSON_Delete(out);
}

static void test_list_includes_descriptions_on_request(void) {
    fixture fx;
    cJSON *out = cJSON_CreateObject();
    const cJSON *game;

    fixture_init(&fx, kLibrarySql);
    exec_sql(fx.db_path, "INSERT INTO game_settings VALUES (6, 'user_synopsis', 'Mine', 1);");
    assert(cs_metadata_list_games(&fx.paths, &fx.gba, 1, out) == CS_METADATA_OK);
    game = find_game(cJSON_GetObjectItemCaseSensitive(out, "games"), "primary", "GBA/Zelda.gba");
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(game, "descriptionOverride")));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "defaultDescription")->valuestring, "Scraped text") == 0);
    game = find_game(cJSON_GetObjectItemCaseSensitive(out, "games"), "primary", "GBA/imported.gba");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "descriptionOverride")->valuestring, "Mine") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(game, "defaultDescription")->valuestring, "") == 0);
    cJSON_Delete(out);

    out = cJSON_CreateObject();
    assert(cs_metadata_list_games(&fx.paths, &fx.gba, 0, out) == CS_METADATA_OK);
    game = find_game(cJSON_GetObjectItemCaseSensitive(out, "games"), "primary", "GBA/Zelda.gba");
    assert(!cJSON_GetObjectItemCaseSensitive(game, "defaultDescription"));
    cJSON_Delete(out);
}

/* Apply through the same steps the route takes, keeping the snapshot on success. */
static cs_metadata_status bulk_apply(fixture *fx, cJSON *changes) {
    cJSON *snapshot = cs_metadata_undo_build(&fx->gba, changes, ALLOW_ALL);
    cJSON *conflicts = NULL;
    const char *problem = NULL;
    int saved = 0;
    cs_metadata_status status;

    assert(snapshot);
    assert(cs_metadata_undo_prepare(&fx->paths, snapshot) == 0);
    cJSON_Delete(snapshot);
    status = cs_metadata_apply(&fx->paths, &fx->gba, changes, ALLOW_ALL, &saved, &conflicts, &problem);
    if (status == CS_METADATA_OK) {
        assert(cs_metadata_undo_commit(&fx->paths) == 0);
    } else {
        cs_metadata_undo_discard(&fx->paths);
    }
    cJSON_Delete(conflicts);
    cJSON_Delete(changes);
    return status;
}

static int undo_now(fixture *fx, cJSON **skipped_out) {
    cJSON *snapshot = cs_metadata_undo_read(&fx->paths, "GBA");
    cJSON *skipped = cJSON_CreateArray();
    int restored = -1;

    assert(snapshot);
    assert(cs_metadata_undo_apply(&fx->paths, &fx->gba, snapshot, &restored, skipped) == CS_METADATA_OK);
    cJSON_Delete(snapshot);
    cs_metadata_undo_clear(&fx->paths);
    *skipped_out = skipped;
    return restored;
}

static void test_undo_restores_raw_state(void) {
    fixture fx;
    cJSON *changes = cJSON_CreateArray();
    cJSON *skipped = NULL;

    fixture_init(&fx, kLibrarySql);
    /* Spacing and emptiness the editor wouldn't write itself. */
    exec_sql(fx.db_path,
             "INSERT INTO game_settings VALUES (1, 'display_name', 'My  Zelda', 1);"
             "INSERT INTO game_settings VALUES (6, 'display_name', '', 1);");
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/Zelda.gba", NULL, "title", "New", "My  Zelda"));
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/imported.gba", NULL, "title", "New 2", ""));
    cJSON_AddItemToArray(changes, make_change("secondary_sd", "GBA/Zelda.gba", LIVE_CARD, "title", "New 3", NULL));
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/Zelda.gba", NULL, "description", "Words", NULL));
    assert(bulk_apply(&fx, changes) == CS_METADATA_OK);
    expect_setting(&fx, 1, "display_name", "New");
    expect_setting(&fx, 2, "display_name", "New 3");
    expect_setting(&fx, 1, "user_synopsis", "Words");

    assert(undo_now(&fx, &skipped) == 4);
    assert(cJSON_GetArraySize(skipped) == 0);
    cJSON_Delete(skipped);
    /* Each back as it was: raw text, an empty row, and no row at all. Nothing frozen. */
    expect_setting(&fx, 1, "display_name", "My  Zelda");
    expect_setting(&fx, 6, "display_name", "");
    expect_setting(&fx, 2, "display_name", NULL);
    expect_setting(&fx, 1, "user_synopsis", NULL);
    expect_setting(&fx, 1, "ss.synopsis", "Scraped text");
    assert(cs_metadata_undo_read(&fx.paths, "GBA") == NULL);
}

static void test_undo_keeps_later_edits(void) {
    fixture fx;
    cJSON *changes = cJSON_CreateArray();
    cJSON *skipped = NULL;
    const cJSON *row;

    fixture_init(&fx, kLibrarySql);
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/Zelda.gba", NULL, "title", "Bulk", NULL));
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/imported.gba", NULL, "title", "Bulk 2", NULL));
    cJSON_AddItemToArray(changes, make_change("secondary_sd", "GBA/Zelda.gba", LIVE_CARD, "title", "Bulk 3", NULL));
    assert(bulk_apply(&fx, changes) == CS_METADATA_OK);

    /* Edited on the handheld since, and the second card swapped. */
    exec_sql(fx.db_path,
             "UPDATE game_settings SET value='Handheld' WHERE game_id=1 AND key='display_name';"
             "UPDATE library_cards SET card_key='card-key-other' WHERE is_live=1;");
    assert(undo_now(&fx, &skipped) == 1);
    assert(cJSON_GetArraySize(skipped) == 2);
    row = cJSON_GetArrayItem(skipped, 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(row, "reason")->valuestring, "changed_since") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(row, "path")->valuestring, "GBA/Zelda.gba") == 0);
    row = cJSON_GetArrayItem(skipped, 1);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(row, "reason")->valuestring, "card_changed") == 0);
    cJSON_Delete(skipped);
    expect_setting(&fx, 1, "display_name", "Handheld");
    expect_setting(&fx, 2, "display_name", "Bulk 3");
    expect_setting(&fx, 6, "display_name", NULL);
}

static void test_rejected_apply_keeps_previous_undo(void) {
    fixture fx;
    cJSON *first = cJSON_CreateArray();
    cJSON *rejected = cJSON_CreateArray();
    cJSON *snapshot;
    cJSON *skipped = NULL;

    fixture_init(&fx, kLibrarySql);
    cJSON_AddItemToArray(first, make_change("primary", "GBA/Zelda.gba", NULL, "title", "First", NULL));
    assert(bulk_apply(&fx, first) == CS_METADATA_OK);

    cJSON_AddItemToArray(rejected, make_change("primary", "GBA/Zelda.gba", NULL, "title", "Second", "Wrong"));
    assert(bulk_apply(&fx, rejected) == CS_METADATA_CONFLICT);
    expect_setting(&fx, 1, "display_name", "First");

    snapshot = cs_metadata_undo_read(&fx.paths, "GBA");
    assert(snapshot);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(snapshot, "entries")) == 1);
    cJSON_Delete(snapshot);

    /* A snapshot belongs to its own platform. */
    assert(cs_metadata_undo_read(&fx.paths, "SFC") == NULL);

    assert(undo_now(&fx, &skipped) == 1);
    cJSON_Delete(skipped);
    expect_setting(&fx, 1, "display_name", NULL);
}

static cJSON *folder_change(const char *path, const char *value, const char *expected) {
    cJSON *change = cJSON_CreateObject();

    cJSON_AddStringToObject(change, "path", path);
    cJSON_AddStringToObject(change, "field", "folderRank");
    if (value) {
        cJSON_AddStringToObject(change, "value", value);
    } else {
        cJSON_AddNullToObject(change, "value");
    }
    if (expected) {
        cJSON_AddStringToObject(change, "expected", expected);
    } else {
        cJSON_AddNullToObject(change, "expected");
    }
    return change;
}

static void expect_folder_rank(const fixture *fx, const char *key, const char *want) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int found;

    assert(sqlite3_open(fx->db_path, &db) == SQLITE_OK);
    assert(sqlite3_prepare_v2(db, "SELECT value FROM system_settings WHERE system='GBA' AND key=?;", -1, &stmt, NULL)
           == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    found = sqlite3_step(stmt) == SQLITE_ROW;
    if (!want) {
        assert(!found);
    } else {
        assert(found && strcmp((const char *) sqlite3_column_text(stmt, 0), want) == 0);
    }
    sqlite3_finalize(stmt);
    assert(sqlite3_close(db) == SQLITE_OK);
}

static void test_ranks(void) {
    fixture fx;
    char reason[64];
    cJSON *changes = cJSON_CreateArray();
    cJSON *skipped = NULL;
    char system[64];

    fixture_init(&fx, kLibrarySql);
    assert(cs_metadata_system_id(&fx.paths, &fx.gba, system, sizeof(system)) == CS_METADATA_OK);
    assert(strcmp(system, "GBA") == 0);

    /* A daemon that can't honor them doesn't get them. */
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "rank", "1", NULL), CS_METADATA_ALLOW_DESCRIPTION,
                     reason, sizeof(reason))
           == CS_METADATA_INVALID);
    assert(strcmp(reason, "order_unsupported") == 0);
    assert(apply_one(&fx, folder_change("Hacks", "1", NULL), CS_METADATA_ALLOW_DESCRIPTION, reason, sizeof(reason))
           == CS_METADATA_INVALID);

    /* Only positive whole numbers. */
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "rank", "0", NULL), ALLOW_ALL, reason, sizeof(reason))
           == CS_METADATA_INVALID);
    assert(strcmp(reason, "invalid_rank") == 0);
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "rank", "1x", NULL), ALLOW_ALL, reason, sizeof(reason))
           == CS_METADATA_INVALID);
    assert(apply_one(&fx, folder_change("/abs", "1", NULL), ALLOW_ALL, reason, sizeof(reason)) == CS_METADATA_INVALID);

    /* A game and a folder in one apply; the folder's key is the merged, lowercase path. */
    cJSON_AddItemToArray(changes, make_change("primary", "GBA/Zelda.gba", NULL, "rank", "2", NULL));
    cJSON_AddItemToArray(changes, folder_change("Hacks/Translated", "1", NULL));
    assert(bulk_apply(&fx, changes) == CS_METADATA_OK);
    expect_setting(&fx, 1, "sort_rank", "2");
    expect_folder_rank(&fx, "folder_rank:hacks/translated", "1");

    /* Stale ranks are rejected, for folders too. */
    assert(apply_one(&fx, folder_change("Hacks/Translated", "3", "9"), ALLOW_ALL, reason, sizeof(reason))
           == CS_METADATA_CONFLICT);
    assert(strcmp(reason, "stale") == 0);
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "rank", "3", "9"), ALLOW_ALL, reason, sizeof(reason))
           == CS_METADATA_CONFLICT);

    /* Junk isn't a pin to the launcher, so it can't block a save either. */
    exec_sql(fx.db_path, "INSERT INTO game_settings VALUES (6, 'sort_rank', 'soon', 1);");
    assert(apply_one(&fx, make_change("primary", "GBA/imported.gba", NULL, "rank", "4", NULL), ALLOW_ALL, reason, sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 6, "sort_rank", "4");

    /* Undo takes a rank back out, and a folder's. */
    assert(undo_now(&fx, &skipped) == 2);
    cJSON_Delete(skipped);
    expect_setting(&fx, 1, "sort_rank", NULL);
    expect_folder_rank(&fx, "folder_rank:hacks/translated", NULL);

    /* A rank on a game or folder that isn't listed stays when another level is saved. */
    exec_sql(fx.db_path,
             "INSERT INTO game_settings VALUES (3, 'sort_rank', '1', 1);"
             "INSERT INTO system_settings VALUES ('GBA', 'folder_rank:gone', '1', 1);");
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "rank", "1", NULL), ALLOW_ALL, reason, sizeof(reason))
           == CS_METADATA_OK);
    expect_setting(&fx, 3, "sort_rank", "1");
    expect_folder_rank(&fx, "folder_rank:gone", "1");
}

static void test_card_keys_on_rows(void) {
    fixture fx;
    cJSON *rows = cJSON_CreateArray();
    cJSON *primary = cJSON_CreateObject();
    cJSON *second = cJSON_CreateObject();
    cJSON *folder = cJSON_CreateObject();

    fixture_init(&fx, kLibrarySql);
    cJSON_AddStringToObject(primary, "source", "primary");
    cJSON_AddStringToObject(second, "source", "secondary_sd");
    cJSON_AddStringToObject(folder, "kind", "folder");
    cJSON_AddItemToArray(rows, primary);
    cJSON_AddItemToArray(rows, second);
    cJSON_AddItemToArray(rows, folder);
    assert(cs_metadata_add_card_keys(&fx.paths, rows) == CS_METADATA_OK);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(primary, "cardKey")));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(second, "cardKey")->valuestring, LIVE_CARD) == 0);
    assert(!cJSON_GetObjectItemCaseSensitive(folder, "cardKey"));
    cJSON_Delete(rows);
}

/* A library from before source_id can't identify a row the way the editor needs. */
static void test_old_library_is_unavailable(void) {
    fixture fx;
    char reason[64];
    cJSON *out = cJSON_CreateObject();

    fixture_init(&fx,
                 "CREATE TABLE games (id INTEGER PRIMARY KEY, system TEXT NOT NULL, name TEXT NOT NULL,"
                 " rom_path TEXT NOT NULL UNIQUE);"
                 "INSERT INTO games (system, name, rom_path) VALUES ('GBA', 'Zelda', 'Roms/GBA/Zelda.gba');");
    assert(cs_metadata_list_games(&fx.paths, &fx.gba, 0, out) == CS_METADATA_UNAVAILABLE);
    cJSON_Delete(out);
    assert(apply_one(&fx, make_change("primary", "GBA/Zelda.gba", NULL, "title", "Mine", NULL), 0, reason,
                     sizeof(reason))
           == CS_METADATA_UNAVAILABLE);
}

static void test_normalize(void) {
    char text[] = "\t  One\r\n\n two  \x7f three \n";
    char empty[] = " \n\t ";

    cs_metadata_normalize_text(text);
    assert(strcmp(text, "One two three") == 0);
    cs_metadata_normalize_text(empty);
    assert(strcmp(empty, "") == 0);
}

int main(void) {
    test_normalize();
    test_list_covers_listed_games();
    test_save_and_reset_title();
    test_stale_edit_is_rejected();
    test_card_identity();
    test_no_card_registry_means_primary_only();
    test_unlisted_and_unknown_rows_are_left_alone();
    test_malformed_changes();
    test_description_override();
    test_list_includes_descriptions_on_request();
    test_undo_restores_raw_state();
    test_undo_keeps_later_edits();
    test_rejected_apply_keeps_previous_undo();
    test_ranks();
    test_card_keys_on_rows();
    test_old_library_is_unavailable();
    return 0;
}
