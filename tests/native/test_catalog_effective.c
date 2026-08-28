#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_catalog.h"

static void make_dir(const char *path) {
    char copy[PATH_MAX];
    char *cursor;
    assert(snprintf(copy, sizeof(copy), "%s", path) > 0);
    for (cursor = copy + 1; *cursor; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        assert(mkdir(copy, 0700) == 0 || access(copy, F_OK) == 0);
        *cursor = '/';
    }
    assert(mkdir(copy, 0700) == 0 || access(copy, F_OK) == 0);
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static void join(char *out, size_t size, const char *left, const char *right) {
    int written = snprintf(out, size, "%s/%s", left, right);
    assert(written > 0 && (size_t) written < size);
}

static void write_generation(const char *catalog_root,
                             const char *generation,
                             const char *system_id,
                             const char *group) {
    char root[PATH_MAX];
    char path[PATH_MAX];
    char systems[1024];
    join(root, sizeof(root), catalog_root, generation);
    join(path, sizeof(path), root, "info");
    make_dir(path);
    join(path, sizeof(path), root, "systems.json");
    assert(snprintf(systems, sizeof(systems),
                    "{\"version\":2,\"systems\":[{\"id\":\"%s\","
                    "\"name\":\"Pak System\",\"rom_root\":\"Roms/%s\","
                    "\"image_root\":\"Images/%s\",\"group\":\"%s\","
                    "\"bios_directory\":\"pakbios\","
                    "\"icon_flat\":\"icons/flat.png\","
                    "\"provider\":\"mlp1/Test.pak\"}]}",
                    system_id, system_id, system_id, group) > 0);
    write_file(path, systems);
    join(path, sizeof(path), root, "cores.json");
    write_file(path, "{\"version\":2,\"cores\":[]}");
    join(path, sizeof(path), root, "stamp.json");
    write_file(path,
               "{\"schema\":1,\"platform\":\"mlp1\","
               "\"release_id\":\"release-test\"}");
}

int main(void) {
    static const char gen_a[] =
        "gen-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    static const char gen_b[] =
        "gen-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    char template[] = "/tmp/cs-catalog-effective-XXXXXX";
    char *root = mkdtemp(template);
    char defaults[PATH_MAX];
    char internal[PATH_MAX];
    char catalog_root[PATH_MAX];
    char selector[PATH_MAX];
    char release_path[PATH_MAX];
    char selected_root[PATH_MAX];
    char selected_systems[PATH_MAX];
    char text[80];
    cs_paths paths = {0};
    cs_catalog catalog = {0};
    cs_catalog_error error = {0};

    assert(root != NULL);
    join(defaults, sizeof(defaults), root, "defaults");
    join(internal, sizeof(internal), root, "internal");
    join(catalog_root, sizeof(catalog_root), internal, "catalog");
    make_dir(defaults);
    make_dir(catalog_root);
    join(paths.systems_catalog_path, sizeof(paths.systems_catalog_path),
         defaults, "systems.json");
    join(paths.cores_catalog_path, sizeof(paths.cores_catalog_path),
         defaults, "cores.json");
    write_file(paths.systems_catalog_path,
               "{\"version\":2,\"systems\":[{\"id\":\"RELEASE\","
               "\"name\":\"Release\",\"rom_root\":\"Roms/RELEASE\"}]}");
    write_file(paths.cores_catalog_path, "{\"version\":2,\"cores\":[]}");
    join(selector, sizeof(selector), catalog_root, "current");
    join(release_path, sizeof(release_path), internal, "release.json");
    write_file(release_path, "{\"release_id\":\"release-test\"}");
    snprintf(paths.internal_data_root, sizeof(paths.internal_data_root), "%s", internal);
    snprintf(paths.catalog_selector_path, sizeof(paths.catalog_selector_path), "%s", selector);
    snprintf(paths.release_identity_path, sizeof(paths.release_identity_path), "%s", release_path);
    snprintf(paths.platform_id, sizeof(paths.platform_id), "mlp1");

    write_generation(catalog_root, gen_a, "PAKA", "Pak Group A");
    write_generation(catalog_root, gen_b, "PAKB", "Pak Group B");
    assert(snprintf(text, sizeof(text), "%s\n", gen_a) > 0);
    write_file(selector, text);
    assert(cs_catalog_load_for_paths(&paths, &catalog, &error) == 0);
    assert(strcmp(catalog.generation, gen_a) == 0);
    assert(strcmp(cs_catalog_find_system(&catalog, "PAKA")->group, "Pak Group A") == 0);
    cs_catalog_free(&catalog);

    assert(snprintf(text, sizeof(text), "%s\n", gen_b) > 0);
    write_file(selector, text);
    assert(cs_catalog_load_for_paths(&paths, &catalog, &error) == 0);
    assert(strcmp(catalog.generation, gen_b) == 0);
    assert(cs_catalog_find_system(&catalog, "PAKA") == NULL);
    assert(cs_catalog_find_system(&catalog, "PAKB") != NULL);
    cs_catalog_free(&catalog);

    write_file(selector, "broken\n");
    assert(cs_catalog_load_for_paths(&paths, &catalog, &error) == 0);
    assert(catalog.generation[0] == '\0');
    assert(cs_catalog_find_system(&catalog, "RELEASE") != NULL);
    cs_catalog_free(&catalog);

    assert(snprintf(text, sizeof(text), "%s\n", gen_a) > 0);
    write_file(selector, text);
    join(selected_root, sizeof(selected_root), catalog_root, gen_a);
    join(selected_systems, sizeof(selected_systems), selected_root, "systems.json");
    write_file(selected_systems, "{broken");
    assert(cs_catalog_load_for_paths(&paths, &catalog, &error) == 0);
    assert(cs_catalog_find_system(&catalog, "RELEASE") != NULL);
    cs_catalog_free(&catalog);

    write_file(selector, "broken\n");
    write_file(paths.systems_catalog_path, "{broken");
    assert(cs_catalog_load_for_paths(&paths, &catalog, &error) != 0);
    assert(error.kind == CS_CATALOG_ERROR_RELEASE_DEFAULTS);
    assert(strcmp(cs_catalog_error_kind_name(error.kind),
                  "release-defaults-invalid") == 0);
    return 0;
}
