#include "SyncPresets.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/tv5725.h"
#include "../gbs/GBSPhase.h"
#include "Integer1080.h"
#include "SyncWatcher.h"
#include <LittleFS.h>

typedef TV5725<GBS_ADDR> GBS;

// The stored state. Keep the field order: it is the file format too.
struct SyncPreset
{
    uint8_t valid;
    uint8_t sogLevel;
    uint8_t sogMode;
    uint8_t cstStLo; // SP_H_CST_ST (12 bits)
    uint8_t cstStHi;
    uint8_t cstSpLo; // SP_H_CST_SP (12 bits)
    uint8_t cstSpHi;
    uint8_t preCoast;
    uint8_t postCoast;
    uint8_t pulseIgnore;
    uint8_t hProtect;
    uint8_t noCoast;
    uint8_t phaseSP;
    uint8_t phaseADC;
    uint8_t coastWasSet;  // the flags decide whether the watcher recomputes what was captured
    uint8_t clampWasSet;
};

#define SYNC_PRESET_FIELDS 16

static SyncPreset slots[SYNC_PRESET_SLOTS];

static uint8_t activeSlot()
{
    // (after v107v) a profile without measurements keeps a sync state of its own; the first four are as before (an unknown
    // console still shares the Mega Drive's, exactly as it did)
    const uint8_t own = settingsProfile();
    const uint8_t prof = (own >= CONSOLE_PROFILE_FIRST_EMPTY) ? own : activeConsoleProfile();
    return (prof < SYNC_PRESET_SLOTS) ? prof : 0;
}

static String slotName(uint8_t slot)
{
    switch (slot) {
        case 1: return F("Mega Drive");
        case 2: return F("SNES");
        case 3: return F("PC Engine");
        default:
            if (slot >= CONSOLE_PROFILE_FIRST_EMPTY && slot < CONSOLE_PROFILE_COUNT)
                return String(consoleProfileName(slot));
            return F("no profile");
    }
}

static void saveToFile()
{
    File f = LittleFS.open("/syncv1.txt", "w");
    if (!f) {
        SerialM.println(F("sync states: open file failed"));
        return;
    }
    const char *hex = "0123456789ABCDEF";
    for (uint8_t s = 0; s < SYNC_PRESET_SLOTS; s++) {
        const uint8_t *raw = (const uint8_t *)&slots[s];
        for (uint8_t i = 0; i < SYNC_PRESET_FIELDS; i++) {
            f.write(hex[(raw[i] >> 4) & 0x0f]);
            f.write(hex[raw[i] & 0x0f]);
        }
        f.write('\n');
    }
    f.close();
}

static uint8_t nibble(int c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

void syncPresetsLoad()
{
    memset(slots, 0, sizeof(slots));
    File f = LittleFS.open("/syncv1.txt", "r");
    if (!f) {
        return;
    }
    for (uint8_t s = 0; s < SYNC_PRESET_SLOTS; s++) {
        uint8_t raw[SYNC_PRESET_FIELDS];
        memset(raw, 0, sizeof(raw));
        bool complete = true;
        for (uint8_t i = 0; i < SYNC_PRESET_FIELDS; i++) {
            const int hi = f.read();
            const int lo = f.read();
            if (hi < 0 || lo < 0) {
                complete = false;
                break;
            }
            raw[i] = (uint8_t)((nibble(hi) << 4) | nibble(lo));
        }
        if (!complete) {
            break;
        }
        memcpy(&slots[s], raw, sizeof(raw));
        while (true) {
            const int c = f.read();
            if (c < 0 || c == '\n') {
                break;
            }
        }
    }
    f.close();
    for (uint8_t s = 0; s < SYNC_PRESET_SLOTS; s++) {
        if (slots[s].valid) {
            SerialM.print(F("[3x4x-v122] sync state saved for "));
            SerialM.println(slotName(s));
        }
    }
}

bool syncPresetAvailable()
{
    return slots[activeSlot()].valid != 0;
}

void syncPresetCaptureNow()
{
    const uint8_t s = activeSlot();
    SyncPreset &p = slots[s];
    p.valid = 1;
    p.sogLevel = rto->currentLevelSOG;
    p.sogMode = GBS::SP_SOG_MODE::read();
    const uint16_t cstSt = GBS::SP_H_CST_ST::read();
    const uint16_t cstSp = GBS::SP_H_CST_SP::read();
    p.cstStLo = cstSt & 0xff;
    p.cstStHi = (cstSt >> 8) & 0x0f;
    p.cstSpLo = cstSp & 0xff;
    p.cstSpHi = (cstSp >> 8) & 0x0f;
    p.preCoast = GBS::SP_PRE_COAST::read();
    p.postCoast = GBS::SP_POST_COAST::read();
    p.pulseIgnore = GBS::SP_H_PULSE_IGNOR::read();
    p.hProtect = GBS::SP_H_PROTECT::read();
    p.noCoast = GBS::SP_NO_COAST_REG::read();
    p.phaseSP = rto->phaseSP;
    p.phaseADC = rto->phaseADC;
    p.coastWasSet = rto->coastPositionIsSet ? 1 : 0;
    p.clampWasSet = rto->clampPositionIsSet ? 1 : 0;
    rto->syncPresetActive = true; // v70: from now on the recovery leaves these values alone
    saveToFile();
    SerialM.print(F("[3x4x-v122] sync state captured for "));
    SerialM.print(slotName(s));
    SerialM.print(F(": SOG level "));
    SerialM.print(p.sogLevel);
    SerialM.print(F(", coast "));
    SerialM.print(cstSt);
    SerialM.print('/');
    SerialM.print(cstSp);
    SerialM.print(F(" set "));
    SerialM.print(p.coastWasSet);
    SerialM.print(F(", pulse ignore "));
    SerialM.print(p.pulseIgnore);
    SerialM.print(F(", phase SP/ADC "));
    SerialM.print(p.phaseSP);
    SerialM.print('/');
    SerialM.print(p.phaseADC);
    SerialM.println(F(". It is written back after every preset load"));
}

static void writeState(uint8_t sogLevel, uint16_t cstSt, uint16_t cstSp, uint8_t preCoast, uint8_t postCoast,
                       uint8_t pulseIgnore, uint8_t hProtect, uint8_t noCoast, bool coastWasSet, bool clampWasSet)
{
    rto->currentLevelSOG = sogLevel;
    setAndUpdateSogLevel(sogLevel);
    GBS::SP_H_CST_ST::write(cstSt);
    GBS::SP_H_CST_SP::write(cstSp);
    GBS::SP_PRE_COAST::write(preCoast);
    GBS::SP_POST_COAST::write(postCoast);
    GBS::SP_H_PULSE_IGNOR::write(pulseIgnore);
    GBS::SP_H_PROTECT::write(hProtect);
    GBS::SP_NO_COAST_REG::write(noCoast);
    // the flags are what keeps the watcher from recomputing the coast / clamp that were captured
    rto->coastPositionIsSet = coastWasSet;
    rto->clampPositionIsSet = clampWasSet;
}

void syncPresetApply(bool quiet)
{
    const uint8_t s = activeSlot();
    const SyncPreset &p = slots[s];
    if (!p.valid) {
        if (!quiet) {
            SerialM.print(F("[3x4x-v122] no sync state saved for "));
            SerialM.println(slotName(s));
        }
        return;
    }
    const uint16_t cstSt = (uint16_t)p.cstStLo | ((uint16_t)(p.cstStHi & 0x0f) << 8);
    const uint16_t cstSp = (uint16_t)p.cstSpLo | ((uint16_t)(p.cstSpHi & 0x0f) << 8);
    writeState(p.sogLevel, cstSt, cstSp, p.preCoast, p.postCoast, p.pulseIgnore, p.hProtect, p.noCoast,
               p.coastWasSet != 0, p.clampWasSet != 0);
    rto->phaseSP = p.phaseSP;
    rto->phaseADC = p.phaseADC;
    setAndUpdateSogLevel(rto->currentLevelSOG); // re-latches SP / ADPLL with the phase above
    rto->phaseIsSet = true;                     // do not search for a phase over the captured one
    SerialM.print(F("[3x4x-v122] sync state written for "));
    SerialM.print(slotName(s));
    SerialM.print(F(": SOG level "));
    SerialM.print(p.sogLevel);
    SerialM.print(F(", coast "));
    SerialM.print(cstSt);
    SerialM.print('/');
    SerialM.print(cstSp);
    SerialM.print(F(", pulse ignore "));
    SerialM.println(p.pulseIgnore);
    rto->syncPresetActive = true; // v70: from here on the recovery must leave the pulse ignore and the coast alone
    syncFinishSetup(); // v67: the clamp and the scanlines, which an early lock would otherwise never reach
}

void syncPresetClear()
{
    const uint8_t s = activeSlot();
    memset(&slots[s], 0, sizeof(SyncPreset));
    rto->syncPresetActive = false;
    saveToFile();
    SerialM.print(F("[3x4x-v122] sync state removed for "));
    SerialM.println(slotName(s));
}

void syncPresetPrintSaved()
{
    const uint8_t s = activeSlot();
    const SyncPreset &p = slots[s];
    SerialM.print(F("[3x4x-v122] saved sync state for "));
    SerialM.print(slotName(s));
    if (!p.valid) {
        SerialM.println(F(": none"));
        return;
    }
    const uint16_t cstSt = (uint16_t)p.cstStLo | ((uint16_t)(p.cstStHi & 0x0f) << 8);
    const uint16_t cstSp = (uint16_t)p.cstSpLo | ((uint16_t)(p.cstSpHi & 0x0f) << 8);
    SerialM.print(F(": SOG level "));
    SerialM.print(p.sogLevel);
    SerialM.print(F(", SOG mode "));
    SerialM.print(p.sogMode);
    SerialM.print(F(", coast "));
    SerialM.print(cstSt);
    SerialM.print('/');
    SerialM.print(cstSp);
    SerialM.print(F(" set "));
    SerialM.print(p.coastWasSet);
    SerialM.print(F(", pre/post coast "));
    SerialM.print(p.preCoast);
    SerialM.print('/');
    SerialM.print(p.postCoast);
    SerialM.print(F(", pulse ignore "));
    SerialM.print(p.pulseIgnore);
    SerialM.print(F(", protect "));
    SerialM.print(p.hProtect);
    SerialM.print(F(", no coast "));
    SerialM.print(p.noCoast);
    SerialM.print(F(", phase SP/ADC "));
    SerialM.print(p.phaseSP);
    SerialM.print('/');
    SerialM.print(p.phaseADC);
    SerialM.print(F(", clamp set "));
    SerialM.println(p.clampWasSet);
}

void syncPresetApplyOdvRecipe()
{
    // The state the user measured on v64 with the PC Engine through the ODV, in the one attempt that came out perfect:
    // SOG level 14, coast 16 / 256 left unfixed (the values of the recovery path), pulse ignore 51, clamp fixed.
    // It is not a promise: the picture also depends on what the chip latched at that moment, but it starts where it worked.
    // v69: the clamp is NOT claimed as already placed any more (that is what left the picture with a colour cast), and the
    // setup is finished afterwards, as loading a saved state does.
    writeState(14, 16, 256, 9, 9, 51, GBS::SP_H_PROTECT::read(), GBS::SP_NO_COAST_REG::read(), false, false);
    rto->syncPresetActive = true;
    syncFinishSetup();
    SerialM.println(F("[3x4x-v122] ODV recipe written: SOG level 14, coast 16/256 unfixed, pulse ignore 51. Capture it if the picture comes out right"));
}
