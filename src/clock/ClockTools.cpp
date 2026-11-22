#include "ClockTools.h"
#include "Si5351Manager.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../storage/PreferencesManager.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSPhase.h"
#include "../gbs/GBSVideoProcessing.h" // (after v111h) autoGainSavedValue()
#include "../gbs/tv5725.h"
#include "../video/framesync.h"

typedef TV5725<GBS_ADDR> GBS;

volatile uint32_t clockToolsPendingHz = 0;
volatile uint16_t clockToolsPendingAdc = 0;
volatile uint32_t clockToolsPendingSi = 0;

// the exact output rate: 40.0000 .. 90.0000 Hz
static void applyExactHz(uint32_t v)
{
    if (v < 400000 || v > 900000) {
        SerialM.println(F("[3x4x-v123l] exact output rate: out of range (40..90 Hz)"));
        return;
    }
    uopt->outputRateExact = v;
    saveUserPrefs();
    SM_PRINTF("[3x4x-v123l] exact output rate: %lu.%04lu Hz%s\n", (unsigned long)(v / 10000), (unsigned long)(v % 10000),
                   uopt->outputRateMode == 3 ? "" : " (saved; choose \"exacta\" to use it)");
    if (uopt->outputRateMode == 3)
        externalClockGenRateKick();
}

// the ADC clock, the same recipe as Developer's "PLL div" (gbs-control.ino, 'n'), at an exact value
static void applyAdc(uint16_t md)
{
    if (md < 1000 || md > 4095) {
        SerialM.println(F("[3x4x-v123l] ADC clock: out of range (1000..4095)"));
        return;
    }
    const uint16_t before = GBS::PLLAD_MD::read();
    // (after v111f) his test of 2026-11-18: Rama's 480p, 2345 -> 1876 in one go scrambled the picture. "PLL div" only
    // moves HRST and LINE, which is fine for a few steps; a big change leaves IF_HB_ST2 (1116) past the new line end
    // (HRST 938). It now keeps its distance to HRST, as the line end moves.
    const int16_t hrstOld = (int16_t)GBS::IF_HSYNC_RST::read();
    const int16_t st2Old = (int16_t)GBS::IF_HB_ST2::read();
    GBS::PLLAD_MD::write(md);
    GBS::IF_HSYNC_RST::write(md / 2);
    GBS::IF_LINE_SP::write((md / 2) + 1 + 0x40);
    int16_t st2New = (int16_t)(st2Old + ((int16_t)(md / 2) - hrstOld));
    if (st2New < 2)
        st2New = 2;
    GBS::IF_HB_ST2::write((uint16_t)(st2New & 0xfffe));
    latchPLLAD();
    delay(1);
    updateClampPosition();
    updateCoastPosition(0);
    SM_PRINTF("[3x4x-v123l] ADC clock (PLLAD_MD): %u -> %u, IF HRST/LINE %u/%u, HB ST2 %u (not saved; a preset load puts it back)\n",
                   (unsigned)before, (unsigned)md, (unsigned)GBS::IF_HSYNC_RST::read(), (unsigned)GBS::IF_LINE_SP::read(),
                   (unsigned)GBS::IF_HB_ST2::read());
}

// the Si5351 output clock, 20..200 MHz
static void applySi(uint32_t f)
{
    if (!rto->extClockGenDetected) {
        SerialM.println(F("[3x4x-v123l] output clock: no Si5351"));
        return;
    }
    if (f < 20000000UL || f > 200000000UL) {
        SerialM.println(F("[3x4x-v123l] output clock: out of range (20..200 MHz)"));
        return;
    }
    const uint32_t before = rto->freqExtClockGen;
    Si.setFreq(0, f);
    rto->freqExtClockGen = f;
    SM_PRINTF("[3x4x-v123l] output clock: %lu -> %lu Hz (not saved; the output rate steering and the frame time lock "
                   "may move it again)\n", (unsigned long)before, (unsigned long)f);
}

void clockToolsTick()
{
    if (clockToolsPendingHz) {
        const uint32_t v = clockToolsPendingHz;
        clockToolsPendingHz = 0;
        applyExactHz(v);
    }
    if (clockToolsPendingAdc) {
        const uint16_t v = clockToolsPendingAdc;
        clockToolsPendingAdc = 0;
        applyAdc(v);
    }
    if (clockToolsPendingSi) {
        const uint32_t v = clockToolsPendingSi;
        clockToolsPendingSi = 0;
        applySi(v);
    }
}

void clockToolsReport(char *buf, size_t n)
{
    snprintf(buf, n, " hzx=%lu orm=%u adc=%u si=%lu ftlp=%d oc=%u agp=%u", (unsigned long)uopt->outputRateExact, (unsigned)uopt->outputRateMode,
             (unsigned)GBS::PLLAD_MD::read(), (unsigned long)(rto->extClockGenDetected ? rto->freqExtClockGen : 0),
             (int)g_ftlTargetPhase, (unsigned)uopt->outputChosen, (unsigned)autoGainSavedValue());
}
