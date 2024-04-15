#include "action_layer.h"
#include "rgb_matrix.h"
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
    return (((uint32_t)funptr)|1);
}

#define DYNLD_FUNC_SIZE 1024 // dynld function max size
volatile uint8_t dynld_func_buf[DYNLD_FUN_ID_MAX][DYNLD_FUNC_SIZE] __attribute__((aligned(4)));
dynld_funcs_t g_dynld_funcs = { 0 };

void load_function(const uint16_t fun_id, const uint8_t* data, size_t offset, size_t size) {
    // set function pointer after fully loaded
    if (offset == 0xffff) {
        g_dynld_funcs.func[fun_id] = (void*)thumb_fun_addr((uint8_t*)dynld_func_buf[fun_id]);
        //dprintf_buf((uint8_t*)&dynld_func_buf[fun_id][0], 16);
        return;
    }
    if (offset + size > DYNLD_FUNC_SIZE) {
        memset((void*)&dynld_func_buf[fun_id][offset], 0, DYNLD_FUNC_SIZE);
        g_dynld_funcs.func[fun_id] = NULL;
        dprintf("[DYNLD]fun too large!\n");
        return;
    }
    if (fun_id >= DYNLD_FUN_ID_MAX) {
        dprintf("[DYNLD]fun id too large!\n");
        return;
    }
    if (offset == 0) {
        memset((void*)&dynld_func_buf[fun_id][offset], 0, DYNLD_FUNC_SIZE);
        g_dynld_funcs.func[fun_id] = NULL;
        if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0) return;
    }
    memcpy((void*)&dynld_func_buf[fun_id][offset], data, size);
}

//------------------------------------------------------------------------------
rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

void firmata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t *buf) {
    if (cmd == FRMT_CMD_SET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_RGB_MATRIX_BUF)   _frmt_handle_cmd_set_rgb_matrix_buf(cmd, len, buf);
        if (id == FRMT_ID_DEFAULT_LAYER)    _frmt_handle_cmd_set_default_layer(cmd, len, buf);
        if (id == FRMT_ID_DEBUG_MASK)       _frmt_handle_cmd_set_debug_mask(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _frmt_handle_cmd_set_macwin_mode(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_MODE)  _frmt_handle_cmd_set_rgb_matrix_mode(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNCTION)   _frmt_handle_cmd_set_dynld_function(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNEXEC)    _frmt_handle_cmd_set_dynld_funexec(cmd, len, buf);
    }
    if (cmd == FRMT_CMD_GET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_DEFAULT_LAYER)    _frmt_handle_cmd_get_default_layer(cmd, len, buf);
        if (id == FRMT_ID_DEBUG_MASK)       _frmt_handle_cmd_get_debug_mask(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _frmt_handle_cmd_get_macwin_mode(cmd, len, buf);
        if (id == FRMT_ID_BATTERY_STATUS)   _frmt_handle_cmd_get_battery_status(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_MODE)  _frmt_handle_cmd_get_rgb_matrix_mode(cmd, len, buf);
    }
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(rgb_matrix_buf) {
    for (uint16_t i = 0; i < len;) {
        //dprintf("rgb:%d(%d):%d,%d,%d\n", buf[i], buf[i+1], buf[i+2], buf[i+3], buf[i+4]);
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
    dprintf("[L]%d\n", buf[0]);
    layer_state_t state = 1 << buf[0];
    default_layer_set(state);
}

_FRMT_HANDLE_CMD_GET(default_layer) {
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(debug_mask) {
    if (len < 4) {
        debug_config.raw = buf[0];
        dprintf("[DMSK]%02x\n", debug_config.raw);
        return;
    }
    if (len >= 4) {
        //uint32_t dbg_mask = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
        debug_config.raw = buf[3];
    }
    if (len >= 8) {
        const int off = 4;
        uint32_t dbg_user_mask = buf[off] << 24 | buf[off+1] << 16 | buf[off+2] << 8 | buf[off+3];
        debug_config_user.raw = dbg_user_mask;
        dprintf("[DMSK]%02x:%08lx\n", debug_config.raw, debug_config_user.raw);
    }
}

_FRMT_HANDLE_CMD_GET(debug_mask) {
    uint8_t response[9];
    response[0] = FRMT_ID_DEBUG_MASK;
    response[1] = response[2] = response[3] = 0;
    response[4] = debug_config.raw;
    response[5] = (debug_config_user.raw >> 24) & 0xff;
    response[6] = (debug_config_user.raw >> 16) & 0xff;
    response[7] = (debug_config_user.raw >> 8) & 0xff;
    response[8] = (debug_config_user.raw) & 0xff;
    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_SET(macwin_mode) {
    extern void keyb_user_set_macwin_mode(int mode);
    int mode = buf[0];
    dprintf("macwin:%c\n", buf[0]);
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
    dprintf("[RGB]mode=%d\n", matrix_mode);
    if (matrix_mode) {
        rgb_matrix_enable_noeeprom();
        rgb_matrix_mode_noeeprom(matrix_mode);
    } else {
        rgb_matrix_disable_noeeprom();
    }
}

//------------------------------------------------------------------------------
_FRMT_HANDLE_CMD_GET(rgb_matrix_mode) {
    uint8_t response[2];
    response[0] = FRMT_ID_RGB_MATRIX_MODE;
    response[1] = rgb_matrix_get_mode();

    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
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

typedef int (*funptr_printf)(const char* fmt, ...);

static dynld_test_env_t s_dynld_test_env = {
    .printf = dynld_env_printf
};

_FRMT_HANDLE_CMD_SET(dynld_function) // uint8_t cmd, uint8_t len, uint8_t *buf
{
    uint16_t fun_id = buf[0] | buf[1] << 8;
    uint16_t offset = buf[2] | buf[3] << 8;
    len -= 4;
    dprintf("[DYNLD]func id=%d off=%d, len=%d\n", fun_id, offset, len);
    load_function(fun_id, &buf[4], offset, len);
}

_FRMT_HANDLE_CMD_SET(dynld_funexec)
{
    uint16_t fun_id = buf[0] | buf[1] << 8;
    len -= 2;
    dprintf("[DYNLD]funexec id=%d\n", fun_id);

    if (fun_id == DYNLD_FUN_ID_TEST) {
        funptr_test_t fun_test = (funptr_test_t)g_dynld_funcs.func[DYNLD_FUN_ID_TEST];
        int ret = fun_test(&s_dynld_test_env); (void) ret;

        dprintf("[DYNLD]fun ret=%d\n", ret);
        dprintf_buf(s_dynld_test_env.buf, 32);
    }
}
