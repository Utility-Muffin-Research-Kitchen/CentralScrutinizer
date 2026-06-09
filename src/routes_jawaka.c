#include "cs_jawaka_ipc.h"
#include "cs_routes_helpers.h"

#include "civetweb.h"

int cs_route_library_rescan_handler(struct mg_connection *conn, void *cbdata) {
    char status[256];
    int guard_status = cs_routes_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }

    if (cs_jawaka_request_library_rescan(status, sizeof(status)) != 0) {
        return cs_routes_write_json(conn, 503, "Service Unavailable", "{\"ok\":false,\"error\":\"rescan_failed\"}");
    }

    return cs_routes_write_json(conn, 200, "OK", "{\"ok\":true}");
}
