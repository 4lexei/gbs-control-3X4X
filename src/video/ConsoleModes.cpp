#include "ConsoleModes.h"
#include "../core/Globals.h"
#include "../core/options.h"
#include "../web/SerialMirror.h"
#include "../gbs/GBSController.h"
#include "../storage/PreferencesManager.h"
#include "Integer1080.h"
#include <LittleFS.h>

// (after v107v) see ConsoleModes.h. Keep the field order: it is the file format too.
struct ConsoleMode
{
    uint8_t valid;
    uint8_t pref;     // uopt->presetPreference
    uint8_t slot;     // uopt->presetSlot, with a personal preset
    uint8_t i720;     // uopt->integer720
    uint8_t i1080;    // uopt->integer1080
    uint8_t iLow;     // uopt->integerLow
    uint8_t step;     // uopt->sizeStep (PAL: the width lives here)
    uint8_t centered; // rto->integer1080Centered (NTSC: 1 = 320 px, 2 = 256 px)
    uint8_t fill;     // uopt->integer720Fill (NTSC)
    uint8_t wide;     // uopt->integer1080Wide (NTSC)
    uint8_t narrow;   // uopt->integer1080Narrow (the 256 px loads that need the ADC clock at 80 %)
    uint8_t spare;    // (after v108h) uopt->integer1080Tv43: the Mega Drive's 1080p x4 in «4:3» (0 in every older record)
};

#define CM_RECORDS 34 // profiles 1..17, 60 Hz and 50 Hz (fixed: «Otras» goes after the Dreamcast's two, see below)
// (after v107z) his decision of 2026-11-16: the Dreamcast keeps its 480i / 576i mode apart from its 240p / 288p one (the
// buttons are not the same: 720p x3 and 1080p x4 / x5 in 240p, one 1080p with its 4:3 / 16:9 in 480i). Two more
// records after the others, 60 Hz and 50 Hz, so the file of v107z still reads the same.
#define CM_DC_INTERLACED CM_RECORDS
#define CM_OTHERS (CM_RECORDS + 2) // (after v108e) «Otras», 60 Hz and 50 Hz
#define CM_ALL (CM_RECORDS + 4)
static const char *CM_FILE = "/cmode.bin";
static const uint8_t CM_VERSION = 1;

static ConsoleMode recs[CM_ALL];
static uint8_t lastKept = 0xFF;    // the record written last: the console that was on when the GBS was switched off
static int16_t lastSeen = -1;      // the console / norm the check last looked at
static int16_t restoreKey = -1;    // a load waiting for the main loop
static int16_t justRestored = -1;  // the record just written back: it is never written back twice in a row
static uint8_t pendingWidth = 0;   // the NTSC width the next load places
static uint32_t saveAt = 0;
static uint32_t nextTick = 0;

static bool isPal(int16_t key)
{
    return (key & 1) != 0;
}

// the record of the console on screen, or -1 when there is no console to keep anything for
static int16_t keyNow()
{
    if (!rto->boardHasPower || rto->sourceDisconnected)
        return -1;
    const uint8_t vs = rto->videoStandardInput;
    if (vs < 1 || vs > 4)
        return -1; // consoles only (240p / 288p, 480i / 576i, 480p / 576p), never a PC
    if (uopt->consoleProfile == 0 && !rto->consoleDetectIsFresh)
        return -1; // Auto, and this console has not been measured since the last load
    const uint8_t prof = settingsProfile();
    if (prof == 0 || prof >= CONSOLE_PROFILE_COUNT)
        return -1; // the unknown console keeps nothing (his decision: "el resto de consolas ahora nos dan igual")
    // (after v107z) the Dreamcast through VGA keeps nothing until it has modes of its own: it used to share the 60 Hz
    // record of its 240p, so plugging the VGA in loaded the 240p mode on it
    if (prof == CONSOLE_PROFILE_DREAMCAST && dcSignal() == 5)
        return -1;
    const uint8_t pal = (vs == 2 || vs == 4) ? 1 : 0;
    if (prof == CONSOLE_PROFILE_DREAMCAST && (dcSignal() == 3 || dcSignal() == 4))
        return (int16_t)(CM_DC_INTERLACED + pal);
    if (prof == CONSOLE_PROFILE_OTHERS)
        return (int16_t)(CM_OTHERS + pal);
    return (int16_t)((prof - 1) * 2 + pal);
}

// only what decides the picture for that family of modes, so that a value left behind by another mode never makes two
// identical modes look different
static void normalize(ConsoleMode &m, bool pal)
{
    ConsoleMode n;
    memset(&n, 0, sizeof(n));
    n.valid = 1;
    n.pref = m.pref;
    if (m.pref == OutputCustomized) {
        n.slot = m.slot;
    } else if (m.pref == Output720P && m.i720) {
        n.i720 = m.i720;
        if (pal) {
            n.step = m.step & 3;
        } else {
            n.centered = m.centered;
            n.fill = m.fill;
        }
    } else if (m.pref == Output1080P && m.i1080) {
        n.i1080 = m.i1080;
        n.narrow = m.narrow;
        n.spare = m.spare; // (after v108h) its 4:3
        if (pal) {
            n.step = m.step & 3;
        } else {
            n.centered = m.centered;
            n.wide = m.wide;
        }
    } else if ((m.pref == Output960P || m.pref == Output480P) && m.iLow) {
        n.iLow = m.iLow;
        n.narrow = m.narrow;
        if (!pal)
            n.centered = m.centered;
    }
    m = n;
}

// the mode on screen; false while it is not settled (a load under way, a slot chosen but not loaded yet)
static bool snapshotNow(ConsoleMode &m, bool pal)
{
    memset(&m, 0, sizeof(m));
    m.valid = 1;
    m.pref = (uint8_t)uopt->presetPreference;
    if (m.pref == OutputCustomized) {
        if (!rto->isCustomPreset)
            return false;
        m.slot = uopt->presetSlot;
        normalize(m, pal);
        return true;
    }
    if (rto->isCustomPreset)
        return false;
    if (m.pref == OutputBypass) {
        if (!rto->outModeHdBypass)
            return false;
    } else if (m.pref == Output720P && uopt->integer720) {
        if (!rto->integer720Active || rto->integer1080Centered == 0)
            return false;
        m.i720 = uopt->integer720;
        m.fill = uopt->integer720Fill;
    } else if (m.pref == Output1080P && uopt->integer1080) {
        if (!rto->integer1080Active || rto->integer1080Centered == 0)
            return false;
        m.i1080 = uopt->integer1080;
        m.wide = uopt->integer1080Wide;
        m.narrow = uopt->integer1080Narrow;
        m.spare = uopt->integer1080Tv43 ? 1 : 0; // (after v108h)
    } else if ((m.pref == Output960P || m.pref == Output480P) && uopt->integerLow) {
        if (!rto->integerLowActive || rto->integer1080Centered == 0)
            return false;
        m.iLow = uopt->integerLow;
        m.narrow = uopt->integer1080Narrow;
    }
    m.step = uopt->sizeStep;
    m.centered = rto->integer1080Centered;
    normalize(m, pal);
    return true;
}

static bool sameMode(const ConsoleMode &a, const ConsoleMode &b)
{
    return memcmp(&a, &b, sizeof(ConsoleMode)) == 0;
}

// a personal preset is only brought back if its file is still there for this kind of signal
static bool slotStillThere(uint8_t slot)
{
    const uint8_t vs = rto->videoStandardInput;
    const char *kind = (vs == 1) ? "/preset_ntsc." : (vs == 2) ? "/preset_pal." : (vs == 3) ? "/preset_ntsc_480p." : "/preset_pal_576p.";
    return LittleFS.exists(String(kind) + String((char)slot));
}

static void saveFile()
{
    File f = LittleFS.open(CM_FILE, "w");
    if (!f) {
        SerialM.println(F("[3x4x-v111] console modes: could not open the file"));
        return;
    }
    f.write(CM_VERSION);
    f.write(lastKept);
    f.write((const uint8_t *)recs, sizeof(recs));
    f.close();
}

void consoleModesLoad()
{
    memset(recs, 0, sizeof(recs));
    lastKept = 0xFF;
    File f = LittleFS.open(CM_FILE, "r");
    if (!f)
        return;
    if (f.read() == CM_VERSION) {
        lastKept = (uint8_t)f.read();
        f.read((uint8_t *)recs, sizeof(recs)); // a shorter file leaves the rest empty
    }
    f.close();
    for (uint8_t i = 0; i < CM_ALL; i++) {
        if (recs[i].valid != 1)
            memset(&recs[i], 0, sizeof(ConsoleMode));
    }
    // The GBS starts in the mode it was switched off in, which is the last console's. In NTSC the width of that mode was
    // never kept across a load (each load placed 320 px, or 256 px with the SNES), so it is taken from that console's
    // record: if it is the same console again, nothing has to be loaded a second time once it is recognised.
    if (lastKept >= CM_ALL || !recs[lastKept].valid || isPal(lastKept))
        return;
    const ConsoleMode &r = recs[lastKept];
    if (r.pref != (uint8_t)uopt->presetPreference)
        return;
    if (r.pref == Output720P && r.i720 && r.i720 == uopt->integer720) {
        pendingWidth = r.centered;
        uopt->integer720Fill = r.fill;
    } else if (r.pref == Output1080P && r.i1080 && r.i1080 == uopt->integer1080) {
        pendingWidth = r.centered;
        uopt->integer1080Wide = r.wide;
        uopt->integer1080Narrow = r.narrow;
        uopt->integer1080Tv43 = r.spare ? 1 : 0; // (after v108h)
    } else if ((r.pref == Output960P || r.pref == Output480P) && r.iLow && r.iLow == uopt->integerLow) {
        pendingWidth = r.centered;
        uopt->integer1080Narrow = r.narrow;
    }
}

bool consoleModeCheck()
{
    const int16_t key = keyNow();
    if (key < 0) {
        lastSeen = -1;
        return false;
    }
    if (key == lastSeen)
        return false; // same console and norm: a mode the user just chose is never undone here
    lastSeen = key;
    if (key == justRestored) {
        // the measurement after our own load: whatever came out of it is accepted (and kept by the tick). Without this,
        // a mode that does not come back exactly as recorded would be loaded again at every measurement.
        justRestored = -1;
        return false;
    }
    justRestored = -1;
    if (!recs[key].valid)
        return false; // nothing kept yet: it stays as it is, and what it is now becomes its own
    ConsoleMode cur;
    const bool settled = snapshotNow(cur, isPal(key));
    if (settled && sameMode(cur, recs[key]))
        return false;
    if (recs[key].pref == OutputCustomized && !slotStillThere(recs[key].slot)) {
        SerialM.println(F("[3x4x-v111] console mode: its personal preset is gone, the mode stays as it is"));
        return false;
    }
    restoreKey = key;
    return true;
}

static void restoreNow(int16_t key)
{
    const ConsoleMode r = recs[key];
    const bool pal = isPal(key);
    linesSetDolor(0xFF); // DOLOR is never remembered
    uopt->presetPreference = (PresetPreference)r.pref;
    if (r.pref == OutputCustomized) {
        uopt->presetSlot = r.slot;
    } else if (r.pref == Output720P) {
        uopt->integer720 = r.i720;
        uopt->integer720Fill = r.fill;
        uopt->integer1080Wide = 0;
        uopt->integerLow = 0;
        uopt->integer1080Narrow = 0;
        if (r.i720 == 1 || r.i720 == 4)
            uopt->x3Wide = (r.i720 == 4) ? 1 : 0;
    } else if (r.pref == Output1080P) {
        uopt->integer1080 = r.i1080;
        uopt->integer1080Wide = r.wide;
        uopt->integer1080Narrow = r.narrow;
        uopt->integer1080Tv43 = r.spare ? 1 : 0; // (after v108h)
    } else if (r.pref == Output960P || r.pref == Output480P) {
        uopt->integerLow = r.iLow;
        uopt->integer720 = 0;
        uopt->integer1080 = 0;
        uopt->integer720Fill = 0;
        uopt->integer1080Wide = 0;
        uopt->integer1080Narrow = r.narrow;
    }
    if (pal)
        uopt->sizeStep = r.step & 3;
    else
        pendingWidth = r.centered;
    saveUserPrefs(); // first: a personal preset is read from the slot the preferences file names
    SerialM.print(F("[3x4x-v111] console mode: "));
    if (key >= CM_OTHERS)
        SerialM.print(F("Others"));
    else if (key >= CM_DC_INTERLACED)
        SerialM.print(pal ? F("Dreamcast 576i") : F("Dreamcast 480i"));
    else
        SerialM.print(consoleProfileName((uint8_t)(key / 2 + 1)));
    SerialM.print(pal ? F(" 50 Hz") : F(" 60 Hz"));
    SerialM.println(F(", back to the mode it was last used in"));
    justRestored = key;
    rto->useHdmiSyncFix = 1;
    applyPresets(rto->videoStandardInput);
    lastSeen = keyNow(); // -1 until it is measured again; the check then finds the same console and leaves it
}

void consoleModeTick()
{
    const uint32_t now = millis();
    if (saveAt != 0 && (int32_t)(now - saveAt) >= 0) {
        saveAt = 0;
        saveFile();
    }
    if ((int32_t)(now - nextTick) < 0)
        return;
    nextTick = now + 1000;
    if (restoreKey >= 0) {
        const int16_t k = restoreKey;
        restoreKey = -1;
        if (k == keyNow()) // still the same console and norm
            restoreNow(k);
        return;
    }
    const int16_t key = keyNow();
    if (key < 0 || key != lastSeen)
        return; // the check has not looked at this console yet
    ConsoleMode cur;
    if (!snapshotNow(cur, isPal(key)))
        return;
    if (sameMode(cur, recs[key]) && lastKept == (uint8_t)key)
        return;
    recs[key] = cur;
    lastKept = (uint8_t)key;
    saveAt = now + 3000;
    if (saveAt == 0)
        saveAt = 1;
}

uint8_t consoleModePendingWidth()
{
    return pendingWidth;
}

uint16_t consoleModeLoadWidth(uint16_t def)
{
    if (pendingWidth == 1)
        return 320;
    if (pendingWidth == 2)
        return 256;
    return def;
}

void consoleModeLoadDone()
{
    pendingWidth = 0;
}
