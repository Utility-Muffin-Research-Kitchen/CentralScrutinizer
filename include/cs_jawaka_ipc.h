#ifndef CS_JAWAKA_IPC_H
#define CS_JAWAKA_IPC_H

#include <stddef.h>

/* What a library editor needs to know from the running daemon. All zero when it can't
 * be reached, or for what a release from before metadata_caps doesn't report. */
typedef struct cs_jawaka_library_caps {
    int reachable;
    int scan_busy;
    int user_synopsis;
    int metadata_changed;
    int sort_rank;
    int folder_rows;
} cs_jawaka_library_caps;

int cs_jawaka_request_library_rescan(char *status, size_t status_size);
void cs_jawaka_read_library_caps(cs_jawaka_library_caps *caps);
/* Tells the launcher the library's titles or descriptions changed, without waiting
 * for it to act. Returns 0 when the daemon accepted that. */
int cs_jawaka_notify_library_metadata(const cs_jawaka_library_caps *caps);

/* One level of a system's list as the launcher draws it, from the daemon's own folder
 * model, or NULL. The caller frees the reply. */
struct cJSON;
struct cJSON *cs_jawaka_read_folder_rows(const char *system, const char *folder);

#endif
