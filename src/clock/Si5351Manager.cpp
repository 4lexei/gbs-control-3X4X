#include "Si5351Manager.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSVideoProcessing.h" // (after v111f)
#include "../utils/DebugHelpers.h"
#include "../gbs/tv5725.h"
#include "../video/framesync.h"
#include "../video/Integer1080.h"
#include "../video/ConsoleModes.h"
#include "../video/ColorPresets.h"
#include "../video/SyncWatcher.h"
#include "../video/VideoScaling.h"
#include "../video/ImageTuning.h"
#include "../../lib/si5351mcu/si5351mcu.h"

typedef TV5725<GBS_ADDR> GBS;

void externalClockGenResetClock()
{
    if (!rto->extClockGenDetected) {
        return;
    }
    fsDebugPrintf("externalClockGenResetClock()\n");

    auto readSi5351Reg = [](uint8_t reg) -> int {
        Wire.beginTransmission(SIADDR);
        Wire.write(reg);
        if (Wire.endTransmission() != 0) return -1;
        size_t n = Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n != 1) return -2;
        return Wire.read();
    };
    auto writeSi5351Reg = [](uint8_t reg, uint8_t val) -> bool {
        Wire.beginTransmission(SIADDR);
        Wire.write(reg);
        Wire.write(val);
        return Wire.endTransmission() == 0;
    };
    auto waitSi5351Lock = [&](const char *where) -> bool {
        uint32_t t0 = millis();
        uint32_t lastLog = 0;
        int st = -999;
        // Wait longer on ESP32 after reprogramming PLL/MS for 108MHz.
        while (millis() - t0 < 250) {
            st = readSi5351Reg(0x00);     // device status

            uint32_t elapsed = millis() - t0;
            if (elapsed == 0 || elapsed - lastLog >= 25) {
                lastLog = elapsed;

            }

            // We only use CLK0 (PLLA) in this firmware; some modules report LOLB even if PLLB is unused.
            // Treat LOLA as the lock indicator; LOS may remain asserted on some modules.
            if (st >= 0 && (st & 0x20) == 0 && (st & 0x80) == 0) {
                return true;
            }
            delay(1);
        }
        return false;
    };

    uint8_t activeDisplayClock = GBS::PLL648_CONTROL_01::read();

    if (activeDisplayClock == 0x25)
        rto->freqExtClockGen = 40500000;
    else if (activeDisplayClock == 0x45)
        rto->freqExtClockGen = 54000000;
    else if (activeDisplayClock == 0x55)
        rto->freqExtClockGen = 64800000;
    else if (activeDisplayClock == 0x65)
        rto->freqExtClockGen = 81000000;
    else if (activeDisplayClock == 0x85)
        rto->freqExtClockGen = 108000000;
    else if (activeDisplayClock == 0x95)
        rto->freqExtClockGen = 129600000;
    else if (activeDisplayClock == 0xa5)
        rto->freqExtClockGen = 162000000;
    else if (activeDisplayClock == 0x35)
        rto->freqExtClockGen = 81000000; // clock unused
    else if (activeDisplayClock == 0)
        rto->freqExtClockGen = 81000000; // no preset loaded
    else if (!rto->outModeHdBypass) {
        SerialM.print(F("preset display clock: 0x"));
        SerialM.println(activeDisplayClock, HEX);
    }

    // NOTE: Historically there was a "pre-step" workaround for 108MHz/40.5MHz.
    // On ESP32 this can interact badly with Si5351 lock; we'll skip the pre-step
    // and rely on direct programming
#ifndef ESP32
    // problem: around 108MHz the library seems to double the clock
    if (rto->freqExtClockGen == 108000000) {
        Si.setFreq(0, 87000000);
        delay(1); // quick fix
    }
    // same thing it seems at 40500000
    if (rto->freqExtClockGen == 40500000) {
        Si.setFreq(0, 48500000);
        delay(1); // quick fix
    }
#endif
    // Re-apply best XTAL_CL discovered during init before programming new freq.
    writeSi5351Reg(183, g_si5351_best_xtal_cl);
    (void)readSi5351Reg(183);

    // Keep CKIN disabled while (re)programming the Si5351 to avoid loading the output
    // during the critical lock window. We'll only enable CKIN after we confirm lock.
    GBS::PAD_CKIN_ENZ::write(1); // 1 = clock input disable (pin40)

    Si.setFreq(0, rto->freqExtClockGen);
    // Mirror init behavior: enable output and clear interrupt flags before waiting for lock.
    Si.enable(0);
    writeSi5351Reg(0x01, 0xFF); // clear sticky interrupt status
    (void)readSi5351Reg(0x00);
    (void)readSi5351Reg(0x01);
    FrameSync::clearFrequency();

    SerialM.print(F("clock gen reset: "));
    SerialM.println(rto->freqExtClockGen);

    // If Si5351 isn't locked yet, it will continue to try and lock in the background.
    // We keep CKIN enabled (0) because the user wants to stay on the external clock regardless.
    bool locked = waitSi5351Lock("externalClockGenResetClock");
    if (!locked) {
        // Try a PLL reset once, then wait again.
        Si5351mcu::reset();
        locked = waitSi5351Lock("externalClockGenResetClock_after_reset");
    }
    
    GBS::PAD_CKIN_ENZ::write(0); // 0 = clock input enable (pin40)
    
    if (locked) {
        (void)readSi5351Reg(0x00);
    }
    (void)writeSi5351Reg(0x01, 0xFF);
    (void)readSi5351Reg(0x00);
}

// [patch output-rate] Returns true when nothing (more) needs to be done, false while the clock is
// still being steered towards its target (or the measurement failed).
// v37: informational console guess from the input frame rate (no behaviour change). Only rates that are
// unambiguous are named; 59.94 / 60.10 style rates are shared by several consoles and stay unnamed.
// (after v107y) the sync pulse as a share of the line, x10000 (the "hsr" of the fingerprint): the middle of five readings,
// so one odd sample cannot decide it. It does not depend on the mode on screen (the pulse and the line are counted in the
// same clock).
static uint32_t syncPulseShare()
{
    uint16_t hsv[5];
    const uint16_t htv = GBS::STATUS_SYNC_PROC_HTOTAL::read();
    for (uint8_t i = 0; i < 5; i++) {
        hsv[i] = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
        delay(1);
    }
    for (uint8_t i = 1; i < 5; i++) // five values: insertion sort, then the middle one
        for (uint8_t k = i; k > 0 && hsv[k - 1] > hsv[k]; k--) {
            const uint16_t t = hsv[k];
            hsv[k] = hsv[k - 1];
            hsv[k - 1] = t;
        }
    return htv ? ((uint32_t)hsv[2] * 10000u) / htv : 0;
}

static const char *consoleGuess(float sfr)
{
    if (sfr > 59.905f && sfr < 59.930f) return "Mega Drive NTSC";
    if (sfr > 59.80f && sfr < 59.84f) return "PC Engine";
    if (sfr > 49.60f && sfr < 49.80f) return "Mega Drive PAL";
    if (sfr > 50.12f && sfr < 50.22f) return "Mega Drive NTSC @50Hz";
    if (sfr > 59.35f && sfr < 59.45f) return "Mega Drive PAL @60Hz";
    // v39, measured by the user: SNES / NES PAL 50.0073 Hz, SNES PAL forced to 60 Hz 59.55 Hz, SNES / NES NTSC ~60.10 Hz
    if (sfr > 49.995f && sfr < 50.02f) return "SNES / NES PAL";
    if (sfr > 59.50f && sfr < 59.60f) return "SNES PAL @60Hz";
    if (sfr > 60.08f && sfr < 60.12f) return "SNES / NES NTSC";
    return "";
}

// v37: status bar line for the web UI
// v90: THREE lines, as the user asked, because one long line of pipes is unreadable at a glance:
//   1  the rates: in, out, deviation, and the sync flags, which belong with the rates
//   2  the consoles: the one detected from the field rate, and the profile actually in use
//   3  the picture: resolution, mode, aspect and size step
// The web UI renders this with white-space: pre-line, and markActiveModes() matches on substrings of it, so the tokens
// "720p x3", "720p x4", "720p x5", "1080p x4", "1080p x5", "16:9" and "step n/4" must survive any rewording here.
static void sendStatusBar(float sfr, float ofr)
{
    static char line[800]; // (after v111b) 700 -> 800 // v107j / v107l: 260 -> 600 for the data lines; static so it stays off the stack (not re-entered)
    const bool anyMode = rto->integer1080Active || rto->integer720Active || rto->integerLowActive;

    // ---- line 1: rates and sync state
    const char *tol = uopt->tolerantSync ? " | tol.sync" : ""; // v63
    const char *wat = uopt->autoRetrySync ? " | retrying" : (rto->syncWatcherEnabled ? "" : " | watcher off"); // v64 / v66
    char stats[40] = "";
    if (rto->statLosses || rto->statRetries)
        snprintf(stats, sizeof(stats), " | losses %u retries %u", (unsigned)rto->statLosses, (unsigned)rto->statRetries);

    // ---- line 2: console detected, profile selected
    const char *guess = consoleGuess(sfr);
    // (after v107w) the guess reads the field rate only; the detection now also looks at interlacing, so where they part
    // the bar follows the detection: the Dreamcast by name, and nothing where an interlaced source was refused
    if (rto->consoleDetectIsFresh) {
        if (rto->consoleDetected == 3)
            guess = !rto->syncTypeCsync ? "Dreamcast VGA" : ((sfr < 49.95f) ? "Dreamcast 288p" : ((sfr < 55.0f) ? "Dreamcast PAL" : (sfr < 59.9f ? "Dreamcast 240p" : "Dreamcast @60Hz")));
        else if (rto->consoleDetected == 0 && !rto->consoleIsMd)
            guess = "";
        // (after v108h) a Mega Drive or SNES kept through an interlaced game (Sonic 2 for two players): its rate reads as
        // another console's (59.81 is the PC Engine window), so the bar names the console the detection kept
        else if (rto->consoleIsMd && strncmp(guess, "Mega Drive", 10) != 0)
            guess = "Mega Drive (interlaced)";
        else if (rto->consoleDetected == 1 && !rto->consoleIsMd && strncmp(guess, "SNES", 4) != 0)
            guess = "SNES (interlaced)";
    }
    char prof[40] = "";
    // v62: PC Engine has a profile of its own (Mega Drive values for now), so the bar names it instead of MD
    { // v107j: always, not only in the whole-pixel modes: the web shows the profile as a choice, and it also picks the colour preset
        const char *pname = "MD";
        switch (activeConsoleProfile()) {
            case 2: pname = "SNES"; break;
            case 3: pname = "PC-Engine"; break;
            default: break;
        }
        // (after v107v) a profile without measurements is named as it is (its placement is the Mega Drive's: the web
        // treats these names like "MD" for the width buttons)
        if (uopt->consoleProfile >= CONSOLE_PROFILE_FIRST_EMPTY)
            pname = consoleProfileName(uopt->consoleProfile);
        else if (uopt->consoleProfile == 0 && settingsProfile() >= CONSOLE_PROFILE_FIRST_EMPTY)
            pname = consoleProfileName(settingsProfile()); // (after v107w) "profile auto Dreamcast"
        snprintf(prof, sizeof(prof), "profile %s%s", uopt->consoleProfile == 0 ? "auto " : "", pname);
    }

    // ---- line 3: resolution, mode, aspect, step
    const char *mode = "";
    const char *aspect = "";
    if (rto->integer1080Active) {
        mode = (uopt->integer1080 == 4) ? "1080p x5" : "1080p x4";
        if (md43Able()) // (after v108h) the Mega Drive's 1080p x4 has the two shapes, as 720p x3
            aspect = tv43Active() ? " | 4:3" : " | 16:9";
    } else if (rto->integer720Active) {
        mode = (uopt->integer720 == 2) ? "720p x5" : (uopt->integer720 == 3) ? "720p x4" : "720p x3";
        // v107f: only x3 has a 4:3 version (integer720 == 1). x4 and x5 are 16:9 modes; they were labelled 4:3 here, so the
        // web screen looked for "720p x4 4:3", found no description, and showed none.
        aspect = (uopt->integer720 == 1) ? " | 4:3" : " | 16:9";
    } else if (rto->integerLowActive) {
        mode = (uopt->integerLow == 2) ? "480p x2" : "960p x4"; // v62
    } else if (rto->dcVgaActive) {
        if (uopt->presetPreference == Output720P) { // (after v108d) its 720p
            mode = "720p x3";
            aspect = (uopt->integer720 == 1) ? " | 4:3" : " | 16:9";
        } else {
            mode = "1080p x4"; // (after v108b) the Dreamcast through VGA: the web names it «1080p»
            aspect = uopt->integer1080Wide ? " | 4:3" : " | 16:9";
        }
    }
    // v88: in the PAL presets the width is the SIZE STEP, not the 256 / 320 pair. The two width buttons and the fill button
    // write uopt->sizeStep and return before rto->integer1080Centered is touched, so this used to show whatever that old
    // variable still held: two dumps of two different steps both came out labelled "256 px". Name the step itself.
    char width[32] = "";
    if (rto->presetID == 0x13 || rto->presetID == 0x15) {
        static const char *stepName[4] = {"square", "CRT", "wide", "fill"};
        // v107f: 720p x3 4:3 and x5 PAL have two sizes only (256 and CRT), so they count to 2
        // v107q: and the Mega Drive in 720p x4 (256 and 320 only, his decision)
        const bool two = (rto->presetID == 0x13 && rto->integer720Active && (uopt->integer720 == 1 || uopt->integer720 == 2)) ||
                         sizeStepsTwoMd() || md43Able(); // (after v108h) the Mega Drive's 1080p x4 «4:3»: 256 and 320; (after v111f) its 16:9 too (no «ancho»)
        // v107q: and the Mega Drive in PAL 1080p x4 has three (no fill step: its line does not fit stretched to 1920)
        const bool three = !two && sizeStepsThree();
        uint8_t st = two ? ((uopt->sizeStep & 3) ? 1 : 0) : (uopt->sizeStep & 3);
        if (three && st > 2)
            st = 2;
        snprintf(width, sizeof(width), " | step %u/%u %s", (unsigned)(st + 1), two ? 2u : (three ? 3u : 4u), stepName[st]);
    }
    // (after v107z) the Dreamcast's own modes: no size steps and no 256 / 320, the signal instead; in 1080p 480i its shape
    const uint8_t dcs = dcSignal();
    if (rto->dcVgaActive) {
        snprintf(width, sizeof(width), " | dc vga");
    } else if (anyMode && dcs >= 1 && dcs <= 4) {
        static const char *dcName[5] = {"", "dc 240p", "dc 288p", "dc 480i", "dc 576i"};
        snprintf(width, sizeof(width), " | %s", dcName[dcs]);
        if (rto->integer1080Active && (dcs == 3 || dcs == 4))
            aspect = uopt->integer1080Wide ? " | 4:3" : " | 16:9";
    } else if (anyMode && !(rto->presetID == 0x13 || rto->presetID == 0x15)) {
        const bool wide = rto->integer1080Active ? (uopt->integer1080Wide != 0) : (uopt->integer720 == 4 && uopt->integer720Fill != 0);
        snprintf(width, sizeof(width), " | %s", wide ? "wide" : (rto->integer1080Centered == 2 ? "256 px" : "320 px"));
    }

    // (after v111d) built straight after the 0x1e marker and sent from here: SerialMirror::sendStatus copied it into a
    // second buffer of 820 bytes just to put the marker in front
    line[0] = '\x1e';
    int len = snprintf(line + 1, sizeof(line) - 1,
        "IN %.4fHz | OUT %.4fHz | dev %+.4f%s%s%s\n"
        "detected %s%s%s\n"
        "%s%s%s%s\n"
        "mem %u %u sl %u fh %u\n",
        sfr, ofr, ofr - sfr, tol, wat, stats,
        guess[0] ? guess : "not yet", prof[0] ? " | " : "", prof,
        mode[0] ? mode : "stock preset", aspect, width,
        scaleHorizontalFine ? " | fine" : "", // v107h: the web's fine-step switch needs to know its real state
        // v107j: a fourth line the web reads and does not show: the SDRAM clock in MHz (0 = feedback clock) and the
        // phase 0..15, so the two memory buttons can say what they are set to without anyone reading the log
        (unsigned)memClockMhzFor(GBS::PLL_MS::read()),
        (unsigned)((GBS::MEM_INTER_DLYCELL_SEL::read() << 0) | (GBS::MEM_CLK_DLYCELL_SEL::read() << 1) |
                   (GBS::MEM_FBK_CLK_DLYCELL_SEL::read() << 2) | (GBS::MEM_PAD_CLK_INVERT::read() << 3)),
        // v107j: and the scanline intensity, 0..10, worked out exactly as the log line of stepScanlineStrength() does
        (unsigned)((0x50 - ((uopt->scanlineStrength / 8) * 8)) / 8),
        (unsigned)ESP.getFreeHeap()); // (after v111c) free memory, for the diagnostics
    if (len < 0)
        return;
    if ((size_t)len > sizeof(line) - 2)
        len = (int)sizeof(line) - 2;
    // v107j: line 5, the picture values for the web (Color and Filtros tabs). (after v111e) written straight into the line,
    // after the four lines above (it had a buffer of its own, 480 bytes of RAM)
    char *vals = line + 1 + len;
    const size_t valsRoom = sizeof(line) - 1 - (size_t)len;
    imageTuningReport(vals, valsRoom);
    // (after v107z) for the web: what the Dreamcast is sending (dcSignal), so the Modo tab shows only its measured modes
    if (dcs) {
        const size_t used = strlen(vals);
        if (used + 8 < valsRoom)
            snprintf(vals + used, valsRoom - used, " dc=%u", (unsigned)dcs);
    }
    // (after v111c) his report: the web stopped answering («Handler did not handle the request» = under 10 KB free).
    // The data line is the long part of the bar (300-500 bytes) and almost always the same, so it goes only when it
    // changed, when the number of open pages changed, or every 15 s; the web keeps the last one it got.
    // (after v111d) compared by a checksum, not a copy
    uint32_t valsHash = 2166136261u;
    for (const char *c = vals; *c; c++)
        valsHash = (valsHash ^ (uint8_t)*c) * 16777619u;
    static uint32_t lastValsHash = 0;
    static unsigned long lastValsSent = 0;
    static uint8_t lastClients = 0;
    const uint8_t clients = (uint8_t)webSocket.connectedClients();
    const bool sendVals = valsHash != lastValsHash || clients != lastClients || millis() - lastValsSent > 15000;
    if (sendVals) {
        lastValsHash = valsHash;
        lastValsSent = millis();
        lastClients = clients;
    } else {
        vals[0] = 0; // the four lines only
    }
    if (ESP.getFreeHeap() >= 10500) // (after v111e) the bar's margin, see SerialMirror.cpp
        webSocket.broadcastTXT((uint8_t *)line, strlen(line));
}

static bool externalClockGenSyncImpl()
{
    fsDebugPrintf("externalClockGenSyncInOutRate()\n");

    if (!rto->extClockGenDetected) {
        return true;
    }
    if (GBS::PAD_CKIN_ENZ::read() != 0) {
        return true;
    }
    if (rto->outModeHdBypass) {
        return true;
    }
    if (GBS::PLL648_CONTROL_01::read() != 0x75) {
        return true;
    }

    // When FTL is enabled and the PLL is fully operational,
    // runFrequency() handles clock tracking. Skip coarse sync to avoid
    // disrupting it. Sync still runs when: FTL off, PLL not yet locked,
    // or frequency mapping not yet initialized (after preset change).
    // (Not applicable with a fixed output rate: runFrequency() is not used then.)
    if (uopt->outputRateMode == 0 && uopt->enableFrameTimeLock && FrameSync::frequencyReady()) {
        fsDebugPrintf("sync skipped: FTL PLL active\n");
        return true;
    }

    float sfr = getSourceFieldRate(0);

    if (sfr < 47.0f || sfr > 86.0f) {
        SerialM.print(F("sync skipped sfr wrong: "));
        SerialM.println(sfr);
        return false;
    }

    float ofr = getOutputFrameRate();

    if (ofr < 47.0f || ofr > 86.0f) {
        SerialM.print(F("sync skipped ofr wrong: "));
        SerialM.println(ofr);
        return false;
    }

    // EMA smoothing on sfr to filter ISR measurement jitter.
    // Re-seed when sfr jumps >2Hz (mode change, not jitter).
    static float smoothedSfr = 0.0f;
    constexpr float EMA_ALPHA = 0.3f;
    // (after v111f) 2 Hz -> 0.05 Hz: progressive <-> interlaced moves the rate by ~0.11 Hz (59.92 <-> 59.81), and the
    // blend made the steering walk there in 25 kHz steps from a rate that was already gone (his log of 2026-11-18)
    if (smoothedSfr < 47.0f || fabsf(sfr - smoothedSfr) > 0.05f) {
        smoothedSfr = sfr; // initialize or re-seed after mode change
    }
    smoothedSfr = EMA_ALPHA * sfr + (1.0f - EMA_ALPHA) * smoothedSfr;

    // [patch output-rate] pick the rate the output should run at
    float targetRate = smoothedSfr; // 0: follow the source
    bool fixedRate = false;
    if (uopt->outputRateMode == 3) {
        // (after v111b) his exact rate, when the source is near it (within 1.5 Hz); otherwise the source is followed, so
        // a 50 Hz console never gets a 60 Hz rate meant for another one
        const float exact = uopt->outputRateExact / 10000.0f;
        if (uopt->outputRateExact && fabsf(smoothedSfr - exact) < 1.5f) {
            targetRate = exact;
            fixedRate = true;
        }
    } else if (uopt->outputRateMode != 0) {
        if (fabsf(smoothedSfr - 60.0f) < 1.0f) {
            targetRate = (uopt->outputRateMode == 1) ? 59.94f : 60.0f;
            fixedRate = true;
        } else if (fabsf(smoothedSfr - 50.0f) < 1.0f) {
            targetRate = 50.0f;
            fixedRate = true;
        }
    }
    (void)fixedRate;
    // (after v108h) his report of 2026-11-17: set to 60.00 Hz, a Mega Drive (59.92) stopped at 59.9772, and 59.94 did
    // nothing at all, because 0.03 Hz was taken as "on target". The output rate is timed with the CPU clock over three
    // samples with the odd one dropped, so its error is a few thousandths at most: 0.005 Hz now.
    if (fabsf(ofr - targetRate) < 0.005f) {
        // already on target (within measurement noise): leave the clock alone, but keep the
        // frequency mapping used by runFrequency() initialized
        FrameSync::initFrequency(ofr, rto->freqExtClockGen);
        return true;
    }

    float ratio = (ofr > 0.0f) ? (targetRate / ofr) : 1.0f;

    uint32_t old = rto->freqExtClockGen;
    uint32_t newClockFreq = ratio * old;
    int32_t delta = (int32_t)newClockFreq - (int32_t)old;

    // Clamp delta — large changes go through externalClockGenResetClock().
    constexpr int32_t MAX_DELTA = 25000; // 25kHz per cycle
    if (delta > MAX_DELTA) delta = MAX_DELTA;
    if (delta < -MAX_DELTA) delta = -MAX_DELTA;
    newClockFreq = old + delta;

    SM_PRINTF("sync: sfr=%.2fHz(%.2f) ofr=%.2fHz target=%.2fHz delta=%.1fkHz\n",
        sfr, smoothedSfr, ofr, targetRate, delta / 1000.0f);

    Si.setFreq(0, newClockFreq);
    rto->freqExtClockGen = newClockFreq;
    // (after v111b) the clock per output frame is the clock the output rate was MEASURED with, over that rate (stock
    // code paired the old rate with the new clock: up to 0.03 % off, half of what the frame time lock may correct)
    FrameSync::initFrequency(ofr, old);

    SM_PRINTF("sync done: clock=%.2fMHz (%+.1fkHz)\n",
        rto->freqExtClockGen / 1000000.0f, delta / 1000.0f);
    // Allow Si5351 output to settle before next sync cycle.
    delay(1);
    return false; // measure again to confirm
}

// v39: the status bar is sent from the main loop once per second (v37 / v38 only sent it while the clock sync ran, so
// a page opened later stayed on "waiting for status"). Only measures while a web page is connected.
void statusBarTick()
{
    static unsigned long last = 0;
    static float smoothed = 0.0f;
    // v107p: the two rate measurements are the expensive part of this routine (each one times a whole frame, 17-40 ms with
    // the main loop held), so with a page open they now take turns every 3 s instead of both running every second: input
    // on tick 0, output on tick 1, nothing on tick 2. The bar itself still goes out every second with the last values,
    // because its data lines are what the buttons show their new value from. A new source is measured at once.
    static uint8_t turn = 0;
    static float lastOfr = 0.0f;
    unsigned long now = millis();
    if (now - last < 1000) {
        return;
    }
    last = now;
    // v107p: THE DETECTION NO LONGER NEEDS THE WEB. It lived inside the status bar routine, which returned straight away
    // with no page connected, so without the web the console was never identified and the picture memory of the console
    // never came in. Measuring the field rate is not free (a few frames timed on the test bus, which the frame time lock
    // also uses), so without a page it is measured only while the source has not been identified yet: once per new
    // source, as soon as the picture is steady. With a page open it runs for the bar (see the 3 s turns above).
    const bool web = (webSocket.connectedClients() != 0);
    if (!web && rto->consoleDetectIsFresh) {
        return;
    }
    // v67 / v69: with the watcher stopped the stable count freezes and with it the bar and the console detection. The
    // measurement only reads the field rate, so it is allowed whenever a picture is up.
    const bool held = syncHeldWithPicture();
    if (!rto->boardHasPower || rto->sourceDisconnected || rto->noSyncCounter != 0 ||
        (rto->continousStableCounter <= 20 && !held)) {
        smoothed = 0.0f;
        lastOfr = 0.0f;
        turn = 0;
        if (web)
            SerialM.sendStatus("No signal / syncing...");
        return;
    }
    const bool measureIn = (smoothed < 10.0f) || !rto->consoleDetectIsFresh || (turn == 0);
    if (measureIn) {
    float sfr = getSourceFieldRate(0);
    if (sfr < 10.0f || sfr > 100.0f) {
        if (web)
            SerialM.sendStatus("Measuring...");
        return;
    }
    if (smoothed < 10.0f || fabsf(sfr - smoothed) > 0.5f) {
        smoothed = sfr; // first value or mode change
    }
    // (after v108h) his report of 2026-11-17: Sonic 2 in two-player mode turns the Mega Drive interlaced, and the field
    // rate drops from 59.92 to 59.81 Hz (half a line more per field). Blended slowly, the rate crossed the PC Engine window
    // on the way down and on the way back. So when a 15 kHz source changes between progressive and interlaced (odd or
    // even line count), the new rate is taken at once instead of being blended with the old one.
    if (rto->videoStandardInput == 1 || rto->videoStandardInput == 2) {
        static int8_t lastParity = -1;
        const int8_t parity = (int8_t)(GBS::VPERIOD_IF::read() % 2);
        if (lastParity >= 0 && parity != lastParity)
            smoothed = sfr;
        lastParity = parity;
    }
    smoothed = 0.3f * sfr + 0.7f * smoothed;
    // v43: SNES / NES detection from the field rate (user measurements: NTSC 60.10 Hz, PAL forced to NTSC 59.55 Hz, PAL 50.007 Hz).
    // Everything else (Mega Drive 59.92, PC Engine / PSX / N64 59.82, ...) uses the default placement.
    {
        // v62: 1 = SNES / NES, 2 = PC Engine (59.80 - 59.84 Hz, measured by the user), 0 = anything else (Mega Drive values)
        uint8_t detected = ((smoothed > 60.08f && smoothed < 60.12f) || (smoothed > 59.50f && smoothed < 59.60f) || (smoothed > 49.995f && smoothed < 50.02f)) ? 1 : 0;
        if (!detected && smoothed > 59.80f && smoothed < 59.84f)
            detected = 2;
        const bool wasStale = !rto->consoleDetectIsFresh;
        // v107p: the Mega Drive is now told apart from "anything else", for the picture memory of each console: an unknown
        // console gets a memory of its own instead of writing over the Mega Drive's. Same windows as consoleGuess().
        const uint8_t md = ((smoothed > 59.905f && smoothed < 59.930f) || (smoothed > 49.60f && smoothed < 49.80f) ||
                            (smoothed > 50.12f && smoothed < 50.22f) || (smoothed > 59.35f && smoothed < 59.45f)) ? 1 : 0;
        uint8_t mdNow = md;
        // (after v107w) INTERLACED SOURCES, his decision of 2026-11-15. The input formatter counts the lines of a frame: an
        // even count (524, 624) is a 480i / 576i source, an odd one a 240p / 288p (the same test the sync watcher uses to
        // switch the deinterlacer on). Only for 15 kHz sources: a 480p source counts 524 too and is progressive.
        //  - at the Dreamcast's own rates (measured over RGB: 49.9992-49.9994 Hz PAL, 59.9392 Hz PAL60) it is the Dreamcast;
        //    before this, its 49.999 fell into the SNES PAL window and it was taken for a SNES
        //  - any other interlaced source is never made a Mega Drive, SNES or PC Engine, UNLESS that console was already
        //    the one recognised (Sonic 2 in two-player mode, the few interlaced SNES games): it keeps its profile then,
        //    instead of changing colours and placement in the middle of the game
        if (rto->videoStandardInput == 1 || rto->videoStandardInput == 2) {
            const uint16_t vpi = GBS::VPERIOD_IF::read();
            interlaceNote(vpi); // (after v107z) the Dreamcast's 240p / 480i (288p / 576i) and their mode memory
            const bool interlaced = ((vpi >= 520 && vpi <= 528) || (vpi >= 620 && vpi <= 628)) && (vpi % 2 == 0);
            if (interlaced) {
                const bool dc = (smoothed > 49.996f && smoothed < 50.002f) || (smoothed > 59.936f && smoothed < 59.942f);
                // (after v108h) his report of 2026-11-17 (Sonic 2, two players: vp 524, fr 59.8076; normal play vp 523,
                // fr 59.9217). An interlaced field carries half a line more, so the rate drops by 262 / 262.5 (60 Hz
                // family) or 312 / 312.5 (50 Hz family): 59.92 -> 59.81, which sits in the PC Engine window and lost the
                // Mega Drive's profile. A console that was ALREADY recognised keeps its profile anywhere between its
                // interlaced rate and its progressive one. Only measured on the NTSC Mega Drive; the other windows are
                // the same arithmetic on the measured progressive rates.
                const float r = smoothed;
                // (after v110b) his PAL fingerprint of Sonic 2 for two players (his Japanese console at 50 Hz): fr 50.2378,
                // vp 624, against 50.158 in normal play. At 50 Hz the field gets SHORTER (x 312 / 311.5, +0.16 %), not
                // longer as at 60 Hz, so the 50 Hz windows reach both ways now: 49.70 -> ~49.78 for a real PAL console.
                const bool keepMd = rto->consoleIsMd &&
                                    ((r > 59.79f && r < 59.93f) ||   // NTSC 59.92 -> 59.81 (measured)
                                     (r > 59.23f && r < 59.50f) ||   // PAL at 60 Hz 59.40 (either way, not measured)
                                     (r > 49.52f && r < 49.88f) ||   // PAL 49.70 -> ~49.78 (from the 50 Hz measurement)
                                     (r > 50.04f && r < 50.30f));    // NTSC at 50 Hz 50.158 -> 50.238 (measured)
                const bool keepSnes = !rto->consoleIsMd && rto->consoleDetected == 1 &&
                                      ((r > 59.96f && r < 60.12f) || // NTSC 60.10 -> 59.98
                                       (r > 59.38f && r < 59.65f) || // PAL forced to 60 Hz 59.55 (either way)
                                       (r > 49.91f && r < 50.12f));  // PAL 50.007 (either way, not measured)
                if (dc) {
                    detected = 3;
                    mdNow = 0;
                } else if (keepMd) {
                    detected = 0;
                    mdNow = 1;
                } else if (keepSnes) {
                    detected = 1;
                    mdNow = 0;
                } else if ((detected != 0 || mdNow) && !(detected == rto->consoleDetected && mdNow == rto->consoleIsMd)) {
                    detected = 0;
                    mdNow = 0;
                }
            }
        }
        // (after v107y) THE DREAMCAST THROUGH VGA, his decision: same profile. Measured on his console (menu, 480p):
        // fr 59.9392, separate sync (csync 0), sync pulse 141 of 2341... (hs / ht = 0.0760, "hsr" 759.7). The GameCube through
        // the ODV in 480p has the same rate but composite sync and a 729 pulse; a PC at 640x480 is 59.94 Hz with separate sync
        // too, but its VGA pulse is far longer (~3.8 us, hsr ~1200). So: that rate, separate sync and a pulse of 740..780.
        // The pulse is taken as the middle of five readings, so one odd sample cannot decide it.
        if (!rto->syncTypeCsync && smoothed > 59.936f && smoothed < 59.942f) {
            const uint32_t hsr = syncPulseShare();
            if (hsr >= 740 && hsr <= 780) {
                detected = 3;
                mdNow = 0;
            }
        }
        // (after v107y) THE DREAMCAST IN 240p, his decision: it runs at 59.8253 Hz, inside the PC Engine window (his PC
        // Engine through the ODV reads 59.8249: no field rate can part them). The sync pulse does: the Dreamcast over RGB
        // reads 747..755 in every fingerprint, anything through the ODV 729..738 (the ODV makes its own pulse). So in the PC
        // Engine window a pulse of 745 or more is the Dreamcast. A Dreamcast through the ODV stays a PC Engine (he never
        // uses it that way), and a PC Engine over RGB has a pulse nobody has measured yet.
        if (detected == 2 && rto->syncTypeCsync) {
            const uint32_t hsr = syncPulseShare();
            if (hsr >= 745 && hsr <= 780) {
                detected = 3;
                mdNow = 0;
            }
        }
        // (after v107z) THE DREAMCAST IN 288p, measured by him on 2026-11-16 (240p Test Suite, "288p" mode, three readings):
        // fr 49.9195, vt 312, hsr 748.9, csync 1, vp 625 (progressive). The Saturn PAL (49.9198, vt 312) and the N64 PAL
        // (49.9198, vt 311) sit at the same rate, and the same line count for the Saturn, so neither can part them; the
        // pulse can, as in 240p: the Dreamcast over RGB reads 747..755, the Saturn and the N64 730..735 (a standard 4.7 us
        // pulse). So: a progressive 15 kHz source at 49.90..49.94 Hz with composite sync and a pulse of 745..780.
        if (detected == 0 && !mdNow && rto->syncTypeCsync && smoothed > 49.90f && smoothed < 49.94f &&
            (rto->videoStandardInput == 1 || rto->videoStandardInput == 2)) {
            const uint16_t vpi = GBS::VPERIOD_IF::read();
            if (vpi % 2 == 1) {
                const uint32_t hsr = syncPulseShare();
                if (hsr >= 745 && hsr <= 780)
                    detected = 3;
            }
        }
        const bool changed = (detected != rto->consoleDetected) || (mdNow != rto->consoleIsMd);
        rto->consoleDetected = detected;
        rto->consoleIsMd = mdNow;
        rto->consoleDetectIsFresh = true;
        // (after v107v) a console (or a norm) other than the one last seen: if it has a mode of its own, that is loaded
        // instead, and the load brings its placement and colours with it
        const bool modeRestore = consoleModeCheck();
        if (!modeRestore && uopt->consoleProfile == 0 && (changed || wasStale)) {
            if (changed || (!rto->dcVgaActive && dcVgaWanted())) // (after v108e) the Dreamcast through VGA, also when the
                integerProfileRefresh();                         // detection only turned fresh (it may have held DC already)
            // v79: the colour preset was applied at preset load time, when the detection still held the console that was
            // connected before (the user saw his PC Engine colours land on a SNES). Now it waits for this moment.
            colorPresetApply();
            autoGainApplySaved(); // (after v111h) the console is known now: the gain he saved for it, if there is one
        }
    }
    } // measureIn
    if (web) {
        if (turn == 1 || lastOfr <= 0.0f)
            lastOfr = getOutputFrameRate();
        sendStatusBar(smoothed, lastOfr);
    }
    turn = (uint8_t)((turn + 1) % 3);
}

void externalClockGenSyncInOutRate()
{
    externalClockGenSyncImpl();
}

// [patch output-rate] Called from the main loop. After the output rate mode is changed (or after a
// preset load), keeps nudging the Si5351 until the output sits on the chosen rate. Once settled it only
// re-checks every few seconds. Does nothing in "follow source" mode unless the mode was just changed.
static bool rateKick = false; // (after v111b) a new exact rate: steer at once

void externalClockGenRateKick()
{
    rateKick = true;
}

void externalClockGenRateModeTick()
{
    static uint32_t lastRun = 0;
    static uint8_t lastMode = 0;
    static bool settled = true;
    if (rateKick) {
        rateKick = false;
        settled = false;
    }

    if (!rto->extClockGenDetected) {
        return;
    }
    if (lastMode != uopt->outputRateMode) {
        lastMode = uopt->outputRateMode;
        settled = false; // new mode: steer (also when going back to "follow")
    }
    // (after v110c) his report of 2026-11-17: in «follow» with the frame time lock off, the output was steered only after a
    // preset load or a change of output mode, so when the source changed rate on its own (the 240p suite switched to 480i:
    // IN 59.8075, OUT 59.8312) nobody corrected it. Now «follow» re-checks every 5 s like the fixed rates, and changes the
    // clock only when the output is more than 0.005 Hz off. With the frame time lock on, it is the lock that follows.
    // (after v111b) ...unless the lock has no "clock per output frame" figure (dropped when the HTotal changes, see
    // VideoTiming.cpp): then the steering runs, measures the output and gives the lock a new one.
    if (uopt->outputRateMode == 0 && settled && uopt->enableFrameTimeLock && FrameSync::frequencyReady()) {
        return;
    }
    // v68 / v69: with the watcher stopped the stable count sits where it stopped, which used to switch the output rate
    // steering off - the very thing that keeps the capture from stuttering. It runs whenever a picture is up, however the
    // watcher came to be stopped (the auto-lock or by hand).
    const bool held = syncHeldWithPicture();
    if (rto->noSyncCounter != 0 || (rto->continousStableCounter <= 20 && !held) || rto->sourceDisconnected ||
        rto->outModeHdBypass || (!rto->syncWatcherEnabled && !held)) {
        return;
    }
    uint32_t interval = settled ? 5000 : 400;
    if (millis() - lastRun < interval) {
        return;
    }
    lastRun = millis();
    settled = externalClockGenSyncImpl();
}

void externalClockGenDetectAndInitialize()
{

    // XTAL load capacitance. Some Si5351 boards are sensitive and may fail to oscillate (LOS=1)
    // depending on this setting and supply/noise. We'll probe a few options at runtime.
    // 10pF (0xD2), 8pF (0x92), 6pF (0x52)
    const uint8_t xtal_cl_default = 0xD2; // NOTE: Per AN619, the low bytes should be written 0b010010

    // MHz: 27, 32.4, 40.5, 54, 64.8, 81, 108, 129.6, 162
    rto->freqExtClockGen = 81000000;
    rto->extClockGenDetected = 0;

    // Snapshot Si5351 status before detection/init.
    int st_pre = -999, st_pre2 = -999;
    int et0 = -999; // endTransmission return
    int n0 = -999;  // bytes read
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    et0 = Wire.endTransmission();
    if (et0 == 0) {
        n0 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n0 == 1) st_pre = Wire.read();
    }
    // read twice to detect flaky bus
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() == 0) {
        int n1 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n1 == 1) st_pre2 = Wire.read();
    }
    (void)st_pre;
    (void)st_pre2;
    (void)et0;
    (void)n0;

    if (uopt->disableExternalClockGenerator) {
        SerialM.println(F("ExternalClockGenerator disabled, skipping detection"));
        return;
    }

    uint8_t retVal = 0;
    Wire.beginTransmission(SIADDR);
    retVal = Wire.endTransmission();

    if (retVal != 0) {
        return;
    }

    Wire.beginTransmission(SIADDR);
    Wire.write(0); // Device Status
    Wire.endTransmission();
    size_t bytes_read = Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);

    if (bytes_read == 1) {
        retVal = Wire.read();
        if ((retVal & 0x80) == 0) {
            // SYS_INIT indicates device is ready.
            rto->extClockGenDetected = 1;
        } else {
            return;
        }
    } else {
        return;
    }

    auto readSt = []() -> int {
        Wire.beginTransmission(SIADDR);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0) return -1;
        int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n != 1) return -2;
        return Wire.read();
    };
    auto clearInt = []() -> bool {
        Wire.beginTransmission(SIADDR);
        Wire.write(0x01); // interrupt status (sticky)
        Wire.write(0xFF); // clear all
        return Wire.endTransmission() == 0;
    };
    auto isLockedSt = [](int st) -> bool {
        // We only use CLK0 (PLLA) in this firmware; ignore LOLB (PLLB) for lock.
        // st bit5=LOLA, bit6=LOLB, bit4=LOS, bit7=SYS_INIT
        return (st >= 0) && ((st & 0x20) == 0) && ((st & 0x80) == 0);
    };
    auto waitLock = [&](uint32_t timeoutMs) -> int {
        uint32_t t0 = millis();
        int st = -999;
        while (millis() - t0 < timeoutMs) {
            st = readSt();
            if (isLockedSt(st)) return st;
            delay(1);
        }
        return st;
    };
    auto readRegs = [](uint8_t startReg, uint8_t count, uint8_t *out) -> bool {
        Wire.beginTransmission(SIADDR);
        Wire.write(startReg);
        if (Wire.endTransmission() != 0) return false;
        int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)count, false);
        if (n != (int)count) return false;
        for (uint8_t i = 0; i < count; i++) out[i] = (uint8_t)Wire.read();
        return true;
    };

    // IMPORTANT (runtime-proven): Using the wrong XTAL here scales the output frequency.
    // Default to 25MHz, as most Si5351 boards use it.
    uint32_t xtal = 25000000UL;

    // Probe XTAL_CL settings to see if we can clear LOS/LOLA on ESP32.
    uint8_t xtal_cls[3] = {xtal_cl_default, 0x92, 0x52};
    int st_after = -999;
    // Keep the best (lowest) status we observed so later clock resets reuse it.
    uint8_t best_xtal_cl = xtal_cl_default;
    int best_score = 0x7fffffff;
    auto scoreSt = [](int st) -> int {
        // Lower is better. We care mainly about lock-related flags.
        // bit4=LOS, bit5=LOLA, bit6=LOLB, bit7=SYS_INIT
        if (st < 0) return 1000;
        int score = 0;
        if (st & 0x80) score += 100; // SYS_INIT
        if (st & 0x10) score += 10;  // LOS
        if (st & 0x20) score += 5;   // LOLA
        if (st & 0x40) score += 5;   // LOLB
        return score;
    };
    for (uint8_t t = 0; t < 3; t++) {
        uint8_t xtal_cl = xtal_cls[t];
        Si.init(xtal);
        Wire.beginTransmission(SIADDR);
        Wire.write(183); // XTAL_CL
        Wire.write(xtal_cl);
        Wire.endTransmission();
        Si.setPower(0, SIOUT_6mA);
        Si.setFreq(0, rto->freqExtClockGen);
        Si.enable(0);
        bool clrOk = clearInt();
        delay(2);
        int st0 = readSt();
        int stw = waitLock(200);
        st_after = readSt();

        // Read back key PLL/MS registers to detect partial programming.
        uint8_t pllA[8] = {0}, ms0[8] = {0};
        bool pllOk = readRegs(26, 8, pllA); // PLLA params
        bool msOk = readRegs(42, 8, ms0);   // MS0 params (regs 42..49)
        // Also read OE/CLK0 control for sanity.
        int oe = -999, clk0 = -999;
        Wire.beginTransmission(SIADDR);
        Wire.write(0x03);
        if (Wire.endTransmission() == 0) {
            int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
            if (n == 1) oe = Wire.read();
        }
        Wire.beginTransmission(SIADDR);
        Wire.write(0x10);
        if (Wire.endTransmission() == 0) {
            int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
            if (n == 1) clk0 = Wire.read();
        }

        Si.disable(0);
        (void)clrOk;
        (void)st0;
        (void)stw;
        (void)st_after;
        (void)pllOk;
        (void)msOk;
        (void)pllA;
        (void)ms0;
        (void)oe;
        (void)clk0;

        int sc = scoreSt(st_after);
        if (sc < best_score) {
            best_score = sc;
            best_xtal_cl = xtal_cl;
        }
        if (isLockedSt(st_after)) {
            break;
        }
    }
    // Persist best observed XTAL_CL for later use in externalClockGenResetClock().
    g_si5351_best_xtal_cl = best_xtal_cl;
    (void)best_score;

    int st_post = -999, st_post2 = -999;
    int etp0 = -999, np0 = -999;
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    etp0 = Wire.endTransmission();
    if (etp0 == 0) {
        np0 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (np0 == 1) st_post = Wire.read();
    }
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() == 0) {
        int np1 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (np1 == 1) st_post2 = Wire.read();
    }
    (void)st_post;
    (void)st_post2;
    (void)etp0;
    (void)np0;
}

// used for RGBHV to determine the ADPLL speed "level" / can jitter with SOG Sync
uint32_t getPllRate()
{
    typedef TV5725<GBS_ADDR> GBS;
#ifdef ESP32
    uint32_t tickRateHz = 1000000;
#else
    uint32_t tickRateHz = ESP.getCpuFreqMHz() * 1000000;
#endif
    uint8_t testBusSelBackup = GBS::TEST_BUS_SEL::read();
    uint8_t spBusSelBackup = GBS::TEST_BUS_SP_SEL::read();
    uint8_t debugPinBackup = GBS::PAD_BOUT_EN::read();

    if (testBusSelBackup != 0xa) {
        GBS::TEST_BUS_SEL::write(0xa);
    }
    if (rto->syncTypeCsync) {
        if (spBusSelBackup != 0x6b)
            GBS::TEST_BUS_SP_SEL::write(0x6b);
    } else {
        if (spBusSelBackup != 0x09)
            GBS::TEST_BUS_SP_SEL::write(0x09);
    }
    GBS::PAD_BOUT_EN::write(1); // enable output to pin for test
    yield();                    // BOUT signal and wifi
    delayMicroseconds(200);
    uint32_t ticks = FrameSync::getPulseTicks();

    // restore
    GBS::PAD_BOUT_EN::write(debugPinBackup);
    if (testBusSelBackup != 0xa) {
        GBS::TEST_BUS_SEL::write(testBusSelBackup);
    }
    GBS::TEST_BUS_SP_SEL::write(spBusSelBackup);

    uint32_t retVal = 0;
    if (ticks > 0) {
        retVal = tickRateHz / ticks;
    }

    return retVal;
}
