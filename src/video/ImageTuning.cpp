#include "ImageTuning.h"
#include <math.h>
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../storage/PreferencesManager.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSPhase.h"
#include "../gbs/tv5725.h"
#include "Integer1080.h" // v107n: the Líneas row in the data line

typedef TV5725<GBS_ADDR> GBS;

// Values the preset (or a custom preset) loaded; level changes are always relative to these,
// so cycling back to "stock" restores exactly what the preset set.
static bool baseValid = false;
static bool wbBaseValid = false;
static bool hueBaseValid = false; // v74: the colour matrix, so hue and saturation do not fight each other
static uint8_t satU = 0, satV = 0; // the COS gains at hue zero (what the saturation buttons set)
static int8_t hueStep = 0;         // one step = 3 degrees, range +/- 63
static bool offBaseValid = false; // v73: the ADC black level the preset loaded, so Reset color can put it back
static uint8_t offR = 0, offG = 0, offB = 0;
static uint8_t wbR = 0, wbG = 0, wbB = 0, wbU = 0, wbV = 0;
static uint8_t baseLB = 0, baseLH = 0, baseStep = 0, baseFltr = 0;
static uint8_t baseYOfst = 0, baseYGain = 0; // v107p: brightness and contrast as the preset loaded them

static void captureBase()
{
    baseLB = GBS::VDS_PK_LB_GAIN::read();
    baseLH = GBS::VDS_PK_LH_GAIN::read();
    baseStep = GBS::VDS_STEP_GAIN::read();
    baseFltr = GBS::ADC_FLTR::read();
    baseYOfst = GBS::VDS_Y_OFST::read();
    baseYGain = GBS::VDS_Y_GAIN::read();
    baseValid = true;
}

static uint8_t scaleGain(uint8_t base, uint8_t level)
{
    uint16_t v = base;
    switch (level) {
        case 0:
            v = (base * 5) / 10; // x0.5
            break;
        case 2:
            v = (base * 16) / 10; // x1.6
            break;
        case 3:
            v = (base * 19) / 10; // x1.9 (between 2 and 4)
            break;
        case 4:
            v = (base * 23) / 10; // x2.3
            break;
        default:
            break; // stock
    }
    if (v > 0x3f) {
        v = 0x3f;
    }
    return (uint8_t)v;
}

static uint8_t stepFor(uint8_t base, uint8_t level)
{
    uint16_t v = base;
    switch (level) {
        case 0:
            v = 0;
            break;
        case 2:
            v = base + 2;
            break;
        case 3:
            v = base + 5;
            break;
        default:
            break;
    }
    if (v > 15) {
        v = 15;
    }
    return (uint8_t)v;
}

static void writeLevels()
{
    GBS::VDS_PK_LB_GAIN::write(scaleGain(baseLB, uopt->peakLevel));
    GBS::VDS_PK_LH_GAIN::write(scaleGain(baseLH, uopt->peakLevel));
    GBS::VDS_STEP_GAIN::write(stepFor(baseStep, uopt->stepLevel));
    GBS::ADC_FLTR::write(uopt->adcFilterLevel == 0 ? baseFltr : uopt->adcFilterLevel);
}

void applyImageTuning()
{
    // the preset has just been applied: take its values as the new base
    captureBase();
    wbBaseValid = false;  // v54: new preset, new white balance base
    offBaseValid = false; // v73: and a new ADC black level base
    hueBaseValid = false; // v74: the preset brings its own colour matrix, so the hue starts at zero again
    if (uopt->peakLevel != 1 || uopt->stepLevel != 1 || uopt->adcFilterLevel != 0) {
        writeLevels();
    }
}

static uint8_t clampU8(int v, int hi)
{
    return (uint8_t)(v < 0 ? 0 : (v > hi ? hi : v));
}

// v52: peaking in steps of 1 on both luma gains (the cycle button works in big steps)
void nudgePeaking(int8_t delta)
{
    if (!baseValid) {
        captureBase();
    }
    GBS::VDS_PK_LB_GAIN::write(clampU8((int)GBS::VDS_PK_LB_GAIN::read() + delta, 0x3f));
    GBS::VDS_PK_LH_GAIN::write(clampU8((int)GBS::VDS_PK_LH_GAIN::read() + delta, 0x3f));
    SerialM.print(F("Peaking fine: gains "));
    SerialM.print(GBS::VDS_PK_LB_GAIN::read());
    SerialM.print(F(" / "));
    SerialM.println(GBS::VDS_PK_LH_GAIN::read());
}

// v107j: step response in steps of 1, like the fine peaking. It replaces the four-level cycle in the web: the user asked for
// one - / + per sharpening control, with the preset's own value shown as the reference.
void nudgeStepGain(int8_t delta)
{
    if (!baseValid) {
        captureBase();
    }
    GBS::VDS_STEP_GAIN::write(clampU8((int)GBS::VDS_STEP_GAIN::read() + delta, 15));
    SerialM.print(F("Step response fine: gain "));
    SerialM.print(GBS::VDS_STEP_GAIN::read());
    SerialM.print(F(" of 15 (preset "));
    SerialM.print(baseStep);
    SerialM.println(')');
}

// v52: black level (the chip's black level stage, bypassed by the presets): the first press switches it on with the
// preset's own values, then the level moves in steps of 2. Reset color switches it off again.
void nudgeBlackLevel(int8_t delta)
{
    if (GBS::VDS_BLEV_BYPS::read() == 1) {
        GBS::VDS_BLEV_AUTO_EN::write(0);
        GBS::VDS_BLEV_LEVEL::write(0x26);
        GBS::VDS_BLEV_GAIN::write(0x07);
        GBS::VDS_BLEV_BYPS::write(0);
        SerialM.println(F("Black level: stage switched on"));
    }
    GBS::VDS_BLEV_LEVEL::write(clampU8((int)GBS::VDS_BLEV_LEVEL::read() + delta * 2, 255));
    SerialM.print(F("Black level: "));
    SerialM.print(GBS::VDS_BLEV_LEVEL::read());
    SerialM.println(F(" (preset 38)"));
}

// v54: white balance (ADC gain per channel) and colour cast (U / V offset). The first press takes the current values as the
// base so Reset color can put them back. Auto Gain may overwrite the ADC gains again if it is switched on.

static void captureWb()
{
    if (wbBaseValid)
        return;
    wbR = GBS::ADC_RGCTRL::read();
    wbG = GBS::ADC_GGCTRL::read();
    wbB = GBS::ADC_BGCTRL::read();
    wbU = GBS::VDS_U_OFST::read();
    wbV = GBS::VDS_V_OFST::read();
    wbBaseValid = true;
}

// v59: EXPERIMENTAL "composite look": horizontal smoothing of the picture so dithering / patterns blend (blind: the chip has no
// documented per-pixel horizontal filter, these are its two horizontal filter stages and the ADC low-pass).
// 'l' output filter (VDS_TAP6) on / off, 'm' input filter (IF_HS_TAP11) on / off,
// 'n' back to the preset values, 'o' composite preset (both filters on, ADC 70 MHz, step response off).
void imageTuningWbBase(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *u, uint8_t *v)
{
    captureWb();
    if (r) *r = wbR;
    if (g) *g = wbG;
    if (b) *b = wbB;
    if (u) *u = wbU;
    if (v) *v = wbV;
}

// v72: ADC black level. The three offset registers are also what the boot calibration writes, and GBSController re-applies
// adco->r_off / g_off / b_off after every preset load, so both the register and adco are set here: otherwise the next preset
// load would undo it. Steps of 1 because these are the analog offset codes and a couple of counts are already visible.
// ---- v74: hue -------------------------------------------------------------------------------------------------
// The chip carries a full 2x2 colour matrix at 3_36..3_39 (UCOS, VCOS, USIN, VSIN). Until now the firmware wrote only
// the COS pair, which is the per-axis chroma gain the saturation buttons move, and left the SIN pair at zero. Writing
// all four turns it into a rotation:
//     U' = gu * (U*cos - V*sin)        V' = gv * (U*sin + V*cos)
// so the saturation stays where the user put it and the tone turns. The two have to go through the same writer, or
// pressing saturation would throw the rotation away.
// NOTE: the SIN registers are declared unsigned in tv5725.h. If the hardware reads them as two's complement, both
// directions work; if it reads them as unsigned, only one side will look right. The negative side is written as a
// two's complement byte so that the signed case is covered.
static void captureHueBase()
{
    if (hueBaseValid)
        return;
    satU = GBS::VDS_UCOS_GAIN::read();
    satV = GBS::VDS_VCOS_GAIN::read();
    hueStep = 0;
    hueBaseValid = true;
}

static uint8_t matrixByte(float v)
{
    int n = (int)lroundf(v);
    if (n > 127) n = 127;
    if (n < -128) n = -128;
    return (uint8_t)(n & 0xff);
}

static void writeColourMatrix()
{
    const float a = (float)hueStep * 3.0f * 0.01745329f;
    const float c = cosf(a);
    const float s = sinf(a);
    GBS::VDS_UCOS_GAIN::write(matrixByte((float)satU * c));
    GBS::VDS_VCOS_GAIN::write(matrixByte((float)satV * c));
    GBS::VDS_USIN_GAIN::write(matrixByte((float)satU * s));
    GBS::VDS_VSIN_GAIN::write(matrixByte((float)satV * s));
}

void nudgeHue(int8_t dir)
{
    captureHueBase();
    int n = hueStep + dir;
    if (n > 21) n = 21;
    if (n < -21) n = -21;
    hueStep = (int8_t)n;
    writeColourMatrix();
    SerialM.print(F("[3x4x-v111] hue "));
    SerialM.print(hueStep * 3);
    SerialM.print(F(" degrees (U/V cos "));
    SerialM.print(GBS::VDS_UCOS_GAIN::read());
    SerialM.print('/');
    SerialM.print(GBS::VDS_VCOS_GAIN::read());
    SerialM.print(F(", sin "));
    SerialM.print(GBS::VDS_USIN_GAIN::read());
    SerialM.print('/');
    SerialM.print(GBS::VDS_VSIN_GAIN::read());
    SerialM.println(')');
}

// v74: back to no rotation, keeping the saturation. Used by Reset color and by the hue reset button.
void resetHue()
{
    if (!hueBaseValid)
        return;
    hueStep = 0;
    GBS::VDS_USIN_GAIN::write(0);
    GBS::VDS_VSIN_GAIN::write(0);
    GBS::VDS_UCOS_GAIN::write(satU);
    GBS::VDS_VCOS_GAIN::write(satV);
    SerialM.println(F("[3x4x-v111] hue back to 0 degrees"));
}

// v74: saturation now goes through the matrix too. which: 0 = both, 1 = Pb/U, 2 = Pr/V.
void nudgeSaturation(uint8_t which, int8_t delta)
{
    captureHueBase();
    if (which == 0 || which == 1) {
        const int n = (int)satU + delta;
        satU = (uint8_t)(n < 0 ? 0 : (n > 127 ? 127 : n));
    }
    if (which == 0 || which == 2) {
        const int n = (int)satV + delta;
        satV = (uint8_t)(n < 0 ? 0 : (n > 127 ? 127 : n));
    }
    writeColourMatrix();
    SerialM.print(F("[3x4x-v111] saturation U/V "));
    SerialM.print(satU);
    SerialM.print('/');
    SerialM.print(satV);
    if (hueStep != 0) {
        SerialM.print(F(" at hue "));
        SerialM.print(hueStep * 3);
        SerialM.print(F(" degrees"));
    }
    SerialM.println();
}

void imageTuningHue(int8_t *step, uint8_t *u, uint8_t *v)
{
    if (step) *step = hueBaseValid ? hueStep : 0;
    if (u) *u = hueBaseValid ? satU : GBS::VDS_UCOS_GAIN::read();
    if (v) *v = hueBaseValid ? satV : GBS::VDS_VCOS_GAIN::read();
}

void imageTuningSetHue(int8_t step, uint8_t u, uint8_t v)
{
    satU = u;
    satV = v;
    hueStep = step;
    hueBaseValid = true;
    writeColourMatrix();
}

static void captureAdcOffsetBase()
{
    if (offBaseValid)
        return;
    offR = GBS::ADC_ROFCTRL::read();
    offG = GBS::ADC_GOFCTRL::read();
    offB = GBS::ADC_BOFCTRL::read();
    offBaseValid = true;
    SerialM.print(F("[3x4x-v111] ADC black level base taken: "));
    SerialM.print(offR);
    SerialM.print('/');
    SerialM.print(offG);
    SerialM.print('/');
    SerialM.print(offB);
    SerialM.println(F(" (Reset color returns here; write it down to go back by hand)"));
}

// v73: back to the base taken the first time a black level button was pressed. Does nothing if none ever was.
void resetAdcOffset()
{
    if (!offBaseValid)
        return;
    adco->r_off = offR;
    adco->g_off = offG;
    adco->b_off = offB;
    GBS::ADC_ROFCTRL::write(offR);
    GBS::ADC_GOFCTRL::write(offG);
    GBS::ADC_BOFCTRL::write(offB);
    SerialM.print(F("[3x4x-v111] ADC black level back to the base: "));
    SerialM.print(offR);
    SerialM.print('/');
    SerialM.print(offG);
    SerialM.print('/');
    SerialM.println(offB);
}

void nudgeAdcOffset(uint8_t channel, int8_t delta)
{
    captureAdcOffsetBase(); // v73: before the first change, so there is somewhere to go back to
    if (channel == 0 || channel == 1) {
        adco->r_off = clampU8((int)GBS::ADC_ROFCTRL::read() + delta, 255);
        GBS::ADC_ROFCTRL::write(adco->r_off);
    }
    if (channel == 0 || channel == 2) {
        adco->g_off = clampU8((int)GBS::ADC_GOFCTRL::read() + delta, 255);
        GBS::ADC_GOFCTRL::write(adco->g_off);
    }
    if (channel == 0 || channel == 3) {
        adco->b_off = clampU8((int)GBS::ADC_BOFCTRL::read() + delta, 255);
        GBS::ADC_BOFCTRL::write(adco->b_off);
    }
    SerialM.print(F("[3x4x-v111] ADC black level R/G/B: "));
    SerialM.print(GBS::ADC_ROFCTRL::read());
    SerialM.print('/');
    SerialM.print(GBS::ADC_GOFCTRL::read());
    SerialM.print('/');
    SerialM.println(GBS::ADC_BOFCTRL::read());
}

void imageTuningAdcOffsets(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (r) *r = GBS::ADC_ROFCTRL::read();
    if (g) *g = GBS::ADC_GOFCTRL::read();
    if (b) *b = GBS::ADC_BOFCTRL::read();
}

// code: 'a'/'b' red +/-, 'c'/'d' green +/-, 'e'/'f' blue +/-, 'g'/'h' U (blue-yellow cast) +/-, 'i'/'j' V (red-cyan cast) +/-
void imageCommand(char code)
{
    captureWb();
    const int d = ((code - 'a') % 2 == 0) ? 1 : -1;
    switch (code) {
        case 'a': case 'b':
            adco->r_gain = clampU8((int)GBS::ADC_RGCTRL::read() + d * 2, 255);
            GBS::ADC_RGCTRL::write(adco->r_gain);
            SerialM.print(F("White balance R gain: 0x"));
            SerialM.println(adco->r_gain, HEX);
            break;
        case 'c': case 'd':
            adco->g_gain = clampU8((int)GBS::ADC_GGCTRL::read() + d * 2, 255);
            GBS::ADC_GGCTRL::write(adco->g_gain);
            SerialM.print(F("White balance G gain: 0x"));
            SerialM.println(adco->g_gain, HEX);
            break;
        case 'e': case 'f':
            adco->b_gain = clampU8((int)GBS::ADC_BGCTRL::read() + d * 2, 255);
            GBS::ADC_BGCTRL::write(adco->b_gain);
            SerialM.print(F("White balance B gain: 0x"));
            SerialM.println(adco->b_gain, HEX);
            break;
        case 'g': case 'h':
            GBS::VDS_U_OFST::write(clampU8((int)GBS::VDS_U_OFST::read() + d, 255));
            SerialM.print(F("Colour cast U offset: "));
            SerialM.println(GBS::VDS_U_OFST::read());
            break;
        case 'i': case 'j':
            GBS::VDS_V_OFST::write(clampU8((int)GBS::VDS_V_OFST::read() + d, 255));
            SerialM.print(F("Colour cast V offset: "));
            SerialM.println(GBS::VDS_V_OFST::read());
            break;
        default:
            break;
    }
}

void resetImageFine()
{
    if (wbBaseValid) {
        adco->r_gain = wbR;
        adco->g_gain = wbG;
        adco->b_gain = wbB;
        GBS::ADC_RGCTRL::write(wbR);
        GBS::ADC_GGCTRL::write(wbG);
        GBS::ADC_BGCTRL::write(wbB);
        GBS::VDS_U_OFST::write(wbU);
        GBS::VDS_V_OFST::write(wbV);
        SerialM.println(F("White balance and colour cast back to the base"));
    }
    if (!baseValid) {
        captureBase();
    }
    resetAdcOffset(); // v73: the analog black level too
    resetHue();       // v74: and the colour matrix rotation
    writeLevels(); // peaking gains back to the base of the chosen strength level
    GBS::VDS_Y_OFST::write(baseYOfst); // v107p: brightness and contrast too
    GBS::VDS_Y_GAIN::write(baseYGain);
    GBS::VDS_BLEV_BYPS::write(1);
    GBS::VDS_BLEV_LEVEL::write(0x26);
    GBS::VDS_BLEV_GAIN::write(0x07);
    SerialM.println(F("Peaking and black level back to the preset"));
}

void cyclePeakLevel()
{
    if (!baseValid) {
        captureBase();
    }
    uopt->peakLevel = (uopt->peakLevel + 1) % 5;
    writeLevels();
    saveUserPrefs();
    SerialM.print(F("Peaking strength: "));
    SerialM.print(uopt->peakLevel);
    SerialM.print(F(" of 4 (1 = stock) gains 0x"));
    SerialM.print(GBS::VDS_PK_LB_GAIN::read(), HEX);
    SerialM.print(F("/0x"));
    SerialM.print(GBS::VDS_PK_LH_GAIN::read(), HEX);
    if (!uopt->wantPeaking) {
        SerialM.print(F("  (peaking itself is switched off)"));
    }
    SerialM.println();
}

void cycleStepLevel()
{
    if (!baseValid) {
        captureBase();
    }
    uopt->stepLevel = (uopt->stepLevel + 1) & 3;
    writeLevels();
    saveUserPrefs();
    SerialM.print(F("Step response strength: "));
    SerialM.print(uopt->stepLevel);
    SerialM.print(F(" of 3 (1 = stock) gain "));
    SerialM.print(GBS::VDS_STEP_GAIN::read());
    if (GBS::VDS_UV_STEP_BYPS::read() == 1) {
        SerialM.print(F("  (step response itself is switched off)"));
    }
    SerialM.println();
}

void cycleAdcFilterLevel()
{
    if (!baseValid) {
        captureBase();
    }
    uopt->adcFilterLevel = (uopt->adcFilterLevel + 1) & 3;
    writeLevels();
    saveUserPrefs();
    SerialM.print(F("ADC low-pass filter: "));
    switch (uopt->adcFilterLevel) {
        case 1: SerialM.println(F("110 MHz (light)")); break;
        case 2: SerialM.println(F("70 MHz (medium)")); break;
        case 3: SerialM.println(F("40 MHz (strong)")); break;
        default: SerialM.println(F("stock (preset value)")); break;
    }
}

void stepManualPhaseAdc(int8_t delta)
{
    int16_t cur = (manualPhaseADC >= 0) ? manualPhaseADC : rto->phaseADC;
    manualPhaseADC = (int8_t)((cur + delta) & 0x1f);
    setAndLatchPhaseADC();
    latchPLLAD();
    SerialM.print(F("ADC phase: "));
    saveUserPrefs();
    SerialM.print(manualPhaseADC);
    SerialM.println(F(" of 31 (manual, saved)"));
}

void resetManualPhaseAdc()
{
    manualPhaseADC = -1;
    rto->phaseIsSet = 0; // let the sync watcher choose the phase again
    saveUserPrefs();
    SerialM.println(F("ADC phase: automatic"));
}

// v107j: every picture control the web shows with - / + or as a cycle, in one line, so the web can show each value next to
// its button and nobody has to read the log to know what a press did. Keyed, space separated, read live from the chip
// (or from the state the controls keep). Each value is given so that it goes UP when the + of its button is pressed:
// the ADC gain register works the other way round (lower = more gain), so it is sent as 255 - register.
void imageTuningReport(char *buf, size_t n)
{
    int8_t hs = 0;
    uint8_t su = 0, sv = 0;
    imageTuningHue(&hs, &su, &sv);
    const bool blevOn = (GBS::VDS_BLEV_BYPS::read() == 0);
    const int phase = (manualPhaseADC >= 0) ? manualPhaseADC : rto->phaseADC;
    snprintf(buf, n,
             "v pk=%u st=%u nf=%u pf=%u ph=%d pa=%u br=%u ct=%u bl=%d ga=%u gauto=%u su=%u sv=%u hu=%d "
             "wr=%u wg=%u wb=%u tu=%u tv=%u or=%u og=%u ob=%u sg=%u pkb=%u stb=%u "
             "hbs=%u hbp=%u dhs=%u dhp=%u dvs=%u dvp=%u hbin=%u vbs=%u vbp=%u ln=%u lna=%u cfg=%u cp=%u",
             (unsigned)uopt->peakLevel, (unsigned)uopt->stepLevel, (unsigned)uopt->adcFilterLevel,
             (unsigned)GBS::VDS_PK_LB_GAIN::read(), phase, manualPhaseADC >= 0 ? 0u : 1u,
             (unsigned)GBS::VDS_Y_OFST::read(), (unsigned)GBS::VDS_Y_GAIN::read(),
             blevOn ? (int)GBS::VDS_BLEV_LEVEL::read() : -1,
             (unsigned)(255 - GBS::ADC_RGCTRL::read()), (unsigned)uopt->enableAutoGain,
             (unsigned)su, (unsigned)sv, (int)hs * 3,
             (unsigned)GBS::ADC_RGCTRL::read(), (unsigned)GBS::ADC_GGCTRL::read(), (unsigned)GBS::ADC_BGCTRL::read(),
             (unsigned)GBS::VDS_U_OFST::read(), (unsigned)GBS::VDS_V_OFST::read(),
             (unsigned)GBS::ADC_ROFCTRL::read(), (unsigned)GBS::ADC_GOFCTRL::read(), (unsigned)GBS::ADC_BOFCTRL::read(),
             // v107j: the step-response gain, and what the preset itself loaded for peaking and step response
             (unsigned)GBS::VDS_STEP_GAIN::read(), (unsigned)(baseValid ? baseLB : GBS::VDS_PK_LB_GAIN::read()),
             (unsigned)(baseValid ? baseStep : GBS::VDS_STEP_GAIN::read()),
             // v107l: the window registers behind the quick border / memory / capture controls of the Ajuste tab
             (unsigned)GBS::VDS_HB_ST::read(), (unsigned)GBS::VDS_HB_SP::read(),
             (unsigned)GBS::VDS_DIS_HB_ST::read(), (unsigned)GBS::VDS_DIS_HB_SP::read(),
             (unsigned)GBS::VDS_DIS_VB_ST::read(), (unsigned)GBS::VDS_DIS_VB_SP::read(),
             (unsigned)GBS::IF_HBIN_SP::read(),
             // v107n: the vertical memory window, and the Líneas row (setting, and what the mode offers: 1 row, 2 DOLOR)
             (unsigned)GBS::VDS_VB_ST::read(), (unsigned)GBS::VDS_VB_SP::read(),
             (unsigned)linesModeEffective(), (unsigned)linesAvailability(),
             // v107p: whose picture memory is in use (0 unknown console, 1 Mega Drive, 2 SNES, 3 PC Engine)
             (unsigned)settingsProfile(),
             // (after v107r) a personal profile is loaded: "deshacer borde" then erases nothing and does not ask
             rto->isCustomPreset ? 1u : 0u);
}
