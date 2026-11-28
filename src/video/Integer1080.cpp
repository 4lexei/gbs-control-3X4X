#include "Integer1080.h"
#include "ConsoleModes.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/tv5725.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSPhase.h" // (after v108c) clamp and coast after the ADC clock change of the VGA 2 x 2
#include "VideoScaling.h"
#include "VideoShifting.h"
#include "Scanlines.h" // v92: re-kick the RFF buffer after a memory reset
#include <LittleFS.h> // (after v107q) the edges, one record per mode

typedef TV5725<GBS_ADDR> GBS;

// Net change this module has applied since the preset was loaded.
struct Tracked
{
    int16_t hbinSp;    // IF_HBIN_SP
    int16_t hsyncRst;  // IF_HSYNC_RST
    int16_t lineSp;    // IF_LINE_SP
    int16_t ifHbSt2;   // IF_HB_ST2 (non SD inputs)
    int16_t ifHbSp2;   // IF_HB_SP2 (non SD inputs)
    int16_t hbSt;      // VDS_HB_ST
    int16_t hbSp;      // VDS_HB_SP
    int16_t disHbSt;   // VDS_DIS_HB_ST
    int16_t disHbSp;   // VDS_DIS_HB_SP
    bool scaled;       // HScale was changed (x5 + 320 px)
    uint16_t prevHscale;
    uint8_t prevHscaleByps;
    // (after v110b) the 1080p x4 4:3 placement: the registers it writes, as they were before it, written back
    // absolutely by undoAll() (with the 4:3 edge on top of them, a difference would not bring them back).
    // Before, going back to 16:9 kept the 4:3 values: bad edges and memory glitches until the preset was reloaded.
    bool md43;
    uint16_t m43[16];
};
static Tracked tr;

void integer1080Reset()
{
    memset(&tr, 0, sizeof(tr));
}

// subtract a tracked difference from a register (only when something was tracked)
#define INT1080_UNDO(reg, delta) \
    do { \
        if (delta) { \
            reg::write((uint16_t)((int16_t)reg::read() - (delta))); \
        } \
    } while (0)

// (after v110b) the registers of the 1080p x4 4:3 placement, in this order
static void md43Snap()
{
    uint16_t *m = tr.m43;
    m[0] = GBS::VDS_HSCALE::read();     m[1] = GBS::VDS_HSCALE_BYPS::read();
    m[2] = GBS::VDS_VSCALE::read();     m[3] = GBS::VDS_VSCALE_BYPS::read();
    m[4] = GBS::VDS_HB_ST::read();      m[5] = GBS::VDS_HB_SP::read();
    m[6] = GBS::VDS_DIS_HB_ST::read();  m[7] = GBS::VDS_DIS_HB_SP::read();
    m[8] = GBS::IF_HBIN_ST::read();     m[9] = GBS::IF_HBIN_SP::read();
    m[10] = GBS::VDS_DIS_VB_ST::read(); m[11] = GBS::VDS_DIS_VB_SP::read();
    m[12] = GBS::VDS_VB_ST::read();     m[13] = GBS::VDS_VB_SP::read();
    m[14] = GBS::IF_VB_ST::read();      m[15] = GBS::IF_VB_SP::read();
    tr.md43 = true;
}

static void md43Restore()
{
    const uint16_t *m = tr.m43;
    GBS::VDS_HSCALE_BYPS::write(m[1]);  GBS::VDS_HSCALE::write(m[0]);
    GBS::VDS_VSCALE_BYPS::write(m[3]);  GBS::VDS_VSCALE::write(m[2]);
    GBS::VDS_HB_ST::write(m[4]);        GBS::VDS_HB_SP::write(m[5]);
    GBS::VDS_DIS_HB_ST::write(m[6]);    GBS::VDS_DIS_HB_SP::write(m[7]);
    GBS::IF_HBIN_ST::write(m[8]);       GBS::IF_HBIN_SP::write(m[9]);
    GBS::VDS_DIS_VB_ST::write(m[10]);   GBS::VDS_DIS_VB_SP::write(m[11]);
    GBS::VDS_VB_ST::write(m[12]);       GBS::VDS_VB_SP::write(m[13]);
    GBS::IF_VB_ST::write(m[14]);        GBS::IF_VB_SP::write(m[15]);
}

static void undoAll()
{
    // (after v110b) first: back to the state the 4:3 found, then the other differences take it to the preset
    if (tr.md43)
        md43Restore();
    if (tr.scaled) {
        GBS::VDS_HSCALE::write(tr.prevHscale);
        GBS::VDS_HSCALE_BYPS::write(tr.prevHscaleByps);
    }
    INT1080_UNDO(GBS::VDS_HB_ST, tr.hbSt);
    INT1080_UNDO(GBS::VDS_HB_SP, tr.hbSp);
    INT1080_UNDO(GBS::VDS_DIS_HB_ST, tr.disHbSt);
    INT1080_UNDO(GBS::VDS_DIS_HB_SP, tr.disHbSp);
    INT1080_UNDO(GBS::IF_HBIN_SP, tr.hbinSp);
    INT1080_UNDO(GBS::IF_HSYNC_RST, tr.hsyncRst);
    INT1080_UNDO(GBS::IF_LINE_SP, tr.lineSp);
    INT1080_UNDO(GBS::IF_HB_ST2, tr.ifHbSt2);
    INT1080_UNDO(GBS::IF_HB_SP2, tr.ifHbSp2);
    integer1080Reset();
}

// presses of the normal "move left / right" button (or the fine one), tracking what actually changed
static void pressMove(bool right, uint8_t times, bool fine)
{
    const bool prevFine = scaleHorizontalFine;
    scaleHorizontalFine = fine;
    for (uint8_t i = 0; i < times; i++) {
        const int16_t a0 = GBS::IF_HBIN_SP::read();
        const int16_t b0 = GBS::IF_HSYNC_RST::read();
        const int16_t c0 = GBS::IF_LINE_SP::read();
        const int16_t d0 = GBS::IF_HB_ST2::read();
        const int16_t e0 = GBS::IF_HB_SP2::read();
        moveCanvasHorizontal(right);
        tr.hbinSp += (int16_t)GBS::IF_HBIN_SP::read() - a0;
        tr.hsyncRst += (int16_t)GBS::IF_HSYNC_RST::read() - b0;
        tr.lineSp += (int16_t)GBS::IF_LINE_SP::read() - c0;
        tr.ifHbSt2 += (int16_t)GBS::IF_HB_ST2::read() - d0;
        tr.ifHbSp2 += (int16_t)GBS::IF_HB_SP2::read() - e0;
    }
    scaleHorizontalFine = prevFine;
}

// x5 + 320 px: HScale 818 = 5.0 columns per game pixel.
// The generic HScale routine (made for the 720p presets) moves the data anchor (VDS_HB_SP) ~206 units left and
// widens the window past the end of the line: picture cut on the left, garbage on the right. Measured on hardware
// (NTSC 1080p integer x5, HScale 1023 -> 818): HB ST/SP 1592/56, HB(d) ST/SP 1594/346. Target: picture 1600 px wide
// centred at 960 (anchor +82 units), window 16 px wider than the picture on each side.
static void placeX5Window()
{
    const int16_t hbSt = GBS::VDS_HB_ST::read();
    const int16_t hbSp = GBS::VDS_HB_SP::read();
    const int16_t disSt = GBS::VDS_DIS_HB_ST::read();
    const int16_t disSp = GBS::VDS_DIS_HB_SP::read();
    tr.prevHscale = GBS::VDS_HSCALE::read();
    tr.prevHscaleByps = GBS::VDS_HSCALE_BYPS::read();

    scaleHorizontalTo(818);
    GBS::VDS_HB_SP::write(GBS::VDS_HB_SP::read() + 82);
    GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() - 93);
    GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() - 153);
    GBS::VDS_HB_ST::write(GBS::VDS_HB_ST::read() - 153);

    tr.hbSt += (int16_t)GBS::VDS_HB_ST::read() - hbSt;
    tr.hbSp += (int16_t)GBS::VDS_HB_SP::read() - hbSp;
    tr.disHbSt += (int16_t)GBS::VDS_DIS_HB_ST::read() - disSt;
    tr.disHbSp += (int16_t)GBS::VDS_DIS_HB_SP::read() - disSp;
    tr.scaled = true;
}

// PAL x5 + 320 px (round 1, estimated from the PAL x4 measurements, not yet verified on hardware).
// HScale 818 = x1.25 on top of the x4 sampling (PLLAD_MD 2230): 5.0 columns per game pixel, picture 1600 px wide.
// The scaling pivots on the data anchor (VDS_HB_SP): at x4 the anchor sits at x~94 (stock HB_SP 164, ~1.535 px per
// unit, x = 1.535 * u - 158) and the picture starts 226 px after it; at x1.25 that distance is ~283 px. For the
// picture to start at x=160 (centred: 160..1760) the anchor must be at x~-123 -> HB_SP ~24.
// Output window 16 px wider than the picture per side: x=144..1776 -> DIS_HB_SP ~196, DIS_HB_ST ~1260.
// v35, measured on hardware with Sonic 2 (v34: game x=152..1752 -> anchor +5 units to centre it at 160..1760; window
// edges = game edges: DIS_HB_SP 225, DIS_HB_ST 1268; x = 1.535 * u - 185). HB_ST is raised (never cut: cutting it made lines wobble in v28 / v29) so it does not blank the right of the picture.
static void placePalX5Window()
{
    const int16_t hbSt = GBS::VDS_HB_ST::read();
    const int16_t hbSp = GBS::VDS_HB_SP::read();
    const int16_t disSt = GBS::VDS_DIS_HB_ST::read();
    const int16_t disSp = GBS::VDS_DIS_HB_SP::read();
    tr.prevHscale = GBS::VDS_HSCALE::read();
    tr.prevHscaleByps = GBS::VDS_HSCALE_BYPS::read();

    GBS::VDS_HSCALE::write(818);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_HB_SP::write(29);
    // v36: HB_ST == DIS_HB_ST. With the line filter on, DIS_HB_ST below HB_ST shifted the picture one line and broke the
    // scanlines (measured on hardware, v35: 1268 / 1270 broken, 1272 / 1270 fine). The original scale / shift code keeps the
    // same rule (DIS_HB_ST >= HB_ST).
    GBS::VDS_HB_ST::write(1268);
    GBS::VDS_DIS_HB_SP::write(225);
    GBS::VDS_DIS_HB_ST::write(1268);

    tr.hbSt += (int16_t)GBS::VDS_HB_ST::read() - hbSt;
    tr.hbSp += (int16_t)GBS::VDS_HB_SP::read() - hbSp;
    tr.disHbSt += (int16_t)GBS::VDS_DIS_HB_ST::read() - disSt;
    tr.disHbSp += (int16_t)GBS::VDS_DIS_HB_SP::read() - disSp;
    tr.scaled = true;
}

// IF line recipe for a lowered ADC clock (verified on PAL hardware, same as the stock rule: HRST = PLLAD_MD/2 + 6,
// LINE = HRST + 65, ST2 = (PLLAD_MD/2 - 47) & ~1).
static void applyIfRecipe()
{
    const uint16_t md2 = GBS::PLLAD_MD::read() >> 1;
    const int16_t newSt2 = (int16_t)((md2 - 47) & 0xfffe);
    tr.ifHbSt2 += newSt2 - (int16_t)GBS::IF_HB_ST2::read();
    GBS::IF_HB_ST2::write((uint16_t)newSt2);
    const int16_t target = (int16_t)(md2 + 6);
    tr.hsyncRst += target - (int16_t)GBS::IF_HSYNC_RST::read();
    tr.lineSp += (int16_t)(target + 65) - (int16_t)GBS::IF_LINE_SP::read();
    GBS::IF_HSYNC_RST::write((uint16_t)target);
    GBS::IF_LINE_SP::write((uint16_t)(target + 65));
}

// absolute write of a register, tracking the change
#define INT1080_SET(reg, field, value) \
    do { \
        const int16_t before_ = (int16_t)reg::read(); \
        reg::write((uint16_t)(value)); \
        tr.field += (int16_t)reg::read() - before_; \
    } while (0)

// v43: SNES / NES start ~10 px further right than the Mega Drive in 256 px mode (measured, same offset at x4 and x5).
static bool snesProfile()
{
    // v67: the PC Engine shares the SNES pixel clock (both are 21.477 MHz / 4 for a 256 px picture), and its 336 and 512 px
    // modes are that same clock divided by 3 and by 2, so one scale gives whole columns in all three and the picture is
    // 1024 px wide in each. Measured on the 256 px grid through the ODV: 4.000 columns per pixel, 1024 px, no uneven column.
    // So the PC Engine uses the SNES placement until it has measurements of its own.
    const uint8_t prof = activeConsoleProfile();
    return prof == 2 || prof == 3;
}

// v72: the PC Engine now has its own measured placement in the 720p modes, so those entries must not fall through to the
// SNES ones. Measured on the 240p suite 256 px grid, through the ODV:
//   720p x3 4:3   256 px -> HScale 603 (4.000 columns, 1024 px picture), "320 px" -> 483 (5.000 columns, 1280 px)
//   720p x3 16:9  256 px -> HScale 804 (3.003 columns, 768 px),          "320 px" -> 603 (4.003 columns, 1024 px)
// The 1080p x4 (HScale 1023) and 1080p x5 entries were measured too and came out at 4.003 and 5.003 columns, centred,
// so those keep the shared placement and are not overridden here.
static bool pceProfile()
{
    return activeConsoleProfile() == 3;
}

// v62: which console profile is in force. 1 = Mega Drive, 2 = SNES / NES, 3 = PC Engine.
// PC Engine shares the Mega Drive dot clock, so it uses the Mega Drive values until its own are measured:
// it is a profile of its own only so that it can be given different values later (and so the status bar names it).
uint8_t activeConsoleProfile()
{
    // (after v107v) the profiles without measurements (4..17, see consoleProfileName) place the picture as an unknown
    // console does, with the Mega Drive values: they exist to keep each console's own memories, not a placement yet
    if (uopt->consoleProfile >= CONSOLE_PROFILE_FIRST_EMPTY)
        return 1;
    if (uopt->consoleProfile != 0)
        return uopt->consoleProfile;
    if (rto->consoleDetected == 1)
        return 2; // SNES / NES
    if (rto->consoleDetected == 2)
        return 3; // PC Engine
    return 1;     // Mega Drive (and anything else)
}

// v107p: whose picture memory is in use (ColorPresets): the chosen profile, or in auto the detected console, with 0 for a
// console the detection does not know. activeConsoleProfile() above still answers 1 there, because the PLACEMENT of an
// unknown console uses the Mega Drive values; its settings must not land on the Mega Drive's.
// (after v107v) the list of 2026-11-14: profiles with no measured pixel yet, chosen by hand (the field rate cannot tell
// them apart: the PlayStation and the N64 read like the PC Engine, the NES like the SNES). No TurboGrafx-16: // it is the PC Engine itself (its 50 Hz memory is the PC Engine's 50 Hz record). Nothing here borrows the measurements of
// another console: they place the picture as an unknown console does. They keep the colour and
// filters, the sync state, the edges and the last mode of each console; the placement is the unknown console's.
// The names are the ones the status bar prints after "profile " and the web matches them literally.
static const char *const CONSOLE_NAMES[CONSOLE_PROFILE_COUNT] = {
    "unknown", "MD", "SNES", "PC-Engine",
    "PlayStation", "Saturn", "Nintendo 64", "NES", "Master System", "Neo Geo AES", "Neo Geo CD",
    "3DO", "Jaguar", "Dreamcast", "PlayStation 2", "Xbox", "GameCube", "Wii", "Others"}; // (after v108f) English like the rest of the bar; the web shows «Otras» in Spanish

const char *consoleProfileName(uint8_t prof)
{
    return (prof < CONSOLE_PROFILE_COUNT) ? CONSOLE_NAMES[prof] : CONSOLE_NAMES[0];
}

uint8_t settingsProfile()
{
    if (uopt->consoleProfile != 0)
        return uopt->consoleProfile;
    if (rto->consoleDetected == 1)
        return 2;
    if (rto->consoleDetected == 2)
        return 3;
    if (rto->consoleDetected == 3)
        return CONSOLE_PROFILE_DREAMCAST; // (after v107w) recognised by Auto: 576i / 480i at its own field rates
    return rto->consoleIsMd ? 1 : 0;
}

// (after v107z) see dcSignal() in Integer1080.h
static bool vpIsInterlaced(uint16_t v)
{
    return ((v >= 520 && v <= 528) || (v >= 620 && v <= 628)) && (v % 2 == 0);
}
static uint8_t ilState = 0; // 1 = interlaced
static uint8_t ilCount = 0;

void interlaceNote(uint16_t vperiodIf)
{
    const uint8_t il = vpIsInterlaced(vperiodIf) ? 1 : 0;
    if (il == ilState) {
        ilCount = 0;
    } else if (++ilCount >= 2) { // two readings in a row, as the sync watcher does for the deinterlacer
        ilState = il;
        ilCount = 0;
    }
}

// at a placement: three readings one field apart, the majority wins (the latch may still hold the signal before the load)
static void interlaceFresh()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < 3; i++) {
        if (i)
            delay(17);
        n += vpIsInterlaced(GBS::VPERIOD_IF::read()) ? 1 : 0;
    }
    ilState = (n >= 2) ? 1 : 0;
    ilCount = 0;
}

uint8_t dcSignal()
{
    if (settingsProfile() != CONSOLE_PROFILE_DREAMCAST)
        return 0;
    const uint8_t vs = rto->videoStandardInput;
    if (vs == 1)
        return ilState ? 3 : 1;
    if (vs == 2)
        return ilState ? 4 : 2;
    return 5;
}

bool dcVgaWanted()
{
    if (settingsProfile() != CONSOLE_PROFILE_DREAMCAST)
        return false;
    if (!(rto->videoStandardInput == 3 || GBS::GBS_OPTION_SCALING_RGBHV::read()))
        return false;
    // (after v108d) 1080p with our 1080p button, or 720p with our 720p x3 (4:3 = 1, 16:9 = 4)
    const bool p1080 = uopt->presetPreference == Output1080P && uopt->integer1080 == 3;
    const bool p720 = uopt->presetPreference == Output720P && (uopt->integer720 == 1 || uopt->integer720 == 4);
    if (!p1080 && !p720)
        return false;
    return !rto->isCustomPreset && !rto->outModeHdBypass;
}

// Rama's ADC clock for this load, noted before anything of ours touches it, so the 700 ms pass writes the same values
static uint16_t dcVgaStockMd = 0;

static void dcVgaClock(uint16_t md, uint16_t hrst, uint16_t line, uint16_t st2, uint16_t sp2)
{
    GBS::PLLAD_MD::write(md);
    GBS::IF_HSYNC_RST::write(hrst);
    GBS::IF_LINE_SP::write(line);
    GBS::IF_HB_ST2::write(st2);
    GBS::IF_HB_SP2::write(sp2);
    latchPLLAD();
    delay(1);
    updateClampPosition();
    updateCoastPosition(0);
}

// ===== (after v108h) THE MEGA DRIVE'S 1080p x4: «4:3» AND THE 256 / 320 SWITCH WITHOUT A RELOAD =====
// 2026-11-17. «4:3» is the same idea as 720p x3 4:3: for a 16:9 set switched to 4:3 by its remote, the
// picture 4/3 wider (5.33 columns per pixel) at the same height, so once the set squeezes it the pixel is square again.
// Only the Mega Drive (its own profile, Auto or chosen), only 1080p x4: measured on the 240p Test Suite grids.
bool md43Able()
{
    // (after v123e) 2026-11-22: the PC Engine's 1080p x5 gets a «4:3» too ("lo más parecido a casi llenar la
    // pantalla, muy poco desborde"). Only there: the x5 of the other consoles keeps its 16:9 alone.
    const bool pceX5 = (uopt->integer1080 == 4) && pceProfile();
    const bool snesX5 = (uopt->integer1080 == 4) && !pceProfile() && activeConsoleProfile() == 2; // (after v127g) 2026-11-25
    const bool mdX5 = (uopt->integer1080 == 4) && !pceProfile() && activeConsoleProfile() == 1; // (after v127x) 2026-11-26: the Mega Drive's x5 «4:3» too (PAL and NTSC)
    if (!rto->integer1080Active || (uopt->integer1080 != 3 && !pceX5 && !snesX5 && !mdX5) || rto->isCustomPreset || rto->outModeHdBypass)
        return false;
    if (rto->presetID != 0x05 && rto->presetID != 0x15)
        return false;
    if (dcSignal() != 0)
        return false;
    // (after v120) 2026-11-21: the PC Engine gets a 4:3 of its own too. NTSC only (there is no PAL PC
    // Engine), and with its own placement, still to be measured.
    // (after v130g) and in PAL: every mode exists in both (the PAL placement is the NTSC one carried over, see md43Measured)
    if (pceProfile())
        return rto->presetID == 0x05 || rto->presetID == 0x15;
    // (after v127g) 2026-11-25: the SNES gets a 1080p x4 «4:3» too (NTSC), placement still to be measured
    if (activeConsoleProfile() == 2)
        return (rto->presetID == 0x05 || rto->presetID == 0x15) && settingsProfile() == 2; // (after v127p) and the PAL one
    return settingsProfile() == 1;
}

bool tv43Active()
{
    return uopt->integer1080Tv43 && md43Able();
}

// (after v120) the "no «ancho»" rule. (after v120c) 2026-11-21: now that the PC Engine has its 4:3,
// «llenar» is pointless there too, so the rule is the same for both consoles: wherever there is a 4:3, no «llenar».
// (after v120d) and for the release notes to be true: no «llenar» in ANY PC Engine mode.
// The only one left was 1080p x5, which has no 4:3 of its own and so was still offering it.
bool md43NoWide()
{
    if (pceProfile() && rto->integer1080Active && (rto->presetID == 0x05 || rto->presetID == 0x15))
        return true;
    // (after v127g) 2026-11-25: no «llenar» in the SNES 1080p x4 (it has its 4:3 now)
    if (activeConsoleProfile() == 2 && rto->integer1080Active && uopt->integer1080 == 3 && (rto->presetID == 0x05 || rto->presetID == 0x15))
        return true;
    return md43Able();
}

// The 256 px picture of 1080p x4 16:9 needs the ADC clock at 80 % (4 columns per pixel would need HScale 1269, the
// register stops at 1023). Until now that meant loading the preset again, and the set lost the signal for a moment (
// symptom: the screen went black at every 256 <-> 320). The output timing does not depend on the ADC clock, so the clock
// is changed in place now, as the Dreamcast's VGA 2 x 2 already does: the set keeps its signal, the picture only jumps
// while the PLL relocks. Normal clock: the one the table loads with, and its own input line (the dumps: NTSC 1251 / 1316,
// canvas 1124 / 104; PAL the recipe of 2230, 1121 / 1186, 1068 / 104). 80 %: the same arithmetic as the narrow tables, and
// the recipe for the input line.
static bool narrowWanted(uint16_t nativeWidth)
{
    if (!rto->integer1080Active || uopt->integer1080 != 3 || rto->isCustomPreset || rto->outModeHdBypass)
        return uopt->integer1080Narrow != 0;
    // (after v111g) 2026-11-18: the PC Engine changes width without a black screen too, as the Mega Drive.
    // Its 1080p x4 states (v76) are written absolute for both widths, and the 80 % clock comes from the same arithmetic as
    // the reload (table x 0.973 x 0.8), so the clock is changed in place exactly like the Mega Drive's. NTSC only (its
    // console is NTSC; the ODV gives NTSC).
    // (after v120b) 2026-11-21: in 4:3 the 256 px button scrambled the picture and scrolled sideways. The
    // 80 % clock belongs to the 16:9 only. The 4:3 keeps the normal clock in both widths, exactly as the Mega Drive's
    // 4:3 does, and the two widths differ by HScale alone (767 and 959).
    if (pceProfile() && rto->presetID == 0x05)
        return nativeWidth == 256 && !tv43Active();
    // (after v127g) 2026-11-25: the SNES in NTSC follows the width like the PC Engine and the Mega Drive:
    // 256 px at the 80 % clock (PLLAD_MD 1991, 4 columns per pixel), CRT at the normal one (HScale 1023 = 5.000 columns),
    // changed in place with no reload. Before, it loaded narrow, reloaded the preset at every 256 <-> CRT and had a jump.
    if (snesProfile() && !pceProfile() && rto->presetID == 0x05 && settingsProfile() != CONSOLE_PROFILE_DREAMCAST)
        return nativeWidth == 256 && !tv43Active();
    if (settingsProfile() == CONSOLE_PROFILE_DREAMCAST || snesProfile())
        return uopt->integer1080Narrow != 0; // the SNES loads narrow and stays so; the Dreamcast never is
    if (rto->presetID == 0x15) {
        if (activeConsoleProfile() != 1 || settingsProfile() != 1)
            return uopt->integer1080Narrow != 0;
        return uopt->sizeStep == 0 && !tv43Active(); // (after v109) PAL 256 px at 80 %: wifi trouble in the test, open
    }
    if (rto->presetID == 0x05)
        return nativeWidth == 256 && !tv43Active();
    return uopt->integer1080Narrow != 0;
}

bool integer1080ClockLive(bool narrow)
{
    if (!rto->integer1080Active || uopt->integer1080 != 3 || (rto->presetID != 0x05 && rto->presetID != 0x15))
        return false;
    if ((uopt->integer1080Narrow != 0) == narrow)
        return true;
    const bool pal = (rto->presetID == 0x15);
    // Back to the state the table loaded, exactly: the input line registers (HRST, LINE, canvas) are the table's in both
    // clocks (the narrow table only lowers the clock; its input line comes from the recipe the placement writes, tracked),
    // so undoing what this module tracked leaves them as a load of either clock leaves them. Then only the clock changes,
    // and the placement that follows writes what it writes after a load.
    undoAll();
    const uint16_t normal = integer1080NormalMd(pal);
    // (after v109) the test of 2026-11-17: with the Mega Drive in PAL at 1784 the wifi dropped even with nobody touching
    // anything, as the Dreamcast's VGA clock did. His choice: move that clock a little, to 1750 (about 2 % lower, which
    // moves its harmonics away from where they were). The SNES and the PC Engine keep their 1784.
    // (after v110) and then 1759, at 1750 a ripple ran now and then through the top right of the picture; the
    // clock was stepped up with Developer's «PLL div» and at 1759 it was gone, the wifi still fine
    const uint16_t palNarrow = pceProfile() ? 1991 : (snesProfile() ? 1784 : 1759); // (after v130h) the PC Engine on the NTSC 80 % clock
    const uint16_t md = narrow ? (pal ? palNarrow : (uint16_t)((normal * 8 + 5) / 10)) : normal;
    GBS::PLLAD_MD::write(md);
    latchPLLAD();
    delay(1);
    updateClampPosition();
    updateCoastPosition(0);
    uopt->integer1080Narrow = narrow ? 1 : 0;
    SerialM.print(F("[3x4x-v130k] 1080p x4: ADC clock "));
    SerialM.print(md);
    SerialM.println(narrow ? F(" (80 %, 256 px) changed in place, no reload") : F(" (normal) changed in place, no reload"));
    return true;
}

// the measurements of 2026-11-17 (12:14, 12:29, 12:44, 12:53), Mega Drive, 1080p x4 «4:3», normal ADC clock:
//   NTSC 320 px: HScale 771, 1708 px wide (centre 958.5)     NTSC 256 px: HScale 964, 1365 px (centre 959.5)
//   PAL  320 px: HScale 768, 1711 px (770 glitched)          PAL  256 px: HScale 964, 1365 px (centre 960)
// The 320 button with a 256 px game gives the CRT size (both fill the same line time). Vertical: the 1080p x4 one,
// NTSC 1031 / 135 as at the start (the choice), PAL the PAL x4 one, so the shapes and the widths never jump.
static void md43Measured(uint16_t nativeWidth)
{
    if (!tv43Active())
        return;
    const bool pal = (rto->presetID == 0x15);
    const bool w256 = pal ? (uopt->sizeStep == 0) : (nativeWidth == 256);
    uint16_t hs, hbst, hbsp, dst, dsp, hbin;
    // (after v120) PC Engine, NTSC only, 2026-11-21. Worked out from THE 1080p x4 16:9 grid of that day
    // (320 button): 60.0 output px per 12 px cell = 5.000 columns exactly at HScale 1023, window 874 units = 1201 px,
    // centre 959.5 (the edges crop about 16 px of console each side). A set in 4:3 squeezes the line to 0.75, so the
    // picture is drawn wider here to come out right: 256 px button = 5.333 columns (4.000 once squeezed, whole pixels),
    // 320 px button = 6.667 (5.000 once squeezed: exactly the current size). HScale = 5.000 * 1023 / columns; the
    // windows keep the centre (846) and the crop, scaled by the same factor, and the 145-unit gap between HB_SP and
    // DIS_HB_SP. Vertical its own, 1066 / 109 in every width ( of that day).
    // 320 px CONFIRMED by the dump of 2026-11-21 06:25: 80.03 output px per 12 px cell = 6.669 columns (5.000 once the
    // set squeezes it), window 1601 px centred on 960.0; the only thing to move was IF HBIN, 212 -> 264, because
    // the wider window uncovered a strip of garbage on the left that the 16:9 edges used to hide.
    // Both widths measured on 2026-11-21 and fixed here.
    // (after v123e) the PC Engine's 1080p x5 «4:3». Its 16:9 measured on 2026-11-22: CRT HScale 818 = 6.257 columns
    // (constant 5118), 256 px HScale 1023 = 5.000. A set in 4:3 squeezes to 0.75, so: CRT 6.25 / 0.75 = 8.333 columns =
    // HScale 614 (overflows the 4:3 screen a little, on purpose), 256 px 5 / 0.75 = 6.667 columns = HScale 768 (5 square
    // ones once squeezed). Windows, vertical and input start are the 16:9 ones for now: CALCULATED SCALE, to be opened with the
    // windows with the captures. Grid cells expected in the capture: 100 px CRT, 80 px 256 (75 / 60 in the 4:3 canvas).
    if (pceProfile() && uopt->integer1080 == 4) {
        if (w256) { hs = 768; hbst = 1501; hbsp = 78; dst = 1501; dsp = 188; hbin = 210; } // (after v126c) the 22:17 dump (CsVT 272): 1493 / 1493/196 before; // 256 measured by Alexei (80 px square cells in 4:3 canvas)
        else      { hs = 614; hbst = 1545; hbsp = 1564; dst = 1545; dsp = 146; hbin = 286; } // (after v126c) the 22:17 dump confirms 286 with the recipe locked right (22:14 was a CsVT-273 lock); (after v123f) the 09:54 move, HBIN 276 before // CRT measured by Alexei (75 px cells in 4:3 canvas)
        // (after v130g) PAL: the NTSC placement carried over like the rest of the PAL 1080p path: scale as it is, windows x 0.8975,
        // HBIN x 0.896, vertical as it is, IF VB + 52. ESTIMATES until the PAL dumps.
        uint16_t ifVb = 52;
        if (pal) {
            hbst = (uint16_t)(((uint32_t)hbst * 8975 + 5000) / 10000);
            hbsp = (uint16_t)(((uint32_t)hbsp * 8975 + 5000) / 10000);
            dst = (uint16_t)(((uint32_t)dst * 8975 + 5000) / 10000);
            dsp = (uint16_t)(((uint32_t)dsp * 8975 + 5000) / 10000);
            hbin = (uint16_t)(((uint32_t)hbin * 896 + 500) / 1000);
            ifVb = 104;
        }
        if (!tr.md43)
            md43Snap();
        GBS::VDS_HSCALE::write(hs);
        GBS::VDS_HSCALE_BYPS::write(0);
        GBS::VDS_VSCALE::write(409);
        GBS::VDS_VSCALE_BYPS::write(0);
        GBS::VDS_HB_ST::write(hbst);
        GBS::VDS_HB_SP::write(hbsp);
        GBS::VDS_DIS_HB_ST::write(dst);
        GBS::VDS_DIS_HB_SP::write(dsp);
        GBS::IF_HBIN_ST::write(0);
        GBS::IF_HBIN_SP::write(hbin);
        GBS::VDS_DIS_VB_ST::write(1120);
        GBS::VDS_DIS_VB_SP::write(40);
        GBS::IF_VB_ST::write(ifVb);
        GBS::IF_VB_SP::write((uint16_t)(ifVb + 2));
        SerialM.print(pal ? F("[3x4x-v130k] 1080p x5 PAL | 4:3 | PC Engine | the NTSC placement carried over (ESTIMATE) | ") : F("[3x4x-v130k] 1080p x5 | 4:3 | PC Engine | "));
        SerialM.println(w256 ? F("256 px") : F("320 px"));
        return;
    }
    if (pceProfile()) {
        // 256 px on the same normal clock as the 320 px (see narrowWanted). MEASURED on 2026-11-21 10:16 from
        // the 320 button at HScale 959: cells of 64.0 px exactly = 5.333 columns (4.000 whole ones once the set squeezes
        // it), picture 1282 px centred on 958.5, and the same height as the 320 px.
        if (w256) { hs = 959; hbst = 1311; hbsp = 207; dst = 1311; dsp = 379; hbin = 178; }
        else      { hs = 767; hbst = 1429; hbsp = 119; dst = 1429; dsp = 264; hbin = 264; }
        // (after v124e) the new 320 / 352 px buttons, measured on 2026-11-23 (85.4 px cells in the signal = 64 in the
        // 4:3 canvas, square; both centred on 960). Vertical 1064 / 103 in both (the dumps).
        const uint8_t mid = rto->integer1080Centered;
        if (mid == 3) { hs = 719; hbst = 1489; hbsp = 47; dst = 1489; dsp = 202; hbin = 226; }
        if (mid == 4) { hs = 719; hbst = 1498; hbsp = 47; dst = 1498; dsp = 192; hbin = 226; } // (after v126c) the 22:10 dump, border closed (HB 1538, DIS 1546/144 before)
        // (after v130g) PAL: as the x5 above (windows x 0.8975, HBIN x 0.896, scale and vertical as they are, IF VB + 52). ESTIMATES.
        uint16_t ifVb = 27;
        if (pal) {
            hbst = (uint16_t)(((uint32_t)hbst * 8975 + 5000) / 10000);
            hbsp = (uint16_t)(((uint32_t)hbsp * 8975 + 5000) / 10000);
            dst = (uint16_t)(((uint32_t)dst * 8975 + 5000) / 10000);
            dsp = (uint16_t)(((uint32_t)dsp * 8975 + 5000) / 10000);
            hbin = (uint16_t)(((uint32_t)hbin * 896 + 500) / 1000);
            ifVb = 79;
        }
        if (!tr.md43)
            md43Snap();
        GBS::VDS_HSCALE::write(hs);
        GBS::VDS_HSCALE_BYPS::write(0);
        GBS::VDS_VSCALE::write(512);
        GBS::VDS_VSCALE_BYPS::write(0);
        GBS::VDS_HB_ST::write(hbst);
        GBS::VDS_HB_SP::write(hbsp);
        GBS::VDS_DIS_HB_ST::write(dst);
        GBS::VDS_DIS_HB_SP::write(dsp);
        GBS::IF_HBIN_ST::write(0);
        GBS::IF_HBIN_SP::write(hbin);
        GBS::VDS_DIS_VB_ST::write(mid >= 3 ? 1064 : 1066); // the PC Engine's own 1080p x4 border, 2026-11-21: the same in every width
        GBS::VDS_DIS_VB_SP::write(103); // (after v126c) every width 103: the dumps of 22:09 (256) and 22:11 (CRT) with the new recipe, 109 before
        GBS::IF_VB_ST::write(ifVb);
        GBS::IF_VB_SP::write((uint16_t)(ifVb + 2));
        SerialM.print(pal ? F("[3x4x-v130k] 1080p x4 PAL | 4:3 | PC Engine | the NTSC placement carried over (ESTIMATE) | ") : F("[3x4x-v130k] 1080p x4 | 4:3 | PC Engine | "));
        SerialM.println(mid == 3 ? F("320 px whole pixels") : mid == 4 ? F("352 px whole pixels") : w256 ? F("256 px") : F("CRT"));
        return;
    }
    if (activeConsoleProfile() == 2 && uopt->integer1080 == 4) {
        // (after v127g) 2026-11-25: the SNES 1080p x5 «4:3» (256 px and CRT), built from THE two 16:9 modes
        // (256: HScale 1023 bypassed, DIS 1328/373, HB 1328/272, HBIN 242; CRT: HScale 818, DIS 1436/265, HB 1436/144,
        // HBIN 246; both vertical 1125/40, IF VB 39/41). The 4:3 set squeezes the line to 0.75, so the picture is drawn
        // 4/3 wider: 256 px = 5.0 / 0.75 = 6.667 columns (HScale 768), CRT = 6.25 / 0.75 = 8.333 (HScale 614 = 818 x 0.75).
        // The windows are the 16:9 ones widened 4/3 around their centre (850.5), HBIN as the 16:9 (same clock): STARTING
        // VALUES, CALCULATED, NOT MEASURED: to be placed with the captures.
        if (w256) { hs = 768; hbst = 1481; hbsp = 62; dst = 1481; dsp = 221; hbin = 188; } // (after v127g) THE 256 px, dump of 25-11 11:36 (1440 x 1080 canvas: cells 80 x 80, picture 1273 px centred 720): HB 1481/62, DIS 1481/221, HBIN 188, IF VB 38/40
        else      { hs = 614; hbst = 1513; hbsp = 1539; dst = 1559; dsp = 141; hbin = 248; } // (after v127g) THE CRT, last dump of 25-11 11:40 at HScale 614 (the exact 818 x 0.75; the 612 of 11:39 is gone): HB 1513/1539, DIS 1559/141, HBIN 248, IF VB 38/40
        // (after v127p) PAL: the NTSC placement carried over (windows x 0.8975 and HBIN x 0.896 for the PAL line of 1437 and ADC
        // 2230, the scale as it is (constant 5103 against 5115), the vertical as it is with the IF VB + 52 of the forced
        // 1080p dump, 16/18 -> 68/70). ESTIMATES until the PAL dumps.
        if (pal) {
            hbst = (uint16_t)(((uint32_t)hbst * 8975 + 5000) / 10000);
            hbsp = (uint16_t)(((uint32_t)hbsp * 8975 + 5000) / 10000);
            dst = (uint16_t)(((uint32_t)dst * 8975 + 5000) / 10000);
            dsp = (uint16_t)(((uint32_t)dsp * 8975 + 5000) / 10000);
            hbin = (uint16_t)(((uint32_t)hbin * 8975 + 5000) / 10000);
            // (after v127x) THE SNES PAL 1080p x5 4:3 CRT, centred, dump of 2026-11-25 23:26: HScale 614, HB 1358/1381, DIS HB 1399/119,
            // HBIN 263, DIS VB 1125/40, IF VB 90/92, memory 108 MHz phase 12 (the slot's own).
            // (after v127y), dump of 2026-11-25 23:45 (it came up corrupted; one touch of scale each way recovered it):
            // HB 1358/1381 -> 1360/1381, DIS HB 1399/119 -> 1404/111, DIS VB ST 1125 -> 1120 (SP 40); HScale 614, HBIN 263, IF VB 90/92 stand.
            if (!w256) { hs = 614; hbst = 1360; hbsp = 1381; dst = 1404; dsp = 111; hbin = 263; }
            // (after v127x) and the 256 px («píxel cuadrado 8:7»), dump of 2026-11-25 23:28: HScale 768, HB 1317/32, DIS HB 1317/182, HBIN 171,
            // DIS VB 1125/40, IF VB 90/92, memory 185 MHz phase 12 (the slot's own).
            // (after v127y), dump of 2026-11-25 23:44: DIS HB 1317/182 -> 1313/166 (HB 1313/32), DIS VB 1125 -> 1120 (SP 40),
            // memory 162 MHz phase 12 (memDefaultFor); HScale 768, HBIN 171 and IF VB 90/92 stand.
            if (w256) { hs = 768; hbst = 1313; hbsp = 32; dst = 1313; dsp = 166; hbin = 171; }
        }
        if (!tr.md43)
            md43Snap();
        GBS::VDS_HSCALE::write(hs);
        GBS::VDS_HSCALE_BYPS::write(0);
        GBS::VDS_VSCALE::write(409);
        GBS::VDS_VSCALE_BYPS::write(0);
        GBS::VDS_HB_ST::write(hbst);
        GBS::VDS_HB_SP::write(hbsp);
        GBS::VDS_DIS_HB_ST::write(dst);
        GBS::VDS_DIS_HB_SP::write(dsp);
        GBS::IF_HBIN_ST::write(0);
        GBS::IF_HBIN_SP::write(hbin);
        GBS::VDS_DIS_VB_ST::write(pal ? 1120 : 1125); // (after v127g) as the 16:9 x5; (after v127y) the PAL 4:3 256: 1120 (1120 was my mistake: the 256 4:3 dump has 1125)
        GBS::VDS_DIS_VB_SP::write(40);
        GBS::IF_VB_ST::write(pal ? 90 : 38); // (after v127g) the two 4:3 dumps: 38/40 (PAL + 52)
        GBS::IF_VB_SP::write(pal ? 92 : 40);
        SerialM.print(F("[3x4x-v130k] 1080p x5 | 4:3 | SNES | "));
        SerialM.println(w256 ? F("256 px, the measured placement") : F("CRT, the measured placement"));
        return;
    }
    // (after v127x) 2026-11-26: the Mega Drive's 1080p x5 «4:3», PAL and NTSC. Built from THE 16:9 x5 dumps of 25-11
    // (PAL 256: HScale 1015, HB 1174/145, DIS 1174/317, HBIN 74; PAL 320 square: HScale 812, HB 1280/116, DIS 1280/212, HBIN 226;
    // vertical 1125/40 (1120 in the 320), IF VB 90/92). The 4:3 set squeezes the line to 0.75, so the picture is drawn 4/3
    // wider: HScale x 0.75 (761 and 609), the display window widened 4/3 around its centre (256: centre 745.5, 857 -> 1143
    // units: 1317 / 174; 320: centre 746, 1068 -> 1424: 1458 / 34), HB_ST = DIS_HB_ST and HB_SP at the same distance below
    // DIS_HB_SP as in the 16:9. CALCULATED STARTING VALUES, NOT MEASURED: to be placed with the captures. NTSC: the same
    // numbers with the windows / 0.8975 and HBIN / 0.896 (line 1601 against 1437), clamped to the line, IF VB 38/40 (PAL - 52):
    // even rougher, nothing of the NTSC Mega Drive x5 was at hand.
    if (activeConsoleProfile() == 1 && uopt->integer1080 == 4) {
        if (w256) { hs = 761; hbst = 1317; hbsp = 2;    dst = 1317; dsp = 174; hbin = 74; }
        else      { hs = 609; hbst = 1458; hbsp = 1375; dst = 1458; dsp = 34;  hbin = 226; }
        // (after v127z) THE Mega Drive PAL 1080p x5 4:3 320 px, done by hand from the 256 (the calculated one failed), dump of
        // 2026-11-26 08:02: HScale 609 (as calculated), HB 1376/1392, DIS HB 1397/118, HBIN 266, DIS VB 1125/42, IF VB 90/92, memory
        // 162 MHz phase 13. (Also called «720x4»; HT 1433, VScale 409 and HScale 609 are this mode.) The 256 px (calculated) went well.
        if (pal && !w256) { hbst = 1376; hbsp = 1392; dst = 1397; dsp = 118; hbin = 266; }
        // (after v127z) and the 256 px, centred, dump of 2026-11-26 07:08 (the calculated one «iba bien, solo había que ajustar un poco»):
        // HScale 761, DIS HB 1317/174, HBIN 74, IF VB 90/92 stand; HB 1317/2 -> 1279/1397, DIS VB ST 1125 -> 1120 (SP 42); memory 162/13.
        if (pal && w256) { hbst = 1279; hbsp = 1397; }
        if (!pal) {
            hbst = (uint16_t)(((uint32_t)hbst * 10000 + 4487) / 8975);
            hbsp = (uint16_t)(((uint32_t)hbsp * 10000 + 4487) / 8975);
            dst = (uint16_t)(((uint32_t)dst * 10000 + 4487) / 8975);
            dsp = (uint16_t)(((uint32_t)dsp * 10000 + 4487) / 8975);
            hbin = (uint16_t)(((uint32_t)hbin * 10000 + 4480) / 8960);
            if (hbst > 1600) hbst = 1600;
            if (dst > 1600) dst = 1600;
            if (hbsp > 1600) hbsp = 1600;
        }
        if (!tr.md43)
            md43Snap();
        GBS::VDS_HSCALE::write(hs);
        GBS::VDS_HSCALE_BYPS::write(0);
        GBS::VDS_VSCALE::write(409);
        GBS::VDS_VSCALE_BYPS::write(0);
        GBS::VDS_HB_ST::write(hbst);
        GBS::VDS_HB_SP::write(hbsp);
        GBS::VDS_DIS_HB_ST::write(dst);
        GBS::VDS_DIS_HB_SP::write(dsp);
        GBS::IF_HBIN_ST::write(0);
        GBS::IF_HBIN_SP::write(hbin);
        GBS::VDS_DIS_VB_ST::write(pal ? (w256 ? 1120 : 1125) : (w256 ? 1125 : 1120));
        GBS::VDS_DIS_VB_SP::write(pal ? 42 : 40);
        GBS::IF_VB_ST::write(pal ? 90 : 38);
        GBS::IF_VB_SP::write(pal ? 92 : 40);
        SerialM.print(F("[3x4x-v130k] 1080p x5 | 4:3 | Mega Drive "));
        SerialM.print(pal ? F("PAL | ") : F("NTSC | "));
        SerialM.println(w256 ? F("256 px, calculated placement") : F("320 px, calculated placement"));
        return;
    }
    if (activeConsoleProfile() == 2) {
        // (after v127g) 2026-11-25: the SNES 1080p x4 «4:3». The 4:3 set squeezes the line to 0.75, so the
        // picture is drawn 4/3 wider: 256 px = 4 columns / 0.75 = 5.333 (HScale 959 on the NORMAL clock, 4 whole columns
        // once squeezed), CRT = 5 columns / 0.75 = 6.667 (HScale 767: 5115 / 767). CALCULATED SCALE, as the PC Engine's
        // (959 / 767 measured there). The windows are the PC Engine's for now (STARTING VALUES, not measured on the SNES):
        // to be placed with the captures. Vertical OPEN, the decision («nunca se sabe qué resolución tendrá el juego»):
        // the CRT's own, DIS VB 1059/103, IF VB 16/18 (the whole 239 lines).
        if (w256) { hs = 959; hbst = 1354; hbsp = 256; dst = 1354; dsp = 346; hbin = 278; } // (after v127g) THE 256 px, dump of 25-11 10:47: HB 1354/256, DIS 1354/346, HBIN 278 (picture ~1020 px in the 1440 x 1080 capture, 64 x 64 cells = 16 px at 4 x 4, centred 720 / 539)
        else      { hs = 767; hbst = 1478; hbsp = 48; dst = 1478; dsp = 222; hbin = 168; } // (after v127g) THE CRT, dump of 25-11 10:40: HB 1478/48, DIS 1478/222, HBIN 168 (picture 1702 px, cells 106.7 x 64 = 16 px at 6.667 x 4, centred 961 / 539)
        // (after v127p) PAL: the NTSC placement carried over (windows x 0.8975 and HBIN x 0.896 for the PAL line of 1437 and ADC
        // 2230, the scale as it is (constant 5103 against 5115), the vertical as it is with the IF VB + 52 of the forced
        // 1080p dump, 16/18 -> 68/70). ESTIMATES until the PAL dumps.
        if (pal) {
            hbst = (uint16_t)(((uint32_t)hbst * 8975 + 5000) / 10000);
            hbsp = (uint16_t)(((uint32_t)hbsp * 8975 + 5000) / 10000);
            dst = (uint16_t)(((uint32_t)dst * 8975 + 5000) / 10000);
            dsp = (uint16_t)(((uint32_t)dsp * 8975 + 5000) / 10000);
            hbin = (uint16_t)(((uint32_t)hbin * 8975 + 5000) / 10000);
        }
        if (!tr.md43)
            md43Snap();
        GBS::VDS_HSCALE::write(hs);
        GBS::VDS_HSCALE_BYPS::write(0);
        GBS::VDS_VSCALE::write(512);
        GBS::VDS_VSCALE_BYPS::write(0);
        GBS::VDS_HB_ST::write(hbst);
        GBS::VDS_HB_SP::write(hbsp);
        GBS::VDS_DIS_HB_ST::write(dst);
        GBS::VDS_DIS_HB_SP::write(dsp);
        GBS::IF_HBIN_ST::write(0);
        GBS::IF_HBIN_SP::write(hbin);
        GBS::VDS_DIS_VB_ST::write(w256 ? 1061 : 1063); // (after v127g) the dumps: 256 px 1061/101 (10:47), CRT 1063/101 (10:40)
        GBS::VDS_DIS_VB_SP::write(101);
        GBS::VDS_VB_ST::write(94);
        GBS::VDS_VB_SP::write(96);
        GBS::IF_VB_ST::write(pal ? 68 : 16); // PAL: + 52, the forced PAL dump 68/70
        GBS::IF_VB_SP::write(pal ? 70 : 18);
        // (after v127p) THE PAL SNES 1080p x4 «4:3» (the log header of those dumps said 4:3: they were THIS mode, my first guess
        // was right and I moved them to the 16:9 by mistake; the 16:9 is back to the carried-over values). The 256 px button
        // (dump of 15:23, «píxel cuadrado 8:7») runs the ADC at 1784: HScale 1023 bypass, HB 1092/290, DIS HB 1088/407, HBIN 140,
        // DIS VB 1062/100, IF VB 68/70, IF line 898/963 (recipe), ST2/SP2 844/104. The CRT button (dump of 15:05) at 1900:
        // HScale 872, HB 1436/230, DIS HB 1170/325, HBIN 214, DIS VB 1062/100, IF VB 68/70, IF line 956/1021, ST2/SP2 844/104.
        // The clock is written in both because each step leaves the other's behind.
        if (pal) {
            GBS::PLLAD_MD::write(w256 ? 1784 : 1888); // CRT: 1888 (the dump of 15:42: IF HRST 950 = 1888 / 2 + 6)
            latchPLLAD();
            // (after v127p) the target (montage of 16:27): in the 4:3 the picture is drawn 4/3 wider (the set squeezes the line to
            // 0.75): 256 px = 5.333 columns, HScale 1023 x 0.75 = 767 at 1784; CRT = 6.667 columns, HScale 872 x 0.75 = 654
            // at 1900. ONLY the scale is changed: the windows are still those of the 15:23 / 15:05 dumps (ESTIMATE until the
            // 4:3 dumps: the display window is a mask, not the picture's width, so it is not scaled by guess).
            GBS::VDS_HSCALE::write(w256 ? 767 : 654);
            GBS::VDS_HSCALE_BYPS::write(0);
            GBS::VDS_HB_ST::write(w256 ? 49 : 16); // 256: the 4:3 dump of 15:33
            GBS::VDS_HB_SP::write(w256 ? 154 : 27);
            GBS::VDS_DIS_HB_ST::write(w256 ? 1202 : 1313);
            GBS::VDS_DIS_HB_SP::write(w256 ? 295 : 183);
            GBS::IF_HBIN_SP::write(w256 ? 138 : 145); // (after v127y) CRT: the dump of 2026-11-25 23:47, HBIN 151 -> 145 (centring); the rest stands
            GBS::VDS_DIS_VB_ST::write(w256 ? 1062 : 1063);
            GBS::VDS_DIS_VB_SP::write(w256 ? 100 : 101);
            GBS::IF_VB_ST::write(68);
            GBS::IF_VB_SP::write(70);
            GBS::IF_HSYNC_RST::write(w256 ? 898 : 950);
            GBS::IF_LINE_SP::write(w256 ? 963 : 1015);
            GBS::IF_HB_ST2::write(w256 ? 844 : 896);
            GBS::IF_HB_SP2::write(104);
        }
        SerialM.print(F("[3x4x-v130k] 1080p x4 | 4:3 | SNES | "));
        SerialM.println(w256 ? F("256 px, the measured placement") : F("CRT, the measured placement"));
        return;
    }
    if (pal) {
        if (w256) { hs = 964; hbst = 1185; hbsp = 119; dst = 1191; dsp = 302; hbin = 78; }
        else      { hs = 768; hbst = 1292; hbsp = 1430; dst = 1303; dsp = 188; hbin = 96; }
    } else {
        if (w256) { hs = 964; hbst = 1341; hbsp = 212; dst = 1341; dsp = 348; hbin = 194; }
        else      { hs = 771; hbst = 1465; hbsp = 90; dst = 1465; dsp = 223; hbin = 208; }
    }
    // (after v110b) the registers as the 16:9 left them, for undoAll() (written back before any other undo)
    if (!tr.md43)
        md43Snap();
    GBS::VDS_HSCALE::write(hs);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(512);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(hbst);
    GBS::VDS_HB_SP::write(hbsp);
    GBS::VDS_DIS_HB_ST::write(dst);
    GBS::VDS_DIS_HB_SP::write(dsp);
    GBS::IF_HBIN_ST::write(0);
    GBS::IF_HBIN_SP::write(hbin);
    GBS::VDS_DIS_VB_ST::write(1031);
    GBS::VDS_DIS_VB_SP::write(pal ? 133 : 135);
    GBS::VDS_VB_ST::write(pal ? 80 : 94);
    GBS::VDS_VB_SP::write(pal ? 82 : 96);
    GBS::IF_VB_ST::write(pal ? 59 : 12);
    GBS::IF_VB_SP::write(pal ? 61 : 14);
    SerialM.print(F("[3x4x-v130k] 1080p x4 | 4:3 | Mega Drive "));
    SerialM.print(pal ? F("PAL | ") : F("NTSC | "));
    SerialM.println(w256 ? F("256 px, whole pixels once the set is in 4:3 | measured placement")
                         : F("320 px (a 256 px game at its CRT size) | measured placement"));
}

void dcVgaLoad()
{
    dcVgaStockMd = GBS::PLLAD_MD::read();
    dcVga1080Apply();
}

// «16:9», 2 x 2 exact (the state of 2026-11-17 04:30, channel 11): at HScale 1023, the narrowest the scaler draws, the
// VGA line comes out at 2.052 columns, so the ADC clock goes 2.5 % down (Rama's 1920x1080 for scaling RGB HV, the input
// line in proportion: HRST 1279 -> 1247, LINE +65). Measured: 64.1 output px per 32 px cell = 2.003 columns, 64 lines =
// 2.000; the 1280 x 960 picture centred. WARNING, Rama's note on the horizontal PLL: a clock in a narrow band disturbs
// the ESP's wifi. This one did on the router's channel 6 (the web lost the board until the console was switched off)
// and not on channel 11. So it is never the default: the «1080p» button loads «4:3», and this shape is chosen by hand.
static void dcVga2x2()
{
    if (dcVgaStockMd == 0)
        dcVgaStockMd = GBS::PLLAD_MD::read();
    const uint16_t md = (uint16_t)(((uint32_t)dcVgaStockMd * 975 + 500) / 1000);
    dcVgaClock(md, 1247, 1312, 1171, 191); // (2026-11-17 14:54) the centring with Sonic (14:35 grid: 1173 / 193)
    GBS::VDS_HSCALE::write(1023);
    GBS::VDS_HSCALE_BYPS::write(1);
    GBS::VDS_VSCALE::write(512);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(1296);
    GBS::VDS_HB_SP::write(262);
    GBS::VDS_DIS_HB_ST::write(1314);
    GBS::VDS_DIS_HB_SP::write(374);
    // (2026-11-17) the vertical of the 4:3 (same VScale 512), the two shapes keep the same height and place, so
    // switching them does not make the picture jump (its own 16:9 state had DIS_VB 1125 / 42, VB 28 / 30, IF VB 522 / 524)
    GBS::VDS_DIS_VB_ST::write(1093);
    GBS::VDS_DIS_VB_SP::write(84);
    GBS::VDS_VB_ST::write(63);
    GBS::VDS_VB_SP::write(65);
    GBS::IF_VB_ST::write(12);
    GBS::IF_VB_SP::write(14);
    SerialM.print(F("[3x4x-v130k] 1080p | 16:9 | Dreamcast VGA | 2 x 2 | ADC clock "));
    SerialM.print(dcVgaStockMd);
    SerialM.print(F(" -> "));
    SerialM.print(md);
    SerialM.println(F(" (-2.5 %): if the web is lost, switch the console off and change the router's wifi channel"));
}

// «4:3», the concept (2026-11-17 05:00): the SAME HEIGHT as the 16:9 and 4/3 wider, so a set switched to 4:3 shows the
// 16:9 picture exactly, and a 16:9 set shows the anamorphic games in their shape (1707 x 960 is 16:9). His state of 05:08
// on Rama's 1080p with Rama's ADC clock (no wifi risk): HScale 788 = 2.665 columns (85.3 output px per 32 px cell; 4/3 of
// 2 is 2.667), frame 111..1814 centred at 962; VScale 512 = 2 lines exact, 960 lines. It replaces the 04:06 state that
// filled the whole 1080 at 2.25 lines (HScale 700 / VScale 455), which was not the 4:3.
// (after v108d) 720p, the states of 2026-11-17 on Rama's 1280x720 with Rama's ADC clock (no wifi risk), same height in
// both, VScale 683 = 1.5 lines (the 480 lines in the 720, not whole): «4:3» HScale 480 = 2.002 columns (32.0 output px
// per 16 px cell in the 1:1 720p capture), the 1280 whole; «16:9» HScale 641 = 1.500 columns (24.0 per cell), 960 wide.
static void dcVga720()
{
    const bool w43 = (uopt->integer720 == 1);
    GBS::VDS_HSCALE::write(w43 ? 480 : 641);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(683);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(w43 ? 2292 : 2178);
    GBS::VDS_HB_SP::write(w43 ? 123 : 443);
    GBS::VDS_DIS_HB_ST::write(w43 ? 2320 : 2020);
    GBS::VDS_DIS_HB_SP::write(w43 ? 338 : 605);
    GBS::VDS_DIS_VB_ST::write(750);
    GBS::VDS_DIS_VB_SP::write(26);
    GBS::VDS_VB_ST::write(14);
    GBS::VDS_VB_SP::write(16);
    // (2026-11-17 14:46) the centring with Sonic Adventure (a game matters more than the grid): 4:3 canvas 51 / 147 (was
    // 56 / 152) and the capture one line later, 27 / 29 (was 26 / 28), in both shapes so they keep the same vertical
    GBS::IF_VB_ST::write(27);
    GBS::IF_VB_SP::write(29);
    GBS::IF_HB_ST2::write(w43 ? 51 : 72); // 16:9: the centring with Sonic, 14:49 (was 78 / 174)
    GBS::IF_HB_SP2::write(w43 ? 147 : 168);
    SerialM.println(w43 ? F("[3x4x-v130k] 720p x3 | 4:3 | Dreamcast VGA | 2 columns x 1.5 lines, the whole width | measured placement")
                        : F("[3x4x-v130k] 720p x3 | 16:9 | Dreamcast VGA | 1.5 columns x 1.5 lines | measured placement"));
}

void dcVga1080Apply()
{
    if (uopt->presetPreference == Output720P) {
        dcVga720();
        return;
    }
    if (!uopt->integer1080Wide) {
        dcVga2x2();
        return;
    }
    if (dcVgaStockMd && GBS::PLLAD_MD::read() != dcVgaStockMd)
        dcVgaClock(dcVgaStockMd, 1279, 1344, 1195, 185); // back from the 2 x 2 without a reload
    GBS::VDS_HSCALE::write(788);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(512);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(60);
    GBS::VDS_HB_SP::write(78);
    GBS::VDS_DIS_HB_ST::write(1473);
    GBS::VDS_DIS_HB_SP::write(220);
    GBS::VDS_DIS_VB_ST::write(1093);
    GBS::VDS_DIS_VB_SP::write(84);
    GBS::VDS_VB_ST::write(63);
    GBS::VDS_VB_SP::write(65);
    GBS::IF_VB_ST::write(12);
    GBS::IF_VB_SP::write(14);
    GBS::IF_HB_ST2::write(1195); // (2026-11-17 14:52) the centring with Sonic (was 1199 / 189)
    GBS::IF_HB_SP2::write(185);
    GBS::IF_HSYNC_RST::write(1279); // Rama's own, in case a 2 x 2 was on before
    GBS::IF_LINE_SP::write(1344);
    SerialM.println(F("[3x4x-v130k] 1080p | 4:3 | Dreamcast VGA | 2.67 columns x 2 lines, the 16:9 picture 4/3 wider | measured placement"));
}

// His measured states, written absolute on top of whatever the mode's own path placed (the Dreamcast places as the
// Mega Drive does, which is the path every one of these was measured on). Only what the dumps show changing is written.
struct DcPlace {
    uint16_t hs, vs, hbSt, hbSp, dSt, dSp, dvSt, dvSp, vbSt, vbSp, ifVbSt, ifVbSp, hbin;
};

static void writeDc1080(const DcPlace &p, bool pal)
{
    GBS::VDS_HSCALE::write(p.hs);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(p.vs);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(p.hbSt);
    GBS::VDS_HB_SP::write(p.hbSp);
    GBS::VDS_DIS_HB_ST::write(p.dSt);
    GBS::VDS_DIS_HB_SP::write(p.dSp);
    GBS::VDS_DIS_VB_ST::write(p.dvSt);
    GBS::VDS_DIS_VB_SP::write(p.dvSp);
    GBS::VDS_VB_ST::write(p.vbSt);
    GBS::VDS_VB_SP::write(p.vbSp);
    GBS::IF_VB_ST::write(p.ifVbSt);
    GBS::IF_VB_SP::write(p.ifVbSp);
    if (pal)
        GBS::IF_HBIN_ST::write(0); // the PAL dumps (and the Mega Drive's PAL path) read 0
    GBS::IF_HBIN_SP::write(p.hbin);
}

// true when a measured Dreamcast state was written
static bool dc1080Measured()
{
    const uint8_t dc = dcSignal();
    const bool x5 = (uopt->integer1080 == 4);
    const char *what = nullptr;
    if (dc == 1 && !x5) {
        // 240p, 1080p x4: 4 x 4, the whole 320 x 240 (the final state of 23:40, HScale 1021 = 4.000 columns)
        // (2026-11-17 13:52) the edge for it: window 1318 / 374 wide, 1070 / 75 high (was 1325 / 349, 1066 / 97); memory
        // window start 1318 (was 1314)
        writeDc1080(DcPlace{1021, 512, 1318, 264, 1318, 374, 1070, 75, 94, 96, 16, 18, 236}, false);
        what = "240p | 4 x 4, the 240 lines whole";
    } else if (dc == 1 && x5) {
        // 240p, 1080p x5: HScale 816 = 5.0007 columns, VScale 409 = 5 lines, 216 of the 240 lines (measured on the SNES
        // path, same placement as this one at 320 px; no final framing of the yet)
        // (2026-11-17 14:12) the final framing: window 1432 / 258, memory start 1432, capture start 254 (was 1443 / 265,
        // 1443, 246)
        writeDc1080(DcPlace{816, 409, 1432, 142, 1432, 258, 1125, 40, 28, 30, 39, 41, 254}, false);
        what = "240p | 5 x 5, 216 of the 240 lines (experimental)";
    } else if (dc == 2 && !x5) {
        // 288p, 1080p x4: HScale 1010 = 4.000 columns (64 px cells, 1280 px frame), the 264 lines whole (1056)
        writeDc1080(DcPlace{1010, 512, 1201, 155, 1201, 327, 1120, 7, 33, 35, 45, 47, 116}, true);
        what = "288p | 4 x 4, the 264 lines whole";
    } else if (dc == 3 && !x5 && uopt->integer1080Wide) {
        // 480i, «4:3», the concept (2026-11-17): the 16:9 picture (2 x 2) with the SAME HEIGHT and 4/3 wider, so a set in
        // 4:3 shows the 16:9 picture exactly and a 16:9 set shows the anamorphic games in their shape. His state of 13:00
        // on this mode: HScale 766 = 2.666 columns (42.66 px per 16 px cell; 4/3 of 2 is 2.667), frame 106..1812 = 1707,
        // centred at 959; VScale 512 = 2 lines, 960. It replaces the 2.25-line full screen (681 / 455) of 22:54.
        writeDc1080(DcPlace{766, 512, 1466, 126, 1466, 221, 1084, 113, 94, 96, 8, 10, 295}, false);
        what = "480i | 4:3 | 2.67 columns x 2 lines, the 16:9 picture 4/3 wider";
    } else if (dc == 3 && !x5) {
        // 480i, «16:9»: 2 x 2 exact, 1280 x 960 with bands (the centred state of 22:44)
        writeDc1080(DcPlace{1021, 512, 1312, 258, 1312, 377, 1084, 115, 94, 96, 8, 10, 228}, false);
        what = "480i | 16:9 | 2 x 2, 1280 x 960";
    } else if (dc == 4 && !x5 && uopt->integer1080Wide) {
        // 576i, «4:3», the concept (the 16:9 picture, same height, 4/3 wider). His state of 2026-11-17 13:07 on this mode:
        // HScale 758 = 2.664 columns (42.63 px per 16 px cell), frame 106..1812, centred; VScale 512 = 2 lines, the 480
        // lines at 60..1019. It replaces the stock-preset full screen (674 / 455) of 03:05.
        writeDc1080(DcPlace{758, 512, 1359, 1385, 1319, 114, 1120, 7, 80, 82, 60, 62, 76}, true);
        what = "576i | 4:3 | 2.67 columns x 2 lines, the 16:9 picture 4/3 wider";
    } else if (dc == 4 && !x5) {
        // 576i, «16:9»: 2 x 2 exact, the 480 lines the Dreamcast draws (the dump of 23:06)
        // (2026-11-17) the vertical of the 4:3 (IF VB 60 / 62, window 1120 / 7), so switching the two
        // shapes does not make the picture jump up and down
        writeDc1080(DcPlace{1011, 512, 1218, 151, 1229, 331, 1120, 7, 80, 82, 60, 62, 112}, true);
        what = "576i | 16:9 | 2 x 2, 1280 x 960";
    }
    if (!what)
        return false;
    SerialM.print(F("[3x4x-v130k] 1080p | Dreamcast "));
    SerialM.print(what);
    SerialM.println(F(" | measured placement"));
    return true;
}


static void integer1080ApplyInner(uint16_t nativeWidth) // v107n: wrapped, see integer1080Apply() at the end
{
    undoAll();
    const bool prevFine = scaleHorizontalFine;
    scaleHorizontalFine = false;
    const bool x5 = (uopt->integer1080 == 4);
    // v42: 256 px sources get the SAME width per pixel as 320 px sources (pixel perfect, the picture is simply narrower):
    //  - x5: 5 columns per pixel = the plain sampling of the table (1280 px wide picture)
    //  - x4: 4 columns per pixel needs the ADC clock at 80% (table reloaded, see integer1080Narrow): 1024 px wide picture
    const bool narrow = (!x5 && nativeWidth == 256 && uopt->integer1080Narrow);
    // v58: wide fill state (1920 px picture, NTSC only, ESTIMATE): the x5 320 px placement with HScale 682 and a full width window
    // (after v107z) not with the Dreamcast: there integer1080Wide is the «4:3» shape of 480i, placed from the measurement
    // on top of the plain x4 path (the path it was measured on), never through this estimate
    const bool wide = (uopt->integer1080Wide != 0) && !narrow && rto->presetID != 0x15 && dcSignal() == 0;

    // (after v130) THE MEMORY OF EVERY OTHER 1080p MODE. applyMemSettings() was reached only by the SNES PAL x5 block
    // below, so the Mega Drive, the PC Engine and every NTSC 1080p mode kept the clock the preset loads (129 MHz) and the
    // stored or factory pair only came back by pressing «memoria de fábrica». Memory first, placement afterwards, the same
    // rule as the SNES block, for every console: what is stored for the mode always comes back.
    if (!(rto->presetID == 0x15 && snesProfile() && !pceProfile())) {
        if (applyMemSettings()) {
            ResetSDRAM();
            scanlinesAfterSdramReset();
        }
    }
    // v96: THIS BLOCK MUST COME FIRST. Until now it sat below the "if (presetID == 0x15)" estimate block, which returns,
    // so for PAL it was DEAD CODE: every measurement made for PAL 1080p from v88 onwards was written into lines
    // that never ran, and the picture always came from the v42 estimate below (its own comment says "ROUND 1, not
    // measured"). That is why nothing changed ever moved the picture, and why a profile saved by hand worked:
    // the saved profile was the only thing that ever reached the chip. The estimate below now serves only the Mega Drive
    // and the PC Engine in PAL, which keep behaving exactly as they did.
    if (rto->presetID == 0x15 && snesProfile() && !pceProfile()) {
        const uint8_t step = uopt->sizeStep;
        const char *label = "square";
        // v93: THE MEMORY FIRST, THE PLACEMENT AFTERWARDS. This is the rule the whole 1080p PAL path was breaking. The clock
        // and the phase move the picture, and a ResetSDRAM disturbs it, so whatever is written before them does not survive.
        // The load wrote the placement and then reset the SDRAM twice on top of it (once in the load itself, once in the
        // 700 ms reapply), and v92 made it worse by applying the per-step memory after the windows. The user noticed the
        // symptom from the other side: a profile saved by hand loads perfectly, because it is one complete register set
        // and because a custom preset makes all of this code stand aside, so nothing touches the picture afterwards.
        if (applyMemSettings()) {
            ResetSDRAM();
            scanlinesAfterSdramReset();
        }
        // v97: and THEN the input line length, which this block never wrote. The estimate block it replaced called
        // applyIfRecipe() first thing; this one did not, so IF_HSYNC_RST kept the preset's own 1276 while the ADC was
        // running at PLLAD_MD 1784, whose real line is 892 units. An input line declared far longer than it is makes the
        // picture wrap: the grid came out repeated in four pieces. The recipe is not a guess, it reproduces every IF
        // value in the good dumps exactly: x4 (MD 1784) -> 898 / 963 with the canvas at 844, x5 (MD 2230) -> 1121 / 1186
        // with the canvas at 1068. Those are the numbers to the unit, canvas included.
        applyIfRecipe();
        if (x5) {
            // v86: the four steps measured in 1080p x5. Order: square, CRT, wide, fills the screen.
            uint16_t hs, hbst, hbsp, dst, dsp, hbin;
            switch (step) {
                // v92: the constant of this mode is 5103, which its own two dumps give twice over (1023 -> 1277 px and
                // 818 -> 1597 px). The four steps now aim at the same shapes as every other mode: pixel 1.00, 1.167, 1.33,
                // and then the full width of the screen. The old table had 818 as the CRT at pixel 1.25 and 854 as "wide" at
                // 5.98 columns, which is NARROWER than the CRT before it, so the cycle went backwards at that point. That is
                // why those two steps looked as if they had never been created.
                case 1: // CRT, pixel 1.168. 875 glitches on every memory setting tried; 874 is clean on the feedback clock
                        hs = 874; hbst = 1184; hbsp = 85; dst = 1270; dsp = 226; hbin = 126;
                        label = "16:9 | fills the height, a few lines cropped | CRT shape, as a TV showed it"; break;
                case 2: // v92: wide, pixel 1.33, measured at 02:16. HB_SP is past HB_ST here: at this width the blanking
                        // window wraps round the end of the line, which is normal for the widest states and must be written
                        // as it stands, not "corrected". Feedback clock and phase 12, the same pair as the CRT step.
                        // v107m: the border of 2026-11-12 10:39 for the garbage on the right (it had been left out when the
                        // measurement was taken): HB_ST and DIS_HB_ST both pulled in to 1311, the relation that keeps these modes clean.
                        hs = 765; hbst = 1311; hbsp = 1412; dst = 1311; dsp = 184; hbin = 88;
                        label = "16:9 | fills the height, a few lines cropped | wider than a CRT"; break;
                case 3: // v92: fill, 7.50 columns, 1920 px exactly, measured at 02:22. The hardest one of the four: the memory had
                        // to be hunted down to 129 MHz with phase 8, and the blanking window wraps here as well.
                        hs = 680; hbst = 1323; hbsp = 1339; dst = 1403; dsp = 120; hbin = 107;
                        label = "16:9 | fills the height, a few lines cropped | fills the screen width"; break;
                default: // square, pixel 1.00
                        // v99: re-centred at 12:55 because the mode was showing a lot of garbage. the dump is headed
                        // "step 4/4 fill", but with the scaler bypassed every step rendered the unscaled picture, which is
                        // this one, so the placement belongs here. Nine units of left edge against the old 1169 / 329.
                        // Taken with the memory at 129 MHz phase 8 (the fill step's pair, applied because the cycle thought
                        // it was on step 4). This entry keeps 185 / 13, its own pair for the square: if it comes up dirty,
                        // 129 / 8 is the first thing to try, since that is what was running when it was centred.
                        hs = 1023; hbst = 1171; hbsp = 166; dst = 1171; dsp = 320; hbin = 107;
                        label = "16:9 | fills the height, a few lines cropped | square pixels, sharpest"; break;
            }
            GBS::VDS_HSCALE::write(hs);
            // v99: and TURN THE SCALER BACK ON. HScale 1023 is not a ceiling, it is the value at which VideoScaling.cpp sets
            // VDS_HSCALE_BYPS to 1 and bypasses the horizontal scaler altogether. The square step sits at 1023, so the bit is
            // left on, and every other step wrote its HScale into a register the chip was ignoring: the cycle announced four
            // widths in the log and the picture never moved. These values were measured by hand with the scale buttons, which
            // clear the bit on the way past, which is why they worked under the fingers and not from here.
            GBS::VDS_HSCALE_BYPS::write(hs == 1023 ? 1 : 0);
            GBS::VDS_HB_ST::write(hbst);
            GBS::VDS_HB_SP::write(hbsp);
            GBS::VDS_DIS_HB_ST::write(dst);
            GBS::VDS_DIS_HB_SP::write(dsp);
            GBS::IF_HBIN_SP::write(hbin);
            // v88: the one vertical for all four steps, from its own dumps. Both the 1023 and the 818 states came back with
            // IF VB 90 / 92, two lines above the 92 / 94 this used to write, which is why the picture arrived low and had
            // to be nudged up after every load. DIS_VB 1125 / 42 already matched.
            // v92: back to 92 / 94. Three dumps in a row (01:59, 02:00, 02:11) read 92 / 94, against the two older ones that
            // read 90 / 92, so the newer centring is the one in use. The mask is right either way and is not a
            // measurement at all: 224 x 5 = 1120 lines do not fit in 1080, so it shows everything, 42 / 1125.
            GBS::VDS_DIS_VB_ST::write(1125);
            GBS::VDS_DIS_VB_SP::write(42);
            GBS::IF_VB_ST::write(92);
            GBS::IF_VB_SP::write(94);
            // (after v127p) THE NTSC SNES 1080p x5 CARRIED OVER to the PAL preset, as the x4 below: the horizontal windows
            // x 0.8975 (PAL line 1437 against 1601), HBIN x 0.896 (ADC 2230 against 2489), the vertical as it is (the NTSC
            // IF VB 39/41 + 52 = 91/93, next to the 92/94 of its own PAL measurements). Step 0 = the 256 px button (the NTSC:
            // bypass 1023, HB 1328/272, DIS HB 1328/373, HBIN 242, DIS VB 1125/40); step 1 = the CRT button (HScale 818,
            // HB 1436/144, DIS HB 1436/265, HBIN 246). The PAL constant of the mode is 5103 against the NTSC 5115, so the
            // scales stay. ESTIMATES until the PAL dumps.
            if (step == 0) {
                GBS::VDS_HSCALE::write(1023);
                GBS::VDS_HSCALE_BYPS::write(1);
                // (after v127x) THE SNES PAL 1080p x5 256 px (16:9), dump of 2026-11-25 23:20: HScale 1023 bypassed, DIS VB 1125/40, IF VB 91/93,
                // IF line 1121/1186, ST2/SP2 1068/104 as they were; HB 1192/244 -> 1188/228, DIS HB 1192/335 -> 1188/326, HBIN 217 -> 229.
                // Memory 162 MHz phase 13 (memDefaultFor).
                GBS::VDS_HB_ST::write(1188);
                GBS::VDS_HB_SP::write(228);
                GBS::VDS_DIS_HB_ST::write(1188);
                GBS::VDS_DIS_HB_SP::write(326);
                GBS::IF_HBIN_SP::write(229);
                GBS::VDS_DIS_VB_ST::write(1125);
                GBS::VDS_DIS_VB_SP::write(40);
                GBS::IF_VB_ST::write(91);
                GBS::IF_VB_SP::write(93);
                label = "16:9 | fills the height, a few lines cropped | 256 px, the PAL one";
            } else if (step == 1) {
                GBS::VDS_HSCALE::write(818);
                GBS::VDS_HSCALE_BYPS::write(0);
                // (after v127x) THE SNES PAL 1080p x5 CRT (16:9), dump of 2026-11-25 23:22: HScale 818, HB 1289/129, DIS VB 1125/40, IF VB 91/93,
                // IF line 1121/1186 and memory 144 MHz phase 12 as they were; DIS HB 1289/238 -> 1293/218, HBIN 220 -> 260.
                GBS::VDS_HB_ST::write(1289);
                GBS::VDS_HB_SP::write(129);
                GBS::VDS_DIS_HB_ST::write(1293);
                GBS::VDS_DIS_HB_SP::write(218);
                GBS::IF_HBIN_SP::write(260);
                GBS::VDS_DIS_VB_ST::write(1125);
                GBS::VDS_DIS_VB_SP::write(40);
                GBS::IF_VB_ST::write(91);
                GBS::IF_VB_SP::write(93);
                label = "16:9 | fills the height, a few lines cropped | CRT, the NTSC one carried over";
            }
        } else {
            // 1080p x4: only two steps are measured. 256 px = 4.005 col x 4.000 lines, square and whole; 320 px = 5.005 col
            // x 4.000 lines, whole AND the 1.25 CRT shape. Steps 2 and 3 have no measurement yet and reuse the wide one.
            //
            // v88, from the repair procedure logged on 2026-11-10 23:02. This mode arrives with sync garbage filling
            // the right of the line, and the only way out is to move the CAPTURE canvas left, away from the
            // garbage (moveCanvasHorizontal, which writes IF_HB_ST2 / IF_HB_SP2), bring the output window back right
            // (shiftHorizontalRight on VDS_HB_ST / SP), then pull the display edge in to mask what is left. The first of those
            // three was never written by this firmware IN ANY MODE, so every preset load put the canvas back where it was and
            // the picture went straight back into the garbage. That, and not the SDRAM clock, is why this mode never held.
            // v90: two steps are measured now, both measured, both already clear of the garbage.
            //   step 1 (square, 256 button): HB 1224 / 276, edge 1088 / 381, HBIN_SP 104   (dump of 23:02)
            //   step 3 (wide):               HB 1220 / 272, edge 1084 / 381, HBIN_SP  98   (dump of 23:37)
            // The CRT step has no state of its own: it was loading something narrower and dirtier than the 256 was
            // before the repair, which was the old constructed 1200 / 166 entry. It is gone. Until it is measured, the CRT
            // step loads the square one and the log says so, and the fill step loads the wide one.
            // v96: BACK TO THE OWN PAL MEASUREMENTS. The NTSC transplant of v95 was built on the belief that the PAL
            // states were somehow wrong, because they kept coming back dirty after a load. They were not wrong: this whole
            // block was dead code, sitting below a "if (presetID == 0x15) { ... return; }" that caught every PAL load, so
            // the measurements never reached the chip and the picture always came from the v42 estimate. Now that the block
            // runs, its own numbers get their first real test. The transplant, if it is ever needed again:
            //   HSYNC_RST 1609, HS 20 / 72, VSCALE 512, VB 94 / 96, DIS_VB 1033 / 133, IF_VB 16 / 18,
            //   IF_HSYNC_RST 1001, IF_LINE_SP 1066, HB 1290 / 310, edge 1230 / 476, HBIN_SP 71, canvas 948 / 104.
            // v98: THE FOUR WIDTH STEPS. The constant of this mode is K = 4100, measured on its own capture of the square
            // step: the grid came out 1026 x 897 px, which is 4.008 columns per source pixel and a pixel shape of 1.002,
            // and the 897 confirms the 224 x 4 = 896 lines. HScale = K / columns, and columns = shape x 4 lines:
            //   square 4.00 col -> 1025 (the measured 1023 gives 4.008, left as it was)
            //   CRT    4.67 col ->  879, 1194 px, shape 1.167
            //   wide   5.33 col ->  769, 1365 px, shape 1.333
            //   fill   7.50 col ->  547, 1920 px, shape 1.874 (the whole screen width)
            // The CRT and wide values land within a few units of the x5 ones (874 and 765) because for the same pixel shape
            // the extra line and the lower ADC clock cancel out. The fill does not, because there the target is a width.
            //
            // v107f: all four steps are measured now. The square uses the shared block below; CRT, wide and fill override the
            // horizontal window and capture start with its own centring (and wide / fill also the vertical window).
            // v99: see the note in the x5 block above. Every step must set VDS_HSCALE_BYPS itself, or the scaler stays
            // bypassed at the square step's 1023 and no other width can take effect.
            // v107f: the CRT step is measured (the dump of 2026-11-11 22:26, memory 162 MHz phase 15). It has its own window
            // and capture start, written after the shared block below so they override it.
            if (step == 1) {
                GBS::VDS_HSCALE::write(879);
                GBS::VDS_HSCALE_BYPS::write(0);
                label = "16:9 | whole picture | CRT shape, as a TV showed it";
            } else if (step == 2) {
                GBS::VDS_HSCALE::write(769);
                GBS::VDS_HSCALE_BYPS::write(0);
                label = "16:9 | whole picture | wider than a CRT";
            } else if (step == 3) {
                GBS::VDS_HSCALE::write(547);
                GBS::VDS_HSCALE_BYPS::write(0);
                label = "16:9 | whole picture | fills the screen width";
            } else {
                GBS::VDS_HSCALE::write(1023);
                GBS::VDS_HSCALE_BYPS::write(1); // 1023 = bypass, which is the state the square step was measured in
                label = "16:9 | whole picture | square pixels, sharpest";
            }
            // the repair of 01:51, made with the memory already at the clock and phase this mode loads. The three steps
            // above start from this same window on purpose.
            GBS::VDS_HB_ST::write(1220);
            GBS::VDS_HB_SP::write(272);
            GBS::VDS_DIS_HB_ST::write(1088);
            GBS::VDS_DIS_HB_SP::write(381);
            // v105: 98, not 96. Re-centred by hand on 2026-11-11 16:15 and that is the only register that moved
            // from the state signed off at 12:37. Two units of capture canvas. The "wide" measurement of
            // the night before also read 98, so the two agree.
            GBS::IF_HBIN_SP::write(98);
            // the capture canvas, moved off the sync garbage at the end of the line. Same for every step: the garbage
            // belongs to the line, not to the width.
            GBS::IF_HB_ST2::write(844);
            GBS::IF_HB_SP2::write(104);
            // one vertical for all four steps. The exact centring is 42 + 92 = 134 and 134 + 896 = 1030, but 1030 CUTS the
            // last line of the picture: measured on the capture, the grid occupies 897 output lines and not 896, so a window
            // that ends at 1030 clips the magenta line the SNES draws at the bottom. It has to stay visible, and the
            // own working state reads 1036, so that is what goes in. Six lines of slack is the price of seeing the last line.
            GBS::VDS_DIS_VB_ST::write(1036);
            GBS::VDS_DIS_VB_SP::write(134);
            GBS::IF_VB_ST::write(59);
            GBS::IF_VB_SP::write(61);
            if (step == 1) { // v107f: the centring of the CRT step; everything else is the shared block above
                GBS::VDS_HB_ST::write(1212);
                GBS::VDS_HB_SP::write(264);
                GBS::VDS_DIS_HB_ST::write(1144);
                GBS::VDS_DIS_HB_SP::write(349);
                GBS::IF_HBIN_SP::write(210);
            } else if (step == 2) { // v107f: the centring of the wide step (dump of 22:37, memory 162 MHz phase 8)
                GBS::VDS_HB_ST::write(1208);
                GBS::VDS_HB_SP::write(154);
                GBS::VDS_DIS_HB_ST::write(1213);
                GBS::VDS_DIS_HB_SP::write(301);
                GBS::IF_HBIN_SP::write(136);
                GBS::VDS_DIS_VB_ST::write(1073); // the vertical window for this step, not the shared 1036 / 134
                GBS::VDS_DIS_VB_SP::write(97);
            } else if (step == 3) { // v107f: the centring of the fill step (dump of 22:43, memory 162 MHz phase 8)
                // VDS_HB_ST > VDS_HB_SP here is not a typo: the blanking window wraps past the end of the line, as in the
                // x5 wide step
                GBS::VDS_HB_ST::write(1314);
                GBS::VDS_HB_SP::write(1330);
                GBS::VDS_DIS_HB_ST::write(1377);
                GBS::VDS_DIS_HB_SP::write(81);
                GBS::IF_HBIN_SP::write(90);
                GBS::VDS_DIS_VB_ST::write(1073);
                GBS::VDS_DIS_VB_SP::write(97);
            }
            // (after v127o) THE NTSC SNES 1080p x4 CARRIED OVER to the PAL preset (2026-11-25: the PAL profile
            // is a copy of the NTSC one). The PAL line is 1437 units against the NTSC 1601 (0.8975) and the ADC runs at the
            // same 80 % clock (1784 against 1991, 0.896), so the horizontal windows are the NTSC ones x 0.8975 and the
            // vertical ones are the NTSC ones as they are (same 1125 lines, same input lines). Step 0 = the 256 px button:
            // from the v127n dump of 13:47 (HB 1290/310, DIS HB 1230/468, HBIN 71, DIS VB 1061/102, IF VB 67/69, bypass
            // 1023). Step 1 = the CRT button: the NTSC CRT (HB = DIS_HB_ST 1322, SP 268 / 378, HBIN 236, DIS VB 1059/103,
            // IF VB 16/18 + 52) at 5.0 columns, which at the 80 % clock is HScale 4100 / 5 = 820, HBIN x 0.717. ESTIMATES
            // until the dumps of the PAL ones arrive.
            if (step == 0) {
                // (after v127p) THE PAL SNES 1080p x4 16:9 256 px («píxel cuadrado 8:7»), dump of 2026-11-25 15:23: ADC 1784,
                // HScale 1023 bypass (4 columns, square cells), HB 1092/290, DIS HB 1088/407, HBIN 140, DIS VB 1062/100,
                // IF VB 68/70, IF line 898/963, ST2/SP2 844/104. Memory 144 MHz phase 15 (memDefaultFor). The clock is written
                // here because the CRT step leaves it at 1900.
                GBS::PLLAD_MD::write(1784);
                latchPLLAD();
                GBS::VDS_HSCALE::write(1023);
                GBS::VDS_HSCALE_BYPS::write(1);
                GBS::VDS_HB_ST::write(1092);
                GBS::VDS_HB_SP::write(290);
                GBS::VDS_DIS_HB_ST::write(1088);
                GBS::VDS_DIS_HB_SP::write(407);
                // (after v127q) the border and the position of the 15:23 dump were not good; the corrected dump of 15:55:
                // HBIN 140 -> 136, DIS VB 1062/100 -> 1062/84, memory window VB 94/96 -> 80/82.
                GBS::IF_HBIN_SP::write(136);
                GBS::VDS_DIS_VB_ST::write(1062);
                GBS::VDS_DIS_VB_SP::write(84);
                GBS::VDS_VB_ST::write(83); // (after v127r) 2026-11-25: 3 touches down (80/82 + 3)
                GBS::VDS_VB_SP::write(85);
                GBS::IF_VB_ST::write(68);
                GBS::IF_VB_SP::write(70);
                GBS::IF_HSYNC_RST::write(898);
                GBS::IF_LINE_SP::write(963);
                GBS::IF_HB_ST2::write(844);
                GBS::IF_HB_SP2::write(104);
                label = "16:9 | whole picture | 256 px, the NTSC one carried over";
            } else if (step == 1) {
                // (after v127p) THE PAL SNES 1080p x4 16:9 CRT, dump of 2026-11-25 15:05 (1280 x 960, 5 columns): ADC 1900,
                // HScale 872, HB 1436/230, DIS HB 1170/325, HBIN 214, DIS VB 1062/100, IF VB 68/70, IF line 956/1021,
                // ST2/SP2 844/104. Memory 129 MHz phase 15.
                GBS::PLLAD_MD::write(1900);
                latchPLLAD();
                GBS::VDS_HSCALE::write(872);
                GBS::VDS_HSCALE_BYPS::write(0);
                GBS::VDS_HB_ST::write(1436);
                GBS::VDS_HB_SP::write(230);
                // (after v127q) the corrected dump of 15:56: DIS HB 1170/325 -> 1174/321, DIS VB 1062/100 -> 1062/84, memory window
                // VB 94/96 -> 80/82 (HB 1436/230, HBIN 214, HScale 872, IF as before). Memory 144 MHz phase 15 now (was 129).
                GBS::VDS_DIS_HB_ST::write(1174);
                GBS::VDS_DIS_HB_SP::write(321);
                GBS::IF_HBIN_SP::write(214);
                GBS::VDS_DIS_VB_ST::write(1062);
                GBS::VDS_DIS_VB_SP::write(84);
                GBS::VDS_VB_ST::write(83); // (after v127r) 2026-11-25: 3 touches down (80/82 + 3)
                GBS::VDS_VB_SP::write(85);
                GBS::IF_VB_ST::write(68);
                GBS::IF_VB_SP::write(70);
                GBS::IF_HSYNC_RST::write(956);
                GBS::IF_LINE_SP::write(1021);
                GBS::IF_HB_ST2::write(844);
                GBS::IF_HB_SP2::write(104);
                label = "16:9 | whole picture | CRT, the NTSC one carried over";
            }
        }
        // v92: the memory settings belong to the STEP, so they have to be written on every step change, not only after a
        // preset load: switching steps does not reload the preset, so the CRT step used to arrive with the square step's
        // clock still in the chip. v93 moved that write to the TOP of this block, before the windows, for the reason given
        // there. Nothing may touch the memory from here on.
        SerialM.print(F("[3x4x-v130k] 1080p x"));
        SerialM.print(x5 ? 5 : 4);
        SerialM.print(F(" PAL | step "));
        SerialM.print(step + 1);
        SerialM.print(F("/4 | "));
        SerialM.println(label);
        scaleHorizontalFine = prevFine;
        return; // v96: and do NOT fall through into the estimate block below
    }

    if (rto->presetID == 0x15) {
        // PAL x4, first round (estimate from the stock PAL grid captures: picture 1280 px wide must start at x=320;
        // the stock preset starts it at x=198 with 4.58 columns per pixel; one press = 8 samples ~ 5.8 px)
        // v24: measured 320 px picture 6 px right of centre with 25 presses -> 24 (256 px: 23 -> 22, same correction)
        if (!narrow)
            pressMove(true, nativeWidth == 320 ? 24 : 22, false);
        {
            // v26/v27, measured on hardware (PAL tests A / B): with the lowered ADC clock (PLLAD_MD 2230, real line
            // MD/2 = 1115 units) the picture only holds together when the IF line is MD/2 + 6 long (test B, stable;
            // with +65 it stayed broken). IF_HB_ST2 must lie below the end of the line (the preset's 1124 does not).
            // ST2 = end of the valid data: 1068 (MD/2 - 47) keeps the picture's whole right border (measured: data ends at
            // x=1657; 1018 cut 11 px of the picture and doubled the garbage). The garbage between the data end and the
            // IF line end (x=1742, the fixed right edge of the output window) is hidden by narrowing the VDS output
            // window instead (v28, estimated at ~1.55 px per unit: 52 units = ~80 px -> window ends at x~1662).
            applyIfRecipe();
            if (x5 && nativeWidth == 320) {
                // x5 for 320 px sources: HScale 818 and its own window (the x4 window below is for 1280 px pictures)
                placePalX5Window();
            } else if (narrow) {
                // v42 round 1 (ESTIMATE, not measured): 256 px at 4 columns per pixel, picture 1024 px at x=448..1472.
                // Positions from the PAL x4 model x = 1.535 * u - 158 (anchor) / 1.535 * u - 185 (window).
                INT1080_SET(GBS::IF_HBIN_SP, hbinSp, 40);
                INT1080_SET(GBS::VDS_HB_SP, hbSp, 252);
                INT1080_SET(GBS::VDS_DIS_HB_SP, disHbSp, 381);
                INT1080_SET(GBS::VDS_DIS_HB_ST, disHbSt, 1111);
            } else {
                // v34: window exactly on the game (measured on Sonic 2 with the v32 window x=285..1607: game x=320..1600,
                // y=90..986; Mega Drive games paint the console border with their backdrop colour, e.g. Sonic's blue):
                // left edge +35 px (+23 units), right edge -7 px (-5 units) from the v32 values, ~1.535 px per unit.
                // Window x = 320..1600 (1280 px = 320 px x 4, or 256 px x 5).
                const int16_t cut = 93;
                GBS::VDS_DIS_HB_ST::write((uint16_t)((int16_t)GBS::VDS_DIS_HB_ST::read() - cut));
                tr.disHbSt -= cut;
                const int16_t padL = 79;
                GBS::VDS_DIS_HB_SP::write((uint16_t)((int16_t)GBS::VDS_DIS_HB_SP::read() + padL));
                tr.disHbSp += padL;
                if (x5) {
                    // v42: x5 + 256 px: same picture as x4 + 256 px (5 columns per pixel), HB_ST = DIS_HB_ST for the line filter
                    INT1080_SET(GBS::VDS_HB_ST, hbSt, GBS::VDS_DIS_HB_ST::read());
                }
            }
        }
        // (after v130c) THE PC ENGINE 320 / 352 px BUTTONS IN PAL, 1080p x4 (16:9, 4:3) and x5 16:9. The NTSC entries of the two buttons
        // (measured 2026-11-23, further down) carried over with the factors of the PAL 1080p path: HScale as it is, windows x 0.8975,
        // IF HBIN x 0.896, vertical as it is, IF VB + 52. ESTIMATES until the PAL dumps. This block ignored the two buttons before.
        if (pceProfile() && (rto->integer1080Centered == 3 || rto->integer1080Centered == 4)) {
            const bool m352 = (rto->integer1080Centered == 4);
            const uint16_t hSt = x5 ? (m352 ? 1516 : 1448) : (m352 ? 1343 : 1335);
            const uint16_t hSp = x5 ? (m352 ? 71 : 103) : 228;
            const uint16_t dSp = x5 ? (m352 ? 176 : 244) : (m352 ? 347 : 355);
            const uint16_t hbin = x5 ? (m352 ? 192 : 240) : 216;
            auto pal = [](uint16_t v) { return (uint16_t)(((uint32_t)v * 8975 + 5000) / 10000); };
            GBS::VDS_HSCALE_BYPS::write(0);
            GBS::VDS_HSCALE::write(x5 ? 767 : 959);
            GBS::VDS_HB_ST::write(pal(hSt));
            GBS::VDS_HB_SP::write(pal(hSp));
            GBS::VDS_DIS_HB_ST::write(pal(hSt));
            GBS::VDS_DIS_HB_SP::write(pal(dSp));
            GBS::VDS_DIS_VB_ST::write(x5 ? 1120 : 1064);
            GBS::VDS_DIS_VB_SP::write(x5 ? 40 : 103);
            GBS::IF_VB_ST::write((uint16_t)((x5 ? 52 : 27) + 52));
            GBS::IF_VB_SP::write((uint16_t)((x5 ? 54 : 29) + 52));
            GBS::IF_HBIN_SP::write((uint16_t)(((uint32_t)hbin * 896 + 500) / 1000));
            SerialM.print(F("[3x4x-v130k] 1080p x"));
            SerialM.print(x5 ? 5 : 4);
            SerialM.println(m352 ? F(" PAL | PC Engine 352 px | the NTSC placement carried over (ESTIMATE)")
                                 : F(" PAL | PC Engine 320 px | the NTSC placement carried over (ESTIMATE)"));
        }
        // (after v130h) THE PC ENGINE 256 px BUTTON IN PAL, 1080p x4 16:9, ADC clock 1991 (the NTSC 80 % clock itself: the sampling
        // frequency is then 31.1 MHz, as in NTSC, where the wifi holds; 1784 = 27.9 MHz took it down). The PAL output line is 0.8975 of
        // the NTSC one and the samples per line are the same as in NTSC, so: input window as in NTSC, scale x 0.8975 (1023 -> 918, no
        // longer the bypass value), output windows x 0.8975, vertical as in NTSC, IF VB + 52. ESTIMATES until the PAL dumps.
        if (pceProfile() && narrow && !x5 && !tv43Active() && rto->integer1080Centered != 3 && rto->integer1080Centered != 4) {
            auto pal = [](uint16_t v) { return (uint16_t)(((uint32_t)v * 8975 + 5000) / 10000); };
            GBS::VDS_HSCALE_BYPS::write(0);
            GBS::VDS_HSCALE::write(918);
            GBS::VDS_VSCALE::write(512);
            GBS::VDS_VSCALE_BYPS::write(0);
            INT1080_SET(GBS::VDS_HB_ST, hbSt, pal(1290));
            INT1080_SET(GBS::VDS_HB_SP, hbSp, pal(310));
            INT1080_SET(GBS::VDS_DIS_HB_ST, disHbSt, pal(1224));
            INT1080_SET(GBS::VDS_DIS_HB_SP, disHbSp, pal(462));
            INT1080_SET(GBS::IF_HBIN_SP, hbinSp, 71);
            GBS::VDS_DIS_VB_ST::write(1068);
            GBS::VDS_DIS_VB_SP::write(102);
            GBS::IF_VB_ST::write(79);
            GBS::IF_VB_SP::write(81);
            SerialM.println(F("[3x4x-v130k] 1080p x4 PAL | PC Engine | 256 px at ADC clock 1991: the NTSC placement carried over (ESTIMATE)"));
        }
        scaleHorizontalFine = prevFine;
        return;
    }

    if (narrow) {
        // v45/v46: ADC clock at 80%, IF line recipe, picture 1024 px centred at 960. v42 (HBIN_SP 79, whole picture inside the valid
        // data) measured: picture 69 px left of centre on a Mega Drive (x=379), ~58 px on a SNES (starts ~10 px later). v43 / v44
        // moved it with HBIN_SP (29 / 36): that pushed the end of the picture past the end of the valid line data (IF HB ST2,
        // garbage from x=1427) and ate ~15 px of the right side. Now HBIN_SP stays at 79 and the picture is moved on the output
        // side with the read anchor VDS_HB_SP (1.366 px per unit): +50 units (68 px) Mega Drive, +42 (57 px) SNES.
        applyIfRecipe();
        // v57: SNES = the corrected state (HBIN 71, HB_SP 310 absolute); Mega Drive unchanged
        if (snesProfile()) {
            INT1080_SET(GBS::IF_HBIN_SP, hbinSp, 71);
            INT1080_SET(GBS::VDS_HB_SP, hbSp, 310);
        } else {
            INT1080_SET(GBS::IF_HBIN_SP, hbinSp, 79);
            INT1080_SET(GBS::VDS_HB_SP, hbSp, 312); // v61: the refined timing (absolute)
        }
    } else {
        pressMove(true, 3, false); // measured round 1: the picture sat left of centre
        if (nativeWidth == 320 || wide) {
            if (x5 || wide) {
                placeX5Window();
                pressMove(true, 1, false); // v22 round: 1 more normal press right
                pressMove(true, 2, true);  // measured: 2 fine presses to centre
            } else {
                pressMove(true, 4, false); // measured: centred
            }
        } else {
            // 256 px (x4 before v42, and x5 since v42: both 5 columns per pixel, 1280 px wide picture)
            pressMove(true, 1, false);
            pressMove(true, 1, true); // v22 round: +2 fine presses right of the previous 1 fine left
            if (x5 && snesProfile())
                pressMove(false, 1, false); // v43: measured on a SNES: ~10 px right of the Mega Drive placement -> 1 press left
        }
    }
    // v37: exact NTSC game window (measured on Sonic 2): the console border is painted with the backdrop colour.
    // x4: window 380..1312, x5: 262..1428 (HB_ST follows the right edge: display edge must be >= memory blank start)
    {
        const bool x5w320 = (x5 && nativeWidth == 320) || wide;
        // v44: narrow window back to the v42 values (441 / 1260 = x 405..1530, wider than the 448..1472 picture): the v43 cut
        // (472 / 1218 with HB_ST 1218) made the picture shake on a SNES, the v42 one was static.
        const uint16_t wSp = narrow ? (snesProfile() ? 468 : 474) /* (after v127g) the SNES 256 dump of 25-11 10:33: back to 468 (the 472 of 10:15 is gone) */ : (wide ? 160 : (x5w320 ? 262 : 380));
        const uint16_t wSt = narrow ? (snesProfile() ? 1230 : 1218) /* (after v127g) the dump of 10:33: back to 1230 */ : (wide ? 1531 : (x5w320 ? 1428 : ((!x5 && nativeWidth == 320 && !snesProfile()) ? 1315 : 1312))); // v62: Mega Drive x4 320 px: right edge +3 fine steps in all (v59 +1, v62 +2 to cover the blue frame)
        const int16_t dSp = (int16_t)wSp - (int16_t)GBS::VDS_DIS_HB_SP::read();
        const int16_t dSt = (int16_t)wSt - (int16_t)GBS::VDS_DIS_HB_ST::read();
        GBS::VDS_DIS_HB_SP::write(wSp);
        GBS::VDS_DIS_HB_ST::write(wSt);
        tr.disHbSp += dSp;
        tr.disHbSt += dSt;
        if (wide) {
            // v58 ESTIMATE: 1920 px = 1.2 x the x5 320 px picture (HScale 818 -> 682); the picture starts 160 px further left (~ -117 units)
            GBS::VDS_HSCALE::write(682);
            INT1080_SET(GBS::VDS_HB_SP, hbSp, (int16_t)GBS::VDS_HB_SP::read() - 117);
        }
        if (x5 || wide) {
            const int16_t dHb = (int16_t)wSt - (int16_t)GBS::VDS_HB_ST::read();
            GBS::VDS_HB_ST::write(wSt);
            tr.hbSt += dHb;
        } else if (narrow) {
            // v46: HB_ST is NOT cut to the window any more. Measured on a SNES (v45): with HB_ST = DIS_HB_ST = 1260 the whole
            // picture shimmered; every right-edge press removed (+4 on both) improved it, three presses (1272) made it static.
            // HB_ST goes to 1290 (margin) and the garbage right of the picture is hidden with DIS_HB_ST only (1230 = x~1478),
            // as in PAL x4 (v29). Window 468..1230 = x 437..1478 (picture 444..1465).
            INT1080_SET(GBS::VDS_HB_ST, hbSt, 1290);
        }
    }
    // v79: PAL, measured on the PAL SNES at 50 Hz with the 240p suite 256 x 224 grid. The PAL 1080p preset has
    // its own line length (HT 1437) and ADC clock (PLLAD_MD 1784), so none of the NTSC placements apply. The console draws
    // 224 lines: 224 x 4 = 896 fits in 1080 whole, and 224 x 5 = 1120 crops by 40.
    //   x4 narrow -> 4.008 col/px, 1026 px, pixel 4 x 4 SQUARE and whole
    //   x4 wide   -> 5.00 col/px (same HScale, the wider input capture does it), 1280 px
    //   x5 narrow -> 4.988 col/px, 1277 px, pixel 5 x 5 SQUARE and whole
    //   x5 middle -> 6.24 col/px, 1597 px, pixel 1.25 = the PAL CRT shape (columns not whole)
    //   x5 wide   -> 7.002 col/px, 1794 px, measured at HScale 732
    // Also measured but with no control left to reach it: x5 at HScale 854 = 6.002 col/px, 1537 px. It goes in when the
    // four-step cycle lands on the fill button.
    // The vertical is the same on every step of each mode, so the picture does not jump when the width changes.
    // v76: PC Engine, the four states measured on the 240p suite grid (2026-11-10). The 1080p path above places the
    // picture with relative presses, which cannot express the hand-centred windows, so the states are written absolute here,
    // last. Measured: x4 256 px = 4.003 columns, x4 320 px = 5.002 columns (1281 px, centre 960.0, pixel 5 x 4 = the CRT
    // proportion), x5 256 px = 5.003 columns, x5 320 px = 6.005 columns (1537 px, centre 960.0).
    // v77: SNES, 1080p x4. The 1080p path does not set the vertical window at all (it comes from the preset), so the border
    // positioned by hand was lost on every reload. The two x4 states are written here; x5 is still the stock placement until
    // it is measured.
    if (snesProfile() && !pceProfile() && x5 && rto->presetID != 0x15) {
        // v77: the two x5 states. HB_ST and HB(d)_ST are equal in both, the relation that has been keeping these modes clean,
        // and the vertical window is the same for the two widths, so the picture does not jump when switching.
        // (after v127g) in the x5 «narrow» is never true (it needs !x5), so both buttons used the else-block. The 256 px
        // button now has its own state, THE (dumps of 2026-11-25 11:07 and 11:10): HScale 818 = 5.0 columns x 5.0 lines,
        // the garbage at both sides covered: HB 1324/276, DIS HB 1324/373, IF HBIN 246. The CRT keeps the else-block
        // until it is measured.
        if (nativeWidth == 256) {
            // (after v127g) 2026-11-25 12:3x: the 256 px had the CRT's scale. 5.0 columns x 5 lines = whole
            // square pixels = HScale 1023 with the scaler bypassed (1023 is the bypass value; k = 5115, as the PC Engine's
            // x5 256 px). The windows are the 11:10 ones, measured on that 5.0 picture.
            GBS::VDS_HSCALE_BYPS::write(1);
            GBS::VDS_HSCALE::write(1023);
            // (after v127g) THE final 256 px, capture and dump of 2026-11-25 12:31: cells 80 x 80, picture ~1275 px
            // centred on 960: HB 1328/272, DIS HB 1328/373, IF HBIN 242 (1324/276, 1324/373, 246 before).
            GBS::VDS_HB_ST::write(1328);
            GBS::VDS_HB_SP::write(272);
            GBS::VDS_DIS_HB_ST::write(1328);
            GBS::VDS_DIS_HB_SP::write(373);
            GBS::IF_HBIN_SP::write(242);
        } else {
            // (after v127g) THE SNES 1080p x5 CRT, the capture and dump of 2026-11-25 12:24: HScale 818 with the scaler on =
            // 6.25 columns x 5 lines, cells of 100 x 80 px in the signal (16 game px), the TV shape 1.25, picture ~1592 px
            // centred on 961. It is the firmware's own state (v77): HB 1436/144, DIS HB 1436/265, HBIN 246; the 654 /
            // 1563 / 244 of 11:15 are gone. Vertical 1125/40 as the 256 px.
            GBS::VDS_HSCALE_BYPS::write(0);
            GBS::VDS_HSCALE::write(818);
            GBS::VDS_HB_ST::write(1436);
            GBS::VDS_HB_SP::write(144);
            GBS::VDS_DIS_HB_ST::write(1436);
            GBS::VDS_DIS_HB_SP::write(265);
            GBS::IF_HBIN_SP::write(246);
        }
        GBS::VDS_DIS_VB_ST::write(1125);
        GBS::VDS_DIS_VB_SP::write(40);
        GBS::IF_VB_ST::write(39);
        GBS::IF_VB_SP::write(41);
        SerialM.print(F("[3x4x-v130k] 1080p x5 SNES: the border ("));
        SerialM.print(nativeWidth == 256 ? F("256 px button") : F("320 px button"));
        SerialM.println(')');
    }
    if (snesProfile() && !pceProfile() && !x5 && rto->presetID != 0x15) {
        if (narrow) {
            // (after v127g) the SNES 1080p x4 256 px, last dump of 2026-11-25 10:33 (replaces the 10:15 one): the grid is
            // 256 x 239 and the vertical border is closed to the 224 lines (8:7 picture): DIS VB 1061/102, IF VB 15/17
            // (1039/135 and 12/14 before). The CRT button keeps its own.
            GBS::VDS_DIS_VB_ST::write(1061);
            GBS::VDS_DIS_VB_SP::write(102);
            GBS::IF_VB_ST::write(15);
            GBS::IF_VB_SP::write(17);
        } else {
            // (after v127g) THE SNES 1080p x4 CRT, the last dump of 2026-11-25 10:31 (the 10:29 one was replaced by it):
            // HB 1322/268, DIS HB 1322/378 (1318 and 1318/382 before), the vertical opened to the whole 256 x 239 grid,
            // DIS VB 1059/103 and the input window IF VB 16/18 (12/14 before), IF HBIN 236 (230 before). The 256 px
            // button keeps its 224 lines. Picture 1275 px wide, cells 80 x 64 (5 x 4, the TV's shape).
            GBS::VDS_HB_ST::write(1322);
            GBS::VDS_HB_SP::write(268);
            GBS::VDS_DIS_HB_ST::write(1322);
            GBS::VDS_DIS_HB_SP::write(378);
            GBS::VDS_DIS_VB_ST::write(1059);
            GBS::VDS_DIS_VB_SP::write(103);
            GBS::IF_HBIN_SP::write(236);
            GBS::IF_VB_ST::write(16);
            GBS::IF_VB_SP::write(18);
        }
        SerialM.print(F("[3x4x-v130k] 1080p x4 SNES: the border ("));
        SerialM.print(narrow ? F("256 px button") : F("320 px button"));
        SerialM.println(')');
    }
    if (pceProfile() && rto->presetID != 0x15) {
        // (after v123) 2026-11-22: in 1080p x5 the 256 px button loaded the 320 px placement (the log said
        // "the measured placement (320 px button)" right under "loaded for 256 px sources"). "narrow" only tracks the
        // width in the x4, where the 256 px button lowers the ADC clock; the x5 keeps one clock for both widths, so there
        // the width has to come from the width itself.
        const bool w320 = x5 ? (nativeWidth == 320) : !narrow;
        // (after v123) the captures of 2026-11-22 01:39: loaded, the x5 drew 5.04 columns per pixel instead of 6.25, and
        // one step of the scale (818 -> 819) put it right. The scale written here was being ignored: this block never
        // cleared VDS_HSCALE_BYPS, so arriving from a mode at 1023 (which IS the bypass value) left the bypass on and the
        // picture came out at the preset's own scale inside our window: cut, with garbage on the right. The 720p modes
        // clear it in write720(); the x4 never showed it because its scale is 1023 anyway. Same rule as elsewhere here.
        GBS::VDS_HSCALE_BYPS::write(0); // set again below for whichever entry writes 1023
        if (x5) {
            if (w320) {
                // (after v121) the dump of 2026-11-21 11:07 placed this button (HB 1406/174, DIS 1406/286, HBIN 252
                // before). the capture of 12:08 then measured it at 6.000 columns and 5.000 lines, whole pixels on both
                // axes, and the decision followed: "queremos proporcion de tele, no pixel perfecto. El pixel perfecto
                // solo es para el modo 256". The shape comes from its own 1080p x4 CRT, 5 columns over 4 lines = 1.25,
                // so over 5 lines it is 6.25 columns: HScale = 853 * 6.000 / 6.25 = 819, cells of 75.0 px. The window
                // keeps the centre (845.5) and the crop, widened by the same factor, and the 147-unit HB_SP gap.
                // the dump of 2026-11-21 11:12 with that shape on screen, entered as left: 818 instead of the
                // calculated 819 (6.257 columns, cells of 75.1 px) and the window one unit further right.
                GBS::VDS_HSCALE::write(818);
                // (after v122) the dumps of 2026-11-22 01:22 / 01:23: loaded, the mode came up cut on the right with
                // garbage showing, and one touch of the scale (817 and back to 818) fixed it. The only two registers that
                // moved are these: the window end was seven units short. 1393 / 1393 before, which is what had been measured
                // while the equal-ends rule still held.
                // (after v123e) the dump of 2026-11-22 09:35: borders opened. 1395/148, DIS 1400/299 before.
                // (after v123f) the dump of 2026-11-22 09:50: 1406/148, DIS 1406/280, HBIN 244 before.
                GBS::VDS_HB_ST::write(1454);
                GBS::VDS_HB_SP::write(148);
                GBS::VDS_DIS_HB_ST::write(1454);
                GBS::VDS_DIS_HB_SP::write(236); // (after v126c) the 22:22 dump with the new recipe, 248 before
                GBS::VDS_DIS_VB_ST::write(1120);
                GBS::VDS_DIS_VB_SP::write(40);
                // (after v121) the dump of 2026-11-21 11:16: three fine steps up, which move the input window. 49/51 before.
                GBS::IF_VB_ST::write(52);
                GBS::IF_VB_SP::write(54);
                GBS::IF_HBIN_SP::write(254);
            } else {
                // (after v122) the dump of 2026-11-22 01:26, entered exactly as left on screen. HScale 853 is the
                // pixel perfect of this mode, 6.000 columns over 5.000 lines, its own choice; what was wrong was the
                // placement (HB 1283/278, DIS 1283/409, HBIN 242 before), cut on the right like the CRT button was.
                // (after v123b) the captures of 2026-11-22 03:05 settle what "256 pixel perfect" means here: SQUARE
                // pixels, as many columns as lines. His 1080p x4 gives 4.000 columns over 4 lines; the x5 has 5 lines, so
                // it is 5 columns, which is HScale 1023 (and the bypass, 1023 being that value). 853 = 6.000 columns,
                // which I accepted on 01:47 for being a whole number without checking the shape was square: my mistake,
                // and the reason the two buttons of this mode were only 4 % apart instead of 25 %.
                GBS::VDS_HSCALE::write(1023);
                GBS::VDS_HSCALE_BYPS::write(1);
                // the dump of 2026-11-22 02:13, measured on the capture: 60.00 px per 12 px cell = 5.000 columns
                // exactly, square pixels, picture 1208 px centred on 960.5. My calculated window was 1283/257 and
                // 1283/408. His vertical moves of that session are left out on purpose: the PC Engine's sync had drifted
                // the picture up and it was pushed further; reloading the sync put it back. The height stays the CRT's.
                // (after v123f) the dump of 2026-11-22 09:51, new framing: 1292/262, DIS 1292/402, HBIN 214 before.
                GBS::VDS_HB_ST::write(1340);
                GBS::VDS_HB_SP::write(262);
                GBS::VDS_DIS_HB_ST::write(1340);
                GBS::VDS_DIS_HB_SP::write(354);
                // (after v121) the same height as the CRT button. Window 1125 -> 1120 and the input window
                // 49/51 -> 52/54, the three fine steps up.
                GBS::VDS_DIS_VB_ST::write(1120);
                GBS::VDS_DIS_VB_SP::write(40);
                GBS::IF_VB_ST::write(52);
                GBS::IF_VB_SP::write(54);
                GBS::IF_HBIN_SP::write(220); // (after v123f) the 09:51 framing; 214 before, 260 before v123b
            }
        } else {
            GBS::VDS_HSCALE::write(1023);
            GBS::VDS_HSCALE_BYPS::write(1); // 1023 is the bypass value
            if (w320) {
                // (after v120) the dump of 2026-11-21: the retouched edges become the value here (1298/264, 1312/380,
                // HBIN 222 before). Measured on that capture: 5.000 columns exactly, window 1201 px centred on 959.5.
                // (after v123f) the dump of 2026-11-22 10:01: HB 1283/264, DIS 1283/409, DIS_VB_SP 109, HBIN 212 before.
                // (after v126c) the dump of 2026-11-23 22:05 with the new sync recipe (it places the picture differently):
                // HB 1335/264, DIS 1335/355, DIS_VB_SP 107 before.
                GBS::VDS_HB_ST::write(1303);
                GBS::VDS_HB_SP::write(264);
                GBS::VDS_DIS_HB_ST::write(1303);
                GBS::VDS_DIS_HB_SP::write(375);
                GBS::VDS_DIS_VB_ST::write(1066);
                GBS::VDS_DIS_VB_SP::write(103);
                GBS::IF_HBIN_SP::write(222);
            } else {
                // (after v120) the dump of 2026-11-21: the retouched edges become the value here (DIS_HB 1221/475 and
                // DIS_VB_SP 105 before). Window 698 units = 959 px, centred on 844.
                GBS::VDS_HB_ST::write(1290);
                GBS::VDS_HB_SP::write(310);
                // (after v123f) the dump of 2026-11-22 09:57: it was badly cut; DIS 1193/495 before.
                // (after v126c) the dump of 2026-11-23 22:08 with the new recipe: DIS 1232/462, DIS_VB 1066/110 before
                GBS::VDS_DIS_HB_ST::write(1224);
                GBS::VDS_DIS_HB_SP::write(462);
                GBS::VDS_DIS_VB_ST::write(1068);
                GBS::VDS_DIS_VB_SP::write(102);
                GBS::IF_HBIN_SP::write(71);
            }
            GBS::IF_VB_ST::write(27);
            GBS::IF_VB_SP::write(29);
        }
        // (after v124e) the new 320 / 352 px buttons (the PC Engine's middle dot clock in whole pixels), measured on
        // 2026-11-23 with the suite grids: x4 HScale 959 = 64 px square cells, x5 HScale 767 = 80 px, all centred on 960
        if (rto->integer1080Centered == 3 || rto->integer1080Centered == 4) {
            const bool m352 = (rto->integer1080Centered == 4);
            GBS::VDS_HSCALE_BYPS::write(0);
            if (x5) {
                GBS::VDS_HSCALE::write(767);
                GBS::VDS_HB_ST::write(m352 ? 1516 : 1448);
                GBS::VDS_HB_SP::write(m352 ? 71 : 103);
                GBS::VDS_DIS_HB_ST::write(m352 ? 1516 : 1448);
                GBS::VDS_DIS_HB_SP::write(m352 ? 176 : 244);
                GBS::VDS_DIS_VB_ST::write(1120);
                GBS::VDS_DIS_VB_SP::write(40);
                GBS::IF_VB_ST::write(52);
                GBS::IF_VB_SP::write(54);
                GBS::IF_HBIN_SP::write(m352 ? 192 : 240);
            } else {
                GBS::VDS_HSCALE::write(959);
                GBS::VDS_HB_ST::write(m352 ? 1343 : 1335); // (after v126c) 352: the 22:06 dump, 1375 before
                GBS::VDS_HB_SP::write(228);
                GBS::VDS_DIS_HB_ST::write(m352 ? 1343 : 1335);
                GBS::VDS_DIS_HB_SP::write(m352 ? 347 : 355); // (after v126c) 352: 315 before
                GBS::VDS_DIS_VB_ST::write(1064);
                GBS::VDS_DIS_VB_SP::write(103);
                GBS::IF_VB_ST::write(27);
                GBS::IF_VB_SP::write(29);
                GBS::IF_HBIN_SP::write(216);
            }
        }
        SerialM.print(F("[3x4x-v130k] 1080p x"));
        SerialM.print(x5 ? 5 : 4);
        SerialM.print(F(" PC Engine: the measured placement ("));
        SerialM.print(rto->integer1080Centered == 3 ? F("320 px whole pixels") : rto->integer1080Centered == 4 ? F("352 px whole pixels") : w320 ? F("CRT button") : F("256 px button"));
        SerialM.println(')');
    }
    scaleHorizontalFine = prevFine;
}

// v47: 720p presets made by hand (stock 720p preset, PLLAD_MD 2345, then HScale / VScale / windows / IF offsets).
// Columns per source pixel = (2345 / 624 for 320 px, 2345 / 499.2 for 256 px) * 512 / HScale; lines per source line = 2048 / VScale.
struct Mode720 {
    uint16_t hscale, vscale;
    uint16_t hbSt, hbSp;     // memory blanking (HB)
    uint16_t dHbSt, dHbSp;   // display window (HB(d))
    uint16_t dVbSt, dVbSp;   // display window (VB(d))
    uint16_t ifVbSt, ifVbSp; // IF vertical blanking (crops the lines in x5)
    uint16_t hbinSp;         // IF horizontal start
};

static void write720(const Mode720 &m)
{
    GBS::VDS_HSCALE::write(m.hscale);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(m.vscale);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HS_ST::write(16);
    GBS::VDS_HS_SP::write(144);
    GBS::VDS_HB_ST::write(m.hbSt);
    GBS::VDS_HB_SP::write(m.hbSp);
    GBS::VDS_DIS_HB_ST::write(m.dHbSt);
    GBS::VDS_DIS_HB_SP::write(m.dHbSp);
    GBS::VDS_VS_ST::write(1);
    GBS::VDS_VS_SP::write(6);
    GBS::VDS_DIS_VB_ST::write(m.dVbSt);
    GBS::VDS_DIS_VB_SP::write(m.dVbSp);
    GBS::VDS_VB_ST::write(14);
    GBS::VDS_VB_SP::write(16);
    GBS::IF_VB_ST::write(m.ifVbSt);
    GBS::IF_VB_SP::write(m.ifVbSp);
    GBS::IF_HBIN_ST::write(0);
    GBS::IF_HBIN_SP::write(m.hbinSp);
}

// 240p in 720p x3, both shapes measured perfect at the first try (23:42 and 23:47)
static bool dc720Measured()
{
    if (dcSignal() == 2 && uopt->integer720 == 1) {
        // 288p, 720p x3 4:3, the dump of 01:15 (PAL 720p preset): HScale 530 = 3.996 columns (cells of 95.9 in the 4:3
        // capture = 63.9 output px), the 320 px frame across the whole 1280; 3 lines exact, 240 of the 264 lines shown.
        // The memory window wraps round the line (2120 / 2136), as left.
        write720(Mode720{530, 683, 2120, 2136, 2136, 344, 748, 26, 72, 74, 16});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 4:3 | Dreamcast 288p | 4 x 3, the whole width, 240 of the 264 lines | measured placement"));
        return true;
    }
    if (dcSignal() == 4 && uopt->integer720 == 1) {
        // 576i, 720p x3 4:3, the dump of 2026-11-17 03:10: the whole width for the set's 4:3 / 16:9 button, HScale 530 =
        // 1.999 columns (32.0 output px per 16), the 640 px frame across the whole 1280; 1.5 lines per line (24 per 16, not
        // whole), the 480 lines in the 720. His idea of the 720p x3 «4:3», for the interlaced signal.
        // IF VB 64 / 66: the correction of 03:11, the picture one input line up (62 / 64 in the measured capture)
        write720(Mode720{530, 683, 2117, 2133, 2131, 344, 745, 26, 64, 66, 22});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 4:3 | Dreamcast 576i | 2 columns x 1.5 lines, the whole width | measured placement"));
        return true;
    }
    if (dcSignal() == 3 && uopt->integer720 == 1) {
        // 480i, 720p x3 4:3, the dump of 2026-11-17 03:13 (grid of the "scaled 240p assets", 32 lines per cell): HScale 480
        // = 2.003 columns (64.1 output px per 32 px, within the reading error; the same 480 gave 4.000 exact in 240p),
        // the whole width; 1.5 lines per line, the 480 lines in the 720. The window and capture start are hand-made.
        write720(Mode720{480, 683, 2284, 132, 2320, 338, 745, 25, 12, 14, 138});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 4:3 | Dreamcast 480i | 2 columns x 1.5 lines, the whole width | measured placement"));
        return true;
    }
    if (dcSignal() == 3 && uopt->integer720 == 4) {
        // 480i, 720p 16:9, the dump of 2026-11-17 14:00: HScale 640 = 1.501 columns (36.03 per 16 px cell in the 1080 capture
        // of the 720p = 24.0 output px), frame 960 px centred; 1.5 lines, the same height as the 4:3
        write720(Mode720{640, 683, 2018, 402, 2018, 610, 745, 25, 12, 14, 122});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 16:9 | Dreamcast 480i | 1.5 columns x 1.5 lines | measured placement"));
        return true;
    }
    if (dcSignal() == 4 && uopt->integer720 == 4) {
        // 576i, 720p 16:9, the dump of 2026-11-17 14:09 (no capture): HScale 707 = 530 (2.000 columns, measured) x 4/3,
        // so 1.5 columns; the same height and capture start (IF VB 64 / 66) as the 4:3
        write720(Mode720{707, 683, 1813, 358, 1813, 546, 745, 26, 64, 66, 154});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 16:9 | Dreamcast 576i | 1.5 columns x 1.5 lines | measured placement"));
        return true;
    }
    if (dcSignal() == 2 && uopt->integer720 == 4) {
        // 288p, 720p x3 16:9, the dump of 01:25: HScale 706 = 3.000 columns (every cell 96 in the 2560 capture = 48
        // output px), frame 960 px centred at 639.3 of 1280; 3 lines exact, 240 of the 264 lines shown
        write720(Mode720{706, 683, 1808, 312, 1817, 552, 748, 26, 72, 74, 86});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 16:9 | Dreamcast 288p | 3 x 3, 240 of the 264 lines | measured placement"));
        return true;
    }
    if (dcSignal() != 1)
        return false;
    if (uopt->integer720 == 4) {
        write720(Mode720{640, 683, 2017, 410, 2032, 608, 745, 25, 12, 14, 128});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 16:9 | Dreamcast 240p | 3 x 3 | measured placement"));
        return true;
    }
    if (uopt->integer720 == 1) {
        write720(Mode720{480, 683, 2284, 132, 2320, 338, 745, 25, 12, 14, 134});
        SerialM.println(F("[3x4x-v130k] 720p x3 | 4:3 | Dreamcast 240p | 4 x 3, the whole width | measured placement"));
        return true;
    }
    return false;
}

static void integer720ApplyInner(uint16_t nativeWidth) // v107n: wrapped, see integer720Apply() at the end
{
    // v100: the memory first, the placement afterwards, the same rule the 1080p path follows. Until now applyMemSettings()
    // was reached from ONE place in the whole 720p path, inside "if (integer720 == 2 && (!snesProfile() || pceProfile()))":
    // x5 only, and non-SNES only. So 720p x4 with the SNES never got the 162 MHz its slot holds and came up on the preset's
    // 129, which had to be raised by hand every single time the picture was stretched.
    if (applyMemSettings()) {
        ResetSDRAM();
        scanlinesAfterSdramReset();
    }
    const bool x5 = (uopt->integer720 == 2);
    const bool snes = snesProfile();
    const bool x4 = (uopt->integer720 == 3);
    const bool w169 = (uopt->integer720 == 4); // v55: x3 for a 16:9 TV, whole pixels 3 x 3
    // v53: the SNES / NES only has 256 px games: its "320 px" is the same picture as the fill (HScale 481 = 5 columns per
    // pixel); it used the 320 px capture window and came out corrupted, so it now uses the fill values (and the x5 256 px ones)
    // v57: the fill is now ONE state (uopt->integer720Fill) set by the buttons: SNES "320 px" in the x3 / x4 (4:3) modes switches
    // it on (a 320 px game fills 1280 px there), "256 px" switches it off, the fill button toggles it. In the x3 16:9 mode the
    // SNES "320 px" is its own picture (960 px wide, 3.75 columns per pixel) and the fill is the full 1280 px.
    const bool fill = (uopt->integer720Fill != 0) && (uopt->integer720 == 1 || x4 || w169);
    // v78: PAL. Measured on a PAL SNES at 50 Hz with the 240p suite 256 x 224 grid. The PAL 720p preset has its
    // own line length (HT 2156, not 2402) and its own horizontal constant, 2689, so none of the NTSC entries apply. The
    // console draws 224 active lines, and 224 x 3 = 672 fits in 720, so VScale 683 (3 lines per line) works for every step.
    // The four steps and what each is for:
    //   896 -> 3.00 columns,  768 px, square pixels              (CONSTRUCTED, not measured: see below)
    //   768 -> 3.50 columns,  896 px, 1.17 = the PAL CRT shape   (measured, centred)
    //   672 -> 4.00 columns, 1024 px, 1.33, whole and wider      (measured, dead centre)
    //   538 -> 5.00 columns, 1278 px, fills the screen           (measured)
    // The 896 one is derived from the 672 state, scaling both windows by 768 / 1024 about their centres and keeping the
    // relations that every clean state of the shows: HB_ST three units under HB(d)_ST, and the memory window starting 211
    // units before the display one. 896 could not be reached by hand because every route through the controls passes through
    // states that corrupt the picture; written in one go as a mode it may hold. If it does not, drop this step.
    // This runs BEFORE the SNES width collapse below, so the two buttons and the fill state still mean what they say.
    if (rto->presetID == 0x13) {
        Mode720 pm;
        const __FlashStringHelper *what;
        // v79: the 16:9 preset needs its own placement, as in NTSC: the two steps below were centred in it and they came
        // out different from the 4:3 ones. The two still marked "4:3 values" have no 16:9 measurement yet and fall back.
        const bool pal169 = (uopt->integer720 == 4);
        // v90: the SD2SNES menu was put side by side at 60 Hz and at 50 Hz in the x3 16:9 mode. Measured on the two
        // captures: the picture is the SAME, 1152 x 976 against 1151 x 977 in a 1920-wide frame, and the horizontal position
        // matches to one pixel. The ONLY difference is that the 50 Hz one sits 7 capture pixels higher, which at 1.5x is about
        // 4 or 5 output lines, or a line and a half of input. So PAL needs no format of its own here: it needed its IF_VB
        // moved down. 64 / 66 -> 62 / 64, one press of the vertical button, which lands within 2 capture pixels. The 4:3
        // entries are left alone: this was measured in 16:9 and nowhere else.
        // v86: the four size steps of the cycle, in the order: square -> CRT -> wide -> fills the screen.
        // Always the four, never collapsed even where two come out close: the cycle has to land where you expect. The fill
        // button walks them and the 256 / 320 buttons are shortcuts to steps 0 and 1.
        switch (uopt->sizeStep) {
            case 1: // CRT shape, 3.50 col/px, 896 px, pixel 1.17
                pm = pal169 ? Mode720{768, 683, 1713, 404, 1779, 588, 725, 50, 62, 64, 110}
                            : Mode720{576, 683, 1949, 125, 1969, 392, 748, 26, 62, 64, 66};
                what = pal169 ? F("CRT shape, as a TV showed it")
                              : F("CRT shape, fills the 4:3 frame");
                break;
            case 2: // wide, 4.00 col/px, 1024 px, pixel 4 x 3
                // v90: the 16:9 one is measured now. Too much border on the left was corrected: the only
                // register that moved from the 4:3 entry is DIS_HB_SP, 519 -> 507. IF_VB follows the 62 / 64 of the other
                // 16:9 steps, not the 64 / 66 the dump carries, so the four steps keep the same height between them; the
                // capture pair showed 50 Hz sitting a line and a half above 60 Hz with the old value.
                pm = pal169 ? Mode720{672, 683, 1857, 308, 1860, 507, 748, 26, 62, 64, 114}
                            : Mode720{576, 683, 1949, 125, 1969, 392, 748, 26, 62, 64, 66};
                what = pal169 ? F("wider than a CRT, for a 16:9 screen")
                              : F("the CRT step already fills the 4:3 frame, so this loads it");
                break;
            case 3: // fills the screen, 5.00 col/px, 1278 px
                pm = pal169 ? Mode720{538, 683, 2144, 4, 2155, 344, 748, 26, 62, 64, 12}
                            : Mode720{576, 683, 1949, 125, 1969, 392, 748, 26, 62, 64, 66};
                what = pal169 ? F("fills the screen width")
                              : F("the CRT step already fills the 4:3 frame, so this loads it");
                break;
            default: // square pixels, 3.00 col/px, 768 px
                // v101: in 4:3 the whole-pixel base is FOUR columns, not three. This mode exists so the SNES shows whole pixels on a
                // 4:3 screen without touching the set's aspect button, and a 4:3 display squashes each output pixel to 0.75 of
                // its width, so three columns come out as a tall rectangle. NTSC has always done it this way: its 4:3 SNES entry
                // is HScale 601 = 4.0 columns. PAL is 2688 / 4 = 672, and that placement was measured. The 896 belongs to
                // 16:9, where the pixel really is 1:1, and stays there.
                pm = pal169 ? Mode720{896, 683, 1673, 477, 1685, 668, 748, 26, 62, 64, 58}
                            : Mode720{672, 683, 1843, 253, 1867, 508, 748, 26, 62, 64, 42};
                what = pal169 ? F("square pixels, sharpest, with side borders")
                              : F("square pixels on a 4:3 screen, sharpest");
                break;
        }
        // v107q: the Mega Drive in PAL 720p x3 4:3, CRT step (and the two steps that load it): the correction of
        // 2026-11-12 19:36, on a real PAL Mega Drive. Display window 1969/392 -> 1974/378 and capture start 66 -> 24: the
        // Mega Drive's picture starts at another point of the line than the SNES's, for which the 66 was measured. The
        // SNES keeps its values.
        if (!pal169 && uopt->integer720 == 1 && uopt->sizeStep >= 1 && activeConsoleProfile() == 1) {
            pm.dHbSt = 1974;
            pm.dHbSp = 378;
            pm.hbinSp = 24;
        }
        // v107q: the Mega Drive's 256 button in 720p x3 4:3, the dump of 2026-11-12 22:19 with the 256 px grid, measured in
        // real 4:3 this time (the 22:09 one was taken with the bar on 16:9): HScale 666 = 4.00 columns (every cell 96 px in
        // the 4:3 capture), grid 191..1727, centred at 959; on a 4:3 screen 4 x 0.75 = 3 columns against 3 lines, so whole
        // and square, the SNES's criterion for this button (its 672). Only the horizontal fields: the vertical ones stay
        // those of the 4:3 table (748/26, 62/64), which is what was measured with.
        if (!pal169 && uopt->integer720 == 1 && uopt->sizeStep == 0 && activeConsoleProfile() == 1) {
            pm.hscale = 666;
            pm.hbSt = 1861;
            pm.hbSp = 247;
            pm.dHbSt = 1863;
            pm.dHbSp = 513;
            pm.hbinSp = 30;
        }
        // v107q: the Mega Drive in PAL 720p x3 16:9, 320 button (CRT step), the dumps of 21:44 / 21:46 on a real PAL Mega
        // Drive: HScale 710 = 3.00 columns exactly (every cell of the 320 px grid 72 capture px = 48 output px), 960 px,
        // centred at 958.5 of 1920. Square and whole (3 x 3). The 707 worked out from the 4:3 capture gave 3.01. His
        // vertical window (748 / 26) shows the 240 lines; the 224 one here keeps its centre (the Líneas row opens it).
        if (pal169 && uopt->integer720 == 4 && uopt->sizeStep == 1 && activeConsoleProfile() == 1)
            // (after v127u) THE positioned 320 px, dump of 2026-11-25 21:13 (nine fine touches left, border adjusted): HScale 710 and
            // HB_SP 320 stay; HB_ST 1821 -> 1812, DIS HB 1821/556 -> 1812/543, HBIN 36 -> 54, DIS VB 724/50 -> 748/26, IF VB 62/64 as it was.
            pm = Mode720{710, 683, 1812, 320, 1812, 543, 748, 26, 62, 64, 54};
        // v107q: the 256 button, the dump of 22:01 with the 256 px grid: HScale 888 = 3.00 columns for a 256 px game
        // (every cell 72 capture px), 768 px, centred at 960.5. Whole and square; narrower than a 320 px game, as it
        // should be: whoever wants it wider picks the 320 button.
        if (pal169 && uopt->integer720 == 4 && uopt->sizeStep == 0 && activeConsoleProfile() == 1)
            // (after v127u) THE positioned 256 px, dump of 2026-11-25 21:15: HScale 888, HB 1685/522 and IF VB 62/64 stay; DIS HB
            // 1696/684 -> 1686/667, DIS VB 724/50 -> 745/26, HBIN 120 -> 144.
            pm = Mode720{888, 683, 1685, 522, 1686, 667, 745, 26, 62, 64, 144};
        // v107q: and its wide step, the dump of 21:56: HScale 609 = 3.5 columns (84 px cells), 1680 px, centred at 958.5.
        // It is wide enough: this console has no fill step in this mode (sizeStepsThree), so a step 4 left
        // over from another console loads this one too.
        if (pal169 && uopt->integer720 == 4 && uopt->sizeStep >= 2 && activeConsoleProfile() == 1)
            pm = Mode720{609, 683, 160, 255, 1925, 451, 724, 50, 62, 64, 120};
        // v80: the vertical is the MODE's, not always the x3's. Until now this branch wrote VScale 683 whatever button was
        // pressed, so x4 and x5 in PAL gave exactly the same picture as x3. The horizontal entries above
        // are right for all three; only the lines per line and the crop change.
        //   x3 -> 3 lines, 224 x 3 = 672 of 720, the whole picture fits
        //   x4 -> 4 lines, 224 x 4 = 896, so 180 of the 224 lines show
        //   x5 -> 5 lines, 224 x 5 = 1120, so 144 of the 224 lines show
        // v83 built the missing steps by scaling the x3 entry and stretching its windows (growWindow); v87 removed it. It
        // produced memory garbage every time there was no room left to grow, and in x5 it destroyed the picture outright.
        // v87: x4 and x5 no longer CONSTRUCT a step that has no measurement. Such a step loads the measured one for that
        // mode and the log says so. Never pretty, never broken.
        //
        // WHICH BUTTON MEASURED WHICH STEP. The size steps are 0 square, 1 CRT, 2 wide, 3 fills the screen, and the two width
        // buttons are shortcuts into them: 256 px -> step 0, 320 px -> step 1. So a state centred by pressing 320
        // belongs on STEP 1, not on the fill step. v87 first put the x4 320 state on step 3, where no button reaches it, and
        // the 320 button answered "not measured" while its own measurement sat two steps away. Both x4 entries below carry the
        // vertical (VScale 512, IF VB 126 / 128), which is how you can tell they are measured and not constructed.
        //   x4: step 0 = HScale 672 (256 button), step 1 = HScale 404 (320 button). Steps 2 and 3 not measured.
        //   x5: step 0 = HScale 538 (256 button), step 1 = HScale 461 (320 button). No steps 2 and 3 (v107f).
        const char *vwhat = pal169 ? "720p x3 | 16:9 | whole picture"
                                   : "720p x3 | 4:3 | whole picture";
        bool measured = true; // false: this step has no measurement of its own, so another one is loaded
        if (uopt->integer720 == 3) { // x4: 4 lines per line, 180 of the 224 show
            if (sizeStepsTwoMd() && uopt->sizeStep > 1)
                uopt->sizeStep = 1; // v107q: the Mega Drive has no wider step here (sizeStepsTwoMd)
            if (uopt->sizeStep == 0) { // the 256 button
                pm = {672, 512, 1891, 275, 1860, 509, 748, 26, 126, 128, 76};
                // v107q: the Mega Drive's 256 button in PAL 720p x4, dump of 2026-11-12 22:33 with the
                // 256 grid: every cell 128 px in the 2560 capture = 4.00 col/px, 1023 px, centred at 1279.
                if (activeConsoleProfile() == 1)
                    pm = {666, 512, 1863, 263, 1863, 470, 745, 24, 123, 125, 46};
                // (after v127v) THE touch-up of 2026-11-25 21:34 (a hair): DIS HB_SP 492 -> 470, DIS VB_SP 25 -> 24, IF VB 124/126 -> 123/125, HBIN 48 -> 46.
                // (after v127u) THE 256 px dump of 2026-11-25 21:22 (Mega Drive PAL 720p x4): HScale 666 and HB 1863/263 stay; DIS HB
                // 514 -> 492, DIS VB 748/26 -> 745/25, IF VB 126/128 -> 124/126, HBIN 50 -> 48.
                vwhat = "720p x4 | 16:9 | zoom, 180 of the 224 lines | square pixels, sharpest";
            } else if (uopt->sizeStep == 1) { // the 320 button
                // v102: 404 was 1705 px wide on a 1280 screen, a third of the picture off each edge. The 4:3 shape for x4 is
                // 4.67 columns = HScale 576, which gives 1196 x 896 = 1.334 and nearly fills the width without passing it.
                // Old entry, if it is ever wanted back: {404, 512, 1974, 174, 2152, 336, 748, 26, 128, 130, 441}
                // v107n: the correction of 2026-11-12 13:46: display window 1950/433 -> 1964/400 and IF HBIN 76 -> 90. The
                // rest (HScale, memory window, vertical, memory 129 MHz / phase 13) is unchanged.
                pm = {576, 512, 1934, 147, 1964, 400, 748, 26, 126, 128, 90};
                // v107q: the Mega Drive's 320 button in PAL 720p x4, dump of 2026-11-12 22:38: HScale 533,
                // 1280 px edge to edge; the 256 grid shows every cell 160 px in the 2560 capture = 5.00 col/px, centred at
                // 1279, so a 320 game gets 4.00 col/px, whole. Replaces the 22:24 entry (672, 3.17 col/px, not whole).
                if (activeConsoleProfile() == 1)
                    pm = {533, 512, 93, 109, 2148, 342, 745, 26, 124, 126, 110};
                // (after v127u) THE 320 px dump of 2026-11-25 21:23 (Mega Drive PAL 720p x4, after resetting values): HScale 533, HB 93/109,
                // DIS HB_SP 342 and IF VB 124/126 stay; DIS HB_ST 2168 -> 2148, DIS VB 748/26 -> 745/26, HBIN 116 -> 110.
                vwhat = "720p x4 | 16:9 | zoom, 180 of the 224 lines | CRT shape, as a TV showed it";
            } else {
                // v105: 5.009 col/px = 1282 px on a 1280 screen, so this step fills it exactly and the fourth has nothing
                // left to grow into: it loads this same state and says so. Measured with the memory at 144 MHz,
                // which is its own value, neither the 129 of the first two steps nor the 162 the old table assumed.
                pm = {537, 512, 2013, 93, 2024, 337, 748, 26, 126, 128, 108};
                vwhat = (uopt->sizeStep == 2) ? "720p x4 | 16:9 | zoom, 180 of the 224 lines | fills the screen width"
                                              : "720p x4 | 16:9 | zoom, 180 of the 224 lines | nothing wider fits: loading the widest";
            }
        } else if (uopt->integer720 == 2) { // x5: 5 lines per line, 144 of the 224 show
            // v107f: two sizes, both measured on 11 Nov with the vertical retouched so the Game Boy's 144 lines all
            // fit (VScale 409 -> 410, IF VB 163/165 -> 161/163; one line was eaten before) and the memory at 185 MHz phase 12.
            // There is no wider step: 538 is 256 x 5 = 1280, the whole canvas, so steps 2 and 3 are not offered (the cycle
            // alternates 0 <-> 1, see cycleFill) and if one is left over from another mode it loads CRT.
            if (uopt->sizeStep == 0) {
                // square pixels, 5.00 columns (2690 / 538): at the edge of what the scaler draws in 720p, so the side
                // borders are there on purpose, covering the garbage past the end of the line
                pm = {538, 410, 1778, 311, 1851, 513, 745, 26, 161, 163, 334};
                vwhat = "720p x5 | 16:9 | zoom, 144 of the 224 lines | square pixels, at the edge of the canvas";
            } else {
                // CRT shape, 5.83 columns (2690 / 461) = 5 x 7/6, the same proportion as the x4 CRT step
                pm = {461, 410, 1958, 158, 2107, 287, 745, 26, 161, 163, 320};
                vwhat = "720p x5 | 16:9 | zoom, 144 of the 224 lines | CRT shape, as a TV showed it";
            }
            // v103: EXPERIMENT, off unless turned on. The preset was loaded with the ADC sampling raised, so the
            // same HScale would now draw a WIDER picture: scale it by the same ratio and the picture comes out exactly where
            // it was, drawn from more samples. The input-line registers live in that same sample domain and scale with it;
            // the output windows are in output units and do not. Nothing here runs when the experiment is off.
            if (uopt->x5Oversample) {
                const uint32_t md = GBS::PLLAD_MD::read();
                if (md > 1458) {
                    pm.hscale = (uint16_t)((uint32_t)pm.hscale * md / 1458);
                    if (pm.hscale > 1023)
                        pm.hscale = 1023;
                    GBS::IF_HSYNC_RST::write((uint16_t)(1170UL * md / 1458));
                    GBS::IF_LINE_SP::write((uint16_t)(1235UL * md / 1458));
                    GBS::IF_HB_ST2::write((uint16_t)(1122UL * md / 1458));
                    GBS::IF_HB_SP2::write((uint16_t)(116UL * md / 1458));
                    vwhat = "720p x5 | 16:9 | zoom, 144 of the 224 lines | OVERSAMPLING EXPERIMENT ON";
                }
            }
        }
        // (after v127w) 720p x5 of the Mega Drive in PAL, 320 px button (step 1) ONLY: the square-pixel state, glitch-free, with the
        // ADC clock lowered to 1900, dump of 2026-11-25 22:21: HScale 350, HB 1988/109, DIS HB 2107/287, DIS VB 745/26, IF VB
        // 161/163, HBIN 264, IF HRST/LINE 950/1015, ST2/SP2 902/116, memory 185 MHz phase 13 (the slot's own). The clock, HScale and
        // the input line registers go in one go (PLLAD_MD + latch first): by hand, step by step, they were out of step for a while
        // and the GBS froze on the last frame. The 256 px button keeps the stock clock and the stock table (the estimates at 1900 /
        // 1800 / 1750 / 1740 did not work out), and coming back from 320 puts the stock clock and input line back, read the first time
        // the mode loaded at the stock clock (>= 2200). Another mode reloads the preset (clock back by itself).
        if (activeConsoleProfile() == 1 && uopt->integer720 == 2 && rto->presetID == 0x13) {
            static uint16_t stockMd = 0, stockHrst = 0, stockLine = 0, stockSt2 = 0;
            const uint16_t nowMd = GBS::PLLAD_MD::read();
            if (nowMd >= 2200) {
                stockMd = nowMd;
                stockHrst = GBS::IF_HSYNC_RST::read();
                stockLine = GBS::IF_LINE_SP::read();
                stockSt2 = GBS::IF_HB_ST2::read();
            }
            if (uopt->sizeStep <= 1) {
                if (stockMd) {
                    GBS::PLLAD_MD::write(1900);
                    latchPLLAD();
                    GBS::IF_HSYNC_RST::write(950);
                    GBS::IF_LINE_SP::write(1015);
                    GBS::IF_HB_ST2::write(902);
                    if (uopt->sizeStep == 1) {
                        pm.hscale = 350;
                        pm.hbSt = 1988;
                        pm.hbSp = 109;
                        pm.hbinSp = 264;
                        vwhat = "720p x5 | 16:9 | zoom, 144 of the 224 lines | square pixel, ADC clock 1900";
                    } else {
                        // (after v127z) «intento final» of 2026-11-26: the 256 px at the SAME clock and the SAME input line as the
                        // working 320 (1900, 950/1015, 902/116), all in one go. HScale 437 = the 320's 350 x 320 / 256 = 437.5 (5.00 columns,
                        // square, the measure of 4.995 at 350 gives 437.1); output windows as the stock 256 (HB 1778/311, DIS 1851/513, DIS VB
                        // 745/26); HBIN 334 x 1900 / 2345 = 270.6 -> 271 (the input line is in that sample domain). ESTIMATE, to be repositioned.
                        // (after v127z) THE FINAL 256 px at 1900 («ya funcionan los dos perfectamente»), dump of 2026-11-26 07:49: HScale 437,
                        // IF line 950/1015, ST2/SP2 902/116, DIS VB 745/26 and IF VB 161/163 stand; HB 1778/311 -> 1974/221, DIS HB
                        // 1851/513 -> 2055/259, HBIN 271 -> 202; memory 185 MHz phase 8 (memDefaultFor case 9, Mega Drive step 0).
                        pm.hscale = 437;
                        pm.hbSt = 1974;
                        pm.hbSp = 221;
                        pm.dHbSt = 2055;
                        pm.dHbSp = 259;
                        pm.hbinSp = 202;
                        vwhat = "720p x5 | 16:9 | zoom, 144 of the 224 lines | square pixel 256 px, ADC clock 1900 (final attempt)";
                    }
                }
            } else if (stockMd && nowMd < 2200) {
                GBS::PLLAD_MD::write(stockMd);
                latchPLLAD();
                GBS::IF_HSYNC_RST::write(stockHrst);
                GBS::IF_LINE_SP::write(stockLine);
                GBS::IF_HB_ST2::write(stockSt2);
            }
        }
        // (after v127p) THE NTSC SNES 720p CARRIED OVER to the PAL preset (2026-11-25: the PAL profile is a copy
        // of the NTSC one, no forced NTSC). Both buttons (step 0 = 256 px, step 1 = 320 px), every 720p mode. The NTSC
        // entries of integer720ApplyInner are the source: HScale x 1.119 (PAL constant 2689 against 2403: its own PAL x4 256
        // px 672 = 601 x 1.119 and x5 538 = 481 x 1.119 confirm it), the horizontal windows and HBIN x 0.8975 (line 2156
        // against 2402), the display vertical as it is and the IF VB + 54 (the NTSC 8/10 -> the PAL 62/64, the dumps of
        // 12:24 and 12:17). The x3 16:9 vertical is the corrected one (744/28). ESTIMATES until the PAL dumps.
        if (snesProfile() && !pceProfile() && activeConsoleProfile() == 2 && uopt->sizeStep <= 1) {
            struct N720 { uint16_t hs, hbSt, hbSp, dSt, dSp, hbin, ifSt, ifSp, dvSt, dvSp; };
            const uint8_t s1 = uopt->sizeStep;
            N720 n;
            bool have = true;
            if (uopt->integer720 == 4)
                n = s1 == 0 ? N720{801, 2016, 552, 1889, 753, 77, 8, 10, 744, 28}
                            : N720{641, 2146, 422, 2027, 615, 134, 8, 10, 744, 28};
            else if (uopt->integer720 == 1)
                n = s1 == 0 ? N720{601, 2191, 363, 2156, 518, 134, 8, 10, 750, 26}
                            : N720{481, 2322, 162, 2328, 346, 154, 8, 10, 750, 26};
            else if (uopt->integer720 == 3)
                n = s1 == 0 ? N720{601, 2191, 363, 2156, 518, 134, 62, 64, 750, 26}
                            : N720{481, 2322, 162, 2328, 346, 154, 62, 64, 750, 26};
            else if (uopt->integer720 == 2)
                n = s1 == 0 ? N720{481, 2322, 162, 2328, 346, 154, 110, 112, 750, 26}
                            : N720{385, 2272, 202, 2272, 382, 392, 109, 111, 750, 26};
            else {
                have = false;
                n = N720{0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            }
            if (have) {
                pm.hscale = (uint16_t)(((uint32_t)n.hs * 1119 + 500) / 1000);
                pm.hbSt = (uint16_t)(((uint32_t)n.hbSt * 8975 + 5000) / 10000);
                pm.hbSp = (uint16_t)(((uint32_t)n.hbSp * 8975 + 5000) / 10000);
                pm.dHbSt = (uint16_t)(((uint32_t)n.dSt * 8975 + 5000) / 10000);
                pm.dHbSp = (uint16_t)(((uint32_t)n.dSp * 8975 + 5000) / 10000);
                pm.hbinSp = (uint16_t)(((uint32_t)n.hbin * 8975 + 5000) / 10000);
                pm.dVbSt = n.dvSt;
                pm.dVbSp = n.dvSp;
                pm.ifVbSt = n.ifSt + 54;
                pm.ifVbSp = n.ifSp + 54;
                // (after v127p) THE PAL 720p x3 4:3, 256 px button (step 0), positioned by hand, dump of 2026-11-25 14:28
                // (cells 72 x 72): HScale 673 and HB_SP 326 are the carried-over ones; HB 1839, DIS HB 1859/509, HBIN 138,
                // DIS VB 749/27 are the hand-set ones (carried over: 1966, 1935/465, 120, 750/26).
                // (after v127p) THE PAL 720p x3 4:3 CRT, 320 px button (step 1), dump of 2026-11-25 14:31 (cells 90 x 72, the
                // TV shape 1.25): the carried-over values hit HScale 538, HB 2084/145, DIS HB 2089/311 exactly; moved HBIN
                // 138 -> 158 and the vertical 750/26 -> 749/27.
                // (after v127p) THE PAL 720p x3 16:9 CRT, 320 px button (step 1), dump of 2026-11-25 14:37 (cells 60 x 48, the
                // TV shape 1.25): carried over hit HScale 717, HB_SP 379 and DIS HB_SP 552; moved HB_ST 1926 -> 1807,
                // DIS HB_ST 1819 -> 1813, HBIN 120 -> 144 and the vertical 744/28 -> 747/25.
                // (after v127p) THE PAL 720p x3 16:9, 256 px button (step 0, 8:7), dump of 2026-11-25 14:39: carried over hit
                // HScale 896, HB_SP 495 and DIS HB_SP 676; moved HB_ST 1809 -> 1679, DIS HB_ST 1695 -> 1691, HBIN 69 -> 85
                // and the vertical 744/28 -> 747/25.
                // (after v127p) THE PAL 720p x4 16:9, 256 px button (step 0), dump of 2026-11-25 14:43: carried over hit
                // HScale 673, HB_SP 326 and the vertical 750/26; moved HB_ST 1966 -> 1855, DIS HB 1935/465 -> 1856/507,
                // HBIN 120 -> 138 and IF VB 116/118 -> 125/127.
                // (after v127p) THE PAL 720p x4 16:9 CRT, 320 px button (step 1), dump of 2026-11-25 14:44: carried over hit
                // HScale 538, HB 2084/145, DIS HB 2089/311 and the vertical 750/26; moved HBIN 138 -> 158 and IF VB
                // 116/118 -> 125/127 (the same two as in the x4 256 px).
                if (uopt->integer720 == 3 && s1 == 1) {
                    pm.hbinSp = 158;
                    pm.ifVbSt = 126; // (after v127s) the dump of 20:35: one fine touch up, 125/127 -> 126/128 (the 256 px keeps 125/127)
                    pm.ifVbSp = 128;
                }
                if (uopt->integer720 == 3 && s1 == 0) {
                    pm.hbSt = 1855;
                    pm.hbSp = 326;
                    pm.dHbSt = 1856;
                    pm.dHbSp = 507;
                    pm.hbinSp = 138;
                    pm.dVbSt = 750;
                    pm.dVbSp = 26;
                    pm.ifVbSt = 125;
                    pm.ifVbSp = 127;
                }
                if (uopt->integer720 == 4 && s1 == 0) {
                    pm.hbSt = 1679;
                    pm.hbSp = 495;
                    pm.dHbSt = 1691;
                    pm.dHbSp = 676;
                    pm.hbinSp = 85;
                    pm.dVbSt = 747;
                    pm.dVbSp = 25;
                }
                if (uopt->integer720 == 4 && s1 == 1) {
                    pm.hbSt = 1807;
                    pm.hbSp = 379;
                    pm.dHbSt = 1813;
                    pm.dHbSp = 552;
                    pm.hbinSp = 144;
                    pm.dVbSt = 747;
                    pm.dVbSp = 25;
                }
                // (after v127p) THE PAL 720p x5 16:9, 256 px button (step 0), dump of 2026-11-25 14:47: carried over hit HScale
                // 538, HB 2084/145, DIS HB 2089/311 and IF VB 164/166; moved HBIN 138 -> 160 and DIS VB 750/26 -> 745/26.
                if (uopt->integer720 == 2 && s1 == 0) {
                    pm.hbinSp = 160;
                    pm.dVbSt = 745;
                    pm.dVbSp = 26;
                }
                if (uopt->integer720 == 1 && s1 == 1) {
                    pm.hbinSp = 158;
                    pm.dVbSt = 749;
                    pm.dVbSp = 27;
                }
                if (uopt->integer720 == 1 && s1 == 0) {
                    pm.hbSt = 1839;
                    pm.hbSp = 326;
                    pm.dHbSt = 1859;
                    pm.dHbSp = 509;
                    pm.hbinSp = 144; // (after v127y) the dump of 2026-11-25 23:49 (HB 1842/329 with the 3 touches, DIS 1859/509, dVb 749/27 stand): HBIN 138 -> 144
                    pm.dVbSt = 749;
                    pm.dVbSp = 27;
                }
                // (after v127r) 2026-11-25: the SNES PAL 720p x3 (4:3 and 16:9) and x4 move «two or three fine touches»
                // to the right: three, as the move button does it (HB_ST and HB_SP + 3; one fine touch = 1 unit). Both size
                // steps (256 and CRT). If it is too much or too little it is this one number.
                if (activeConsoleProfile() == 2 && s1 <= 1 && (uopt->integer720 == 1 || uopt->integer720 == 3 || uopt->integer720 == 4)) {
                    const uint16_t kRight = 3;
                    pm.hbSt = (uint16_t)(pm.hbSt + kRight);
                    pm.hbSp = (uint16_t)(pm.hbSp + kRight);
                }
                vwhat = "720p PAL | the NTSC placement carried over";
            }
        }
        // (after v127t) of 2026-11-25: the Mega Drive in PAL is the NTSC one carried over completely, whatever was
        // measured in PAL weeks ago. Step by step; this is the first: 720p x3 4:3, both buttons. Source = the NTSC Mega Drive
        // entries of integer720ApplyInner (256 px {603 / 2203 / 353 / 2060 / 569 / HBIN 120}, 320 px {481 / 2282 / 137 / 2320 /
        // 338 / HBIN 116}, vertical 722/52), the same factors as the SNES: HScale x 1.119, horizontal windows and HBIN x 0.8975,
        // IF VB 8/10 + 54 = 62/64, display vertical as it is. ESTIMATES until the PAL dumps.
        if (activeConsoleProfile() == 1 && uopt->integer720 == 1 && uopt->sizeStep <= 1) {
            const bool st0 = (uopt->sizeStep == 0);
            const uint16_t hs = st0 ? 603 : 481, hbSt = st0 ? 2203 : 2282, hbSp = st0 ? 353 : 137;
            const uint16_t dSt = st0 ? 2060 : 2320, dSp = st0 ? 569 : 338, hbin = st0 ? 120 : 116;
            pm.hscale = (uint16_t)(((uint32_t)hs * 1119 + 500) / 1000);
            pm.hbSt = (uint16_t)(((uint32_t)hbSt * 8975 + 5000) / 10000);
            pm.hbSp = (uint16_t)(((uint32_t)hbSp * 8975 + 5000) / 10000);
            pm.dHbSt = (uint16_t)(((uint32_t)dSt * 8975 + 5000) / 10000);
            pm.dHbSp = (uint16_t)(((uint32_t)dSp * 8975 + 5000) / 10000);
            pm.hbinSp = (uint16_t)(((uint32_t)hbin * 8975 + 5000) / 10000);
            pm.dVbSt = 722;
            pm.dVbSp = 52;
            pm.ifVbSt = 62;
            pm.ifVbSp = 64;
            // (after v127u) THE 320 px (step 1), dump of 2026-11-25 21:04 (cells 64 x 48, centred): the carried-over HScale 538, HB 2048/123,
            // DIS HB 2082/303, HBIN 104 and IF VB 62/64 stand; changed the display vertical 722/52 -> 745/26.
            if (!st0) {
                pm.dVbSt = 745;
                pm.dVbSp = 24; // (after v127z) the dump of 2026-11-26 07:55: 26 -> 24, HBIN 104 -> 116
                pm.hbinSp = 116;
            }
            // (after v127u) THE 256 px (step 0), dump of 2026-11-25 21:08 (seven fine touches left): HScale 675, HB 1977/317, DIS HB 1849/511
            // and IF VB 62/64 equal the carried-over values; changed HBIN 108 -> 122 and the display vertical 722/52 -> 745/26.
            if (st0) {
                pm.hbinSp = 122;
                pm.dVbSt = 745;
                pm.dVbSp = 26;
            }
            vwhat = "720p x3 4:3 | Mega Drive PAL | the NTSC placement carried over";
        }
        // (after v130c) THE PC ENGINE 320 / 352 px BUTTONS IN PAL, 720p x3 (4:3 and 16:9). Every mode exists in PAL and in NTSC: the
        // NTSC entries of these two buttons (integer720ApplyInner, measured 2026-11-23) carried over with the factors of the rest of
        // the PAL path: HScale x 1.119, horizontal windows and HBIN x 0.8975, display vertical as it is, IF VB + 54.
        // ESTIMATES until the PAL dumps. Until now the PAL block ignored them (it only knows the size steps), so the buttons did nothing.
        if (pceProfile() && (uopt->integer720 == 1 || pal169) && (rto->integer1080Centered == 3 || rto->integer1080Centered == 4)) {
            const bool m352 = (rto->integer1080Centered == 4);
            const Mode720 n = pal169 ? (m352 ? Mode720{603, 683, 2039, 313, 2051, 577, 745, 27, 25, 27, 62}
                                             : Mode720{603, 683, 2037, 316, 2047, 585, 745, 27, 25, 27, 64})
                                     : Mode720{452, 683, 2394, 124, 2398, 382, 745, 27, 25, 27, 174};
            pm.hscale = (uint16_t)(((uint32_t)n.hscale * 1119 + 500) / 1000);
            pm.vscale = n.vscale;
            pm.hbSt = (uint16_t)(((uint32_t)n.hbSt * 8975 + 5000) / 10000);
            pm.hbSp = (uint16_t)(((uint32_t)n.hbSp * 8975 + 5000) / 10000);
            pm.dHbSt = (uint16_t)(((uint32_t)n.dHbSt * 8975 + 5000) / 10000);
            pm.dHbSp = (uint16_t)(((uint32_t)n.dHbSp * 8975 + 5000) / 10000);
            pm.dVbSt = n.dVbSt;
            pm.dVbSp = n.dVbSp;
            pm.ifVbSt = (uint16_t)(n.ifVbSt + 54);
            pm.ifVbSp = (uint16_t)(n.ifVbSp + 54);
            pm.hbinSp = (uint16_t)(((uint32_t)n.hbinSp * 8975 + 5000) / 10000);
            // (after v130g) 4:3: the display window ends 4 units after the memory window (NTSC dump of 2026-11-28: 8 split the
            // picture in two, 4 is clean). Scaling both by 0.8975 made it 3, so the NTSC distance is kept here.
            if (!pal169)
                pm.dHbSt = (uint16_t)(pm.hbSt + 4);
            measured = true;
            vwhat = m352 ? (pal169 ? "720p x3 16:9 | PC Engine 352 px PAL | the NTSC placement carried over" : "720p x3 4:3 | PC Engine 352 px PAL | the NTSC placement carried over")
                         : (pal169 ? "720p x3 16:9 | PC Engine 320 px PAL | the NTSC placement carried over" : "720p x3 4:3 | PC Engine 320 px PAL | the NTSC placement carried over");
        }
        write720(pm);
        // v106: the mode names itself inside vwhat now, so the prefix no longer repeats "720p PAL"
        SerialM.print(F("[3x4x-v130k] "));
        if (measured) {
            SerialM.print(vwhat);
            if (uopt->integer720 == 1 || pal169) { // x3 and 16:9 use the horizontal entries above as they stand
                SerialM.print(F(" | "));
                SerialM.print(what);
            }
            SerialM.println();
        } else { // say it plainly instead of inventing a placement
            SerialM.print(vwhat);
            SerialM.println(F(" | this size step is NOT MEASURED in this mode yet: loading the measured one instead"));
        }
        return;
    }
    // v72: the PC Engine has its own measured entries below, and its "320 px" button is a real second size step (a wider
    // picture with whole columns), not the SNES trick of reusing the 256 px values, so it must not be folded into 256.
    // (after v121) 2026-11-21: every PC Engine 720p mode sat off centre and wanted five fine steps to the
    // right. A fine step is +1 on VDS_HB_ST and VDS_HB_SP (shiftHorizontal), so the three PC Engine pairs below carry +5
    // on those two fields: 16:9, x5 and x3 (4:3), and the x3 pair covers x4 as well, which reuses its horizontal.
    const bool pce = pceProfile();
    // v78: the SNES "320 px" button used to collapse to the 256 px values in every mode, because in v53 its own capture window
    // came out corrupted. In x5 it is back: it works at HScale 401 with the SDRAM clock at 185 MHz and the
    // right border pulled in to hide the garbage past the end of the valid line data (the same technique the PAL 1080p path
    // uses on purpose). The collapse stays for x3 and x4, where the fill behaviour is correct and confirmed.
    const bool snes320x5 = snes && !pce && nativeWidth == 320 && x5;
    const bool snes320 = snes && !pce && nativeWidth == 320 && !snes320x5;
    const bool snes320w169 = snes320 && w169 && !fill;
    if (snes320 && !snes320w169) {
        nativeWidth = 256; // SNES: the 256 px values (x5 and the fill use them too)
    }
    // hbSt, hbSp, dHbSt, dHbSp, dVbSt, dVbSp, ifVbSt, ifVbSp, hbinSp
    Mode720 m;
    if (w169 && !fill) {
        // v55/v56: x3 for a 16:9 TV, whole pixels 3 x 3 (picture 768 px for 256 px games, 960 px for 320 px games). With the fill
        // button on, this mode uses exactly the same values as the 720p x3 (4:3) mode (the chain below).
        // SNES 256 px (v56): the centred state (HScale 801, window 1889/753, VB(d) 730/50, IF HBIN 78); measured with the
        // suite grid: 21 x 24 px cells of 63 x 72 output px, no uneven column.
        // Mega Drive (ESTIMATED from the hand-made state with HScale 644: window 616..2014 = 960 px, HB 2158/413, IF HBIN 104):
        // 320 px: HScale 641 = 3.00 columns per pixel (960 px); 256 px: HScale 804 = 3.00 columns (768 px, window narrowed
        // keeping the same centre 1315).
        if (pce) {
            // v72: measured on the 256 px grid. 256 px: 3.003 columns, picture 771 px, centre exactly 960.
            // "320 px": 3.99 columns, picture 1023 px, centre 960.75. The old shared value 641 gave 3.76 columns, which is
            // why that button could never be centred on this console.
            if (nativeWidth == 320)
                m = {643, 683, 1991, 372, 1995, 613, 745, 27, 25, 27, 72}; // (after v126c) the dump: HB ST 1991 (1998 before), DIS HB 1995/613 (2063/569 before). (after v126c) IF VB 25/27, the request (24/26 before). (after v124) the capture + dump of 2026-11-23 09:19: CRT = fill 4:3 (3.75 columns, 60 px cells); 603 / HB 2040/314 / HBIN 76 before. (after v123k) 5 fine touches left (HBIN +2 each, 66 before); v77: IF VB matched to the 256 px button so the picture does not jump between widths; (after v111g) the state of 2026-11-18 08:05 (HB_ST 2323 -> 2035, window 2063/569), measured 4.000 columns, 1024 px, centre 639.5 of 1280
            else
                m = {804, 683, 1881, 563, 1905, 727, 745, 27, 25, 27, 92}; // (after v126c) the dump: DIS HB 1905/727 (1877/755 before). (after v126c) IF VB 25/27, the request. (after v124) the dump of 2026-11-23 09:23 (HBIN 94 before). (after v123k) 5 fine touches left (HBIN +2 each, 84 before); (after v111g) the borders of 2026-11-18 08:03 (HB 1886 -> 1876, window 1903/753 -> 1877/755)
        } else if (snes320w169)
            m = {641, 683, 2146, 422, 2027, 615, 730, 50, 8, 10, 134}; // (after v127f) the dump: DIS HB SP 615 (623 before). // v77: measured and centred (the v57 estimate had HBIN 78, which left it off-centre). 3.75 columns, 960 px, CRT proportion
        else if (snes)
            m = {801, 683, 2016, 552, 1889, 753, 730, 50, 8, 10, 77}; // (after v127f) 1 px right (HBIN 78 -> 77, ~1.15 output px per unit at 3 columns)
        else if (nativeWidth == 320)
            m = {641, 683, 2158, 413, 2010, 608, 722, 50, 8, 10, 116}; // v60: the hand-made timings (HB(d) 2010/608, VB(d) 722/50)
        else
            m = {804, 683, 2016, 552, 1856, 737, 722, 52, 8, 10, 104}; // v61: the refined window
    } else if (x5) {
        if (nativeWidth == 320) {
            // ESTIMATE (not measured): 5 columns for 320 px = HScale 385, picture 1600 px wide (160 px lost on each side);
            // IF HBIN +60 over the 320 px x4 preset (~32 game pixels = 60 IF units skipped on the left)
            if (snes)
                // v78: measured. 401 = 6.00 columns per pixel, picture 1536 px, so ~128 px fall off each side;
                // the right border is pulled in to 2124 to cover the garbage that appears past IF_HB_ST2. Needs the SDRAM
                // clock raised, which the x5 load now does for this profile too.
                m = {401, 409, 2096, 2, 2124, 514, 750, 26, 109, 111, 206};
            else
                // (after v111f) MEASURED, the capture of 2026-11-18 01:51 (suite grid, 720p x5 320 px): 385 gave 5.012 columns a
                // pixel (cells 160.4 capture px at x2), one wider column every ~80 game pixels; 386 = 4.999. Centred (both
                // axes within 1.5 capture px). Vertical 409 = 5.007 lines, the nearest the chip has (x5 exact needs 409.6).
                // His state at 386 (01:53): HB 2286/141, the rest unchanged.
                m = {386, 409, 2286, 141, 2320, 338, 750, 26, 108, 110, 326};
        } else if (pce) {
            // v76: the measured x5 states. HB_ST and HB(d)_ST both sit at 2328, which is what stopped the corrupted columns.
            // 320 px uses HScale 389 instead of 385 (385 glitched), plus the SDRAM clock raised below.
            if (nativeWidth == 320)
                m = {389, 409, 2333, 167, 2328, 346, 750, 26, 110, 112, 302};
            else
                m = {482, 409, 2333, 167, 2328, 346, 750, 26, 122, 124, 152};
        } else if (snes) {
            // (after v127f) THE SNES x5 CRT (dump 2026-11-24 23:44): HScale 401 = 6 whole columns (1536 px, 1.20 against
            // the TV's 1.25: the choice for now, clean; 385 can still be tried). Needs 144 MHz phase 11, below in memDefaultFor.
            if (nativeWidth == 320)
                m = {385, 409, 2272, 202, 2272, 382, 750, 26, 109, 111, 392}; // (after v127f) THE 385 (dump 23:49): the exact TV shape 1.25, clean
            else
            m = {481, 409, 2322, 162, 2328, 346, 750, 26, 110, 112, 154}; // SNES: measured
        } else {
            // v54 (attempt): Mega Drive 256 px x5 = HScale 481 with the horizontal values of the Mega Drive 320 px profile, which
            // clean at x3 (the old SNES-like values 2322/2328 left HB_ST well below HB(d)_ST, the cause suspected for the sparkle)
            m = {481, 409, 2282, 137, 2320, 338, 750, 26, 107, 109, 134}; // v61: the refined timings
        }
    // (after v123c) 2026-11-22: the PC Engine's 720p x3 4:3 moves two fine steps left (hbSt and hbSp -2)
    // and its left border opens two fine steps (dHbSp -2, less black on that side). Both widths.
    } else if (pce && !fill) {
        // v72: PC Engine 720p x3 (4:3), the hand-centred windows, written down while the scale was still on the shared
        // 601 / 481; the grid measurements put the whole-column values at 603 and 483 (a 0.3 % difference, about 3 px over the
        // picture), so the scale here is the measured one and the picture may sit up to 2 px off the centring. The vertical
        // window 745 / 27 is the one the later PC Engine dumps show in every mode.
        if (nativeWidth == 320)
            m = {482, 683, 2257, 161, 2256, 382, 745, 27, 25, 27, 152}; // (after v123l) 5 fine touches left (HBIN 142 before); (after v123e) the dump of 2026-11-22 08:57: HBIN 148 -> 142 // v76: the centred state at 482 (chosen over 483); (after v111g) IF VB 23/25 -> 25/27, the vertical of 2026-11-18 08:00 (measured 5.005 columns, 1280 px, centred)
        else
            m = {603, 683, 2062, 366, 2090, 542, 745, 27, 25, 27, 132}; // (after v126c) the dump: DIS HB 2090/542 (2062/566 before). (after v123l) 5 fine touches left (HBIN 122 before); (after v123e) the dump of 2026-11-22 08:59: HB_ST 2194 -> 2062, HBIN 130 -> 122 // v75: re-centred at scale 603 and measured; (after v111g) IF VB 25/27, the same height as the 320 px of 2026-11-18 (4.000 columns confirmed)
                                                                        // on the grid: 4.008 columns, 1026 px, centre 959 of 1920
    } else if (nativeWidth == 320) {
        m = {481, 683, 2282, 137, 2320, 338, 722, 52, 8, 10, 116}; // Mega Drive 320 px: the final x3 profile (v48, read from the .gbsp)
    } else if (fill) {
        if (snes)
            m = {481, 683, 2322, 162, 2328, 346, 750, 26, 8, 10, 154}; // fills the width: 5.0 columns
        else
            m = {481, 683, 2282, 137, 2320, 338, 722, 52, 8, 10, 116}; // v54: Mega Drive fill = its working 320 px values (the 2322 set corrupted pixels)
    } else if (snes) {
        m = {601, 683, 2191, 363, 2156, 518, 750, 26, 8, 10, 134}; // SNES 256 px, measured: 4.0 columns
    } else {
        // v53: Mega Drive 256 px, x3 and x4: the hand-made state with Valis (HB_ST not cut: the older values with HB_ST 2042
        // made some lines tremble); the x4 vertical part is set below
        m = {603, 683, 2203, 353, 2060, 569, 722, 52, 8, 10, 120}; // v61: Mega Drive vertical window 722/52 (the border), as the 320 px entry
    }
    // (after v124d) THE NEW BUTTONS: the PC Engine's middle dot clock (its 320 and 352 px games) in whole pixels, 720p x3 only.
    // Measured on 2026-11-23 with the suite grids (48 px square cells in 16:9; 64 px signal = 48 in a 4:3 canvas).
    // integer1080Centered 3 = 320 px, 4 = 352 px. In 4:3 both are the same registers (the 352 loses 16 px a side).
    if (pce && !fill && !x4 && !x5 && (rto->integer1080Centered == 3 || rto->integer1080Centered == 4)) {
        if (w169)
            m = (rto->integer1080Centered == 3) ? Mode720{603, 683, 2037, 316, 2047, 585, 745, 27, 25, 27, 64} /* (after v126c) the dump: DIS HB 2047/585 (2087/557 before) */ /* (after v124f) the dump 19:02, borders opened (DIS 2063/569 before) */
                                                : Mode720{603, 683, 2039, 313, 2051, 577, 745, 27, 25, 27, 62} /* (after v126c) the dump: DIS HB 2051/577 (2099/529 before) */;
        else
            m = Mode720{452, 683, 2394, 124, 2398, 382, 745, 27, 25, 27, 174}; // (after v130f) the dump of 2026-11-28 03:39: with DIS HB 2402 (8 over HB_ST) the picture came out split in two until «deshacer escala» wrote 2398; 4 over HB_ST is the clean state
    }
    if (x4) {
        // v48: x4 = the x3 horizontal placement with 4 lines per game line (VScale 512), cropped to 180 lines (IF VB 62/64 from the SNES x4)
        m.vscale = 512;
        m.dVbSt = 750;
        m.dVbSp = 26;
        m.ifVbSt = 62;
        m.ifVbSp = 64;
        if (pce) { // v76: the crop positioned by hand, both widths
            m.ifVbSt = 84;
            m.ifVbSp = 86;
        }
        // (after v123k) the PC Engine x4 has its own entries, independent of the x3 4:3 ones it used to
        // borrow. Starting values = that borrowed placement + the five fine touches to the left (HBIN +10). Change these
        // without touching the x3 4:3 and the other way round.
        if (pce && !fill) {
            if (nativeWidth == 320)
                m = {482, 512, 2257, 161, 2256, 382, 750, 26, 84, 86, 152};
            else
                m = {603, 512, 2062, 366, 2094, 538, 750, 26, 84, 86, 132}; // (after v126c) the dump 16:9 256: DIS HB 2094/538 (2062/566 before).
        }
        // (after v127e) THE MEGA DRIVE x4 320 px (dump of 2026-11-24 13:00): the x3 16:9 grid (48 px square
        // cells, the 960 px 4:3 area) made 4/3 bigger: 64 x 64 px cells checked on the capture, a few px over each side.
        if (!pce && !snes && !fill && nativeWidth == 320)
            m = {481, 512, 2220, 129, 2253, 372, 744, 22, 70, 72, 108};
        // (after v127f) and the 256 px, the dump of 18:57: 4 x 4 square pixels (= 8:7 picture), 1024 px, centred and bordered by hand.
        // The 528 of 13:02 (4.57 columns) was a misreading of 8:7 and is gone.
        else if (!pce && !snes && !fill)
            m = {603, 512, 2061, 341, 2061, 568, 744, 22, 70, 72, 106};
        // (after v127f) THE SNES x4 256 px (dump 22:49): 4 x 4 square pixels checked on the grid (84 x 96 cells of 21 x 24),
        // ~1024 px centred. Own vertical: DIS VB 745/26, IF VB 73/75 (the shared x4 crop gave 750/26 and 62/64).
        if (snes && !pce && !fill && nativeWidth != 320)
            m = {601, 512, 2191, 363, 2156, 518, 745, 26, 73, 75, 134};
        // (after v127r) 2026-11-25: the SNES 720p x4 256 and CRT buttons were not in the same vertical position
        // (the two captures: the CRT 48 px lower on 2560 x 1440) and the 256 one is the good one (dump of 20:10: DIS VB 745/26,
        // IF VB 73/75). The CRT was still on the shared x4 crop (750/26 and 62/64): the same vertical as the 256 now.
        if (snes && !pce && (fill || nativeWidth == 320)) { // the SNES CRT is the «llenar» state in NTSC x4: the 20:16 dump showed 750/26 and 68/70
            m.dVbSt = 745;
            m.dVbSp = 26;
            m.ifVbSt = 74; // (after v127s) the dump of 20:26: one fine touch up from 73/75, now level with the 256 px
            m.ifVbSp = 76;
        }
    }
    write720(m);
}

// v62: 960p x4 (1280x960) and 480p x2 (720x480). Both stock presets already use PLLAD_MD 2345 and HScale 512, so the
// columns-per-pixel formula of the 720p modes holds: HScale 481 = 4 columns for a 320 px pixel, 603 / 601 = 4 columns for
// 256 px, 962 = 2 columns for 320 px. 960p: VScale 512 is exactly 4 lines per game line and 320 px x 4 = 1280 px fills the
// screen (240 lines x 4 = 960). 480p: the stock VScale 1023 is 2.002 lines per line (an exact x2 would need 1024, one more
// than the register holds) and 320 px x 2 = 640 px of the 720.
// The windows come from each preset's own full-width window (720p 340..2322, 960p 612..2594, 480p 510..2490, all 1982 / 1980
// units wide), with the memory blanking kept at the same distance as in the hand-made 720p profiles. ROUND 1: measured
// nothing, so the placement is an estimate to be corrected with the move / edge buttons.
void integerLowApply(uint16_t nativeWidth)
{
    const bool x2 = (uopt->integerLow == 2); // 480p x2
    const bool snes = snesProfile();
    Mode720 m;
    if (x2) {
        if (nativeWidth == 320) {
            // 2 columns per pixel: 640 px of the 720 (40 px of border each side)
            m = {962, 1023, 2313, 263, 2380, 620, 520, 32, 8, 10, 116};
        } else {
            // 256 px at 2 columns needs the ADC clock at 80% (the preset was reloaded for it): 512 px picture
            m = {962, 1023, 2137, 439, 2204, 796, 520, 32, 8, 10, (uint16_t)(snes ? 71 : 79)};
        }
    } else {
        if (nativeWidth == 320) {
            // 4 columns per pixel: 1280 px, the whole screen width
            m = {481, 512, 2556, 411, 2594, 612, 1000, 38, 8, 10, 116};
        } else if (snes) {
            m = {601, 512, 2358, 609, 2396, 810, 1000, 38, 8, 10, 134};
        } else {
            m = {603, 512, 2358, 609, 2396, 810, 1000, 38, 8, 10, 120};
        }
    }
    write720(m);
}

void integerLowLoad()
{
    // IF line for the stock ADC clock, exactly as the 720p modes do it
    const uint16_t md2 = GBS::PLLAD_MD::read() >> 1;
    GBS::IF_HSYNC_RST::write(md2);
    GBS::IF_LINE_SP::write(md2 + 65);
    GBS::IF_HB_ST2::write((md2 - 47) & 0xfffe);
    GBS::IF_HB_SP2::write(116);
    const bool x2 = (uopt->integerLow == 2);
    // the SNES has no 320 px games; in 480p x2 a 256 px picture needs the lowered ADC clock, so it only loads that way
    // when the 256 px button asked for it (which reloads the preset)
    const uint16_t w = consoleModeLoadWidth(((snesProfile() && !pceProfile()) || (x2 && uopt->integer1080Narrow)) ? 256 : 320); // (after v107v)
    rto->integer1080Centered = (w == 320) ? 1 : 2; // (after v123i) before the placement: its border record
    integerLowApply(w);
    SerialM.print(x2 ? F("[3x4x-v130k] 480p x2 loaded for ") : F("[3x4x-v130k] 960p x4 loaded for "));
    SerialM.print(w);
    SerialM.println(F(" px sources (ROUND 1: estimated placement, correct it with move / borders)"));
}

void integer720Load()
{
    // IF line for the stock ADC clock (stock rule: HRST = PLLAD_MD / 2, LINE = HRST + 65, ST2 = (PLLAD_MD / 2 - 47) & ~1)
    const uint16_t md2 = GBS::PLLAD_MD::read() >> 1;
    GBS::IF_HSYNC_RST::write(md2);
    GBS::IF_LINE_SP::write(md2 + 65);
    GBS::IF_HB_ST2::write((md2 - 47) & 0xfffe);
    GBS::IF_HB_SP2::write(116);
    // (after v107v) or the width this console was last used in (ConsoleModes)
    const uint16_t w = consoleModeLoadWidth((snesProfile() && !pceProfile()) ? 256 : 320); // the SNES has no 320 px games; (after v123h) the PC Engine opens in CRT
    rto->integer1080Centered = (w == 320) ? 1 : 2; // (after v123i) before the placement: its border record
    // (after v124d) the PC Engine's 320 / 352 px whole-pixel buttons, if that is what this console used last (720p x3 only)
    {
        const uint8_t mid = consoleModePendingMid();
        if (mid && pceProfile() && (uopt->integer720 == 1 || uopt->integer720 == 4))
            rto->integer1080Centered = mid;
    }
    integer720Apply(w);
    SerialM.print(F("[3x4x-v130k] 720p x"));
    SerialM.print(uopt->integer720 == 2 ? 5 : (uopt->integer720 == 3 ? 4 : 3));
    SerialM.print(F(" loaded for "));
    SerialM.print(w);
    SerialM.println(F(" px sources"));
}

bool integerIsSnesProfile()
{
    return snesProfile();
}

bool integerIsPceProfile() // (after v123h) for the 1080p load in GBSController
{
    return pceProfile();
}

// v75: the PC Engine shares the SNES placement, but NOT the SNES trick where the "320 px" button means "fill the width with
// the 256 px values". On this console that button is a real second size step with its own measured entries, so the buttons
// must not turn the fill on for it: doing so skipped the PC Engine entries entirely and landed on the fill values
// (HScale 481 with the 2322 / 2328 window), which is the combination that corrupts columns.
// v77: the MHz a PLL_MS value gives, for the messages (same table as the "cycle ram" command).
// v84: the four SDRAM clock delay bits of bank 4, as one 0..15 value. They shift the phase, not the frequency: a few lines
// landing in the wrong place at a frequency that is otherwise fine is a sampling-edge problem, and nothing in this firmware
// had ever touched them. 0 leaves whatever the preset loaded.
// v88: which memory slot the mode on screen uses. (PAL ? 8 : 0) + (1080p ? 4 : 0) + the mode variant. The variant numbers are
// the ones integer720 / integer1080 already use, so x5 is 1 and x4 is 2 in 720p; in 1080p integer1080 is 3 for x4 and 4 for x5,
// mapped to 2 and 1 here so the two halves of the table line up. Returns 16 (a scratch slot that is never saved) when no
// integer mode is on screen, so a caller never has to check first.
uint8_t memSettingIndex()
{
    const bool pal = (rto->presetID == 0x13 || rto->presetID == 0x15);
    if (rto->integer1080Active && (rto->presetID == 0x05 || rto->presetID == 0x15)) {
        const uint8_t variant = (uopt->integer1080 == 4) ? 1 : 2; // x5 -> 1, x4 -> 2
        return (uint8_t)((pal ? 8 : 0) + 4 + variant);
    }
    if (rto->integer720Active && (rto->presetID == 0x03 || rto->presetID == 0x13)) {
        uint8_t variant = 0; // x3
        if (uopt->integer720 == 2) variant = 1;      // x5
        else if (uopt->integer720 == 3) variant = 2; // x4
        else if (uopt->integer720 == 4) variant = 3; // x3 16:9
        return (uint8_t)((pal ? 8 : 0) + variant);
    }
    return 16;
}

// v88: the name of the mode a memory slot belongs to, for the log, so it is obvious which setting was just stored.
const char *memModeName()
{
    switch (memSettingIndex()) {
        case 0:  return "NTSC 720p x3";
        case 1:  return "NTSC 720p x5";
        case 2:  return "NTSC 720p x4";
        case 3:  return "NTSC 720p x3 16:9";
        case 5:  return "NTSC 1080p x5";
        case 6:  return "NTSC 1080p x4";
        case 8:  return "PAL 720p x3";
        case 9:  return "PAL 720p x5";
        case 10: return "PAL 720p x4";
        case 11: return "PAL 720p x3 16:9";
        case 13: return "PAL 1080p x5";
        case 14: return "PAL 1080p x4";
        default: return "no integer mode";
    }
}

// v88: write the clock and the phase chosen FOR THIS MODE. Both are "0 = leave what the preset loaded", so a mode never
// tuned is untouched. This replaces applyMemPhase(), which read one global byte and only ever ran in 720p x5.
// Returns true if anything was written, so the caller knows whether the SDRAM needs resetting.
// v90: what a mode needs when nothing has been set. These are the measurements, not guesses: PAL 720p x4 and
// PAL 1080p x4 clean only with the memory at 162 MHz (PLL_MS 3) and the phase at 15, and PAL 1080p x5 needing 185 MHz
// (PLL_MS 5), whose phase is not pinned down yet. 0 means "leave what the preset loaded".
// v92: and it is per SIZE STEP, not just per mode. In PAL 1080p x5 the square step is clean at 185 MHz with the
// phase the preset leaves (13), and the CRT step at HScale 874 clean only on the FEEDBACK clock with phase 12 — a different
// clock AND a different phase one step away. Since the steps are switched without reloading the preset, every step has to
// write its own pair: "leave what the preset loaded" only means anything immediately after a load.
static void memDefaultFor(uint8_t slot, uint8_t step, uint8_t *clock, uint8_t *phase)
{
    *clock = 0;
    *phase = 0;
    // v107n: DOLOR has one placement whatever the size step, measured at 129 MHz phase 13 in both modes
    if (slot < 16 && linesDolorSlot(slot)) {
        *clock = 7;
        *phase = 13;
        return;
    }
    switch (slot) {
        // (after v127f) NTSC 720p x5, SNES CRT only (the dump 23:44): 144 MHz, phase 11. The 256 keeps the preset's own.
        case 1:
            if (snesProfile() && !pceProfile() && rto->integer1080Centered != 2) { *clock = 4; *phase = 11; }
            break;
        // v100: PAL 720p x5 needs 185 MHz. Until now this slot was empty and the only code that raised the
        // clock in 720p x5 was gated on "!snesProfile()", so with the SNES it never ran and it had to be set by hand every time.
        // The phase is written explicitly as the preset's 13, because stepping back from another mode leaves whatever that
        // one set.
        case 9: *clock = 5; *phase = (activeConsoleProfile() == 1 && (step & 3) == 0) ? 8 : 13; break; // PAL 720p x5: 185 MHz, phase 13 (v107n: better than 12); (after v127z) Mega Drive 256 px: phase 8 (the dump of 2026-11-26 07:49)
        case 10: // PAL 720p x4, per step: the 256 px picture is clean on the preset's own 129 MHz, and the stretch
                 // breaking until the clock is walked up to 162. So the square keeps 129 and the wider steps get 162. The
                 // square has to write its 129 explicitly, not "leave what is there": coming back from a wider step, what
                 // is there is 162.
            // v102: the 320 step measured at HScale 576 and it needs no more memory than the 256 one: the preset's own
            // 129 MHz holds. The 162 MHz is only wanted if the picture is widened beyond that, so it waits in the two steps
            // past the 320 one rather than being forced on a step that does not need it.
            if ((step & 3) <= 1) { *clock = 7; *phase = 13; } // 256 and 320: 129 MHz, the preset's own pair
            else { *clock = 4; *phase = 15; }                 // the step that fills the screen: 144 MHz, measured
            break;
        // v96: back to the pair its own PAL repair of 01:51 was made under, now that the placement is the one being written.
        // The 129 MHz / phase 13 came with the NTSC transplant, which is no longer in use here.
        case 14:
            *clock = 3;
            *phase = ((step & 3) >= 2) ? 8 : 15; // PAL 1080p x4: 162 MHz; v107f: wide and fill measured clean at phase 8
            if (activeConsoleProfile() == 2 && (step & 3) == 1) { *clock = 4; *phase = 15; } // (after v127q) CRT 144 MHz phase 15 in 4:3 (dump 15:42) and in 16:9 (dump 15:56)
            // (after v127p) the dump of 2026-11-25 15:23: the SNES 256 px step runs at 144 MHz (PLL_MS 4), phase 15
            if (activeConsoleProfile() == 2 && (step & 3) == 0) { *clock = 4; *phase = 15; }
            break;
        // (after v127p) 2026-11-25: the SNES PAL 1080p x4 CRT step (the NTSC CRT carried over) needs 129 MHz
        // (clock 7), phase 15. Applied after the line above so it overrides it; the other steps and consoles keep theirs.

        case 13:                                 // PAL 1080p x5: measured step by step
            // v107q: the Mega Drive's CRT step (320 button) was measured clean at 144 MHz phase 8; the SNES pairs below
            // are the SNES's, whose path samples the line differently
            if (activeConsoleProfile() == 1 && (step & 3) <= 2) {
                // 320: 144 MHz phase 8; 256 (v107q, 21:25) and wide (21:30): 129 MHz phase 13
                *clock = ((step & 3) >= 1) ? 4 : 7; // (after v127x) wide too: 144 MHz (the dump of 23:07)
                if (tv43Active() && (step & 3) <= 1) *clock = 3; // (after v127z) the 4:3 320 px: 162 MHz (dump of 2026-11-26 08:02); the 256 px too ( of the same day)
                *phase = 13; // (after v127x) the 320 px dump of 2026-11-25 23:05: 144 MHz phase 13 (was 8)
                break;
            }
            switch (step & 3) {
                case 1: if (tv43Active()) { *clock = 8; *phase = 12; } else { *clock = 4; *phase = 12; } break; // CRT: (after v127q) 2026-11-25, 16:9 only: 144 MHz phase 12; the 4:3 (after v127q, the request) 108 MHz phase 12 (clock 8: 8 & 7 = PLL_MS 0, since 0 itself means leave)
                case 2: *clock = 2; *phase = 9; break;  // wide, HScale 765: feedback clock, phase 9 (v107m, the dump of 10:39)
                case 3: *clock = 7; *phase = 8; break;  // fill, HScale 680: 129 MHz, the hardest of the four to settle
                default: if (tv43Active()) { *clock = 3; *phase = 12; } else if (activeConsoleProfile() == 2) { *clock = 3; *phase = 13; } else { *clock = 7; *phase = 13; } break; // (after v127x) SNES 16:9: 162 MHz phase 13 (the SNES PAL dump of 23:20; was 129) square: (after v127q) 2026-11-25, 16:9 only: 129 MHz phase 13; the 4:3 (after v127q, the request) 185 MHz phase 12
            }
            break;
        default: break;
    }
}

// v90: what will actually be written for the mode on screen: the stored setting if there is one, otherwise the mode's default.
// v107m: which of the four step slots of a mode is in use. Only the PAL presets (slots 8..15) have size steps; the NTSC
// modes keep one setting, on slot 0, whatever step a PAL mode left in sizeStep.
static uint8_t memStepKey(uint8_t slot)
{
    if (slot < 8)
        return 0;
    const uint8_t st = uopt->sizeStep & 3;
    if (st > 1 && sizeStepsTwoMd())
        return 1; // v107q: the Mega Drive's 720p x4 has two steps only
    return (st == 3 && sizeStepsThree()) ? 2 : st; // v107q: the Mega Drive's 1080p x4 has no fill step
}

// (after v120c) 2026-11-21: "si tocas manualmente la ram en un perfil, que no altere a las otras
// consolas". The 16 x 4 table used to be one per mode and shared by every console, so a phase tuned on the PC Engine
// moved the Mega Drive's too. The table in uopt is now the ACTIVE console's working copy, kept in /memsetN.txt, one
// file per console profile slot, 128 hex characters (clock and phase, one digit each, for the 16 modes x 4 steps).
// A console with no file of its own starts from the table that was in preferences before this change, so nothing
// had already tuned is lost; from the first save on, each console goes its own way.
static uint8_t memLegacyClock[16][4];
// (after v127q) 2026-11-25: the «4:3» has its OWN memory settings, different values from the 16:9 for the same
// step (SNES PAL 1080p x5: 4:3 CRT 108 MHz phase 12, 16:9 CRT 144 MHz phase 12). Same slot / step indexes, a second table
// that is used while the 4:3 is on screen. It is saved after the 16:9 one in the same per-console file (an older file
// without that part reads as zeros = the mode's own defaults; an older firmware ignores the extra bytes).
static uint8_t memClock43[16][4];
static uint8_t memPhase43[16][4];
static inline uint8_t (*memClockTab())[4] { return tv43Active() ? memClock43 : uopt->memClockStep; }
static inline uint8_t (*memPhaseTab())[4] { return tv43Active() ? memPhase43 : uopt->memPhaseStep; }
static uint8_t memLegacyPhase[16][4];
static bool memLegacyKept = false;
static int16_t memLoadedConsole = -1;

static uint8_t memHexVal(int c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

static void memSettingsPath(char *out, uint8_t console)
{
    snprintf(out, 20, "/memset%u.txt", (unsigned)console);
}

void memSettingsEnsureLoaded()
{
    const uint8_t console = settingsProfile();
    if (memLoadedConsole == (int16_t)console)
        return;
    if (!memLegacyKept) {
        // The shared table as it stood before this change: the starting point for a console with no file yet. It is
        // frozen in /memsetbase.txt the first time, because uopt's copy now belongs to whichever console was last
        // active and saveUserPrefs() writes that one into the preferences: without freezing it, the starting point
        // would drift to the last console used.
        memLegacyKept = true;
        File b = LittleFS.open("/memsetbase.txt", "r");
        if (b) {
            for (uint8_t i = 0; i < 16; i++) {
                for (uint8_t k = 0; k < 4; k++) {
                    if (b.available() < 2) {
                        memLegacyClock[i][k] = 0;
                        memLegacyPhase[i][k] = 0;
                        continue;
                    }
                    memLegacyClock[i][k] = memHexVal(b.read()) & 7;
                    memLegacyPhase[i][k] = memHexVal(b.read()) & 0x0f;
                }
            }
            b.close();
        } else {
            memcpy(memLegacyClock, uopt->memClockStep, sizeof(memLegacyClock));
            memcpy(memLegacyPhase, uopt->memPhaseStep, sizeof(memLegacyPhase));
            File w = LittleFS.open("/memsetbase.txt", "w");
            if (w) {
                const char *hex = "0123456789abcdef";
                for (uint8_t i = 0; i < 16; i++) {
                    for (uint8_t k = 0; k < 4; k++) {
                        char d[2];
                        d[0] = hex[memLegacyClock[i][k] & 7];
                        d[1] = hex[memLegacyPhase[i][k] & 0x0f];
                        w.write((const uint8_t *)d, 2);
                    }
                }
                w.close();
            }
        }
    }
    memLoadedConsole = (int16_t)console;
    char path[20];
    memSettingsPath(path, console);
    File f = LittleFS.open(path, "r");
    if (!f) { // never tuned on this console: the old shared values
        memcpy(uopt->memClockStep, memLegacyClock, sizeof(memLegacyClock));
        memcpy(uopt->memPhaseStep, memLegacyPhase, sizeof(memLegacyPhase));
        memset(memClock43, 0, sizeof(memClock43));
        memset(memPhase43, 0, sizeof(memPhase43));
        return;
    }
    for (uint8_t i = 0; i < 16; i++) {
        for (uint8_t k = 0; k < 4; k++) {
            if (f.available() < 2) {
                uopt->memClockStep[i][k] = 0;
                uopt->memPhaseStep[i][k] = 0;
                continue;
            }
            uopt->memClockStep[i][k] = memHexVal(f.read()) & 0x0f;
            uopt->memPhaseStep[i][k] = memHexVal(f.read()) & 0x0f;
        }
    }
    for (uint8_t i = 0; i < 16; i++) { // the 4:3 part (after v127q)
        for (uint8_t k = 0; k < 4; k++) {
            if (f.available() < 2) {
                memClock43[i][k] = 0;
                memPhase43[i][k] = 0;
                continue;
            }
            memClock43[i][k] = memHexVal(f.read()) & 0x0f;
            memPhase43[i][k] = memHexVal(f.read()) & 0x0f;
        }
    }
    f.close();
}

void memSettingsSave()
{
    memSettingsEnsureLoaded();
    char path[20];
    memSettingsPath(path, settingsProfile());
    File f = LittleFS.open(path, "w");
    if (!f) {
        SerialM.println(F("[3x4x-v130k] memory settings: could not open the file"));
        return;
    }
    const char *hex = "0123456789abcdef";
    for (uint8_t i = 0; i < 16; i++) {
        for (uint8_t k = 0; k < 4; k++) {
            char d[2];
            d[0] = hex[uopt->memClockStep[i][k] & 0x0f];
            d[1] = hex[uopt->memPhaseStep[i][k] & 0x0f];
            f.write((const uint8_t *)d, 2);
        }
    }
    for (uint8_t i = 0; i < 16; i++) { // the 4:3 part (after v127q)
        for (uint8_t k = 0; k < 4; k++) {
            char d[2];
            d[0] = hex[memClock43[i][k] & 0x0f];
            d[1] = hex[memPhase43[i][k] & 0x0f];
            f.write((const uint8_t *)d, 2);
        }
    }
    f.close();
}

// "memoria de fábrica": this mode forgets what was stored and goes back to what the firmware carries
void memSettingsResetMode()
{
    typedef TV5725<GBS_ADDR> GBS;
    const uint8_t i = memSettingIndex();
    if (i >= 16) {
        SerialM.println(F("[3x4x-v130k] factory memory: not one of the modes with a setting of its own"));
        return;
    }
    memSettingsEnsureLoaded();
    const uint8_t k = memStepKey(i);
    memClockTab()[i][k] = 0;
    memPhaseTab()[i][k] = 0;
    memSettingsSave();
    uint8_t clock, phase;
    memDefaultFor(i, k, &clock, &phase);
    // (after v121) 2026-11-21: in NTSC 1080p x5 the button did nothing, because that mode has no pair of
    // its own in memDefaultFor (0 / 0 = "whatever the preset loads") and writing nothing left the 185 MHz in the chip.
    // With no pair of its own the only way back is to load the preset again, which is what carries those registers.
    if (!clock && !phase) {
        SerialM.println(F("[3x4x-v130k] factory memory: this mode has no pair of its own, loading the preset again"));
        applyPresets(rto->videoStandardInput);
        return;
    }
    if (clock)
        GBS::PLL_MS::write(clock & 7);
    if (phase) {
        GBS::MEM_INTER_DLYCELL_SEL::write((phase >> 0) & 1);
        GBS::MEM_CLK_DLYCELL_SEL::write((phase >> 1) & 1);
        GBS::MEM_FBK_CLK_DLYCELL_SEL::write((phase >> 2) & 1);
        GBS::MEM_PAD_CLK_INVERT::write((phase >> 3) & 1);
    }
    ResetSDRAM();
    scanlinesAfterSdramReset();
    SM_PRINTF("[3x4x-v130k] factory memory for %s: clock %u (%u MHz), phase %u/15\n", memModeName(), (unsigned)clock,
              (unsigned)memClockMhzFor(clock), (unsigned)phase);
}

uint8_t *memClockOwn()
{
    memSettingsEnsureLoaded();
    const uint8_t i = memSettingIndex();
    return (i < 16) ? &memClockTab()[i][memStepKey(i)] : nullptr;
}

uint8_t *memPhaseOwn()
{
    memSettingsEnsureLoaded();
    const uint8_t i = memSettingIndex();
    return (i < 16) ? &memPhaseTab()[i][memStepKey(i)] : nullptr;
}

// (after v130) 2026-11-28: RAM settings were not kept. Clock 0 (108 MHz) and phase 0 were
// stored as 0 = "no setting of its own", so choosing either of them was forgotten and the mode's default came back on the
// next preset load. Bit 3 of the stored clock digit (0x08) now means "this pair is stored, 0 included"; the digit has 4 bits
// in the file, so older files read exactly as before (flag clear = the old 0 = default rule).
static bool memLastExplicit = false;

void memEffective(uint8_t *clock, uint8_t *phase)
{
    memSettingsEnsureLoaded();
    *clock = 0;
    *phase = 0;
    memLastExplicit = false;
    const uint8_t i = memSettingIndex();
    if (i >= 16)
        return;
    uint8_t defClock, defPhase;
    memDefaultFor(i, memStepKey(i), &defClock, &defPhase); // v107q: the step as the mode counts it (no fill on the MD)
    // v107m: the user's choice for THIS STEP of the mode, otherwise the step's measured pair
    const uint8_t k = memStepKey(i);
    const uint8_t c = memClockTab()[i][k];
    const uint8_t ph = memPhaseTab()[i][k];
    if (c & 0x08) {
        memLastExplicit = true;
        *clock = c & 7;
        *phase = ph & 0x0f;
        return;
    }
    *clock = c ? c : defClock;
    *phase = ph ? ph : defPhase;
}

bool applyMemSettings()
{
    const uint8_t i = memSettingIndex();
    if (i >= 16)
        return false;
    uint8_t clock, phase;
    memEffective(&clock, &phase);
    const bool explicitPair = memLastExplicit;
    bool wrote = false;
    if (clock != 0 || explicitPair) {
        GBS::PLL_MS::write(clock & 7);
        wrote = true;
    }
    if (phase != 0 || explicitPair) {
        const uint8_t v = phase & 0x0f;
        GBS::MEM_INTER_DLYCELL_SEL::write((v >> 0) & 1);
        GBS::MEM_CLK_DLYCELL_SEL::write((v >> 1) & 1);
        GBS::MEM_FBK_CLK_DLYCELL_SEL::write((v >> 2) & 1);
        GBS::MEM_PAD_CLK_INVERT::write((v >> 3) & 1);
        wrote = true;
    }
    return wrote;
}

void applyMemPhase() // kept so the old callers keep working; now goes through the per-mode slot
{
    applyMemSettings();
}

uint16_t memClockMhzFor(uint8_t pllMs)
{
    switch (pllMs) {
        case 0: return 108;
        case 1: return 81;
        case 2: return 0; // feedback clock
        case 3: return 162;
        case 4: return 144;
        case 5: return 185;
        case 6: return 216;
        case 7: return 129;
        default: return 0;
    }
}

bool integerWidthButtonIsFill()
{
    return snesProfile() && !pceProfile();
}

void integer720ReapplyTick()
{
    if (rto->integer720ReapplyAt == 0 || (long)(millis() - rto->integer720ReapplyAt) < 0)
        return;
    rto->integer720ReapplyAt = 0;
    if (rto->outModeHdBypass || rto->isCustomPreset)
        return;
    if (rto->dcVgaActive) { // (after v108b) the Dreamcast through VGA: its placement again, once the chip has settled
        dcVga1080Apply();
        return;
    }
    // v88: this tick now serves the 1080p modes too. Their placement is written during the load, but the SDRAM clock and phase
    // the saved ones were not, so the tuning was lost on every load. Writing memory settings needs the chip settled, which is
    // exactly what this deadline is for.
    if (rto->integer1080Active && (rto->presetID == 0x05 || rto->presetID == 0x15)) {
        // v93: and then PLACE IT AGAIN. This is the hole that made every 1080p PAL mode drift while the 720p ones held: the
        // 720p branch below has always re-written its placement here, 700 ms after the load, once the chip had settled. The
        // 1080p branch applied the memory and returned, so the only placement it ever got was the one written mid-load,
        // underneath a ResetSDRAM and a phase change. integer1080Apply() now does memory first and windows last, so calling
        // it here leaves the picture as the final word.
        integer1080Apply(rto->integer1080Centered == 2 ? 256 : 320);
        // v106: this used to narrate the SDRAM clock and phase of the mode on every load. The user does not need to know
        // that one mode runs at 162 MHz and another at 185: if a mode glitches the button changes it, and that
        // belongs in the documentation, not in the log. It still happens, it just stops announcing itself.
        return;
    }
    if (!rto->integer720Active || (rto->presetID != 0x03 && rto->presetID != 0x13))
        return; // v78: 0x13 = the PAL 720p preset
    // v93: the memory block below used to run AFTER this placement, so in 720p x5 the picture was written and then had its
    // clock and phase changed and the SDRAM reset underneath it. Same bug as 1080p, smaller blast radius because only the
    // x5 modes of the Mega Drive and PC Engine reach it. Memory first, placement last, everywhere.
    // v76: the x5 modes need more SDRAM bandwidth than any other. This was gated on !snesProfile(), so the PC Engine (which
    // shares that profile family) never got it and glitched in x5 until the clock was raised by hand. It now covers both,
    // and the PC Engine gets 185 MHz (PLL_MS 5) rather than 129, because its x5 draws more pixels per line.
    // 185 is a mild overclock of a 166 MHz part: right for this board, but another board's memory may not take it. The
    // "cycle ram" button still lowers it, and this only runs on an x5 load.
    if (uopt->integer720 == 2 && (!snesProfile() || pceProfile())) {
        // v78: the SNES does NOT get a bump. v77 raised it to 185 MHz believing its x5 320 px mode needed it, but the
        // then ran that same mode at the default 108 MHz with no glitches: what fixed it was the border hiding the garbage
        // past the end of the valid line data, not the clock. The SNES had never had a bump before v77 and does not need one.
        // The value is a preference: 0 means "use the default for this profile", anything else is what the user chose with the
        // "cycle ram" button while in x5, so a board whose memory will not take 185 keeps its own setting across preset loads.
        const uint8_t def = pceProfile() ? 5 : 7; // 5 = 185 MHz (PC Engine), 7 = 129 MHz (Mega Drive, measured in v56)
        // v88: the per-mode slot wins over the old single byte, which in turn wins over the profile default
        const uint8_t *ownp = memClockOwn(); // v107m: per mode and step
        const uint8_t own = ownp ? (uint8_t)(*ownp & 7) : 0; // (after v127z) bit 3 = flag
        const uint8_t ms = own ? own : (uopt->x5MemClock ? uopt->x5MemClock : def);
        GBS::PLL_MS::write(ms);
        applyMemSettings(); // v84 / v88: the phase of this mode (and its clock, already written above)
        ResetSDRAM();
        scanlinesAfterSdramReset(); // v92
        // v106: the x5 clock is applied silently now; it is only announced when changed by hand
    }
    // v93: and the placement last, so nothing moves it afterwards
    integer720Apply((rto->integer1080Centered == 2) ? 256 : 320);
}

// v43: console profile changed (button) or detected (field rate): place the picture again for the current width.
void integerProfileRefresh()
{
    if (rto->outModeHdBypass || rto->isCustomPreset)
        return;
    if (rto->integer1080Centered >= 3 && !pceProfile())
        rto->integer1080Centered = 1; // (after v124d) the 320 / 352 px whole-pixel widths are the PC Engine's only
    const uint16_t w = (rto->integer1080Centered == 2) ? 256 : 320;
    // v105: THE PAL PRESETS TOO. This listed only 0x05 and 0x03, so in PAL the picture was never placed again when the
    // console was finally identified. That matters because activeConsoleProfile() answers "Mega Drive" until the field rate
    // has been measured: on a cold boot the preset loads before that, the SNES branch is skipped, the old v42 estimate runs
    // instead, and with no refresh coming the picture simply stayed wrong. Reloading the preset by hand fixed it because by
    // then the console was detected. That is exactly the "something overwrites the states" problem, and the
    // reason it was not reproducible.
    if (rto->integer1080Active && (rto->presetID == 0x05 || rto->presetID == 0x15))
        integer1080Apply(w);
    else if (rto->integer720Active && (rto->presetID == 0x03 || rto->presetID == 0x13))
        integer720Apply(w);
    else if (!rto->dcVgaActive && dcVgaWanted()) {
        // (after v108e) THE DREAMCAST THROUGH VGA, 2026-11-17: switched on, neither 720p nor 1080p lit and the
        // picture sat on garbage. Rama's preset loads before the field rate has been measured, so at that moment the
        // console is not the Dreamcast yet and its placement is skipped; once Auto identifies it, it is written here, on
        // the preset already loaded (no reload), and the 700 ms pass writes it again.
        rto->dcVgaActive = true;
        dcVgaLoad();
        rto->integer720ReapplyAt = millis() + 700;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// v107n: the "Líneas" row of the Modo tab (224 / 240 or 239 / DOLOR). See uopt->linesMode.
// Where it exists: the PAL presets, with the Mega Drive or the SNES profile, in 720p x3 (4:3 slot 8, 16:9 slot 11) and in
// 1080p x4 (slot 14): the only modes with room for the 240 lines (720 = 240 x 3, 1080 > 240 x 4). DOLOR only where
// it was measured: the Mega Drive in 720p x3 16:9 and in 1080p x4. Its values depend on how the ADC samples the line, which
// is not the same in the SNES path, so the SNES gets 224 / 239 until it has a DOLOR of its own.
static bool linesSlotHasRow(uint8_t slot)
{
    if (rto->presetID != 0x13 && rto->presetID != 0x15)
        return false;
    if (dcSignal() >= 1 && dcSignal() <= 4)
        return false; // (after v107z) the Dreamcast's modes show all its lines, measured: no 240 / DOLOR row
    const uint8_t prof = activeConsoleProfile();
    if (prof != 1 && prof != 2)
        return false;
    return slot == 8 || slot == 11 || slot == 14;
}

// v107p: the SNES has no 224 button. Its games draw no frame round the picture (outside the game window everything is
// black), so nothing needs hiding top or bottom: the window always stays open to the 239 lines, and its row is only
// "239" and DOLOR.
static bool linesSlotSnesOpen(uint8_t slot)
{
    return linesSlotHasRow(slot) && activeConsoleProfile() == 2;
}

static bool linesSlotHasDolor(uint8_t slot)
{
    // v107o: 720p x3 4:3 (slot 8) measured too.
    // v107p: and the SNES in the two 720p x3 modes, with the Mega Drive's values. In PAL 720p the placement does not
    // depend on the console (the 0x13 block runs before any SNES branch, on the same preset and the same ADC clock), and
    // DOLOR shows the whole PAL frame, so the same window should fit; to be confirmed on the SNES. 1080p x4 is not
    // offered: there the SNES runs its own path with the ADC at 80 %, which changes every horizontal value.
    if (!linesSlotHasRow(slot))
        return false;
    if (activeConsoleProfile() == 1)
        return slot == 8 || slot == 11 || slot == 14;
    return slot == 8 || slot == 11;
}

// (after v107q) 2026-11-13: the row is only 240 and DOLOR. 240 is always the starting point: it loads
// the window the firmware has for the mode, open to the 240 lines, and on top of it each player puts or takes away the
// edge chosen, which is kept for that mode (see the user's edges below). DOLOR is a joke: it can be switched on, but it
// is never remembered. It lives here, in RAM, for the slot it was switched on in; the next mode load (any mode button)
// or a power cycle brings back 240. uopt->linesMode is no longer read or written (kept in the file for compatibility).
static uint8_t linesDolorRt = 0xFF;

void linesSetDolor(uint8_t slot)
{
    linesDolorRt = slot;
}

bool linesDolorSlot(uint8_t slot)
{
    return slot < 16 && linesSlotHasDolor(slot) && linesDolorRt == slot;
}

// what the row is set to for the mode on screen: 0 when the mode has no row, 1 = 240 (239 with the SNES), 2 = DOLOR
uint8_t linesModeEffective()
{
    const uint8_t slot = memSettingIndex();
    if (slot >= 16 || !linesSlotHasRow(slot))
        return 0;
    return linesDolorSlot(slot) ? 2 : 1;
}

// for the web: bit 0 = the row is shown, bit 1 = DOLOR is offered, bit 2 = no 224 button (SNES: 239 is its floor)
uint8_t linesAvailability()
{
    const uint8_t slot = memSettingIndex();
    if (slot >= 16 || !linesSlotHasRow(slot))
        return 0;
    // (after v127u) 2026-11-25: the SNES in 1080p x4 has no «Líneas» row (it is the only way to present its
    // games, a button is not needed). Only the row goes: the mode keeps working as it did (the 239 window of linesModeEffective,
    // and the edges kept under that key), so the picture placed by hand does not move.
    if (slot == 14 && activeConsoleProfile() == 2)
        return 0;
    return (uint8_t)(1 | (linesSlotHasDolor(slot) ? 2 : 0) | (linesSlotSnesOpen(slot) ? 4 : 0));
}

bool linesDolorActive()
{
    return linesModeEffective() == 2;
}

// written AFTER the mode has placed the picture, so it only changes what the row is about
static void applyLinesOverride()
{
    const uint8_t slot = memSettingIndex();
    const uint8_t mode = linesModeEffective();
    if (mode == 2) {
        if (slot == 8) {
            // v107o: the DOLOR for 720p x3 4:3 (2026-11-12 17:08, the same Mega Drive): the PAL frame fills the 4:3 width at
            // HScale 576 (the 16:9 one, 768, times 0.75), the same squash (819) and open window, capture from line 10
            // (after v127q) THE SNES DOLOR positioned and with the edges, dump of 2026-11-25 15:52: DIS HB 2014/340 -> 1970/392
            // and HBIN 52 -> 64 (the rest as it was: HB 1949/125, DIS VB 748/26, IF VB 10/12). The Mega Drive keeps the old one.
            if (activeConsoleProfile() == 2)
                write720(Mode720{576, 819, 1949, 125, 1970, 392, 748, 26, 10, 12, 64});
            else
                // (after v127u) 2026-11-25: the Mega Drive DOLOR five fine touches to the left: HB 1949/125 -> 1944/120
                write720(Mode720{576, 819, 1944, 120, 2014, 340, 748, 26, 10, 12, 52});
        } else if (slot == 11) {
            // the DOLOR of 2026-11-12 15:17 (Japanese Mega Drive at 50 Hz): the CRT width, the picture squashed to 2.5 lines
            // per line (288 lines in 720), the vertical window open and the capture starting at line 11 for the top band
            // (after v127u) the dump of 2026-11-25 21:17 (Mega Drive PAL, 720p x3 16:9): everything as it was except HBIN 126 -> 146.
            // The Mega Drive only; the SNES keeps 126.
            write720(Mode720{768, 819, 1738, 429, 1816, 540, 745, 25, 11, 13, (uint16_t)(activeConsoleProfile() == 1 ? 146 : 126)});
        } else if (slot == 14) {
            // the DOLOR of 2026-11-12 15:49: 3.75 lines per line (288 in 1080). The memory window goes to 70 / 72: at the
            // 80 / 82 it fell inside the picture (which starts at 76) and drew a black line across the top band.
            GBS::VDS_HSCALE::write(1023);
            GBS::VDS_HSCALE_BYPS::write(0);
            GBS::VDS_VSCALE::write(546);
            GBS::VDS_VSCALE_BYPS::write(0);
            GBS::VDS_HB_ST::write(1198);
            GBS::VDS_HB_SP::write(164);
            GBS::VDS_DIS_HB_ST::write(1203);
            GBS::VDS_DIS_HB_SP::write(289);
            GBS::VDS_DIS_VB_ST::write(1102);
            GBS::VDS_DIS_VB_SP::write(76);
            GBS::VDS_VB_ST::write(70);
            GBS::VDS_VB_SP::write(72);
            GBS::IF_VB_ST::write(43);
            GBS::IF_VB_SP::write(45);
            GBS::IF_HBIN_ST::write(0);
            GBS::IF_HBIN_SP::write(80);
        }
        // DOLOR: no scanlines (they cannot line up with 2.5 or 3.75 lines per line). The preference is left alone, so they
        // come back by themselves on leaving DOLOR; enableScanlines() refuses while DOLOR is on.
        disableScanlines();
        SerialM.println(F("[3x4x-v130k] lines: DOLOR"));
        return;
    }
    if (mode == 1) {
        // 240 (Mega Drive) / 239 (SNES): open the vertical window round the centre of the one the mode placed, to the
        // height of 240 lines plus one output line of slack each side. Nothing else moves: the size step keeps its width.
        const uint16_t k = (slot == 14) ? 4 : 3;
        const uint16_t vt = GBS::VDS_VSYNC_RST::read();
        const uint16_t st = GBS::VDS_DIS_VB_ST::read();
        const uint16_t sp = GBS::VDS_DIS_VB_SP::read();
        const int16_t centre = (int16_t)((st + sp) / 2);
        const int16_t half = (int16_t)(120 * k + 1);
        int16_t nsp = centre - half;
        int16_t nst = centre + half;
        if (nsp < 8) nsp = 8;              // past the vertical sync (1..6)
        if (nst > (int16_t)vt - 1) nst = (int16_t)vt - 1;
        GBS::VDS_DIS_VB_SP::write((uint16_t)nsp);
        GBS::VDS_DIS_VB_ST::write((uint16_t)nst);
        // the memory window must not fall inside what is now visible (that is the black line seen in DOLOR)
        const uint16_t mst = GBS::VDS_VB_ST::read();
        const uint16_t msp = GBS::VDS_VB_SP::read();
        if (msp >= (uint16_t)nsp && mst <= (uint16_t)nst && nsp > 8) {
            GBS::VDS_VB_ST::write((uint16_t)(nsp - 6));
            GBS::VDS_VB_SP::write((uint16_t)(nsp - 4));
        }
        SerialM.print(F("[3x4x-v130k] lines: 240, window "));
        SerialM.print(nst);
        SerialM.print('/');
        SerialM.println(nsp);
    }
    // 224 or 240: the scanlines as the user has them
    if (uopt->wantScanlines && !rto->scanlinesEnabled && !rto->motionAdaptiveDeinterlaceActive)
        enableScanlines();
}

// v107n: every placement of these modes goes through here, whatever path it takes inside (step change, preset load, the
// 700 ms second pass, profile change), so the "Líneas" row is applied on top of all of them.
// v107q: the Mega Drive in PAL 1080p x4, measured for the first time on a real PAL Mega Drive (2026-11-12 20:06, 320 px
// grid, CRT step). Until now this console ran the v42 "round 1" estimate block in PAL 1080p. At HScale 1023 the 320 px
// grid measured 1270 px (3.97 columns); 1023 x 1270 / 1280 = 1015, and at 1015 (the dump of 20:12) every cell of the
// grid is 64 px exactly: 4.00 columns, whole pixels, the grid at 319..1600 (centre 959.5 of 1920), all 240 lines.
// His vertical window (1062 / 103) shows the 240 lines; the 224 one here is the same centre with 224 x 4 lines, so the
// "Líneas" row opens it to the 240 by itself.
// Square step (256 button), the dump of 20:22 with the 256 px grid: the same HScale 1015, because the Mega Drive draws
// its 256 and 320 px modes over the same stretch of the line (10 master clocks per pixel against 8), so a 256 px game
// comes out at 5.00 columns x 4 lines: whole pixels, 1.25 wide, near the 1.14 a CRT gave it. Every cell measured 80 px.
// Wide step, the dump of 20:26 (320 px grid): HScale 846 = 4.8 columns, 1536 px, centred at 961. HB 58 / 74 are hand-set:
// the memory window wraps round the line at this width, and the capture showed no black column from it.
// v107q: the Mega Drive in PAL 1080p x4 has three size steps, not four. Its line enters with the ADC at full speed (the
// SNES runs this mode at 80 %), so stretched to 1920 px it no longer fits in the chip's line memory: no
// clean "llenar" came out with any memory setting. So there is no fill step for it; a step 4 left over from another console loads
// the wide one, and the web hides the button.
// v107q: the Mega Drive in PAL 720p x4 has two size steps only, 256 (HScale 666) and 320 (HScale 533, 1280 px edge to
// edge). 2026-11-12: no wider modes for this preset. A step 2 or 3 left over from another console loads
// the 320 one.
bool sizeStepsTwoMd()
{
    // (after v127p) 2026-11-25: the SNES in PAL 720p x4 has no «ancho» / «llenar» either: 256 and 320 only
    // (profile 1 = Mega Drive, profile 2 = SNES / NES)
    return (activeConsoleProfile() == 1 || activeConsoleProfile() == 2) && rto->presetID == 0x13 && rto->integer720Active &&
           uopt->integer720 == 3;
}

bool sizeStepsThree()
{
    // v107q: and 1080p x5 too, no fill step there either. And 720p x3 16:9, where the wide step
    // wide enough.
    if (activeConsoleProfile() != 1)
        return false;
    if (rto->presetID == 0x15 && rto->integer1080Active && (uopt->integer1080 == 3 || uopt->integer1080 == 4))
        return true;
    return rto->presetID == 0x13 && rto->integer720Active && uopt->integer720 == 4;
}

static void mdPal1080x5Measured()
{
    // v107q: the Mega Drive in PAL 1080p x5, CRT step (320 button), the dump of 2026-11-12 21:05 with the 320 px grid:
    // HScale 812 = 5.00 columns (1015 x 4 / 5), 1600 px, every cell 80 px, centred at 959; VScale 409, 216 lines shown,
    // centred. Memory 144 MHz phase 8 (see memDefaultFor).
    // Square step (256 button), the dump of 21:25 with the 256 px grid: HScale 1015 = 5.00 columns, 1280 px, every cell
    // 80 x 80 (5 x 5, whole and square), centred at 959; memory 129 MHz phase 13.
    // Wide step, the dump of 21:30 (320 px grid): HScale 738 = 5.5 columns, 1760 px, centred at 959, clean; the memory
    // window wraps round the line (1332 / 1365), memory 129 MHz phase 13. Fill is not measured (and may not fit).
    const uint8_t st = (uopt->sizeStep > 2) ? 2 : uopt->sizeStep; // no fill step (sizeStepsThree)
    const bool sq = (st == 0);
    //                         square  CRT   wide
    static const uint16_t hs[]  = {1015, 812,  738};
    static const uint16_t hbs[] = {1174, 1280, 1332};
    static const uint16_t hbp[] = {145,  116,  1365};
    static const uint16_t dhs[] = {1174, 1280, 1332};
    // (after v127x) THE 256 px (square step) of 2026-11-25 23:03, Mega Drive PAL 1080p x5, cells 80 x 80: HScale 1015, HB 1174/145, DIS HB ST
    // 1174, DIS VB 1125/42, VB 30/32, IF VB 90/92, VScale 409 stand; DIS HB SP 331 -> 317, HBIN 58 -> 74.
    // (after v127x) THE 320 px (CRT step) of 2026-11-25 23:05, cells 80 x 80, memory changed to 144 MHz phase 13: HScale 812, HB 1280/116,
    // DIS HB ST 1280, VB 30/32, IF VB 90/92 stand; DIS HB SP 225 -> 212, HBIN 214 -> 226, DIS VB ST 1125 -> 1120 (SP 42).
    // (after v127x) the wide step too, the dump of 2026-11-25 23:07 (an experiment, but placed): HScale 738, HB 1332/1365,
    // DIS HB ST 1332, DIS VB 1125/42, IF VB 90/92 stand; DIS HB SP 172 -> 160, HBIN 12 -> 42; memory 144 MHz phase 13.
    static const uint16_t dhp[] = {317,  212,  160};
    static const uint16_t hbin[] = {74,  226,  42};
    static const uint16_t dvst[] = {1125, 1120, 1125};
    GBS::VDS_HSCALE::write(hs[st]);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(409);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(hbs[st]);
    GBS::VDS_HB_SP::write(hbp[st]);
    GBS::VDS_DIS_HB_ST::write(dhs[st]);
    GBS::VDS_DIS_HB_SP::write(dhp[st]);
    GBS::VDS_DIS_VB_ST::write(dvst[st]);
    GBS::VDS_DIS_VB_SP::write(42);
    GBS::VDS_VB_ST::write(30);
    GBS::VDS_VB_SP::write(32);
    GBS::IF_VB_ST::write(90);
    GBS::IF_VB_SP::write(92);
    GBS::IF_HBIN_ST::write(0);
    GBS::IF_HBIN_SP::write(hbin[st]);
    SerialM.print(F("[3x4x-v130k] 1080p x5 PAL | Mega Drive | step "));
    SerialM.print(st + 1);
    SerialM.println(sq ? F(" (256): measured placement") : F(": measured placement"));
}

static void mdPal1080Measured()
{
    if (rto->presetID != 0x15)
        return;
    if (snesProfile() || pceProfile())
        return;
    if (uopt->integer1080 == 4) {
        mdPal1080x5Measured();
        return;
    }
    const uint8_t st = (uopt->sizeStep > 2) ? 2 : uopt->sizeStep; // no fill step here: see sizeStepsThree()
    const bool sq = (st == 0);
    //                          square  CRT   wide
    static const uint16_t hs[]   = {1015, 1015, 846};
    static const uint16_t hbs[]  = {1189, 1225, 58};
    static const uint16_t hbp[]  = {165,  165,  74};
    static const uint16_t dhs[]  = {1171, 1175, 1260};
    // (after v127x) THE 320 px (CRT step) of 2026-11-25 22:52, Mega Drive PAL 1080p x4 16:9, square cells 64 x 64 (4 x 4):
    // HScale 1015, HB 1225/165, DIS HB ST 1175, IF VB 59/61 and the rest as they were; DIS HB SP 331 -> 315, HBIN 80 -> 96.
    // His vertical 1063/101 is the 240 window of the same table (1031/133 is the 224 one, +-32): nothing to change there.
    static const uint16_t dhp[]  = {327,  315,  248};
    static const uint16_t hbin[] = {104,  96,   104};
    GBS::VDS_HSCALE::write(hs[st]);
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE::write(512);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HB_ST::write(hbs[st]);
    GBS::VDS_HB_SP::write(hbp[st]);
    GBS::VDS_DIS_HB_ST::write(dhs[st]);
    GBS::VDS_DIS_HB_SP::write(dhp[st]);
    GBS::VDS_DIS_VB_ST::write(1031);
    GBS::VDS_DIS_VB_SP::write(133);
    GBS::VDS_VB_ST::write(80);
    GBS::VDS_VB_SP::write(82);
    GBS::IF_VB_ST::write(59);
    GBS::IF_VB_SP::write(61);
    GBS::IF_HBIN_ST::write(0);
    GBS::IF_HBIN_SP::write(hbin[st]);
    if (sq && uopt->integer1080Narrow) {
        // (after v108h)  of 2026-11-17: at the normal clock the 256 px step came out exactly as wide as the
        // 320 px one (a 256 px game at 5 columns x 4 lines, 1280 px). At the 80 % clock it is 4 x 4, 1024 px, whole
        // pixels. PROVISIONAL, TO BE MEASURED: the same scale (4.00 columns once the clock is at 80 %), the window drawn in
        // by the 256 px of picture it loses (~70 units a side) and the capture start scaled with the clock.
        // (after v110) THE STATE of 2026-11-17 14:26, ADC clock 1759 (1784 took the wifi down, 1750 rippled at the top
        // right): HScale 1012 = 1024 px exactly, 4.00 columns (1011 glitched), centre 958.5. The input line is the one
        // Developer's «PLL div» left (clock / 2 and + 65, not our recipe), the one the ripple went away with: HRST 879,
        // LINE 944, canvas end 828 / 104. Window 1079 / 412, HB 1273 / 321, capture start 197. Written tracked, so the
        // next placement takes them back like the recipe it replaces.
        INT1080_SET(GBS::IF_HSYNC_RST, hsyncRst, 879);
        INT1080_SET(GBS::IF_LINE_SP, lineSp, 944);
        INT1080_SET(GBS::IF_HB_ST2, ifHbSt2, 828);
        INT1080_SET(GBS::IF_HB_SP2, ifHbSp2, 104);
        GBS::VDS_HSCALE::write(1012);
        GBS::VDS_HB_ST::write(1273);
        GBS::VDS_HB_SP::write(321);
        GBS::VDS_DIS_HB_ST::write(1079);
        GBS::VDS_DIS_HB_SP::write(412);
        GBS::IF_HBIN_SP::write(197);
        SerialM.println(F("[3x4x-v130k] 1080p x4 PAL | Mega Drive | 256 px at ADC clock 1759 (4 x 4): measured placement"));
    }
    SerialM.print(F("[3x4x-v130k] 1080p x4 PAL | Mega Drive | step "));
    SerialM.print(st + 1);
    SerialM.println(sq ? F(" (256): measured placement") : F(": measured placement"));
}

// (after v107q) THE USER'S EDGES, KEPT PER MODE. 2026-11-13: every game draws its frame and its garbage
// in its own place (Sonic 2's is not Streets of Rage's), so no placement can hide it for all of them. Whatever is done
// with the edge buttons is kept, 15 s after the last press, for that mode alone, and comes back every time the mode is
// placed. "Mode" is everything that changes the window: the slot (resolution, factor, 4:3 / 16:9), the size step, the
// Líneas choice (224 / 240 / DOLOR) and the console profile (unknown, MD, SNES, PCE). 16 x 4 x 3 x 4 = 768 records of
// 4 x int16 (8 bytes) in /edges.bin, 6 KB at most; only the record in use is held in RAM, read and written with a seek.
// What is stored is the CHANGE the user made to the display window (VDS_DIS_HB_SP / _ST, VDS_DIS_VB_SP / _ST), counted
// press by press, not the registers: the placement keeps writing its own window and the user's change goes on top, so a
// later correction of a placement still reaches everybody who already saved edges.
static int16_t edgeLive[4] = {0, 0, 0, 0}; // left (DIS_HB_SP), right (DIS_HB_ST), top (DIS_VB_SP), bottom (DIS_VB_ST)
static int16_t edgeKey = -1;
static uint32_t edgeSaveAt = 0;
static const char *EDGE_FILE = "/edges.bin";

// His design of 2026-11-13 (first version; the 224 button was then removed, see edgesKeptHere): the 224 button IS the
// edge each user tailors. Most Mega Drive games need nothing, some sit on
// black, but those with coloured frames and garbage (Sonic 2) take the 224 button, and since every game may place its
// picture a little differently, the edge left there is the one kept. 240 and DOLOR mean "show everything": they
// load no stored edge, and what is touched in them is only for the moment, never saved. The SNES has no 224 (its floor
// is 239, the "224 / 239" button), so for it that button plays the 224 role. Modes without the Líneas row keep edges too.
// (after v107q, second decision of 2026-11-13) with 224 gone, that role is the 240's: edges are kept in every mode
// except DOLOR, which is never remembered either.
static bool edgesKeptHere(uint8_t slot)
{
    (void)slot;
    return linesModeEffective() != 2;
}

static int16_t edgeKeyNow()
{
    // a personal profile (custom preset) carries its own window and keeps it itself: what is done to it must never leak
    // into the edge kept for the mode, and the mode's edge must not be laid over it
    if (rto->isCustomPreset || rto->outModeHdBypass)
        return -1;
    const uint8_t slot = memSettingIndex();
    if (slot >= 16)
        return -1; // stock preset: nothing of ours is placed, nothing is kept
    if (!edgesKeptHere(slot))
        return -1; // 240 / DOLOR: everything visible, nothing kept
    // (after v107v) the shape on screen, for every mode. memStepKey() gives 0 for every NTSC slot (the RAM is not tuned per
    // step there), so in NTSC every width shared one edge: 2026-11-13, the PC Engine's 256 px edge came back
    // on its CRT button. In NTSC the width lives in integer1080Centered (2 = 256 px, 1 = 320 px) plus the fill / wide
    // states; in PAL in the size step.
    uint8_t step;
    if (slot < 8) {
        if (uopt->integer720Fill || uopt->integer1080Wide)
            step = 2;
        else
            step = (rto->integer1080Centered == 2) ? 0 : (rto->integer1080Centered == 3 ? 3 : 1); // (after v124d) 3 = PCE 320 px
    } else {
        step = memStepKey(slot);
    }
    uint8_t lines = linesModeEffective();
    if (lines > 2)
        lines = 0;
    // (after v107z) the Dreamcast has no Líneas row, so that field tells its progressive signals from the interlaced ones:
    // 240p and 480i (288p and 576i) share the mode slot but not the window
    if (dcSignal() == 3 || dcSignal() == 4)
        lines = 1;
    if (dcSignal() == 4 && uopt->integer1080Wide)
        lines = 2; // in PAL the step does not tell the two 576i shapes apart (in NTSC the wide state does)
    // (after v124d) the PC Engine's 352 px whole-pixel width (720p x3): a block of its own after the 4:3 one (max 6123)
    if (slot < 8 && rto->integer1080Centered == 4 && !uopt->integer720Fill && !uopt->integer1080Wide && !tv43Active())
        return (int16_t)(6200 + ((slot * 3) + lines) * CONSOLE_PROFILE_COUNT + settingsProfile());
    // (after v124e) and in the 1080p x4 «4:3» both of them, apart from the CRT / 256 px 4:3 records (max 8523)
    if (tv43Active() && (rto->integer1080Centered == 3 || rto->integer1080Centered == 4))
        return (int16_t)(6700 + (((slot * 2) + (rto->integer1080Centered == 4 ? 1 : 0)) * 3 + lines) * CONSOLE_PROFILE_COUNT + settingsProfile());
    // (after v108h) the Mega Drive's 1080p x4 «4:3» keeps its edges apart from the 16:9 ones, after everything else
    if (tv43Active()) {
        const uint8_t w256 = (rto->presetID == 0x15) ? (uopt->sizeStep == 0 ? 1 : 0) : (rto->integer1080Centered == 2 ? 1 : 0);
        // (after v123h) the console was missing here, so the Mega Drive and the PC Engine shared the 4:3
        // edges of 1080p x4. Now one record per console too, after the old 4200..4295 block (left unused).
        return (int16_t)(4300 + (((slot * 2) + w256) * 3 + lines) * CONSOLE_PROFILE_COUNT + settingsProfile());
    }
    const uint8_t prof = settingsProfile();
    if (prof < CONSOLE_PROFILE_FIRST_EMPTY)
        return (int16_t)((((slot * 4) + step) * 3 + lines) * 4 + prof);
    // (after v107v) the profiles without measurements go after the 768 records of the first four, so the edges already
    // kept by the Mega Drive, the SNES, the PC Engine and the unknown console stay where they are
    // (after v108e) «Otras» after all of them, so the 14 already there keep their records (the 14 is fixed for that reason)
    if (prof == CONSOLE_PROFILE_OTHERS)
        return (int16_t)(4000 + (((slot * 4) + step) * 3 + lines));
    return (int16_t)(768 + ((((slot * 4) + step) * 3 + lines) * 14 + (prof - CONSOLE_PROFILE_FIRST_EMPTY)));
}

// (after v123j) the file is a parameter: the picture position (/pos.bin) uses the same record layout and keys
static void edgeRead(int16_t key, int16_t *d, const char *file = EDGE_FILE)
{
    d[0] = d[1] = d[2] = d[3] = 0;
    File f = LittleFS.open(file, "r");
    if (!f)
        return;
    if (f.size() >= (size_t)(key + 1) * 8 && f.seek((size_t)key * 8))
        f.read((uint8_t *)d, 8);
    f.close();
}

static void edgeWrite(int16_t key, const int16_t *d, const char *file = EDGE_FILE)
{
    File f = LittleFS.open(file, LittleFS.exists(file) ? "r+" : "w+");
    if (!f) {
        SerialM.println(F("[3x4x-v130k] borders: could not open the file"));
        return;
    }
    const size_t at = (size_t)key * 8;
    if (f.size() < at) { // grow the file with empty records up to this one
        f.seek(f.size());
        const uint8_t zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        while (f.size() < at)
            f.write(zero, (at - f.size()) < 8 ? (at - f.size()) : 8);
    }
    f.seek(at);
    f.write((const uint8_t *)d, 8);
    f.close();
}

static void edgesFlush()
{
    if (edgeSaveAt == 0 || edgeKey < 0) {
        edgeSaveAt = 0;
        return;
    }
    edgeSaveAt = 0;
    edgeWrite(edgeKey, edgeLive);
    SerialM.print(F("[3x4x-v130k] borders kept for this mode: left "));
    SerialM.print(edgeLive[0]);
    SerialM.print(F(", right "));
    SerialM.print(edgeLive[1]);
    SerialM.print(F(", top "));
    SerialM.print(edgeLive[2]);
    SerialM.print(F(", bottom "));
    SerialM.println(edgeLive[3]);
}

// (after v123j) of 2026-11-22: the picture position (Mover: ◀▶ = IF_HBIN_SP + IF_HSYNC_RST / IF_LINE_SP,
// or IF_HB_ST2 / SP2 outside 15 kHz; ▲▼ = IF_VB) is kept per mode like the borders: same key, 30 s after the last press,
// written at once on any placement and before a restart. Kept as differences from the firmware's placement (placeSnap),
// in /pos.bin, 4 x int16: HBIN_SP, HSYNC_RST (LINE_SP follows it), HB_ST2 (SP2 follows it), VB (ST and SP together).
static const char *POS_FILE = "/pos.bin";
static int16_t posLive[4] = {0, 0, 0, 0};
static int16_t posKey = -1;
static uint32_t posSaveAt = 0;
static void posFlush()
{
    if (posSaveAt == 0 || posKey < 0) {
        posSaveAt = 0;
        return;
    }
    posSaveAt = 0;
    edgeWrite(posKey, posLive, POS_FILE);
    SerialM.print(F("[3x4x-v130k] position kept for this mode: horizontal "));
    SerialM.print(posLive[0]);
    SerialM.print(F(", vertical "));
    SerialM.println(posLive[3]);
}

// called by every edge button after it has moved the window: which register, by how much (signed, in its own units)
void edgesNote(uint8_t which, int16_t amount)
{
    if (which > 3)
        return;
    const int16_t k = edgeKeyNow();
    if (k < 0) {
        // (after v107r) DOLOR or a personal profile: nothing is kept, but the change is counted, so "deshacer escala"
        // puts the window back with it and "deshacer borde" knows what to take away
        if (edgeKey < 0)
            edgeLive[which] += amount;
        return;
    }
    if (k != edgeKey) { // the mode changed without a placement in between: start from what that mode has stored
        edgesFlush();
        edgeKey = k;
        edgeRead(k, edgeLive);
    }
    edgeLive[which] += amount;
    edgeSaveAt = millis() + 30000; // (after v123j) 30 s, as the preferences (15 s before)
    if (edgeSaveAt == 0)
        edgeSaveAt = 1;
}

// "deshacer": forget the presses not yet kept, so the placement that follows brings back the last kept edges instead of
// first saving the very change the user wants to undo
void edgesDiscardPending()
{
    edgeSaveAt = 0;
    edgeKey = -1;
}

void edgesFlushNow() // (after v123j) before a restart: the borders and the position
{
    edgesFlush();
    posFlush();
}

void edgesTick()
{
    if (edgeSaveAt != 0 && (long)(millis() - edgeSaveAt) >= 0)
        edgesFlush();
    if (posSaveAt != 0 && (long)(millis() - posSaveAt) >= 0)
        posFlush();
}

void posDiscardPending() // (after v123j) the factory reset: nothing waiting comes back after the wipe
{
    posSaveAt = 0;
    posKey = -1;
}

// forget the stored edges of the mode on screen (the caller places the mode again to show its own window)
void edgesResetMode()
{
    const int16_t k = edgeKeyNow();
    if (k < 0)
        return;
    edgeSaveAt = 0;
    const int16_t zero[4] = {0, 0, 0, 0};
    edgeWrite(k, zero);
    edgeKey = -1;
    SerialM.println(F("[3x4x-v130k] borders of this mode: back to the mode's own"));
}

// (after v107r) THREE UNDO BUTTONS, 2026-11-13: "deshacer" can't send everything back, only what it
// names. Right after every placement, before the user's edge goes on, the firmware takes a photo of the three groups
// the pad touches; each button puts back its own group and leaves the other two alone.
//   move  : the input window (IF_VB, IF_HBIN, IF_HSYNC_RST / IF_LINE_SP, IF_HB_ST2 / SP2), what ▲▼◀▶ in Mover change
//   scale : VDS_HSCALE / VSCALE (and their bypass bits) and VDS_HB_ST / SP, what Escalar changes
//   edge  : the display window VDS_DIS_*, the firmware's own, without the user's edge on it
// The horizontal scale of the old code sometimes also moves VDS_DIS_HB_ST: undoing the scale writes the window back as
// the firmware's plus the user's edge, so the edge kept survives.
struct PlaceSnap {
    bool valid;
    uint16_t ifVbSp, ifVbSt, ifHbinSp, ifHsyncRst, ifLineSp, ifHbSt2, ifHbSp2;
    uint16_t hscale, vscale, hbSt, hbSp;
    uint8_t hscaleByps, vscaleByps;
    uint16_t disHbSp, disHbSt, disVbSp, disVbSt;
};
static PlaceSnap placeSnap = {};

// (after v123j) called after every Mover press (◀▶▲▼): the position now, as differences from the firmware's placement
void posNote()
{
    if (!placeSnap.valid)
        return;
    const int16_t k = edgeKeyNow();
    if (k < 0)
        return; // DOLOR or a personal profile: nothing is kept, as with the borders
    if (k != posKey) {
        posFlush();
        posKey = k;
    }
    posLive[0] = (int16_t)GBS::IF_HBIN_SP::read() - (int16_t)placeSnap.ifHbinSp;
    posLive[1] = (int16_t)GBS::IF_HSYNC_RST::read() - (int16_t)placeSnap.ifHsyncRst;
    posLive[2] = (int16_t)GBS::IF_HB_ST2::read() - (int16_t)placeSnap.ifHbSt2;
    posLive[3] = (int16_t)GBS::IF_VB_SP::read() - (int16_t)placeSnap.ifVbSp;
    posSaveAt = millis() + 30000;
    if (posSaveAt == 0)
        posSaveAt = 1;
}

// the stored position on top of the firmware's placement (the placement has just been written)
static void applyUserPosition()
{
    if (!posLive[0] && !posLive[1] && !posLive[2] && !posLive[3])
        return;
    GBS::IF_HBIN_SP::write((uint16_t)((int16_t)placeSnap.ifHbinSp + posLive[0]));
    GBS::IF_HSYNC_RST::write((uint16_t)((int16_t)placeSnap.ifHsyncRst + posLive[1]));
    GBS::IF_LINE_SP::write((uint16_t)((int16_t)placeSnap.ifLineSp + posLive[1]));
    GBS::IF_HB_ST2::write((uint16_t)((int16_t)placeSnap.ifHbSt2 + posLive[2]));
    GBS::IF_HB_SP2::write((uint16_t)((int16_t)placeSnap.ifHbSp2 + posLive[2]));
    GBS::IF_VB_SP::write((uint16_t)((int16_t)placeSnap.ifVbSp + posLive[3]));
    GBS::IF_VB_ST::write((uint16_t)((int16_t)placeSnap.ifVbSt + posLive[3]));
}

static void placeSnapTake()
{
    placeSnap.ifVbSp = GBS::IF_VB_SP::read();
    placeSnap.ifVbSt = GBS::IF_VB_ST::read();
    placeSnap.ifHbinSp = GBS::IF_HBIN_SP::read();
    placeSnap.ifHsyncRst = GBS::IF_HSYNC_RST::read();
    placeSnap.ifLineSp = GBS::IF_LINE_SP::read();
    placeSnap.ifHbSt2 = GBS::IF_HB_ST2::read();
    placeSnap.ifHbSp2 = GBS::IF_HB_SP2::read();
    placeSnap.hscale = GBS::VDS_HSCALE::read();
    placeSnap.vscale = GBS::VDS_VSCALE::read();
    placeSnap.hscaleByps = GBS::VDS_HSCALE_BYPS::read();
    placeSnap.vscaleByps = GBS::VDS_VSCALE_BYPS::read();
    placeSnap.hbSt = GBS::VDS_HB_ST::read();
    placeSnap.hbSp = GBS::VDS_HB_SP::read();
    placeSnap.disHbSp = GBS::VDS_DIS_HB_SP::read();
    placeSnap.disHbSt = GBS::VDS_DIS_HB_ST::read();
    placeSnap.disVbSp = GBS::VDS_DIS_VB_SP::read();
    placeSnap.disVbSt = GBS::VDS_DIS_VB_ST::read();
    placeSnap.valid = true;
}

// (after v107r, the decision) with a personal profile loaded the photo is taken from the profile itself, right after it
// loads, so the three buttons bring back what the profile has saved. Nothing is said about it: touching a firmware mode
// is how one leaves a profile, and that is evident.
static bool placeSnapUsable()
{
    if (!placeSnap.valid || rto->outModeHdBypass || (!rto->isCustomPreset && !rto->integer720Active && !rto->integer1080Active)) {
        SerialM.println(F("[3x4x-v130k] undo: only in the whole-pixel modes or a personal profile"));
        return false;
    }
    return true;
}

void placeSnapFromCustom()
{
    edgesFlush(); // a change still waiting belongs to the mode that was on screen before the profile
    posFlush();
    posKey = -1;
    posLive[0] = posLive[1] = posLive[2] = posLive[3] = 0;
    edgeKey = -1;
    edgeLive[0] = edgeLive[1] = edgeLive[2] = edgeLive[3] = 0;
    placeSnapTake();
}

// the firmware's window plus the user's edge (the same sum and limits as when the mode is placed)
static void writeWindowWithEdge(const int16_t *d)
{
    const int32_t htotal = GBS::VDS_HSYNC_RST::read();
    const int32_t vtotal = GBS::VDS_VSYNC_RST::read();
    int32_t hsp = (int32_t)placeSnap.disHbSp + d[0];
    int32_t hst = (int32_t)placeSnap.disHbSt + d[1];
    int32_t vsp = (int32_t)placeSnap.disVbSp + d[2];
    int32_t vst = (int32_t)placeSnap.disVbSt + d[3];
    if (hsp < 1) hsp = 1;
    if (hst > htotal - 1) hst = htotal - 1;
    if (hst < hsp + 17) hst = hsp + 17;
    if (vsp < 7) vsp = 7;
    if (vst > vtotal - 5) vst = vtotal - 5;
    if (vst < vsp + 9) vst = vsp + 9;
    GBS::VDS_DIS_HB_SP::write((uint16_t)hsp);
    GBS::VDS_DIS_HB_ST::write((uint16_t)hst);
    GBS::VDS_DIS_VB_SP::write((uint16_t)vsp);
    GBS::VDS_DIS_VB_ST::write((uint16_t)vst);
    keepHbStWithDisplayEdge();
}

void undoMove()
{
    if (!placeSnapUsable())
        return;
    GBS::IF_VB_SP::write(placeSnap.ifVbSp);
    GBS::IF_VB_ST::write(placeSnap.ifVbSt);
    GBS::IF_HBIN_SP::write(placeSnap.ifHbinSp);
    GBS::IF_HSYNC_RST::write(placeSnap.ifHsyncRst);
    GBS::IF_LINE_SP::write(placeSnap.ifLineSp);
    GBS::IF_HB_ST2::write(placeSnap.ifHbSt2);
    GBS::IF_HB_SP2::write(placeSnap.ifHbSp2);
    // (after v123j) and the kept position of this mode goes too, as «deshacer borde» does with the border
    posSaveAt = 0;
    posLive[0] = posLive[1] = posLive[2] = posLive[3] = 0;
    {
        const int16_t k = edgeKeyNow();
        if (k >= 0)
            edgeWrite(k, posLive, POS_FILE);
    }
    SerialM.println(F("[3x4x-v130k] undo move: the picture is back where the firmware placed it"));
}

void undoScale()
{
    if (!placeSnapUsable())
        return;
    GBS::VDS_HSCALE_BYPS::write(placeSnap.hscaleByps);
    GBS::VDS_HSCALE::write(placeSnap.hscale);
    GBS::VDS_VSCALE_BYPS::write(placeSnap.vscaleByps);
    GBS::VDS_VSCALE::write(placeSnap.vscale);
    GBS::VDS_HB_ST::write(placeSnap.hbSt);
    GBS::VDS_HB_SP::write(placeSnap.hbSp);
    writeWindowWithEdge(edgeLive); // the current edge (kept or still waiting) stays
    // (after v123l) the same break as «deshacer borde» (PCE 720p x3 4:3 320 px, HB_ST 2257 over DIS 2256 is
    // clean, the guard pulled it to 2256). With no border the window is the firmware's: its own HB_ST goes back.
    if (!edgeLive[0] && !edgeLive[1] && !edgeLive[2] && !edgeLive[3])
        GBS::VDS_HB_ST::write(placeSnap.hbSt);
    SerialM.println(F("[3x4x-v130k] undo scale: the firmware's scale, the border kept"));
}

void undoEdge()
{
    if (!placeSnapUsable())
        return;
    edgesResetMode(); // erases the kept edge of this mode (the web asks first)
    edgeLive[0] = edgeLive[1] = edgeLive[2] = edgeLive[3] = 0;
    const int16_t zero[4] = {0, 0, 0, 0};
    // (after v123l) PCE 720p x3 4:3 320 px comes with HB_ST 2257 over DIS_HB_ST 2256 and is clean; the guard in
    // writeWindowWithEdge pulled HB_ST down to 2256 and that corrupted it. Removing the border must not touch HB_ST, so
    // the value it had (the firmware's, or the one the scale left) goes back.
    // (if a border had pulled HB_ST down, the firmware's value is the right one while the scale is still the firmware's)
    const bool scaleAsFirmware = GBS::VDS_HSCALE::read() == placeSnap.hscale;
    const uint16_t hbStBefore = GBS::VDS_HB_ST::read();
    writeWindowWithEdge(zero);
    GBS::VDS_HB_ST::write(scaleAsFirmware ? placeSnap.hbSt : hbStBefore);
    SerialM.println(F("[3x4x-v130k] undo border: the firmware's border for this mode"));
}

// written last, after the placement and the Líneas row, so the user's change sits on top of the window the mode chose
static void applyUserEdges()
{
    placeSnapTake(); // (after v107r) the firmware's placement, before the user's edge, for the three undo buttons
    edgesFlush(); // a change still waiting belongs to the mode that was on screen: keep it before reading the new one
    posFlush();   // (after v123j) the same for the position
    posKey = edgeKeyNow();
    if (posKey < 0)
        posLive[0] = posLive[1] = posLive[2] = posLive[3] = 0;
    else
        edgeRead(posKey, posLive, POS_FILE);
    applyUserPosition();
    edgeKey = edgeKeyNow();
    if (edgeKey < 0) {
        edgeLive[0] = edgeLive[1] = edgeLive[2] = edgeLive[3] = 0;
        return;
    }
    edgeRead(edgeKey, edgeLive);
    if (!edgeLive[0] && !edgeLive[1] && !edgeLive[2] && !edgeLive[3])
        return;
    const int32_t htotal = GBS::VDS_HSYNC_RST::read();
    const int32_t vtotal = GBS::VDS_VSYNC_RST::read();
    int32_t hsp = (int32_t)GBS::VDS_DIS_HB_SP::read() + edgeLive[0];
    int32_t hst = (int32_t)GBS::VDS_DIS_HB_ST::read() + edgeLive[1];
    int32_t vsp = (int32_t)GBS::VDS_DIS_VB_SP::read() + edgeLive[2];
    int32_t vst = (int32_t)GBS::VDS_DIS_VB_ST::read() + edgeLive[3];
    // the same limits the buttons keep
    if (hsp < 1) hsp = 1;
    if (hst > htotal - 1) hst = htotal - 1;
    if (hst < hsp + 17) hst = hsp + 17;
    if (vsp < 7) vsp = 7;
    if (vst > vtotal - 5) vst = vtotal - 5;
    if (vst < vsp + 9) vst = vsp + 9;
    GBS::VDS_DIS_HB_SP::write((uint16_t)hsp);
    GBS::VDS_DIS_HB_ST::write((uint16_t)hst);
    GBS::VDS_DIS_VB_SP::write((uint16_t)vsp);
    GBS::VDS_DIS_VB_ST::write((uint16_t)vst);
    keepHbStWithDisplayEdge();
}

// (after v127i) PAL 50 Hz input with the NTSC preset forced: the PAL picture starts about 52 input lines later than the NTSC
// one (the positioned dump: IF VB 16/18 -> 68/70). The IF window moves down by that many lines, so the picture goes back up.
static void palForceShift()
{
    if (!rto->presetIsPalForce60)
        return;
    const uint16_t sp = GBS::IF_VB_SP::read() + 52;
    GBS::IF_VB_SP::write(sp);
    GBS::IF_VB_ST::write(sp - 2);
}

void integer1080Apply(uint16_t nativeWidth)
{
    // (after v107z) the Dreamcast: every state of it was measured from the 320 px placement, and in PAL from the second
    // size step (its memory slot); neither the 256 px button nor the size steps exist for it
    if (settingsProfile() == CONSOLE_PROFILE_DREAMCAST) {
        interlaceFresh();
        if (dcSignal() >= 1 && dcSignal() <= 4) {
            nativeWidth = 320;
            if (rto->presetID == 0x15)
                uopt->sizeStep = 1;
        }
    }
    // (after v111f) 2026-11-18: the Mega Drive has no «ancho» in 1080p x4 (the 4:3 button gives that
    // picture). A wide state left over from before (saved PAL step 3, NTSC wide flag) falls back to 320 px.
    if (md43Able()) {
        if (rto->presetID == 0x15 && uopt->sizeStep > 1)
            uopt->sizeStep = 1;
        if (rto->presetID == 0x05 && uopt->integer1080Wide && md43NoWide()) // (after v120c) no «llenar» where there is a 4:3
            uopt->integer1080Wide = 0;
    }
    // (after v108h) the 256 px <-> 320 px clock change of 1080p x4, in place (see integer1080ClockLive)
    {
        const bool want = narrowWanted(nativeWidth);
        if (want != (uopt->integer1080Narrow != 0))
            integer1080ClockLive(want);
    }
    // (after v108h) «4:3» was measured from the 320 px placement for both widths (the 256 px one at its own scale and
    // window on top), so that is the path it is placed from
    integer1080ApplyInner(tv43Active() ? 320 : nativeWidth);
    mdPal1080Measured(); // v107q
    md43Measured(nativeWidth); // (after v108h)
    applyLinesOverride();
    dc1080Measured(); // (after v107z) last of the placements, before the user's edges
    palForceShift(); // (after v127i)
    applyUserEdges(); // (after v107q)
}

void integer720Apply(uint16_t nativeWidth)
{
    if (settingsProfile() == CONSOLE_PROFILE_DREAMCAST) {
        interlaceFresh();
        if ((dcSignal() == 2 || dcSignal() == 4) && rto->presetID == 0x13)
            uopt->sizeStep = 1; // the 288p and 576i states were measured on the second step (its memory slot)
    }
    // (after v111g) 2026-11-18: the PC Engine has no «llenar» in 720p x3 16:9 (its 4:3 shape gives the
    // stretched picture), as the Mega Drive in 1080p x4. A fill state left over from before falls back to 320 px.
    if (pceProfile() && uopt->integer720 == 4 && uopt->integer720Fill)
        uopt->integer720Fill = 0;
    // (after v127r) 2026-11-25 (SNES 60 Hz, 720p x4): the mode came up labelled «320 px» (CRT) but with the 256 px
    // placement (HScale 601, IF VB 73/75), so the 256 button «did nothing» (it was already there) and the 320 one changed the
    // picture. The label reads rto->integer1080Centered; make it say what is being placed (1 = 320 px, 2 = 256 px; the PC
    // Engine's 3 / 4 stay as they are).
    if (rto->integer1080Centered == 1 || rto->integer1080Centered == 2)
        rto->integer1080Centered = (nativeWidth == 320) ? 1 : 2;
    integer720ApplyInner(nativeWidth);
    applyLinesOverride();
    dc720Measured(); // (after v107z)
    // (after v127i) PAL 50 Hz on the NTSC preset, SNES 720p x3 16:9: the two dumps of 2026-11-25 (12:24 as it came out, 12:17
    // positioned): IF VB 8/10 -> 62/64 and DIS VB 730/50 -> 744/28; everything else unchanged
    if (rto->presetIsPalForce60 && uopt->integer720 == 4 && activeConsoleProfile() == 2) {
        GBS::IF_VB_SP::write(64);
        GBS::IF_VB_ST::write(62);
        GBS::VDS_DIS_VB_ST::write(744);
        GBS::VDS_DIS_VB_SP::write(28);
    }
    applyUserEdges(); // (after v107q)
}
