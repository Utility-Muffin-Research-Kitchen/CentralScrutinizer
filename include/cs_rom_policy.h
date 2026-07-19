#ifndef CS_ROM_POLICY_H
#define CS_ROM_POLICY_H

#include "cs_catalog.h"

typedef enum cs_rom_entry_status {
    CS_ROM_ENTRY_ACCEPTED = 0,
    CS_ROM_ENTRY_UNSUPPORTED,
    CS_ROM_ENTRY_IGNORED,
    CS_ROM_ENTRY_HIDDEN
} cs_rom_entry_status;

/* Effective ROM-upload acceptance policy for a resolved platform. It folds one
   or more catalog rows: direct extensions, playlist extensions, exact accept
   names, and only *pass-through* archive extensions are unioned; ignored names
   are unioned with reject precedence. `enforced` is false for custom or
   empty-policy platforms (fail open). Lists are lowercase-normalized so matching
   is case-insensitive. This mirrors Jawaka's jw__metadata_accepts_rom so an
   accepted upload will not vanish after a rescan. */
typedef struct cs_rom_upload_policy {
    int enforced;
    cs_catalog_string_list extensions;
    cs_catalog_string_list playlist_extensions;
    cs_catalog_string_list archive_extensions; /* pass-through only */
    cs_catalog_string_list file_names;         /* exact accept */
    cs_catalog_string_list ignore_file_names;  /* exact reject (precedence) */
} cs_rom_upload_policy;

void cs_rom_upload_policy_init(cs_rom_upload_policy *policy);
void cs_rom_upload_policy_free(cs_rom_upload_policy *policy);

/* Fold one catalog system's acceptance fields into the policy. Only archive
   extensions whose system archive_mode is "pass_through" are added, so a
   pass-through row cannot make a different row's non-pass-through archive
   extension valid. Returns 0 on success, non-zero on allocation failure. */
int cs_rom_upload_policy_add_system(cs_rom_upload_policy *policy,
                                    const cs_catalog_system *system);

/* Classify a candidate ROM entrypoint filename (basename). Mirrors Jawaka's
   jw__metadata_accepts_rom plus its hidden/private-name skip. When the policy is
   not enforced, every non-empty name is accepted (fail open). */
cs_rom_entry_status cs_rom_upload_policy_classify(const cs_rom_upload_policy *policy,
                                                  const char *filename);

const char *cs_rom_entry_status_name(cs_rom_entry_status status);

#endif
