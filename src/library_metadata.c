#include "cs_library_metadata.h"

#include "cs_platforms.h"

#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CS_METADATA_SOURCE_PRIMARY "primary"
#define CS_METADATA_SOURCE_SECONDARY "secondary_sd"
#define CS_METADATA_KEY_TITLE "display_name"
#define CS_METADATA_KEY_DESCRIPTION "user_synopsis"
#define CS_METADATA_KEY_SCRAPED_DESCRIPTION "ss.synopsis"
/* A writer can hold the library for a while, as when Leaf refreshes Pak Rat. */
#define CS_METADATA_BUSY_TIMEOUT_MS 5000
#define CS_METADATA_SYSTEM_COUNT 6

typedef struct {
    int has_hidden_roms;
    int has_live_card;
    char live_card_key[128];
    int second_card_listed;
} cs_metadata_library;

static int cs_metadata_open(const cs_paths *paths, int flags, sqlite3 **db_out) {
    char db_path[CS_PATH_MAX];
    sqlite3 *db = NULL;

    *db_out = NULL;
    if (!paths || paths->internal_data_root[0] == '\0') {
        return -1;
    }
    if (snprintf(db_path, sizeof(db_path), "%s/library.db", paths->internal_data_root)
        >= (int) sizeof(db_path)) {
        return -1;
    }
    if (access(db_path, R_OK) != 0 || sqlite3_open_v2(db_path, &db, flags, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    sqlite3_busy_timeout(db, CS_METADATA_BUSY_TIMEOUT_MS);
    *db_out = db;
    return 0;
}

static int cs_metadata_table_exists(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int exists = 0;

    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", -1, &stmt, NULL)
        == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        exists = sqlite3_step(stmt) == SQLITE_ROW;
    }
    sqlite3_finalize(stmt);
    return exists;
}

static int cs_metadata_games_have_identity(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int ok = sqlite3_prepare_v2(db, "SELECT source_id, rom_relpath FROM games LIMIT 0;", -1, &stmt, NULL)
             == SQLITE_OK;

    sqlite3_finalize(stmt);
    return ok;
}

/* Fills library from the open database. Returns -1 for a library from before the
 * source_id and game_settings the editor relies on. */
static int cs_metadata_read_library(const cs_paths *paths, sqlite3 *db, cs_metadata_library *library) {
    struct stat st;

    memset(library, 0, sizeof(*library));
    if (!cs_metadata_games_have_identity(db) || !cs_metadata_table_exists(db, "game_settings")) {
        return -1;
    }
    library->has_hidden_roms = cs_metadata_table_exists(db, "hidden_roms");
    if (cs_metadata_table_exists(db, "library_cards")) {
        sqlite3_stmt *stmt = NULL;

        if (sqlite3_prepare_v2(db, "SELECT card_key FROM library_cards WHERE is_live=1;", -1, &stmt, NULL)
                == SQLITE_OK
            && sqlite3_step(stmt) == SQLITE_ROW) {
            const char *key = (const char *) sqlite3_column_text(stmt, 0);

            if (key && key[0] != '\0') {
                snprintf(library->live_card_key, sizeof(library->live_card_key), "%s", key);
                library->has_live_card = 1;
            }
        }
        sqlite3_finalize(stmt);
    }
    library->second_card_listed = library->has_live_card && paths->source_count > 1
                                  && stat(paths->sources[1].roms_root, &st) == 0 && S_ISDIR(st.st_mode);
    return 0;
}

static const char *cs_metadata_blank(const char *value) {
    return value ? value : "";
}

/* The same names cs_library_db_count_roms_for_platform matches a platform's games by. */
static void cs_metadata_bind_systems(sqlite3_stmt *stmt, int first_index, const cs_platform_info *platform) {
    const cs_platform_info *fallback = cs_platform_find(platform->tag);
    const char *systems[CS_METADATA_SYSTEM_COUNT] = {
        cs_metadata_blank(platform->primary_code),
        cs_metadata_blank(platform->tag),
        cs_metadata_blank(platform->rom_directory),
        cs_metadata_blank(platform->canonical_rom_directory),
        fallback ? cs_metadata_blank(fallback->primary_code) : "",
        fallback ? cs_metadata_blank(fallback->rom_directory) : "",
    };

    for (int i = 0; i < CS_METADATA_SYSTEM_COUNT; i++) {
        sqlite3_bind_text(stmt, first_index + i, systems[i], -1, SQLITE_STATIC);
    }
}

void cs_metadata_normalize_text(char *text) {
    size_t write = 0;
    int pending_space = 0;

    if (!text) {
        return;
    }
    for (size_t read = 0; text[read] != '\0'; read++) {
        unsigned char c = (unsigned char) text[read];

        if (c <= 0x20 || c == 0x7F) {
            pending_space = write > 0;
            continue;
        }
        if (pending_space) {
            text[write++] = ' ';
            pending_space = 0;
        }
        text[write++] = (char) c;
    }
    text[write] = '\0';
}

static void cs_metadata_add_nullable(cJSON *object, const char *name, const char *value) {
    if (value) {
        cJSON_AddStringToObject(object, name, value);
    } else {
        cJSON_AddNullToObject(object, name);
    }
}

cs_metadata_status cs_metadata_list_games(const cs_paths *paths,
                                          const cs_platform_info *platform,
                                          int include_descriptions,
                                          cJSON *out) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    cs_metadata_library library;
    cJSON *games;
    char sql[1800];
    int written;
    int step_rc;

    if (!paths || !platform || !out) {
        return CS_METADATA_INTERNAL;
    }
    if (cs_metadata_open(paths, SQLITE_OPEN_READONLY, &db) != 0) {
        return CS_METADATA_UNAVAILABLE;
    }
    if (cs_metadata_read_library(paths, db, &library) != 0) {
        sqlite3_close(db);
        return CS_METADATA_UNAVAILABLE;
    }

    written = snprintf(
        sql,
        sizeof(sql),
        "SELECT g.source_id, g.rom_relpath, g.name, manual.value, imported.value%s "
        "FROM games g "
        "LEFT JOIN game_settings manual ON manual.game_id=g.id AND manual.key='" CS_METADATA_KEY_TITLE "' "
        "LEFT JOIN game_settings imported ON imported.game_id=g.id AND imported.key='imported_display_name' "
        "%s"
        "WHERE g.system IN (?, ?, ?, ?, ?, ?) AND (g.source_id='" CS_METADATA_SOURCE_PRIMARY "'%s)%s "
        "ORDER BY g.id;",
        include_descriptions ? ", own.value, scraped.value" : "",
        include_descriptions
            ? "LEFT JOIN game_settings own ON own.game_id=g.id AND own.key='" CS_METADATA_KEY_DESCRIPTION "' "
              "LEFT JOIN game_settings scraped ON scraped.game_id=g.id "
              "AND scraped.key='" CS_METADATA_KEY_SCRAPED_DESCRIPTION "' "
            : "",
        library.second_card_listed ? " OR g.source_id='" CS_METADATA_SOURCE_SECONDARY "'" : "",
        library.has_hidden_roms
            ? " AND NOT EXISTS (SELECT 1 FROM hidden_roms h WHERE h.source_id=g.source_id "
              "AND h.rom_relpath=g.rom_relpath AND h.member='')"
            : "");
    if (written < 0 || written >= (int) sizeof(sql)
        || sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return CS_METADATA_INTERNAL;
    }
    cs_metadata_bind_systems(stmt, 1, platform);

    games = cJSON_AddArrayToObject(out, "games");
    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *source = (const char *) sqlite3_column_text(stmt, 0);
        const char *path = (const char *) sqlite3_column_text(stmt, 1);
        const char *name = (const char *) sqlite3_column_text(stmt, 2);
        const char *manual = (const char *) sqlite3_column_text(stmt, 3);
        const char *imported = (const char *) sqlite3_column_text(stmt, 4);
        const char *default_title = imported && imported[0] != '\0' ? imported : cs_metadata_blank(name);
        cJSON *game = cJSON_CreateObject();

        if (!source || !path || !game || !cJSON_AddItemToArray(games, game)) {
            cJSON_Delete(game);
            continue;
        }
        cJSON_AddStringToObject(game, "source", source);
        cJSON_AddStringToObject(game, "path", path);
        cs_metadata_add_nullable(game,
                                 "cardKey",
                                 strcmp(source, CS_METADATA_SOURCE_SECONDARY) == 0 ? library.live_card_key : NULL);
        cJSON_AddStringToObject(game, "title", manual && manual[0] != '\0' ? manual : default_title);
        cJSON_AddStringToObject(game, "defaultTitle", default_title);
        cs_metadata_add_nullable(game, "titleOverride", manual);
        if (include_descriptions) {
            cs_metadata_add_nullable(game, "descriptionOverride", (const char *) sqlite3_column_text(stmt, 5));
            cJSON_AddStringToObject(game,
                                    "defaultDescription",
                                    cs_metadata_blank((const char *) sqlite3_column_text(stmt, 6)));
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return step_rc == SQLITE_DONE ? CS_METADATA_OK : CS_METADATA_INTERNAL;
}

/* The id of the game a change names, or 0 if the platform has no such game. */
static int cs_metadata_find_game(sqlite3 *db,
                                 const cs_platform_info *platform,
                                 const char *source,
                                 const char *path) {
    sqlite3_stmt *stmt = NULL;
    int id = 0;

    if (sqlite3_prepare_v2(db,
                           "SELECT id FROM games WHERE source_id=? AND rom_relpath=? "
                           "AND system IN (?, ?, ?, ?, ?, ?);",
                           -1,
                           &stmt,
                           NULL)
        == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, source, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
        cs_metadata_bind_systems(stmt, 3, platform);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return id;
}

/* Copies a game's raw setting. Returns 1 when the row exists, 0 when it doesn't,
 * which is what tells a cleared override from one holding an empty string. */
static int cs_metadata_read_setting(sqlite3 *db, int game_id, const char *key, char **value_out) {
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    *value_out = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM game_settings WHERE game_id=? AND key=?;", -1, &stmt, NULL)
        != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, game_id);
    sqlite3_bind_text(stmt, 2, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *value = (const char *) sqlite3_column_text(stmt, 0);

        *value_out = strdup(value ? value : "");
        found = *value_out ? 1 : -1;
    }
    sqlite3_finalize(stmt);
    return found;
}

cs_metadata_status cs_metadata_get_game(const cs_paths *paths,
                                        const cs_platform_info *platform,
                                        const char *source,
                                        const char *path,
                                        cJSON *out) {
    sqlite3 *db = NULL;
    cs_metadata_library library;
    char *override_value = NULL;
    char *scraped = NULL;
    int game_id;
    int override_found;
    int scraped_found;
    cs_metadata_status status = CS_METADATA_OK;

    if (!paths || !platform || !source || !path || !out) {
        return CS_METADATA_INTERNAL;
    }
    if (cs_metadata_open(paths, SQLITE_OPEN_READONLY, &db) != 0) {
        return CS_METADATA_UNAVAILABLE;
    }
    if (cs_metadata_read_library(paths, db, &library) != 0) {
        sqlite3_close(db);
        return CS_METADATA_UNAVAILABLE;
    }
    game_id = cs_metadata_find_game(db, platform, source, path);
    if (game_id <= 0) {
        sqlite3_close(db);
        return CS_METADATA_CONFLICT;
    }
    override_found = cs_metadata_read_setting(db, game_id, CS_METADATA_KEY_DESCRIPTION, &override_value);
    scraped_found = cs_metadata_read_setting(db, game_id, CS_METADATA_KEY_SCRAPED_DESCRIPTION, &scraped);
    if (override_found < 0 || scraped_found < 0) {
        status = CS_METADATA_INTERNAL;
    } else {
        cs_metadata_add_nullable(out, "descriptionOverride", override_found ? override_value : NULL);
        cJSON_AddStringToObject(out, "defaultDescription", scraped_found ? scraped : "");
    }
    free(override_value);
    free(scraped);
    sqlite3_close(db);
    return status;
}

static void cs_metadata_add_conflict(cJSON *conflicts,
                                     int index,
                                     const char *reason,
                                     const char *current,
                                     int include_current) {
    cJSON *conflict = cJSON_CreateObject();

    if (!conflict) {
        return;
    }
    cJSON_AddNumberToObject(conflict, "index", index);
    cJSON_AddStringToObject(conflict, "reason", reason);
    if (include_current) {
        cs_metadata_add_nullable(conflict, "current", current);
    }
    if (!cJSON_AddItemToArray(conflicts, conflict)) {
        cJSON_Delete(conflict);
    }
}

static int cs_metadata_game_hidden(sqlite3 *db, const char *source, const char *path) {
    sqlite3_stmt *stmt = NULL;
    int hidden = 0;

    if (sqlite3_prepare_v2(db,
                           "SELECT 1 FROM hidden_roms WHERE source_id=? AND rom_relpath=? AND member='';",
                           -1,
                           &stmt,
                           NULL)
        == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, source, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
        hidden = sqlite3_step(stmt) == SQLITE_ROW;
    }
    sqlite3_finalize(stmt);
    return hidden;
}

/* Why this change can't be applied, or NULL when it can. Fills game_id. */
static const char *cs_metadata_check_identity(sqlite3 *db,
                                              const cs_metadata_library *library,
                                              const cs_platform_info *platform,
                                              const char *source,
                                              const char *path,
                                              const char *card_key,
                                              int *game_id_out) {
    *game_id_out = 0;
    if (strcmp(source, CS_METADATA_SOURCE_SECONDARY) == 0) {
        if (!library->second_card_listed || !card_key || strcmp(card_key, library->live_card_key) != 0) {
            return "card_changed";
        }
    }
    *game_id_out = cs_metadata_find_game(db, platform, source, path);
    if (*game_id_out <= 0) {
        return "not_found";
    }
    if (library->has_hidden_roms && cs_metadata_game_hidden(db, source, path)) {
        return "not_listed";
    }
    return NULL;
}

static int cs_metadata_write_setting(sqlite3 *db, int game_id, const char *key, const char *value) {
    sqlite3_stmt *stmt = NULL;
    int ok;

    if (value) {
        ok = sqlite3_prepare_v2(db,
                                "INSERT INTO game_settings (game_id, key, value, updated_at) "
                                "VALUES (?1, ?2, ?3, strftime('%s','now')) "
                                "ON CONFLICT(game_id, key) DO UPDATE SET value=?3, "
                                "updated_at=strftime('%s','now');",
                                -1,
                                &stmt,
                                NULL)
             == SQLITE_OK;
    } else {
        ok = sqlite3_prepare_v2(db, "DELETE FROM game_settings WHERE game_id=?1 AND key=?2;", -1, &stmt, NULL)
             == SQLITE_OK;
    }
    if (ok) {
        sqlite3_bind_int(stmt, 1, game_id);
        sqlite3_bind_text(stmt, 2, key, -1, SQLITE_STATIC);
        if (value) {
            sqlite3_bind_text(stmt, 3, value, -1, SQLITE_STATIC);
        }
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    return ok ? 0 : -1;
}

typedef struct {
    const char *source;
    const char *path;
    const char *card_key;
    const char *key;
    const char *expected;
    char *value; /* normalized and owned; NULL clears the override */
} cs_metadata_change;

/* Parses one change without touching the library. Returns why it's malformed, or
 * NULL with parsed filled, and parsed->value then needs freeing. */
static const char *cs_metadata_parse_change(const cJSON *change,
                                            int allow_description,
                                            cs_metadata_change *parsed) {
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(change, "source");
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(change, "path");
    const cJSON *card_key = cJSON_GetObjectItemCaseSensitive(change, "cardKey");
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(change, "field");
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(change, "value");
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(change, "expected");
    size_t limit;

    memset(parsed, 0, sizeof(*parsed));
    if (!cJSON_IsString(source) || !cJSON_IsString(path) || !cJSON_IsString(field)
        || (!cJSON_IsNull(value) && !cJSON_IsString(value))
        || (!cJSON_IsNull(expected) && !cJSON_IsString(expected))
        || (card_key && !cJSON_IsNull(card_key) && !cJSON_IsString(card_key))) {
        return "invalid_change";
    }
    if (strcmp(source->valuestring, CS_METADATA_SOURCE_PRIMARY) != 0
        && strcmp(source->valuestring, CS_METADATA_SOURCE_SECONDARY) != 0) {
        return "invalid_source";
    }
    if (strcmp(field->valuestring, "title") == 0) {
        parsed->key = CS_METADATA_KEY_TITLE;
        limit = CS_METADATA_TITLE_MAX;
    } else if (strcmp(field->valuestring, "description") == 0) {
        if (!allow_description) {
            return "description_unsupported";
        }
        parsed->key = CS_METADATA_KEY_DESCRIPTION;
        limit = CS_METADATA_DESCRIPTION_MAX;
    } else {
        return "invalid_field";
    }
    if (cJSON_IsString(value)) {
        char *normalized = strdup(value->valuestring);

        if (!normalized) {
            return "out_of_memory";
        }
        cs_metadata_normalize_text(normalized);
        if (strlen(normalized) > limit) {
            free(normalized);
            return "value_too_long";
        }
        if (normalized[0] != '\0') {
            parsed->value = normalized;
        } else {
            free(normalized);
        }
    }
    parsed->source = source->valuestring;
    parsed->path = path->valuestring;
    parsed->card_key = cJSON_IsString(card_key) ? card_key->valuestring : NULL;
    parsed->expected = cJSON_IsString(expected) ? expected->valuestring : NULL;
    return NULL;
}

static int cs_metadata_expected_matches(const char *expected, const char *current) {
    if (!expected || !current) {
        return !expected && !current;
    }
    return strcmp(expected, current) == 0;
}

/* Adds every reason the changes can't apply to conflicts. Returns -1 if the library
 * couldn't be read. */
static int cs_metadata_collect_conflicts(sqlite3 *db,
                                         const cs_metadata_library *library,
                                         const cs_platform_info *platform,
                                         const cJSON *changes,
                                         int allow_description,
                                         cJSON *conflicts) {
    const cJSON *change;
    int index = 0;

    cJSON_ArrayForEach(change, changes) {
        cs_metadata_change parsed;
        const char *reason;
        char *current = NULL;
        int game_id = 0;

        if (cs_metadata_parse_change(change, allow_description, &parsed)) {
            return -1;
        }
        free(parsed.value);
        reason = cs_metadata_check_identity(db, library, platform, parsed.source, parsed.path, parsed.card_key, &game_id);
        if (reason) {
            cs_metadata_add_conflict(conflicts, index, reason, NULL, 0);
        } else {
            int found = cs_metadata_read_setting(db, game_id, parsed.key, &current);

            if (found < 0) {
                return -1;
            }
            if (!cs_metadata_expected_matches(parsed.expected, found ? current : NULL)) {
                cs_metadata_add_conflict(conflicts, index, "stale", found ? current : NULL, 1);
            }
            free(current);
        }
        index++;
    }
    return 0;
}

static int cs_metadata_write_changes(sqlite3 *db,
                                     const cs_platform_info *platform,
                                     const cJSON *changes,
                                     int allow_description,
                                     int *saved_out) {
    const cJSON *change;
    int saved = 0;

    cJSON_ArrayForEach(change, changes) {
        cs_metadata_change parsed;
        int game_id;
        int rc = -1;

        if (cs_metadata_parse_change(change, allow_description, &parsed)) {
            return -1;
        }
        game_id = cs_metadata_find_game(db, platform, parsed.source, parsed.path);
        if (game_id > 0) {
            rc = cs_metadata_write_setting(db, game_id, parsed.key, parsed.value);
        }
        free(parsed.value);
        if (rc != 0) {
            return -1;
        }
        saved++;
    }
    *saved_out = saved;
    return 0;
}

cs_metadata_status cs_metadata_apply(const cs_paths *paths,
                                     const cs_platform_info *platform,
                                     const cJSON *changes,
                                     int allow_description,
                                     int *saved_out,
                                     cJSON **conflicts_out,
                                     const char **error_out) {
    sqlite3 *db = NULL;
    cs_metadata_library library;
    cJSON *conflicts = NULL;
    const cJSON *change;

    if (!paths || !platform || !cJSON_IsArray(changes) || !saved_out || !conflicts_out || !error_out) {
        return CS_METADATA_INTERNAL;
    }
    *saved_out = 0;
    *conflicts_out = NULL;
    *error_out = NULL;

    /* A malformed request is rejected before the library is opened. */
    cJSON_ArrayForEach(change, changes) {
        cs_metadata_change parsed;
        const char *problem = cs_metadata_parse_change(change, allow_description, &parsed);

        if (problem) {
            *error_out = problem;
            return CS_METADATA_INVALID;
        }
        free(parsed.value);
    }

    if (cs_metadata_open(paths, SQLITE_OPEN_READWRITE, &db) != 0) {
        return CS_METADATA_UNAVAILABLE;
    }
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return CS_METADATA_INTERNAL;
    }
    /* Read inside the lock, so a card reconciled while this waited counts. */
    if (cs_metadata_read_library(paths, db, &library) != 0) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(db);
        return CS_METADATA_UNAVAILABLE;
    }
    conflicts = cJSON_CreateArray();
    if (!conflicts
        || cs_metadata_collect_conflicts(db, &library, platform, changes, allow_description, conflicts) != 0) {
        goto failed;
    }
    if (cJSON_GetArraySize(conflicts) > 0) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(db);
        *conflicts_out = conflicts;
        return CS_METADATA_CONFLICT;
    }
    if (cs_metadata_write_changes(db, platform, changes, allow_description, saved_out) != 0
        || sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        *saved_out = 0;
        goto failed;
    }
    cJSON_Delete(conflicts);
    sqlite3_close(db);
    return CS_METADATA_OK;

failed:
    cJSON_Delete(conflicts);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(db);
    return CS_METADATA_INTERNAL;
}

/* The file names, under the same folder as CS's settings. */
static int cs_metadata_undo_path(const cs_paths *paths, const char *suffix, char *path, size_t path_size) {
    int written;

    if (!paths || paths->shared_state_root[0] == '\0') {
        return -1;
    }
    written = snprintf(path, path_size, "%s/library-metadata-undo.json%s", paths->shared_state_root, suffix);
    return written < 0 || (size_t) written >= path_size ? -1 : 0;
}

static int cs_metadata_ensure_dir(const char *path) {
    char buffer[CS_PATH_MAX];

    if (snprintf(buffer, sizeof(buffer), "%s", path) >= (int) sizeof(buffer)) {
        return -1;
    }
    for (char *cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(buffer, 0775) != 0 && errno != EEXIST) {
                return -1;
            }
            *cursor = '/';
        }
    }
    return mkdir(buffer, 0775) != 0 && errno != EEXIST ? -1 : 0;
}

cJSON *cs_metadata_undo_build(const cs_platform_info *platform, const cJSON *changes, int allow_description) {
    cJSON *root = cJSON_CreateObject();
    cJSON *entries = root ? cJSON_AddArrayToObject(root, "entries") : NULL;
    const cJSON *change;

    if (!platform || !entries) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "tag", platform->tag);
    cJSON_ArrayForEach(change, changes) {
        cs_metadata_change parsed;
        cJSON *entry;
        const cJSON *field = cJSON_GetObjectItemCaseSensitive(change, "field");

        if (cs_metadata_parse_change(change, allow_description, &parsed)) {
            cJSON_Delete(root);
            return NULL;
        }
        entry = cJSON_CreateObject();
        if (!entry || !cJSON_AddItemToArray(entries, entry)) {
            cJSON_Delete(entry);
            free(parsed.value);
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(entry, "source", parsed.source);
        cJSON_AddStringToObject(entry, "path", parsed.path);
        cs_metadata_add_nullable(entry, "cardKey", parsed.card_key);
        cJSON_AddStringToObject(entry, "field", field->valuestring);
        /* Undo puts back the raw prior override, or deletes it if there wasn't one, and
         * only where the apply's own value is still there. */
        cs_metadata_add_nullable(entry, "value", parsed.expected);
        cs_metadata_add_nullable(entry, "expected", parsed.value);
        free(parsed.value);
    }
    return root;
}

int cs_metadata_undo_prepare(const cs_paths *paths, const cJSON *snapshot) {
    char path[CS_PATH_MAX];
    char *body;
    FILE *file;
    int rc = -1;

    if (!snapshot || cs_metadata_undo_path(paths, ".new", path, sizeof(path)) != 0
        || cs_metadata_ensure_dir(paths->shared_state_root) != 0) {
        return -1;
    }
    body = cJSON_PrintUnformatted(snapshot);
    if (!body) {
        return -1;
    }
    file = fopen(path, "wb");
    if (file) {
        size_t len = strlen(body);

        rc = fwrite(body, 1, len, file) == len && fflush(file) == 0 && fsync(fileno(file)) == 0 ? 0 : -1;
        if (fclose(file) != 0) {
            rc = -1;
        }
        if (rc != 0) {
            unlink(path);
        }
    }
    cJSON_free(body);
    return rc;
}

int cs_metadata_undo_commit(const cs_paths *paths) {
    char prepared[CS_PATH_MAX];
    char path[CS_PATH_MAX];

    if (cs_metadata_undo_path(paths, ".new", prepared, sizeof(prepared)) != 0
        || cs_metadata_undo_path(paths, "", path, sizeof(path)) != 0) {
        return -1;
    }
    return rename(prepared, path);
}

void cs_metadata_undo_discard(const cs_paths *paths) {
    char path[CS_PATH_MAX];

    if (cs_metadata_undo_path(paths, ".new", path, sizeof(path)) == 0) {
        unlink(path);
    }
}

void cs_metadata_undo_clear(const cs_paths *paths) {
    char path[CS_PATH_MAX];

    if (cs_metadata_undo_path(paths, "", path, sizeof(path)) == 0) {
        unlink(path);
    }
}

cJSON *cs_metadata_undo_read(const cs_paths *paths, const char *tag) {
    char path[CS_PATH_MAX];
    FILE *file;
    long size;
    char *body;
    cJSON *root = NULL;
    const cJSON *stored_tag;

    if (!tag || cs_metadata_undo_path(paths, "", path, sizeof(path)) != 0) {
        return NULL;
    }
    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0) {
        body = malloc((size_t) size + 1);
        if (body) {
            if (fread(body, 1, (size_t) size, file) == (size_t) size) {
                body[size] = '\0';
                root = cJSON_Parse(body);
            }
            free(body);
        }
    }
    fclose(file);
    stored_tag = cJSON_GetObjectItemCaseSensitive(root, "tag");
    if (!cJSON_IsString(stored_tag) || strcmp(stored_tag->valuestring, tag) != 0
        || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "entries"))) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static void cs_metadata_add_skipped(cJSON *skipped, const char *path, const char *field, const char *reason) {
    cJSON *row = cJSON_CreateObject();

    if (!row) {
        return;
    }
    cJSON_AddStringToObject(row, "path", path);
    cJSON_AddStringToObject(row, "field", field);
    cJSON_AddStringToObject(row, "reason", reason);
    if (!cJSON_AddItemToArray(skipped, row)) {
        cJSON_Delete(row);
    }
}

cs_metadata_status cs_metadata_undo_apply(const cs_paths *paths,
                                          const cs_platform_info *platform,
                                          const cJSON *snapshot,
                                          int *restored_out,
                                          cJSON *skipped) {
    sqlite3 *db = NULL;
    cs_metadata_library library;
    const cJSON *entry;
    int restored = 0;

    if (!paths || !platform || !snapshot || !restored_out || !cJSON_IsArray(skipped)) {
        return CS_METADATA_INTERNAL;
    }
    *restored_out = 0;
    if (cs_metadata_open(paths, SQLITE_OPEN_READWRITE, &db) != 0) {
        return CS_METADATA_UNAVAILABLE;
    }
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return CS_METADATA_INTERNAL;
    }
    if (cs_metadata_read_library(paths, db, &library) != 0) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(db);
        return CS_METADATA_UNAVAILABLE;
    }

    cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(snapshot, "entries")) {
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(entry, "source");
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(entry, "path");
        const cJSON *card_key = cJSON_GetObjectItemCaseSensitive(entry, "cardKey");
        const cJSON *field = cJSON_GetObjectItemCaseSensitive(entry, "field");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(entry, "value");
        const cJSON *expected = cJSON_GetObjectItemCaseSensitive(entry, "expected");
        const char *key;
        const char *reason;
        char *current = NULL;
        int game_id = 0;
        int found;

        if (!cJSON_IsString(source) || !cJSON_IsString(path) || !cJSON_IsString(field)) {
            continue;
        }
        key = strcmp(field->valuestring, "description") == 0 ? CS_METADATA_KEY_DESCRIPTION : CS_METADATA_KEY_TITLE;
        reason = cs_metadata_check_identity(db,
                                            &library,
                                            platform,
                                            source->valuestring,
                                            path->valuestring,
                                            cJSON_IsString(card_key) ? card_key->valuestring : NULL,
                                            &game_id);
        if (reason) {
            cs_metadata_add_skipped(skipped, path->valuestring, field->valuestring, reason);
            continue;
        }
        found = cs_metadata_read_setting(db, game_id, key, &current);
        if (found < 0) {
            goto failed;
        }
        if (!cs_metadata_expected_matches(cJSON_IsString(expected) ? expected->valuestring : NULL,
                                          found ? current : NULL)) {
            cs_metadata_add_skipped(skipped, path->valuestring, field->valuestring, "changed_since");
            free(current);
            continue;
        }
        free(current);
        /* The raw value as it was, which an override written elsewhere may have had
         * with spacing or emptiness the editor wouldn't produce. */
        if (cs_metadata_write_setting(db, game_id, key, cJSON_IsString(value) ? value->valuestring : NULL) != 0) {
            goto failed;
        }
        restored++;
    }
    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        goto failed;
    }
    sqlite3_close(db);
    *restored_out = restored;
    return CS_METADATA_OK;

failed:
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(db);
    return CS_METADATA_INTERNAL;
}
