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

/* Roomy for a whole system's titles, while one bad request can't exhaust the handheld. */
#define CS_METADATA_BODY_MAX (4u * 1024u * 1024u)

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
    cJSON *out;
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
    out = cJSON_CreateObject();
    if (!out) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"internal\"}");
    }
    status = cs_metadata_list_games(&app->paths, &platform, out);
    if (status != CS_METADATA_OK) {
        cJSON_Delete(out);
        return cs_metadata_write_status_error(conn, status);
    }
    cs_jawaka_read_library_caps(&caps);
    cJSON_AddBoolToObject(out, "descriptionSupported", caps.user_synopsis);
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

    status = cs_metadata_apply(&app->paths, &platform, changes, caps.user_synopsis, &saved, &conflicts, &problem);
    cJSON_Delete(root);
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
    return cs_metadata_write_object(conn, 200, "OK", out);
}
