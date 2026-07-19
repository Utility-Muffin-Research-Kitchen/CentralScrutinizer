#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cs_catalog.h"
#include "cs_rom_policy.h"

/* Shared regression vectors for the ROM-upload acceptance predicate. These must
   stay aligned with Jawaka's jw__metadata_accepts_rom (see
   Jawaka/internal/discovery/discovery.c). */

static cs_catalog_string_list list_of(char **items, size_t n) {
    cs_catalog_string_list l;
    l.items = items;
    l.count = n;
    return l;
}
#define LIST(arr) list_of((arr), sizeof(arr) / sizeof((arr)[0]))
static const cs_catalog_string_list EMPTY = { NULL, 0 };

static cs_rom_entry_status classify(const cs_catalog_system *systems, size_t n,
                                    const char *filename, int *enforced_out) {
    cs_rom_upload_policy policy;
    cs_rom_entry_status status;
    size_t i;
    cs_rom_upload_policy_init(&policy);
    for (i = 0; i < n; ++i) {
        assert(cs_rom_upload_policy_add_system(&policy, &systems[i]) == 0);
    }
    if (enforced_out) {
        *enforced_out = policy.enforced;
    }
    status = cs_rom_upload_policy_classify(&policy, filename);
    cs_rom_upload_policy_free(&policy);
    return status;
}

static cs_rom_entry_status classify1(const cs_catalog_system *sys, const char *filename) {
    return classify(sys, 1, filename, NULL);
}

static cs_catalog_system make_system(cs_catalog_string_list ext,
                                     cs_catalog_string_list archive_ext,
                                     char *archive_mode,
                                     cs_catalog_string_list playlist,
                                     cs_catalog_string_list file_names,
                                     cs_catalog_string_list ignore) {
    cs_catalog_system s;
    memset(&s, 0, sizeof(s));
    s.extensions = ext;
    s.archive_extensions = archive_ext;
    s.archive_mode = archive_mode;
    s.playlist_extensions = playlist;
    s.file_names = file_names;
    s.ignore_file_names = ignore;
    return s;
}

static void test_psp_direct(void) {
    char *ext[] = { "chd", "cso", "iso", "pbp" };
    cs_catalog_system psp = make_system(LIST(ext), EMPTY, "pass_through", EMPTY, EMPTY, EMPTY);
    int enforced = 0;
    assert(classify(&psp, 1, "Game.iso", &enforced) == CS_ROM_ENTRY_ACCEPTED);
    assert(enforced == 1);
    assert(classify1(&psp, "Game.ISO") == CS_ROM_ENTRY_ACCEPTED);   /* case-insensitive */
    assert(classify1(&psp, "x.chd") == CS_ROM_ENTRY_ACCEPTED);
    assert(classify1(&psp, "x.cso") == CS_ROM_ENTRY_ACCEPTED);
    assert(classify1(&psp, "x.pbp") == CS_ROM_ENTRY_ACCEPTED);
    /* the motivating bug: PSP has no pass-through zip */
    assert(classify1(&psp, "FIFA 14 (Netherlands).zip") == CS_ROM_ENTRY_UNSUPPORTED);
    assert(classify1(&psp, "noext") == CS_ROM_ENTRY_UNSUPPORTED);
}

static void test_zip_capable_cartridge(void) {
    char *ext[] = { "bin", "gba" };
    char *arc[] = { "7z", "zip" };
    cs_catalog_system gba = make_system(LIST(ext), LIST(arc), "pass_through", EMPTY, EMPTY, EMPTY);
    assert(classify1(&gba, "Game.zip") == CS_ROM_ENTRY_ACCEPTED);   /* pass-through zip stays valid */
    assert(classify1(&gba, "Game.7Z") == CS_ROM_ENTRY_ACCEPTED);
    assert(classify1(&gba, "Game.gba") == CS_ROM_ENTRY_ACCEPTED);
    assert(classify1(&gba, "Game.iso") == CS_ROM_ENTRY_UNSUPPORTED);
}

static void test_ignore_precedence(void) {
    char *arc[] = { "zip", "7z" };
    char *ignore[] = { "neocd.zip", "neogeo.zip" };
    cs_catalog_system neo = make_system(EMPTY, LIST(arc), "pass_through", EMPTY, EMPTY, LIST(ignore));
    assert(classify1(&neo, "mslug.zip") == CS_ROM_ENTRY_ACCEPTED);
    assert(classify1(&neo, "neogeo.zip") == CS_ROM_ENTRY_IGNORED);  /* ignore beats zip suffix */
    assert(classify1(&neo, "NeoGeo.ZIP") == CS_ROM_ENTRY_IGNORED);  /* casefold */
}

static void test_playlist_and_exact_and_pico8(void) {
    char *ext[] = { "cue", "bin" };
    char *pl[] = { "m3u" };
    cs_catalog_system disc = make_system(LIST(ext), EMPTY, "pass_through", LIST(pl), EMPTY, EMPTY);
    assert(classify1(&disc, "Game.m3u") == CS_ROM_ENTRY_ACCEPTED);

    char *names[] = { "boot.rom" };
    cs_catalog_system exact = make_system(EMPTY, EMPTY, "pass_through", EMPTY, LIST(names), EMPTY);
    assert(classify1(&exact, "boot.rom") == CS_ROM_ENTRY_ACCEPTED);
    assert(classify1(&exact, "BOOT.ROM") == CS_ROM_ENTRY_ACCEPTED); /* casefold exact name */
    assert(classify1(&exact, "other.rom") == CS_ROM_ENTRY_UNSUPPORTED);

    char *p8[] = { "p8", "png" };
    cs_catalog_system pico = make_system(LIST(p8), EMPTY, "pass_through", EMPTY, EMPTY, EMPTY);
    assert(classify1(&pico, "cart.p8.png") == CS_ROM_ENTRY_ACCEPTED); /* final suffix png */
    assert(classify1(&pico, "cart.p8") == CS_ROM_ENTRY_ACCEPTED);
}

static void test_archive_mode_variants(void) {
    char *ext[] = { "iso" };
    char *arc[] = { "zip" };
    cs_catalog_system ign = make_system(LIST(ext), LIST(arc), "ignore", EMPTY, EMPTY, EMPTY);
    assert(classify1(&ign, "Game.zip") == CS_ROM_ENTRY_UNSUPPORTED); /* ignore mode: no pass-through */
    assert(classify1(&ign, "Game.iso") == CS_ROM_ENTRY_ACCEPTED);

    cs_catalog_system deferred = make_system(LIST(ext), LIST(arc), "extract", EMPTY, EMPTY, EMPTY);
    assert(classify1(&deferred, "Game.zip") == CS_ROM_ENTRY_UNSUPPORTED); /* unknown/deferred */
}

static void test_folded_no_archive_leakage(void) {
    /* Row A permits pass-through zip; row B lists 7z but as ignore. The folded
       policy must accept zip (from A) yet still reject 7z (B's mode does not
       leak into A's permission). */
    char *a_arc[] = { "zip" };
    char *a_ext[] = { "iso" };
    char *b_arc[] = { "7z" };
    char *b_ext[] = { "bin" };
    cs_catalog_system rows[2];
    rows[0] = make_system(LIST(a_ext), LIST(a_arc), "pass_through", EMPTY, EMPTY, EMPTY);
    rows[1] = make_system(LIST(b_ext), LIST(b_arc), "ignore", EMPTY, EMPTY, EMPTY);
    assert(classify(rows, 2, "x.zip", NULL) == CS_ROM_ENTRY_ACCEPTED);
    assert(classify(rows, 2, "x.7z", NULL) == CS_ROM_ENTRY_UNSUPPORTED);
    assert(classify(rows, 2, "x.iso", NULL) == CS_ROM_ENTRY_ACCEPTED);
    assert(classify(rows, 2, "x.bin", NULL) == CS_ROM_ENTRY_ACCEPTED);
}

static void test_empty_policy_fails_open(void) {
    cs_catalog_system empty = make_system(EMPTY, EMPTY, "pass_through", EMPTY, EMPTY, EMPTY);
    int enforced = 1;
    assert(classify(&empty, 1, "whatever.xyz", &enforced) == CS_ROM_ENTRY_ACCEPTED);
    assert(enforced == 0);                                          /* not enforced */
    assert(classify1(&empty, "_private.zip") == CS_ROM_ENTRY_ACCEPTED); /* fail open, even hidden */
}

static void test_hidden_private_when_enforced(void) {
    char *ext[] = { "iso" };
    cs_catalog_system psp = make_system(LIST(ext), EMPTY, "pass_through", EMPTY, EMPTY, EMPTY);
    assert(classify1(&psp, "_Game.iso") == CS_ROM_ENTRY_HIDDEN);
    assert(classify1(&psp, ".Game.iso") == CS_ROM_ENTRY_HIDDEN);
    assert(classify1(&psp, "._Game.iso") == CS_ROM_ENTRY_HIDDEN);
    assert(classify1(&psp, "Game.iso") == CS_ROM_ENTRY_ACCEPTED);
}

int main(void) {
    test_psp_direct();
    test_zip_capable_cartridge();
    test_ignore_precedence();
    test_playlist_and_exact_and_pico8();
    test_archive_mode_variants();
    test_folded_no_archive_leakage();
    test_empty_policy_fails_open();
    test_hidden_private_when_enforced();
    printf("test_rom_policy: ok\n");
    return 0;
}
