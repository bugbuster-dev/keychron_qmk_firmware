#pragma once

#include <stdbool.h>
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#define _Static_assert static_assert
#endif

/*
 * debug user custom flags
 */
typedef union {
    struct {
        bool    stats : 1;
        bool    dynld : 1;
        //bool    via : 1;
        //uint32_t reserved : ..;
    };
    uint32_t raw;
} debug_config_user_t;

#define STATIC_ASSERT_SIZEOF_STRUCT_RAW(type, msg) _Static_assert(sizeof(type) == sizeof(((type*)0)->raw), msg)
STATIC_ASSERT_SIZEOF_STRUCT_RAW(debug_config_user_t, "debug_config_t out of size spec.");

extern debug_config_user_t  debug_config_user;

#ifdef __cplusplus
}
#endif
