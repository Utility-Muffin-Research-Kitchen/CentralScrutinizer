#include "cs_dotclean.h"
#include "cs_util.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int cs_dotclean_is_top_level_target(const char *name) {
    return name && (strcmp(name, ".Spotlight-V100") == 0 || strcmp(name, ".fseventsd") == 0
                    || strcmp(name, ".TemporaryItems") == 0 || strcmp(name, ".Trash") == 0
                    || strcmp(name, ".Trashes") == 0 || strcmp(name, ".apDisk") == 0
                    || strcmp(name, ".apdisk") == 0);
}

static int cs_dotclean_should_skip_subtree(const char *relative_path) {
    return relative_path && (strcmp(relative_path, ".system") == 0 || strcmp(relative_path, "Bios") == 0);
}

static int cs_dotclean_should_match(const char *name, int depth, int is_dir, const char **reason_out) {
    if (!name || !reason_out) {
        return 0;
    }

    if (depth == 0 && cs_dotclean_is_top_level_target(name)) {
        *reason_out = "Top-level macOS metadata";
        return 1;
    }
    if (is_dir && strcmp(name, "__MACOSX") == 0) {
        *reason_out = "Archive extraction folder";
        return 1;
    }
    if (!is_dir && strcmp(name, ".DS_Store") == 0) {
        *reason_out = "Finder metadata file";
        return 1;
    }
    /* The shell reference also matches *_cache[0-9].db; this port omits those to avoid deleting emulator caches. */
    if (!is_dir && strncmp(name, "._", 2) == 0) {
        *reason_out = "AppleDouble sidecar file";
        return 1;
    }

    return 0;
}

static int cs_dotclean_add_entry(cs_dotclean_entry *entries,
                                 size_t *count,
                                 size_t capacity,
                                 const char *relative_path,
                                 int is_dir,
                                 const char *reason,
                                 const struct stat *st) {
    if (!count || !relative_path || !reason || !st) {
        return -1;
    }

    if (!entries || *count >= capacity) {
        *count += 1;
        return 0;
    }

    memset(&entries[*count], 0, sizeof(entries[*count]));
    if (CS_SAFE_SNPRINTF(entries[*count].path, sizeof(entries[*count].path), "%s", relative_path) != 0
        || CS_SAFE_SNPRINTF(entries[*count].kind,
                            sizeof(entries[*count].kind),
                            "%s",
                            is_dir ? "directory" : "file")
               != 0
        || CS_SAFE_SNPRINTF(entries[*count].reason, sizeof(entries[*count].reason), "%s", reason) != 0) {
        return -1;
    }

    entries[*count].size = (unsigned long long) st->st_size;
    entries[*count].modified = (long long) st->st_mtime;
    *count += 1;
    return 0;
}

static int cs_dotclean_join_relative(char *dst, size_t dst_size, const char *left, const char *right) {
    if (!dst || dst_size == 0 || !right) {
        return -1;
    }
    if (!left || left[0] == '\0') {
        return CS_SAFE_SNPRINTF(dst, dst_size, "%s", right);
    }
    if (right[0] == '\0') {
        return CS_SAFE_SNPRINTF(dst, dst_size, "%s", left);
    }

    return CS_SAFE_SNPRINTF(dst, dst_size, "%s/%s", left, right);
}

static int cs_dotclean_scan_dir(const char *absolute_path,
                                const char *scan_relative_path,
                                const char *entry_prefix,
                                int depth,
                                cs_dotclean_entry *entries,
                                size_t *count,
                                size_t capacity) {
    DIR *dir;
    struct dirent *entry;

    if (!absolute_path || !scan_relative_path || !entry_prefix || !count) {
        return -1;
    }
    if (depth >= CS_DOTCLEAN_MAX_DEPTH) {
        return 0;
    }

    dir = opendir(absolute_path);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_absolute[CS_PATH_MAX];
        char child_scan_relative[CS_PATH_MAX];
        char child_entry_relative[CS_PATH_MAX];
        struct stat st;
        const char *reason = NULL;
        int is_dir;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (scan_relative_path[0] == '\0') {
            if (CS_SAFE_SNPRINTF(child_scan_relative, sizeof(child_scan_relative), "%s", entry->d_name) != 0) {
                continue;
            }
        } else if (CS_SAFE_SNPRINTF(child_scan_relative,
                                    sizeof(child_scan_relative),
                                    "%s/%s",
                                    scan_relative_path,
                                    entry->d_name)
                       != 0) {
            continue;
        }
        if (cs_dotclean_join_relative(child_entry_relative,
                                      sizeof(child_entry_relative),
                                      entry_prefix,
                                      child_scan_relative)
            != 0) {
            continue;
        }
        if (CS_SAFE_SNPRINTF(child_absolute, sizeof(child_absolute), "%s/%s", absolute_path, entry->d_name) != 0) {
            continue;
        }
        if (lstat(child_absolute, &st) != 0 || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
            continue;
        }

        is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        if (is_dir && cs_dotclean_should_skip_subtree(child_scan_relative)) {
            continue;
        }
        if (cs_dotclean_should_match(entry->d_name, depth, is_dir, &reason)) {
            if (cs_dotclean_add_entry(entries, count, capacity, child_entry_relative, is_dir, reason, &st) != 0) {
                (void) closedir(dir);
                return -1;
            }
            if (is_dir) {
                continue;
            }
        }
        if (is_dir
            && cs_dotclean_scan_dir(child_absolute,
                                    child_scan_relative,
                                    entry_prefix,
                                    depth + 1,
                                    entries,
                                    count,
                                    capacity)
                   != 0) {
            (void) closedir(dir);
            return -1;
        }
    }

    (void) closedir(dir);
    return 0;
}

static int cs_dotclean_compare_paths(const void *left, const void *right) {
    const cs_dotclean_entry *a = (const cs_dotclean_entry *) left;
    const cs_dotclean_entry *b = (const cs_dotclean_entry *) right;

    return strcmp(a->path, b->path);
}

int cs_dotclean_scan(const cs_paths *paths,
                     cs_dotclean_entry *entries,
                     size_t entry_capacity,
                     size_t *entry_count_out,
                     int *truncated_out) {
    size_t count = 0;

    if (entry_count_out) {
        *entry_count_out = 0;
    }
    if (truncated_out) {
        *truncated_out = 0;
    }
    if (!paths) {
        return -1;
    }
    if (paths->source_count > 1) {
        size_t i;

        for (i = 0; i < paths->source_count; ++i) {
            if (cs_dotclean_scan_dir(paths->sources[i].root,
                                     "",
                                     paths->sources[i].alias,
                                     0,
                                     entries,
                                     &count,
                                     entry_capacity)
                != 0) {
                return -1;
            }
        }
    } else {
        const char *root = paths->source_count == 1 ? paths->sources[0].root : paths->sdcard_root;

        if (cs_dotclean_scan_dir(root, "", "", 0, entries, &count, entry_capacity) != 0) {
            return -1;
        }
    }
    if (entries) {
        size_t limit = count < entry_capacity ? count : entry_capacity;

        qsort(entries, limit, sizeof(entries[0]), cs_dotclean_compare_paths);
    }
    if (entry_count_out) {
        *entry_count_out = count;
    }
    if (truncated_out && entries && count > entry_capacity) {
        *truncated_out = 1;
    }
    return 0;
}
