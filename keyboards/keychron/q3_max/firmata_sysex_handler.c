#include "action_layer.h"

#include "firmata/Firmata_QMK.h"

#include "rgb_matrix.h"
#include "dynld_func.h"

#include "debug.h"

//------------------------------------------------------------------------------

static void dprintf_buf(uint8_t *buf, uint8_t len) {
    for (int i = 0; i < len; i++) {
        dprintf("%02x ", buf[i]);
        if (((i+1)%16) == 0) dprintf("\n");
    }
    dprintf("\n");
}


// Function to check if currently executing in Thumb mode
int in_thumb_mode(void) {
    uint32_t xpsr;
    asm("MRS %0, xpsr" : "=r" (xpsr) ); // Read xPSR into xpsr variable
    return (xpsr & (1 << 24)) != 0; // Check T bit (bit 24)
}

// adjusting the function pointer for thumb mode
uint32_t thumb_fun_addr(void* fun) {
    return (((uint32_t)fun) | 1);
}


#define DYNLD_FUNC_SIZE 1024 // dynld function max size
volatile uint8_t dynld_func_buf[DYNLD_FUN_ID_MAX][DYNLD_FUNC_SIZE] __attribute__((aligned(4)));

dynld_funcs_t g_dynld_funcs = { 0 };

void load_function(const uint16_t fun_id, const uint8_t* data, size_t offset, size_t size) {
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
    //todo bb: set after fully loaded
    g_dynld_funcs.func[fun_id] = (void*)thumb_fun_addr((uint8_t*)dynld_func_buf[fun_id]); //(void*)dynld_func_buf[fun_id];
    //dprintf_buf((uint8_t*)&dynld_func_buf[fun_id][offset], 16);
}

//------------------------------------------------------------------------------

rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

#ifdef VIRTSER_FIRMATA
void virtser_recv(uint8_t c)
{
    if (firmata_recv(c) < 0) {
        dprintf("[E]firmata_recv\n");
    }
}
#endif


void firmata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t *buf)
{
    if (cmd == FRMT_CMD_SET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_RGB_MATRIX_BUF)   _process_cmd_set_rgb_matrix_buf(cmd, len, buf);
        if (id == FRMT_ID_DEFAULT_LAYER)    _process_cmd_set_default_layer(cmd, len, buf);
        if (id == FRMT_ID_DEBUG_MASK)       _process_cmd_set_debug_mask(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _process_cmd_set_macwin_mode(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_MODE)  _process_cmd_set_rgb_matrix_mode(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNCTION)   _process_cmd_set_dynld_function(cmd, len, buf);
        if (id == FRMT_ID_DYNLD_FUNEXEC)    _process_cmd_set_dynld_funexec(cmd, len, buf);
    }
    if (cmd == FRMT_CMD_GET) {
        uint8_t id = buf[0];
        buf++; len--;
        if (id == FRMT_ID_DEFAULT_LAYER)    _process_cmd_get_default_layer(cmd, len, buf);
        if (id == FRMT_ID_DEBUG_MASK)       _process_cmd_get_debug_mask(cmd, len, buf);
        if (id == FRMT_ID_MACWIN_MODE)      _process_cmd_get_macwin_mode(cmd, len, buf);
        if (id == FRMT_ID_BATTERY_STATUS)   _process_cmd_get_battery_status(cmd, len, buf);
        if (id == FRMT_ID_RGB_MATRIX_MODE)  _process_cmd_get_rgb_matrix_mode(cmd, len, buf);
    }
}


void _process_cmd_set_rgb_matrix_buf(uint8_t cmd, uint8_t len, uint8_t *buf)
{
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

void _process_cmd_set_default_layer(uint8_t cmd, uint8_t len, uint8_t *buf)
{
    dprintf("[L]%d\n", buf[0]);
    layer_state_t state = 1 << buf[0];
    default_layer_set(state);
}

void _process_cmd_set_debug_mask(uint8_t cmd, uint8_t len, uint8_t *buf)
{
    debug_config = (debug_config_t) buf[0];
    dprintf("[D]%02x\n", buf[0]);
}

void _process_cmd_set_macwin_mode(uint8_t cmd, uint8_t len, uint8_t *buf)
{
    extern void keyb_user_set_macwin_mode(int mode);
    int mode = buf[0];
    dprintf("macwin:%c\n", buf[0]);
    if (buf[0] == '-') mode = -1;
    keyb_user_set_macwin_mode(mode);
}

void _process_cmd_get_default_layer(uint8_t cmd, uint8_t len, uint8_t *buf)
{
}

void _process_cmd_get_debug_mask(uint8_t cmd, uint8_t len, uint8_t *buf)
{
    uint8_t response[2];
    response[0] = FRMT_ID_DEBUG_MASK;
    response[1] = *((uint8_t*)&debug_config);
    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

void _process_cmd_get_macwin_mode(uint8_t cmd, uint8_t len, uint8_t *buf)
{
    extern int keyb_user_get_macwin_mode(void);
    uint8_t response[2];
    response[0] = FRMT_ID_MACWIN_MODE;
    response[1] = keyb_user_get_macwin_mode();
    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}

void _process_cmd_get_battery_status(uint8_t cmd, uint8_t len, uint8_t *buf)
{
}

_PROCESS_CMD_SET(rgb_matrix_mode)
{
    uint8_t matrix_mode = buf[0];
    dprintf("[RGB]mode=%d\n", matrix_mode);
    if (matrix_mode) {
        rgb_matrix_enable_noeeprom();
        rgb_matrix_mode_noeeprom(matrix_mode);
    } else {
        rgb_matrix_disable_noeeprom();
    }
}

_PROCESS_CMD_GET(rgb_matrix_mode)
{
    uint8_t response[2];
    response[0] = FRMT_ID_RGB_MATRIX_MODE;
    response[1] = rgb_matrix_get_mode();

    firmata_send_sysex(FRMT_CMD_RESPONSE, response, sizeof(response));
}


void rgb_matrix_host_buf_set(int li, uint8_t r, uint8_t g, uint8_t b)
{
    //dprintf("[RGB]host buf=%d rgb=%d,%d,%d\n",li,r,g,b);
    g_rgb_matrix_host_buf.led[li].duration = 255;
    g_rgb_matrix_host_buf.led[li].r = r;
    g_rgb_matrix_host_buf.led[li].g = g;
    g_rgb_matrix_host_buf.led[li].b = b;
    g_rgb_matrix_host_buf.written = 1;
}

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

typedef int (*funptr_printf)(const char* fmt, ...);

static dynld_test_env_t s_dynld_test_env = {
    .printf = dynld_env_printf
};

_PROCESS_CMD_SET(dynld_function) // uint8_t cmd, uint8_t len, uint8_t *buf
{
    uint16_t fun_id = buf[0] | buf[1] << 8;
    uint16_t offset = buf[2] | buf[3] << 8;
    len -= 4;
    dprintf("[DYNLD]func id=%d off=%d\n", fun_id, offset);
    load_function(fun_id, &buf[4], offset, len);
}

_PROCESS_CMD_SET(dynld_funexec)
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
