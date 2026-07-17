#include <assert.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cs_jawaka_ipc.h"

typedef struct fake_exchange {
    const char *expected_request_type;
    const char *response;
} fake_exchange;

static int read_all(int fd, void *buffer, size_t size) {
    unsigned char *cursor = (unsigned char *) buffer;

    while (size > 0) {
        ssize_t read_count = read(fd, cursor, size);

        if (read_count <= 0) {
            return -1;
        }
        cursor += (size_t) read_count;
        size -= (size_t) read_count;
    }
    return 0;
}

static int write_all(int fd, const void *buffer, size_t size) {
    const unsigned char *cursor = (const unsigned char *) buffer;

    while (size > 0) {
        ssize_t write_count = write(fd, cursor, size);

        if (write_count <= 0) {
            return -1;
        }
        cursor += (size_t) write_count;
        size -= (size_t) write_count;
    }
    return 0;
}

static void serve_exchange(int server_fd, const fake_exchange *exchange) {
    int client_fd;
    uint32_t request_size_network;
    uint32_t request_size;
    uint32_t response_size_network;
    char request[512];
    char expected_type[128];
    size_t response_size;

    client_fd = accept(server_fd, NULL, NULL);
    assert(client_fd >= 0);
    assert(read_all(client_fd, &request_size_network, sizeof(request_size_network)) == 0);
    request_size = ntohl(request_size_network);
    assert(request_size > 0 && request_size < sizeof(request));
    assert(read_all(client_fd, request, request_size) == 0);
    request[request_size] = '\0';
    assert(snprintf(expected_type,
                    sizeof(expected_type),
                    "\"type\":\"%s\"",
                    exchange->expected_request_type)
           > 0);
    assert(strstr(request, expected_type) != NULL);

    response_size = strlen(exchange->response);
    response_size_network = htonl((uint32_t) response_size);
    assert(write_all(client_fd, &response_size_network, sizeof(response_size_network)) == 0);
    assert(write_all(client_fd, exchange->response, response_size) == 0);
    assert(close(client_fd) == 0);
}

static pid_t start_fake_server(const char *socket_path,
                               const fake_exchange *exchanges,
                               size_t exchange_count) {
    struct sockaddr_un address;
    int server_fd;
    pid_t child;
    size_t i;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(server_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    assert(strlen(socket_path) < sizeof(address.sun_path));
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1u);
    assert(bind(server_fd, (struct sockaddr *) &address, sizeof(address)) == 0);
    assert(listen(server_fd, 4) == 0);

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        for (i = 0; i < exchange_count; ++i) {
            serve_exchange(server_fd, &exchanges[i]);
        }
        close(server_fd);
        _exit(0);
    }

    assert(close(server_fd) == 0);
    return child;
}

static void finish_fake_server(pid_t child, const char *socket_path, const char *directory) {
    int child_status = 0;

    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFEXITED(child_status));
    assert(WEXITSTATUS(child_status) == 0);
    assert(unlink(socket_path) == 0);
    assert(rmdir(directory) == 0);
}

static void make_socket_fixture(char *directory,
                                size_t directory_size,
                                char *socket_path,
                                size_t socket_path_size) {
    assert(snprintf(directory, directory_size, "/tmp/cs-jawaka-ipc-XXXXXX") > 0);
    assert(mkdtemp(directory) != NULL);
    assert(snprintf(socket_path, socket_path_size, "%s/jawakad.sock", directory) > 0);
}

static void test_waits_for_async_scan_completion(void) {
    const fake_exchange exchanges[] = {
        {"scan-library", "{\"type\":\"ok\",\"action\":\"scan-library started\"}"},
        {"library-status", "{\"type\":\"library-status\",\"generation\":4,\"scan_running\":true,\"pending_rescan\":false,\"scan_error\":\"\"}"},
        {"library-status", "{\"type\":\"library-status\",\"generation\":5,\"scan_running\":false,\"pending_rescan\":false,\"scan_error\":\"\"}"},
    };
    char directory[128];
    char socket_path[256];
    char status[256];
    pid_t child;

    make_socket_fixture(directory, sizeof(directory), socket_path, sizeof(socket_path));
    child = start_fake_server(socket_path, exchanges, sizeof(exchanges) / sizeof(exchanges[0]));
    assert(setenv("JAWAKA_SOCKET_PATH", socket_path, 1) == 0);

    assert(cs_jawaka_request_library_rescan(status, sizeof(status)) == 0);
    assert(strcmp(status, "scan complete") == 0);

    unsetenv("JAWAKA_SOCKET_PATH");
    finish_fake_server(child, socket_path, directory);
}

static void test_accepts_legacy_synchronous_scan_reply(void) {
    const fake_exchange exchanges[] = {
        {"scan-library", "{\"type\":\"ok\",\"game_count\":12,\"app_count\":3}"},
    };
    char directory[128];
    char socket_path[256];
    char status[256];
    pid_t child;

    make_socket_fixture(directory, sizeof(directory), socket_path, sizeof(socket_path));
    child = start_fake_server(socket_path, exchanges, sizeof(exchanges) / sizeof(exchanges[0]));
    assert(setenv("JAWAKA_SOCKET_PATH", socket_path, 1) == 0);

    assert(cs_jawaka_request_library_rescan(status, sizeof(status)) == 0);
    assert(strcmp(status, "scan complete: 12 games, 3 apps") == 0);

    unsetenv("JAWAKA_SOCKET_PATH");
    finish_fake_server(child, socket_path, directory);
}

int main(void) {
    test_waits_for_async_scan_completion();
    test_accepts_legacy_synchronous_scan_reply();
    return 0;
}
