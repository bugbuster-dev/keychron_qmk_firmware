#include "Firmata_QMK.h"
#include "Firmata.h"

extern "C" {
#include "raw_hid.h"
#include "virtser.h"
#include "util.h"
#include "timer.h"
#include "debug_user.h"
#include "version.h"
}

typedef uint16_t tx_buffer_index_t;
typedef uint16_t rx_buffer_index_t;

typedef void (*send_data_fn)(uint8_t *data, uint16_t len);
typedef void (*send_char_fn)(uint8_t ch);

// BufferStream is a "Firmata Stream" implementation that uses a buffer for "streaming" by
// receiving with received() into rx buffer and transmitting with write() to tx buffer and flush() to send it.
// sending is done in flush() by calling send_data() or send_char()
class BufferStream : public Stream
{
uint8_t *_rx_buffer;
uint16_t _rx_buffer_size;
uint8_t *_tx_buffer;
uint16_t _tx_buffer_size;

rx_buffer_index_t _rx_buffer_head;
rx_buffer_index_t _rx_buffer_tail;
tx_buffer_index_t _tx_buffer_head;
tx_buffer_index_t _tx_buffer_tail;

// any byte been written to tx buffer
bool _tx_written;
bool _tx_flush; // flag to flush it
uint16_t _tx_last_flush = 0;

send_data_fn    _send_data; // send data buf function
send_char_fn    _send_char; // send char function

public:

    BufferStream(uint8_t* rx_buf, uint16_t rx_buf_size,
                 uint8_t* tx_buf, uint16_t tx_buf_size,
                 send_data_fn send_data, send_char_fn send_char) {
        _rx_buffer = rx_buf;
        _rx_buffer_size = rx_buf_size;
        _rx_buffer_head = 0;
        _rx_buffer_tail = 0;

        _tx_buffer = tx_buf;
        _tx_buffer_size = tx_buf_size;
        _tx_buffer_head = 0;
        _tx_buffer_tail = 0;
        _tx_written = 0;
        _tx_flush = 0;
        _tx_last_flush = 0;

        _send_data = send_data;
        _send_char = send_char;
    };

    void begin(unsigned long baud) { begin(baud, 0); }
    void begin(unsigned long, uint8_t) {}
    void end() {
        // wait for transmission of outgoing data
        flush();

        // clear any received data
        _rx_buffer_head = _rx_buffer_tail = 0;
    }

    // received byte insert at head
    int received(uint8_t c) {
        rx_buffer_index_t i = (_rx_buffer_head + 1) % _rx_buffer_size;
        _rx_buffer[_rx_buffer_head] = c;
        _rx_buffer_head = i;

        if (_rx_buffer_head == _rx_buffer_tail) {
            _rx_buffer_head = _rx_buffer_tail = 0;
            return -1;
        }

        return 1;
    }

    virtual int available(void) { if (_rx_buffer_head != _rx_buffer_tail) return 1; return 0; }

    virtual int peek(void) {
        if (_rx_buffer_head == _rx_buffer_tail) {
            return -1;
        } else {
            return _rx_buffer[_rx_buffer_tail];
        }
    }

    // read from tail
    virtual int read(void) {
        if (_rx_buffer_head == _rx_buffer_tail) {
            return -1;
        } else {
            unsigned char c = _rx_buffer[_rx_buffer_tail];
            _rx_buffer_tail = (rx_buffer_index_t)(_rx_buffer_tail + 1) % _rx_buffer_size;
            if (_rx_buffer_tail == _rx_buffer_head) {
                _rx_buffer_head = 0;
                _rx_buffer_tail = 0;
            }
            return c;
        }
    }

    virtual int availableForWrite(void) {
        if (_tx_buffer_head < _tx_buffer_size) return 1;
        return 0;
    }

    virtual void flush(void) {
        _tx_last_flush = timer_read();
        if (!_tx_written) return;

        if (_send_data) {
            if (_tx_buffer_head != _tx_buffer_tail) {
                _send_data(&_tx_buffer[_tx_buffer_tail], _tx_buffer_head - _tx_buffer_tail);
            }
        } else
        if (_send_char) {
            while (_tx_buffer_head != _tx_buffer_tail) {
                uint8_t c = _tx_buffer[_tx_buffer_tail];
                _tx_buffer_tail = (_tx_buffer_tail + 1) % _tx_buffer_size;
                _send_char(c);
            }
        }
        _tx_written = 0;
        _tx_buffer_head = 0;
        _tx_buffer_tail = 0;
        _tx_flush = 0;
    }

    virtual size_t write(uint8_t c) {
        tx_buffer_index_t i = (_tx_buffer_head + 1) % _tx_buffer_size;
        _tx_buffer[_tx_buffer_head] = c;
        _tx_buffer_head = i;
        _tx_written = 1;

        // flush when eol or buffer is getting full
        if (c == '\n') _tx_flush = 1;
        if (i >= _tx_buffer_size/2) _tx_flush = 1;
        return 1;
    }

    bool need_flush() {
        if (_tx_flush) return 1;
        if ((_tx_buffer_head != _tx_buffer_tail) &&
           (timer_elapsed(_tx_last_flush) > 100)) return 1;
        return 0;
    }
};

class QmkFirmata : public firmata::FirmataClass
{
    bool _started = 0;
    bool _paused  = 0;
public:
    QmkFirmata() : FirmataClass() {}

    void begin(Stream &s) {
        FirmataClass::begin(s);
        _started = 1;
    }

    void pause() {  // todo bb: pause/resume could be used to prevent sending firmata messages when via is active
        _paused = 1;
    }

    void resume() {
        _paused = 0;
    }

    int available() {
        if (_paused) return 0;
        return FirmataClass::available();
    }

    bool started() { return _started; }
};
static QmkFirmata s_firmata;

//------------------------------------------------------------------------------
static void _rawhid_send_data(uint8_t *data, uint16_t len) {
    uint8_t buf[RAW_EPSIZE_FIRMATA] = {0};
    uint8_t *hdr = data - 1;
    *hdr = RAWHID_FIRMATA_MSG; // firmata

    while (len) {
        uint8_t send_len = MIN(RAW_EPSIZE_FIRMATA, len+1);
        if (send_len < RAW_EPSIZE_FIRMATA) {
            memset(buf, 0, sizeof(buf));
            memcpy(buf, hdr, send_len);
            raw_hid_send(buf, RAW_EPSIZE_FIRMATA);
            return;
        }

        raw_hid_send(hdr, RAW_EPSIZE_FIRMATA);
        len -= send_len - 1;
        if (len) {
            hdr += send_len - 1;
            *hdr = RAWHID_FIRMATA_MSG;
        }
    }
}

static void _send_console_string(uint8_t *data, uint16_t len) {
    if (!s_firmata.started()) return;
#ifdef DEVEL_BUILD
    static bool build_date_sent = 0;
    if (!build_date_sent) {
        s_firmata.sendString(QMK_BUILDDATE "\n");
        build_date_sent = 1;
    }
#endif
    data[len] = 0;
    s_firmata.sendString((char*)data);
}

//------------------------------------------------------------------------------
#define TX_BUF_RESERVE 4 // reserve bytes before tx buffer for RAWHID_FIRMATA_MSG
static uint8_t _qmk_firmata_rx_buf[256] = {};
static uint8_t _qmk_firmata_tx_buf[512+TX_BUF_RESERVE] = {};
static uint8_t _qmk_firmata_console_buf[240] = {}; // adjust size as needed to hold console output until "firmata task" is called

static BufferStream s_rawhid_stream(_qmk_firmata_rx_buf, sizeof(_qmk_firmata_rx_buf),
                                    _qmk_firmata_tx_buf+TX_BUF_RESERVE, sizeof(_qmk_firmata_tx_buf)-TX_BUF_RESERVE,
                                    _rawhid_send_data, nullptr);
static BufferStream s_console_stream(nullptr, 0,
                                     _qmk_firmata_console_buf, sizeof(_qmk_firmata_console_buf)-1,
                                     _send_console_string, nullptr);

extern "C" {

void debug_led_on(int li)
{
#ifdef DEVEL_BUILD
    extern rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;
    static uint8_t s_li = 0;
    if (li == -1) li = s_li;
    g_rgb_matrix_host_buf.led[li].duration = 250;
    g_rgb_matrix_host_buf.led[li].r = 0;
    g_rgb_matrix_host_buf.led[li].g = 200;
    g_rgb_matrix_host_buf.led[li].b = 200;
    g_rgb_matrix_host_buf.written = 1;
    s_li = (s_li+1)%RGB_MATRIX_LED_COUNT;
#endif
}

// console sendchar
int8_t sendchar(uint8_t c) {
    s_console_stream.write(c);
    //if (g_console_stream.need_flush()) debug_led_on(0);
    return 0;
}

void firmata_initialize(const char* firmware) {
    extern void firmata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t* buf);
    s_firmata.setFirmwareNameAndVersion(firmware, FIRMATA_QMK_MAJOR_VERSION, FIRMATA_QMK_MINOR_VERSION);
    s_firmata.attach(0, firmata_sysex_handler);
}

void firmata_start() {
    s_firmata.begin(s_rawhid_stream);
}

void firmata_send_sysex(uint8_t cmd, uint8_t* data, int len) {
    if (!s_firmata.started()) return;

    s_firmata.sendSysex(cmd, len, data);
}

int firmata_recv(uint8_t c) {
    if (!s_firmata.started()) {
        firmata_start();
    }
    return s_rawhid_stream.received(c);
}

int firmata_recv_data(uint8_t *data, uint8_t len) {
    int i = 0;
    while (len--) {
        if (firmata_recv(data[i++]) < 0) {
            DBG_USR(firmata, "[FA]","recv:error\n"); // buffer overflow
            return -1;
        }
    }
    //debug_led_on(-1);
    return 0;
}

void firmata_task() {
    if (!s_firmata.started()) return;

    const uint8_t max_iterations = 64;
    uint8_t n = 0;
    while (s_firmata.available()) {
        s_firmata.processInput();
        if (n++ >= max_iterations) break;
    }

    if (s_console_stream.need_flush()) {
        s_console_stream.flush();
    }
}

}
