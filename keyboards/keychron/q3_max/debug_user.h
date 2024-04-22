#pragma once

#include <stdbool.h>
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#define _Static_assert static_assert
#endif

#define STATIC_ASSERT_SIZEOF_STRUCT_RAW(type, msg) _Static_assert(sizeof(type) == sizeof(((type*)0)->raw), msg)

/*
 * debug user custom flags
 */
typedef union {
    struct {
        bool    firmata : 1;
        bool    stats : 1;
        bool    user_anim : 1;
        //bool    via : 1;
        //uint32_t reserved : ..;
    };
    uint32_t raw;
} debug_config_user_t;

extern debug_config_user_t  debug_config_user;

STATIC_ASSERT_SIZEOF_STRUCT_RAW(debug_config_user_t, "debug_config_t out of size spec.");

#ifdef DEVEL_BUILD
#define DBG_USR(m, ...) do { if (debug_config_user.m) dprintf(__VA_ARGS__); } while (0)
#else
#define DBG_USR(m, ...)
#endif

#ifdef __cplusplus
}
#endif
