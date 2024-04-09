#include "Firmata_QMK.h"
#include "Firmata.h"

extern "C" {
#include "raw_hid.h"
#include "virtser.h"
#include "timer.h"
}


#define RX_BUFFER_SIZE 256
#define TX_BUFFER_SIZE 256

typedef uint16_t tx_buffer_index_t;
typedef uint16_t rx_buffer_index_t;

typedef void (*send_data_fn)(uint8_t *data, uint16_t len);
typedef void (*send_char_fn)(uint8_t ch);


class BufferStream : public Stream
{
public:

uint8_t _rx_buffer[RX_BUFFER_SIZE] = {};
uint8_t _tx_hdr_buffer[TX_BUFFER_SIZE+4] = {};
uint8_t *_tx_buffer;

volatile rx_buffer_index_t _rx_buffer_head;
volatile rx_buffer_index_t _rx_buffer_tail;
volatile tx_buffer_index_t _tx_buffer_head;
volatile tx_buffer_index_t _tx_buffer_tail;

// Has any byte been written to the UART since begin()
bool _written;
bool _tx_flush; // flag to flush it
uint16_t _tx_last_flush = 0;

send_data_fn    _send_data;
send_char_fn    _send_char;

public:

    BufferStream(send_data_fn send_data, send_char_fn send_char) {
        _written = 0;
        _rx_buffer_head = 0;
        _rx_buffer_tail = 0;

        _tx_buffer = _tx_hdr_buffer + 4; // 4 bytes reserved for header
        _tx_buffer_head = 0;
        _tx_buffer_tail = 0;
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
        rx_buffer_index_t i = (_rx_buffer_head + 1) % RX_BUFFER_SIZE;
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
        // if the head isn't ahead of the tail, we don't have any characters
        if (_rx_buffer_head == _rx_buffer_tail) {
            return -1;
        } else {
            unsigned char c = _rx_buffer[_rx_buffer_tail];
            _rx_buffer_tail = (rx_buffer_index_t)(_rx_buffer_tail + 1) % RX_BUFFER_SIZE;
            if (_rx_buffer_tail == _rx_buffer_head) {
                _rx_buffer_head = 0;
                _rx_buffer_tail = 0;
            }
            return c;
        }
    }

    virtual int availableForWrite(void) {
        if (_tx_buffer_head < TX_BUFFER_SIZE) return 1;
        return 0;
    }

    virtual void flush(void) {
        if (!_written) return;

        if (_send_data) {
            if (_tx_buffer_head != _tx_buffer_tail) {
                _send_data(&_tx_buffer[_tx_buffer_tail], _tx_buffer_head - _tx_buffer_tail);
            }
        } else
        if (_send_char) {
            while (_tx_buffer_head != _tx_buffer_tail) {
                uint8_t c = _tx_buffer[_tx_buffer_tail];
                _tx_buffer_tail = (_tx_buffer_tail + 1) % TX_BUFFER_SIZE;
                _send_char(c);
            }
        }

        _written = 0;
        _tx_buffer_head = 0;
        _tx_buffer_tail = 0;
        _tx_flush = 0;
        _tx_last_flush = timer_read();
    }

    virtual size_t write(uint8_t c) {
        tx_buffer_index_t i = (_tx_buffer_head + 1) % TX_BUFFER_SIZE;
        _tx_buffer[_tx_buffer_head] = c;
        _tx_buffer_head = i;
        _written = 1;

        // flush when eol or buffer is getting full
        if (c == '\n') _tx_flush = 1;
        if (i >= TX_BUFFER_SIZE/2) _tx_flush = 1;
        return 1;
    }

    bool need_flush() {
        if (_tx_flush) return 1;
        if (timer_elapsed(_tx_last_flush) > 100) return 1;
        return 0;
    }
};


//------------------------------------------------------------------------------

#define MIN(a,b) (((a)<(b))?(a):(b))

void rawhid_send_data(uint8_t *data, uint16_t len) {
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

static firmata::FirmataClass g_firmata; // todo bb: override FirmataClass and add "started flag"
static bool g_firmata_started = 0;
static BufferStream g_rawhid_stream(rawhid_send_data, nullptr);
static BufferStream g_console_stream(nullptr, nullptr);

extern "C" {

void debug_led_on(int led)
{
    extern rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;
    static uint8_t i = 0;
    if (led == -1) led = i;
    g_rgb_matrix_host_buf.led[led].duration = 250;
    g_rgb_matrix_host_buf.led[led].r = 0;
    g_rgb_matrix_host_buf.led[led].g = 200;
    g_rgb_matrix_host_buf.led[led].b = 200;
    g_rgb_matrix_host_buf.written = 1;
    i = (i+1)%RGB_MATRIX_LED_COUNT;
}

// console sendchar
int8_t sendchar(uint8_t c) {
    g_console_stream.write(c);
    //if (g_console_stream.need_flush()) debug_led_on(0);
    return 0;
}


void firmata_initialize(const char* firmware) {
    g_firmata.setFirmwareNameAndVersion(firmware, FIRMATA_QMK_MAJOR_VERSION, FIRMATA_QMK_MINOR_VERSION);
}

void firmata_start() {
    g_firmata.begin(g_rawhid_stream);
    g_firmata_started = 1;
}

void firmata_attach(uint8_t cmd, sysexCallbackFunction newFunction) {
    g_firmata.attach(cmd, newFunction);
}


void firmata_send_sysex(uint8_t cmd, uint8_t* data, int len) {
    if (!g_firmata_started) return;

    g_firmata.sendSysex(cmd, len, data);
}

int firmata_recv(uint8_t c) {
    if (!g_firmata_started) {
        firmata_start();
    }
    return g_rawhid_stream.received(c);
}

int firmata_recv_data(uint8_t *data, uint8_t len) {
    int i = 0;
    while (len--) {
        if (firmata_recv(data[i++]) < 0) return -1;
    }
    //debug_led_on(-1);
    return 0;
}

void firmata_process() {
    if (!g_firmata_started) return;

    const uint8_t max_iterations = 64;
    uint8_t n = 0;
    while (g_firmata.available()) {
        g_firmata.processInput();
        if (n++ >= max_iterations) break;
    }

    if (g_console_stream.need_flush()) {
        g_console_stream._tx_buffer[g_console_stream._tx_buffer_head] = 0;
        g_firmata.sendString((char*)&g_console_stream._tx_buffer[g_console_stream._tx_buffer_tail]);
        g_console_stream.flush();
    }
}

}
