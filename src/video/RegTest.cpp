#include "RegTest.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/tv5725.h"

typedef TV5725<GBS_ADDR> GBS;

// (after v111) the registers under test. None of them is written anywhere else in the firmware (checked against the whole
// tree on 2026-11-17). What each one does is guessed from its name only: there is no datasheet.
struct RegTestEntry
{
    // (after v111d) no name here: the names cost RAM on the ESP8266; the web shows them (REGTEST_NAMES in index.ts)
    uint8_t (*rd)();
    void (*wr)(uint8_t);
    uint8_t max;
};

#define REGTEST(R, M) { []() -> uint8_t { return (uint8_t)GBS::R::read(); }, [](uint8_t v) { GBS::R::write(v); }, M }

static const RegTestEntry regs[] = {
    // the motion adaptive deinterlacer (segment 2)
    REGTEST(MADPT_STILL_NOISE_EST_EN, 1),
    REGTEST(MADPT_MI_1BIT_BYPS, 1),
    REGTEST(MADPT_MI_1BIT_FRAME2_EN, 1),
    REGTEST(MADPT_MI_1BIT_DLY, 3),
    REGTEST(MADPT_VT_FILTER_CNTRL, 1),
    REGTEST(MADPT_HTAP_BYPS, 1),
    REGTEST(MADPT_BIT_STILL_EN, 1),
    REGTEST(MADPT_VTAP2_BYPS, 1),
    REGTEST(MADPT_VTAP2_ROUND_SEL, 1),
    REGTEST(MADPT_EN_NOUT_FOR_STILL, 1),
    REGTEST(MADPT_EN_NOUT_FOR_LESS_STILL, 1),
    REGTEST(MADPT_DD0_SEL, 1),
    REGTEST(MADPT_NRD_VIIR_PD_BYPS, 1),
    REGTEST(MADPT_UVDLY_PD_BYPS, 1),
    REGTEST(MADPT_CMP_EN, 1),
    REGTEST(MADPT_EN_STILL_FOR_NRD, 1),
    // the output fields: the one of the field group never tried in the two rounds of 15-11 (v107x / v107y)
    REGTEST(VDS_HALF_EN, 1),
    // (after v127) the noise reduction of the output (VDS_NR_*), never written by the firmware (checked
    // against the whole tree on 2026-11-24). For the ODV-II composite noise; what each does is guessed from its name.
    REGTEST(VDS_NR_Y_BYPS, 1),
    REGTEST(VDS_NR_C_BYPS, 1),
    REGTEST(VDS_NR_DIF_LPF5_BYPS, 1),
    REGTEST(VDS_NR_MI_TH_EN, 1),
    REGTEST(VDS_NR_MI_OFFSET, 127),
    REGTEST(VDS_NR_MIG_USER_EN, 1),
    REGTEST(VDS_NR_MI_GAIN, 15),
    REGTEST(VDS_NR_STILL_GAIN, 15),
    REGTEST(VDS_NR_MI_THRES, 15),
    REGTEST(VDS_NR_EN_H_NOISY, 1),
    REGTEST(VDS_NR_EN_GLB_STILL, 1),
    REGTEST(VDS_NR_GLB_STILL_MENU, 1),
    REGTEST(VDS_NR_NOISY_OFFSET, 127),
    // (after v127b) the fine sharpness bands of the output (VDS_PK_HL / HB / HH), never written by the
    // firmware (checked 2026-11-24). GAIN = strength, CORE = small changes (noise) left alone, CMP = guessed from the name.
    REGTEST(VDS_PK_HL_GAIN, 63),
    REGTEST(VDS_PK_HL_CORE, 7),
    REGTEST(VDS_PK_HL_CMP, 31),
    REGTEST(VDS_PK_HB_GAIN, 63),
    REGTEST(VDS_PK_HB_CORE, 7),
    REGTEST(VDS_PK_HB_CMP, 31),
    REGTEST(VDS_PK_HH_GAIN, 63),
    REGTEST(VDS_PK_HH_CORE, 7),
    REGTEST(VDS_PK_HH_CMP, 31),
    // (after v127d) the switches the bands above may need (guessed from names and positions: the bands may
    // be the vertical peaking, off while the preset leaves its bypass at 1). Never written by the firmware (2026-11-24).
    REGTEST(VDS_PK_Y_V_BYPS, 1),
    REGTEST(VDS_PK_RAM_BYPS, 1),
    REGTEST(VDS_PK_VH_HL_SEL, 1),
    REGTEST(VDS_PK_VH_HH_SEL, 1),
    // (after v127d) the scaler's two interpolators (Rama left them commented out: "disable RGB stage
    // interpolator" / "disable YUV stage interpolator"). In whole-pixel scaling they might give OSSC-like hard pixels.
    REGTEST(VDS_1ST_INT_BYPS, 1),
    REGTEST(VDS_2ND_INT_BYPS, 1),
};
static const uint8_t REG_COUNT = sizeof(regs) / sizeof(regs[0]);

static uint8_t sel = 0;
static uint8_t before[REG_COUNT];
static bool touched[REG_COUNT];

static void logReg(const char *what, uint8_t i, uint8_t from, uint8_t to)
{
    SM_PRINTF("[3x4x-v130k] test %s: register %u %u -> %u (before the tests %u)\n", what, (unsigned)(i + 1), (unsigned)from,
                   (unsigned)to, (unsigned)(touched[i] ? before[i] : from));
}

void regTestNext()
{
    sel = (uint8_t)((sel + 1) % REG_COUNT);
    const uint8_t v = regs[sel].rd();
    SM_PRINTF("[3x4x-v130k] test register %u of %u = %u (0..%u)\n", (unsigned)(sel + 1), (unsigned)REG_COUNT,
                   (unsigned)v, (unsigned)regs[sel].max);
}

static void logSel()
{
    const uint8_t v = regs[sel].rd();
    SM_PRINTF("[3x4x-v130k] test register %u of %u = %u (0..%u)\n", (unsigned)(sel + 1), (unsigned)REG_COUNT,
                   (unsigned)v, (unsigned)regs[sel].max);
}

// (after v127c) the previous register, and a jump to the first one of each group (no clicking round the
// whole list). The group starts follow the order of regs[] above: keep them in step with it.
void regTestPrev()
{
    sel = (uint8_t)((sel == 0) ? REG_COUNT - 1 : sel - 1);
    logSel();
}

void regTestGroup(uint8_t g)
{
    static const uint8_t first[] = {0, 17, 30}; // MADPT_STILL_NOISE_EST_EN, VDS_NR_Y_BYPS, VDS_PK_HL_GAIN
    if (g < sizeof(first))
        sel = first[g];
    logSel();
}

void regTestStep(int8_t dir)
{
    const RegTestEntry &r = regs[sel];
    const uint8_t v = r.rd();
    if (!touched[sel]) {
        before[sel] = v;
        touched[sel] = true;
    }
    const uint8_t n = (dir > 0) ? (v >= r.max ? 0 : v + 1) : (v == 0 ? r.max : v - 1);
    r.wr(n);
    logReg("set", sel, v, r.rd());
}

void regTestUndo()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < REG_COUNT; i++) {
        if (!touched[i])
            continue;
        const uint8_t v = regs[i].rd();
        regs[i].wr(before[i]);
        logReg("undo", i, v, regs[i].rd());
        touched[i] = false;
        count++;
    }
    if (!count)
        SerialM.println(F("[3x4x-v130k] test undo: nothing was changed"));
}

void regTestForget()
{
    memset(touched, 0, sizeof(touched));
}

void regTestReport(char *buf, size_t n)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < REG_COUNT; i++)
        count += touched[i] ? 1 : 0;
    const uint8_t v = regs[sel].rd();
    snprintf(buf, n, " rt=%u:%u:%u:%u", (unsigned)sel, (unsigned)v, (unsigned)(touched[sel] ? before[sel] : v), (unsigned)count);
}
