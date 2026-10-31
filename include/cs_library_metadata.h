#ifndef CS_LIBRARY_METADATA_H
#define CS_LIBRARY_METADATA_H

#include <stddef.h>

#include "cjson/cJSON.h"
#include "cs_library.h"
#include "cs_paths.h"

/* What fits Jawaka's jw_game_entry.name and jw_game_meta.synopsis. */
#define CS_METADATA_TITLE_MAX 255u
#define CS_METADATA_DESCRIPTION_MAX 1199u

typedef enum {
    CS_METADATA_OK = 0,
    CS_METADATA_UNAVAILABLE, /* no library, or one too old for the editor */
    CS_METADATA_INVALID,     /* the request itself is malformed */
    CS_METADATA_CONFLICT,    /* a change doesn't match the library; nothing was saved */
    CS_METADATA_INTERNAL,
} cs_metadata_status;

/* Turn user text into what the launcher can draw on one wrapped line: control bytes
 * become spaces, runs of whitespace collapse, and the ends are trimmed. In place. */
void cs_metadata_normalize_text(char *text);

/* Adds the platform's listed games to out as "games", each with its identity, title
 * and raw title override, and the same for descriptions when asked for. A second
 * card's games appear only when it is mounted and the library knows which card it is,
 * since a swap can't be detected otherwise. */
cs_metadata_status cs_metadata_list_games(const cs_paths *paths,
                                          const cs_platform_info *platform,
                                          int include_descriptions,
                                          cJSON *out);

/* Adds one game's raw description override and scraped description to out. */
cs_metadata_status cs_metadata_get_game(const cs_paths *paths,
                                        const cs_platform_info *platform,
                                        const char *source,
                                        const char *path,
                                        cJSON *out);

/* Applies a JSON array of changes in one transaction, or none of them:
 *
 *   {source, path, cardKey, field: "title"|"description", value, expected}
 *
 * value null, or empty once normalized, clears the override by deleting its row.
 * expected is the raw override the change replaces, null when there is none. On
 * CS_METADATA_CONFLICT, conflicts_out holds an array naming each rejected change and
 * why. On CS_METADATA_INVALID, error_out names the problem. allow_description is
 * false on a Leaf that wouldn't show the override. */
cs_metadata_status cs_metadata_apply(const cs_paths *paths,
                                     const cs_platform_info *platform,
                                     const cJSON *changes,
                                     int allow_description,
                                     int *saved_out,
                                     cJSON **conflicts_out,
                                     const char **error_out);

/* Undo of the last bulk apply, one level across the library. The snapshot is written
 * and synced before the apply starts, and only replaces the previous one once the
 * apply has committed, so a rejected apply leaves the previous undo in place:
 *
 *   build, prepare, cs_metadata_apply, then commit or discard. */
cJSON *cs_metadata_undo_build(const cs_platform_info *platform, const cJSON *changes, int allow_description);
int cs_metadata_undo_prepare(const cs_paths *paths, const cJSON *snapshot);
int cs_metadata_undo_commit(const cs_paths *paths);
void cs_metadata_undo_discard(const cs_paths *paths);
void cs_metadata_undo_clear(const cs_paths *paths);

/* The snapshot for this platform, or NULL when the last bulk apply was elsewhere. */
cJSON *cs_metadata_undo_read(const cs_paths *paths, const char *tag);

/* Restores each entry whose current value is still what the apply wrote. The rest are
 * added to skipped, so a later edit is never overwritten. */
cs_metadata_status cs_metadata_undo_apply(const cs_paths *paths,
                                          const cs_platform_info *platform,
                                          const cJSON *snapshot,
                                          int *restored_out,
                                          cJSON *skipped);

#endif
