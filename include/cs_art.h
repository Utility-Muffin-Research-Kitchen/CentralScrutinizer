#ifndef CS_ART_H
#define CS_ART_H

#include <stddef.h>

/* Box-art naming shared by the browser thumbnail resolver and Replace Art.
 *
 * Leaf's launcher owns this rule (Jawaka internal/discovery/art_path.c); CS
 * cannot link Jawaka internals, so keep this copy in step with it:
 *   - <stem>.png, <stem>.jpg, <stem>.jpeg are art, with the extension matched
 *     case-insensitively.
 *   - PNG beats JPG beats JPEG. Within a format: all lower-case, then all
 *     upper-case, then mixed spellings in bytewise order.
 *   - The stem is compared byte for byte, so on a case-sensitive card "Game"
 *     never borrows "game.jpg"; where the folder's filesystem folds case it is
 *     compared without case, as opening the file by name would.
 *   - Folders are read once and matched in memory; the result is the spelling
 *     on disk and does not depend on directory order.
 * Unlike the launcher, CS does not treat symlinks as art. */

/* A cache of art folders, each read once on first use and never refreshed:
 * keep one for a single listing. Not thread-safe. */
typedef struct cs_art_index cs_art_index;

/* NULL when out of memory; lookups accept NULL and read the folder per call. */
cs_art_index *cs_art_index_new(void);
void cs_art_index_free(cs_art_index *index);

/* Find the art for `stem` in `dir`. Writes the matched filename to out_name
 * and returns 0; returns 1 when there is none, -1 when a name does not fit. */
int cs_art_find(cs_art_index *index, const char *dir, const char *stem, char *out_name, size_t out_name_size);

/* Returns 1 when `filename` has an uploadable art extension (.png, .jpg,
 * .jpeg, any case) and writes it lower-cased, without the dot, to ext_out. */
int cs_art_upload_extension(const char *filename, char *ext_out, size_t ext_out_size);

/* After Replace Art promoted `keep_name` into `dir`, remove every other
 * same-stem artwork file: PNG/JPG/JPEG in any extension case, plus legacy
 * WebP. Candidates are probed by name, so on a case-insensitive filesystem a
 * spelling that resolves to a differently cased old file removes that file,
 * while one that resolves to the promoted file itself is skipped by file
 * identity. Other stems, including case variants of the stem on a
 * case-sensitive filesystem, are never touched. Returns 0 when no sibling
 * remains, -1 with errno set when the promoted file cannot be identified or a
 * sibling could not be removed (the promoted file is always kept). */
int cs_art_remove_siblings(const char *dir, const char *stem, const char *keep_name);

#endif
