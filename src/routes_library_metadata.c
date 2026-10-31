#include "cs_app.h"
#include "cs_jawaka_ipc.h"
#include "cs_library_metadata.h"
#include "cs_platforms.h"
#include "cs_routes_helpers.h"
#include "cs_server.h"

#include "civetweb.h"
#include "cjson/cJSON.h"

#include <stdlib.h>
#include <string.h>

/* Roomy for a whole system's titles and descriptions, while one bad request can't
 * exhaust the handheld. */
#define CS_METADATA_BODY_MAX (16u * 1024u * 1024u)

/* Takes out. A list of several thousand titles is too long for one printf. */
static int cs_metadata_write_object(struct mg_connection *conn, int status, const char *reason, cJSON *out) {
    char *body = out ? cJSON_PrintUnformatted(out) : NULL;
    size_t body_len = body ? strlen(body) : 0;

    cJSON_Delete(out);
    if (!body) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
    }
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n",
              status,
              reason,
              body_len);
    mg_write(conn, body, body_len);
    cJSON_free(body);
    return 1;
}

static int cs_metadata_write_status_error(struct mg_connection *conn, cs_metadata_status status) {
    if (status == CS_METADATA_UNAVAILABLE) {
        return cs_routes_write_json(conn, 503, "Service Unavailable", "{\"error\":\"editor_unavailable\"}");
    }
    return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
}

int cs_route_library_games_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_platform_info platform = {0};
    cs_jawaka_library_caps caps;
    cs_metadata_status status;
    char tag[64];
    char descriptions[8] = "";
    cJSON *out;
    cJSON *undo;
    int guard_status = cs_routes_guard_get_strict(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    if (!request || !request->query_string
        || mg_get_var(request->query_string, strlen(request->query_string), "tag", tag, sizeof(tag)) <= 0) {
        return cs_routes_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_tag\"}");
    }
    if (cs_platform_resolve(&app->paths, tag, &platform) != 0) {
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }
    (void) mg_get_var(request->query_string, strlen(request->query_string), "descriptions", descriptions,
                      sizeof(descriptions));
    out = cJSON_CreateObject();
    if (!out) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
    }
    cs_jawaka_read_library_caps(&caps);
    status = cs_metadata_list_games(&app->paths, &platform, caps.user_synopsis && descriptions[0] == '1', out);
    if (status != CS_METADATA_OK) {
        cJSON_Delete(out);
        return cs_metadata_write_status_error(conn, status);
    }
    cJSON_AddBoolToObject(out, "descriptionSupported", caps.user_synopsis);
    cJSON_AddBoolToObject(out, "orderSupported", caps.sort_rank && caps.folder_rows);
    undo = cs_metadata_undo_read(&app->paths, platform.tag);
    cJSON_AddNumberToObject(out,
                            "undoCount",
                            undo ? cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(undo, "entries")) : 0);
    cJSON_Delete(undo);
    return cs_metadata_write_object(conn, 200, "OK", out);
}

int cs_route_library_order_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_platform_info platform = {0};
    cs_jawaka_library_caps caps;
    cs_metadata_status status;
    char tag[64];
    char folder[CS_PATH_MAX] = "";
    char system[128];
    size_t query_len;
    cJSON *rows_reply;
    cJSON *out;
    cJSON *undo;
    int guard_status = cs_routes_guard_get_strict(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    query_len = request && request->query_string ? strlen(request->query_string) : 0;
    if (query_len == 0 || mg_get_var(request->query_string, query_len, "tag", tag, sizeof(tag)) <= 0) {
        return cs_routes_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_tag\"}");
    }
    (void) mg_get_var(request->query_string, query_len, "folder", folder, sizeof(folder));
    if (cs_platform_resolve(&app->paths, tag, &platform) != 0) {
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }
    cs_jawaka_read_library_caps(&caps);
    if (!caps.sort_rank || !caps.folder_rows) {
        return cs_routes_write_json(conn, 409, "Conflict", "{\"error\":\"order_unsupported\"}");
    }
    status = cs_metadata_system_id(&app->paths, &platform, system, sizeof(system));
    if (status != CS_METADATA_OK) {
        return cs_metadata_write_status_error(conn, status);
    }
    /* The launcher's own folder model, so what's arranged here is what the handheld shows. */
    rows_reply = system[0] ? cs_jawaka_read_folder_rows(system, folder) : NULL;
    if (!rows_reply) {
        return cs_routes_write_json(conn, 503, "Service Unavailable", "{\"error\":\"rows_unavailable\"}");
    }
    status = cs_metadata_add_card_keys(&app->paths, cJSON_GetObjectItemCaseSensitive(rows_reply, "rows"));
    if (status != CS_METADATA_OK) {
        cJSON_Delete(rows_reply);
        return cs_metadata_write_status_error(conn, status);
    }
    out = cJSON_CreateObject();
    if (!out) {
        cJSON_Delete(rows_reply);
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
    }
    cJSON_AddBoolToObject(out,
                          "showFolders",
                          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rows_reply, "show_folders")));
    cJSON_AddItemToObject(out, "folder", cJSON_DetachItemFromObjectCaseSensitive(rows_reply, "folder"));
    if (cJSON_GetObjectItemCaseSensitive(rows_reply, "parent")) {
        cJSON_AddItemToObject(out, "parent", cJSON_DetachItemFromObjectCaseSensitive(rows_reply, "parent"));
    } else {
        cJSON_AddNullToObject(out, "parent");
    }
    cJSON_AddItemToObject(out, "rows", cJSON_DetachItemFromObjectCaseSensitive(rows_reply, "rows"));
    cJSON_Delete(rows_reply);
    undo = cs_metadata_undo_read(&app->paths, platform.tag);
    cJSON_AddNumberToObject(out,
                            "undoCount",
                            undo ? cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(undo, "entries")) : 0);
    cJSON_Delete(undo);
    return cs_metadata_write_object(conn, 200, "OK", out);
}

int cs_route_library_game_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_platform_info platform = {0};
    cs_metadata_status status;
    char tag[64];
    char source[32];
    char path[CS_PATH_MAX];
    size_t query_len;
    cJSON *out;
    int guard_status = cs_routes_guard_get_strict(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    query_len = request && request->query_string ? strlen(request->query_string) : 0;
    if (query_len == 0 || mg_get_var(request->query_string, query_len, "tag", tag, sizeof(tag)) <= 0
        || mg_get_var(request->query_string, query_len, "source", source, sizeof(source)) <= 0
        || mg_get_var(request->query_string, query_len, "path", path, sizeof(path)) <= 0) {
        return cs_routes_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_game\"}");
    }
    if (cs_platform_resolve(&app->paths, tag, &platform) != 0) {
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }
    out = cJSON_CreateObject();
    if (!out) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
    }
    status = cs_metadata_get_game(&app->paths, &platform, source, path, out);
    if (status != CS_METADATA_OK) {
        cJSON_Delete(out);
        if (status == CS_METADATA_CONFLICT) {
            return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"game_not_found\"}");
        }
        return cs_metadata_write_status_error(conn, status);
    }
    return cs_metadata_write_object(conn, 200, "OK", out);
}

static char *cs_metadata_read_body(struct mg_connection *conn) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    long long content_length = request ? request->content_length : -1;
    size_t received = 0;
    char *body;

    if (content_length <= 0 || content_length > (long long) CS_METADATA_BODY_MAX) {
        return NULL;
    }
    body = malloc((size_t) content_length + 1);
    if (!body) {
        return NULL;
    }
    while (received < (size_t) content_length) {
        int nread = mg_read(conn, body + received, (size_t) content_length - received);

        if (nread <= 0) {
            free(body);
            return NULL;
        }
        received += (size_t) nread;
    }
    body[received] = '\0';
    return body;
}

int cs_route_library_metadata_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_platform_info platform = {0};
    cs_jawaka_library_caps caps;
    cs_metadata_status status;
    const cJSON *tag;
    const cJSON *changes;
    cJSON *root;
    cJSON *conflicts = NULL;
    cJSON *out;
    const char *problem = NULL;
    char *body;
    unsigned allowed;
    int undoable;
    int undo_kept = 0;
    int saved = 0;
    int guard_status = cs_routes_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    body = cs_metadata_read_body(conn);
    root = body ? cJSON_Parse(body) : NULL;
    free(body);
    tag = cJSON_GetObjectItemCaseSensitive(root, "tag");
    changes = cJSON_GetObjectItemCaseSensitive(root, "changes");
    if (!cJSON_IsString(tag) || !cJSON_IsArray(changes)) {
        cJSON_Delete(root);
        return cs_routes_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_request\"}");
    }
    if (cs_platform_resolve(&app->paths, tag->valuestring, &platform) != 0) {
        cJSON_Delete(root);
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }

    /* A scan can swap the second card's rows, so nothing is written during one. An
     * unreachable daemon isn't scanning, and the save still stands without it. */
    cs_jawaka_read_library_caps(&caps);
    if (caps.scan_busy) {
        cJSON_Delete(root);
        return cs_routes_write_json(conn, 409, "Conflict", "{\"error\":\"scan_running\"}");
    }

    /* Written and synced first: without a way back, a bulk apply doesn't start. */
    allowed = (caps.user_synopsis ? CS_METADATA_ALLOW_DESCRIPTION : 0u)
              | (caps.sort_rank && caps.folder_rows ? CS_METADATA_ALLOW_ORDER : 0u);
    undoable = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "undoable")) && cJSON_GetArraySize(changes) > 0;
    if (undoable) {
        cJSON *snapshot = cs_metadata_undo_build(&platform, changes, allowed);

        /* A malformed change has no snapshot, and the apply below reports why. */
        if (snapshot && cs_metadata_undo_prepare(&app->paths, snapshot) != 0) {
            cJSON_Delete(snapshot);
            cJSON_Delete(root);
            return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"undo_unavailable\"}");
        }
        undoable = snapshot != NULL;
        cJSON_Delete(snapshot);
    }

    status = cs_metadata_apply(&app->paths, &platform, changes, allowed, &saved, &conflicts, &problem);
    cJSON_Delete(root);
    if (undoable) {
        if (status == CS_METADATA_OK) {
            undo_kept = cs_metadata_undo_commit(&app->paths) == 0;
        } else {
            cs_metadata_undo_discard(&app->paths);
        }
    }
    out = cJSON_CreateObject();
    if (status == CS_METADATA_INVALID) {
        cJSON_AddStringToObject(out, "error", problem ? problem : "invalid_request");
        return cs_metadata_write_object(conn, 400, "Bad Request", out);
    }
    if (status == CS_METADATA_CONFLICT) {
        cJSON_AddStringToObject(out, "error", "conflict");
        cJSON_AddItemToObject(out, "conflicts", conflicts);
        return cs_metadata_write_object(conn, 409, "Conflict", out);
    }
    if (status != CS_METADATA_OK) {
        cJSON_Delete(out);
        return cs_metadata_write_status_error(conn, status);
    }

    cJSON_AddBoolToObject(out, "ok", 1);
    cJSON_AddNumberToObject(out, "saved", saved);
    cJSON_AddBoolToObject(out, "refreshed", saved == 0 || cs_jawaka_notify_library_metadata(&caps) == 0);
    cJSON_AddBoolToObject(out, "undoKept", undo_kept);
    return cs_metadata_write_object(conn, 200, "OK", out);
}

int cs_route_library_undo_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_platform_info platform = {0};
    cs_jawaka_library_caps caps;
    cs_metadata_status status;
    const cJSON *tag;
    cJSON *root;
    cJSON *snapshot;
    cJSON *skipped;
    cJSON *out;
    char *body;
    int restored = 0;
    int guard_status = cs_routes_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    body = cs_metadata_read_body(conn);
    root = body ? cJSON_Parse(body) : NULL;
    free(body);
    tag = cJSON_GetObjectItemCaseSensitive(root, "tag");
    if (!cJSON_IsString(tag)) {
        cJSON_Delete(root);
        return cs_routes_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_request\"}");
    }
    if (cs_platform_resolve(&app->paths, tag->valuestring, &platform) != 0) {
        cJSON_Delete(root);
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }
    cJSON_Delete(root);

    cs_jawaka_read_library_caps(&caps);
    if (caps.scan_busy) {
        return cs_routes_write_json(conn, 409, "Conflict", "{\"error\":\"scan_running\"}");
    }
    snapshot = cs_metadata_undo_read(&app->paths, platform.tag);
    if (!snapshot) {
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"nothing_to_undo\"}");
    }
    out = cJSON_CreateObject();
    skipped = out ? cJSON_AddArrayToObject(out, "skipped") : NULL;
    if (!skipped) {
        cJSON_Delete(snapshot);
        cJSON_Delete(out);
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
    }
    status = cs_metadata_undo_apply(&app->paths, &platform, snapshot, &restored, skipped);
    cJSON_Delete(snapshot);
    if (status != CS_METADATA_OK) {
        cJSON_Delete(out);
        return cs_metadata_write_status_error(conn, status);
    }
    cs_metadata_undo_clear(&app->paths);
    cJSON_AddBoolToObject(out, "ok", 1);
    cJSON_AddNumberToObject(out, "restored", restored);
    cJSON_AddBoolToObject(out, "refreshed", restored == 0 || cs_jawaka_notify_library_metadata(&caps) == 0);
    return cs_metadata_write_object(conn, 200, "OK", out);
}
