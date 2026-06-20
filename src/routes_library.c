#include "cs_app.h"
#include "cs_library.h"
#include "cs_platforms.h"
#include "cs_server.h"
#include "cs_states.h"

#include "civetweb.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int cs_write_json(struct mg_connection *conn, int status, const char *reason, const char *body);
static int cs_method_is(const struct mg_connection *conn, const char *method);

static int cs_route_guard_get(struct mg_connection *conn, void *cbdata) {
    const char *cookie = mg_get_header(conn, "Cookie");

    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}

static int cs_route_guard_post(struct mg_connection *conn, void *cbdata) {
    const char *cookie = mg_get_header(conn, "Cookie");
    const char *csrf = mg_get_header(conn, "X-CS-CSRF");

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_csrf_is_valid(cookie, csrf)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}

static int cs_read_body(struct mg_connection *conn, char *buffer, size_t buffer_size, size_t *body_len_out) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    long long content_length;
    size_t received = 0;

    if (!request || !buffer || buffer_size == 0 || !body_len_out) {
        return -1;
    }

    content_length = request->content_length;
    if (content_length < 0 || (size_t) content_length >= buffer_size) {
        return -1;
    }

    while (received < (size_t) content_length) {
        int nread = mg_read(conn, buffer + received, (size_t) content_length - received);

        if (nread <= 0) {
            return -1;
        }
        received += (size_t) nread;
    }

    buffer[received] = '\0';
    *body_len_out = received;
    return 0;
}

static int cs_method_is(const struct mg_connection *conn, const char *method) {
    const struct mg_request_info *request = mg_get_request_info(conn);

    return request && request->request_method && strcmp(request->request_method, method) == 0;
}

static int cs_parse_offset(const char *value, size_t *offset_out) {
    char *end = NULL;
    unsigned long parsed;

    if (!value || !offset_out) {
        return -1;
    }
    if (value[0] == '\0') {
        *offset_out = 0;
        return 0;
    }
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return -1;
        }
    }

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || (size_t) parsed != parsed) {
        return -1;
    }

    *offset_out = (size_t) parsed;
    return 0;
}

static int cs_parse_browser_sort_column(const char *value, cs_browser_sort_column *column_out) {
    if (!value || !column_out) {
        return -1;
    }
    if (value[0] == '\0' || strcmp(value, "name") == 0) {
        *column_out = CS_BROWSER_SORT_NAME;
        return 0;
    }
    if (strcmp(value, "size") == 0) {
        *column_out = CS_BROWSER_SORT_SIZE;
        return 0;
    }
    if (strcmp(value, "modified") == 0) {
        *column_out = CS_BROWSER_SORT_MODIFIED;
        return 0;
    }

    return -1;
}

static int cs_parse_browser_sort_direction(const char *value, cs_browser_sort_direction *direction_out) {
    if (!value || !direction_out) {
        return -1;
    }
    if (value[0] == '\0' || strcmp(value, "asc") == 0) {
        *direction_out = CS_BROWSER_SORT_ASC;
        return 0;
    }
    if (strcmp(value, "desc") == 0) {
        *direction_out = CS_BROWSER_SORT_DESC;
        return 0;
    }

    return -1;
}

static int cs_write_json(struct mg_connection *conn, int status, const char *reason, const char *body) {
    size_t body_len = body ? strlen(body) : 0;

    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              status,
              reason,
              body_len,
              body ? body : "");
    return 1;
}

static int cs_stream_begin_json_response(struct mg_connection *conn) {
    if (!conn) {
        return -1;
    }

    return mg_printf(conn,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     CS_SERVER_SECURITY_HEADERS_HTTP
                     "Cache-Control: no-store\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "\r\n")
                   <= 0
               ? -1
               : 0;
}

static int cs_stream_begin_ndjson_response(struct mg_connection *conn) {
    if (!conn) {
        return -1;
    }

    return mg_printf(conn,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/x-ndjson\r\n"
                     CS_SERVER_SECURITY_HEADERS_HTTP
                     "Cache-Control: no-store\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "\r\n")
                   <= 0
               ? -1
               : 0;
}

static int cs_stream_literal(struct mg_connection *conn, const char *literal) {
    size_t len;

    if (!conn || !literal) {
        return -1;
    }

    len = strlen(literal);
    if (len == 0) {
        return 0;
    }

    return mg_send_chunk(conn, literal, (unsigned int) len) < 0 ? -1 : 0;
}

static int cs_stream_escaped_string(struct mg_connection *conn, const char *value) {
    const unsigned char *cursor = (const unsigned char *) (value ? value : "");
    char out[512];
    size_t used = 0;

    while (*cursor != '\0') {
        const char *fragment = NULL;
        size_t fragment_len = 0;
        char escaped[8];

        switch (*cursor) {
            case '\\':
                fragment = "\\\\";
                fragment_len = 2;
                break;
            case '"':
                fragment = "\\\"";
                fragment_len = 2;
                break;
            case '\n':
                fragment = "\\n";
                fragment_len = 2;
                break;
            case '\r':
                fragment = "\\r";
                fragment_len = 2;
                break;
            case '\t':
                fragment = "\\t";
                fragment_len = 2;
                break;
            default:
                if (*cursor < 0x20) {
                    int n = snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int) *cursor);

                    if (n <= 0 || (size_t) n >= sizeof(escaped)) {
                        return -1;
                    }
                    fragment = escaped;
                    fragment_len = (size_t) n;
                } else {
                    escaped[0] = (char) *cursor;
                    escaped[1] = '\0';
                    fragment = escaped;
                    fragment_len = 1;
                }
                break;
        }

        if (used + fragment_len > sizeof(out)) {
            if (mg_send_chunk(conn, out, (unsigned int) used) < 0) {
                return -1;
            }
            used = 0;
        }

        memcpy(out + used, fragment, fragment_len);
        used += fragment_len;
        cursor += 1;
    }

    if (used > 0 && mg_send_chunk(conn, out, (unsigned int) used) < 0) {
        return -1;
    }

    return 0;
}

static int cs_stream_unsigned(struct mg_connection *conn, unsigned long long value) {
    char buffer[64];

    if (snprintf(buffer, sizeof(buffer), "%llu", value) < 0) {
        return -1;
    }

    return cs_stream_literal(conn, buffer);
}

static int cs_stream_signed(struct mg_connection *conn, long long value) {
    char buffer[64];

    if (snprintf(buffer, sizeof(buffer), "%lld", value) < 0) {
        return -1;
    }

    return cs_stream_literal(conn, buffer);
}

static int cs_stream_browser_entry(struct mg_connection *conn, const cs_browser_entry *entry, int *first_entry) {
    if (!conn || !entry || !first_entry) {
        return -1;
    }
    if (!*first_entry && cs_stream_literal(conn, ",") != 0) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"name\":\"") != 0
        || cs_stream_escaped_string(conn, entry->name) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, entry->path) != 0
        || cs_stream_literal(conn, "\",\"type\":\"") != 0
        || cs_stream_escaped_string(conn, entry->type) != 0
        || cs_stream_literal(conn, "\",\"size\":") != 0
        || cs_stream_unsigned(conn, entry->size) != 0
        || cs_stream_literal(conn, ",\"modified\":") != 0
        || cs_stream_signed(conn, entry->modified) != 0
        || cs_stream_literal(conn, ",\"status\":\"") != 0
        || cs_stream_escaped_string(conn, entry->status) != 0
        || cs_stream_literal(conn, "\",\"thumbnailPath\":\"") != 0
        || cs_stream_escaped_string(conn, entry->thumbnail_path) != 0
        || cs_stream_literal(conn, "\",\"favorite\":") != 0
        || cs_stream_literal(conn, entry->favorite ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"favoriteSupported\":") != 0
        || cs_stream_literal(conn, entry->favorite_supported ? "true" : "false") != 0
        || cs_stream_literal(conn, "}") != 0) {
        return -1;
    }

    *first_entry = 0;
    return 0;
}

static int cs_stream_breadcrumb(struct mg_connection *conn,
                                const cs_browser_breadcrumb *breadcrumb,
                                int *first_entry) {
    if (!conn || !breadcrumb || !first_entry) {
        return -1;
    }
    if (!*first_entry && cs_stream_literal(conn, ",") != 0) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"label\":\"") != 0
        || cs_stream_escaped_string(conn, breadcrumb->label) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, breadcrumb->path) != 0
        || cs_stream_literal(conn, "\"}") != 0) {
        return -1;
    }

    *first_entry = 0;
    return 0;
}

static int cs_parse_favorite_value(const char *value, int *favorite_out) {
    if (!value || !favorite_out) {
        return -1;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
        *favorite_out = 1;
        return 0;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0) {
        *favorite_out = 0;
        return 0;
    }
    return -1;
}

int cs_route_game_favorite_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    char body[4096];
    char tag_value[64];
    char path_value[CS_PATH_MAX];
    char favorite_value[16];
    cs_platform_info resolved_platform = {0};
    size_t body_len = 0;
    int favorite = 0;
    int guard_status = cs_route_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (cs_read_body(conn, body, sizeof(body), &body_len) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (mg_get_var(body, body_len, "tag", tag_value, sizeof(tag_value)) <= 0
        || mg_get_var(body, body_len, "path", path_value, sizeof(path_value)) <= 0
        || mg_get_var(body, body_len, "favorite", favorite_value, sizeof(favorite_value)) <= 0
        || cs_parse_favorite_value(favorite_value, &favorite) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (cs_platform_resolve(&app->paths, tag_value, &resolved_platform) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }
    if (cs_library_db_set_game_favorite(&app->paths, &resolved_platform, path_value, favorite) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"error\":\"favorite_target_not_found\"}");
    }

    return cs_write_json(conn, 200, "OK", "{\"ok\":true}");
}

static int cs_count_files_recursive(const char *path, int allow_hidden) {
    DIR *dir;
    struct dirent *entry;
    int total = 0;

    if (!path) {
        return 0;
    }
    dir = opendir(path);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child[CS_PATH_MAX];
        struct stat st;
        int is_dir;
        int is_reg;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!allow_hidden && entry->d_name[0] == '.') {
            continue;
        }
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) < 0) {
            continue;
        }

        /* Skip lstat() when d_type is reliable. Device filesystems (ext4/F2FS) report it,
           which roughly halves syscalls per scan on large libraries. Symlinks and DT_UNKNOWN
           still fall through to the lstat path. */
        is_dir = 0;
        is_reg = 0;
#ifdef DT_REG
        if (entry->d_type == DT_REG) {
            is_reg = 1;
        } else if (entry->d_type == DT_DIR) {
            is_dir = 1;
        } else if (entry->d_type != DT_UNKNOWN && entry->d_type != DT_LNK) {
            continue;
        } else
#endif
        {
            if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) {
                continue;
            }
            is_dir = S_ISDIR(st.st_mode);
            is_reg = S_ISREG(st.st_mode);
        }

        if (is_dir) {
            if (cs_platform_is_shortcut_directory(entry->d_name, child)) {
                continue;
            }
            total += cs_count_files_recursive(child, allow_hidden);
        } else if (is_reg) {
            total += 1;
        }
    }

    closedir(dir);
    return total;
}

static int cs_stream_platform_object(struct mg_connection *conn,
                                     const cs_paths *paths,
                                     const cs_platform_info *platform) {
    char rom_root[CS_PATH_MAX];
    char save_root[CS_PATH_MAX];
    char bios_root[CS_PATH_MAX];
    char overlays_root[CS_PATH_MAX];
    char cheats_root[CS_PATH_MAX];
    size_t state_count = 0;
    int rom_count = 0;
    int save_count = 0;
    int bios_count = 0;
    int overlay_count = 0;
    int cheat_count = 0;
    int supports_roms;
    int supports_saves;
    int supports_states;
    int supports_bios;
    int supports_overlays;
    int supports_cheats;

    if (!conn || !paths || !platform) {
        return -1;
    }
    supports_roms = cs_platform_supports_resource(platform, "roms");
    supports_saves = cs_platform_supports_resource(platform, "saves");
    supports_states = cs_platform_supports_resource(platform, "states");
    supports_bios = cs_platform_supports_resource(platform, "bios");
    supports_overlays = cs_platform_supports_resource(platform, "overlays");
    supports_cheats = cs_platform_supports_resource(platform, "cheats");

    if (supports_roms) {
        if (cs_library_db_count_roms_for_platform(paths, platform, &rom_count) != 0
            && cs_browser_root_for_scope(paths, CS_SCOPE_ROMS, platform, rom_root, sizeof(rom_root)) == 0) {
            rom_count = cs_count_files_recursive(rom_root, 0);
        }
    }
    if (supports_saves && cs_browser_root_for_scope(paths, CS_SCOPE_SAVES, platform, save_root, sizeof(save_root)) == 0) {
        save_count = cs_count_files_recursive(save_root, 0);
    }
    if (!supports_states || cs_states_collect(paths, platform, NULL, 0, &state_count, NULL) != 0) {
        state_count = 0;
    }
    if (supports_bios && cs_browser_root_for_scope(paths, CS_SCOPE_BIOS, platform, bios_root, sizeof(bios_root)) == 0) {
        bios_count = cs_count_files_recursive(bios_root, 0);
    }
    if (supports_overlays
        && cs_browser_root_for_scope(paths, CS_SCOPE_OVERLAYS, platform, overlays_root, sizeof(overlays_root)) == 0) {
        overlay_count = cs_count_files_recursive(overlays_root, 0);
    }
    if (supports_cheats
        && cs_browser_root_for_scope(paths, CS_SCOPE_CHEATS, platform, cheats_root, sizeof(cheats_root)) == 0) {
        cheat_count = cs_count_files_recursive(cheats_root, 0);
    }

    if (cs_stream_literal(conn, "{\"tag\":\"") != 0
        || cs_stream_escaped_string(conn, platform->tag) != 0
        || cs_stream_literal(conn, "\",\"name\":\"") != 0
        || cs_stream_escaped_string(conn, platform->name) != 0
        || cs_stream_literal(conn, "\",\"group\":\"") != 0
        || cs_stream_escaped_string(conn, platform->group) != 0
        || cs_stream_literal(conn, "\",\"icon\":\"") != 0
        || cs_stream_escaped_string(conn, platform->icon) != 0
        || cs_stream_literal(conn, "\",\"isCustom\":") != 0
        || cs_stream_literal(conn, platform->is_custom ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"romPath\":\"Roms/") != 0
        || cs_stream_escaped_string(conn, platform->rom_directory) != 0
        || cs_stream_literal(conn, "\",\"savePath\":\"Saves/") != 0
        || cs_stream_escaped_string(conn, platform->primary_code) != 0
        || cs_stream_literal(conn, "\",\"biosPath\":\"BIOS/") != 0
        || cs_stream_escaped_string(conn, platform->primary_code) != 0
        || cs_stream_literal(conn, "\",\"supportedResources\":{\"roms\":") != 0
        || cs_stream_literal(conn, supports_roms ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"saves\":") != 0
        || cs_stream_literal(conn, supports_saves ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"states\":") != 0
        || cs_stream_literal(conn, supports_states ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"bios\":") != 0
        || cs_stream_literal(conn, supports_bios ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"overlays\":") != 0
        || cs_stream_literal(conn, supports_overlays ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"cheats\":") != 0
        || cs_stream_literal(conn, supports_cheats ? "true" : "false") != 0
        || cs_stream_literal(conn, "},\"counts\":{\"roms\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) rom_count) != 0
        || cs_stream_literal(conn, ",\"saves\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) save_count) != 0
        || cs_stream_literal(conn, ",\"states\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) state_count) != 0
        || cs_stream_literal(conn, ",\"bios\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) bios_count) != 0
        || cs_stream_literal(conn, ",\"overlays\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) overlay_count) != 0
        || cs_stream_literal(conn, ",\"cheats\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) cheat_count) != 0
        || cs_stream_literal(conn, "}}") != 0) {
        return -1;
    }

    return 0;
}

static int cs_stream_platform_event(struct mg_connection *conn,
                                    const cs_paths *paths,
                                    const cs_platform_info *platform) {
    if (!conn || !platform) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"type\":\"platform\",\"group\":\"") != 0
        || cs_stream_escaped_string(conn, platform->group) != 0
        || cs_stream_literal(conn, "\",\"platform\":") != 0
        || cs_stream_platform_object(conn, paths, platform) != 0
        || cs_stream_literal(conn, "}\n") != 0) {
        return -1;
    }
    return 0;
}

static int cs_stream_catalog_error_event(struct mg_connection *conn, const cs_catalog_error *error) {
    const char *kind = error ? cs_catalog_error_kind_name(error->kind) : "parse";
    const char *path = error ? error->path : "";

    if (!conn) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"type\":\"catalog_error\",\"kind\":\"") != 0
        || cs_stream_escaped_string(conn, kind) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, path) != 0
        || cs_stream_literal(conn, "\"}\n") != 0) {
        return -1;
    }
    return 0;
}

int cs_route_platforms_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_platform_info platforms[256];
    size_t platform_count = 0;
    size_t i;
    int guard_status;
    cs_catalog_error catalog_error = {0};

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_route_guard_get(conn, cbdata);
    if (guard_status != 0) {
        return guard_status;
    }

    if (cs_stream_begin_ndjson_response(conn) != 0) {
        return 1;
    }

    if (cs_platform_discover_with_error(&app->paths,
                                        platforms,
                                        sizeof(platforms) / sizeof(platforms[0]),
                                        &platform_count,
                                        &catalog_error)
        != 0) {
        if (catalog_error.kind != CS_CATALOG_ERROR_NONE) {
            if (cs_stream_catalog_error_event(conn, &catalog_error) != 0
                || cs_stream_literal(conn, "{\"type\":\"done\"}\n") != 0
                || mg_send_chunk(conn, "", 0) < 0) {
                goto stream_fail;
            }
            return 1;
        }
        goto stream_fail;
    }

    /* Emit one NDJSON line per platform, flushed as the counts finish so the browser
       can render cards incrementally instead of waiting for all platforms to scan. */
    for (i = 0; i < platform_count; ++i) {
        if (cs_stream_platform_event(conn, &app->paths, &platforms[i])
            != 0) {
            goto stream_fail;
        }
    }

    if (cs_stream_literal(conn, "{\"type\":\"done\"}\n") != 0 || mg_send_chunk(conn, "", 0) < 0) {
        goto stream_fail;
    }

    return 1;

stream_fail:
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}

int cs_route_browser_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char scope_value[32];
    char tag_value[64];
    char path_value[CS_PATH_MAX];
    char offset_value[32];
    char query_value[CS_BROWSER_QUERY_MAX];
    char sort_value[32];
    char direction_value[16];
    cs_browser_scope scope;
    cs_browser_sort_options sort_options = {CS_BROWSER_SORT_NAME, CS_BROWSER_SORT_ASC};
    cs_platform_info resolved_platform = {0};
    const cs_platform_info *platform = NULL;
    cs_browser_result *result = NULL;
    cs_browser_list_status browser_status;
    size_t i;
    size_t offset = 0;
    int first_entry = 1;
    int guard_status;

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_route_guard_get(conn, cbdata);
    if (guard_status != 0) {
        return guard_status;
    }

    memset(scope_value, 0, sizeof(scope_value));
    memset(tag_value, 0, sizeof(tag_value));
    memset(path_value, 0, sizeof(path_value));
    memset(offset_value, 0, sizeof(offset_value));
    memset(query_value, 0, sizeof(query_value));
    memset(sort_value, 0, sizeof(sort_value));
    memset(direction_value, 0, sizeof(direction_value));

    if (!request || !request->query_string || request->query_string[0] == '\0') {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_scope\"}");
    }
    if (mg_get_var(request->query_string, strlen(request->query_string), "scope", scope_value, sizeof(scope_value))
        <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_scope\"}");
    }
    scope = cs_browser_scope_parse(scope_value);
    if (scope == CS_SCOPE_INVALID) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_scope\"}");
    }

    (void) mg_get_var(request->query_string, strlen(request->query_string), "tag", tag_value, sizeof(tag_value));
    (void) mg_get_var(request->query_string, strlen(request->query_string), "path", path_value, sizeof(path_value));
    (void) mg_get_var(request->query_string,
                      strlen(request->query_string),
                      "offset",
                      offset_value,
                      sizeof(offset_value));
    (void) mg_get_var(request->query_string,
                      strlen(request->query_string),
                      "q",
                      query_value,
                      sizeof(query_value));
    (void) mg_get_var(request->query_string, strlen(request->query_string), "sort", sort_value, sizeof(sort_value));
    (void) mg_get_var(request->query_string,
                      strlen(request->query_string),
                      "direction",
                      direction_value,
                      sizeof(direction_value));

    if (cs_parse_offset(offset_value, &offset) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_offset\"}");
    }
    if (cs_parse_browser_sort_column(sort_value, &sort_options.column) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_sort\"}");
    }
    if (cs_parse_browser_sort_direction(direction_value, &sort_options.direction) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_sort_direction\"}");
    }

    if (cs_browser_scope_requires_platform(scope)) {
        if (cs_platform_resolve(&app->paths, tag_value, &resolved_platform) != 0) {
            return cs_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
        }
        if (!cs_browser_scope_supported_for_platform(&resolved_platform, scope)) {
            return cs_write_json(conn, 404, "Not Found", "{\"error\":\"scope_not_supported\"}");
        }
        platform = &resolved_platform;
    }

    result = (cs_browser_result *) calloc(1, sizeof(*result));
    if (!result) {
        free(result);
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"alloc_failed\"}");
    }

    browser_status = cs_browser_list_with_sort(&app->paths,
                                               scope,
                                               platform,
                                               path_value,
                                               offset,
                                               query_value,
                                               &sort_options,
                                               result);
    if (browser_status != CS_BROWSER_LIST_OK) {
        free(result);
        if (browser_status == CS_BROWSER_LIST_NOT_FOUND) {
            return cs_write_json(conn, 404, "Not Found", "{\"error\":\"path_not_found\"}");
        }
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"browser_list_failed\"}");
    }

    if (cs_stream_begin_json_response(conn) != 0) {
        free(result);
        return 1;
    }
    if (cs_stream_literal(conn, "{\"scope\":\"") != 0
        || cs_stream_escaped_string(conn, result->scope) != 0
        || cs_stream_literal(conn, "\",\"title\":\"") != 0
        || cs_stream_escaped_string(conn, result->title) != 0
        || cs_stream_literal(conn, "\",\"rootPath\":\"") != 0
        || cs_stream_escaped_string(conn, result->root_path) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, result->path) != 0
        || cs_stream_literal(conn, "\",\"breadcrumbs\":[") != 0) {
        goto stream_fail;
    }

    first_entry = 1;
    for (i = 0; i < result->breadcrumb_count; ++i) {
        if (cs_stream_breadcrumb(conn, &result->breadcrumbs[i], &first_entry) != 0) {
            goto stream_fail;
        }
    }
    if (cs_stream_literal(conn, "],\"entries\":[") != 0) {
        goto stream_fail;
    }

    first_entry = 1;
    for (i = 0; i < result->count; ++i) {
        if (cs_stream_browser_entry(conn, &result->entries[i], &first_entry) != 0) {
            goto stream_fail;
        }
    }

    if (cs_stream_literal(conn, "],\"totalCount\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) result->total_count) != 0
        || cs_stream_literal(conn, ",\"offset\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) result->offset) != 0
        || cs_stream_literal(conn, ",\"truncated\":") != 0
        || cs_stream_literal(conn, result->truncated ? "true" : "false") != 0) {
        goto stream_fail;
    }
    if (cs_stream_literal(conn, "}") != 0 || mg_send_chunk(conn, "", 0) < 0) {
        goto stream_fail;
    }

    free(result);
    return 1;

stream_fail:
    free(result);
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}
