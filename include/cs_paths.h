#ifndef CS_PATHS_H
#define CS_PATHS_H

#include <limits.h>
#include <stddef.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* realpath(3) requires a PATH_MAX-sized destination when the caller supplies
 * the buffer. Keep every stored path at that same platform limit: 1024 on
 * macOS, 4096 on the MLP1's Linux userspace. */
#define CS_PATH_MAX PATH_MAX
#define CS_PATH_SOURCE_MAX 4

typedef struct cs_path_source {
    char alias[64];
    char root[CS_PATH_MAX];
    char roms_root[CS_PATH_MAX];
    char images_root[CS_PATH_MAX];
    char apps_root[CS_PATH_MAX];
    char bios_root[CS_PATH_MAX];
    char saves_root[CS_PATH_MAX];
    char states_root[CS_PATH_MAX];
    char cheats_root[CS_PATH_MAX];
    /* Staging directory on this source's own filesystem. Uploads must be
     * promoted with rename(2), which cannot cross a mount boundary, so every
     * source stages on the card it writes to. */
    char temp_upload_root[CS_PATH_MAX];
} cs_path_source;

typedef struct cs_paths {
    char sdcard_root[CS_PATH_MAX];
    char system_root[CS_PATH_MAX];
    char shared_userdata_root[CS_PATH_MAX];
    char shared_state_root[CS_PATH_MAX];
    char web_root[CS_PATH_MAX];
    char roms_root[CS_PATH_MAX];
    char images_root[CS_PATH_MAX];
    char apps_root[CS_PATH_MAX];
    char saves_root[CS_PATH_MAX];
    char states_root[CS_PATH_MAX];
    char bios_root[CS_PATH_MAX];
    char overlays_root[CS_PATH_MAX];
    char cheats_root[CS_PATH_MAX];
    char temp_upload_root[CS_PATH_MAX];
    char cores_root[CS_PATH_MAX];
    char info_root[CS_PATH_MAX];
    char systems_catalog_path[CS_PATH_MAX];
    char cores_catalog_path[CS_PATH_MAX];
    char catalog_selector_path[CS_PATH_MAX];
    char release_identity_path[CS_PATH_MAX];
    char platform_id[64];
    int catalog_paths_overridden;
    char logs_root[CS_PATH_MAX];
    char internal_data_root[CS_PATH_MAX];
    cs_path_source sources[CS_PATH_SOURCE_MAX];
    size_t source_count;
} cs_paths;

int cs_paths_init(cs_paths *paths);
int cs_paths_source_index_for_alias(const cs_paths *paths, const char *alias);
/* Staging root for uploads destined for source_root. Falls back to the primary
 * temp root when source_root does not name a configured source. */
const char *cs_paths_temp_upload_root_for(const cs_paths *paths, const char *source_root);
int cs_paths_resolve_files_path(const cs_paths *paths,
                                const char *virtual_path,
                                char *root,
                                size_t root_size,
                                char *relative,
                                size_t relative_size,
                                const cs_path_source **source_out);

#endif
