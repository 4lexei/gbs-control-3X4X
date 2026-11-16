#include "SerialMirror.h"
#include "../core/Globals.h"
#include <Arduino.h>

void SerialMirror::flushToWebSocket() {
    if (wsBufferPos > 0 && ESP.getFreeHeap() > 10000) {
        webSocket.broadcastTXT((uint8_t*)wsBuffer, wsBufferPos);
    }
    wsBufferPos = 0;
    lastFlushTime = millis();
}

// lines starting with these are repetitive housekeeping and hidden in "clean" mode
static const char *const NOISY_PREFIXES[] = {
    "user command", "sync: sfr", "sync done", "sync skipped", "clock gen reset",
    // v37.1: FTL (frame time lock) housekeeping
    "runFreq:", "runFrequency()", "Skipping FrameSyncManager", "externalClockGenSync",
    "FrameSyncManager::runFrequency()",
};

bool SerialMirror::isNoisyLine(const char *s, size_t n) {
    for (size_t i = 0; i < sizeof(NOISY_PREFIXES) / sizeof(NOISY_PREFIXES[0]); i++) {
        size_t pl = strlen(NOISY_PREFIXES[i]);
        if (n >= pl && strncmp(s, NOISY_PREFIXES[i], pl) == 0) {
            return true;
        }
    }
    return false;
}

void SerialMirror::addToBuffer(char c) {
    if (lineDropped) {
        if (c == '\n') {
            lineDropped = false;
            lineLen = 0;
            lineDecided = false;
        }
        return;
    }
    if (wsBufferPos >= WS_BUFFER_SIZE - 1) {
        flushToWebSocket();
        lineStart = 0;
    }
    if (lineLen == 0) {
        lineStart = wsBufferPos;
        lineDecided = false;
    }
    wsBuffer[wsBufferPos++] = c;
    lineLen++;

    // decide once the line is long enough to tell (or ends): drop it from the buffer if it is noise
    if (!lineDecided && (lineLen >= 16 || c == '\n')) {
        lineDecided = true;
        if (cleanLog && isNoisyLine(wsBuffer + lineStart, wsBufferPos - lineStart)) {
            wsBufferPos = lineStart;
            if (c != '\n') {
                lineDropped = true;
            } else {
                lineLen = 0;
                lineDecided = false;
            }
            return;
        }
    }
    if (c == '\n') {
        lineLen = 0;
        lineDecided = false;
        flushToWebSocket();
        return;
    }
    // timed flush only when the current line is already classified
    if (lineDecided && (millis() - lastFlushTime) > FLUSH_INTERVAL_MS) {
        flushToWebSocket();
    }
}

void SerialMirror::sendStatus(const char *text) {
    if (ESP.getFreeHeap() < 10000) {
        return;
    }
    // v92: 128 was enough for the old one-line status bar but not for the three-line one: the user's bar came to 129
    // characters and arrived cut off at "step 1/4 squ". The sender builds at most 260, so this has to hold that plus the
    // 0x1e marker and the terminator.
    static char buf[670]; // v107j / v107l: the sender now builds up to 600 (the data lines); static, off the stack
    size_t n = snprintf(buf, sizeof(buf), "\x1e%s", text);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    webSocket.broadcastTXT((uint8_t *)buf, n);
}

size_t SerialMirror::write(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        addToBuffer((char)data[i]);
    }
    Serial.write(data, size);
    return size;
}

size_t SerialMirror::write(const char *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        addToBuffer(data[i]);
    }
    Serial.write(data, size);
    return size;
}

size_t SerialMirror::write(uint8_t data) {
    addToBuffer((char)data);
    Serial.write(data);
    return 1;
}

size_t SerialMirror::write(char data) {
    addToBuffer(data);
    Serial.write(data);
    return 1;
}

void SerialMirror::flush() { 
    flushToWebSocket(); 
}

// Global instance
SerialMirror SerialM;

