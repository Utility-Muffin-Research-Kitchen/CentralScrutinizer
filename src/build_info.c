#include "cs_build_info.h"

const char *cs_build_info_platform_name(void) {
#if defined(PLATFORM_MLP1)
    return "mlp1";
#elif defined(PLATFORM_MAC)
    return "mac";
#else
#error "unsupported platform: define PLATFORM_MLP1 or PLATFORM_MAC"
#endif
}
