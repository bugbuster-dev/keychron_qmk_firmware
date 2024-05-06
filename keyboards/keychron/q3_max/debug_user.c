#include "debug_user.h"

debug_config_user_t debug_config_user = {
    .raw = 0,
};

devel_config_t devel_config = {
    .pub_keypress = 0, // publish keypress events
    .process_keypress = 1,
};
