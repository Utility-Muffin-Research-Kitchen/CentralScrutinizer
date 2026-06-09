#if defined(CS_ENABLE_CATASTROPHE_UI)
#define CAT_IMPLEMENTATION
#include "catastrophe.h"
#define CAT_WIDGETS_IMPLEMENTATION
#include "catastrophe_widgets.h"
#endif

#include "cs_app.h"

int main(int argc, char **argv) {
    return cs_app_run(argc, argv);
}
