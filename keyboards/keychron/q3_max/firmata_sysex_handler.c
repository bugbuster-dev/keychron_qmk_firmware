#include "action_layer.h"
#include "rgb_matrix.h"
#include "keycode_config.h"
#include "debug.h"
#include "debug_user.h"

#include "firmata/Firmata_QMK.h"
#include "dynld_func.h"

//------------------------------------------------------------------------------

static void dprintf_buf(uint8_t *buf, uint8_t len) {
#ifdef CONSOLE_ENABLE
    if (debug_config.enable) {
        for (int i = 0; i < len; i++) {
            dprintf("%02x ", buf[i]);
            if (((i+1)%16) == 0) dprintf("\n");
        }
        dprintf("\n");
    }
#endif
}

// adjusting the function pointer for thumb mode
uint32_t thumb_fun_addr(void* funptr) {
    uint8_t thumb_bit = 0;
#ifdef THUMB_PRESENT
    thumb_bit = 1;
#endif
    return (((uint32_t)funptr) | thumb_bit);
}

#define DYNLD_FUNC_SIZE 1024 // dynld function max size
static uint8_t dynld_func_buf[DYNLD_FUN_ID_MAX][DYNLD_FUNC_SIZE] __attribute__((aligned(4)));
dynld_funcs_t g_dynld_funcs = { 0 };

void load_function(const uint16_t fun_id, const uint8_t* data, size_t offset, size_t size) {
    // set function pointer after fully loaded
    if (offset == 0xffff) {
        if (memcmp(dynld_func_buf[fun_id], "\0\0", 2) != 0) {
            g_dynld_funcs.func[fun_id] = (void*)thumb_fun_addr((uint8_t*)dynld_func_buf[fun_id]);
            DBG_USR(firmata, "[FA]dynld", " fun[%d]:%p\n", (int)fun_id, g_dynld_funcs.func[fun_id]);
        }
        return;
    }
    if (offset + size > DYNLD_FUNC_SIZE) {
        memset((void*)&dynld_func_buf[fun_id][offset], 0, DYNLD_FUNC_SIZE);
        g_dynld_funcs.func[fun_id] = NULL;
        DBG_USR(firmata, "[FA]dynld", " fun too large\n");
        return;
    }
    if (fun_id >= DYNLD_FUN_ID_MAX) {
        DBG_USR(firmata, "[FA]dynld", " fun id too large\n");
        return;
    }
    if (offset == 0) {
        g_dynld_funcs.func[fun_id] = NULL;
        memset((void*)dynld_func_buf[fun_id], 0, DYNLD_FUNC_SIZE);
        if (size >= 2 && memcmp(data, "\0\0", 2) == 0) {
            DBG_USR(firmata, "[FA]dynld", " fun[%d]:0\n", (int)fun_id);
            return;
        }
    }
    memcpy((void*)&dynld_func_buf[fun_id][offset], data, size);
}

//------------------------------------------------------------------------------
rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

void firmata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t *buf) {
    if (cmd == FRMT_CMD_SET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_RGB_MATRIX_BUF)   _FRMT_HANDLE_CMD_SET_FN(rgb_matrix_buf)(cmd, len, buf);
        if (id == FRMT_ID_DEFAULT_LAYER)    _FRMT_HANDLE_CMD_SET_FN(default_layer)(cmd, len, buf);
        if (id == FRMT_ID_DEBUG_MASK)       _FRMT_HANDLE_CMD_SET_FN(debug_mask)(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _FRMT_HANDLE_CMD_SET_FN(macwin_mode)(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_MODE)  _FRMT_HANDLE_CMD_SET_FN(rgb_matrix_mode)(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_HSV)   _FRMT_HANDLE_CMD_SET_FN(rgb_matrix_hsv)(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNCTION)   _FRMT_HANDLE_CMD_SET_FN(dynld_function)(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNEXEC)    _FRMT_HANDLE_CMD_SET_FN(dynld_funexec)(cmd, len, buf);
        if (id == FRMT_ID_CONFIG)           _FRMT_HANDLE_CMD_SET_FN(config)(cmd, len, buf);
    }
    if (cmd == FRMT_CMD_GET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_DEFAULT_LAYER)    _FRMT_HANDLE_CMD_GET_FN(default_layer)(cmd, len, buf);
        if (id == FRMT_ID_DEBUG_MASK)       _FRMT_HANDLE_CMD_GET_FN(debug_mask)(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _FRMT_HANDLE_CMD_GET_FN(macwin_mode)(cmd, len, buf);
        if (id == FRMT_ID_BATTERY_STATUS)   _FRMT_HANDLE_CMD_GET_FN(battery_status)(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_MODE)  _FRMT_HANDLE_CMD_GET_FN(rgb_matrix_mode)(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_HSV)   _FRMT_HANDLE_CMD_GET_FN(rgb_matrix_hsv)(cmd, len, buf);
        if (id == FRMT_ID_CONFIG_LAYOUT)    _FRMT_HANDLE_CMD_GET_FN(config_layout)(cmd, len, buf);
        if (id == FRMT_ID_CONFIG)           _FRMT_HANDLE_CMD_GET_FN(config)(cmd, len, buf);
    }
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(rgb_matrix_buf) {
    for (uint16_t i = 0; i < len;) {
        //DBG_USR(firmata, "rgb:","%d(%d):%d,%d,%d\n", (int)buf[i], (int)buf[i+1], (int)buf[i+2], (int)buf[i+3], (int)buf[i+4]);
        uint8_t li = buf[i++];
        if (li < RGB_MATRIX_LED_COUNT) {
            g_rgb_matrix_host_buf.led[li].duration = buf[i++];
            g_rgb_matrix_host_buf.led[li].r = buf[i++];
            g_rgb_matrix_host_buf.led[li].g = buf[i++];
            g_rgb_matrix_host_buf.led[li].b = buf[i++];

            g_rgb_matrix_host_buf.written = 1;
        }
        else
            break;
    }
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(default_layer) {
    DBG_USR(firmata, "[FA]","layer:%d\n", (int)buf[0]);
    layer_state_t state = 1 << buf[0];
    default_layer_set(state);
}

_FRMT_HANDLE_CMD_GET(default_layer) {
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(debug_mask) {
    debug_config.raw = buf[0];
    if (len < 4) {
        DBG_USR(firmata, "[FA]","debug:%02x\n", buf[0]);
        return;
    }
    if (len == 8) {
        const int off = 4;
        uint32_t dbg_user_mask = buf[off] | buf[off+1] << 8 | buf[off+2] << 16 | buf[off+3] << 24;
        debug_config_user.raw = dbg_user_mask;
        DBG_USR(firmata, "[FA]","debug:%02x:%08lx\n", debug_config.raw, debug_config_user.raw);
    }
}

_FRMT_HANDLE_CMD_GET(debug_mask) {
    uint8_t response[9];
    response[0] = FRMT_ID_DEBUG_MASK;
    response[1] = debug_config.raw;
    response[2] = response[3] = response[4] = 0;
    response[5] = debug_config_user.raw;
    response[6] = debug_config_user.raw >> 8;
    response[7] = debug_config_user.raw >> 16;
    response[8] = debug_config_user.raw >> 24;
    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(macwin_mode) {
    extern void keyb_user_set_macwin_mode(int mode);
    int mode = buf[0];
    DBG_USR(firmata, "[FA]","macwin:%c\n", buf[0]);
    if (buf[0] == '-') mode = -1;
    keyb_user_set_macwin_mode(mode);
}

_FRMT_HANDLE_CMD_GET(macwin_mode) {
    extern int keyb_user_get_macwin_mode(void);
    uint8_t response[2];
    response[0] = FRMT_ID_MACWIN_MODE;
    response[1] = keyb_user_get_macwin_mode();
    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_GET(battery_status) {
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(rgb_matrix_mode) {
    uint8_t matrix_mode = buf[0];
    DBG_USR(firmata, "[FA]","rgb:%d\n", (int)matrix_mode);
    if (matrix_mode) {
        rgb_matrix_enable_noeeprom();
        rgb_matrix_mode_noeeprom(matrix_mode);
    } else {
        rgb_matrix_disable_noeeprom();
    }
}

_FRMT_HANDLE_CMD_GET(rgb_matrix_mode) {
    uint8_t response[2];
    response[0] = FRMT_ID_RGB_MATRIX_MODE;
    response[1] = rgb_matrix_get_mode();

    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(rgb_matrix_hsv) {
    uint8_t h = buf[0];
    uint8_t s = buf[1];
    uint8_t v = buf[2];
    DBG_USR(firmata, "[FA]","hsv:%d,%d,%d\n", (int)h, (int)s, (int)v);
    rgb_matrix_sethsv_noeeprom(h, s, v);
}

_FRMT_HANDLE_CMD_GET(rgb_matrix_hsv) {
    uint8_t response[4];
    HSV hsv = rgb_matrix_get_hsv();
    response[0] = FRMT_ID_RGB_MATRIX_HSV;
    response[1] = hsv.h;
    response[2] = hsv.s;
    response[3] = hsv.v;
    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

//------------------------------------------------------------------------------
enum config_id {
    CONFIG_ID_DEBUG = 1,
    CONFIG_ID_DEBUG_USER,
    CONFIG_ID_RGB_MATRIX,
    CONFIG_ID_KEYMAP,
    CONFIG_ID_MAX
};

_FRMT_HANDLE_CMD_SET(config) { // todo bb: set "configuration struct" (debug, rgb, keymap, ...)
    uint8_t config_id = buf[0];
    DBG_USR(firmata, "[FA]","config:set:%u\n", config_id);

    if (config_id == CONFIG_ID_DEBUG) {
        memcpy(&debug_config, &buf[1], sizeof(debug_config));
    }
    if (config_id == CONFIG_ID_DEBUG_USER) {
        memcpy(&debug_config_user, &buf[1], sizeof(debug_config_user));
    }
    if (config_id == CONFIG_ID_RGB_MATRIX) {
        memcpy(&rgb_matrix_config, &buf[1], sizeof(rgb_matrix_config));
    }
    if (config_id == CONFIG_ID_KEYMAP) {
        memcpy(&keymap_config, &buf[1], sizeof(keymap_config));
    }
}

_FRMT_HANDLE_CMD_GET(config) {
    uint8_t config_id = buf[0];
    DBG_USR(firmata, "[FA]","config:get:%u\n", config_id);

    uint8_t resp[16];
    resp[0] = FRMT_ID_CONFIG;
    resp[1] = config_id;
    if (config_id == CONFIG_ID_DEBUG) {
        resp[2] = debug_config.raw;
        firmata_send_sysex(FRMT_CMD_RESPONSE, resp, 2+sizeof(debug_config));
    }
    if (config_id == CONFIG_ID_DEBUG_USER) {
        memcpy(&resp[2], &debug_config_user, sizeof(debug_config_user));
        firmata_send_sysex(FRMT_CMD_RESPONSE, resp, 2+sizeof(debug_config_user));
    }
    if (config_id == CONFIG_ID_RGB_MATRIX) {
        memcpy(&resp[2], &rgb_matrix_config, sizeof(rgb_matrix_config));
        firmata_send_sysex(FRMT_CMD_RESPONSE, resp, 2+sizeof(rgb_matrix_config));
    }
    if (config_id == CONFIG_ID_KEYMAP) {
        memcpy(&resp[2], &keymap_config, sizeof(keymap_config));
        firmata_send_sysex(FRMT_CMD_RESPONSE, resp, 2+sizeof(keymap_config));
    }
}

//------------------------------------------------------------------------------
// todo monitoring task
// - config changes
// - state machine to handle future commands which needs multiple "task steps"
// ...

//------------------------------------------------------------------------------
enum config_debug_field {
    CONFIG_FIELD_DEBUG_ENABLE = 1,
    CONFIG_FIELD_DEBUG_MATRIX,
    CONFIG_FIELD_DEBUG_KEYBOARD,
    CONFIG_FIELD_DEBUG_MOUSE,
};

enum config_debug_user_field {
    CONFIG_FIELD_DEBUG_USER_FIRMATA = 1,
    CONFIG_FIELD_DEBUG_USER_STATS,
    CONFIG_FIELD_DEBUG_USER_USER_ANIM,
};

enum config_rgb_field {
    CONFIG_FIELD_RGB_ENABLE = 1,
    CONFIG_FIELD_RGB_MODE,
    CONFIG_FIELD_RGB_HSV_H,
    CONFIG_FIELD_RGB_HSV_S,
    CONFIG_FIELD_RGB_HSV_V,
    CONFIG_FIELD_RGB_SPEED,
    CONFIG_FIELD_RGB_FLAGS,
};

enum config_keymap_field {
    CONFIG_FIELD_KEYMAP_SWAP_CONTROL_CAPSLOCK = 1,
    CONFIG_FIELD_KEYMAP_CAPSLOCK_TO_CONTROL,
    CONFIG_FIELD_KEYMAP_SWAP_LALT_LGUI,
    CONFIG_FIELD_KEYMAP_SWAP_RALT_RGUI,
    CONFIG_FIELD_KEYMAP_NO_GUI,
    CONFIG_FIELD_KEYMAP_SWAP_GRAVE_ESC,
    CONFIG_FIELD_KEYMAP_SWAP_BACKSLASH_BACKSPACE,
    CONFIG_FIELD_KEYMAP_NKRO,
    CONFIG_FIELD_KEYMAP_SWAP_LCTL_LGUI,
    CONFIG_FIELD_KEYMAP_SWAP_RCTL_RGUI,
    CONFIG_FIELD_KEYMAP_ONESHOT_ENABLE,
    CONFIG_FIELD_KEYMAP_SWAP_ESCAPE_CAPSLOCK,
    CONFIG_FIELD_KEYMAP_AUTOCORRECT_ENABLE,
};

enum config_field_type {
    CONFIG_FIELD_TYPE_BIT = 1,
    CONFIG_FIELD_TYPE_UINT8,
    CONFIG_FIELD_TYPE_UINT16,
    CONFIG_FIELD_TYPE_UINT32,
    CONFIG_FIELD_TYPE_UINT64,
    CONFIG_FIELD_TYPE_FLOAT,
    CONFIG_FIELD_TYPE_ARRAY,
};

//<config id>:<size>:<field id>:<type>:<offset>:<size> // offset: byte or bit offset
_FRMT_HANDLE_CMD_GET(config_layout) {
    static int _bit_order = -1;
    if (_bit_order < 0) {
        debug_config_t dc = { .raw = 0 };
        dc.enable = 1;
        if (dc.raw == 1) _bit_order = 0; // lsb first
        else _bit_order = 1;
    }
    uint8_t resp[60];
    int n = 0;
    #define LSB_FIRST (_bit_order == 0)
    #define BITPOS(b,n) LSB_FIRST? b : n-1-b
    #define CONFIG_LAYOUT(id, size) \
            resp[0] = FRMT_ID_CONFIG_LAYOUT; \
            resp[1] = id; \
            resp[2] = size; \
            n = 3;
    #define BITFIELD(id,index,nbits,size) \
            resp[n] = id; \
            resp[n+1] = CONFIG_FIELD_TYPE_BIT; \
            resp[n+2] = BITPOS(index, size); \
            resp[n+3] = nbits; n += 4;
    #define BYTEFIELD(id,offset) \
            resp[n] = id; \
            resp[n+1] = CONFIG_FIELD_TYPE_UINT8; \
            resp[n+2] = offset; \
            resp[n+3] = 1; n += 4;

    //--------------------------------
    CONFIG_LAYOUT(CONFIG_ID_DEBUG, sizeof(debug_config_t))
    BITFIELD(CONFIG_FIELD_DEBUG_ENABLE,     0, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_MATRIX,     1, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_KEYBOARD,   2, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_MOUSE,      3, 1, 8);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    CONFIG_LAYOUT(CONFIG_ID_DEBUG_USER, sizeof(debug_config_user_t))
    BITFIELD(CONFIG_FIELD_DEBUG_USER_FIRMATA,   0, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_USER_STATS,     1, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_USER_USER_ANIM, 2, 1, 8);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    CONFIG_LAYOUT(CONFIG_ID_RGB_MATRIX, sizeof(rgb_config_t));
    BITFIELD(CONFIG_FIELD_RGB_ENABLE,   0, 2, 8);
    BITFIELD(CONFIG_FIELD_RGB_MODE,     2, 6, 8);
    BYTEFIELD(CONFIG_FIELD_RGB_HSV_H,   offsetof(rgb_config_t, hsv) + offsetof(HSV, h));
    BYTEFIELD(CONFIG_FIELD_RGB_HSV_S,   offsetof(rgb_config_t, hsv) + offsetof(HSV, s));
    BYTEFIELD(CONFIG_FIELD_RGB_HSV_V,   offsetof(rgb_config_t, hsv) + offsetof(HSV, v));
    BYTEFIELD(CONFIG_FIELD_RGB_SPEED,   offsetof(rgb_config_t, speed));
    BYTEFIELD(CONFIG_FIELD_RGB_FLAGS,   offsetof(rgb_config_t, flags));
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    int bp = 0;
    CONFIG_LAYOUT(CONFIG_ID_KEYMAP, sizeof(keymap_config_t));
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_CONTROL_CAPSLOCK,     bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_CAPSLOCK_TO_CONTROL,       bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_LALT_LGUI,            bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_RALT_RGUI,            bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_NO_GUI,                    bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_GRAVE_ESC,            bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_BACKSLASH_BACKSPACE,  bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_NKRO,                      bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_LCTL_LGUI,            bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_RCTL_RGUI,            bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_ONESHOT_ENABLE,            bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_SWAP_ESCAPE_CAPSLOCK,      bp, 1, 16); bp++;
    BITFIELD(CONFIG_FIELD_KEYMAP_AUTOCORRECT_ENABLE,        bp, 1, 16); bp++;
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
}

//------------------------------------------------------------------------------
static int dynld_env_printf(const char* fmt, ...) {
/*
    if (debug_config.enable) {
        xprintf(...);
    }
*/
    return -1;
}

static dynld_test_env_t s_dynld_test_env = {
    .printf = dynld_env_printf
};

_FRMT_HANDLE_CMD_SET(dynld_function) {
    uint16_t fun_id = buf[0] | buf[1] << 8;
    uint16_t offset = buf[2] | buf[3] << 8;
    len -= 4;
    DBG_USR(firmata, "[FA]dynld", " id=%d,off=%d,len=%d\n", (int)fun_id, (int)offset, (int)len);
    load_function(fun_id, &buf[4], offset, len);
}

_FRMT_HANDLE_CMD_SET(dynld_funexec) {
    uint16_t fun_id = buf[0] | buf[1] << 8;
    len -= 2;
    DBG_USR(firmata, "[FA]dynld", " exec id=%d\n", (int)fun_id);

    if (fun_id == DYNLD_FUN_ID_TEST) {
        funptr_test_t fun_test = (funptr_test_t)g_dynld_funcs.func[DYNLD_FUN_ID_TEST];
        int ret = fun_test(&s_dynld_test_env); (void) ret;

        if (debug_config_user.firmata) {
            DBG_USR(firmata, "[FA]dynld", " exec ret=%d\n", ret);
            dprintf_buf(s_dynld_test_env.buf, 32);
        }
    }
}
