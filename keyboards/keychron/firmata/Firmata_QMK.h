// todo bb: add license
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMATA_QMK_MAJOR_VERSION   0
#define FIRMATA_QMK_MINOR_VERSION   4

// set/get/add/del pub/sub commands should cover all the scenarios for now
// extension by adding more "ids" to set/get/...
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

// todo bb: group by config/status/control/...
enum {
    FRMT_ID_EXTENDED        = 0,
    FRMT_ID_CONTROL         = 0xff, // todo bb:
    FRMT_ID_RGB_MATRIX_BUF  = 1,    // todo bb: move to CONTROL_...
    FRMT_ID_DEFAULT_LAYER   = 2,
    FRMT_ID_CLI             = 3,
    //FRMT_ID_BATTERY_STATUS  = 4, // deprecated
    FRMT_ID_STATUS          = 4,
    FRMT_ID_MACWIN_MODE     = 5,
    //FRMT_ID_RGB_MATRIX_MODE = 6, // deprecated
    //FRMT_ID_RGB_MATRIX_HSV  = 7, // deprecated
    FRMT_ID_STRUCT_LAYOUT   = 8,
    FRMT_ID_CONFIG          = 9,
    FRMT_ID_KEYEVENT        = 10,   // todo bb: ID_EVENT and add EVENT_ID_KEYPRESS, EVENT_ID_...
    FRMT_ID_DYNLD_FUNCTION  = 250,  // dynamic load function into ram (todo bb: move to CLI or CONTROL_...)
    FRMT_ID_DYNLD_FUNEXEC   = 251,  // exec "dynamic loaded function"
};

#define _FRMT_HANDLE_CMD_SET_FN(name)   _frmt_handle_cmd_set_##name
#define _FRMT_HANDLE_CMD_GET_FN(name)   _frmt_handle_cmd_get_##name
#define _FRMT_HANDLE_CMD_SET(name)      void _FRMT_HANDLE_CMD_SET_FN(name)(uint8_t cmd, uint8_t seqnum, uint8_t len, uint8_t *buf)
#define _FRMT_HANDLE_CMD_GET(name)      void _FRMT_HANDLE_CMD_GET_FN(name)(uint8_t cmd, uint8_t seqnum, uint8_t len, uint8_t *buf)
#define _FRMT_HANDLE_CMD_SETGET(name)   _FRMT_HANDLE_CMD_SET(name); _FRMT_HANDLE_CMD_GET(name)

_FRMT_HANDLE_CMD_SETGET(default_layer);
_FRMT_HANDLE_CMD_SET(cli);
_FRMT_HANDLE_CMD_SETGET(macwin_mode);
_FRMT_HANDLE_CMD_GET(status);
_FRMT_HANDLE_CMD_SET(rgb_matrix_buf);
_FRMT_HANDLE_CMD_GET(struct_layout);
_FRMT_HANDLE_CMD_SETGET(config);
_FRMT_HANDLE_CMD_SET(dynld_function);
_FRMT_HANDLE_CMD_SET(dynld_funexec);

// rgb matrix buffer set from host
typedef struct rgb_matrix_host_buffer {
    struct {
        uint8_t duration;
        uint8_t r; // todo bb: store <8 bits per r/g/b if needed
        uint8_t g;
        uint8_t b;
    } led[RGB_MATRIX_LED_COUNT];

    bool written;
} rgb_matrix_host_buffer_t;

enum DYNLD_FUNC_ID {
    DYNLD_FUN_ID_ANIMATION = 0,
    DYNLD_FUN_ID_EXEC,

    DYNLD_FUN_ID_TEST,
    DYNLD_FUN_ID_MAX
};

typedef struct dynld_funcs {
    void* func[DYNLD_FUN_ID_MAX];
} dynld_funcs_t;

#define RAWHID_FIRMATA_MSG  0xFA

//------------------------------------------------------------------------------
typedef void (*sysexCallbackFunction)(uint8_t command, uint8_t len, uint8_t *buf);

void firmata_initialize(const char* firmware);
void firmata_start(void);
void firmata_task(void);

int firmata_recv(uint8_t c);
int firmata_recv_data(uint8_t *data, uint8_t len);
void firmata_send_sysex(uint8_t cmd, uint8_t* data, int len);

#ifdef __cplusplus
}
#endif
