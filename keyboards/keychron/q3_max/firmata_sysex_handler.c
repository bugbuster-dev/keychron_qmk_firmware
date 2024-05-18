#include <stdio.h>
#include "action_layer.h"
#include "matrix.h"
#include "rgb_matrix.h"
#include "keycode_config.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "dip_switch.h"
#include "battery.h"

#include "debug.h"
#include "debug_user.h"

#include "firmata/Firmata_QMK.h"
#include "dynld_func.h"

//------------------------------------------------------------------------------
// adjusting the function pointer for thumb mode
uint32_t thumb_fun_addr(void* funptr) {
    uint32_t fun_addr = (uint32_t)funptr;
#ifdef THUMB_PRESENT
    fun_addr |= 1;
#endif
    return fun_addr;
}

static int get_bit_order(void) {
    static int _bit_order = -1;
    if (_bit_order < 0) {
        debug_config_t dc = { .raw = 0 };
        dc.enable = 1;
        if (dc.raw == 1) _bit_order = 0; // lsb first
        else _bit_order = 1;
    }
    return _bit_order;
}

enum struct_field_type {
    STRUCT_FIELD_TYPE_BIT = 1,
    STRUCT_FIELD_TYPE_UINT8,
    STRUCT_FIELD_TYPE_UINT16,
    STRUCT_FIELD_TYPE_UINT32,
    STRUCT_FIELD_TYPE_UINT64,
    STRUCT_FIELD_TYPE_FLOAT,
    STRUCT_FIELD_TYPE_ARRAY = 0x80,
};

enum struct_flags {
    STRUCT_FLAG_READ_ONLY   = 0x1
};

#define LSB_FIRST (get_bit_order() == 0)
#define BITPOS(b,ws) LSB_FIRST? b : ws-1-b
#define STRUCT_LAYOUT(lid, sid, size, flags) \
        resp[0] = FRMT_ID_STRUCT_LAYOUT; \
        resp[1] = lid; \
        resp[2] = sid; \
        resp[3] = size; \
        resp[4] = flags; \
        n = 5;
#define BITFIELD(id,index,nbits,size) \
        resp[n] = id; \
        resp[n+1] = STRUCT_FIELD_TYPE_BIT; \
        resp[n+2] = BITPOS(index, size); \
        resp[n+3] = nbits; n += 4;
#define BYTEFIELD(id,offset) \
        resp[n] = id; \
        resp[n+1] = STRUCT_FIELD_TYPE_UINT8; \
        resp[n+2] = offset; \
        resp[n+3] = 1; n += 4;
#define U16FIELD(id,offset) \
        resp[n] = id; \
        resp[n+1] = STRUCT_FIELD_TYPE_UINT16; \
        resp[n+2] = offset; \
        resp[n+3] = 2; n += 4;
#define U32FIELD(id,offset) \
        resp[n] = id; \
        resp[n+1] = STRUCT_FIELD_TYPE_UINT32; \
        resp[n+2] = offset; \
        resp[n+3] = 4; n += 4;
#define ARRAYFIELD(id,type,offset,size) \
        resp[n] = id; \
        resp[n+1] = STRUCT_FIELD_TYPE_ARRAY | type; \
        resp[n+2] = offset; \
        resp[n+3] = size; n += 4;

extern void debug_led_on(int led);

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

//------------------------------------------------------------------------------
rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

void firmata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t *buf) {
    int decoded_bytes = 0;
    DBG_USR(firmata, "[FA]","cmd:%d,len:%u\n", cmd, len);
    for (int i = 0; i < len; i++) {
        buf[decoded_bytes] = buf[i++];
        buf[decoded_bytes] |= (uint8_t)(buf[i] << 7);
        decoded_bytes++;
    }
    len = decoded_bytes;
    if (len == 0) return;

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
        if (id == FRMT_ID_STATUS)           _FRMT_HANDLE_CMD_GET_FN(status)(cmd, len, buf);
        if (id == FRMT_ID_STRUCT_LAYOUT)    _FRMT_HANDLE_CMD_GET_FN(struct_layout)(cmd, len, buf);
        if (id == FRMT_ID_CONFIG)           _FRMT_HANDLE_CMD_GET_FN(config)(cmd, len, buf);
    }
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(rgb_matrix_buf) {
    for (int i = 0; i < len;) {
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

enum cli_cmd {
    CLI_CMD_MEMORY      = 0x01,
    CLI_CMD_EEPROM      = 0x02,
    CLI_CMD_CALL        = 0x03,
    CLI_CMD_EXEC        = 0x04, // todo bb: execute code from buffer
    CLI_CMD_MASK        = 0x3f,
    CLI_CMD_LAYOUT      = 0x40, // todo bb: memory/eeprom/flash layout info
    CLI_CMD_WRITE       = 0x80,
};

enum eeprom_layout_id {
    EEPROM_LAYOUT_DYNAMIC_KEYMAP = 1,
    EEPROM_LAYOUT_EECONFIG_USER,
};

#ifdef DEVEL_BUILD
static void _return_cli_error(uint8_t cli_seq, uint8_t err) {
    uint8_t resp[3];
    resp[0] = FRMT_ID_CLI;
    resp[1] = cli_seq;
    resp[2] = err; // todo bb: error codes
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, sizeof(resp));
}
#endif

_FRMT_HANDLE_CMD_SET(cli) {
#ifdef DEVEL_BUILD
#define MAX_READ_LEN 64
    int off = 0;
    uint8_t cli_seq = buf[off]; off++;
    uint8_t cli_cmd = buf[off]; off++;
    uint8_t wr = cli_cmd & CLI_CMD_WRITE;
    uint8_t lo = cli_cmd & CLI_CMD_LAYOUT;
    cli_cmd &= CLI_CMD_MASK;

    DBG_USR(firmata, "[FA]","cli[%d]:%u, len=%u\n", cli_seq, cli_cmd, len);
    if (cli_cmd == CLI_CMD_MEMORY) { // memory read/write
        uint8_t len = 0;
        uint32_t addr = 0;
        uint32_t val = 0;

        if (lo) {
            dprintf("unsupported\n");
            _return_cli_error(cli_seq, 'u');
            return;
        }
        memcpy(&addr, &buf[off], sizeof(addr)); off += sizeof(addr);
        memcpy(&len, &buf[off], sizeof(len)); off += sizeof(len);
        if (!wr) {
            if (len > MAX_READ_LEN) {
                dprintf("len too large\n");
                _return_cli_error(cli_seq, 'i');
                return;
            }
            uint8_t* ptr = (uint8_t*)addr;
            dprintf("m[0x%lx:%d]=", addr, len);
            if (len == 1) dprintf("%02x\n", *ptr);
            else if (len == 2) dprintf("%04x\n", *(uint16_t*)ptr);
            else if (len == 4) dprintf("%08lx\n", *(uint32_t*)ptr);
            else {
                if (len > 16) {
                    dprintf("\n");
                }
                dprintf_buf(ptr, len);
            }
            {
                uint8_t resp_len = 2+len;
                uint8_t resp[resp_len];
                resp[0] = FRMT_ID_CLI;
                resp[1] = cli_seq;
                memcpy(&resp[2], ptr, len);
                firmata_send_sysex(FRMT_CMD_RESPONSE, resp, resp_len);
            }
        } else {
            memcpy(&val, &buf[off], sizeof(val)); off += sizeof(val);
            switch (len) {
                case 1: {
                    volatile uint8_t* ptr = (volatile uint8_t*)addr; *ptr = val;
                    dprintf("%02x\n", *ptr);
                    break;
                }
                case 2: {
                    volatile uint16_t* ptr = (volatile uint16_t*)addr; *ptr = val;
                    dprintf("%04x\n", *ptr);
                    break;
                }
                case 4: {
                    volatile uint32_t* ptr = (volatile uint32_t*)addr; *ptr = val;
                    dprintf("%08lx\n", *ptr);
                    break;
                }
                default:
                    dprintf("invalid size\n");
                    _return_cli_error(cli_seq, 'i');
                    break;
            }
        }
    }
    if (cli_cmd == CLI_CMD_EEPROM) { // eeprom (flash) read/write
        if (lo) {
            struct {
                uint32_t addr;
                uint32_t size;
            } eeprom_layout[] = {
                {(uint32_t)dynamic_keymap_key_to_eeprom_address(0,0,0), DYNAMIC_KEYMAP_LAYER_COUNT*MATRIX_ROWS*MATRIX_COLS*2},
                {(uint32_t)EECONFIG_USER, EECONFIG_USER_DATA_SIZE},
            }; (void) eeprom_layout;

            uint8_t resp_len = 32;
            uint8_t resp[resp_len];
            int off = 0;
            resp[off] = FRMT_ID_CLI; off++;
            resp[off] = cli_seq; off++;
            for (int i = 0; i < sizeof(eeprom_layout)/sizeof(eeprom_layout[0]); i++) {
                dprintf("eeprom[%d]:0x%lx:%ld\n", i, eeprom_layout[i].addr, eeprom_layout[i].size);
                resp[off] = i+1; off++;
                memcpy(&resp[off], &eeprom_layout[i].addr, sizeof(eeprom_layout[i].addr)); off += sizeof(eeprom_layout[i].addr);
                memcpy(&resp[off], &eeprom_layout[i].size, sizeof(eeprom_layout[i].size)); off += sizeof(eeprom_layout[i].size);
            }
            firmata_send_sysex(FRMT_CMD_RESPONSE, resp, off);
            return;
        }
        uint8_t len = 0;
        uint32_t addr = 0;
        uint32_t val = 0;

        memcpy(&addr, &buf[off], sizeof(addr)); off += sizeof(addr);
        memcpy(&len, &buf[off], sizeof(len)); off += sizeof(len);
        if (!wr) {
            dprintf("e[0x%lx:%d]=", addr, len);
            if (len > MAX_READ_LEN) {
                dprintf("len too large\n");
                _return_cli_error(cli_seq, 'i');
                return;
            }
            uint8_t resp_len = 2+len;
            uint8_t resp[resp_len];
            resp[0] = FRMT_ID_CLI;
            resp[1] = cli_seq;
            bool read = false;
            switch (len) {
                case 1: {
                    uint8_t val = eeprom_read_byte((const uint8_t*)addr); read = true;
                    memcpy(&resp[2], &val, len);
                    dprintf("%02x\n", val);
                    break;
                }
                case 2: {
                    uint16_t val = eeprom_read_word((const uint16_t*)addr); read = true;
                    memcpy(&resp[2], &val, len);
                    dprintf("%04x\n", val);
                    break;
                }
                case 4: {
                    uint32_t val = eeprom_read_dword((const uint32_t*)addr); read = true;
                    memcpy(&resp[2], &val, len);
                    dprintf("%08lx\n", val);
                    break;
                }
                default: break;
            }
            if (!read && len > 0) {
                int i = 0;
                while (len-- > 0) {
                    uint8_t val = eeprom_read_byte((const uint8_t*)addr+i);
                    resp[2+i] = val; i++;
                }
            }
            firmata_send_sysex(FRMT_CMD_RESPONSE, resp, resp_len);
        } else {
            memcpy(&val, &buf[off], sizeof(val)); off += sizeof(val);

            switch (len) {
                case 1: {
                    eeprom_update_byte((uint8_t*)addr, val);
                    val = eeprom_read_byte((const uint8_t*)addr);
                    dprintf("%02x\n", (uint8_t)val);
                    break;
                }
                case 2: {
                    eeprom_update_word((uint16_t*)addr, val);
                    val = eeprom_read_word((const uint16_t*)addr);
                    dprintf("%04x\n", (uint16_t)val);
                    break;
                }
                case 4: {
                    eeprom_update_dword((uint32_t*)addr, val);
                    val = eeprom_read_dword((const uint32_t*)addr);
                    dprintf("%08lx\n", val);
                    break;
                }
                default:
                    break;
            }
        }
    }
    if (cli_cmd == CLI_CMD_CALL) { // call function
        uint32_t fun_addr = 0;
        // todo bb: args and return value
        memcpy(&fun_addr, &buf[off], sizeof(fun_addr)); off += sizeof(fun_addr);
        if (fun_addr) {
            void (*fun)(int) = (void (*)(int))thumb_fun_addr((void*)fun_addr);
            dprintf("call:0x%lx (0x%lx)\n", (uint32_t)fun, (uint32_t)fun_addr);
            fun(-1);
        } else {
            debug_led_on(0);
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
enum status_id {
    STATUS_ID_BATTERY = 1,
    STATUS_ID_DIP_SWITCH,
    STATUS_ID_MATRIX,
    STATUS_ID_MAX
};

struct battery_status {
    uint8_t level;
    uint16_t voltage;
    uint8_t charging;
} g_status_battery = { 0, 0, 0};

extern matrix_row_t raw_matrix[MATRIX_ROWS];
#define NUMBER_OF_DIP_SWITCHES 1
extern bool dip_switch_state[];

static const struct {
    uint8_t* ptr;
    uint8_t size;
} s_status_table[STATUS_ID_MAX] = {
    [STATUS_ID_BATTERY] = { (uint8_t*)&g_status_battery, sizeof(g_status_battery) },
    [STATUS_ID_DIP_SWITCH] = { (uint8_t*)dip_switch_state, NUMBER_OF_DIP_SWITCHES },
    [STATUS_ID_MATRIX] = { (uint8_t*)raw_matrix, sizeof(raw_matrix)},
};

static void _frmt_send_struct_layout_status(void) {
    uint8_t resp[60];
    int n = 0;
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_STATUS, STATUS_ID_BATTERY, sizeof(struct battery_status), STRUCT_FLAG_READ_ONLY)
    BYTEFIELD(1, offsetof(struct battery_status, level));
    U16FIELD(2, offsetof(struct battery_status, voltage));
    BYTEFIELD(3, offsetof(struct battery_status, charging));
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_STATUS, STATUS_ID_DIP_SWITCH, NUMBER_OF_DIP_SWITCHES, STRUCT_FLAG_READ_ONLY)
    BYTEFIELD(1, 0);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_STATUS, STATUS_ID_MATRIX, sizeof(raw_matrix), STRUCT_FLAG_READ_ONLY)
    uint8_t matrix_row_type = STRUCT_FIELD_TYPE_UINT8;
    if (sizeof(matrix_row_t) == 2) matrix_row_type = STRUCT_FIELD_TYPE_UINT16;
    if (sizeof(matrix_row_t) == 4) matrix_row_type = STRUCT_FIELD_TYPE_UINT32;
    ARRAYFIELD(1, matrix_row_type, 0, sizeof(raw_matrix)/sizeof(matrix_row_t));
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
}

_FRMT_HANDLE_CMD_GET(status) {
    uint8_t status_id = buf[0];
    DBG_USR(firmata, "[FA]","status:get:%u\n", status_id);
    if (status_id == 0) return;
    if (status_id >= STATUS_ID_MAX) return;
    if (s_status_table[status_id].ptr == NULL) return;

    if (status_id == STATUS_ID_BATTERY) {
        g_status_battery.level = battery_get_percentage();
        g_status_battery.voltage = battery_get_voltage();
        g_status_battery.charging = 1; // firmata only over usb so charging or full
    }

    uint8_t resp[2+s_status_table[status_id].size];
    resp[0] = FRMT_ID_STATUS;
    resp[1] = status_id;
    DBG_USR(firmata, "[FA]","status[%d]:%lx\n", status_id, (uint32_t)s_status_table[status_id].ptr);
    memcpy(&resp[2], s_status_table[status_id].ptr, s_status_table[status_id].size);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, 2+s_status_table[status_id].size);
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
extern uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS]; // todo bb: replace with "eeprom cache"

static const struct {
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
// - mechanism to hook enable/disable dynamic loaded functions
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

//<config id>:<size>:<field id>:<type>:<offset>:<size> // offset: byte or bit offset
//_FRMT_HANDLE_CMD_GET(config_layout) {
static void _frmt_send_struct_layout_config(void) {
    uint8_t resp[60];
    int n = 0;
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_DEBUG, sizeof(debug_config_t), 0)
    BITFIELD(CONFIG_FIELD_DEBUG_ENABLE,     0, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_MATRIX,     1, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_KEYBOARD,   2, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_MOUSE,      3, 1, 8);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_DEBUG_USER, sizeof(debug_config_user_t), 0 )
    BITFIELD(CONFIG_FIELD_DEBUG_USER_FIRMATA,   0, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_USER_STATS,     1, 1, 8);
    BITFIELD(CONFIG_FIELD_DEBUG_USER_USER_ANIM, 2, 1, 8);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_RGB_MATRIX, sizeof(rgb_config_t), 0);
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
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_KEYMAP, sizeof(keymap_config_t), 0);
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
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_KEYMAP_LAYOUT, keymap_size, STRUCT_FLAG_READ_ONLY);
    ARRAYFIELD(CONFIG_FIELD_KEYMAP_LAYOUT, STRUCT_FIELD_TYPE_UINT16, 0, MATRIX_ROWS*MATRIX_COLS);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_DEBOUNCE, sizeof(uint8_t), 0);
    BYTEFIELD(CONFIG_FIELD_DEBOUNCE, 0);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
    //--------------------------------
    STRUCT_LAYOUT(FRMT_ID_CONFIG, CONFIG_ID_DEVEL, sizeof(devel_config_t), 0);
    BITFIELD(CONFIG_FIELD_DEVEL_PUB_KEYPRESS,       0, 1, 8);
    BITFIELD(CONFIG_FIELD_DEVEL_PROCESS_KEYPRESS,   1, 1, 8);
    firmata_send_sysex(FRMT_CMD_RESPONSE, resp, n);
}

//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------

_FRMT_HANDLE_CMD_GET(struct_layout) {
    uint8_t struct_layout_id = buf[0];
    DBG_USR(firmata, "[FA]","struct_layout:get:%u\n", struct_layout_id);

    if (struct_layout_id == FRMT_ID_STATUS) {
        _frmt_send_struct_layout_status();
        return;
    }
    if (struct_layout_id == FRMT_ID_CONTROL) {
        //todo bb:
        //_frmt_send_struct_layout_control();
        return;
    }
    if (struct_layout_id == FRMT_ID_CONFIG) {
        _frmt_send_struct_layout_config();
        return;
    }
}
