#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include "cs_app.h"
#include "cs_paths.h"
#include "cs_server.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void make_dir_p(const char *path) {
    char buffer[CS_PATH_MAX];
    char *cursor;

    assert(path != NULL);
    assert(snprintf(buffer, sizeof(buffer), "%s", path) > 0);
    for (cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            assert(mkdir(buffer, 0700) == 0 || access(buffer, F_OK) == 0);
            *cursor = '/';
        }
    }
    assert(mkdir(buffer, 0700) == 0 || access(buffer, F_OK) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static int reserve_local_port(void) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);
    assert(getsockname(fd, (struct sockaddr *) &addr, &addr_len) == 0);
    close(fd);
    return (int) ntohs(addr.sin_port);
}

static int read_socket_response(int fd, char *buffer, size_t buffer_len) {
    size_t total = 0;

    assert(buffer != NULL);
    assert(buffer_len > 0);

    while (total + 1 < buffer_len) {
        ssize_t nread = recv(fd, buffer + total, buffer_len - total - 1, 0);

        if (nread < 0) {
            return -1;
        }
        if (nread == 0) {
            break;
        }
        total += (size_t) nread;
    }

    buffer[total] = '\0';
    return 0;
}

static int open_loopback(int port) {
    struct sockaddr_in addr;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);
    return fd;
}

static int post_form_response(int port,
                              const char *path,
                              const char *body,
                              const char *cookie,
                              const char *csrf,
                              char *buffer,
                              size_t buffer_len) {
    int fd;
    char request[2048];
    int written;
    size_t body_len;

    assert(path != NULL);
    assert(body != NULL);
    assert(buffer != NULL);
    assert(buffer_len > 0);

    body_len = strlen(body);
    fd = open_loopback(port);

    written = snprintf(request,
                       sizeof(request),
                       "POST %s HTTP/1.1\r\n"
                       "Host: 127.0.0.1:%d\r\n"
                       "Connection: close\r\n"
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "%s%s%s"
                       "%s%s%s"
                       "Content-Length: %zu\r\n"
                       "\r\n"
                       "%s",
                       path,
                       port,
                       cookie ? "Cookie: " : "",
                       cookie ? cookie : "",
                       cookie ? "\r\n" : "",
                       csrf ? "X-CS-CSRF: " : "",
                       csrf ? csrf : "",
                       csrf ? "\r\n" : "",
                       body_len,
                       body);
    assert(written > 0 && (size_t) written < sizeof(request));
    assert(send(fd, request, (size_t) written, 0) == written);
    assert(read_socket_response(fd, buffer, buffer_len) == 0);
    close(fd);
    return 0;
}

static void extract_cookie_value(const char *response, char *buffer, size_t buffer_len) {
    const char *header;
    const char *value_start;
    const char *value_end;
    size_t value_len;

    assert(response != NULL);
    assert(buffer != NULL);
    assert(buffer_len > 0);

    header = strstr(response, "Set-Cookie: ");
    assert(header != NULL);
    value_start = header + strlen("Set-Cookie: ");
    value_end = strchr(value_start, ';');
    assert(value_end != NULL);
    value_len = (size_t) (value_end - value_start);
    assert(value_len < buffer_len);
    memcpy(buffer, value_start, value_len);
    buffer[value_len] = '\0';
}

static void extract_json_string(const char *response, const char *key, char *buffer, size_t buffer_len) {
    const char *start;
    const char *end;
    size_t value_len;

    assert(response != NULL);
    assert(key != NULL);
    assert(buffer != NULL);
    assert(buffer_len > 0);

    start = strstr(response, key);
    assert(start != NULL);
    start += strlen(key);
    end = strchr(start, '"');
    assert(end != NULL);
    value_len = (size_t) (end - start);
    assert(value_len < buffer_len);
    memcpy(buffer, start, value_len);
    buffer[value_len] = '\0';
}

static int favorite_count(const char *db_path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int count = -1;

    assert(sqlite3_open(db_path, &db) == SQLITE_OK);
    assert(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM favorites WHERE kind = 'game';", -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    count = sqlite3_column_int(stmt, 0);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    assert(sqlite3_close(db) == SQLITE_OK);
    return count;
}

int main(void) {
    char template[] = "/tmp/cs-routes-favorites-XXXXXX";
    char *root;
    char web_root[CS_PATH_MAX];
    char roms_dir[CS_PATH_MAX];
    char system_dir[CS_PATH_MAX];
    char rom_path[CS_PATH_MAX];
    char state_dir[CS_PATH_MAX];
    char db_path[CS_PATH_MAX];
    char response[4096];
    char cookie[256];
    char csrf[128];
    sqlite3 *db = NULL;
    char *err = NULL;
    cs_app app;

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(web_root, sizeof(web_root), "%s/web", root) > 0);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/GBA", root) > 0);
    assert(snprintf(rom_path, sizeof(rom_path), "%s/Pokemon Emerald.gba", system_dir) > 0);
    assert(snprintf(state_dir, sizeof(state_dir), "%s/.system/leaf/platforms/mlp1/state", root) > 0);
    assert(snprintf(db_path, sizeof(db_path), "%s/library.db", state_dir) > 0);

    make_dir(web_root);
    make_dir(roms_dir);
    make_dir(system_dir);
    make_dir_p(state_dir);
    write_file(rom_path, "rom");

    assert(sqlite3_open(db_path, &db) == SQLITE_OK);
    assert(sqlite3_exec(db,
                        "CREATE TABLE games ("
                        "id INTEGER PRIMARY KEY,"
                        "system TEXT NOT NULL,"
                        "name TEXT NOT NULL,"
                        "rom_path TEXT NOT NULL UNIQUE,"
                        "image_path TEXT,"
                        "last_played INTEGER,"
                        "playtime_s INTEGER NOT NULL DEFAULT 0"
                        ");"
                        "CREATE TABLE favorites ("
                        "kind TEXT NOT NULL CHECK (kind IN ('game','app')),"
                        "target_id INTEGER NOT NULL,"
                        "added_at INTEGER NOT NULL,"
                        "PRIMARY KEY (kind, target_id)"
                        ");"
                        "INSERT INTO games (system, name, rom_path, image_path) VALUES "
                        "('GBA', 'Pokemon Emerald', 'Roms/GBA/Pokemon Emerald.gba', NULL);",
                        NULL,
                        NULL,
                        &err)
           == SQLITE_OK);
    assert(err == NULL);
    assert(sqlite3_close(db) == SQLITE_OK);

    assert(setenv("SDCARD_PATH", root, 1) == 0);
    assert(setenv("UMRK_INTERNAL_DATA_PATH", state_dir, 1) == 0);
    assert(setenv("CS_WEB_ROOT", web_root, 1) == 0);
    assert(setenv("CS_PAIRING_CODE", "7391", 1) == 0);
    assert(setenv("CS_PAIRING_CODE_REUSE", "1", 1) == 0);

    memset(&app, 0, sizeof(app));
    atomic_init(&app.terminal_enabled, 0);
    assert(cs_paths_init(&app.paths) == 0);
    app.port = reserve_local_port();
    assert(cs_server_start(&app) == 0);

    assert(post_form_response(app.port,
                              "/api/pair",
                              "browser_id=favorite-route&code=7391",
                              NULL,
                              NULL,
                              response,
                              sizeof(response))
           == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    extract_cookie_value(response, cookie, sizeof(cookie));
    extract_json_string(response, "\"csrf\":\"", csrf, sizeof(csrf));
    assert(cs_server_cookie_is_valid(cookie) == 1);

    assert(favorite_count(db_path) == 0);
    assert(post_form_response(app.port,
                              "/api/favorite/game",
                              "tag=GBA&path=Pokemon+Emerald.gba&favorite=1",
                              cookie,
                              csrf,
                              response,
                              sizeof(response))
           == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(favorite_count(db_path) == 1);

    assert(post_form_response(app.port,
                              "/api/favorite/game",
                              "tag=GBA&path=Pokemon+Emerald.gba&favorite=0",
                              cookie,
                              csrf,
                              response,
                              sizeof(response))
           == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(favorite_count(db_path) == 0);

    assert(post_form_response(app.port,
                              "/api/favorite/game",
                              "tag=GBA&path=Missing.gba&favorite=1",
                              cookie,
                              csrf,
                              response,
                              sizeof(response))
           == 0);
    assert(strstr(response, "HTTP/1.1 404 Not Found") != NULL);

    cs_server_stop();
    assert(unsetenv("CS_PAIRING_CODE_REUSE") == 0);
    assert(unsetenv("CS_PAIRING_CODE") == 0);
    assert(unsetenv("CS_WEB_ROOT") == 0);
    assert(unsetenv("UMRK_INTERNAL_DATA_PATH") == 0);
    assert(unsetenv("SDCARD_PATH") == 0);
    return 0;
}
