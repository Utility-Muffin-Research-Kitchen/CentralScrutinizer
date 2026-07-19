#include "cs_app.h"
#include "cs_library.h"
#include "cs_platforms.h"
#include "cs_rom_policy.h"
#include "cs_routes_helpers.h"
#include "cs_server.h"
#include "cs_uploads.h"

#include "cjson/cJSON.h"
#include "civetweb.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CS_UPLOAD_MAX_FILES 32
#define CS_UPLOAD_MAX_DIRECTORIES 32
#define CS_UPLOAD_PREVIEW_MANIFEST_MAX 8192
#define CS_UPLOAD_ROM_REPORT_MAX 16
#define CS_UPLOAD_MAX_REQUEST_BYTES_DEFAULT (8LL * 1024 * 1024 * 1024)
#define CS_UPLOAD_MAX_BIOS_FILE_BYTES_DEFAULT (64LL * 1024 * 1024)
#define CS_UPLOAD_PREVIEW_TRACK_MAX 256
#define CS_UPLOAD_PREVIEW_RESULT_MAX CS_UPLOAD_PREVIEW_TRACK_MAX

typedef struct cs_upload_preview_file {
    char filename[256];
    char *relative_dir;
} cs_upload_preview_file;

typedef struct cs_upload_preview_bundle_member {
    const cs_upload_preview_file *file;
    cs_rom_entry_status status;
} cs_upload_preview_bundle_member;

typedef struct cs_upload_request {
    cs_app *app;
    char scope[32];
    char tag[64];
    char path[CS_PATH_MAX];
    char file_names[CS_UPLOAD_MAX_FILES][256];
    char relative_dirs[CS_UPLOAD_MAX_FILES][CS_PATH_MAX];
    cs_upload_preview_file *preview_files;
    size_t preview_file_capacity;
    char directories[CS_UPLOAD_MAX_DIRECTORIES][CS_PATH_MAX];
    char final_root[CS_PATH_MAX];
    char final_guard_root[CS_PATH_MAX];
    unsigned int path_flags;
    int metadata_ready;
    int failed;
    int source_required;
    int overwrite_existing;
    cs_upload_plan plans[CS_UPLOAD_MAX_FILES];
    long long file_sizes[CS_UPLOAD_MAX_FILES];
    size_t directory_count;
    size_t plan_count;
    size_t preview_file_count;
    size_t stored_count;
} cs_upload_request;

typedef enum cs_upload_preview_kind {
    CS_UPLOAD_PREVIEW_OVERWRITE = 0,
    CS_UPLOAD_PREVIEW_FILE_OVER_DIRECTORY = 1,
    CS_UPLOAD_PREVIEW_DIRECTORY_OVER_FILE = 2,
} cs_upload_preview_kind;

typedef struct cs_upload_preview_conflict {
    char path[CS_PATH_MAX];
    cs_upload_preview_kind kind;
} cs_upload_preview_conflict;

typedef struct cs_upload_preview_result {
    cs_upload_preview_conflict overwriteable[CS_UPLOAD_PREVIEW_RESULT_MAX];
    cs_upload_preview_conflict blocking[CS_UPLOAD_PREVIEW_RESULT_MAX];
    char seen_paths[CS_UPLOAD_PREVIEW_TRACK_MAX][CS_PATH_MAX];
    cs_upload_preview_kind seen_kinds[CS_UPLOAD_PREVIEW_TRACK_MAX];
    size_t overwriteable_count;
    size_t blocking_count;
    size_t overwriteable_preview_count;
    size_t blocking_preview_count;
    size_t seen_count;
    char unsupported[CS_UPLOAD_ROM_REPORT_MAX][CS_PATH_MAX];
    char unsupported_reasons[CS_UPLOAD_ROM_REPORT_MAX][32];
    char **bundle_entrypoints;
    size_t unsupported_count;
    size_t unsupported_preview_count;
    size_t entrypoint_count;
    size_t companion_count;
    size_t bundle_entrypoint_count;
    size_t bundle_entrypoint_capacity;
} cs_upload_preview_result;

static void cs_upload_top_folder(const char *rel, char *out, size_t out_size);

static int cs_method_is(const struct mg_connection *conn, const char *method) {
    const struct mg_request_info *request = mg_get_request_info(conn);

    return request && request->request_method && strcmp(request->request_method, method) == 0;
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

static int cs_write_upload_bad_request(struct mg_connection *conn,
                                       const char *route,
                                       const char *error,
                                       const char *detail) {
    char body[160];
    const char *route_name = route && route[0] != '\0' ? route : "upload";
    const char *error_name = error && error[0] != '\0' ? error : "upload_bad_request";
    int written;

    if (detail && detail[0] != '\0') {
        fprintf(stderr,
                "Central Scrutinizer %s rejected bad request: %s (%s)\n",
                route_name,
                error_name,
                detail);
    } else {
        fprintf(stderr, "Central Scrutinizer %s rejected bad request: %s\n", route_name, error_name);
    }

    written = snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", error_name);
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false,\"error\":\"upload_bad_request\"}");
    }
    return cs_write_json(conn, 400, "Bad Request", body);
}

static void cs_upload_write_form_failure_detail(char *detail,
                                                size_t detail_size,
                                                int handled_fields,
                                                const cs_upload_request *state) {
    if (!detail || detail_size == 0) {
        return;
    }
    if (!state) {
        (void) snprintf(detail, detail_size, "handled_fields=%d", handled_fields);
        return;
    }
    (void) snprintf(detail,
                    detail_size,
                    "handled_fields=%d failed=%d files=%zu stored=%zu preview_files=%zu dirs=%zu",
                    handled_fields,
                    state->failed,
                    state->plan_count,
                    state->stored_count,
                    state->preview_file_count,
                    state->directory_count);
}

static int cs_write_upload_conflict_response(struct mg_connection *conn, const char *error, const char *path) {
    const char *error_name = error && strcmp(error, "upload_type_conflict") == 0 ? "upload_type_conflict" : "upload_conflict";

    if (!path || path[0] == '\0') {
        return cs_write_json(conn,
                             409,
                             "Conflict",
                             strcmp(error_name, "upload_type_conflict") == 0
                                 ? "{\"ok\":false,\"error\":\"upload_type_conflict\"}"
                                 : "{\"ok\":false,\"error\":\"upload_conflict\"}");
    }

    if (mg_printf(conn,
                  "HTTP/1.1 409 Conflict\r\n"
                  "Content-Type: application/json\r\n"
                  CS_SERVER_SECURITY_HEADERS_HTTP
                  "Cache-Control: no-store\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n")
            <= 0) {
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"ok\":false,\"error\":\"") != 0
        || cs_routes_stream_literal(conn, error_name) != 0
        || cs_routes_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_routes_stream_escaped_string(conn, path) != 0
        || cs_routes_stream_literal(conn, "\"}") != 0
        || mg_send_chunk(conn, "", 0) < 0) {
        (void) mg_send_chunk(conn, "", 0);
    }

    return 1;
}

static int cs_write_upload_errno_response(struct mg_connection *conn, const char *path) {
    if (errno == EEXIST) {
        return cs_write_upload_conflict_response(conn, "upload_conflict", path);
    }
    if (errno == EISDIR || errno == ENOTDIR || errno == EINVAL || errno == ENOTEMPTY) {
        return cs_write_upload_conflict_response(conn, "upload_type_conflict", path);
    }

    return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
}

static int cs_upload_filename_is_safe(const char *filename) {
    return cs_validate_path_component_with_flags(filename, CS_PATH_FLAG_ALLOW_HIDDEN) == 0;
}

static long long cs_upload_env_limit_bytes(const char *name, long long fallback) {
    const char *value = getenv(name);
    char *end = NULL;
    long long parsed;

    if (!value || value[0] == '\0') {
        return fallback;
    }

    parsed = strtoll(value, &end, 10);
    if (!end || *end != '\0' || parsed <= 0) {
        return fallback;
    }

    return parsed;
}

static long long cs_upload_request_limit_bytes(void) {
    return cs_upload_env_limit_bytes("CS_MAX_UPLOAD_REQUEST_BYTES", CS_UPLOAD_MAX_REQUEST_BYTES_DEFAULT);
}

static long long cs_upload_file_limit_bytes(const cs_upload_request *state) {
    cs_browser_scope scope;

    if (!state) {
        return -1;
    }

    scope = cs_browser_scope_parse(state->scope);
    if (scope == CS_SCOPE_BIOS) {
        return cs_upload_env_limit_bytes("CS_MAX_BIOS_UPLOAD_BYTES", CS_UPLOAD_MAX_BIOS_FILE_BYTES_DEFAULT);
    }

    return cs_upload_request_limit_bytes();
}

static int cs_upload_split_client_path(const char *client_path,
                                       char *relative_dir,
                                       size_t relative_dir_size,
                                       char *filename,
                                       size_t filename_size) {
    const char *leaf;
    size_t dir_length;

    if (!client_path || !relative_dir || relative_dir_size == 0 || !filename || filename_size == 0) {
        return -1;
    }

    leaf = strrchr(client_path, '/');
    if (!leaf) {
        if (!cs_upload_filename_is_safe(client_path)) {
            return -1;
        }
        if (snprintf(relative_dir, relative_dir_size, "%s", "") >= (int) relative_dir_size
            || snprintf(filename, filename_size, "%s", client_path) >= (int) filename_size) {
            return -1;
        }
        return 0;
    }

    dir_length = (size_t) (leaf - client_path);
    if (dir_length == 0 || client_path[dir_length + 1] == '\0') {
        return -1;
    }
    if (dir_length >= relative_dir_size) {
        return -1;
    }

    memcpy(relative_dir, client_path, dir_length);
    relative_dir[dir_length] = '\0';
    if (cs_validate_relative_path_with_flags(relative_dir, CS_PATH_FLAG_ALLOW_EMPTY | CS_PATH_FLAG_ALLOW_HIDDEN) != 0) {
        return -1;
    }
    if (!cs_upload_filename_is_safe(leaf + 1)) {
        return -1;
    }
    if (snprintf(filename, filename_size, "%s", leaf + 1) >= (int) filename_size) {
        return -1;
    }

    return 0;
}

static int cs_upload_join_relative_path(const char *base, const char *child, char *buffer, size_t buffer_size) {
    int written;

    if (!base || !child || !buffer || buffer_size == 0) {
        return -1;
    }

    if (base[0] != '\0' && child[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s/%s", base, child);
    } else if (base[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s", base);
    } else {
        written = snprintf(buffer, buffer_size, "%s", child);
    }

    return written < 0 || (size_t) written >= buffer_size ? -1 : 0;
}

static const char *cs_upload_guard_root_for_final(const cs_paths *paths, const char *final_root) {
    size_t i;

    if (!paths || !final_root) {
        return NULL;
    }
    for (i = 0; i < paths->source_count; ++i) {
        size_t len = strlen(paths->sources[i].root);

        if (len > 0 && strncmp(final_root, paths->sources[i].root, len) == 0
            && (final_root[len] == '\0' || final_root[len] == '/')) {
            return paths->sources[i].root;
        }
    }

    return paths->sdcard_root;
}

static void cs_upload_cleanup_temp_files(cs_upload_request *state) {
    size_t i;

    if (!state) {
        return;
    }

    for (i = 0; i < state->plan_count; ++i) {
        if (state->plans[i].temp_path[0] != '\0') {
            (void) remove(state->plans[i].temp_path);
        }
    }
}

static void cs_upload_cleanup_preview_files(cs_upload_request *state) {
    size_t i;

    if (!state) {
        return;
    }
    for (i = 0; i < state->preview_file_count; ++i) {
        free(state->preview_files[i].relative_dir);
    }
    free(state->preview_files);
    state->preview_files = NULL;
    state->preview_file_count = 0;
    state->preview_file_capacity = 0;
}

static int cs_upload_append_preview_file(cs_upload_request *state,
                                         const char *client_path) {
    cs_upload_preview_file *grown;
    cs_upload_preview_file *entry;
    char relative_dir[CS_PATH_MAX];
    char filename[256];
    size_t next_capacity;

    if (!state || !client_path
        || state->preview_file_count >= CS_UPLOAD_PREVIEW_MANIFEST_MAX
        || cs_upload_split_client_path(client_path,
                                       relative_dir,
                                       sizeof(relative_dir),
                                       filename,
                                       sizeof(filename))
               != 0) {
        return -1;
    }
    if (state->preview_file_count == state->preview_file_capacity) {
        next_capacity = state->preview_file_capacity == 0 ? 64 : state->preview_file_capacity * 2;
        if (next_capacity > CS_UPLOAD_PREVIEW_MANIFEST_MAX) {
            next_capacity = CS_UPLOAD_PREVIEW_MANIFEST_MAX;
        }
        grown = (cs_upload_preview_file *) realloc(
            state->preview_files, next_capacity * sizeof(state->preview_files[0]));
        if (!grown) {
            return -1;
        }
        state->preview_files = grown;
        state->preview_file_capacity = next_capacity;
    }

    entry = &state->preview_files[state->preview_file_count];
    memset(entry, 0, sizeof(*entry));
    entry->relative_dir = strdup(relative_dir);
    if (!entry->relative_dir
        || snprintf(entry->filename, sizeof(entry->filename), "%s", filename)
               >= (int) sizeof(entry->filename)) {
        free(entry->relative_dir);
        entry->relative_dir = NULL;
        return -1;
    }
    state->preview_file_count += 1;
    return 0;
}

static int cs_prepare_upload_metadata(cs_upload_request *state) {
    cs_browser_scope scope;
    cs_platform_info resolved_platform = {0};
    const cs_platform_info *platform = NULL;

    if (!state || !state->app) {
        return -1;
    }
    if (state->metadata_ready) {
        return 0;
    }

    scope = cs_browser_scope_parse(state->scope);
    if (scope == CS_SCOPE_INVALID) {
        return -1;
    }
    if (cs_browser_scope_requires_platform(scope)) {
        if (cs_platform_resolve(&state->app->paths, state->tag, &resolved_platform) != 0) {
            return -1;
        }
        platform = &resolved_platform;
    }
    state->path_flags =
        cs_browser_scope_allows_hidden_for_platform(scope, platform) ? CS_PATH_FLAG_ALLOW_HIDDEN : 0;
    if (scope == CS_SCOPE_FILES && state->app->paths.source_count > 1) {
        char effective_path[CS_PATH_MAX];

        if (state->path[0] == '\0') {
            state->source_required = 1;
            return -1;
        }
        if (cs_paths_resolve_files_path(&state->app->paths,
                                        state->path,
                                        state->final_root,
                                        sizeof(state->final_root),
                                        effective_path,
                                        sizeof(effective_path),
                                        NULL)
                != 0
            || snprintf(state->path, sizeof(state->path), "%s", effective_path) >= (int) sizeof(state->path)) {
            return -1;
        }
        state->path_flags = CS_PATH_FLAG_ALLOW_HIDDEN;
    } else if (cs_browser_write_root_for_scope(&state->app->paths,
                                               scope,
                                               platform,
                                               state->final_root,
                                               sizeof(state->final_root))
               != 0) {
        return -1;
    }
    if (snprintf(state->final_guard_root,
                 sizeof(state->final_guard_root),
                 "%s",
                 cs_upload_guard_root_for_final(&state->app->paths, state->final_root))
        >= (int) sizeof(state->final_guard_root)) {
        return -1;
    }

    state->metadata_ready = 1;
    return 0;
}

/* Multipart parsing permits hidden directory components because the effective
 * policy depends on scope/platform metadata that may arrive later in the form.
 * Revalidate every destination directory once path_flags is known, before ROM
 * classification or destination planning can return a misleading error.
 */
static int cs_upload_request_paths_are_valid(const cs_upload_request *state) {
    size_t i;

    if (!state || !state->metadata_ready) {
        return 0;
    }
    for (i = 0; i < state->directory_count; ++i) {
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(state->path,
                                         state->directories[i],
                                         upload_dir,
                                         sizeof(upload_dir))
                != 0
            || upload_dir[0] == '\0'
            || cs_validate_relative_path_with_flags(upload_dir, state->path_flags)
                   != 0) {
            return 0;
        }
    }
    for (i = 0; i < state->plan_count; ++i) {
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(state->path,
                                         state->relative_dirs[i],
                                         upload_dir,
                                         sizeof(upload_dir))
                != 0
            || cs_validate_relative_path_with_flags(
                   upload_dir,
                   state->path_flags | CS_PATH_FLAG_ALLOW_EMPTY)
                   != 0) {
            return 0;
        }
    }
    for (i = 0; i < state->preview_file_count; ++i) {
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(state->path,
                                         state->preview_files[i].relative_dir,
                                         upload_dir,
                                         sizeof(upload_dir))
                != 0
            || cs_validate_relative_path_with_flags(
                   upload_dir,
                   state->path_flags | CS_PATH_FLAG_ALLOW_EMPTY)
                   != 0) {
            return 0;
        }
    }
    return 1;
}

static int cs_upload_boolean_field(const char *value, size_t valuelen) {
    return (valuelen == 1 && value[0] == '1') || (valuelen == 4 && strncmp(value, "true", 4) == 0);
}

static int cs_upload_field_found(const char *key,
                                 const char *filename,
                                 char *path,
                                 size_t pathlen,
                                 void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    cs_upload_plan *plan;
    int written;

    if (!state || !state->app || !path || pathlen == 0) {
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (state->failed) {
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (!key) {
        return MG_FORM_FIELD_STORAGE_SKIP;
    }
    if (strcmp(key, "file") != 0) {
        return MG_FORM_FIELD_STORAGE_GET;
    }
    if (state->plan_count >= CS_UPLOAD_MAX_FILES || !filename || filename[0] == '\0') {
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    plan = &state->plans[state->plan_count];
    if (cs_upload_split_client_path(filename,
                                    state->relative_dirs[state->plan_count],
                                    sizeof(state->relative_dirs[state->plan_count]),
                                    state->file_names[state->plan_count],
                                    sizeof(state->file_names[state->plan_count]))
            != 0
        || cs_upload_reserve_temp_path(&state->app->paths,
                                       state->file_names[state->plan_count],
                                       plan->temp_path,
                                       sizeof(plan->temp_path))
               != 0) {
        if (plan->temp_path[0] != '\0') {
            (void) remove(plan->temp_path);
            plan->temp_path[0] = '\0';
        }
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    written = snprintf(path, pathlen, "%s", plan->temp_path);
    if (written < 0 || (size_t) written >= pathlen) {
        (void) remove(plan->temp_path);
        plan->temp_path[0] = '\0';
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    state->plan_count += 1;
    return MG_FORM_FIELD_STORAGE_STORE;
}

static int cs_upload_field_get(const char *key, const char *value, size_t valuelen, void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    size_t copy_len;
    char *target = NULL;
    size_t target_size = 0;

    if (!state || !key || !value) {
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    if (strcmp(key, "scope") == 0) {
        target = state->scope;
        target_size = sizeof(state->scope);
    } else if (strcmp(key, "tag") == 0) {
        target = state->tag;
        target_size = sizeof(state->tag);
    } else if (strcmp(key, "path") == 0) {
        target = state->path;
        target_size = sizeof(state->path);
    } else if (strcmp(key, "overwrite") == 0) {
        state->overwrite_existing = cs_upload_boolean_field(value, valuelen);
        return MG_FORM_FIELD_HANDLE_NEXT;
    } else if (strcmp(key, "directory") == 0) {
        if (state->directory_count >= CS_UPLOAD_MAX_DIRECTORIES || valuelen == 0
            || valuelen >= sizeof(state->directories[state->directory_count])) {
            state->failed = 1;
            return MG_FORM_FIELD_HANDLE_ABORT;
        }
        memcpy(state->directories[state->directory_count], value, valuelen);
        state->directories[state->directory_count][valuelen] = '\0';
        state->directory_count += 1;
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    if (!target || target_size == 0) {
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    copy_len = valuelen < target_size - 1 ? valuelen : target_size - 1;
    memcpy(target, value, copy_len);
    target[copy_len] = '\0';
    return MG_FORM_FIELD_HANDLE_NEXT;
}

static int cs_upload_preview_field_found(const char *key,
                                         const char *filename,
                                         char *path,
                                         size_t pathlen,
                                         void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;

    (void) filename;
    (void) path;
    (void) pathlen;

    if (!state || state->failed) {
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (!key) {
        return MG_FORM_FIELD_STORAGE_SKIP;
    }
    if (strcmp(key, "file") == 0) {
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    return MG_FORM_FIELD_STORAGE_GET;
}

static int cs_upload_preview_field_get(const char *key, const char *value, size_t valuelen, void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    int common_status;

    common_status = cs_upload_field_get(key, value, valuelen, user_data);
    if (common_status != MG_FORM_FIELD_HANDLE_NEXT || !key || !value) {
        return common_status;
    }

    if (strcmp(key, "file_path") == 0) {
        char client_path[CS_PATH_MAX];

        if (valuelen == 0 || valuelen >= sizeof(client_path)) {
            state->failed = 1;
            return MG_FORM_FIELD_HANDLE_ABORT;
        }

        memcpy(client_path, value, valuelen);
        client_path[valuelen] = '\0';
        if (cs_upload_append_preview_file(state, client_path) != 0) {
            state->failed = 1;
            return MG_FORM_FIELD_HANDLE_ABORT;
        }
    }

    return state->failed ? MG_FORM_FIELD_HANDLE_ABORT : MG_FORM_FIELD_HANDLE_NEXT;
}

static int cs_upload_field_store(const char *path, long long file_size, void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    size_t i;

    if (!state || !path || file_size < 0) {
        return MG_FORM_FIELD_HANDLE_ABORT;
    }

    for (i = 0; i < state->plan_count; ++i) {
        if (strcmp(path, state->plans[i].temp_path) == 0) {
            state->file_sizes[i] = file_size;
            state->stored_count += 1;
            return MG_FORM_FIELD_HANDLE_NEXT;
        }
    }

    state->failed = 1;
    return MG_FORM_FIELD_HANDLE_ABORT;
}

static int cs_upload_preview_field_store(const char *path, long long file_size, void *user_data) {
    (void) path;
    (void) file_size;
    if (user_data) {
        ((cs_upload_request *) user_data)->failed = 1;
    }
    return MG_FORM_FIELD_HANDLE_ABORT;
}

static int cs_upload_preview_conflict_seen(const cs_upload_preview_result *result,
                                           const char *path,
                                           cs_upload_preview_kind kind) {
    size_t i;

    if (!result || !path) {
        return 0;
    }

    for (i = 0; i < result->seen_count; ++i) {
        if (result->seen_kinds[i] == kind && strcmp(result->seen_paths[i], path) == 0) {
            return 1;
        }
    }

    return 0;
}

static void cs_upload_preview_record(cs_upload_preview_result *result,
                                     const char *path,
                                     cs_upload_preview_kind kind,
                                     int blocking) {
    cs_upload_preview_conflict *preview_list;
    size_t *preview_count;
    size_t *total_count;

    if (!result || !path || path[0] == '\0' || cs_upload_preview_conflict_seen(result, path, kind)) {
        return;
    }

    if (result->seen_count < CS_UPLOAD_PREVIEW_TRACK_MAX) {
        (void) snprintf(result->seen_paths[result->seen_count],
                        sizeof(result->seen_paths[result->seen_count]),
                        "%s",
                        path);
        result->seen_kinds[result->seen_count] = kind;
        result->seen_count += 1;
    }

    preview_list = blocking ? result->blocking : result->overwriteable;
    preview_count = blocking ? &result->blocking_preview_count : &result->overwriteable_preview_count;
    total_count = blocking ? &result->blocking_count : &result->overwriteable_count;

    *total_count += 1;
    if (*preview_count >= CS_UPLOAD_PREVIEW_RESULT_MAX) {
        return;
    }

    (void) snprintf(preview_list[*preview_count].path, sizeof(preview_list[*preview_count].path), "%s", path);
    preview_list[*preview_count].kind = kind;
    *preview_count += 1;
}

static void cs_upload_preview_result_free(cs_upload_preview_result *result) {
    size_t i;

    if (!result) {
        return;
    }
    for (i = 0; i < result->bundle_entrypoint_count; ++i) {
        free(result->bundle_entrypoints[i]);
    }
    free(result->bundle_entrypoints);
    free(result);
}

static void cs_upload_preview_record_unsupported(cs_upload_preview_result *result,
                                                 const char *path,
                                                 cs_rom_entry_status status) {
    if (!result || !path) {
        return;
    }
    result->unsupported_count += 1;
    if (result->unsupported_preview_count >= CS_UPLOAD_ROM_REPORT_MAX) {
        return;
    }
    (void) snprintf(result->unsupported[result->unsupported_preview_count],
                    sizeof(result->unsupported[result->unsupported_preview_count]),
                    "%s",
                    path);
    (void) snprintf(result->unsupported_reasons[result->unsupported_preview_count],
                    sizeof(result->unsupported_reasons[result->unsupported_preview_count]),
                    "%s",
                    cs_rom_entry_status_name(status));
    result->unsupported_preview_count += 1;
}

static int cs_upload_preview_add_bundle_entrypoint(cs_upload_preview_result *result,
                                                   const char *path) {
    char **grown;
    size_t next_capacity;

    if (!result || !path || !path[0]) {
        return -1;
    }
    if (result->bundle_entrypoint_count == result->bundle_entrypoint_capacity) {
        next_capacity = result->bundle_entrypoint_capacity == 0
            ? 16
            : result->bundle_entrypoint_capacity * 2;
        grown = (char **) realloc(result->bundle_entrypoints,
                                  next_capacity * sizeof(result->bundle_entrypoints[0]));
        if (!grown) {
            return -1;
        }
        result->bundle_entrypoints = grown;
        result->bundle_entrypoint_capacity = next_capacity;
    }
    result->bundle_entrypoints[result->bundle_entrypoint_count] = strdup(path);
    if (!result->bundle_entrypoints[result->bundle_entrypoint_count]) {
        return -1;
    }
    result->bundle_entrypoint_count += 1;
    return 0;
}

static size_t cs_upload_top_folder_length(const char *relative_dir) {
    const char *slash;

    if (!relative_dir) {
        return 0;
    }
    slash = strchr(relative_dir, '/');
    return slash ? (size_t) (slash - relative_dir) : strlen(relative_dir);
}

static int cs_upload_preview_bundle_member_compare(const void *left_value,
                                                   const void *right_value) {
    const cs_upload_preview_bundle_member *left =
        (const cs_upload_preview_bundle_member *) left_value;
    const cs_upload_preview_bundle_member *right =
        (const cs_upload_preview_bundle_member *) right_value;
    const char *left_dir = left->file->relative_dir;
    const char *right_dir = right->file->relative_dir;
    size_t left_length = cs_upload_top_folder_length(left_dir);
    size_t right_length = cs_upload_top_folder_length(right_dir);
    size_t shared_length = left_length < right_length ? left_length : right_length;
    int compared = memcmp(left_dir, right_dir, shared_length);

    if (compared != 0) {
        return compared;
    }
    return left_length < right_length ? -1 : left_length > right_length ? 1 : 0;
}

static int cs_upload_preview_apply_rom_policy(const cs_upload_request *state,
                                              cs_upload_preview_result *result) {
    cs_rom_upload_policy policy;
    cs_upload_preview_bundle_member *members = NULL;
    size_t member_count = 0;
    size_t i;

    if (!state || !result || cs_browser_scope_parse(state->scope) != CS_SCOPE_ROMS) {
        return 0;
    }
    if (cs_platform_resolve_rom_upload_policy(&state->app->paths, state->tag, &policy) != 0
        || !policy.enforced) {
        cs_rom_upload_policy_free(&policy);
        return 0;
    }

    for (i = 0; i < state->preview_file_count; ++i) {
        const cs_upload_preview_file *file = &state->preview_files[i];
        cs_rom_entry_status status;

        if (file->relative_dir[0] != '\0') {
            continue;
        }
        status = cs_rom_upload_policy_classify(&policy, file->filename);
        if (status == CS_ROM_ENTRY_ACCEPTED) {
            result->entrypoint_count += 1;
        } else {
            cs_upload_preview_record_unsupported(result, file->filename, status);
        }
    }

    for (i = 0; i < state->preview_file_count; ++i) {
        const cs_upload_preview_file *file = &state->preview_files[i];

        if (file->relative_dir[0] != '\0') {
            member_count += 1;
        }
    }
    if (member_count > 0) {
        size_t member_index = 0;

        members = (cs_upload_preview_bundle_member *) calloc(
            member_count, sizeof(members[0]));
        if (!members) {
            cs_rom_upload_policy_free(&policy);
            return -1;
        }
        for (i = 0; i < state->preview_file_count; ++i) {
            const cs_upload_preview_file *file = &state->preview_files[i];

            if (file->relative_dir[0] == '\0') {
                continue;
            }
            members[member_index].file = file;
            members[member_index].status =
                cs_rom_upload_policy_classify(&policy, file->filename);
            member_index += 1;
        }
        qsort(members,
              member_count,
              sizeof(members[0]),
              cs_upload_preview_bundle_member_compare);
    }

    for (i = 0; i < member_count;) {
        size_t group_end = i + 1;
        size_t j;
        size_t top_length =
            cs_upload_top_folder_length(members[i].file->relative_dir);
        int has_entrypoint = 0;
        char first_entrypoint[CS_PATH_MAX] = {0};

        while (group_end < member_count
               && cs_upload_preview_bundle_member_compare(
                      &members[i], &members[group_end])
                      == 0) {
            group_end += 1;
        }
        for (j = i; j < group_end; ++j) {
            const cs_upload_preview_bundle_member *member = &members[j];

            if (member->status == CS_ROM_ENTRY_ACCEPTED) {
                result->entrypoint_count += 1;
                if (!has_entrypoint
                    && cs_upload_join_relative_path(member->file->relative_dir,
                                                    member->file->filename,
                                                    first_entrypoint,
                                                    sizeof(first_entrypoint))
                           != 0) {
                    free(members);
                    cs_rom_upload_policy_free(&policy);
                    return -1;
                }
                has_entrypoint = 1;
            } else {
                result->companion_count += 1;
            }
        }
        if (has_entrypoint) {
            if (cs_upload_preview_add_bundle_entrypoint(result, first_entrypoint) != 0) {
                free(members);
                cs_rom_upload_policy_free(&policy);
                return -1;
            }
        } else {
            char bundle_path[CS_PATH_MAX];

            if (snprintf(bundle_path,
                         sizeof(bundle_path),
                         "%.*s/",
                         (int) top_length,
                         members[i].file->relative_dir)
                >= (int) sizeof(bundle_path)) {
                free(members);
                cs_rom_upload_policy_free(&policy);
                return -1;
            }
            cs_upload_preview_record_unsupported(
                result, bundle_path, CS_ROM_ENTRY_UNSUPPORTED);
        }
        i = group_end;
    }

    free(members);
    cs_rom_upload_policy_free(&policy);
    return 0;
}

static int cs_upload_preview_scan_required_directories(const cs_upload_request *state,
                                                       const char *relative_path,
                                                       cs_upload_preview_result *result) {
    const char *cursor;
    char current[CS_PATH_MAX] = {0};

    if (!state || !relative_path || !result || relative_path[0] == '\0') {
        return 0;
    }

    cursor = relative_path;
    while (*cursor != '\0') {
        const char *slash = strchr(cursor, '/');
        size_t component_len = slash ? (size_t) (slash - cursor) : strlen(cursor);
        char component[CS_PATH_MAX];
        char absolute_path[CS_PATH_MAX];
        char next_path[CS_PATH_MAX];
        struct stat st;

        if (component_len == 0 || component_len >= sizeof(component)) {
            return -1;
        }

        memcpy(component, cursor, component_len);
        component[component_len] = '\0';
        /* next_path is built component-by-component inside this loop, so it is never empty and
         * does not need the ALLOW_EMPTY flag that the file-specific resolver uses.
         */
        if (cs_upload_join_relative_path(current, component, next_path, sizeof(next_path)) != 0
            || cs_resolve_path_under_root_with_flags(state->final_root,
                                                     next_path,
                                                     state->path_flags,
                                                     absolute_path,
                                                     sizeof(absolute_path))
                   != 0) {
            return -1;
        }

        if (lstat(absolute_path, &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                cs_upload_preview_record(result, next_path, CS_UPLOAD_PREVIEW_DIRECTORY_OVER_FILE, 1);
                return 1;
            }
        } else if (errno != ENOENT) {
            return -1;
        }

        if (snprintf(current, sizeof(current), "%s", next_path) >= (int) sizeof(current)) {
            return -1;
        }
        if (!slash) {
            break;
        }
        cursor = slash + 1;
    }

    return 0;
}

static int cs_upload_preview_scan_directory(const cs_upload_request *state,
                                            const char *directory,
                                            cs_upload_preview_result *result) {
    char upload_dir[CS_PATH_MAX];
    int status;

    if (!state || !directory || !result) {
        return -1;
    }
    if (cs_upload_join_relative_path(state->path, directory, upload_dir, sizeof(upload_dir)) != 0 || upload_dir[0] == '\0') {
        return -1;
    }

    status = cs_upload_preview_scan_required_directories(state, upload_dir, result);
    return status < 0 ? -1 : 0;
}

static int cs_upload_preview_scan_file(const cs_upload_request *state,
                                       const char *relative_dir,
                                       const char *filename,
                                       cs_upload_preview_result *result) {
    char upload_dir[CS_PATH_MAX];
    char resolved_dir[CS_PATH_MAX];
    char upload_path[CS_PATH_MAX];
    char final_path[CS_PATH_MAX];
    struct stat st;
    int directory_status;

    if (!state || !filename || !result) {
        return -1;
    }
    if (cs_upload_join_relative_path(state->path, relative_dir ? relative_dir : "", upload_dir, sizeof(upload_dir)) != 0) {
        return -1;
    }

    directory_status = cs_upload_preview_scan_required_directories(state, upload_dir, result);
    if (directory_status != 0) {
        return directory_status < 0 ? -1 : 0;
    }
    if (cs_resolve_path_under_root_with_flags(state->final_root,
                                              upload_dir,
                                              state->path_flags | CS_PATH_FLAG_ALLOW_EMPTY,
                                              resolved_dir,
                                              sizeof(resolved_dir))
        != 0
        || cs_upload_join_relative_path(upload_dir, filename, upload_path, sizeof(upload_path)) != 0
        || snprintf(final_path, sizeof(final_path), "%s/%s", resolved_dir, filename) >= (int) sizeof(final_path)) {
        return -1;
    }

    if (lstat(final_path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (S_ISREG(st.st_mode)) {
        cs_upload_preview_record(result, upload_path, CS_UPLOAD_PREVIEW_OVERWRITE, 0);
        return 0;
    }

    cs_upload_preview_record(result, upload_path, CS_UPLOAD_PREVIEW_FILE_OVER_DIRECTORY, 1);
    return 0;
}

static const char *cs_upload_preview_kind_name(cs_upload_preview_kind kind) {
    switch (kind) {
        case CS_UPLOAD_PREVIEW_OVERWRITE:
            return "overwrite";
        case CS_UPLOAD_PREVIEW_FILE_OVER_DIRECTORY:
            return "file-over-directory";
        case CS_UPLOAD_PREVIEW_DIRECTORY_OVER_FILE:
            return "directory-over-file";
        default:
            return "overwrite";
    }
}

static int cs_upload_preview_write_list(struct mg_connection *conn,
                                        const cs_upload_preview_conflict *items,
                                        size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            return -1;
        }
        if (cs_routes_stream_literal(conn, "{\"path\":\"") != 0
            || cs_routes_stream_escaped_string(conn, items[i].path) != 0
            || cs_routes_stream_literal(conn, "\",\"kind\":\"") != 0
            || cs_routes_stream_literal(conn, cs_upload_preview_kind_name(items[i].kind)) != 0
            || cs_routes_stream_literal(conn, "\"}") != 0) {
            return -1;
        }
    }

    return 0;
}

static int cs_upload_preview_write_unsupported(struct mg_connection *conn,
                                               const cs_upload_preview_result *result) {
    size_t i;

    for (i = 0; result && i < result->unsupported_preview_count; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            return -1;
        }
        if (cs_routes_stream_literal(conn, "{\"path\":\"") != 0
            || cs_routes_stream_escaped_string(conn, result->unsupported[i]) != 0
            || cs_routes_stream_literal(conn, "\",\"reason\":\"") != 0
            || cs_routes_stream_escaped_string(conn, result->unsupported_reasons[i]) != 0
            || cs_routes_stream_literal(conn, "\"}") != 0) {
            return -1;
        }
    }
    return 0;
}

static int cs_upload_preview_write_bundle_entrypoints(
    struct mg_connection *conn,
    const cs_upload_preview_result *result) {
    size_t i;

    for (i = 0; result && i < result->bundle_entrypoint_count; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            return -1;
        }
        if (cs_routes_stream_literal(conn, "\"") != 0
            || cs_routes_stream_escaped_string(conn, result->bundle_entrypoints[i]) != 0
            || cs_routes_stream_literal(conn, "\"") != 0) {
            return -1;
        }
    }
    return 0;
}

static int cs_upload_preview_write_response(struct mg_connection *conn, const cs_upload_preview_result *result) {
    if (!conn || !result) {
        return 1;
    }
    if (cs_routes_stream_begin_json_response(conn) != 0) {
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"ok\":true,\"overwriteableCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->overwriteable_count) != 0
        || cs_routes_stream_literal(conn, ",\"blockingCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->blocking_count) != 0
        || cs_routes_stream_literal(conn, ",\"overwriteable\":[") != 0
        || cs_upload_preview_write_list(conn, result->overwriteable, result->overwriteable_preview_count) != 0
        || cs_routes_stream_literal(conn, "],\"blocking\":[") != 0
        || cs_upload_preview_write_list(conn, result->blocking, result->blocking_preview_count) != 0
        || cs_routes_stream_literal(conn, "],\"unsupportedCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->unsupported_count) != 0
        || cs_routes_stream_literal(conn, ",\"unsupported\":[") != 0
        || cs_upload_preview_write_unsupported(conn, result) != 0
        || cs_routes_stream_literal(conn, "],\"entrypointCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->entrypoint_count) != 0
        || cs_routes_stream_literal(conn, ",\"companionCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->companion_count) != 0
        || cs_routes_stream_literal(conn, ",\"bundleEntrypoints\":[") != 0
        || cs_upload_preview_write_bundle_entrypoints(conn, result) != 0
        || cs_routes_stream_literal(conn, "]}") != 0
        || mg_send_chunk(conn, "", 0) < 0) {
        (void) mg_send_chunk(conn, "", 0);
    }

    return 1;
}

int cs_route_upload_preview_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_upload_request request_state;
    cs_upload_preview_result *preview_result = NULL;
    struct mg_form_data_handler form_handler;
    const struct mg_request_info *request = mg_get_request_info(conn);
    const char *cookie = mg_get_header(conn, "Cookie");
    int handled_fields;
    int response_status;
    size_t i;

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }
    if (!request || request->content_length < 0 || request->content_length > cs_upload_request_limit_bytes()) {
        return cs_write_json(conn, 413, "Payload Too Large", "{\"error\":\"upload_too_large\"}");
    }
    preview_result = (cs_upload_preview_result *) calloc(1, sizeof(*preview_result));
    if (!preview_result) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }

    memset(&request_state, 0, sizeof(request_state));
    request_state.app = app;

    memset(&form_handler, 0, sizeof(form_handler));
    form_handler.field_found = cs_upload_preview_field_found;
    form_handler.field_get = cs_upload_preview_field_get;
    form_handler.field_store = cs_upload_preview_field_store;
    form_handler.user_data = &request_state;

    handled_fields = mg_handle_form_request(conn, &form_handler);
    if (handled_fields < 0 || request_state.failed
        || (request_state.preview_file_count == 0 && request_state.directory_count == 0)) {
        const char *error_code = handled_fields < 0 ? "upload_preview_parse_failed"
                                 : request_state.failed
                                     ? "upload_preview_invalid_form"
                                     : "upload_preview_empty";
        char detail[192];

        cs_upload_write_form_failure_detail(detail, sizeof(detail), handled_fields, &request_state);
        response_status = cs_write_upload_bad_request(conn, "upload preview", error_code, detail);
        cs_upload_cleanup_preview_files(&request_state);
        cs_upload_preview_result_free(preview_result);
        return response_status;
    }
    if (cs_prepare_upload_metadata(&request_state) != 0) {
        response_status = cs_write_upload_bad_request(conn,
                                                      "upload preview",
                                                      request_state.source_required
                                                          ? "upload_source_required"
                                                          : "upload_preview_metadata_failed",
                                                      request_state.source_required
                                                          ? "files scope requires a source path"
                                                          : "target metadata could not be resolved");
        cs_upload_cleanup_preview_files(&request_state);
        cs_upload_preview_result_free(preview_result);
        return response_status;
    }
    if (!cs_upload_request_paths_are_valid(&request_state)) {
        response_status = cs_write_upload_bad_request(conn,
                                                      "upload preview",
                                                      "upload_path_invalid",
                                                      "destination path violates scope policy");
        cs_upload_cleanup_preview_files(&request_state);
        cs_upload_preview_result_free(preview_result);
        return response_status;
    }

    if (cs_upload_preview_apply_rom_policy(&request_state, preview_result) != 0) {
        response_status = cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
        cs_upload_cleanup_preview_files(&request_state);
        cs_upload_preview_result_free(preview_result);
        return response_status;
    }

    for (i = 0; i < request_state.directory_count; ++i) {
        if (cs_upload_preview_scan_directory(&request_state, request_state.directories[i], preview_result) != 0) {
            response_status = cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
            cs_upload_cleanup_preview_files(&request_state);
            cs_upload_preview_result_free(preview_result);
            return response_status;
        }
    }
    for (i = 0; i < request_state.preview_file_count; ++i) {
        if (cs_upload_preview_scan_file(&request_state,
                                        request_state.preview_files[i].relative_dir,
                                        request_state.preview_files[i].filename,
                                        preview_result)
            != 0) {
            response_status = cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
            cs_upload_cleanup_preview_files(&request_state);
            cs_upload_preview_result_free(preview_result);
            return response_status;
        }
    }

    response_status = cs_upload_preview_write_response(conn, preview_result);
    cs_upload_cleanup_preview_files(&request_state);
    cs_upload_preview_result_free(preview_result);
    return response_status;
}

/* First path component of a relative dir ("Foo/Bar" -> "Foo"). */
static void cs_upload_top_folder(const char *rel, char *out, size_t out_size) {
    const char *slash;
    size_t n;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!rel) {
        return;
    }
    slash = strchr(rel, '/');
    n = slash ? (size_t) (slash - rel) : strlen(rel);
    if (n >= out_size) {
        n = out_size - 1;
    }
    memcpy(out, rel, n);
    out[n] = '\0';
}

static int cs_upload_existing_bundle_has_entrypoint_recursive(
    const char *path,
    const cs_rom_upload_policy *policy,
    unsigned int depth) {
    DIR *dir;
    struct dirent *entry;
    int found = 0;

    if (!path || !policy || depth > 32) {
        return 0;
    }
    dir = opendir(path);
    if (!dir) {
        return 0;
    }
    while (!found && (entry = readdir(dir)) != NULL) {
        char child[CS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0
            || snprintf(child, sizeof(child), "%s/%s", path, entry->d_name)
                   >= (int) sizeof(child)
            || lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) {
            continue;
        }
        if (S_ISREG(st.st_mode)
            && cs_rom_upload_policy_classify(policy, entry->d_name)
                   == CS_ROM_ENTRY_ACCEPTED) {
            found = 1;
        } else if (S_ISDIR(st.st_mode)) {
            found = cs_upload_existing_bundle_has_entrypoint_recursive(
                child, policy, depth + 1);
        }
    }
    closedir(dir);
    return found;
}

static int cs_upload_existing_bundle_has_entrypoint(
    const cs_upload_request *state,
    const char *top,
    const cs_rom_upload_policy *policy) {
    char bundle_relative[CS_PATH_MAX];
    char bundle_absolute[CS_PATH_MAX];

    if (!state || !top || !policy
        || cs_upload_join_relative_path(state->path,
                                        top,
                                        bundle_relative,
                                        sizeof(bundle_relative))
               != 0
        || cs_resolve_path_under_root_with_flags(state->final_root,
                                                 bundle_relative,
                                                 state->path_flags,
                                                 bundle_absolute,
                                                 sizeof(bundle_absolute))
               != 0) {
        return 0;
    }
    return cs_upload_existing_bundle_has_entrypoint_recursive(
        bundle_absolute, policy, 0);
}

static void cs_rom_formats_append(cJSON *arr, const cs_catalog_string_list *list) {
    size_t i;
    for (i = 0; list && i < list->count; ++i) {
        cJSON *value;
        int duplicate = 0;
        cJSON_ArrayForEach(value, arr) {
            if (value->valuestring && strcmp(value->valuestring, list->items[i]) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(list->items[i]));
        }
    }
}

/* Reject a ROM-scope upload whose selection cannot become a discoverable game.
   Flat (Upload File) files are strict per-file; files under a top-level folder
   (Upload Folder/ZIP) form a bundle that must contain at least one accepted
   entrypoint, the rest allowed as companion data. Only scope=roms is validated;
   custom/empty/unresolvable policies fail open. On rejection writes a 415 with a
   bounded unsupported list + the effective accepted formats, and returns 1
   (with the response status in *out_status). Returns 0 when acceptable. */
static int cs_upload_rom_scope_rejected(cs_upload_request *state,
                                        struct mg_connection *conn,
                                        int *out_status) {
    cs_rom_upload_policy policy;
    char unsupported[CS_UPLOAD_ROM_REPORT_MAX][CS_PATH_MAX];
    size_t unsupported_count = 0;
    size_t unsupported_total = 0;
    size_t i, j;
    cJSON *root;
    cJSON *list_json;
    cJSON *accepted_json;
    cJSON *accepted_file_names_json;
    char *body;

    if (!state || !state->app) {
        return 0;
    }
    if (cs_browser_scope_parse(state->scope) != CS_SCOPE_ROMS) {
        return 0;
    }
    if (cs_platform_resolve_rom_upload_policy(&state->app->paths, state->tag, &policy) != 0
        || !policy.enforced) {
        cs_rom_upload_policy_free(&policy);
        return 0; /* fail open: unknown/custom/empty policy */
    }

    for (i = 0; i < state->plan_count; ++i) {
        if (state->relative_dirs[i][0] != '\0') {
            continue; /* flat only; bundles handled below */
        }
        if (cs_rom_upload_policy_classify(&policy, state->file_names[i]) != CS_ROM_ENTRY_ACCEPTED) {
            unsupported_total += 1;
            if (unsupported_count < CS_UPLOAD_ROM_REPORT_MAX) {
                snprintf(unsupported[unsupported_count++], CS_PATH_MAX, "%s", state->file_names[i]);
            }
        }
    }

    for (i = 0; i < state->plan_count; ++i) {
        char top[CS_PATH_MAX];
        int already_seen = 0;
        int has_entrypoint = 0;

        if (state->relative_dirs[i][0] == '\0') {
            continue;
        }
        cs_upload_top_folder(state->relative_dirs[i], top, sizeof(top));
        for (j = 0; j < i; ++j) {
            char prev[CS_PATH_MAX];
            if (state->relative_dirs[j][0] == '\0') {
                continue;
            }
            cs_upload_top_folder(state->relative_dirs[j], prev, sizeof(prev));
            if (strcmp(prev, top) == 0) {
                already_seen = 1;
                break;
            }
        }
        if (already_seen) {
            continue;
        }
        for (j = 0; j < state->plan_count; ++j) {
            char cur[CS_PATH_MAX];
            if (state->relative_dirs[j][0] == '\0') {
                continue;
            }
            cs_upload_top_folder(state->relative_dirs[j], cur, sizeof(cur));
            if (strcmp(cur, top) == 0
                && cs_rom_upload_policy_classify(&policy, state->file_names[j]) == CS_ROM_ENTRY_ACCEPTED) {
                has_entrypoint = 1;
                break;
            }
        }
        if (!has_entrypoint) {
            has_entrypoint = cs_upload_existing_bundle_has_entrypoint(
                state, top, &policy);
        }
        if (!has_entrypoint) {
            unsupported_total += 1;
            if (unsupported_count < CS_UPLOAD_ROM_REPORT_MAX) {
                snprintf(unsupported[unsupported_count++], CS_PATH_MAX, "%s/", top);
            }
        }
    }

    if (unsupported_total == 0) {
        cs_rom_upload_policy_free(&policy);
        return 0;
    }

    root = cJSON_CreateObject();
    list_json = cJSON_CreateArray();
    accepted_json = cJSON_CreateArray();
    accepted_file_names_json = cJSON_CreateArray();
    for (i = 0; i < unsupported_count; ++i) {
        cJSON_AddItemToArray(list_json, cJSON_CreateString(unsupported[i]));
    }
    cs_rom_formats_append(accepted_json, &policy.extensions);
    cs_rom_formats_append(accepted_json, &policy.playlist_extensions);
    cs_rom_formats_append(accepted_json, &policy.archive_extensions);
    cs_rom_formats_append(accepted_file_names_json, &policy.file_names);
    cJSON_AddStringToObject(root, "error", "unsupported_rom_format");
    cJSON_AddItemToObject(root, "unsupported", list_json);
    cJSON_AddNumberToObject(root, "unsupportedCount", (double) unsupported_total);
    cJSON_AddItemToObject(root, "accepted", accepted_json);
    cJSON_AddItemToObject(root, "acceptedFileNames", accepted_file_names_json);
    body = cJSON_PrintUnformatted(root);
    *out_status = cs_write_json(conn, 415, "Unsupported Media Type",
                                body ? body : "{\"error\":\"unsupported_rom_format\"}");
    cJSON_free(body);
    cJSON_Delete(root);
    cs_rom_upload_policy_free(&policy);
    return 1;
}

int cs_route_upload_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_upload_request request_state;
    struct mg_form_data_handler form_handler;
    const struct mg_request_info *request = mg_get_request_info(conn);
    const char *cookie = mg_get_header(conn, "Cookie");
    int handled_fields;
    long long request_limit = cs_upload_request_limit_bytes();
    size_t i;

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }
    if (!request || request->content_length < 0 || request->content_length > request_limit) {
        return cs_write_json(conn, 413, "Payload Too Large", "{\"error\":\"upload_too_large\"}");
    }
    if (cs_upload_prepare_temp_root(&app->paths) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"upload_prep_failed\"}");
    }

    memset(&request_state, 0, sizeof(request_state));
    request_state.app = app;

    memset(&form_handler, 0, sizeof(form_handler));
    form_handler.field_found = cs_upload_field_found;
    form_handler.field_get = cs_upload_field_get;
    form_handler.field_store = cs_upload_field_store;
    form_handler.user_data = &request_state;

    handled_fields = mg_handle_form_request(conn, &form_handler);
    if (handled_fields < 0 || request_state.failed
        || (request_state.plan_count == 0 && request_state.directory_count == 0)
        || request_state.stored_count != request_state.plan_count) {
        const char *error_code = handled_fields < 0 ? "upload_parse_failed"
                                 : request_state.failed
                                     ? "upload_invalid_form"
                                     : (request_state.plan_count == 0 && request_state.directory_count == 0)
                                         ? "upload_empty"
                                         : "upload_incomplete";
        char detail[192];

        cs_upload_write_form_failure_detail(detail, sizeof(detail), handled_fields, &request_state);
        cs_upload_cleanup_temp_files(&request_state);
        return cs_write_upload_bad_request(conn, "upload", error_code, detail);
    }
    if (cs_prepare_upload_metadata(&request_state) != 0) {
        cs_upload_cleanup_temp_files(&request_state);
        return cs_write_upload_bad_request(conn,
                                           "upload",
                                           request_state.source_required ? "upload_source_required"
                                                                         : "upload_metadata_failed",
                                           request_state.source_required
                                               ? "files scope requires a source path"
                                               : "target metadata could not be resolved");
    }
    if (!cs_upload_request_paths_are_valid(&request_state)) {
        cs_upload_cleanup_temp_files(&request_state);
        return cs_write_upload_bad_request(conn,
                                           "upload",
                                           "upload_path_invalid",
                                           "destination path violates scope policy");
    }
    for (i = 0; i < request_state.plan_count; ++i) {
        if (request_state.file_sizes[i] > cs_upload_file_limit_bytes(&request_state)) {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_json(conn, 413, "Payload Too Large", "{\"error\":\"upload_too_large\"}");
        }
    }
    {
        /* Catalog-aware ROM format gate: reject a selection the launcher would
           never index, before promoting any temp file to the destination. */
        int reject_status = 0;
        if (cs_upload_rom_scope_rejected(&request_state, conn, &reject_status)) {
            cs_upload_cleanup_temp_files(&request_state);
            return reject_status;
        }
    }

    for (i = 0; i < request_state.directory_count; ++i) {
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(request_state.path,
                                         request_state.directories[i],
                                         upload_dir,
                                         sizeof(upload_dir))
                != 0
            || upload_dir[0] == '\0') {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_upload_bad_request(conn, "upload", "upload_path_invalid", "directory path is invalid");
        }
        if (cs_upload_prepare_final_directory(request_state.final_root,
                                              request_state.final_guard_root,
                                              upload_dir,
                                              request_state.path_flags)
            != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            if (errno == EEXIST || errno == EISDIR || errno == ENOTDIR || errno == EINVAL || errno == ENOTEMPTY) {
                return cs_write_upload_errno_response(conn, upload_dir);
            }
            return cs_write_upload_bad_request(conn,
                                               "upload",
                                               "upload_directory_prepare_failed",
                                               "directory could not be prepared");
        }
    }

    for (i = 0; i < request_state.plan_count; ++i) {
        cs_upload_plan promoted_plan;
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(request_state.path,
                                         request_state.relative_dirs[i],
                                         upload_dir,
                                         sizeof(upload_dir))
            != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_upload_bad_request(conn, "upload", "upload_path_invalid", "file path is invalid");
        }

        if (cs_upload_plan_make(&request_state.app->paths,
                                request_state.final_root,
                                request_state.final_guard_root,
                                upload_dir,
                                request_state.file_names[i],
                                request_state.path_flags,
                                &promoted_plan)
            != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            if (errno == EEXIST || errno == EISDIR || errno == ENOTDIR || errno == EINVAL || errno == ENOTEMPTY) {
                char upload_path[CS_PATH_MAX];

                if (cs_upload_join_relative_path(upload_dir,
                                                 request_state.file_names[i],
                                                 upload_path,
                                                 sizeof(upload_path))
                    == 0) {
                    return cs_write_upload_errno_response(conn, upload_path);
                }
            }
            return cs_write_upload_bad_request(conn, "upload", "upload_plan_failed", "file target could not be planned");
        }

        {
            int written = snprintf(promoted_plan.temp_path,
                                   sizeof(promoted_plan.temp_path),
                                   "%s",
                                   request_state.plans[i].temp_path);

            if (written < 0 || (size_t) written >= sizeof(promoted_plan.temp_path)) {
                cs_upload_cleanup_temp_files(&request_state);
                return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
            }
        }
        request_state.plans[i] = promoted_plan;
    }

    for (i = 0; i < request_state.plan_count; ++i) {
        char upload_path[CS_PATH_MAX];
        char combined_upload_path[CS_PATH_MAX];
        int promote_status;

        if (cs_upload_join_relative_path(request_state.relative_dirs[i],
                                         request_state.file_names[i],
                                         upload_path,
                                         sizeof(upload_path))
            != 0
            || cs_upload_join_relative_path(request_state.path,
                                            upload_path,
                                            combined_upload_path,
                                            sizeof(combined_upload_path))
                   != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
        }

        promote_status = request_state.overwrite_existing ? cs_upload_promote_replace(&request_state.plans[i])
                                                          : cs_upload_promote(&request_state.plans[i]);
        if (promote_status != 0) {
            size_t j;

            for (j = i; j < request_state.plan_count; ++j) {
                (void) remove(request_state.plans[j].temp_path);
            }
            return cs_write_upload_errno_response(conn, combined_upload_path);
        }
    }

    return cs_write_json(conn, 200, "OK", "{\"ok\":true}");
}
