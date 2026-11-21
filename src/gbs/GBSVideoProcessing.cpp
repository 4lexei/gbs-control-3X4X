#include "GBSVideoProcessing.h"
#include <LittleFS.h>              // (after v111h) the gain he saved per console
#include "../video/ColorPresets.h" // (after v111h) COLOR_PRESET_SLOTS: one slot per console, same as his colour profiles
#include "../video/Integer1080.h" // (after v111f) settingsProfile()
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../wifi/WiFiManager.h"
#include "../gbs/GBSController.h"
#include "../utils/DebugHelpers.h"
#include "tv5725.h"

// (after v111h) his decision of 2026-11-21: no table of my measurements and no option to switch it on. The gain that HIS
// board found on HIS cable is what gets kept. He presses "guardar esta ganancia": the value is written for that console
// and locked, so Auto Gain stops touching it until he presses "olvidar". One byte per console profile slot, in
// /autogainv1.txt, two hex digits each; 0 = nothing saved for that console.
static uint8_t agSaved[COLOR_PRESET_SLOTS];
static bool agLoaded = false;

static uint8_t agSlot()
{
    const uint8_t prof = settingsProfile();
    return (prof < COLOR_PRESET_SLOTS) ? prof : 0;
}

static uint8_t agNibble(int c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xff;
}

void autoGainStoreLoad()
{
    agLoaded = true;
    memset(agSaved, 0, sizeof(agSaved));
    File f = LittleFS.open("/autogainv1.txt", "r");
    if (!f)
        return;
    for (uint8_t s = 0; s < COLOR_PRESET_SLOTS; s++) {
        if (!f.available())
            break;
        const uint8_t hi = agNibble(f.read());
        if (!f.available())
            break;
        const uint8_t lo = agNibble(f.read());
        if (hi == 0xff || lo == 0xff)
            break;
        agSaved[s] = (uint8_t)((hi << 4) | lo);
    }
    f.close();
}

static void autoGainStoreSave()
{
    File f = LittleFS.open("/autogainv1.txt", "w");
    if (!f) {
        SerialM.println(F("[3x4x-v122] saved gain: could not open the file"));
        return;
    }
    for (uint8_t s = 0; s < COLOR_PRESET_SLOTS; s++) {
        char d[2];
        const char *hex = "0123456789abcdef";
        d[0] = hex[(agSaved[s] >> 4) & 0x0f];
        d[1] = hex[agSaved[s] & 0x0f];
        f.write((const uint8_t *)d, 2);
    }
    f.close();
}

// the gain saved for the console in front of us, 0 = none. While it is not 0 the gain is locked.
uint8_t autoGainSavedValue()
{
    if (!agLoaded)
        autoGainStoreLoad();
    return agSaved[agSlot()];
}

bool autoGainLocked()
{
    return autoGainSavedValue() != 0;
}

// "guardar esta ganancia": whatever is in the register right now, for this console, and locked from here on
void autoGainSaveCurrent()
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!agLoaded)
        autoGainStoreLoad();
    const uint8_t g = GBS::ADC_GGCTRL::read();
    if (g == 0) {
        SerialM.println(F("[3x4x-v122] saved gain: nothing to save yet"));
        return;
    }
    agSaved[agSlot()] = g;
    autoGainStoreSave();
    SM_PRINTF("[3x4x-v122] gain saved for this console: 0x%02x (locked; Auto Gain will not move it)\n", (unsigned)g);
}

// "olvidar la ganancia": the console goes back to learning it on its own
void autoGainForget()
{
    if (!agLoaded)
        autoGainStoreLoad();
    agSaved[agSlot()] = 0;
    autoGainStoreSave();
    SerialM.println(F("[3x4x-v122] saved gain forgotten: Auto Gain works it out again from 0x48"));
}

// puts the saved gain back. Called after a preset load and once the console is known, whether Auto Gain is on or off:
// a saved gain is a gain he chose, not a starting point.
void autoGainApplySaved()
{
    typedef TV5725<GBS_ADDR> GBS;
    const uint8_t g = autoGainSavedValue();
    if (!g || GBS::ADC_GGCTRL::read() == g)
        return;
    setAdcGain(g);
    SM_PRINTF("[3x4x-v122] saved gain for this console: 0x%02x\n", (unsigned)g);
}

void setAdcGain(uint8_t gain) {
    typedef TV5725<GBS_ADDR> GBS;
    // gain is actually range, increasing it dims the image.
    GBS::ADC_RGCTRL::write(gain);
    GBS::ADC_GGCTRL::write(gain);
    GBS::ADC_BGCTRL::write(gain);

    // Save gain for applying preset. (Gain affects passthrough presets, and
    // loading a passthrough preset loads from adco but doesn't save to it.)
    adco->r_gain = gain;
    adco->g_gain = gain;
    adco->b_gain = gain;
}

void setAdcParametersGainAndOffset()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::ADC_ROFCTRL::write(0x40);
    GBS::ADC_GOFCTRL::write(0x40);
    GBS::ADC_BOFCTRL::write(0x40);

    // Do not call setAdcGain() and overwrite adco->r_gain. This function should
    // only be called during startup, or during doPostPresetLoadSteps(), and if
    // `uopt->enableAutoGain == 1` then doPostPresetLoadSteps() will revert
    // these writes with `adco->r_gain`.
    GBS::ADC_RGCTRL::write(0x7B);
    GBS::ADC_GGCTRL::write(0x7B);
    GBS::ADC_BGCTRL::write(0x7B);
}

void OutputComponentOrVGA()
{
    typedef TV5725<GBS_ADDR> GBS;
    // TODO replace with rto->isCustomPreset?
    boolean isCustomPreset = GBS::GBS_PRESET_CUSTOM::read();
    if (uopt->wantOutputComponent) {
        SerialM.println(F("Output Format: Component"));
        GBS::VDS_SYNC_LEV::write(0x80); // 0.25Vpp sync (leave more room for Y)
        GBS::VDS_CONVT_BYPS::write(1);  // output YUV
        GBS::OUT_SYNC_CNTRL::write(0);  // no H / V sync out to PAD
    } else {
        GBS::VDS_SYNC_LEV::write(0);
        GBS::VDS_CONVT_BYPS::write(0); // output RGB
        GBS::OUT_SYNC_CNTRL::write(1); // H / V sync out enable
    }

    if (!isCustomPreset) {
        if (rto->inputIsYpBpR == true) {
            applyYuvPatches();
        } else {
            applyRGBPatches();
        }
    }
}

void applyComponentColorMixing()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::VDS_Y_GAIN::write(0x64);    // 3_35
    GBS::VDS_UCOS_GAIN::write(0x19); // 3_36
    GBS::VDS_VCOS_GAIN::write(0x19); // 3_37
    GBS::VDS_Y_OFST::write(0xfe);    // 3_3a
    GBS::VDS_U_OFST::write(0x01);    // 3_3b
}

void toggleIfAutoOffset()
{
    typedef TV5725<GBS_ADDR> GBS;
    if (GBS::IF_AUTO_OFST_EN::read() == 0) {
        // and different ADC offsets
        GBS::ADC_ROFCTRL::write(0x40);
        GBS::ADC_GOFCTRL::write(0x42);
        GBS::ADC_BOFCTRL::write(0x40);
        // enable
        GBS::IF_AUTO_OFST_EN::write(1);
        GBS::IF_AUTO_OFST_PRD::write(0); // 0 = by line, 1 = by frame
    } else {
        if (adco->r_off != 0 && adco->g_off != 0 && adco->b_off != 0) {
            GBS::ADC_ROFCTRL::write(adco->r_off);
            GBS::ADC_GOFCTRL::write(adco->g_off);
            GBS::ADC_BOFCTRL::write(adco->b_off);
        }
        // adco->r_off = 0: auto calibration on boot failed, leave at current values
        GBS::IF_AUTO_OFST_EN::write(0);
        GBS::IF_AUTO_OFST_PRD::write(0); // 0 = by line, 1 = by frame
    }
}

// blue only mode: t0t44t1 t0t44t4
void applyYuvPatches()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::ADC_RYSEL_R::write(1);     // midlevel clamp red
    GBS::ADC_RYSEL_B::write(1);     // midlevel clamp blue
    GBS::ADC_RYSEL_G::write(0);     // gnd clamp green
    GBS::DEC_MATRIX_BYPS::write(1); // ADC
    GBS::IF_MATRIX_BYPS::write(1);

    if (GBS::GBS_PRESET_CUSTOM::read() == 0) {
        // colors
        GBS::VDS_Y_GAIN::write(0x80);    // 3_25
        GBS::VDS_UCOS_GAIN::write(0x1c); // 3_26
        GBS::VDS_VCOS_GAIN::write(0x29); // 3_27
        GBS::VDS_Y_OFST::write(0xFE);
        GBS::VDS_U_OFST::write(0x03);
        GBS::VDS_V_OFST::write(0x03);
        if (rto->videoStandardInput >= 5 && rto->videoStandardInput <= 7) {
            // todo: Rec. 709 (vs Rec. 601 used normally
            // needs this on VDS and HDBypass
        }
    }

    if (uopt->wantOutputComponent) {
        applyComponentColorMixing();
    }
}

// blue only mode: t0t44t1 t0t44t4
void applyRGBPatches()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::ADC_RYSEL_R::write(0);     // gnd clamp red
    GBS::ADC_RYSEL_B::write(0);     // gnd clamp blue
    GBS::ADC_RYSEL_G::write(0);     // gnd clamp green
    GBS::DEC_MATRIX_BYPS::write(0); // 5_1f 2 = 1 for YUV / 0 for RGB << using DEC matrix
    GBS::IF_MATRIX_BYPS::write(1);

    if (GBS::GBS_PRESET_CUSTOM::read() == 0) {
        // colors
        GBS::VDS_Y_GAIN::write(0x80);    // 3_25
        GBS::VDS_UCOS_GAIN::write(0x1c); // 3_26
        GBS::VDS_VCOS_GAIN::write(0x29); // 3_27
        GBS::VDS_Y_OFST::write(0x00);
        GBS::VDS_U_OFST::write(0x00);
        GBS::VDS_V_OFST::write(0x00);
    }

    if (uopt->wantOutputComponent) {
        applyComponentColorMixing();
    }
}

void runAutoGain()
{
    typedef TV5725<GBS_ADDR> GBS;
    static unsigned long lastTimeAutoGain = millis();
    uint8_t limit_found = 0, greenValue = 0;
    uint8_t loopCeiling = 0;
    if (autoGainLocked()) // (after v111h) he saved a gain for this console: hands off until he forgets it
        return;
    uint8_t status00reg = GBS::STATUS_00::read(); // confirm no mode changes happened

    if ((millis() - lastTimeAutoGain) < 30000) {
        loopCeiling = 61;
    } else {
        loopCeiling = 8;
    }

    for (uint8_t i = 0; i < loopCeiling; i++) {
        if (i % 20 == 0) {
            handleWiFi(0);
            limit_found = 0;
        }
        greenValue = GBS::TEST_BUS_2F::read();

        if (greenValue == 0x7f) {
            if (getStatus16SpHsStable() && (GBS::STATUS_00::read() == status00reg)) {
                limit_found++;
            } else
                return;

            if (limit_found == 2) {
                limit_found = 0;
                uint8_t level = GBS::ADC_GGCTRL::read();
                if (level < 0xfe) {
                    setAdcGain(level + 2);

                    // remember these gain settings
                    adco->r_gain = GBS::ADC_RGCTRL::read();
                    adco->g_gain = GBS::ADC_GGCTRL::read();
                    adco->b_gain = GBS::ADC_BGCTRL::read();

                    printInfo();
                    delay(2); // let it settle a little
                    lastTimeAutoGain = millis();
                }
            }
        }
    }
}

// Note: runAutoGain is a large function and will be kept in the main file for now

