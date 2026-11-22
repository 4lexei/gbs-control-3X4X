#include "UserCommandHandler.h"
#include "../core/Globals.h"
#include "../video/Integer1080.h"
#include "../web/SerialMirror.h"
#include "../config/WiFiConfig.h"
#include "../utils/Logging.h"
#include "../storage/PreferencesManager.h"
#include "../storage/SlotManager.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSVideoProcessing.h"
#include "../gbs/GBSPhase.h"
#include "../gbs/GBSSync.h"
#include "../video/Scanlines.h"
#include "../video/ImageTuning.h"
#include "../video/ColorPresets.h"
#include "../video/SyncPresets.h"
#include "../video/ConsoleModes.h"
#include "../video/SyncWatcher.h"
#include "../video/VideoScaling.h"
#include "../video/VideoShifting.h"
#include "../video/Deinterlacing.h"
#include "../video/VideoInput.h"
#include "../video/RegTest.h" // (after v111)
#include "../video/framesync.h"
#include "../core/slot.h"
#include "../gbs/tv5725.h"
#include <LittleFS.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

typedef TV5725<GBS_ADDR> GBS;

static void setFrameTimeLockMethod(uint8_t method)
{
    if (uopt->frameTimeLockMethod == method) {
        return;
    }
    if (!rto->extClockGenDetected) {
        FrameSync::reset(uopt->frameTimeLockMethod);
    }
    uopt->frameTimeLockMethod = method;
    saveUserPrefs();
    activeFrameTimeLockInitialSteps();
}

// [patch output-rate] 0 = follow source, 1 = 59.94 Hz, 2 = 60.00 Hz (needs the external clock generator)
static void setOutputRateMode(uint8_t mode)
{
    if (uopt->outputRateMode == mode) {
        return;
    }
    uopt->outputRateMode = mode;
    saveUserPrefs();
    if (!rto->extClockGenDetected) {
        SerialM.println(F("Output rate mode needs the external clock generator (Si5351), not detected"));
    } else if (mode == 0) {
        SerialM.println(F("Output rate: follow source"));
    } else if (mode == 1) {
        SerialM.println(F("Output rate: 59.94 Hz (50 Hz for PAL sources)"));
    } else if (mode == 3) { // (after v111b)
        SM_PRINTF("Output rate: exact, %lu.%04lu Hz (the source is followed when it is more than 1.5 Hz away)\n",
                       (unsigned long)(uopt->outputRateExact / 10000), (unsigned long)(uopt->outputRateExact % 10000));
    } else {
        SerialM.println(F("Output rate: 60.00 Hz (50 Hz for PAL sources)"));
    }
}

// [patch scanlines] Finer scanline intensity: steps of 0x08 over the same 0x00..0x50 range as before
// (0x00 = darkest lines, 0x50 = weakest). delta < 0 = stronger lines, delta > 0 = weaker. Wraps around.
// [patch perfect-scale] HScale for 320 / 256 pixel wide sources.
// The 720p presets define HScale 512 (NTSC) / 626 (PAL) as "whole input line across the screen".
// 480/512 of that fills a 4:3 window edge to edge for 320 px sources (whole pixels in a 1440p monitor chain),
// 514/512 of it gives whole pixels for 256 px sources with a frame. Measured on Sonic 2 (Mega Drive, NTSC, 720p).
void handleType2Command(char argument);

static bool integerRequest = false; // [patch 1080p-integer] set by '!' so that the shared 's' path keeps the variant

// [patch 1080p-integer] 1080p x4 / x5 buttons. mode: 3 = x4 (whole picture), 4 = x5 (fills the height, crops a few lines)
// (after v107q) set only while setLinesMode reloads the mode for DOLOR; any other mode load switches DOLOR off
static bool linesKeepDolor = false;

static void requestInteger1080(uint8_t mode)
{
    if (!linesKeepDolor)
        linesSetDolor(0xFF); // (after v107q) DOLOR is never remembered: a mode load brings back 240
    uopt->integer1080 = mode;
    // v57: with a SNES / NES (detected or chosen) the x4 loads straight in the 256 px mode (ADC clock at 80%: one load, no second reload)
    uopt->integer1080Narrow = (mode == 3 && integerIsSnesProfile()) ? 1 : 0;
    // (after v107z) the Dreamcast in 480i keeps the shape it was last used in («4:3» = integer1080Wide), as 720p x3 does
    if (!(mode == 3 && (dcSignal() == 3 || dcSignal() == 4)))
        uopt->integer1080Wide = 0;
    if (mode == 3 && dcSignal() == 5)
        uopt->integer1080Wide = 1; // (after v108b) the Dreamcast through VGA: «4:3» first, always (its 2 x 2 can take the
                                   // wifi down on some router channels: it is only ever chosen by hand, with the switch)
    if (mode == 3) {
        SerialM.println(F("\n1080p x4 16:9: for a 16:9 screen. The whole picture, four lines per game line. If you see "
                          "glitches or torn lines, step through Cycle SDRAM and SDRAM phase until they clear: the "
                          "right pair depends on the board."));
    } else {
        SerialM.println(F("\n1080p x5 16:9: for a 16:9 screen. Fills the height, a few lines cropped. Move the picture up and down to choose "
                          "which part you see. If you see glitches or torn lines, step through Cycle SDRAM and SDRAM phase until they clear: the right pair depends on the board."));
    }
    integerRequest = true;
    handleType2Command('s'); // reloads the 1920x1080 output
}

static bool integer720Request = false; // [patch 720p-x3] set by '^' so that the shared 'g' path keeps the mode
static bool integerLowRequest = false; // v62: same idea for the 960p x4 / 480p x2 buttons

// v62: 960p x4 (mode 1) and 480p x2 (mode 2): whole pixels in 4:3 on the low-resolution presets
static void requestIntegerLow(uint8_t mode)
{
    if (rto->videoStandardInput == 2 || rto->videoStandardInput == 4) {
        SerialM.println(F("960p x4 / 480p x2: NTSC sources only for now (PAL loads the normal preset)"));
    } else if (mode == 1) {
        SerialM.println(F("960p x4: 4 x 4 whole pixels, 4:3 (320 px games fill the 1280x960; 256 px = 1024 px wide)"));
    } else {
        SerialM.println(F("480p x2: 2 x 2 whole pixels (320 px games = 640 px of the 720; the 256 px button reloads with the ADC clock at 80%)"));
    }
    uopt->integerLow = mode;
    uopt->integer720 = 0;
    uopt->integer1080 = 0;
    uopt->integer720Fill = 0;
    uopt->integer1080Wide = 0;
    uopt->integer1080Narrow = 0;
    integerLowRequest = true;
    handleType2Command(mode == 1 ? 'f' : 'h');
}

// v47: 720p x3 / x5 buttons (NTSC). mode 1 = x3 (his presets: 4 columns for 256 px, 3 lines per line, 224 lines),
// mode 2 = x5 (5 columns, 5 lines per line, crops to 144 lines: the whole Super Game Boy screen at 800 x 720)
static void requestInteger720(uint8_t mode)
{
    if (!linesKeepDolor)
        linesSetDolor(0xFF); // (after v107q) DOLOR is never remembered: a mode load brings back 240
    // v106: one line per mode, written for whoever is using the firmware rather than for us. Every one says which screen
    // it is for, how much of the picture is shown, and, where the mode crops, that the picture can be moved to choose
    // what is on screen. The old text named registers, called itself "v54 attempt", and still claimed PAL had only the
    // x3 measured, which stopped being true two days ago.
    if (mode == 4) {
        SerialM.println(F("\n720p x3 16:9: for a 16:9 screen, without switching it to 4:3. The whole picture, whole pixels, "
                          "with a black border at each side."));
    } else if (mode == 3) {
        SerialM.println(F("\n720p x4 16:9: for a 16:9 screen. Ideal for the Super Game Boy. A 4:3-shaped zoom: 180 of the "
                          "224 lines, the top and bottom "
                          "are cropped. Move the picture up and down to choose which part you see. "
                          "If you see glitches or torn lines, step through Cycle SDRAM and SDRAM phase until they clear: the right pair depends on the board."));
    } else if (mode == 2) {
        SerialM.println(F("\n720p x5 16:9: for a 16:9 screen. The Game Boy screen on its own, filling the height: its 144 "
                          "lines, with the Super Game Boy border left outside. Move the picture up and down to line "
                          "it up. If you see glitches or torn lines, step through Cycle SDRAM and SDRAM phase until "
                          "they clear: the right pair depends on the board."));
    } else {
        SerialM.println(F("\n720p x3 4:3: for a screen or window set to 4:3. The whole picture, whole pixels at 256 px. "
                          "The width buttons and fill step through the sizes."));
    }
    // v107o: remember the shape 720p x3 is used in, for the mode button (web code 53)
    if (mode == 1 || mode == 4) {
        const uint8_t wide = (mode == 4) ? 1 : 0;
        if (uopt->x3Wide != wide) {
            uopt->x3Wide = wide;
            saveUserPrefs();
        }
    }
    uopt->integer720 = mode;
    uopt->integer720Fill = 0;
    uopt->integer1080Wide = 0;
    uopt->integerLow = 0;
    uopt->integer1080Narrow = 0;
    integer720Request = true;
    handleType2Command('g'); // reloads the 1280x720 output
}

static void setPerfectHScale(uint16_t nativeWidth);

// v49: IF HBIN SP -/+ (web buttons): how much of each input line the memory takes in. Step 2 (1 with "fine scale" on).
static void adjustIfHbin(bool up)
{
    const uint16_t step = scaleHorizontalFine ? 1 : 2;
    uint16_t v = GBS::IF_HBIN_SP::read();
    if (up) {
        v = (v + step > 0x1ff) ? 0x1ff : v + step;
    } else {
        v = (v < step + 8) ? 8 : v - step;
    }
    GBS::IF_HBIN_SP::write(v);
    SerialM.print(F("IF HBIN SP: "));
    SerialM.println(v);
}

static void setPerfectHScale(uint16_t nativeWidth)
{
    // v86: in PAL the two width buttons are shortcuts into the step cycle: 256 px = square, 320 px = CRT shape. They do not
    // mean a different source width there, because the SNES only draws 256.
    if (rto->presetID == 0x13 || rto->presetID == 0x15) {
        uopt->sizeStep = (nativeWidth == 320) ? 1 : 0;
        saveUserPrefs();
        if (rto->presetID == 0x13)
            integer720Apply(256);
        else
            integer1080Apply(nativeWidth);
        rto->integer720ReapplyAt = millis() + 700; // v107m: the second pass, as after a load (see cycleFill)
        return;
    }
    const bool wasWide = (uopt->integer1080Wide != 0);
    uopt->integer1080Wide = 0; // v58: the 256 / 320 buttons leave the wide state
    if (rto->integerLowActive) {
        // v62: in 480p x2 a 256 px picture at 2 whole columns needs the ADC clock at 80%, so that one reloads the preset
        const bool x2 = (uopt->integerLow == 2);
        const uint8_t wantNarrow = (x2 && nativeWidth == 256) ? 1 : 0;
        if (x2 && wantNarrow != uopt->integer1080Narrow) {
            uopt->integer1080Narrow = wantNarrow;
            SerialM.println(F("480p x2: reloading the preset for the ADC clock"));
            integerLowRequest = true;
            handleType2Command('h');
            return;
        }
        rto->integer1080Centered = (nativeWidth == 320) ? 1 : 2; // (after v123i) before the placement: its border record
        integerLowApply(nativeWidth);
        SerialM.print(x2 ? F("480p x2 placed for ") : F("960p x4 placed for "));
        SerialM.print(nativeWidth);
        SerialM.println(F(" px sources"));
        return;
    }
    if (rto->integer720Active) {
        // v47: his hand-made presets for each width (and console profile)
        // v57: the buttons set the fill state: SNES 320 px in x3 / x4 (4:3) = fill (a 320 px game fills 1280 px); everything else off
        if (uopt->integer720 == 1 || uopt->integer720 == 3 || uopt->integer720 == 4) {
            uopt->integer720Fill = (integerWidthButtonIsFill() && nativeWidth == 320 && uopt->integer720 != 4) ? 1 : 0; // v75
        }
        rto->integer1080Centered = (nativeWidth == 320) ? 1 : 2; // (after v123i) before the placement: its border record
        integer720Apply(nativeWidth);
        SerialM.print(F("720p x"));
        SerialM.print(uopt->integer720 == 2 ? 5 : (uopt->integer720 == 3 ? 4 : 3));
        if (uopt->integer720 == 4)
            SerialM.print(F(" 16:9"));
        SerialM.print(F(" placed for "));
        SerialM.print(nativeWidth);
        SerialM.println(F(" px sources"));
        // v76: the PC Engine used to print the SNES notes here, which were wrong for it: on this console the 320 px button is
        // a size step of its own, with measured entries, not the SNES reuse of the 256 px picture.
        if (nativeWidth == 320 && activeConsoleProfile() == 3) {
            if (uopt->integer720 == 4)
                SerialM.println(F("PC Engine: 320 px = 4 columns per pixel (1024 px); 256 px = 3 columns (768 px, square pixels)"));
            else if (uopt->integer720 == 2)
                SerialM.println(F("PC Engine: 320 px = 6.2 columns per pixel; SDRAM clock raised for this mode"));
            else
                SerialM.println(F("PC Engine: 320 px = 5 columns per pixel (1281 px)"));
        } else if (nativeWidth == 320 && integerWidthButtonIsFill() && (uopt->integer720 == 1 || uopt->integer720 == 3)) {
            SerialM.println(F("SNES / NES profile: 320 px is the same picture as the fill (5 columns per pixel)"));
        } else if (nativeWidth == 320 && integerWidthButtonIsFill() && uopt->integer720 == 4) {
            SerialM.println(F("SNES / NES profile: 320 px = the width of a 320 px game (960 px, 3.75 columns per pixel); fill = 1280 px"));
        }
        return;
    }
    uint16_t base = 0;
    if (rto->presetID == 0x03) {
        base = 512; // NTSC 1280x720
    } else if (rto->presetID == 0x13) {
        base = 626; // PAL 1280x720
    }
    if ((rto->presetID == 0x05 || rto->presetID == 0x15) && rto->integer1080Active) {
        // [patch 1080p-integer] 320 / 256 px buttons: the 1080p x4 / x5 modes already load with the 320 px placement
        // (see doPostPresetLoadSteps); 256 px switches to the placement for 256 px sources, 320 px switches back.
        uint8_t wanted = (nativeWidth == 320) ? 1 : 2;
        if (rto->integer1080Centered == wanted && !wasWide) {
            SerialM.println(F("1080p x4 / x5: already adjusted for this width"));
            return;
        }
        // v42: x4 + 256 px needs a lower ADC clock (4 columns per pixel): reload the preset (a couple of seconds).
        // Going back to 320 px reloads it again with the normal clock.
        // (after v108h) only the SNES and the PC Engine still reload here (they load at 80 % and their placements were
        // measured that way). Everybody else changes the ADC clock in place inside integer1080Apply(): no black screen.
        // (after v111g) the PC Engine no longer reloads either: its clock changes in place (narrowWanted, Integer1080.cpp)
        if (uopt->integer1080 == 3 && integerIsSnesProfile() && activeConsoleProfile() != 3 &&
            (nativeWidth == 256 || uopt->integer1080Narrow)) {
            uopt->integer1080Narrow = (nativeWidth == 256) ? 1 : 0;
            integerRequest = true;
            handleType2Command('s');
            return;
        }
        scaleHorizontalFine = false;
        // (after v123i) his report: in 1080p x4 4:3, 256 -> CRT ate part of the CRT. The width was set AFTER the
        // placement, so applyUserEdges() (inside integer1080Apply) read the border record of the width just left.
        rto->integer1080Centered = wanted;
        integer1080Apply(nativeWidth);
        SerialM.print(F("[3x4x-v123l] 1080p x"));
        SerialM.print(uopt->integer1080 == 4 ? 5 : 4);
        SerialM.print(F(" for "));
        SerialM.print(nativeWidth);
        SerialM.print(F(" px: HScale "));
        SerialM.println(GBS::VDS_HSCALE::read());
        return;
    }
    if (rto->presetID == 0x01) {
        // [patch 960p] NTSC 1280x960: HScale 480 = 4.0 columns per 320 px pixel (5.0 for 256 px), vertical is already x4
        scaleHorizontalFine = false;
        bool ok960 = scaleHorizontalTo(480);
        SerialM.print(F("[3x4x-v123l] Perfect scale 960p for "));
        SerialM.print(nativeWidth);
        SerialM.print(F(" px: HScale "));
        SerialM.println(GBS::VDS_HSCALE::read());
        SerialM.println(ok960 ? F("(reached) adjust position with move / borders") : F("(target not reached)"));
        return;
    }
    if (base == 0) {
        SerialM.println(F("Perfect scale: only available with the 1280x720 and NTSC 1280x960 output presets"));
        return;
    }
    uint16_t target = (nativeWidth == 320) ? (uint16_t)((base * 480UL + 256) / 512) : (uint16_t)((base * 514UL + 256) / 512);
    // The sequence only works from the normal (non-fine) step size, so force it off first.
    scaleHorizontalFine = false;
    if (nativeWidth == 320) {
        target = (uint16_t)((base * 482UL + 256) / 512); // [patch 482] measured best value for 320 px (NTSC Mega Drive: 482)
    }
    bool ok = scaleHorizontalTo(target);
    if (nativeWidth == 320 && ok) {
        for (uint8_t i = 0; i < 3; i++) {
            moveCanvasHorizontal(true); // centre: 3 presses right
        }
        for (uint8_t i = 0; i < 13; i++) {
            handleType2Command('C'); // top border: 13 presses
        }
        scaleHorizontalFine = true;
        moveCanvasHorizontal(true); // final 1-unit nudge right
        scaleHorizontalFine = false;
    }
    SerialM.print(F("[3x4x-v123l] Perfect scale for "));
    SerialM.print(nativeWidth);
    SerialM.print(F(" px: HScale "));
    SerialM.print(GBS::VDS_HSCALE::read());
    SerialM.println(ok ? F(" (reached)") : F(" (target not reached)"));
}

static void stepScanlineStrength(int8_t delta)
{
    int16_t v = (int16_t)uopt->scanlineStrength;
    // snap values saved by older firmware (steps of 0x10 are multiples of 0x08 already)
    v = (v / 8) * 8;
    v += delta * 8;
    if (v < 0) {
        v = 0x50;
    } else if (v > 0x50) {
        v = 0;
    }
    uopt->scanlineStrength = (uint8_t)v;
    if (rto->scanlinesEnabled) {
        GBS::MADPT_Y_MI_OFFSET::write(uopt->scanlineStrength);
        GBS::MADPT_UV_MI_OFFSET::write(uopt->scanlineStrength);
    }
    saveUserPrefs();
    SerialM.print(F("Scanline intensity: "));
    SerialM.print((0x50 - uopt->scanlineStrength) / 8);
    SerialM.print(F(" of 10 (offset 0x"));
    SerialM.print(uopt->scanlineStrength, HEX);
    SerialM.println(F(")"));
}

// v58: FILL = 3-state cycle: 256 px -> 320 px -> wide -> 256 px (each state is what the 256 / 320 buttons give, plus "wide").
//  720p x3 16:9: 768 px -> 960 px -> 1280 px; 720p x3 / x4 (4:3): 256 <-> 320 (1280 px, no wide state);
//  1080p x4: 1024 -> 1280 -> 1920 px; 1080p x5: 1280 -> 1600 -> 1920 px (wide = estimate).
// v107m: the two wide size steps, each on its own button (web codes 46 and 47). Until now the web had a single "llenar"
// button for both, which walked the cycle: the first press gave "wide", it looked finished, and only a second press reached
// the step that fills the screen (and a third went back to 256). In the PAL presets this jumps straight to the step; the
// modes with two sizes only (720p x3 4:3 and x5) have no such step and are left alone. NTSC still has the old three-state
// fill, so there both buttons do what the single one did.
// v107n: the "Líneas" row (web codes 48..50): 0 = 224, 1 = 240 / 239, 2 = DOLOR. Stored per mode and applied by the
// mode's own placement (integer720Apply / integer1080Apply call applyLinesOverride() last), so it survives step changes,
// reloads and the second pass.
static void setLinesMode(uint8_t m)
{
    const uint8_t avail = linesAvailability();
    const uint8_t slot = memSettingIndex();
    if (avail == 0 || slot >= 16) {
        SerialM.println(F("[3x4x-v123l] lines: not available in this mode"));
        return;
    }
    if (m == 2 && !(avail & 2)) {
        SerialM.println(F("[3x4x-v123l] lines: DOLOR is not measured for this mode / console"));
        return;
    }
    const bool wasDolor = linesDolorActive();
    // (after v107q) only 240 and DOLOR remain; an old 224 request (web code 48) means 240. Nothing is saved: 240 is
    // always the starting point and DOLOR is never remembered (linesSetDolor keeps it in RAM for this slot only).
    if (m == 0)
        m = 1;
    linesSetDolor(m == 2 ? slot : 0xFF);
    // (after v107q) 224 <-> 240 with no DOLOR on either side: only the vertical window changes, so the mode is placed
    // again in place instead of loaded again. The load reset the whole preset and the picture jumped (his report of
    // 2026-11-13); the placement writes the same registers it already holds plus the window, and the memory is not
    // reset when its setting has not changed. Into or out of DOLOR it still loads, see below.
    if (!wasDolor && m != 2) {
        if (rto->integer1080Active)
            integer1080Apply(rto->integer1080Centered == 2 ? 256 : 320);
        else if (rto->integer720Active)
            integer720Apply(256);
        return;
    }
    // v107n (second build): the mode is LOADED AGAIN, exactly as its own button does, instead of only placed again. The
    // placement alone writes the registers that path happens to own, and the 1080p path of the Mega Drive does not own the
    // vertical scale, the vertical windows or the memory window: leaving DOLOR there kept its squashed scale and its open
    // windows, and the user found himself stuck in DOLOR whatever he pressed. A load starts from the preset, so every
    // register DOLOR or 240 touched comes back, and the placement then applies the new choice on top.
    linesKeepDolor = true; // this reload is DOLOR's own: it must not switch it off again
    if (rto->integer1080Active)
        requestInteger1080(uopt->integer1080);
    else if (rto->integer720Active)
        requestInteger720(uopt->integer720);
    linesKeepDolor = false;
}

// v107o: "ver huella" (web code 54). One line with everything the sync processor measures about the source, to build a
// fingerprint per console instead of telling them apart by the field rate alone (the PC Engine, the PlayStation and the
// N64 all sit near 59.82 Hz). Each value is sampled 64 times, 2 ms apart, so the line also shows how steady it is:
//   fr      field rate, Hz (the measurement the detection uses today)
//   vt      lines per frame, sync processor (median, see v107p below)
//   ht      line length, sync processor clock units (median)
//   hs      horizontal sync pulse length, same units (median)
//   hsr     hs / ht x 10000: the pulse as a share of the line, independent of the clock it is counted in
// v107p: khz is gone. vt does not read short by a constant: 3 lines short on the Mega Drive (259 / 262, 310 / 313) but
// 1 on the SNES PAL (311 / 312), so no fixed correction gives the real line rate. And ht is NOT a console feature: the
// sync processor counts in the ADC clock, which the PLL locks to the incoming line, so ht is the preset's divider (2341 on
// the PAL preset for both the Mega Drive at 50 Hz and the SNES, whose lines differ by 0.6 %; 2345 on the NTSC one). That
// makes hs, counted in the same clock, a share of the line: Mega Drive ~213, SNES PAL 176, a clear gap.
//   hp vp   HPERIOD_IF / VPERIOD_IF, the input formatter's own measurement of the same line and frame
//   std     the standard flags the chip raises (N/P = NTSC/PAL, i/p = interlaced/progressive)
//   pol     sync polarities seen by the sync processor (h, v), csync = composite sync detected
// v107p: MEDIANS, not averages. His first 60 Hz fingerprint read hs=237 with a range of 102-1016: with composite sync the
// sync processor also measures the equalising pulses (about 2.5 us) and the broad pulses (about 27 us) of the vertical
// interval, and a few of those in 64 samples wreck an average (the true pulse was ~213). The median ignores them, and
// "odd" says how many samples were more than 5 % away from it, so a noisy reading shows itself.
static uint16_t medianOf(uint16_t *a, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++) { // insertion sort, 64 values
        const uint16_t v = a[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && a[j] > v) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = v;
    }
    return a[n / 2];
}

static uint8_t oddOf(const uint16_t *a, uint8_t n, uint16_t med)
{
    uint8_t odd = 0;
    const uint16_t tol = (uint16_t)(med / 20 + 1);
    for (uint8_t i = 0; i < n; i++)
        if (a[i] + tol < med || a[i] > med + tol)
            odd++;
    return odd;
}

static void fingerprintReport()
{
    const uint8_t n = 64;
    static uint16_t vt[64], ht[64], hs[64];
    for (uint8_t i = 0; i < n; i++) {
        vt[i] = GBS::STATUS_SYNC_PROC_VTOTAL::read();
        ht[i] = GBS::STATUS_SYNC_PROC_HTOTAL::read();
        hs[i] = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
        delay(2);
    }
    const uint16_t vtM = medianOf(vt, n), htM = medianOf(ht, n), hsM = medianOf(hs, n);
    const uint8_t vtO = oddOf(vt, n, vtM), htO = oddOf(ht, n, htM), hsO = oddOf(hs, n, hsM);
    const float fr = getSourceFieldRate(0);
    const float hsr = (htM > 0) ? ((float)hsM * 10000.0f / (float)htM) : 0.0f;
    char std[12];
    snprintf(std, sizeof(std), "%s%s%s%s",
             GBS::STATUS_IF_INP_NTSC_INT::read() ? "Ni" : "", GBS::STATUS_IF_INP_NTSC_PRG::read() ? "Np" : "",
             GBS::STATUS_IF_INP_PAL_INT::read() ? "Pi" : "", GBS::STATUS_IF_INP_PAL_PRG::read() ? "Pp" : "");
    SM_PRINTF("[3x4x-v123l] huella: fr=%.4f vt=%u ht=%u hs=%u hsr=%.1f odd=%u/%u/%u "
                   "hp=%u vp=%u std=%s pol=%u%u csync=%u sog=%u/%u coast=%u/%u\n",
                   fr, vtM, htM, hsM, hsr, vtO, htO, hsO,
                   (unsigned)GBS::HPERIOD_IF::read(), (unsigned)GBS::VPERIOD_IF::read(), std[0] ? std : "-",
                   (unsigned)GBS::STATUS_SYNC_PROC_HSPOL::read(), (unsigned)GBS::STATUS_SYNC_PROC_VSPOL::read(),
                   rto->syncTypeCsync ? 1u : 0u,
                   // v107p: the conditions the sync was separated under. His PC Engine (through the ODV) read vt 266 with an
                   // unsteady sync and 271 once his saved sync state was loaded: what is measured is the sync AFTER the
                   // slicer, so two fingerprints only compare if these match.
                   (unsigned)rto->currentLevelSOG, (unsigned)GBS::SP_SOG_MODE::read(),
                   (unsigned)GBS::SP_H_CST_ST::read(), (unsigned)GBS::SP_H_CST_SP::read());
}

// v107n: the vertical memory window (VDS_VB_ST / SP), the vertical twin of the MEM buttons (web codes 51 / 52). A few
// blanked lines of the memory; if they fall inside the visible window they draw a black line across the picture.
static void nudgeMemVertical(int8_t d)
{
    const int16_t vt = (int16_t)GBS::VDS_VSYNC_RST::read();
    int16_t st = (int16_t)GBS::VDS_VB_ST::read() + d;
    int16_t sp = (int16_t)GBS::VDS_VB_SP::read() + d;
    if (st < 1 || sp < 1 || st > vt - 2 || sp > vt - 2) {
        SerialM.println(F("[3x4x-v123l] memory window: at the limit"));
        return;
    }
    GBS::VDS_VB_ST::write((uint16_t)st);
    GBS::VDS_VB_SP::write((uint16_t)sp);
    SerialM.print(F("[3x4x-v123l] memory window (vertical) "));
    SerialM.print(st);
    SerialM.print('/');
    SerialM.println(sp);
}

// (after v107v) one place for the console profile buttons: the four of before and the fifteen without measurements
static void setConsoleProfileDirect(uint8_t prof)
{
    if (prof >= CONSOLE_PROFILE_COUNT)
        return;
    uopt->consoleProfile = prof;
    saveUserPrefs();
    SerialM.print(F("Console profile: "));
    if (prof == 0)
        SerialM.println(F("auto (by field rate)"));
    else if (prof == 1)
        SerialM.println(F("Mega Drive"));
    else if (prof == 3)
        SerialM.println(F("PC Engine"));
    else {
        SerialM.print(consoleProfileName(prof));
        SerialM.println(prof >= CONSOLE_PROFILE_FIRST_EMPTY ? F(" (no measurements yet: Mega Drive placement)") : F(""));
    }
    // (after v107v) the last mode this console was used in, if it has one and it is not the one on screen; that load
    // brings the placement and the colours with it
    if (consoleModeCheck())
        return;
    integerProfileRefresh();
    colorPresetApply(); // v107p: and the picture this console remembers
}

static void cycleFill();
static void setSizeStepDirect(uint8_t step)
{
    if (rto->presetID == 0x13 || rto->presetID == 0x15) {
        if (rto->presetID == 0x13 && (uopt->integer720 == 1 || uopt->integer720 == 2)) {
            SerialM.println(F("[3x4x-v123l] this mode has two sizes only"));
            return;
        }
        uopt->sizeStep = step & 3;
        if (uopt->sizeStep > 1 && sizeStepsTwoMd())
            uopt->sizeStep = 1; // v107q: the Mega Drive in PAL 720p x4 has 256 and 320 only
        if (uopt->sizeStep == 3 && sizeStepsThree())
            uopt->sizeStep = 2; // v107q: no fill step for the Mega Drive in PAL 1080p x4
        if (uopt->sizeStep > 1 && md43Able())
            uopt->sizeStep = 1; // (after v108h) its «4:3» has two sizes: 256 px and 320 px; (after v111f) its 16:9 too, his decision: «ancho» removed (the 4:3 button gives that picture)
        saveUserPrefs();
        if (rto->presetID == 0x13)
            integer720Apply(256);
        else
            integer1080Apply(rto->integer1080Centered == 2 ? 256 : 320);
        rto->integer720ReapplyAt = millis() + 700; // v107m: the second pass, as after a load (see cycleFill)
        return;
    }
    cycleFill();
}

static void cycleFill()
{
    // v86: in the PAL presets this button is the SIZE STEP cycle the user designed: square -> CRT shape -> wide -> fills the
    // screen, always the four, never collapsed, and the step is remembered. Each step says what it is and what it gives.
    // NTSC keeps the old behaviour until its missing steps are measured.
    if (rto->presetID == 0x13 || rto->presetID == 0x15) {
        // v107f: 720p x3 4:3 has only two real sizes, because the CRT step already fills the 4:3 frame and steps 3 and 4
        // load it again. Walking through them made the button look dead, so there the cycle just alternates 256 <-> CRT.
        if ((rto->presetID == 0x13 && (uopt->integer720 == 1 || uopt->integer720 == 2)) || sizeStepsTwoMd() || md43Able()) // x3 4:3 and x5: two sizes (and the MD in x4, and its 1080p x4 in both shapes: no «ancho», after v111f)
            uopt->sizeStep = (uopt->sizeStep == 0) ? 1 : 0;
        else if (sizeStepsThree()) // v107q: Mega Drive in PAL 1080p x4: square -> CRT -> wide
            uopt->sizeStep = (uopt->sizeStep >= 2) ? 0 : (uint8_t)(uopt->sizeStep + 1);
        else
            uopt->sizeStep = (uint8_t)((uopt->sizeStep + 1) & 3);
        saveUserPrefs();
        if (rto->presetID == 0x13)
            integer720Apply(256);
        else
            integer1080Apply(rto->integer1080Centered == 2 ? 256 : 320);
        // v107m: and place it again 700 ms later, once the SDRAM has settled after the reset this step's memory needed.
        // A preset load has always had this second pass (integer720ReapplyTick); a step change wrote everything once,
        // straight after ResetSDRAM, and the user saw RAM glitches on some step changes that a reload did not show.
        rto->integer720ReapplyAt = millis() + 700;
        return;
    }
    if (rto->integer720Active && (uopt->integer720 == 1 || uopt->integer720 == 3 || uopt->integer720 == 4)) {
        const bool w169 = (uopt->integer720 == 4);
        const bool snes = integerWidthButtonIsFill(); // v75: the PC Engine's "320 px" is a size step, not a fill
        int level; // 0 = 256, 1 = 320, 2 = wide
        if (w169)
            level = uopt->integer720Fill ? 2 : (rto->integer1080Centered == 1 ? 1 : 0);
        else
            level = (rto->integer1080Centered == 1 || uopt->integer720Fill) ? 1 : 0;
        // (after v111g) the PC Engine in 720p x3 16:9: 256 and 320 only, no «llenar» (his decision)
        const int next = (w169 && activeConsoleProfile() != 3) ? (level + 1) % 3 : (level + 1) % 2;
        if (next == 0) {
            uopt->integer720Fill = 0;
            rto->integer1080Centered = 2;
        } else if (next == 1) {
            uopt->integer720Fill = (!w169 && snes) ? 1 : 0;
            rto->integer1080Centered = 1;
        } else {
            uopt->integer720Fill = 1;
            rto->integer1080Centered = 1;
        }
        integer720Apply(rto->integer1080Centered == 2 ? 256 : 320);
        SerialM.print(F("[3x4x-v123l] fill: "));
        SerialM.println(next == 0 ? F("256 px picture") : (next == 1 ? F("320 px picture") : F("wide (1280 px, whole screen width of the 720p)")));
        return;
    }
    if (rto->integer1080Active && (rto->presetID == 0x05 || rto->presetID == 0x15)) {
        const bool pal = (rto->presetID == 0x15);
        int level = uopt->integer1080Wide ? 2 : (rto->integer1080Centered == 2 ? 0 : 1);
        int next = (level + 1) % ((pal || md43NoWide()) ? 2 : 3); // (after v111f) no «ancho» where there is a 4:3 (his decision; after v120c the PC Engine too)
        if (next == 2) {
            uopt->integer1080Wide = 1;
            scaleHorizontalFine = false;
            rto->integer1080Centered = 1; // (after v123i) before the placement: its border record
            integer1080Apply(320);
            SerialM.println(F("[3x4x-v123l] fill: wide (1920 px, ESTIMATE: adjust with move / border buttons)"));
        } else {
            setPerfectHScale(next == 0 ? 256 : 320); // also clears the wide state
            SerialM.println(next == 0 ? F("[3x4x-v123l] fill: 256 px picture") : F("[3x4x-v123l] fill: 320 px picture"));
        }
        return;
    }
    if (rto->integerLowActive) {
        setPerfectHScale(rto->integer1080Centered == 2 ? 320 : 256); // v62: 960p x4 / 480p x2 are 4:3, so only 256 <-> 320
        return;
    }
    SerialM.println(F("fill: only in the 720p x3 / x3 16:9 / x4 and the 1080p x4 / x5 modes"));
}

// [patch edges] x5 (line filter forced): the display window must never end before the memory blanking (DIS_HB_ST >=
// HB_ST), otherwise the picture shifts one line and the scanlines break (measured on hardware). The right edge buttons keep
// HB_ST equal to DIS_HB_ST in x5, as the original scale / shift code does.
void keepHbStWithDisplayEdge()
{
    // v37: x5 (NTSC and PAL) and NTSC x4; PAL x4 excluded (HB_ST changes make its lines jitter)
    // v46: not in the lowered-clock narrow mode (x4 + 256 px): cutting HB_ST there makes the whole picture shimmer
    if (uopt->integer1080Narrow)
        return;
    if (rto->integer1080Active && (uopt->integer1080 == 4 || (uopt->integer1080 == 3 && rto->presetID == 0x05))) {
        GBS::VDS_HB_ST::write(GBS::VDS_DIS_HB_ST::read());
    }
    // v77: the same guard for the 720p modes, which never had it. Measured by the user on a SNES in 720p x5 with HScale 401:
    // with VDS_DIS_HB_ST 2110 = VDS_HB_ST 2110 the picture is clean; one more step of right border puts DIS_HB_ST at 2109,
    // one unit BELOW HB_ST, and the picture jumps a line and the scanlines break (toggling them does not repair it, because
    // the line parity is what moved). VideoScaling.cpp already corrects this relation when the scale changes:
    //     if (VDS_DIS_HB_ST < VDS_HB_ST) VDS_DIS_HB_ST = VDS_HB_ST;
    // but nothing did when only the border moved. Here HB_ST follows the border down instead, so he can keep adding border.
    // It only fires in the state that is broken today, so it cannot change anything that currently works.
    if (rto->integer720Active && GBS::VDS_DIS_HB_ST::read() < GBS::VDS_HB_ST::read()) {
        GBS::VDS_HB_ST::write(GBS::VDS_DIS_HB_ST::read());
    }
}

void handleType2Command(char argument)
{
    myLog("user", argument);
    switch (argument) {
        case '[': // v37: web terminal log: clean
            SerialM.setCleanLog(true);
            SerialM.println(F("log: clean (housekeeping lines hidden)"));
            break;
        case ']': // v37: web terminal log: full
            SerialM.setCleanLog(false);
            SerialM.println(F("log: full"));
            break;
        case '0':
            // (after v107r) the old "Force PAL 50Hz to 60Hz" is gone (his decision): our output rates do the same without a
            // reboot. The command only reports it and keeps it off.
            uopt->PalForce60 = 0;
            SerialM.println(F("pal force 60hz: removed, use the output rate buttons (Frecuencias y FrameTime Lock)"));
            saveUserPrefs();
            break;
        case '\xa4': // (after v108f) «valores de fábrica» with the personal presets too (web code 76, after his second yes)
        case '1':
            // reset to defaults button
            // (after v108f) his request: «que el usuario pueda borrar todo lo que él hizo». Until now only the preferences
            // went back; the colours and filters of each console, the sync states, the edges and the last mode of each
            // console stayed. Now they go too. The personal presets only if he said so to the second question; the wifi
            // always stays (the ESP keeps it apart), so the board is still reachable.
            edgesDiscardPending(); // (after v123j) a border waiting for its 30 s must not come back after the wipe
            posDiscardPending();
            LittleFS.remove("/pos.bin");
            LittleFS.remove("/colorv5.txt");
            LittleFS.remove("/colorv4.txt");
            LittleFS.remove("/syncv1.txt");
            LittleFS.remove("/edges.bin");
            LittleFS.remove("/cmode.bin");
            if (argument == '\xa4') {
                LittleFS.remove(SLOTS_FILE);
                Dir dir = LittleFS.openDir("/");
                String gone[40];
                uint8_t n = 0;
                while (dir.next() && n < 40) {
                    if (dir.fileName().startsWith("preset_") || dir.fileName().startsWith("/preset_"))
                        gone[n++] = dir.fileName();
                }
                for (uint8_t i = 0; i < n; i++)
                    LittleFS.remove(gone[i].startsWith("/") ? gone[i] : "/" + gone[i]);
                SerialM.print(F("personal presets erased: "));
                SerialM.println(n);
            }
            webSocket.disconnect();
            loadDefaultUserOptions();
            saveUserPrefs();
            prefsFlush(); // (after v123j) written now: the board restarts right after
            Serial.println(F("options set to defaults, restarting"));
            delay(60);
#ifdef ESP8266
            ESP.reset(); // don't use restart(), messes up websocket reconnects
#else
            ESP.restart();
#endif
            //
            break;
        case '2':
            //
            break;
        case '3': // load custom preset
        {
            uopt->presetPreference = OutputCustomized; // custom
            uopt->outputChosen = 1; // (after v111f)
            if (rto->videoStandardInput == 14) {
                // vga upscale path: let synwatcher handle it
                rto->videoStandardInput = 15;
            } else {
                // normal path
                applyPresets(rto->videoStandardInput);
            }
            saveUserPrefs();
        } break;
        case '4': // save custom preset
            savePresetToLittleFS();
            uopt->presetPreference = OutputCustomized; // custom
            saveUserPrefs();
            break;
        case '5':
            //Frame Time Lock toggle
            uopt->enableFrameTimeLock = !uopt->enableFrameTimeLock;
            saveUserPrefs();
            if (uopt->enableFrameTimeLock) {
                SerialM.println(F("FTL on"));
            } else {
                SerialM.println(F("FTL off"));
            }
            if (!rto->extClockGenDetected) {
                FrameSync::reset(uopt->frameTimeLockMethod);
            }
            if (uopt->enableFrameTimeLock) {
                activeFrameTimeLockInitialSteps();
            }
            break;
        case '6':
            //
            break;
        case '7':
            uopt->wantScanlines = !uopt->wantScanlines;
            SerialM.print(F("scanlines: "));
            if (uopt->wantScanlines) {
                SerialM.println(F("on (Line Filter recommended)"));
            } else {
                disableScanlines();
                SerialM.println(F("off"));
            }
            saveUserPrefs();
            break;
        case 'a':
            prefsFlush(); // (after v123j) nothing waiting is lost to the restart
            edgesFlushNow();
            webSocket.disconnect();
            Serial.println(F("restart"));
            delay(60);
#ifdef ESP8266
            ESP.reset(); // don't use restart(), messes up websocket reconnects
#else
            ESP.restart();
#endif
            break;
        case 'R':
            uopt->reverseRotaryEncoderForOledMenu ^= 1;
            SerialM.print(F("reverse OLED rotary: "));
            SerialM.println(uopt->reverseRotaryEncoderForOledMenu ? F("on") : F("off"));
            saveUserPrefs();
            break;
        case 'e': // print files on LittleFS
        {
#ifdef ESP8266
            Dir dir = LittleFS.openDir("/");
            while (dir.next()) {
                SerialM.print(dir.fileName());
                SerialM.print(F(" "));
                SerialM.println(dir.fileSize());
                delay(1); // wifi stack
            }
#else
            File root = LittleFS.open("/");
            File file = root.openNextFile();
            while(file){
                SerialM.print(file.name());
                SerialM.print(F(" "));
                SerialM.println(file.size());
                file = root.openNextFile();
                delay(1);
            }
#endif
            ////
            prefsFlush(); // (after v123j) show what is really kept
            File f = LittleFS.open("/preferencesv2.txt", "r");
            if (!f) {
                SerialM.println(F("failed opening preferences file"));
            } else {
                SerialM.print(F("preset preference = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("frame time lock = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("preset slot = "));
                SerialM.println((uint8_t)(f.read()));
                SerialM.print(F("frame lock method = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("auto gain = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("scanlines = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("component output = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("deinterlacer mode = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("line filter = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("peaking = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("preferScalingRgbhv = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("6-tap = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("pal force60 = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("matched = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("step response = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("disable external clock generator = "));
                SerialM.println((uint8_t)(f.read() - '0'));

                f.close();
            }
        } break;
        case 'f':
        case 'g':
        case 'h':
        case 'p':
        case 's':
        case 'L': {
            // load preset via webui
            uopt->outputChosen = 1; // (after v111f) any resolution picked by hand, Rama's or ours: no red notice
            uint8_t videoMode = getVideoMode();
            if (videoMode == 0 && GBS::STATUS_SYNC_PROC_HSACT::read())
                videoMode = rto->videoStandardInput; // last known good as fallback
            //else videoMode stays 0 and we'll apply via some assumptions

            if (argument == 'f') {
                uopt->presetPreference = Output960P; // 1280x960
                if (!integerLowRequest)
                    uopt->integerLow = 0; // the plain 960p button always gives the stock preset
            }
            if (argument == 'g') {
                uopt->presetPreference = Output720P; // 1280x720
                if (!integer720Request) {
                    uopt->integer720 = 0; // the plain 720p button always gives the stock preset
                    uopt->integer1080Narrow = 0;
                }
                integer720Request = false;
            }
            if (argument == 'h') {
                uopt->presetPreference = Output480P; // 720x480/768x576
                if (!integerLowRequest) {
                    uopt->integerLow = 0; // the plain 480p button always gives the stock preset
                    uopt->integer1080Narrow = 0;
                }
            }
            integerLowRequest = false;
            if (argument == 'p')
                uopt->presetPreference = Output1024P; // 1280x1024
            if (argument == 's') {
                uopt->presetPreference = Output1080P; // 1920x1080
                if (!integerRequest) {
                    uopt->integer1080 = 0; // the plain 1080p button always gives the stock preset
                    uopt->integer1080Narrow = 0;
                }
                integerRequest = false;
            }
            if (argument == 'L')
                uopt->presetPreference = OutputDownscale; // downscale

            rto->useHdmiSyncFix = 1; // disables sync out when programming preset
            if (rto->videoStandardInput == 14) {
                // vga upscale path: let synwatcher handle it
                rto->videoStandardInput = 15;
            } else {
                // normal path
                applyPresets(videoMode);
            }
            saveUserPrefs();
        } break;
        case 'i':
            // toggle active frametime lock method
            if (!rto->extClockGenDetected) {
                FrameSync::reset(uopt->frameTimeLockMethod);
            }
            if (uopt->frameTimeLockMethod == 0) {
                uopt->frameTimeLockMethod = 1;
            } else if (uopt->frameTimeLockMethod == 1) {
                uopt->frameTimeLockMethod = 0;
            }
            saveUserPrefs();
            activeFrameTimeLockInitialSteps();
            break;
        case 'b':
            setOutputRateMode(0); // follow source
            break;
        case 'c':
            setOutputRateMode(1); // 59.94 Hz
            break;
        case 'd':
            setOutputRateMode(2); // 60.00 Hz
            break;
        case 'j':
            // FTL method: VTotal + VSST
            setFrameTimeLockMethod(0);
            break;
        case 'k':
            // FTL method: VTotal only
            setFrameTimeLockMethod(1);
            break;
        case 'l':
            // cycle through available SDRAM clocks
            {
                uint8_t PLL_MS = GBS::PLL_MS::read();
                uint8_t memClock = 0;

                if (PLL_MS == 0)
                    PLL_MS = 2;
                else if (PLL_MS == 2)
                    PLL_MS = 7;
                else if (PLL_MS == 7)
                    PLL_MS = 4;
                else if (PLL_MS == 4)
                    PLL_MS = 3;
                else if (PLL_MS == 3)
                    PLL_MS = 5;
                else if (PLL_MS == 5)
                    PLL_MS = 0;

                switch (PLL_MS) {
                    case 0:
                        memClock = 108;
                        break;
                    case 1:
                        memClock = 81;
                        break; // goes well with 4_2C = 0x14, 4_2D = 0x27
                    case 2:
                        memClock = 10;
                        break; // feedback clock
                    case 3:
                        memClock = 162;
                        break;
                    case 4:
                        memClock = 144;
                        break;
                    case 5:
                        memClock = 185;
                        break; // slight OC
                    case 6:
                        memClock = 216;
                        break; // !OC!
                    case 7:
                        memClock = 129;
                        break;
                    default:
                        break;
                }
                GBS::PLL_MS::write(PLL_MS);
                ResetSDRAM();
                scanlinesAfterSdramReset(); // v92
                // v77: while an x5 mode is loaded, remember the choice. Those modes write the SDRAM clock after every preset
                // load, so without this the user's setting was overwritten the next time the preset came back.
                // v88: remembered PER MODE, not just for x5. One byte could only hold one setting, so his 185 MHz for 720p x5
                // and his 162 for PAL 1080p x4 kept erasing each other, and in 1080p nothing was re-applied anyway.
                {
                    // v107m: and per SIZE STEP: stored for the whole mode it overrode the measured pair of every other
                    // step. x5MemClock is no longer updated: it is a fallback for every step and would leak this one.
                    uint8_t *own = memClockOwn();
                    if (own) {
                        *own = PLL_MS;
                        memSettingsSave(); // (after v120c) per console
                        saveUserPrefs();
                        SerialM.print(F("[3x4x-v123l] SDRAM clock saved: "));
                        SerialM.print(memClockMhzFor(PLL_MS));
                        SerialM.println(F(" MHz"));
                    }
                }
                if (memClock != 10) {
                    SerialM.print(F("SDRAM clock: "));
                    SerialM.print(memClock);
                    SerialM.println(F("Mhz"));
                } else {
                    SerialM.print(F("SDRAM clock: "));
                    SerialM.println(F("Feedback clock"));
                }
            }
            break;
        case 'm':
            SerialM.print(F("Line Filter: "));
            if (uopt->wantVdsLineFilter) {
                uopt->wantVdsLineFilter = 0;
                GBS::VDS_D_RAM_BYPS::write(1);
                SerialM.println(F("off"));
            } else {
                uopt->wantVdsLineFilter = 1;
                GBS::VDS_D_RAM_BYPS::write(0);
                SerialM.println(F("on"));
            }
            saveUserPrefs();
            break;
        case 'n':
            SerialM.print(F("ADC gain++ : "));
            uopt->enableAutoGain = 0;
            setAdcGain(GBS::ADC_RGCTRL::read() - 1);
            SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
            break;
        case 'o':
            SerialM.print(F("ADC gain-- : "));
            uopt->enableAutoGain = 0;
            setAdcGain(GBS::ADC_RGCTRL::read() + 1);
            SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
            break;
        case 'A': {
            const uint16_t hs = scaleHorizontalFine ? 1 : 4;
            uint16_t htotal = GBS::VDS_HSYNC_RST::read();
            uint16_t hbstd = GBS::VDS_DIS_HB_ST::read();
            uint16_t hbspd = GBS::VDS_DIS_HB_SP::read();
            if ((hbstd > hs) && (hbspd < (htotal - hs))) {
                GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() - hs);
                GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() + hs);
                edgesNote(1, -(int16_t)hs);
                edgesNote(0, (int16_t)hs);
                keepHbStWithDisplayEdge();
                SerialM.println(F("Left and right borders: reduce the picture window"));
            } else {
                SerialM.println(F("limit"));
            }
        } break;
        case 'B': {
            const uint16_t hs = scaleHorizontalFine ? 1 : 4;
            uint16_t htotal = GBS::VDS_HSYNC_RST::read();
            uint16_t hbstd = GBS::VDS_DIS_HB_ST::read();
            uint16_t hbspd = GBS::VDS_DIS_HB_SP::read();
            if ((hbstd < (htotal - hs)) && (hbspd > hs)) {
                GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() + hs);
                GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() - hs);
                edgesNote(1, (int16_t)hs);
                edgesNote(0, -(int16_t)hs);
                keepHbStWithDisplayEdge();
                SerialM.println(F("Left and right borders: enlarge the picture window"));
            } else {
                SerialM.println(F("limit"));
            }
        } break;
        case 'C': {
            // vert mask +
            const uint16_t vs = scaleHorizontalFine ? 1 : 2;
            uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
            uint16_t vbstd = GBS::VDS_DIS_VB_ST::read();
            uint16_t vbspd = GBS::VDS_DIS_VB_SP::read();
            if ((vbstd > 6) && (vbspd < (vtotal - 4))) {
                GBS::VDS_DIS_VB_ST::write(vbstd - vs);
                GBS::VDS_DIS_VB_SP::write(vbspd + vs);
                edgesNote(3, -(int16_t)vs);
                edgesNote(2, (int16_t)vs);
                SerialM.println(F("Top and bottom borders: reduce the picture window"));
            } else {
                SerialM.println(F("limit"));
            }
        } break;
        // [patch edges] independent edges of the output window (the A / B / C / D borders move both sides at once).
        // Web UI "Edges" panel: '_' '-' left edge out / in, '.' '~' right edge in / out, ',' '*' top edge out / in,
        // ';' '$' bottom edge in / out. Steps: horizontal 4 (fine 1) units ~6 px, vertical 2 (fine 1) lines.
        case '_': // left edge out
        case '-': { // left edge in
            const uint16_t hs = scaleHorizontalFine ? 1 : 4;
            const uint16_t sp = GBS::VDS_DIS_HB_SP::read();
            const uint16_t st = GBS::VDS_DIS_HB_ST::read();
            if (argument == '-' && (sp + hs + 16) < st) {
                GBS::VDS_DIS_HB_SP::write(sp + hs);
                edgesNote(0, (int16_t)hs);
            } else if (argument == '_' && sp > hs) {
                GBS::VDS_DIS_HB_SP::write(sp - hs);
                edgesNote(0, -(int16_t)hs);
            } else {
                SerialM.println(F("limit"));
                break;
            }
            SerialM.print(argument == '_' ? F("Left border: enlarge (window starts at ") : F("Left border: reduce (window starts at "));
            SerialM.print(GBS::VDS_DIS_HB_SP::read());
            SerialM.println(F(")"));
        } break;
        case '.': // right edge in
        case '~': { // right edge out
            const uint16_t hs = scaleHorizontalFine ? 1 : 4;
            const uint16_t htotal = GBS::VDS_HSYNC_RST::read();
            const uint16_t sp = GBS::VDS_DIS_HB_SP::read();
            const uint16_t st = GBS::VDS_DIS_HB_ST::read();
            if (argument == '.' && st > (sp + hs + 16)) {
                GBS::VDS_DIS_HB_ST::write(st - hs);
                edgesNote(1, -(int16_t)hs);
            } else if (argument == '~' && (st + hs) < htotal) {
                GBS::VDS_DIS_HB_ST::write(st + hs);
                edgesNote(1, (int16_t)hs);
            } else {
                SerialM.println(F("limit"));
                break;
            }
            keepHbStWithDisplayEdge();
            SerialM.print(argument == '~' ? F("Right border: enlarge (window ends at ") : F("Right border: reduce (window ends at "));
            SerialM.print(GBS::VDS_DIS_HB_ST::read());
            SerialM.println(F(")"));
        } break;
        case ',': // top edge out
        case '*': { // top edge in
            const uint16_t vs = scaleHorizontalFine ? 1 : 2;
            const uint16_t sp = GBS::VDS_DIS_VB_SP::read();
            const uint16_t st = GBS::VDS_DIS_VB_ST::read();
            if (argument == '*' && (sp + vs + 8) < st) {
                GBS::VDS_DIS_VB_SP::write(sp + vs);
                edgesNote(2, (int16_t)vs);
            } else if (argument == ',' && sp > (6 + vs)) {
                GBS::VDS_DIS_VB_SP::write(sp - vs);
                edgesNote(2, -(int16_t)vs);
            } else {
                SerialM.println(F("limit"));
                break;
            }
            SerialM.print(argument == ',' ? F("Top border: enlarge (window starts at line ") : F("Top border: reduce (window starts at line "));
            SerialM.print(GBS::VDS_DIS_VB_SP::read());
            SerialM.println(F(")"));
        } break;
        case ';': // bottom edge in
        case '$': { // bottom edge out
            const uint16_t vs = scaleHorizontalFine ? 1 : 2;
            const uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
            const uint16_t sp = GBS::VDS_DIS_VB_SP::read();
            const uint16_t st = GBS::VDS_DIS_VB_ST::read();
            if (argument == ';' && st > (sp + vs + 8)) {
                GBS::VDS_DIS_VB_ST::write(st - vs);
                edgesNote(3, -(int16_t)vs);
            } else if (argument == '$' && (st + vs) < (vtotal - 4)) {
                GBS::VDS_DIS_VB_ST::write(st + vs);
                edgesNote(3, (int16_t)vs);
            } else {
                SerialM.println(F("limit"));
                break;
            }
            SerialM.print(argument == '$' ? F("Bottom border: enlarge (window ends at line ") : F("Bottom border: reduce (window ends at line "));
            SerialM.print(GBS::VDS_DIS_VB_ST::read());
            SerialM.println(F(")"));
        } break;
        case 'D': {
            // vert mask -
            const uint16_t vs = scaleHorizontalFine ? 1 : 2;
            uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
            uint16_t vbstd = GBS::VDS_DIS_VB_ST::read();
            uint16_t vbspd = GBS::VDS_DIS_VB_SP::read();
            if ((vbstd < (vtotal - 4)) && (vbspd > 6)) {
                GBS::VDS_DIS_VB_ST::write(vbstd + vs);
                GBS::VDS_DIS_VB_SP::write(vbspd - vs);
                edgesNote(3, (int16_t)vs);
                edgesNote(2, -(int16_t)vs);
                SerialM.println(F("Top and bottom borders: enlarge the picture window"));
            } else {
                SerialM.println(F("limit"));
            }
        } break;
        case 'q':
            if (uopt->deintMode != 1) {
                uopt->deintMode = 1;
                disableMotionAdaptDeinterlace();
                if (GBS::GBS_OPTION_SCANLINES_ENABLED::read()) {
                    disableScanlines();
                }
                saveUserPrefs();
            }
            SerialM.println(F("Deinterlacer: Bob"));
            break;
        case 'r':
            if (uopt->deintMode != 0) {
                uopt->deintMode = 0;
                saveUserPrefs();
                // will enable next loop()
            }
            SerialM.println(F("Deinterlacer: Motion Adaptive"));
            break;
        case 't':
            // unused now
            SerialM.print(F("6-tap: "));
            if (uopt->wantTap6 == 0) {
                uopt->wantTap6 = 1;
                GBS::VDS_TAP6_BYPS::write(0);
                GBS::MADPT_Y_DELAY_UV_DELAY::write(GBS::MADPT_Y_DELAY_UV_DELAY::read() - 1);
                SerialM.println(F("on"));
            } else {
                uopt->wantTap6 = 0;
                GBS::VDS_TAP6_BYPS::write(1);
                GBS::MADPT_Y_DELAY_UV_DELAY::write(GBS::MADPT_Y_DELAY_UV_DELAY::read() + 1);
                SerialM.println(F("off"));
            }
            saveUserPrefs();
            break;
        case 'u':
            // restart to attempt wifi station mode connect
            delay(30);
            WiFi.mode(WIFI_STA);
#ifdef ESP8266
            WiFi.hostname(device_hostname_partial); // _full
#else
            WiFi.setHostname(device_hostname_partial);
#endif
            delay(30);
            prefsFlush(); // (after v123j)
            edgesFlushNow();
#ifdef ESP8266
            ESP.reset();
#else
            ESP.restart();
#endif
            break;
        case 'v': {
            uopt->wantFullHeight = !uopt->wantFullHeight;
            saveUserPrefs();
            uint8_t vidMode = getVideoMode();
            if (uopt->presetPreference == 5) {
                if (GBS::GBS_PRESET_ID::read() == 0x05 || GBS::GBS_PRESET_ID::read() == 0x15) {
                    applyPresets(vidMode);
                }
            }
        } break;
        case 'w':
            uopt->enableCalibrationADC = !uopt->enableCalibrationADC;
            saveUserPrefs();
            break;
        case 'x':
            uopt->preferScalingRgbhv = !uopt->preferScalingRgbhv;
            SerialM.print(F("preferScalingRgbhv: "));
            if (uopt->preferScalingRgbhv) {
                SerialM.println(F("on"));
            } else {
                SerialM.println(F("off"));
            }
            saveUserPrefs();
            break;
        case 'X':
            SerialM.print(F("ExternalClockGenerator "));
            if (uopt->disableExternalClockGenerator == 0) {
                uopt->disableExternalClockGenerator = 1;
                SerialM.println(F("disabled"));
            } else {
                uopt->disableExternalClockGenerator = 0;
                SerialM.println(F("enabled"));
            }
            saveUserPrefs();
            break;
        case 'z':
            // sog slicer level
            if (rto->currentLevelSOG > 0) {
                rto->currentLevelSOG -= 1;
            } else {
                rto->currentLevelSOG = 16;
            }
            setAndUpdateSogLevel(rto->currentLevelSOG);
            optimizePhaseSP();
            SerialM.print(F("Phase: "));
            SerialM.print(rto->phaseSP);
            SerialM.print(F(" SOG: "));
            SerialM.print(rto->currentLevelSOG);
            SerialM.println();
            break;
        case 'E':
            // test option for now
            SerialM.print(F("IF Auto Offset: "));
            toggleIfAutoOffset();
            if (GBS::IF_AUTO_OFST_EN::read()) {
                SerialM.println(F("on"));
            } else {
                SerialM.println(F("off"));
            }
            break;
        case 'F':
            // freeze pic
            if (GBS::CAPTURE_ENABLE::read()) {
                GBS::CAPTURE_ENABLE::write(0);
            } else {
                GBS::CAPTURE_ENABLE::write(1);
            }
            break;
        case 'K':
            // scanline strength: stronger lines (finer steps, [patch scanlines])
            stepScanlineStrength(-1);
            break;
        case 'G':
            // scanline strength: weaker lines ([patch scanlines])
            stepScanlineStrength(+1);
            break;
        case 'J':
            cyclePeakLevel(); // [patch image-tuning]
            break;
        case 'V':
            cycleStepLevel(); // [patch image-tuning]
            break;
        case '(':
            setPerfectHScale(320); // [patch perfect-scale]
            break;
        case ')':
            setPerfectHScale(256);
            break;
        case '!':
            requestInteger1080(3); // [patch 1080p-integer] 1080p x4
            break;
        case '9':
            requestInteger1080(4); // [patch 1080p-integer] 1080p x5
            break;
        case '^':
            requestInteger720(1); // 720p x3
            break;
        case '\x07':
        case '\x08': { // v63: SOG slicer level up / down (the level the sync is sliced at; marginal sources often need another)
            const bool up = (argument == '\x07');
            if (up) {
                rto->currentLevelSOG = (rto->currentLevelSOG >= 16) ? 16 : rto->currentLevelSOG + 1;
            } else {
                rto->currentLevelSOG = (rto->currentLevelSOG == 0) ? 0 : rto->currentLevelSOG - 1;
            }
            setAndUpdateSogLevel(rto->currentLevelSOG);
            optimizePhaseSP();
            SerialM.print(F("[3x4x-v123l] SOG level "));
            SerialM.print(rto->currentLevelSOG);
            SerialM.print(F(" (phase "));
            SerialM.print(rto->phaseSP);
            SerialM.println(F(")"));
        } break;
        case '\x09': // v63: sync state in one line, to compare settings on a source that will not lock
            SerialM.print(F("[3x4x-v123l] sync: SOG level "));
            SerialM.print(rto->currentLevelSOG);
            SerialM.print(F(", SOG mode "));
            SerialM.print(GBS::SP_SOG_MODE::read());
            SerialM.print(F(", csync "));
            SerialM.print(rto->syncTypeCsync ? 1 : 0);
            SerialM.print(F(", coast "));
            SerialM.print(GBS::SP_H_CST_ST::read());
            SerialM.print('/');
            SerialM.print(GBS::SP_H_CST_SP::read());
            SerialM.print(F(" set "));
            SerialM.print(rto->coastPositionIsSet ? 1 : 0);
            SerialM.print(F(", clamp set "));
            SerialM.print(rto->clampPositionIsSet ? 1 : 0);
            SerialM.print(F(", pulse ignore "));
            SerialM.print(GBS::SP_H_PULSE_IGNOR::read());
            SerialM.print(F(", stable "));
            SerialM.print(rto->continousStableCounter);
            SerialM.print(F(", losses "));
            SerialM.print(rto->statLosses);
            SerialM.print(F(", retries "));
            SerialM.print(rto->statRetries);
            SerialM.print(F(", tolerant sync "));
            SerialM.print(uopt->tolerantSync ? F("on") : F("off"));
            SerialM.print(F(", watcher "));
            SerialM.print(rto->syncWatcherEnabled ? F("running") : F("PAUSED"));
            SerialM.print(F(", saved state "));
            SerialM.print(syncPresetAvailable() ? F("yes") : F("no"));
            SerialM.print(F(", auto-retry "));
            SerialM.print(uopt->autoRetrySync ? F("on") : F("off"));
            SerialM.print(F(", auto-lock "));
            if (uopt->autoLockSync == 0) {
                SerialM.println(F("off"));
            } else {
                static const uint8_t lp[4] = {2, 10, 30, 62};
                SerialM.println(lp[(uopt->autoLockSync - 1) & 0x03]);
            }
            syncPresetPrintSaved(); // v68: and the stored one in full, to keep a copy outside the board
            break;
        case '\x0c': // v66: capture the sync state of this console profile at this instant
            syncFinishSetup();       // v67: clamp and scanlines first, so what is captured is the finished picture
            syncPresetCaptureNow();
            uopt->autoRetrySync = 0; // keep this one: stop the hands-free attempts
            if (rto->syncWatcherEnabled) {
                rto->syncWatcherEnabled = false; // v67: and keep the picture that was just captured
                SerialM.println(F("[3x4x-v123l] sync watcher paused: the captured picture stays"));
            }
            break;
        case '\x0e': // v66: write the saved sync state again
            syncPresetApply(false);
            break;
        case '\x0f': // v66: forget it
            syncPresetClear();
            break;
        case '\x10': // v66: the user's known-good PC Engine / ODV state
            syncPresetApplyOdvRecipe();
            break;
        case '\x11': // v66: hands-free attempts on / off
            uopt->autoRetrySync = uopt->autoRetrySync ? 0 : 1;
            if (uopt->autoRetrySync && uopt->autoLockSync == 0) {
                uopt->autoLockSync = 1; // pointless without the auto-lock, so arm it at the first point
                saveUserPrefs();
                SerialM.println(F("[3x4x-v123l] auto-retry ON (the auto-lock was off, set to 2 stable reads)"));
            } else {
                SerialM.println(uopt->autoRetrySync
                    ? F("[3x4x-v123l] auto-retry ON: a locked state is let go after ~3.5 s and the next attempt starts by itself. Watch, and press capture when the picture looks right")
                    : F("[3x4x-v123l] auto-retry OFF"));
            }
            break;
        case '\x0b': { // v64: auto-lock point: off -> 2 -> 10 -> 30 -> 62 stable reads
            uopt->autoLockSync = (uint8_t)((uopt->autoLockSync + 1) % 5);
            saveUserPrefs();
            static const uint8_t lockPoints[4] = {2, 10, 30, 62};
            if (uopt->autoLockSync == 0) {
                SerialM.println(F("[3x4x-v123l] auto-lock OFF"));
            } else {
                SerialM.print(F("[3x4x-v123l] auto-lock at "));
                SerialM.print(lockPoints[uopt->autoLockSync - 1]);
                SerialM.println(F(" stable reads (the later the point, the more of the normal setup has run; the earlier, the easier to reach on a source that barely holds)"));
            }
        } break;
        case '\x0a': // v63: tolerant sync on / off
            uopt->tolerantSync = uopt->tolerantSync ? 0 : 1;
            saveUserPrefs();
            SerialM.println(uopt->tolerantSync
                ? F("[3x4x-v123l] tolerant sync ON: short dropouts (under ~0.9 s) only freeze the picture, the preset is kept and the recovery steps (SOG bump, coast, freeze / unfreeze) are held back")
                : F("[3x4x-v123l] tolerant sync OFF (stock behaviour)"));
            break;
        case '\x02':
            requestIntegerLow(1); // v62: 960p x4
            break;
        case '\x03':
            requestIntegerLow(2); // v62: 480p x2
            break;
        case '\x04':
            colorPresetSaveCurrent(); // v62: colour preset of the active console profile
            break;
        case '\x05':
            colorPresetApply();
            break;
        case '\x06':
            colorPresetClear();
            break;
        // v72: ADC black level (analog offset, before digitising). All three together, then per channel.
        case '\x12': nudgeAdcOffset(0, +1); break;
        case '\x13': nudgeAdcOffset(0, -1); break;
        case '\x14': nudgeAdcOffset(1, +1); break;
        case '\x15': nudgeAdcOffset(1, -1); break;
        case '\x16': nudgeAdcOffset(2, +1); break;
        case '\x17': nudgeAdcOffset(2, -1); break;
        case '\x18': nudgeAdcOffset(3, +1); break;
        case '\x19': nudgeAdcOffset(3, -1); break;
        // v74: hue
        case '\x1a': nudgeHue(+1); break;
        case '\x1b': nudgeHue(-1); break;
        case '\x1c': resetHue(); break;
        case '\x82': // v107j: console profile set directly (web codes 42..45): auto, Mega Drive, SNES / NES, PC Engine.
        case '\x83': // The ':' command only steps to the next one, and walking there re-placed the picture at every
        case '\x84': // profile in between.
        case '\x85':
            setConsoleProfileDirect((uint8_t)(argument - '\x82'));
            break;
        // (after v107v) web codes 60..73: the profiles without measurements, 4..17 in his order
        case '\x94': case '\x95': case '\x96': case '\x97': case '\x98':
        case '\x99': case '\x9a': case '\x9b': case '\x9c': case '\x9d':
        case '\x9e': case '\x9f': case '\xa0': case '\xa1':
            setConsoleProfileDirect((uint8_t)(CONSOLE_PROFILE_FIRST_EMPTY + (uint8_t)(argument - '\x94')));
            break;
        // (after v111) Developer, register tests (web codes 77..80): next register, value + / -, undo all
        case '\xa5': regTestNext(); break;
        case '\xa6': regTestStep(+1); break;
        case '\xa7': regTestStep(-1); break;
        case '\xa8': regTestUndo(); break;
        case '\xa9': setOutputRateMode(3); break; // (after v111b) web code 81: output rate «exacta»
        // (after v111c) his report: back from 480i to 240p the FTL settles (deviation 0) with a still tearing line in the
        // middle. Two things to try live, from Developer: a reset of the picture memory, and moving the FTL's target phase.
        case '\xaa': // web code 82
            ResetSDRAM();
            SerialM.println(F("[3x4x-v123l] picture memory reset (ResetSDRAM)"));
            break;
        // (after v111h) his decision of 2026-11-21: the gain his board found is what gets kept, per console
        case '\xad': // web code 85: guardar esta ganancia (and lock it)
            autoGainSaveCurrent();
            break;
        case '\xae': // web code 86: olvidar la ganancia guardada
            autoGainForget();
            break;
        case '\xaf': // (after v120c) web code 87: memoria de fábrica, his request of 2026-11-21
            memSettingsResetMode();
            break;
        case '\xab': // web code 83
        case '\xac': // web code 84
            g_ftlTargetPhase = (int16_t)(g_ftlTargetPhase + (argument == '\xac' ? 15 : -15));
            if (g_ftlTargetPhase < 0) g_ftlTargetPhase += 360;
            if (g_ftlTargetPhase >= 360) g_ftlTargetPhase -= 360;
            SM_PRINTF("[3x4x-v123l] FTL target phase: %d degrees (stock 90; not saved)\n", (int)g_ftlTargetPhase);
            break;
        case '\xa3': // (after v108e) «Otras» (web code 75)
            setConsoleProfileDirect(CONSOLE_PROFILE_OTHERS);
            break;
        case '\xa2': // (after v107z) the Dreamcast in 480i / 576i, 1080p: «4:3» (the whole width) <-> «16:9» (2 x 2) (web code 74)
            if (rto->dcVgaActive && uopt->presetPreference == Output720P) {
                // (after v108f) its 720p too, his report: the shape change reloaded the preset (the picture went and came
                // back). Both shapes share Rama's clock and the vertical, so only the width and canvas change, live.
                uopt->integer720 = (uopt->integer720 == 1) ? 4 : 1;
                uopt->x3Wide = (uopt->integer720 == 4) ? 1 : 0;
                dcVga1080Apply();
                rto->integer720ReapplyAt = millis() + 700;
                saveUserPrefs(); // the shape is a saved preference (the reload path saved it; this one must too)
            } else if (rto->dcVgaActive) { // (after v108c) the Dreamcast through VGA: 4:3 <-> 16:9 (2 x 2, ADC clock lowered)
                uopt->integer1080Wide = uopt->integer1080Wide ? 0 : 1;
                dcVga1080Apply();
                rto->integer720ReapplyAt = millis() + 700;
            } else if (rto->integer1080Active && uopt->integer1080 == 3 && (dcSignal() == 3 || dcSignal() == 4) && !rto->isCustomPreset &&
                !rto->outModeHdBypass) {
                uopt->integer1080Wide = uopt->integer1080Wide ? 0 : 1;
                integer1080Apply(320);
                rto->integer720ReapplyAt = millis() + 700; // the second pass, as after a load
            } else if (md43Able()) {
                // (after v108h) the Mega Drive's 1080p x4: «16:9» <-> «4:3» (for a 16:9 set switched to 4:3 by its remote),
                // in place: same output, and the ADC clock changes in place too where the 256 px of 16:9 needs it
                uopt->integer1080Tv43 = uopt->integer1080Tv43 ? 0 : 1;
                if (rto->presetID == 0x15) {
                    if (uopt->integer1080Tv43 && uopt->sizeStep > 1)
                        uopt->sizeStep = 1; // «4:3» has 256 px and 320 px only
                } else {
                    uopt->integer1080Wide = 0; // NTSC: the fill state has no «4:3»
                }
                const uint16_t w = (rto->presetID == 0x15) ? (uopt->sizeStep == 0 ? 256 : 320)
                                                           : (rto->integer1080Centered == 2 ? 256 : 320);
                rto->integer1080Centered = (w == 256) ? 2 : 1; // (after v123i) before the placement: its border record
                integer1080Apply(w);
                rto->integer720ReapplyAt = millis() + 700; // the second pass, as after a width change
                saveUserPrefs();
                SerialM.println(uopt->integer1080Tv43 ? F("[3x4x-v123l] 1080p: 4:3 (switch the TV to 4:3 with its remote)")
                                                      : F("[3x4x-v123l] 1080p: 16:9")); // (after v123e) not only the MD's x4 now
            } else {
                SerialM.println(F("[3x4x-v123l] 4:3 / 16:9 in 1080p: only for the Dreamcast in 480i / 576i and the Mega Drive in 1080p x4"));
            }
            break;
        case '\x92': // (after v107v) colour and filters of the active console to /colorexp.txt (web code 58)
            colorPresetExportActive();
            break;
        case '\x93': // (after v107v) colour and filters from /colorimp.txt into the console named in it (web code 59)
            colorPresetImportFile();
            break;
        case '\x8f': // (after v107r) deshacer movimiento (web code 55)
            undoMove();
            break;
        case '\x90': // (after v107r) deshacer borde (web code 56), the web asks first: it erases the kept edge
            undoEdge();
            break;
        case '\x91': // (after v107r) deshacer escala (web code 57)
            undoScale();
            break;
        case '\x8e': // v107o: ver huella (web code 54)
            fingerprintReport();
            break;
        case '\x8d': // v107o: 720p x3 in the shape it was last used in (web code 53, the mode button)
            // (after v107z) the Dreamcast in 480i / 576i: only the 4:3 shape (the whole width) exists there
            requestInteger720(uopt->x3Wide ? 4 : 1); // (after v108e) 480i and 576i have their 16:9 now
            break;
        case '\x88': // v107n: Líneas 224 (web code 48)
        case '\x89': // v107n: Líneas 240 / 239 (49)
        case '\x8a': // v107n: Líneas DOLOR (50)
            setLinesMode((uint8_t)(argument - '\x88'));
            break;
        case '\x8b': // v107n: vertical memory window up (51)
            nudgeMemVertical(-1);
            break;
        case '\x8c': // v107n: vertical memory window down (52)
            nudgeMemVertical(+1);
            break;
        case '\x86': // v107m: size step 3, wide (web code 46)
            setSizeStepDirect(2);
            break;
        case '\x87': // v107m: size step 4, fills the screen (web code 47)
            setSizeStepDirect(3);
            break;
        case '\x80': // v107j: step response +1 (web code 40)
            nudgeStepGain(+1);
            break;
        case '\x81': // v107j: step response -1 (web code 41)
            nudgeStepGain(-1);
            break;
        case '\x1f': { // v107c: undo the manual nudges, by placing the mode again from its stored values
            if (rto->isCustomPreset || rto->outModeHdBypass) {
                SerialM.println(F("[3x4x-v123l] undo: not in a custom preset or bypass"));
                break;
            }
            if (!rto->integer720Active && !rto->integer1080Active) {
                SerialM.println(F("[3x4x-v123l] undo: only in the whole-pixel modes"));
                break;
            }
            // (after v107q) his decision of 2026-11-13: deshacer is literally going back to the firmware's values, the edge
            // included, so the edge kept for this mode is erased (the web asks first). There is no separate "borde
            // original" button: this is it.
            edgesResetMode();
            integerProfileRefresh();
            SerialM.println(F("[3x4x-v123l] back to the saved placement of this mode"));
            break;
        }
        case '\x1e': { // v103: the PAL 720p x5 oversampling experiment. v107h: removed (it corrupted the picture); the
                        // code is kept unreachable so it can be brought back, and a stored "on" is cleared at boot
            uopt->x5Oversample = 0;
            SerialM.println(F("[3x4x-v123l] the x5 oversampling experiment has been removed"));
            break;
        }
        case '\x1d': { // v84: cycle the SDRAM clock phase, 0..15. v88: stored in the slot of the mode on screen
            // v107m: stored per mode AND size step, and counted from the phase in use. It used to count from the stored
            // byte, which is 0 while the step runs on its measured phase, so the first press jumped to 1 instead of to
            // the next phase after the one on screen.
            uint8_t *own = memPhaseOwn();
            const uint8_t curPhase = (uint8_t)((GBS::MEM_INTER_DLYCELL_SEL::read() << 0) | (GBS::MEM_CLK_DLYCELL_SEL::read() << 1) |
                                               (GBS::MEM_FBK_CLK_DLYCELL_SEL::read() << 2) | (GBS::MEM_PAD_CLK_INVERT::read() << 3));
            const uint8_t next = (uint8_t)(((own ? curPhase : uopt->memPhase) + 1) & 0x0f);
            if (own)
                *own = next;
            uopt->memPhase = next; // kept in step for the old global path
            applyMemSettings();
            // v107m: and the phase itself, always: applyMemSettings() leaves phase 0 alone ("0 = no setting of its own"),
            // so walking from 15 to 0 used to leave the chip on 15
            GBS::MEM_INTER_DLYCELL_SEL::write((next >> 0) & 1);
            GBS::MEM_CLK_DLYCELL_SEL::write((next >> 1) & 1);
            GBS::MEM_FBK_CLK_DLYCELL_SEL::write((next >> 2) & 1);
            GBS::MEM_PAD_CLK_INVERT::write((next >> 3) & 1);
            ResetSDRAM();
            scanlinesAfterSdramReset(); // v92
            memSettingsSave(); // (after v120c) per console, so the other consoles are left alone
            saveUserPrefs();
            SerialM.print(F("[3x4x-v123l] SDRAM clock phase "));
            SerialM.print(next);
            SerialM.print(F("/15 for "));
            SerialM.print(memModeName());
            SerialM.println(F(" (phase, not frequency: try these before changing the clock)"));
            break;
        }
        case '\x01':
            requestInteger720(4); // v55: 720p x3 16:9 (sent by the /img channel, code 10)
            break;
        case '>':
            // v47c: 720p x5 ('@' cannot be used: it is the "no command" marker of the main loop, so the web button did nothing)
            requestInteger720(2);
            break;
        case '`':
            adjustIfHbin(false); // v49
            break;
        case '\'':
            adjustIfHbin(true); // v49
            break;
        case '|':
            requestInteger720(3); // v48: 720p x4 ('|' is percent-encoded by the browser like '>' and '<')
            break;
        case '<':
            cycleFill(); // v58: 3-state fill cycle
            break;
        case ':':
            // v43: console profile: auto / Mega Drive / SNES + NES (only moves the 256 px placement of the integer modes)
            uopt->consoleProfile = (uint8_t)((uopt->consoleProfile + 1) % 4); // v62: auto, Mega Drive, SNES / NES, PC Engine
            saveUserPrefs();
            SerialM.print(F("Console profile: "));
            SerialM.println(uopt->consoleProfile == 0 ? F("auto (by field rate)") : (uopt->consoleProfile == 1 ? F("Mega Drive") : (uopt->consoleProfile == 2 ? F("SNES / NES") : F("PC Engine"))));
            integerProfileRefresh();
            break;
        case '8':
            // [patch fine-scale]
            scaleHorizontalFine = !scaleHorizontalFine;
            SerialM.print(F("Fine horizontal scale: "));
            SerialM.println(scaleHorizontalFine ? F("on (steps of 1)") : F("off (steps of 2)"));
            break;
        case 'W':
            cycleAdcFilterLevel(); // [patch image-tuning]
            break;
        case 'y':
            stepManualPhaseAdc(+1); // [patch image-tuning]
            break;
        case 'Y':
            stepManualPhaseAdc(-1);
            break;
        case 'I':
            resetManualPhaseAdc();
            break;
        case 'Z':
            // brightness++
            GBS::VDS_Y_OFST::write(GBS::VDS_Y_OFST::read() + 1);
            SerialM.print(F("Brightness++ : "));
            SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
            break;
        case 'T':
            // brightness--
            GBS::VDS_Y_OFST::write(GBS::VDS_Y_OFST::read() - 1);
            SerialM.print(F("Brightness-- : "));
            SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
        break;
        case 'N':
            // contrast++
            GBS::VDS_Y_GAIN::write(GBS::VDS_Y_GAIN::read() + 1);
            SerialM.print(F("Contrast++ : "));
            SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
        break;
        case 'M':
            // contrast--
            GBS::VDS_Y_GAIN::write(GBS::VDS_Y_GAIN::read() - 1);
            SerialM.print(F("Contrast-- : "));
            SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
        break;
        case 'Q':
             // pb/u gain++  (v74: through the colour matrix, so a hue rotation is not thrown away)
            nudgeSaturation(1, +1);
            break;
        case 'H':
             // pb/u gain--
            nudgeSaturation(1, -1);
            break;
        break;
        case 'P':
            // pr/v gain++
            nudgeSaturation(2, +1);
            break;
        case 'S':
            // pr/v gain--
            nudgeSaturation(2, -1);
            break;
        case '/': // v52: saturation + (Pb/U and Pr/V together)
            nudgeSaturation(0, +1);
            break;
        case '"': // v52: saturation -
            nudgeSaturation(0, -1);
            break;
        case '{': // v52: black level +
            nudgeBlackLevel(+1);
            break;
        case '}': // v52: black level -
            nudgeBlackLevel(-1);
            break;
        case '\\': // v52: peaking fine +
            nudgePeaking(+1);
            break;
        case '?': // v52: peaking fine -
            nudgePeaking(-1);
            break;
        case 'O':
            // info
            if (GBS::ADC_INPUT_SEL::read() == 1)
            {
                SerialM.println(F("RGB reg"));
                SerialM.println(F("------------ "));
                SerialM.print(F("Y_OFFSET: "));
                SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
                SerialM.print(F("U_OFFSET: "));
                SerialM.println( GBS::VDS_U_OFST::read(), DEC);
                SerialM.print(F("V_OFFSET: "));
                SerialM.println(GBS::VDS_V_OFST::read(), DEC);
                SerialM.print(F("Y_GAIN: "));
                SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
                SerialM.print(F("USIN_GAIN: "));
                SerialM.println(GBS::VDS_USIN_GAIN::read(), DEC);
                SerialM.print(F("UCOS_GAIN: "));
                SerialM.println(GBS::VDS_UCOS_GAIN::read(), DEC);
            }
            else
            {
                SerialM.println(F("YPbPr reg"));
                SerialM.println(F("------------ "));
                SerialM.print(F("Y_OFFSET: "));
                SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
                SerialM.print(F("U_OFFSET: "));
                SerialM.println( GBS::VDS_U_OFST::read(), DEC);
                SerialM.print(F("V_OFFSET: "));
                SerialM.println(GBS::VDS_V_OFST::read(), DEC);
                SerialM.print(F("Y_GAIN: "));
                SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
                SerialM.print(F("USIN_GAIN: "));
                SerialM.println(GBS::VDS_USIN_GAIN::read(), DEC);
                SerialM.print(F("UCOS_GAIN: "));
                SerialM.println(GBS::VDS_UCOS_GAIN::read(), DEC);
            }
            break;
        case 'U':
            // default
            if (GBS::ADC_INPUT_SEL::read() == 1)
            {
                GBS::VDS_Y_GAIN::write(128);
                GBS::VDS_UCOS_GAIN::write(28);
                GBS::VDS_VCOS_GAIN::write(41);
                GBS::VDS_Y_OFST::write(0);
                GBS::VDS_U_OFST::write(0);
                GBS::VDS_V_OFST::write(0);
                GBS::ADC_ROFCTRL::write(adco->r_off);
                GBS::ADC_GOFCTRL::write(adco->g_off);
                GBS::ADC_BOFCTRL::write(adco->b_off);
                resetImageFine(); // v52: also peaking and black level
                SerialM.println(F("RGB:defauit"));
            }
            else
            {
                GBS::VDS_Y_GAIN::write(128);
                GBS::VDS_UCOS_GAIN::write(28);
                GBS::VDS_VCOS_GAIN::write(41);
                GBS::VDS_Y_OFST::write(254);
                GBS::VDS_U_OFST::write(3);
                GBS::VDS_V_OFST::write(3);
                GBS::ADC_ROFCTRL::write(adco->r_off);
                GBS::ADC_GOFCTRL::write(adco->g_off);
                GBS::ADC_BOFCTRL::write(adco->b_off);
                resetImageFine(); // v52
                SerialM.println(F("YPbPr:defauit"));
            }
            break;
        default:
            break;
    }
}

