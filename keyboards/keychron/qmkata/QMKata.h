/* Copyright 2024 bugbuster
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QMKATA_MAJOR_VERSION   0
#define QMKATA_MINOR_VERSION   4

// set/get/add/del pub/sub commands should cover all the scenarios for now
// extension by adding more "ids" to set/get/...
enum {
    QMKATA_CMD_EXTENDED = 0, // todo bb: rename to QMKATA_CMD_...
    QMKATA_CMD_SET = 1,
    QMKATA_CMD_GET = 2,
    QMKATA_CMD_ADD = 3,
    QMKATA_CMD_DEL = 4,
    QMKATA_CMD_PUB = 5,
    QMKATA_CMD_SUB = 6,
    QMKATA_CMD_RESPONSE = 0x0f,
};

// todo bb: group by config/status/control/...
enum {
    QMKATA_ID_EXTENDED        = 0,
    QMKATA_ID_CONTROL         = 0xff, // todo bb:
    QMKATA_ID_RGB_MATRIX_BUF  = 1,    // todo bb: move to CONTROL_...
    QMKATA_ID_DEFAULT_LAYER   = 2,
    QMKATA_ID_CLI             = 3,
    //QMKATA_ID_BATTERY_STATUS  = 4, // deprecated
    QMKATA_ID_STATUS          = 4,
    QMKATA_ID_MACWIN_MODE     = 5,
    //QMKATA_ID_RGB_MATRIX_MODE = 6, // deprecated
    //QMKATA_ID_RGB_MATRIX_HSV  = 7, // deprecated
    QMKATA_ID_STRUCT_LAYOUT   = 8,
    QMKATA_ID_CONFIG          = 9,
    QMKATA_ID_KEYEVENT        = 10,   // todo bb: ID_EVENT and add EVENT_ID_KEYPRESS, EVENT_ID_...
    QMKATA_ID_DYNLD_FUNCTION  = 250,  // dynamic load function into ram (todo bb: move to CLI or CONTROL_...)
    QMKATA_ID_DYNLD_FUNEXEC   = 251,  // exec "dynamic loaded function"
};

#define _QMKATA_HANDLE_CMD_SET_FN(name)   _qmkata_handle_cmd_set_##name
#define _QMKATA_HANDLE_CMD_GET_FN(name)   _qmkata_handle_cmd_get_##name
#define _QMKATA_HANDLE_CMD_SET(name)      void _QMKATA_HANDLE_CMD_SET_FN(name)(uint8_t cmd, uint8_t seqnum, uint8_t len, uint8_t *buf)
#define _QMKATA_HANDLE_CMD_GET(name)      void _QMKATA_HANDLE_CMD_GET_FN(name)(uint8_t cmd, uint8_t seqnum, uint8_t len, uint8_t *buf)
#define _QMKATA_HANDLE_CMD_SETGET(name)   _QMKATA_HANDLE_CMD_SET(name); _QMKATA_HANDLE_CMD_GET(name)

_QMKATA_HANDLE_CMD_SETGET(default_layer);
_QMKATA_HANDLE_CMD_SET(cli);
_QMKATA_HANDLE_CMD_SETGET(macwin_mode);
_QMKATA_HANDLE_CMD_GET(status);
_QMKATA_HANDLE_CMD_SET(rgb_matrix_buf);
_QMKATA_HANDLE_CMD_GET(struct_layout);
_QMKATA_HANDLE_CMD_SETGET(config);
_QMKATA_HANDLE_CMD_SET(dynld_function);
_QMKATA_HANDLE_CMD_SET(dynld_funexec);

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

#define RAWHID_QMKATA_MSG  0xFA

//------------------------------------------------------------------------------
typedef void (*sysexCallbackFunction)(uint8_t command, uint8_t len, uint8_t *buf);

void qmkata_init(const char* firmware);
void qmkata_start(void);
void qmkata_task(void);

int qmkata_recv(uint8_t c);
int qmkata_recv_data(uint8_t *data, uint8_t len);
void qmkata_send_sysex(uint8_t cmd, uint8_t* data, int len);

#ifdef __cplusplus
}
#endif
