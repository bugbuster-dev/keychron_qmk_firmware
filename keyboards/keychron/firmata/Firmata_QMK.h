
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMATA_QMK_MAJOR_VERSION   0
#define FIRMATA_QMK_MINOR_VERSION   1

void firmata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t *buf);

#define _PROCESS_CMD_SET(name)       void _process_cmd_set_##name     (uint8_t cmd, uint8_t len, uint8_t *buf)
#define _PROCESS_CMD_GET(name)       void _process_cmd_get_##name     (uint8_t cmd, uint8_t len, uint8_t *buf)
#define _PROCESS_CMD_SETGET(name)    _PROCESS_CMD_SET(name); _PROCESS_CMD_GET(name)

_PROCESS_CMD_SETGET(default_layer);
_PROCESS_CMD_SETGET(debug_mask);
_PROCESS_CMD_SETGET(macwin_mode);
_PROCESS_CMD_SETGET(battery_status);
_PROCESS_CMD_SETGET(rgb_matrix_buf);
_PROCESS_CMD_SETGET(rgb_matrix_mode);
_PROCESS_CMD_SETGET(dynld_function);
_PROCESS_CMD_SETGET(dynld_funexec);


enum {
    FRMT_CMD_EXTENDED = 0,
    FRMT_CMD_SET = 1,
    FRMT_CMD_GET = 2,
    FRMT_CMD_ADD = 3,
    FRMT_CMD_DEL = 4,
    FRMT_CMD_PUB = 5,
    FRMT_CMD_SUB = 6,
    FRMT_CMD_RESPONSE = 0x0f,
};

enum {
    FRMT_ID_RGB_MATRIX_BUF  = 1,
    FRMT_ID_DEFAULT_LAYER   = 2,
    FRMT_ID_DEBUG_MASK      = 3,
    FRMT_ID_BATTERY_STATUS  = 4,
    FRMT_ID_MACWIN_MODE     = 5,
    FRMT_ID_RGB_MATRIX_MODE = 6,
    FRMT_ID_DYNLD_FUNCTION  = 7, // dynamic load function into ram
    FRMT_ID_DYNLD_FUNEXEC   = 8, // exec "dynamic loaded function"
};

// rgb matrix buffer set from host
typedef struct rgb_matrix_host_buffer_t {
    struct {
        uint8_t duration;
        uint8_t r; // todo bb: store 4 bits, value diff around 15 not distinguishable
        uint8_t g;
        uint8_t b;
    } led[RGB_MATRIX_LED_COUNT];

    bool written;
} rgb_matrix_host_buffer_t;


enum DYNLD_FUNC_ID {
    DYNLD_FUN_ID_ANIMATION = 0,

    DYNLD_FUN_ID_TEST = 1,
    DYNLD_FUN_ID_MAX = 2
};

typedef struct dynld_funcs {
    void* func[DYNLD_FUN_ID_MAX];
} dynld_funcs_t;

#define RAWHID_FIRMATA_MSG  0xFA

//------------------------------------------------------------------------------

typedef void (*sysexCallbackFunction)(uint8_t command, uint8_t argc, uint8_t *argv);

void firmata_initialize(const char* firmware);
void firmata_start(void);

void firmata_attach(uint8_t cmd, sysexCallbackFunction newFunction);

int firmata_recv(uint8_t c);
int firmata_recv_data(uint8_t *data, uint8_t len);

void firmata_send_sysex(uint8_t cmd, uint8_t* data, int len);

void firmata_process(void);

#ifdef __cplusplus
}
#endif

