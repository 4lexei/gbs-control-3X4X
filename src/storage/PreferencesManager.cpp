#include "PreferencesManager.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include <LittleFS.h>
#include "../core/options.h"
#include "../menu/OLEDMenuConfig.h"
#include "../config/Config.h"
#include "../gbs/GBSPhase.h"
#include "../video/Integer1080.h" // (after v107v) CONSOLE_PROFILE_COUNT

void syncReverseRotaryEncoderIsrMirror()
{
#if USE_NEW_OLED_MENU
    g_reverseRotaryEncoderForOledMenu = uopt->reverseRotaryEncoderForOledMenu ? 1 : 0;
#endif
}

void loadDefaultUserOptions()
{
    uopt->presetPreference = Output960P;    // #1
    uopt->enableFrameTimeLock = 1; // (after v110c) his decision of 2026-11-17: on by default, so the output follows a source that changes rate (240p <-> 480i). Stock: 0, "does not work on all displays"
    uopt->presetSlot = 'A';          //
    uopt->frameTimeLockMethod = 0; // compatibility with more displays
    uopt->enableAutoGain = 0;
    uopt->wantScanlines = 0;
    uopt->wantOutputComponent = 0;
    uopt->deintMode = 0;
    uopt->wantVdsLineFilter = 0;
    uopt->wantPeaking = 1;
    uopt->preferScalingRgbhv = 1;
    uopt->wantTap6 = 1;
    uopt->PalForce60 = 0;
    uopt->matchPresetSource = 1;             // #14
    uopt->wantStepResponse = 1;              // #15
    uopt->wantFullHeight = 1;                // #16
    uopt->enableCalibrationADC = 1;          // #17
    uopt->scanlineStrength = 0x30;           // #18
    uopt->disableExternalClockGenerator = 0; // #19
    uopt->reverseRotaryEncoderForOledMenu =
        REVERSE_ROTARY_ENCODER_FOR_OLED_MENU > 0 ? 1 : 0; // #20 default from build-time macro
    uopt->outputRateMode = 0;                // #21 follow source
    uopt->outputRateExact = 0;               // #214..#219 (after v111b)
    uopt->outputChosen = 0;                  // #220 (after v111f) nothing picked yet: the web shows the red notice
    uopt->integer1080 = 0;
    uopt->integer1080Narrow = 0;
    uopt->consoleProfile = 0;
    uopt->integer720 = 0;
    uopt->integer720Fill = 0;
    uopt->integer1080Wide = 0;
    uopt->integerLow = 0;
    uopt->tolerantSync = 0;
    uopt->autoLockSync = 0;
    uopt->x5MemClock = 0; // v77: 0 = the per-profile default
    uopt->memPhase = 0;   // v84: 0 = what the preset loads
    uopt->sizeStep = 0;   // v86: square
    uopt->x5Oversample = 0; // v103: experiment off
    for (uint8_t i = 0; i < 16; i++) { // v88: 0 = the default for that mode
        uopt->memClockSlot[i] = 0;
        uopt->memPhaseSlot[i] = 0;
        for (uint8_t k = 0; k < 4; k++) { // v107m
            uopt->memClockStep[i][k] = 0;
            uopt->memPhaseStep[i][k] = 0;
        }
        uopt->linesMode[i] = 0; // v107n
    }
    uopt->x3Wide = 1; // v107o: 720p x3 opens in 16:9
    uopt->integer1080Tv43 = 0; // (after v108h) the Mega Drive's 1080p x4 opens in 16:9
    uopt->autoRetrySync = 0;
    uopt->peakLevel = 1;                     // #22 stock peaking strength
    uopt->stepLevel = 1;                     // #23 stock step response strength
    uopt->adcFilterLevel = 0;                // #24 stock ADC filter
    syncReverseRotaryEncoderIsrMirror();
}

// (after v122) his request of 2026-11-22: the preferences are built in a buffer and compared with what is already in
// the flash. If nothing changed, nothing is written. Two of the writes happen on their own every time a console is
// switched on (the mode restore and the colour preset), and they almost always wrote the very same bytes.
#define PREFS_BUF_MAX 320
struct PrefsBuf {
    uint8_t b[PREFS_BUF_MAX];
    uint16_t n = 0;
    bool over = false;
    void write(uint8_t c)
    {
        if (n < PREFS_BUF_MAX) b[n++] = c; else over = true;
    }
    void write(char c) { write((uint8_t)c); }
    void write(int c) { write((uint8_t)c); }
    void write(const uint8_t *p, size_t len)
    {
        while (len--) write(*p++);
    }
    void close() {}
};

// true when the flash already holds exactly these bytes
static bool prefsSameAsFlash(const uint8_t *buf, uint16_t n)
{
    File r = LittleFS.open("/preferencesv2.txt", "r");
    if (!r)
        return false;
    bool same = ((uint16_t)r.size() == n);
    for (uint16_t i = 0; same && i < n; i++)
        same = (r.read() == (int)buf[i]);
    r.close();
    return same;
}

// (after v123j) his request of 2026-11-22, step 2: the preferences are written 30 s after the last change, like the
// borders, so a run of presses writes the flash once. saveUserPrefs() only arms the deadline (and keeps the encoder
// mirror up to date at once, as before); prefsTick() writes when it comes; prefsFlush() writes now, before anything
// that reads the file or restarts the board.
static uint32_t prefsSaveAt = 0;
static void writeUserPrefsNow();

void saveUserPrefs()
{
    syncReverseRotaryEncoderIsrMirror();
    prefsSaveAt = millis() + 30000;
    if (prefsSaveAt == 0)
        prefsSaveAt = 1;
}

void prefsFlush()
{
    if (prefsSaveAt == 0)
        return;
    prefsSaveAt = 0;
    writeUserPrefsNow();
}

void prefsTick()
{
    if (prefsSaveAt != 0 && (long)(millis() - prefsSaveAt) >= 0)
        prefsFlush();
}

static void writeUserPrefsNow()
{
    PrefsBuf f;
    f.write(uopt->presetPreference + '0'); // #1
    f.write(uopt->enableFrameTimeLock + '0');
    f.write(uopt->presetSlot);
    f.write(uopt->frameTimeLockMethod + '0');
    f.write(uopt->enableAutoGain + '0');
    f.write(uopt->wantScanlines + '0');
    f.write(uopt->wantOutputComponent + '0');
    f.write(uopt->deintMode + '0');
    f.write(uopt->wantVdsLineFilter + '0');
    f.write(uopt->wantPeaking + '0');
    f.write(uopt->preferScalingRgbhv + '0');
    f.write(uopt->wantTap6 + '0');
    f.write(uopt->PalForce60 + '0');
    f.write(uopt->matchPresetSource + '0');             // #14
    f.write(uopt->wantStepResponse + '0');              // #15
    f.write(uopt->wantFullHeight + '0');                // #16
    f.write(uopt->enableCalibrationADC + '0');          // #17
    f.write(uopt->scanlineStrength + '0');              // #18
    f.write(uopt->disableExternalClockGenerator + '0'); // #19
    f.write(uopt->reverseRotaryEncoderForOledMenu + '0'); // #20
    f.write(uopt->outputRateMode + '0');                  // #21
    f.write(uopt->peakLevel + '0');                       // #22
    f.write(uopt->stepLevel + '0');                       // #23
    f.write(uopt->adcFilterLevel + '0');                  // #24
    f.write(uopt->integer1080 + '0');                     // #25 1080p integer variant (0 = off)
    f.write((char)('A' + manualPhaseADC + 1));            // #26 manual ADC phase ('A' = automatic)
    f.write(uopt->integer720 + '0');                      // #27 720p x3 (0 = off)
    f.write((char)('0' + (uopt->consoleProfile >= CONSOLE_PROFILE_COUNT ? 0 : uopt->consoleProfile))); // #28 console profile (v43; v62: 3 = PC Engine; (after v107v) 4..17 = '4'..'A')
    f.write(uopt->integerLow + '0');                      // #29 low-res integer mode (v62: 1 = 960p x4, 2 = 480p x2)
    f.write(uopt->tolerantSync + '0');                    // #30 v63: tolerant sync
    f.write(uopt->autoLockSync + '0');                    // #31 v64: auto-lock (0 = off, 1..4 = lock point)
    f.write(uopt->x5MemClock + '0');                      // #32 v77: SDRAM clock for the x5 modes (0 = per-profile default)
    f.write(uopt->memPhase + '0');                        // #33 v84: SDRAM clock phase, 0..15
    f.write(uopt->sizeStep + '0');                        // #34 v86: size step of the mode, 0..3
    // v88: the SDRAM clock and phase of each mode, 16 slots each. Appended at the end, so a preferences file written by an
    // older version still loads: the reader stops when the file runs out and the slots stay at 0 (= the mode's default).
    for (uint8_t i = 0; i < 16; i++)
        f.write((uopt->memClockSlot[i] & 7) + '0');       // #35..#50
    for (uint8_t i = 0; i < 16; i++)
        f.write((uopt->memPhaseSlot[i] & 0x0f) + '0');    // #51..#66
    f.write((uopt->x5Oversample ? 1 : 0) + '0');          // #67 v103: PAL 720p x5 oversampling experiment
    // v107m: the SDRAM clock and phase per mode and size step, 16 x 4 each. A file that ends before this is migrated once
    // when it is read (see the reader).
    for (uint8_t i = 0; i < 16; i++)
        for (uint8_t k = 0; k < 4; k++)
            f.write((uopt->memClockStep[i][k] & 7) + '0');    // #68..#131
    for (uint8_t i = 0; i < 16; i++)
        for (uint8_t k = 0; k < 4; k++)
            f.write((uopt->memPhaseStep[i][k] & 0x0f) + '0'); // #132..#195
    for (uint8_t i = 0; i < 16; i++)
        f.write((uopt->linesMode[i] <= 2 ? uopt->linesMode[i] : 0) + '0'); // #196..#211 v107n: the "Líneas" row, per mode
    f.write((uopt->x3Wide ? 1 : 0) + '0');                // #212 v107o: the shape 720p x3 was last used in
    f.write((uopt->integer1080Tv43 ? 1 : 0) + '0');       // #213 (after v108h) the Mega Drive's 1080p x4 shape (1 = 4:3)
    { // #214..#219 (after v111b) the exact output rate, six digits, 1/10000 Hz (599400 = 59.94 Hz)
        uint32_t v = uopt->outputRateExact <= 999999 ? uopt->outputRateExact : 0;
        char d[7];
        for (int8_t i = 5; i >= 0; i--) { d[i] = (char)('0' + v % 10); v /= 10; }
        f.write((const uint8_t *)d, 6);
    }
    f.write((uopt->outputChosen ? 1 : 0) + '0'); // #220 (after v111f) an output was picked by hand

    f.close();
    syncReverseRotaryEncoderIsrMirror(); // before the comparison: it must run on every call, as it always did
    if (f.over) { // should never happen: the buffer is bigger than the file
        SerialM.println(F("[3x4x-v123l] preferences: buffer too small, not saved"));
        return;
    }
    if (prefsSameAsFlash(f.b, f.n))
        return; // nothing changed: the flash is left alone
    File w = LittleFS.open("/preferencesv2.txt", "w");
    if (!w) {
        SerialM.println(F("saveUserPrefs: open file failed"));
        return;
    }
    w.write(f.b, f.n);
    w.close();
}

