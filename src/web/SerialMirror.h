#ifndef SERIAL_MIRROR_H
#define SERIAL_MIRROR_H

#include <Arduino.h>
#include <Stream.h>
#include "WebSocketsServer.h"

// Buffered serial mirror class for websocket logs
// 
// Why buffering?
// - printf() calls write() for each character individually, creating separate WebSocket frames
// - Buffering collects characters and sends them as a single frame (on newline or buffer full)
// - String concatenation would also fragment heap; this 128-byte static buffer is predictable
// - Works automatically for all SerialM.print/println/printf calls
class SerialMirror : public Stream
{
private:
    static constexpr size_t WS_BUFFER_SIZE = 128;
    char wsBuffer[WS_BUFFER_SIZE];
    size_t wsBufferPos = 0;
    unsigned long lastFlushTime = 0;
    static constexpr unsigned long FLUSH_INTERVAL_MS = 50;

    // v37: log filter (web terminal only; the serial port always gets everything)
    bool cleanLog = true;
    size_t lineStart = 0;
    size_t lineLen = 0;
    bool lineDecided = false;
    bool lineDropped = false;

    void flushToWebSocket();
    void addToBuffer(char c);
    bool isNoisyLine(const char *s, size_t n);

public:
    size_t write(const uint8_t *data, size_t size);
    size_t write(const char *data, size_t size);
    size_t write(uint8_t data);
    size_t write(char data);
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush();
    // v37: clean = hide repetitive housekeeping lines in the web terminal, full = show everything
    void setCleanLog(bool clean) { cleanLog = clean; }
    bool isCleanLog() { return cleanLog; }
    // v37: one-line status for the web UI status bar (marker byte 0x1E + text), sent as its own frame
    void sendStatus(const char *text);
};

extern SerialMirror SerialM;

// (after v111d) printf with the format kept in flash: on the ESP8266 a plain string literal takes RAM for good, and RAM is
// what the web runs out of. Use it for every new log line.
#ifdef ESP8266
#define SM_PRINTF(fmt, ...) SerialM.printf_P(PSTR(fmt), ##__VA_ARGS__)
#else
#define SM_PRINTF(fmt, ...) SerialM.printf(fmt, ##__VA_ARGS__)
#endif

#endif // SERIAL_MIRROR_H

