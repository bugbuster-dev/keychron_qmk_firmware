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
        bool    qmkata : 1;
        bool    stats : 1;
        bool    user_anim : 1;
        //bool    via : 1;
        //uint32_t reserved : ..;
    };
    uint32_t raw;
} debug_config_user_t;
extern debug_config_user_t debug_config_user;

typedef union {
    struct {
        bool pub_keypress:1;
        bool process_keypress:1;
    };
    uint32_t raw;
} devel_config_t;
extern devel_config_t devel_config;

STATIC_ASSERT_SIZEOF_STRUCT_RAW(debug_config_user_t, "debug_config_t out of size spec.");

#ifdef CONSOLE_ENABLE
#define DBG_USR(m, ...) do { \
    if (debug_config_user.m) { \
        debug_config_user_t dcu = {.m = 1}; \
        if (dcu.qmkata) xprintf("QA:"); \
        if (dcu.stats) xprintf("STS:"); \
        if (dcu.user_anim) xprintf("UAN:"); \
        xprintf(__VA_ARGS__); \
    } } while (0)
#else
#define DBG_USR(m, ...)
#endif

#ifdef __cplusplus
}
#endif
