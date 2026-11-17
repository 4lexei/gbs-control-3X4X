#include "ColorPresets.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/tv5725.h"
#include "Integer1080.h"
#include "ImageTuning.h"
#include "Scanlines.h"
#include "../commands/UserCommandHandler.h"
#include "../storage/PreferencesManager.h"
#include <LittleFS.h>

typedef TV5725<GBS_ADDR> GBS;

// The stored picture. Keep the field order: it is the file format too.
struct ColorPreset
{
    uint8_t valid;
    uint8_t uCos;  // saturation (Pb / U)
    uint8_t vCos;  // saturation (Pr / V)
    uint8_t blevByps;
    uint8_t blevLevel;
    uint8_t blevGain;
    uint8_t pkLb;  // peaking fine (luma low band)
    uint8_t pkLh;  // peaking fine (luma high band)
    // v64: white balance and colour cast are stored as DIFFERENCES against what the preset loaded (int8 in these bytes).
    // The ADC gains are per-preset, per-source calibration (Auto Gain moves them), so keeping absolute values and replaying
    // them over another preset dragged one source's calibration into another and tinted the picture (v62 / v63 bug).
    uint8_t dR;
    uint8_t dG;
    uint8_t dB;
    uint8_t dU;
    uint8_t dV;
    uint8_t peakLevel; // v107j: no longer used (the four-level cycle left the web); written as 1
    uint8_t stepLevel; // v107j: now the fine step-response gain, as 0x10 | gain (0..15). 0..3 = an old cycle level
    uint8_t adcFilterLevel;
    // v72: ADC black level (the analog offsets). These are stored ABSOLUTE, unlike the gains: nothing else moves them
    // (Auto Gain touches the gains only), and they are per source, which is exactly what a per-console slot is for.
    // offValid tells a slot written by v72 from one carried over without them, so an old slot never writes zeros.
    uint8_t offValid;
    uint8_t offR;
    uint8_t offG;
    uint8_t offB;
    // v74: the colour matrix. uCos / vCos above are what is ON the registers; these are the saturation at hue zero plus
    // the rotation, which is what has to be restored so the two do not fight.
    uint8_t hueValid;
    uint8_t hueStep; // int8 in this byte, one step = 3 degrees
    uint8_t satU;
    uint8_t satV;
    // v107p: brightness and contrast, absolute (what the Luz buttons move)
    uint8_t ycValid;
    uint8_t yOfst;
    uint8_t yGain;
    // v107p: the Filtros tab, saved with the colours by the same "guardar" button. (An automatic memory was tried and
    // dropped: saving everything by itself turns the last temporary change, say the brightness raised for one dark
    // game, into the console's setting. The console keeps what the user SAVES; temporary changes stay temporary.)
    uint8_t fValid;
    uint8_t scan;     // scanlines on / off (the wish: DOLOR keeps them off without touching it)
    uint8_t scanStr;  // scanline strength
    uint8_t lineF;    // vertical line filter (the user's choice, not the one the x5 modes force)
    uint8_t peakOn;
    uint8_t stepOn;
    uint8_t deint;    // 0 motion adaptive, 1 bob
};

static uint8_t clamp8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static ColorPreset slots[COLOR_PRESET_SLOTS];
#define COLOR_PRESET_FIELDS_V4 24 // v74: the old record (/colorv4.txt)
#define COLOR_PRESET_FIELDS ((uint8_t)sizeof(ColorPreset)) // v107p: 34 bytes, written as hex pairs in /colorv5.txt

// v62: which slot the active console profile uses. 1 = Mega Drive, 2 = SNES / NES, 3 = PC Engine; slot 0 is the one
// for "no console known".
// v107p: through settingsProfile(), which really returns 0 for a console the detection does not know, so a PlayStation
// or anything else no longer writes over the Mega Drive's picture. Whatever the user does with it may be overwritten by
// the next unknown console; what he wants to keep goes into a saved profile.
static uint8_t activeSlot()
{
    const uint8_t prof = settingsProfile();
    return (prof < COLOR_PRESET_SLOTS) ? prof : 0;
}

static String slotName(uint8_t slot)
{
    switch (slot) {
        case 1: return F("Mega Drive");
        case 2: return F("SNES");
        case 3: return F("PC Engine");
        default:
            // (after v107v) the profiles without measurements by their own name
            if (slot >= CONSOLE_PROFILE_FIRST_EMPTY && slot < CONSOLE_PROFILE_COUNT)
                return String(consoleProfileName(slot));
            return F("unknown console");
    }
}

static void saveToFile()
{
    File f = LittleFS.open("/colorv5.txt", "w");
    if (!f) {
        SerialM.println(F("colour presets: open file failed"));
        return;
    }
    const char *hex = "0123456789ABCDEF";
    for (uint8_t s = 0; s < COLOR_PRESET_SLOTS; s++) {
        const uint8_t *raw = (const uint8_t *)&slots[s];
        for (uint8_t i = 0; i < COLOR_PRESET_FIELDS; i++) {
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

void colorPresetsLoad()
{
    memset(slots, 0, sizeof(slots));
    // v107p: the 34-byte records; a v4 file (24 bytes, colours only) is read into the start of them, so the colour presets
    // saved by hand are kept and the new fields start empty (not applied until the console's memory writes them).
    uint8_t fields = COLOR_PRESET_FIELDS;
    File f = LittleFS.open("/colorv5.txt", "r");
    if (!f) {
        f = LittleFS.open("/colorv4.txt", "r");
        fields = COLOR_PRESET_FIELDS_V4;
    }
    if (!f) {
        return; // nothing saved yet
    }
    for (uint8_t s = 0; s < COLOR_PRESET_SLOTS; s++) {
        uint8_t raw[sizeof(ColorPreset)];
        memset(raw, 0, sizeof(raw));
        bool complete = true;
        for (uint8_t i = 0; i < fields; i++) {
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
        while (true) { // skip the end of the line
            const int c = f.read();
            if (c < 0 || c == '\n') {
                break;
            }
        }
    }
    f.close();
    colorPresetStatus();
}

// v107p: the picture as it is now, in a preset record (the save button)
static void captureCurrent(ColorPreset &p)
{
    memset(&p, 0, sizeof(ColorPreset));
    p.valid = 1;
    p.uCos = GBS::VDS_UCOS_GAIN::read();
    p.vCos = GBS::VDS_VCOS_GAIN::read();
    p.blevByps = GBS::VDS_BLEV_BYPS::read();
    p.blevLevel = GBS::VDS_BLEV_LEVEL::read();
    p.blevGain = GBS::VDS_BLEV_GAIN::read();
    p.pkLb = GBS::VDS_PK_LB_GAIN::read();
    p.pkLh = GBS::VDS_PK_LH_GAIN::read();
    uint8_t bR, bG, bB, bU, bV;
    imageTuningWbBase(&bR, &bG, &bB, &bU, &bV);
    p.dR = (uint8_t)(int8_t)((int)GBS::ADC_RGCTRL::read() - (int)bR);
    p.dG = (uint8_t)(int8_t)((int)GBS::ADC_GGCTRL::read() - (int)bG);
    p.dB = (uint8_t)(int8_t)((int)GBS::ADC_BGCTRL::read() - (int)bB);
    p.dU = (uint8_t)(int8_t)((int)GBS::VDS_U_OFST::read() - (int)bU);
    p.dV = (uint8_t)(int8_t)((int)GBS::VDS_V_OFST::read() - (int)bV);
    p.peakLevel = 1;
    p.stepLevel = (uint8_t)(0x10 | (GBS::VDS_STEP_GAIN::read() & 0x0f)); // v107j: the fine step response
    p.adcFilterLevel = uopt->adcFilterLevel;
    imageTuningAdcOffsets(&p.offR, &p.offG, &p.offB); // v72
    p.offValid = 1;
    { // v74
        int8_t step = 0;
        imageTuningHue(&step, &p.satU, &p.satV);
        p.hueStep = (uint8_t)step;
        p.hueValid = 1;
    }
    p.ycValid = 1;
    p.yOfst = GBS::VDS_Y_OFST::read();
    p.yGain = GBS::VDS_Y_GAIN::read();
    p.fValid = 1;
    p.scan = uopt->wantScanlines ? 1 : 0;
    p.scanStr = uopt->scanlineStrength;
    p.lineF = (rto->integer1080LfForced ? rto->integer1080LfPrev : uopt->wantVdsLineFilter) ? 1 : 0;
    p.peakOn = uopt->wantPeaking ? 1 : 0;
    p.stepOn = uopt->wantStepResponse ? 1 : 0;
    p.deint = uopt->deintMode ? 1 : 0;
}

void colorPresetSaveCurrent()
{
    const uint8_t s = activeSlot();
    captureCurrent(slots[s]);
    saveToFile();
    SerialM.print(F("[3x4x-v111] colour preset saved for "));
    SerialM.println(slotName(s));
}

// v107p: the Filtros part of a record, put back the way the preset load puts these switches (GBSController, after
// applyPresets): the flags first, then the register each one drives.
static void applyFilters(const ColorPreset &p)
{
    // line filter: in the x5 modes it is forced on, and what the user chose waits in integer1080LfPrev
    if (rto->integer1080LfForced)
        rto->integer1080LfPrev = p.lineF;
    else
        uopt->wantVdsLineFilter = p.lineF;
    GBS::VDS_D_RAM_BYPS::write(uopt->wantVdsLineFilter ? 0 : 1);
    uopt->wantPeaking = p.peakOn;
    GBS::VDS_PK_Y_H_BYPS::write(p.peakOn ? 0 : 1);
    uopt->wantStepResponse = p.stepOn;
    GBS::VDS_UV_STEP_BYPS::write((p.stepOn && rto->presetID != 0x05 && rto->presetID != 0x15) ? 0 : 1);
    uopt->scanlineStrength = p.scanStr;
    if (uopt->wantScanlines != p.scan) {
        uopt->wantScanlines = p.scan;
        if (!p.scan)
            disableScanlines(); // switching them on is left to the sync watcher, which knows when the source allows them
    }
    if (rto->scanlinesEnabled) {
        GBS::MADPT_Y_MI_OFFSET::write(uopt->scanlineStrength);
        GBS::MADPT_UV_MI_OFFSET::write(uopt->scanlineStrength);
    }
    if (uopt->deintMode != p.deint)
        handleType2Command(p.deint ? 'q' : 'r'); // the deinterlacer has its own switching sequence
    saveUserPrefs();
}

void colorPresetApply()
{
    const uint8_t s = activeSlot();
    const ColorPreset &p = slots[s];
    if (!p.valid) {
        return;
    }
    if (p.ycValid) { // v107p
        GBS::VDS_Y_OFST::write(p.yOfst);
        GBS::VDS_Y_GAIN::write(p.yGain);
    }
    if (p.hueValid) { // v74: saturation and rotation together, through the matrix writer
        imageTuningSetHue((int8_t)p.hueStep, p.satU, p.satV);
    } else {
        GBS::VDS_UCOS_GAIN::write(p.uCos);
        GBS::VDS_VCOS_GAIN::write(p.vCos);
    }
    GBS::VDS_BLEV_LEVEL::write(p.blevLevel);
    GBS::VDS_BLEV_GAIN::write(p.blevGain);
    GBS::VDS_BLEV_BYPS::write(p.blevByps);
    GBS::VDS_PK_LB_GAIN::write(p.pkLb);
    GBS::VDS_PK_LH_GAIN::write(p.pkLh);
    uint8_t bR, bG, bB, bU, bV;
    imageTuningWbBase(&bR, &bG, &bB, &bU, &bV);
    GBS::VDS_U_OFST::write(clamp8((int)bU + (int8_t)p.dU));
    GBS::VDS_V_OFST::write(clamp8((int)bV + (int8_t)p.dV));
    // the gains are left alone while Auto Gain is running: it would fight them anyway
    const bool gains = (uopt->enableAutoGain == 0);
    if (gains) {
        adco->r_gain = clamp8((int)bR + (int8_t)p.dR);
        adco->g_gain = clamp8((int)bG + (int8_t)p.dG);
        adco->b_gain = clamp8((int)bB + (int8_t)p.dB);
        GBS::ADC_RGCTRL::write(adco->r_gain);
        GBS::ADC_GGCTRL::write(adco->g_gain);
        GBS::ADC_BGCTRL::write(adco->b_gain);
    }
    // v107j: the four-level cycles are gone from the web, so a level stored by an older preset is not brought back: it would
    // scale the next preset load with nothing on screen to show it. Peaking is restored by pkLb / pkLh above; the step
    // response gain, when this preset stored one, comes from its old stepLevel byte.
    uopt->peakLevel = 1;
    uopt->stepLevel = 1;
    if (p.stepLevel & 0x10)
        GBS::VDS_STEP_GAIN::write(p.stepLevel & 0x0f);
    uopt->adcFilterLevel = p.adcFilterLevel;
    if (p.offValid) { // v72: the analog black level, absolute. adco is set too, or the next preset load would undo it
        adco->r_off = p.offR;
        adco->g_off = p.offG;
        adco->b_off = p.offB;
        GBS::ADC_ROFCTRL::write(p.offR);
        GBS::ADC_GOFCTRL::write(p.offG);
        GBS::ADC_BOFCTRL::write(p.offB);
    }
    if (p.fValid) // v107p
        applyFilters(p);
    SerialM.print(F("[3x4x-v111] colour preset applied for "));
    SerialM.print(slotName(s));
    SerialM.print(F(" (white balance "));
    SerialM.print((int8_t)p.dR);
    SerialM.print('/');
    SerialM.print((int8_t)p.dG);
    SerialM.print('/');
    SerialM.print((int8_t)p.dB);
    SerialM.println(gains ? F(" applied)") : F(" left out: Auto Gain is on)"));
}

void colorPresetClear()
{
    const uint8_t s = activeSlot();
    memset(&slots[s], 0, sizeof(ColorPreset));
    saveToFile();
    // v107p: "volver a lo de serie" for this console: the filters back to the firmware defaults as well
    {
        ColorPreset d;
        memset(&d, 0, sizeof(ColorPreset));
        d.scan = 0;
        d.scanStr = 0x30;
        d.lineF = 0;
        d.peakOn = 1;
        d.stepOn = 1;
        d.deint = 0;
        applyFilters(d);
    }
    // v73: forgetting the slot used to leave whatever was on the chip in place. The ADC black level is the one that really
    // stuck, because it is written into adco and re-applied after every preset load, so it survived until a reboot.
    resetAdcOffset();
    resetHue(); // v74
    resetImageFine();
    SerialM.print(F("[3x4x-v111] colour preset removed for "));
    SerialM.println(slotName(s));
}

// (after v107v) EXPORT / IMPORT ONE CONSOLE, his request: separate per console, in case one day it is shared with others.
// The web asks for the export, waits, and downloads /colorexp.txt (two lines: the console slot and the record as hex, or
// an empty second line when that console has nothing saved); it wraps that in its own file with the console's name.
// For the import it uploads /colorimp.txt in the same two-line form and asks for it to be read: the record goes into
// THE CONSOLE NAMED IN THE FILE (not the active one), is saved, and is applied at once if that console is on screen.
void colorPresetExportActive()
{
    const uint8_t s = activeSlot();
    File f = LittleFS.open("/colorexp.txt", "w");
    if (!f) {
        SerialM.println(F("[3x4x-v111] colour export: could not open the file"));
        return;
    }
    f.print(s);
    f.write('\n');
    if (slots[s].valid) {
        const char *hex = "0123456789ABCDEF";
        const uint8_t *raw = (const uint8_t *)&slots[s];
        for (uint8_t i = 0; i < COLOR_PRESET_FIELDS; i++) {
            f.write(hex[(raw[i] >> 4) & 0x0f]);
            f.write(hex[raw[i] & 0x0f]);
        }
    }
    f.write('\n');
    f.close();
    SerialM.print(F("[3x4x-v111] colour export ready for "));
    SerialM.println(slotName(s));
}

void colorPresetImportFile()
{
    File f = LittleFS.open("/colorimp.txt", "r");
    if (!f) {
        SerialM.println(F("[3x4x-v111] colour import: no file"));
        return;
    }
    String first = f.readStringUntil('\n');
    String data = f.readStringUntil('\n');
    f.close();
    LittleFS.remove("/colorimp.txt");
    first.trim();
    data.trim();
    const int s = first.toInt();
    if (first.length() < 1 || first.length() > 2 || s < 0 || s >= COLOR_PRESET_SLOTS || data.length() != (unsigned)(COLOR_PRESET_FIELDS * 2)) {
        SerialM.println(F("[3x4x-v111] colour import: the file is not valid"));
        return;
    }
    uint8_t raw[sizeof(ColorPreset)];
    for (uint8_t i = 0; i < COLOR_PRESET_FIELDS; i++)
        raw[i] = (uint8_t)((nibble(data[2 * i]) << 4) | nibble(data[2 * i + 1]));
    if (raw[0] == 0) { // the "valid" byte: an export of a console with nothing saved
        SerialM.println(F("[3x4x-v111] colour import: the file holds no settings"));
        return;
    }
    memcpy(&slots[s], raw, sizeof(raw));
    saveToFile();
    SerialM.print(F("[3x4x-v111] colour import saved for "));
    SerialM.println(slotName((uint8_t)s));
    if ((uint8_t)s == activeSlot())
        colorPresetApply();
}

void colorPresetStatus()
{
    SerialM.print(F("[3x4x-v111] colour presets:"));
    bool any = false;
    for (uint8_t s = 0; s < COLOR_PRESET_SLOTS; s++) {
        if (slots[s].valid) {
            SerialM.print(' ');
            SerialM.print(slotName(s));
            any = true;
        }
    }
    if (!any) {
        SerialM.print(F(" none saved yet"));
    }
    SerialM.println();
}
