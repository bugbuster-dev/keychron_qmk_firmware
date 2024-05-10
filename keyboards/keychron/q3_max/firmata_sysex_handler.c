#include <stdio.h>
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
        if (id == FRMT_ID_CLI)              _FRMT_HANDLE_CMD_SET_FN(cli)(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_BUF)   _FRMT_HANDLE_CMD_SET_FN(rgb_matrix_buf)(cmd, len, buf);
        if (id == FRMT_ID_DEFAULT_LAYER)    _FRMT_HANDLE_CMD_SET_FN(default_layer)(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _FRMT_HANDLE_CMD_SET_FN(macwin_mode)(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNCTION)   _FRMT_HANDLE_CMD_SET_FN(dynld_function)(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNEXEC)    _FRMT_HANDLE_CMD_SET_FN(dynld_funexec)(cmd, len, buf);
        if (id == FRMT_ID_CONFIG)           _FRMT_HANDLE_CMD_SET_FN(config)(cmd, len, buf);
    }
    if (cmd == FRMT_CMD_GET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_DEFAULT_LAYER)    _FRMT_HANDLE_CMD_GET_FN(default_layer)(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _FRMT_HANDLE_CMD_GET_FN(macwin_mode)(cmd, len, buf);
        if (id == FRMT_ID_BATTERY_STATUS)   _FRMT_HANDLE_CMD_GET_FN(battery_status)(cmd, len, buf);
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
_FRMT_HANDLE_CMD_SET(cli) {
#ifdef DEVEL_BUILD
    DBG_USR(firmata, "[FA]","cli:%s\n", &buf[0]);
    if (buf[0] == 'm') {
        if (buf[1] == 'r') {
            int len = 0;
            uint32_t addr = 0;
            int res = sscanf((char*)&buf[2], "%"SCNx32 " %d", &addr, &len);
            if (res == 2) {
                uint8_t* ptr = (uint8_t*)addr;
                dprintf("[%lx:%d]=", addr, len);
                switch (len) {
                    case 1: dprintf("%02x\n", *ptr); return;
                    case 2: dprintf("%04x\n", *(uint16_t*)ptr); return;
                    case 4: dprintf("%08lx\n", *(uint32_t*)ptr); return;
                    default: break;
                }
                if (len > 16) {
                    dprintf("\n");
                }
                dprintf_buf(ptr, len);
            }
        }
        if (buf[1] == 'w') {
            int len = 0;
            uint32_t addr = 0;
            uint32_t val = 0;
            int res = sscanf((char*)&buf[2], "%"SCNx32 " %d %lx", &addr, &len, &val);
            if (res == 3) {
                switch (len) {
                    case 1: {
                        uint8_t* ptr = (uint8_t*)addr;
                        *ptr = val;
                        dprintf("%02x\n", *ptr);
                        break;
                    }
                    case 2: {
                        uint16_t* ptr = (uint16_t*)addr;
                        *ptr = val;
                        dprintf("%04x\n", *ptr);
                        break;
                    }
                    case 4: {
                        uint32_t* ptr = (uint32_t*)addr;
                        *ptr = val;
                        dprintf("%08lx\n", *ptr);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }
#endif
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
enum config_id {
    CONFIG_ID_DEBUG = 1,
    CONFIG_ID_DEBUG_USER,
    CONFIG_ID_RGB_MATRIX,
    CONFIG_ID_KEYMAP,
    CONFIG_ID_KEYMAP_LAYOUT, // only layer 0
    //CONFIG_ID_BACKLIGHT //backlight_config_t
    //CONFIG_ID_AUDIO //audio_config_t
    //CONFIG_ID_USER, // user_config_t
    //CONFIG_ID_KEYCHRON_INDICATOR // indicator_config_t
    CONFIG_ID_DEBOUNCE, // uint8_t
    CONFIG_ID_DEVEL, // devel_config_t
    CONFIG_ID_MAX
};

extern uint8_t g_debounce;
extern uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS];
//const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS]

static struct {
    uint8_t* ptr;
    uint8_t size;
} s_config_table[CONFIG_ID_MAX] = {
    [CONFIG_ID_DEBUG] =         { (uint8_t*)&debug_config,      sizeof(debug_config) },
    [CONFIG_ID_DEBUG_USER] =    { (uint8_t*)&debug_config_user, sizeof(debug_config_user) },
    [CONFIG_ID_RGB_MATRIX] =    { (uint8_t*)&rgb_matrix_config, sizeof(rgb_matrix_config) },
    [CONFIG_ID_KEYMAP] =        { (uint8_t*)&keymap_config,     sizeof(keymap_config) },
    [CONFIG_ID_KEYMAP_LAYOUT] = { (uint8_t*)keymaps,            sizeof(keymaps[0][0][0])*MATRIX_ROWS*MATRIX_COLS },
    [CONFIG_ID_DEBOUNCE] =      { (uint8_t*)&g_debounce,        sizeof(g_debounce) },
    [CONFIG_ID_DEVEL] =         { (uint8_t*)&devel_config,      sizeof(devel_config) },
};

_FRMT_HANDLE_CMD_SET(config) {
    uint8_t config_id = buf[0];
    DBG_USR(firmata, "[FA]","config:set:%u\n", config_id);
    if (config_id == 0) return; // no extended config id
    if (config_id >= CONFIG_ID_MAX) return;
    if (s_config_table[config_id].ptr == NULL) return;
    memcpy(s_config_table[config_id].ptr, &buf[1], s_config_table[config_id].size);
}

_FRMT_HANDLE_CMD_GET(config) {
    uint8_t config_id = buf[0];
    DBG_USR(firmata, "[FA]","config:get:%u\n", config_id);
    if (config_id == 0) return; // no extended config id
    if (config_id >= CONFIG_ID_MAX) return;
    if (s_config_table[config_id].ptr == NULL) return;

    uint8_t resp[2+s_config_table[config_id].size];
    resp[0] = FRMT_ID_CONFIG;
    resp[1] = config_id;
    DBG_USR(firmata, "[FA]","config[%d]:%lx\n", config_id, (uint32_t)s_config_table[config_id].ptr);
    memcpy(&resp[2], s_config_table[config_id].ptr, s_config_table[config_id].size);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, 2+s_config_table[config_id].size);
}

//------------------------------------------------------------------------------
// todo bb:
// - config changes monitoring
// - generic state machine to handle commands which needs multiple "task steps"
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

enum config_keymap_layout_field {
    CONFIG_FIELD_KEYMAP_LAYOUT = 1
};

enum config_debounce_field {
    CONFIG_FIELD_DEBOUNCE = 1
};

enum config_devel_field {
    CONFIG_FIELD_DEVEL_PUB_KEYPRESS = 1,
    CONFIG_FIELD_DEVEL_PROCESS_KEYPRESS,
};

enum config_field_type {
    CONFIG_FIELD_TYPE_BIT = 1,
    CONFIG_FIELD_TYPE_UINT8,
    CONFIG_FIELD_TYPE_UINT16,
    CONFIG_FIELD_TYPE_UINT32,
    CONFIG_FIELD_TYPE_UINT64,
    CONFIG_FIELD_TYPE_FLOAT,
    CONFIG_FIELD_TYPE_ARRAY = 0x80,
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
    #define ARRAYFIELD(id,type,offset,size) \
            resp[n] = id; \
            resp[n+1] = CONFIG_FIELD_TYPE_ARRAY | type; \
            resp[n+2] = offset; \
            resp[n+3] = size; n += 4;

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
    //--------------------------------
    int keymap_size = sizeof(keymaps[0][0][0])*MATRIX_ROWS*MATRIX_COLS; // only layer 0
    CONFIG_LAYOUT(CONFIG_ID_KEYMAP_LAYOUT, keymap_size);
    ARRAYFIELD(CONFIG_FIELD_KEYMAP_LAYOUT, CONFIG_FIELD_TYPE_UINT16, 0, MATRIX_ROWS*MATRIX_COLS);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    CONFIG_LAYOUT(CONFIG_ID_DEBOUNCE, sizeof(uint8_t));
    BYTEFIELD(CONFIG_FIELD_DEBOUNCE, 0);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    CONFIG_LAYOUT(CONFIG_ID_DEVEL, sizeof(devel_config_t));
    BITFIELD(CONFIG_FIELD_DEVEL_PUB_KEYPRESS,       0, 1, 8);
    BITFIELD(CONFIG_FIELD_DEVEL_PROCESS_KEYPRESS,   1, 1, 8);
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
