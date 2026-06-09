#include "cs_keep_awake.h"

#include "cs_build_info.h"

#include <stdlib.h>

const char *cs_keep_awake_platform_name(void) {
    const char *override = getenv("CS_PLATFORM_NAME_OVERRIDE");

    if (override && override[0] != '\0') {
        return override;
    }

    return cs_build_info_platform_name();
}

int cs_keep_awake_current_platform_uses_settings_override(void) {
    return 0;
}

int cs_keep_awake_enable(const cs_paths *paths) {
    if (!paths) {
        return -1;
    }

    return 0;
}

int cs_keep_awake_disable(const cs_paths *paths) {
    if (!paths) {
        return -1;
    }

    return 0;
}
