#include "Firmata_QMK.h"
#include "Firmata.h"

extern "C" {
#include "virtser.h"
}


#define RX_BUFFER_SIZE 256
#define TX_BUFFER_SIZE 256

typedef uint16_t tx_buffer_index_t;
typedef uint16_t rx_buffer_index_t;

typedef void (*send_char_fn)(uint8_t ch);


class BufferStream : public Stream
{
public:

uint8_t _rx_buffer[RX_BUFFER_SIZE] = {};
uint8_t _tx_buffer[TX_BUFFER_SIZE] = {};

volatile rx_buffer_index_t _rx_buffer_head;
volatile rx_buffer_index_t _rx_buffer_tail;
volatile tx_buffer_index_t _tx_buffer_head;
volatile tx_buffer_index_t _tx_buffer_tail;

// Has any byte been written to the UART since begin()
bool _written;
bool _tx_eol;

send_char_fn    _send_char;


public:

    inline BufferStream(send_char_fn send_char) {
        _tx_eol = 0;
        _written = 0;
        _rx_buffer_head = 0;
        _rx_buffer_tail = 0;
        _tx_buffer_head = 0;
        _tx_buffer_tail = 0;

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

        if (_rx_buffer_head == _rx_buffer_tail) return -1;

        return 1;
    }

    //virtual int available(void) { return ((unsigned int)(RX_BUFFER_SIZE + _rx_buffer_head - _rx_buffer_tail)) % RX_BUFFER_SIZE; }
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
        tx_buffer_index_t head;
        tx_buffer_index_t tail;

        {
            head = _tx_buffer_head;
            tail = _tx_buffer_tail;
        }
        if (head >= tail) return TX_BUFFER_SIZE - 1 - head + tail;
        return tail - head - 1;
    }

    virtual void flush(void) {
        if (!_written)
            return;

        if (_send_char) {
            while (_tx_buffer_head != _tx_buffer_tail) {
                uint8_t c = _tx_buffer[_tx_buffer_tail];
                _tx_buffer_tail = (_tx_buffer_tail + 1) % TX_BUFFER_SIZE;
                _send_char(c);
            }
        }

        _written = 0;
        _tx_eol = 0;
        _tx_buffer_head = 0;
        _tx_buffer_tail = 0;
        //if (_tx_buffer_head == _tx_buffer_tail) // buffer empty
    }

    virtual size_t write(uint8_t c) {
        _written = 1;

        tx_buffer_index_t i = (_tx_buffer_head + 1) % TX_BUFFER_SIZE;
        _tx_buffer[_tx_buffer_head] = c;
        _tx_buffer_head = i;

        if (c == '\n') _tx_eol = 1;
        return 1;
    }

    uint8_t tx_eol() { return _tx_eol; }

    //using Print::write; // pull in write(str) and wC2rite(buf, size) from Print
    //operator bool() { return true; }
};


//------------------------------------------------------------------------------


static firmata::FirmataClass g_firmata;
static bool g_firmata_started = 0;
static BufferStream g_virtser_stream(virtser_send_nonblock);
static BufferStream g_console_stream(nullptr);

extern "C" {

__attribute__((weak)) void virtser_send_nonblock(const uint8_t byte) {}

// "console sendchar"
int8_t sendchar_virtser(uint8_t c) {
    g_console_stream.write(c);
    return 1;
}


void firmata_initialize(const char* firmware) {
    g_firmata.setFirmwareNameAndVersion(firmware, FIRMATA_QMK_MAJOR_VERSION, FIRMATA_QMK_MINOR_VERSION);
    //g_firmata.begin(g_virtser_stream);
}

void firmata_start() {
    g_firmata.begin(g_virtser_stream);
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

    return g_virtser_stream.received(c);
}

void firmata_process() {
    if (!g_firmata_started) return;

    const uint8_t max_iterations = 100;
    uint8_t n = 0;
    while (g_firmata.available()) {
        g_firmata.processInput();
        if (n++ >= max_iterations) break;
    }

    if (g_console_stream.tx_eol()) {
        g_console_stream._tx_buffer[g_console_stream._tx_buffer_head] = 0;
        g_firmata.sendString((char*)&g_console_stream._tx_buffer[g_console_stream._tx_buffer_tail]);
        g_console_stream.flush();
    }
}


}
