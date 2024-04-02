#include "rgb_matrix.h"
#include "firmata/Firmata_QMK.h"
#include "dynld_func.h"
#include <lib/lib8tion/lib8tion.h>

#include "debug.h"

extern dynld_funcs_t g_dynld_funcs;


//typedef HSV (*dx_dy_dist_f)(HSV hsv, int16_t dx, int16_t dy, uint8_t dist, uint8_t time);

static int dynld_env_printf(const char* fmt, ...) {
/*
    char buffer[80];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args); // Build the string
    va_end(args);
    dprintf(buffer);
*/
    return -1;
}

static int _div(int a, int b) {
    return a/b;
}

static int _mod(int a, int b) {
    return a%b;
}

static uint8_t _sqrt16(uint16_t x) {
    return sqrt16(x);
}

static uint8_t _atan2_8(int16_t a, int16_t b) {
    return atan2_8(a, b);
}

static uint8_t _scale8(uint16_t i, uint8_t scale) {
    return scale8(i, scale);
}

static uint8_t _scale16by8(uint16_t i, uint8_t scale) {
    return scale16by8(i, scale);
}

dynld_math_funcs_t s_rgb_math_funs = {
    .div = _div,
    .mod = _mod,
    .sqrt16 = _sqrt16,
    .atan2_8 = _atan2_8,
    .scale8 = _scale8,
    .scale16by8 = _scale16by8,
};


static void _rgb_matrix_set_color_hsv(int index, HSV hsv)
{
    RGB rgb = hsv_to_rgb(hsv);
    rgb_matrix_set_color(index, rgb.r, rgb.g, rgb.b);
}

static dynld_custom_animation_env_t s_custom_animation_env = {
    .set_color = rgb_matrix_set_color,
    .set_color_hsv = _rgb_matrix_set_color_hsv,
    .set_color_all = rgb_matrix_set_color_all,
    .math_funs = &s_rgb_math_funs,

    .led_config = &g_led_config,
    .rgb_config = &rgb_matrix_config,
    .printf = dynld_env_printf
};

uint8_t some_global_state;
void dynld_rgb_animation_init(effect_params_t* params) {
    some_global_state = 1;
}

bool dynld_rgb_animation_run(effect_params_t* params) {
    //dprintf("[DYNLD]rgb anim run iter=%d time=%ld\n", params->iter, s_custom_animation_env.time);
    if (g_dynld_funcs.func[DYNLD_FUN_ID_ANIMATION]) {
        funptr_animation_run_t func_animation = (funptr_animation_run_t)g_dynld_funcs.func[DYNLD_FUN_ID_ANIMATION];
        bool ret = func_animation(&s_custom_animation_env, params);
        if (debug_config.dynld) {
            dprintf("[DYNLD]rgb anim buf: %d %d %d %d\n", s_custom_animation_env.buf[0], s_custom_animation_env.buf[1],
                                                        s_custom_animation_env.buf[2], s_custom_animation_env.buf[3]);
        }
        return ret;
    }
    return true;
}

bool dynld_rgb_animation(effect_params_t* params) {
    if (params->init) dynld_rgb_animation_init(params);
    s_custom_animation_env.time = g_rgb_timer;
    return dynld_rgb_animation_run(params);
}

