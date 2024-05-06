#include "debug_user.h"

debug_config_user_t debug_config_user = {
    .raw = 0,
};

devel_config_t devel_config = {
    .pub_keypress = 0, // publish keypress events
    .process_keypress = 1,
};

#ifdef DEVEL_BUILD
const char* __BUILD_DATE__ = "BUILD: " __DATE__ " " __TIME__ "\n";
#endif
