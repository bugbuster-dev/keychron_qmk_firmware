#pragma once

#include <stdint.h>
#include <stdbool.h>


// dynamically loaded code must contain only 1 function and can not include other functions at the moment,
// so only calling inline functions allowed in the dynamic loaded function.
// some math operations are not supported on arm m0 and software implementation is used.
// these calls to the software implementations do not work in the dynamically loaded code, these callbacks can be used instead.
typedef int     (*funptr_div)(int a, int b);
typedef int     (*funptr_mod)(int a, int b);
typedef uint8_t (*funptr_sqrt16)(uint16_t x);
typedef uint8_t (*funptr_atan2_8)(int16_t a, int16_t b);
typedef uint8_t (*funptr_scale8)(uint16_t i, uint8_t scale);
typedef uint8_t (*funptr_scale16by8)(uint16_t i, uint8_t scale);

typedef struct dynld_math_funcs {
    funptr_div          div;
    funptr_mod          mod;
    funptr_sqrt16       sqrt16;
    funptr_atan2_8      atan2_8;
    funptr_scale8       scale8;
    funptr_scale16by8   scale16by8;
} dynld_math_funcs_t;


typedef void (*funptr_rgb_matrix_set_color)(int index, uint8_t red, uint8_t green, uint8_t blue);
typedef void (*funptr_rgb_matrix_set_color_hsv)(int index, HSV hsv);
typedef void (*funptr_rgb_matrix_set_color_all)(uint8_t red, uint8_t green, uint8_t blue);
typedef int (*funptr_printf)(const char* fmt, ...);

typedef struct __attribute__ ((aligned (4))) dynld_custom_animation_env {
    funptr_rgb_matrix_set_color         set_color;
    funptr_rgb_matrix_set_color_hsv     set_color_hsv;
    funptr_rgb_matrix_set_color_all     set_color_all;
    funptr_printf                       printf;
    dynld_math_funcs_t                 *math_funs;

    led_config_t                       *led_config;
    rgb_config_t                       *rgb_config;
    uint32_t                            time;
    uint8_t                             buf[64];
} dynld_custom_animation_env_t;

typedef bool (*funptr_animation_run_t)(dynld_custom_animation_env_t *anim_env, effect_params_t* params);

typedef struct __attribute__ ((aligned (4))) dynld_test_env {
    funptr_printf   printf;

    uint8_t         buf[256];
} dynld_test_env_t;

typedef int (*funptr_test_t)(dynld_test_env_t *test_env);
