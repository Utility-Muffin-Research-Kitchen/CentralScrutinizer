#include "cs_jawaka_ipc.h"
#include "cs_paths.h"

#include "cJSON.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pwd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define CS_JAWAKA_IPC_MAX_FRAME (16u * 1024u * 1024u)
#define CS_JAWAKA_SCAN_POLL_INTERVAL_US (100u * 1000u)
#define CS_JAWAKA_SCAN_POLL_ATTEMPTS 600u
#define CS_JAWAKA_SCAN_POLL_MAX_FAILURES 5u

typedef struct cs_jawaka_library_status {
    int scan_running;
    int pending_rescan;
    char scan_error[160];
} cs_jawaka_library_status;

static void cs_ipc_copy_status(char *status, size_t status_size, const char *message) {
    if (!status || status_size == 0) {
        return;
    }
    snprintf(status, status_size, "%s", message ? message : "");
}

static const char *cs_ipc_env_first(const char *first_name, const char *second_name) {
    const char *value = NULL;

    if (first_name) {
        value = getenv(first_name);
        if (value && value[0] != '\0') {
            return value;
        }
    }
    if (second_name) {
        value = getenv(second_name);
        if (value && value[0] != '\0') {
            return value;
        }
    }
    return NULL;
}

static const char *cs_ipc_tmpdir(void) {
    const char *tmpdir = getenv("TMPDIR");

    return tmpdir && tmpdir[0] ? tmpdir : "/tmp";
}

#ifdef PLATFORM_MAC
static const char *cs_ipc_username(void) {
    const char *user = getenv("USER");

    if (user && user[0] != '\0') {
        return user;
    }

    struct passwd *pwd = getpwuid(getuid());
    if (pwd && pwd->pw_name && pwd->pw_name[0] != '\0') {
        return pwd->pw_name;
    }

    return "anon";
}
#endif

static int cs_jawaka_socket_path(char *buffer, size_t buffer_size) {
    const char *override = cs_ipc_env_first("JAWAKA_SOCKET_PATH", "JAWAKAD_SOCKET_PATH");
    const char *runtime = cs_ipc_env_first("UMRK_RUNTIME_PATH", "JAWAKA_RUNTIME_DIR");
    int written;

    if (!buffer || buffer_size == 0) {
        return -1;
    }

    if (override && override[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s", override);
    } else if (runtime && runtime[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s/jawakad.sock", runtime);
    } else {
#ifdef PLATFORM_MAC
        written = snprintf(buffer, buffer_size, "%s/jawaka-%s/jawakad.sock", cs_ipc_tmpdir(), cs_ipc_username());
#else
        written = snprintf(buffer, buffer_size, "%s/jawaka-runtime/jawakad.sock", cs_ipc_tmpdir());
#endif
    }

    return (written < 0 || (size_t) written >= buffer_size) ? -1 : 0;
}

static int cs_ipc_write_all(int fd, const void *buffer, size_t len) {
    const char *cursor = (const char *) buffer;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        cursor += (size_t) written;
        remaining -= (size_t) written;
    }

    return 0;
}

static int cs_ipc_read_all(int fd, void *buffer, size_t len) {
    char *cursor = (char *) buffer;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t nread = read(fd, cursor, remaining);

        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (nread == 0) {
            return -1;
        }
        cursor += (size_t) nread;
        remaining -= (size_t) nread;
    }

    return 0;
}

static int cs_ipc_connect(const char *socket_path) {
    int fd;
    struct sockaddr_un addr;
    size_t path_len;

    if (!socket_path) {
        return -1;
    }

    path_len = strlen(socket_path);
    if (path_len == 0 || path_len >= sizeof(addr.sun_path)) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_path, path_len + 1);

    if (connect(fd, (struct sockaddr *) &addr, (socklen_t) (offsetof(struct sockaddr_un, sun_path) + path_len + 1u))
        != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int cs_ipc_request(const char *socket_path, const char *json, char **response_out) {
    int fd;
    uint32_t frame_len;
    uint32_t response_len = 0;
    uint32_t response_payload_len;
    char *response = NULL;
    size_t json_len;

    if (!socket_path || !json || !response_out) {
        return -1;
    }

    json_len = strlen(json);
    if (json_len > CS_JAWAKA_IPC_MAX_FRAME) {
        return -1;
    }

    fd = cs_ipc_connect(socket_path);
    if (fd < 0) {
        return -1;
    }

    frame_len = htonl((uint32_t) json_len);
    if (cs_ipc_write_all(fd, &frame_len, sizeof(frame_len)) != 0
        || cs_ipc_write_all(fd, json, json_len) != 0
        || cs_ipc_read_all(fd, &response_len, sizeof(response_len)) != 0) {
        close(fd);
        return -1;
    }

    response_payload_len = ntohl(response_len);
    if (response_payload_len > CS_JAWAKA_IPC_MAX_FRAME) {
        close(fd);
        return -1;
    }

    response = (char *) malloc((size_t) response_payload_len + 1u);
    if (!response) {
        close(fd);
        return -1;
    }

    if (cs_ipc_read_all(fd, response, response_payload_len) != 0) {
        free(response);
        close(fd);
        return -1;
    }

    close(fd);
    response[response_payload_len] = '\0';
    *response_out = response;
    return 0;
}

static int cs_json_type_is(const cJSON *root, const char *expected) {
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");

    return cJSON_IsString(type) && type->valuestring && strcmp(type->valuestring, expected) == 0;
}

static int cs_jawaka_get_library_status(const char *socket_path,
                                        cs_jawaka_library_status *status_out) {
    char *response_text = NULL;
    cJSON *response = NULL;
    const cJSON *scan_error;

    if (!socket_path || !status_out) {
        return -1;
    }
    memset(status_out, 0, sizeof(*status_out));

    if (cs_ipc_request(socket_path, "{\"type\":\"library-status\"}", &response_text) != 0) {
        return -1;
    }
    response = cJSON_Parse(response_text);
    free(response_text);
    if (!response || !cs_json_type_is(response, "library-status")) {
        cJSON_Delete(response);
        return -1;
    }

    status_out->scan_running =
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "scan_running"));
    status_out->pending_rescan =
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "pending_rescan"));
    scan_error = cJSON_GetObjectItemCaseSensitive(response, "scan_error");
    if (cJSON_IsString(scan_error) && scan_error->valuestring) {
        snprintf(status_out->scan_error,
                 sizeof(status_out->scan_error),
                 "%s",
                 scan_error->valuestring);
    }

    cJSON_Delete(response);
    return 0;
}

static int cs_jawaka_wait_for_library_rescan(const char *socket_path,
                                             char *status,
                                             size_t status_size) {
    unsigned int attempt;
    unsigned int consecutive_failures = 0;

    for (attempt = 0; attempt < CS_JAWAKA_SCAN_POLL_ATTEMPTS; ++attempt) {
        cs_jawaka_library_status library_status;

        if (cs_jawaka_get_library_status(socket_path, &library_status) != 0) {
            consecutive_failures += 1;
            if (consecutive_failures >= CS_JAWAKA_SCAN_POLL_MAX_FAILURES) {
                cs_ipc_copy_status(status, status_size, "could not read library scan status");
                return -1;
            }
        } else {
            consecutive_failures = 0;
            if (!library_status.scan_running && !library_status.pending_rescan) {
                if (library_status.scan_error[0] != '\0') {
                    cs_ipc_copy_status(status, status_size, library_status.scan_error);
                    return -1;
                }
                cs_ipc_copy_status(status, status_size, "scan complete");
                return 0;
            }
        }

        if (attempt + 1u < CS_JAWAKA_SCAN_POLL_ATTEMPTS) {
            usleep(CS_JAWAKA_SCAN_POLL_INTERVAL_US);
        }
    }

    cs_ipc_copy_status(status, status_size, "library scan timed out");
    return -1;
}

int cs_jawaka_request_library_rescan(char *status, size_t status_size) {
    char socket_path[CS_PATH_MAX];
    char *response_text = NULL;
    cJSON *response = NULL;
    const cJSON *game_count;
    const cJSON *app_count;
    int rc = -1;

    cs_ipc_copy_status(status, status_size, "");

    if (cs_jawaka_socket_path(socket_path, sizeof(socket_path)) != 0) {
        cs_ipc_copy_status(status, status_size, "could not resolve jawakad socket");
        return -1;
    }

    if (cs_ipc_request(socket_path, "{\"type\":\"scan-library\"}", &response_text) != 0) {
        cs_ipc_copy_status(status, status_size, "jawakad unavailable");
        return -1;
    }

    response = cJSON_Parse(response_text);
    free(response_text);
    if (!response) {
        cs_ipc_copy_status(status, status_size, "jawakad returned invalid JSON");
        return -1;
    }

    if (!cs_json_type_is(response, "ok")) {
        cs_ipc_copy_status(status, status_size, "jawakad rejected library rescan");
        cJSON_Delete(response);
        return -1;
    }

    game_count = cJSON_GetObjectItemCaseSensitive(response, "game_count");
    app_count = cJSON_GetObjectItemCaseSensitive(response, "app_count");
    if (status && status_size > 0 && cJSON_IsNumber(game_count) && cJSON_IsNumber(app_count)) {
        snprintf(status,
                 status_size,
                 "scan complete: %d games, %d apps",
                 game_count->valueint,
                 app_count->valueint);
        rc = 0;
    } else {
        rc = cs_jawaka_wait_for_library_rescan(socket_path, status, status_size);
    }

    cJSON_Delete(response);
    return rc;
}
