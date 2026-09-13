#include "cs_art.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define CS_ART_NAME_MAX 1024
#define CS_ART_EXT_MAX 8
#define CS_ART_DIR_BUCKETS 64

/* Lower-case extensions in lookup precedence order. */
static const char *const cs_art_extensions[] = {"png", "jpg", "jpeg"};

typedef struct {
    char *name; /* filename as it exists on disk */
    size_t stem_len;
    uint32_t stem_hash; /* of the stem, case-folded when the folder folds case */
    int format;         /* 0 png, 1 jpg, 2 jpeg */
    int spelling;       /* 0 all lower, 1 all upper, 2 mixed */
    size_t next;        /* 1-based chain within a hash bucket, 0 ends it */
} cs_art_entry;

typedef struct cs_art_dir {
    char *path;
    uint32_t path_hash;
    int folds_case;
    cs_art_entry *entries;
    size_t count;
    size_t *heads; /* 1-based entry index per bucket */
    size_t bucket_count;
    struct cs_art_dir *next;
} cs_art_dir;

struct cs_art_index {
    cs_art_dir *buckets[CS_ART_DIR_BUCKETS];
};

static uint32_t cs_art_hash(const char *s, size_t len, int fold) {
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char) s[i];

        h ^= fold ? (unsigned char) tolower(c) : c;
        h *= 16777619u;
    }
    return h;
}

static int cs_art_format(const char *ext) {
    size_t i;

    for (i = 0; i < sizeof(cs_art_extensions) / sizeof(cs_art_extensions[0]); ++i) {
        if (strcasecmp(ext, cs_art_extensions[i]) == 0) {
            return (int) i;
        }
    }
    return -1;
}

static int cs_art_spelling(const char *ext) {
    int lower = 1;
    int upper = 1;
    const char *p;

    for (p = ext; *p; ++p) {
        if (islower((unsigned char) *p)) {
            upper = 0;
        }
        if (isupper((unsigned char) *p)) {
            lower = 0;
        }
    }
    return lower ? 0 : upper ? 1 : 2;
}

static int cs_art_is_regular_not_symlink(const char *dir, const char *name, unsigned char type) {
    char path[4096];
    struct stat st;

#ifdef DT_UNKNOWN
    if (type == DT_REG) {
        return 1;
    }
    if (type != DT_UNKNOWN) {
        return 0;
    }
#else
    (void) type;
#endif
    return snprintf(path, sizeof(path), "%s/%s", dir, name) < (int) sizeof(path) && lstat(path, &st) == 0
           && S_ISREG(st.st_mode);
}

/* Whether this folder's filesystem resolves names without case: look the
 * first art file up under its case-swapped name and compare inodes. Art names
 * always contain letters (the extension). */
static int cs_art_folds_case(const char *dir, const char *name) {
    char original[4096];
    char swapped[4096];
    struct stat a;
    struct stat b;
    char *p;

    if (snprintf(original, sizeof(original), "%s/%s", dir, name) >= (int) sizeof(original)) {
        return 0;
    }
    memcpy(swapped, original, sizeof(swapped));
    for (p = swapped + strlen(dir) + 1; *p; ++p) {
        unsigned char c = (unsigned char) *p;

        *p = (char) (islower(c) ? toupper(c) : tolower(c));
    }
    return lstat(original, &a) == 0 && lstat(swapped, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

static void cs_art_dir_free(cs_art_dir *d) {
    size_t i;

    if (!d) {
        return;
    }
    for (i = 0; i < d->count; ++i) {
        free(d->entries[i].name);
    }
    free(d->entries);
    free(d->heads);
    free(d->path);
    free(d);
}

/* Read one folder. A missing or unreadable folder loads as empty. Returns NULL
 * only when out of memory. */
static cs_art_dir *cs_art_dir_load(const char *path) {
    cs_art_dir *d = calloc(1, sizeof(*d));
    DIR *dir;
    struct dirent *e;
    size_t capacity = 0;
    size_t i;

    if (!d || !(d->path = strdup(path))) {
        free(d);
        return NULL;
    }
    dir = opendir(path);
    if (!dir) {
        return d;
    }
    while ((e = readdir(dir)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        cs_art_entry *entry;
        unsigned char type = 0;
        int format;

        if (!dot || dot == e->d_name) {
            continue;
        }
        format = cs_art_format(dot + 1);
        if (format < 0) {
            continue;
        }
#ifdef DT_UNKNOWN
        type = e->d_type;
#endif
        if (!cs_art_is_regular_not_symlink(path, e->d_name, type)) {
            continue;
        }
        if (d->count == capacity) {
            size_t grown = capacity ? capacity * 2 : 64;
            cs_art_entry *entries = realloc(d->entries, grown * sizeof(*entries));

            if (!entries) {
                break;
            }
            d->entries = entries;
            capacity = grown;
        }
        entry = &d->entries[d->count];
        memset(entry, 0, sizeof(*entry));
        if (!(entry->name = strdup(e->d_name))) {
            break;
        }
        entry->stem_len = (size_t) (dot - e->d_name);
        entry->format = format;
        entry->spelling = cs_art_spelling(dot + 1);
        d->count += 1;
    }
    closedir(dir);

    if (d->count == 0) {
        return d;
    }
    d->folds_case = cs_art_folds_case(path, d->entries[0].name);
    d->bucket_count = 1;
    while (d->bucket_count < d->count * 2) {
        d->bucket_count *= 2;
    }
    d->heads = calloc(d->bucket_count, sizeof(*d->heads));
    if (!d->heads) {
        cs_art_dir_free(d);
        return NULL;
    }
    for (i = 0; i < d->count; ++i) {
        cs_art_entry *entry = &d->entries[i];
        size_t bucket;

        entry->stem_hash = cs_art_hash(entry->name, entry->stem_len, d->folds_case);
        bucket = entry->stem_hash & (d->bucket_count - 1);
        entry->next = d->heads[bucket];
        d->heads[bucket] = i + 1;
    }
    return d;
}

/* Non-zero when a ranks before b: format, then spelling class, then bytewise. */
static int cs_art_better(const cs_art_entry *a, const cs_art_entry *b) {
    int ext;

    if (a->format != b->format) {
        return a->format < b->format;
    }
    if (a->spelling != b->spelling) {
        return a->spelling < b->spelling;
    }
    ext = strcmp(a->name + a->stem_len, b->name + b->stem_len);
    if (ext != 0) {
        return ext < 0;
    }
    return strcmp(a->name, b->name) < 0;
}

static int cs_art_dir_find(const cs_art_dir *d, const char *stem, char *out_name, size_t out_name_size) {
    const cs_art_entry *best = NULL;
    size_t stem_len;
    uint32_t hash;
    size_t i;

    if (d->count == 0) {
        return 1;
    }
    stem_len = strlen(stem);
    hash = cs_art_hash(stem, stem_len, d->folds_case);
    for (i = d->heads[hash & (d->bucket_count - 1)]; i; i = d->entries[i - 1].next) {
        const cs_art_entry *entry = &d->entries[i - 1];
        int same;

        if (entry->stem_hash != hash || entry->stem_len != stem_len) {
            continue;
        }
        same = d->folds_case ? strncasecmp(entry->name, stem, stem_len) == 0
                             : memcmp(entry->name, stem, stem_len) == 0;
        if (same && (!best || cs_art_better(entry, best))) {
            best = entry;
        }
    }
    if (!best) {
        return 1;
    }
    return snprintf(out_name, out_name_size, "%s", best->name) < (int) out_name_size ? 0 : -1;
}

cs_art_index *cs_art_index_new(void) {
    return calloc(1, sizeof(cs_art_index));
}

void cs_art_index_free(cs_art_index *index) {
    size_t b;

    if (!index) {
        return;
    }
    for (b = 0; b < CS_ART_DIR_BUCKETS; ++b) {
        cs_art_dir *d = index->buckets[b];

        while (d) {
            cs_art_dir *next = d->next;

            cs_art_dir_free(d);
            d = next;
        }
    }
    free(index);
}

static const cs_art_dir *cs_art_index_dir(cs_art_index *index, const char *path) {
    uint32_t hash = cs_art_hash(path, strlen(path), 0);
    cs_art_dir **bucket = &index->buckets[hash % CS_ART_DIR_BUCKETS];
    cs_art_dir *d;

    for (d = *bucket; d; d = d->next) {
        if (d->path_hash == hash && strcmp(d->path, path) == 0) {
            return d;
        }
    }
    d = cs_art_dir_load(path);
    if (!d) {
        return NULL;
    }
    d->path_hash = hash;
    d->next = *bucket;
    *bucket = d;
    return d;
}

int cs_art_find(cs_art_index *index, const char *dir, const char *stem, char *out_name, size_t out_name_size) {
    cs_art_dir *owned;
    int rc;

    if (!dir || dir[0] == '\0' || !stem || stem[0] == '\0' || !out_name || out_name_size == 0) {
        return 1;
    }
    out_name[0] = '\0';
    if (index) {
        const cs_art_dir *d = cs_art_index_dir(index, dir);

        if (d) {
            return cs_art_dir_find(d, stem, out_name, out_name_size);
        }
    }
    owned = cs_art_dir_load(dir);
    if (!owned) {
        return 1;
    }
    rc = cs_art_dir_find(owned, stem, out_name, out_name_size);
    cs_art_dir_free(owned);
    return rc;
}

int cs_art_upload_extension(const char *filename, char *ext_out, size_t ext_out_size) {
    const char *dot;
    size_t len;
    size_t i;
    char lower[CS_ART_EXT_MAX + 1];

    if (!filename || !ext_out || ext_out_size == 0) {
        return 0;
    }
    dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return 0;
    }
    len = strlen(dot + 1);
    if (len == 0 || len > CS_ART_EXT_MAX || len >= ext_out_size) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        lower[i] = (char) tolower((unsigned char) dot[1 + i]);
    }
    lower[len] = '\0';

    for (i = 0; i < sizeof(cs_art_extensions) / sizeof(cs_art_extensions[0]); ++i) {
        if (strcmp(lower, cs_art_extensions[i]) == 0) {
            memcpy(ext_out, lower, len + 1);
            return 1;
        }
    }
    return 0;
}

/* Spell `ext` with bit (len-1-i) of mask choosing lower case at position i. */
static void cs_art_spell(const char *ext, unsigned mask, char *out) {
    size_t len = strlen(ext);
    size_t i;

    for (i = 0; i < len; ++i) {
        unsigned bit = 1u << (len - 1 - i);

        out[i] = (mask & bit) ? (char) tolower((unsigned char) ext[i]) : (char) toupper((unsigned char) ext[i]);
    }
    out[len] = '\0';
}

typedef struct {
    int dir_fd;
    const char *stem;
    dev_t keep_dev;
    ino_t keep_ino;
    int error;
} cs_art_cleanup_ctx;

static void cs_art_cleanup_spelling(cs_art_cleanup_ctx *ctx, const char *spelled) {
    char name[CS_ART_NAME_MAX];
    struct stat st;

    if (snprintf(name, sizeof(name), "%s.%s", ctx->stem, spelled) >= (int) sizeof(name)) {
        return;
    }
    if (fstatat(ctx->dir_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) {
        return;
    }
    /* This spelling resolves to the replacement itself (a case alias on a
     * case-insensitive filesystem, or a hard link): keep it. */
    if (st.st_dev == ctx->keep_dev && st.st_ino == ctx->keep_ino) {
        return;
    }
    if (unlinkat(ctx->dir_fd, name, 0) != 0 && errno != ENOENT && ctx->error == 0) {
        ctx->error = errno;
    }
}

int cs_art_remove_siblings(const char *dir, const char *stem, const char *keep_name) {
    static const char *const cleanup_extensions[] = {"png", "jpg", "jpeg", "webp"};
    cs_art_cleanup_ctx ctx;
    struct stat keep_st;
    size_t i;

    if (!dir || !stem || stem[0] == '\0' || !keep_name || keep_name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.stem = stem;
    ctx.dir_fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (ctx.dir_fd < 0) {
        return -1;
    }
    if (fstatat(ctx.dir_fd, keep_name, &keep_st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(keep_st.st_mode)) {
        int saved = errno ? errno : EIO;

        close(ctx.dir_fd);
        errno = saved;
        return -1;
    }
    ctx.keep_dev = keep_st.st_dev;
    ctx.keep_ino = keep_st.st_ino;

    /* Every spelling, probed by name: cleanup must follow what each spelling
     * resolves to on this filesystem, which a directory listing cannot tell. */
    for (i = 0; i < sizeof(cleanup_extensions) / sizeof(cleanup_extensions[0]); ++i) {
        const char *ext = cleanup_extensions[i];
        unsigned mask;
        unsigned count = 1u << strlen(ext);
        char spelled[CS_ART_EXT_MAX + 1];

        for (mask = 0; mask < count; ++mask) {
            cs_art_spell(ext, mask, spelled);
            cs_art_cleanup_spelling(&ctx, spelled);
        }
    }

    close(ctx.dir_fd);
    if (ctx.error != 0) {
        errno = ctx.error;
        return -1;
    }
    return 0;
}
