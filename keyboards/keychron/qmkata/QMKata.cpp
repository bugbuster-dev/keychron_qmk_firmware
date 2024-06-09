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

#include "QMKata.h"
#include "Firmata.h"

extern "C" {
#include "raw_hid.h"
#include "virtser.h"
#include "util.h"
#include "timer.h"
#include "debug_user.h"
#include "version.h"

void debug_led_on(int li);
}

typedef uint16_t tx_buffer_index_t;
typedef uint16_t rx_buffer_index_t;

typedef void (*send_data_fn)(uint8_t *data, uint16_t len);
typedef void (*send_char_fn)(uint8_t ch);

// BufferStream is the "Firmata Stream" implementation, external rx buffer can be set
// to process directly from it or put in its own rx buffer with received().
// Firmata calls write() to write to tx buffer and flush() to send it.
// sending is done in flush() by calling send_data() or send_char() function hooks.
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

send_data_fn    _send_data; // send data buf
send_char_fn    _send_char; // send one char

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

    // set external "rx buffer" and process directly from it if possible
    // otherwise copy to rx buffer with received()
    int rx_buffer_set(uint8_t* buf, uint16_t len) {
        _rx_buffer = buf;
        _rx_buffer_size = len;
        _rx_buffer_tail = 0;
        _rx_buffer_head = len;
        return 0;
    }

    // received byte insert at head
    int received(uint8_t c) {
        if (!_rx_buffer) return -1;

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
            uint8_t ch = _rx_buffer[_rx_buffer_tail];
            _rx_buffer_tail = (rx_buffer_index_t)_rx_buffer_tail + 1;
            if (_rx_buffer_tail == _rx_buffer_head) {
                _rx_buffer_head = 0;
                _rx_buffer_tail = 0;
            }
            _rx_buffer_tail %= _rx_buffer_size;
            return ch;
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
        _tx_buffer[_tx_buffer_head] = c;
        _tx_buffer_head++;
        _tx_written = 1;
        tx_buffer_index_t next_head = _tx_buffer_head % _tx_buffer_size;

        // todo bb: handle buffer full
        if (next_head == 0) {
            debug_led_on(0);
            // buffer full
            flush();
            return 1;
        }

        _tx_buffer_head = next_head;
        if (next_head >= _tx_buffer_size/2) _tx_flush = 1;
        return 1;
    }

    bool need_flush() {
        if (_tx_flush) return 1;
        if ((_tx_buffer_head != _tx_buffer_tail) &&
           (timer_elapsed(_tx_last_flush) > 100)) return 1;
        return 0;
    }
};

class QMKata : public firmata::FirmataClass
{
    bool _started = 0;
    bool _paused  = 0;
public:
    QMKata() : FirmataClass() {}

    void begin(Stream &s) {
        FirmataClass::begin(s);
        _started = 1;
    }

    void pause() {  // todo bb: pause/resume could be used to prevent sending qmkata messages when via is active
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
static QMKata s_qmkata;

//------------------------------------------------------------------------------
static void _rawhid_send_data(uint8_t *data, uint16_t len) {
    uint8_t *hdr = data - 1;
    *hdr = RAWHID_QMKATA_MSG; // qmkata

    while (len) {
        uint8_t send_len = MIN(RAW_EPSIZE_QMKATA, len+1);
        if (send_len < RAW_EPSIZE_QMKATA) {
            uint8_t buf[RAW_EPSIZE_QMKATA] = {0};
            memcpy(buf, hdr, send_len);
            raw_hid_send(buf, RAW_EPSIZE_QMKATA);
            return;
        }

        //xprintf("RS:%u\n", send_len);
        raw_hid_send(hdr, RAW_EPSIZE_QMKATA);
        len -= send_len - 1;
        if (len) {
            hdr += send_len - 1;
            *hdr = RAWHID_QMKATA_MSG;
        }
    }
}

#ifdef DEVEL_BUILD
char __QMK_BUILDDATE__[strlen(QMK_BUILDDATE)+2] = {0}; // variable so it gets in the map file for print test from host
#endif

static void _send_console_string(uint8_t *data, uint16_t len) {
    if (!s_qmkata.started()) return;
#ifdef DEVEL_BUILD
    static bool build_date_sent = 0;
    if (!build_date_sent) {
        int len = strlen(QMK_BUILDDATE);
        memcpy(__QMK_BUILDDATE__, QMK_BUILDDATE, len);
        __QMK_BUILDDATE__[len] = '\n';
        s_qmkata.sendString(__QMK_BUILDDATE__);
        build_date_sent = 1;
    }
#endif
    data[len] = 0;
    s_qmkata.sendString((char*)data);
}

//------------------------------------------------------------------------------
#define TX_BUF_RESERVE 4 // reserve bytes before tx buffer for RAWHID_QMKATA_MSG
static uint8_t _qmkata_tx_buf[512+TX_BUF_RESERVE] = {};
static uint8_t _qmkata_console_buf[240] = {}; // adjust size as needed to hold console output until "qmkata task" is called

static BufferStream s_rawhid_stream(0, 0,
                                    _qmkata_tx_buf+TX_BUF_RESERVE, sizeof(_qmkata_tx_buf)-TX_BUF_RESERVE,
                                    _rawhid_send_data, nullptr);
static BufferStream s_console_stream(nullptr, 0,
                                     _qmkata_console_buf, sizeof(_qmkata_console_buf)-1,
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

void qmkata_init(const char* firmware) {
    s_qmkata.setFirmwareNameAndVersion(firmware, QMKATA_MAJOR_VERSION, QMKATA_MINOR_VERSION);
    //s_qmkata.attach(0, qmkata_sysex_handler);
}

void qmkata_start() {
    s_qmkata.begin(s_rawhid_stream);
}

void qmkata_send_sysex(uint8_t cmd, uint8_t* data, int len) {
    if (!s_qmkata.started()) return;

    s_qmkata.sendSysex(cmd, len, data);
}

int qmkata_recv(uint8_t c) {
    return -1;
}

int qmkata_recv_data(uint8_t *data, uint8_t len) {
    //xprintf("QA:recv_data %p:%u\n", data, len);
    //debug_led_on(-1);
    if (!s_qmkata.started()) {
        qmkata_start();
    }
    // skip RAWHID_QMKATA_MSG byte
    data++; len--;
    // qmkata sysex start without 2x7 bits encoding, call handler directly
    if (data[0] == 0xF1) {
        extern void qmkata_sysex_handler(uint8_t cmd, uint8_t len, uint8_t* buf);
        data++; len--; // skip sysex start
        qmkata_sysex_handler(data[0], len, data+1);
        return 0;
    }
    // firmata sysex start 0xf0, with 2x7 bits encoding, sysex handler should decode it
#ifdef QMKATA_7BIT_SYSEX_ENABLE
    if (data[0] == START_SYSEX) {
        s_rawhid_stream.rx_buffer_set(data, len);
        const uint8_t max_iterations = len+1;
        uint8_t n = 0;
        while (s_qmkata.available()) {
            s_qmkata.processInput();
            if (n++ >= max_iterations) break;
        }
        if (n > len) {
            xprintf("QA:internal error\n");
            return -1;
        }
        return 0;
    }
#endif
    return -1;
}

void qmkata_task() {
    if (!s_qmkata.started()) return;

    if (s_rawhid_stream.availableForWrite()) {
        s_rawhid_stream.flush();
    }
    if (s_console_stream.need_flush()) {
        s_console_stream.flush();
        s_rawhid_stream.flush();
    }
}

}
