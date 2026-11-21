#include "SerialMirror.h"
#include "../core/Globals.h"
#include <Arduino.h>

void SerialMirror::flushToWebSocket() {
    // (after v111e) his v111e: the page loaded but nothing ever arrived («esperando estado»): the board idles between 10
    // and 14 KB free, so a 14 KB margin silenced the bar and the log for good. The log steps aside at 11 KB, the bar at
    // 10.5 KB, the web's own requests at 10 KB.
    if (wsBufferPos > 0 && ESP.getFreeHeap() > 11000) {
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
    // (after v111h) his request of 2026-11-21: the deinterlacer switching back and forth is housekeeping, not news
    "motion adaptive",
};

bool SerialMirror::isNoisyLine(const char *s, size_t n) {
    // (after v111h) our own lines carry the "[3x4x-vNNN] " tag; skip it, so a prefix matches the text itself and does
    // not have to be written with the version in it
    if (n > 7 && strncmp(s, "[3x4x-v", 7) == 0) {
        for (size_t i = 7; i < n; i++) {
            if (s[i] == ']') {
                i++;
                if (i < n && s[i] == ' ') {
                    i++;
                }
                s += i;
                n -= i;
                break;
            }
        }
    }
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
    // (after v111h) 32, not 16: our own lines spend 13 characters on the "[3x4x-vNNN] " tag before the text starts
    if (!lineDecided && (lineLen >= 32 || c == '\n')) {
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
    if (ESP.getFreeHeap() < 10500) { // (after v111e) see flushToWebSocket()
        return;
    }
    // v92: 128 was enough for the old one-line status bar but not for the three-line one: the user's bar came to 129
    // characters and arrived cut off at "step 1/4 squ". The sender builds at most 260, so this has to hold that plus the
    // 0x1e marker and the terminator.
    static char buf[64]; // (after v111d) only the short notices now ("No signal / syncing...", "Measuring..."): the bar itself is
                         // sent by sendStatusBar() (Si5351Manager.cpp) without this copy
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

