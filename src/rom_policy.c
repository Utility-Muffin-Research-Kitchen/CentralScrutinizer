#include "cs_rom_policy.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ROM upload acceptance policy. The classifier mirrors, field for field, the
   order in Jawaka's internal/discovery/discovery.c jw__metadata_accepts_rom so
   Central Scrutinizer accepts exactly what a rescan will index. Keep the two in
   sync; shared regression vectors live in tests/native/test_rom_policy.c. */

static void cs_lower_copy(const char *in, char *out, size_t out_size) {
    size_t i = 0;
    if (!out || out_size == 0) {
        return;
    }
    if (in) {
        for (; in[i] && i + 1 < out_size; ++i) {
            out[i] = (char) tolower((unsigned char) in[i]);
        }
    }
    out[i] = '\0';
}

/* Final extension (after the last dot), lowercased. Matches Jawaka's
   jw__extension_lower: "game.p8.png" -> "png", "game" or ".hidden" -> "". */
static void cs_extension_lower(const char *filename, char *out, size_t out_size) {
    const char *dot;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!filename) {
        return;
    }
    dot = strrchr(filename, '.');
    if (!dot || dot == filename || !dot[1]) {
        return;
    }
    cs_lower_copy(dot + 1, out, out_size);
}

/* `needle` is already lowercased by the caller; list items are stored
   lowercased by cs_policy_add_unique, so a plain strcmp is case-insensitive. */
static int cs_list_contains(const cs_catalog_string_list *list, const char *needle) {
    size_t i;
    if (!list || !needle) {
        return 0;
    }
    for (i = 0; i < list->count; ++i) {
        if (list->items[i] && strcmp(list->items[i], needle) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Append a lowercased copy of `value` to `list` if not already present. */
static int cs_policy_add_unique(cs_catalog_string_list *list, const char *value) {
    char lowered[512];
    char **grown;
    if (!list || !value || !value[0]) {
        return 0;
    }
    cs_lower_copy(value, lowered, sizeof(lowered));
    if (cs_list_contains(list, lowered)) {
        return 0;
    }
    grown = (char **) realloc(list->items, (list->count + 1) * sizeof(list->items[0]));
    if (!grown) {
        return -1;
    }
    list->items = grown;
    list->items[list->count] = strdup(lowered);
    if (!list->items[list->count]) {
        return -1;
    }
    list->count += 1;
    return 0;
}

static int cs_policy_add_list(cs_catalog_string_list *dst, const cs_catalog_string_list *src) {
    size_t i;
    if (!src) {
        return 0;
    }
    for (i = 0; i < src->count; ++i) {
        if (cs_policy_add_unique(dst, src->items[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void cs_policy_list_free(cs_catalog_string_list *list) {
    size_t i;
    if (!list) {
        return;
    }
    for (i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

void cs_rom_upload_policy_init(cs_rom_upload_policy *policy) {
    if (policy) {
        memset(policy, 0, sizeof(*policy));
    }
}

void cs_rom_upload_policy_free(cs_rom_upload_policy *policy) {
    if (!policy) {
        return;
    }
    cs_policy_list_free(&policy->extensions);
    cs_policy_list_free(&policy->playlist_extensions);
    cs_policy_list_free(&policy->archive_extensions);
    cs_policy_list_free(&policy->file_names);
    cs_policy_list_free(&policy->ignore_file_names);
    policy->enforced = 0;
}

static int cs_policy_has_accept_fields(const cs_rom_upload_policy *policy) {
    return policy->extensions.count > 0 || policy->playlist_extensions.count > 0
        || policy->file_names.count > 0 || policy->archive_extensions.count > 0;
}

int cs_rom_upload_policy_add_system(cs_rom_upload_policy *policy,
                                    const cs_catalog_system *system) {
    const char *mode;
    if (!policy || !system) {
        return -1;
    }
    if (cs_policy_add_list(&policy->extensions, &system->extensions) != 0
        || cs_policy_add_list(&policy->playlist_extensions, &system->playlist_extensions) != 0
        || cs_policy_add_list(&policy->file_names, &system->file_names) != 0
        || cs_policy_add_list(&policy->ignore_file_names, &system->ignore_file_names) != 0) {
        return -1;
    }
    /* Only pass-through archive extensions grant a direct archive upload. A
       row with archive_mode "ignore"/deferred contributes none, and cannot be
       overridden by a sibling pass-through row (each row is judged on its own
       mode). */
    mode = (system->archive_mode && system->archive_mode[0]) ? system->archive_mode : "pass_through";
    if (strcmp(mode, "pass_through") == 0) {
        if (cs_policy_add_list(&policy->archive_extensions, &system->archive_extensions) != 0) {
            return -1;
        }
    }
    policy->enforced = cs_policy_has_accept_fields(policy);
    return 0;
}

cs_rom_entry_status cs_rom_upload_policy_classify(const cs_rom_upload_policy *policy,
                                                  const char *filename) {
    char lower[512];
    char ext[64];

    if (!policy || !filename || !filename[0]) {
        return CS_ROM_ENTRY_UNSUPPORTED;
    }
    if (!policy->enforced) {
        return CS_ROM_ENTRY_ACCEPTED; /* custom/empty policy: fail open */
    }
    /* Jawaka's discovery skips hidden (".") and private ("_") entries for every
       system before it consults the metadata predicate. */
    if (filename[0] == '.' || filename[0] == '_') {
        return CS_ROM_ENTRY_HIDDEN;
    }
    cs_lower_copy(filename, lower, sizeof(lower));
    if (cs_list_contains(&policy->ignore_file_names, lower)) {
        return CS_ROM_ENTRY_IGNORED; /* ignore precedence, even if suffix allowed */
    }
    if (cs_list_contains(&policy->file_names, lower)) {
        return CS_ROM_ENTRY_ACCEPTED;
    }
    cs_extension_lower(filename, ext, sizeof(ext));
    if (!ext[0]) {
        return CS_ROM_ENTRY_UNSUPPORTED;
    }
    if (cs_list_contains(&policy->playlist_extensions, ext)
        || cs_list_contains(&policy->extensions, ext)
        || cs_list_contains(&policy->archive_extensions, ext)) {
        return CS_ROM_ENTRY_ACCEPTED;
    }
    return CS_ROM_ENTRY_UNSUPPORTED;
}

const char *cs_rom_entry_status_name(cs_rom_entry_status status) {
    switch (status) {
        case CS_ROM_ENTRY_ACCEPTED:    return "accepted";
        case CS_ROM_ENTRY_UNSUPPORTED: return "unsupported_rom_format";
        case CS_ROM_ENTRY_IGNORED:     return "ignored_rom_name";
        case CS_ROM_ENTRY_HIDDEN:      return "hidden_rom_name";
        default:                       return "unknown";
    }
}
