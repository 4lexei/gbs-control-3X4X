#include "VideoShifting.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/tv5725.h"
#include "framesync.h"
#include "VideoScaling.h"
#include "../gbs/GBSPresets.h"

void shiftHorizontal(uint16_t amountToShift, bool subtracting)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t hrst = GBS::VDS_HSYNC_RST::read();
    uint16_t hbst = GBS::VDS_HB_ST::read();
    uint16_t hbsp = GBS::VDS_HB_SP::read();

    // Perform the addition/subtraction
    if (subtracting) {
        if ((int16_t)hbst - amountToShift >= 0) {
            hbst -= amountToShift;
        } else {
            hbst = hrst - (amountToShift - hbst);
        }
        if ((int16_t)hbsp - amountToShift >= 0) {
            hbsp -= amountToShift;
        } else {
            hbsp = hrst - (amountToShift - hbsp);
        }
    } else {
        if ((int16_t)hbst + amountToShift <= hrst) {
            hbst += amountToShift;
            // also extend hbst_d to maximum hrst-1
            if (hbst > GBS::VDS_DIS_HB_ST::read()) {
                GBS::VDS_DIS_HB_ST::write(hbst);
            }
        } else {
            hbst = 0 + (amountToShift - (hrst - hbst));
        }
        if ((int16_t)hbsp + amountToShift <= hrst) {
            hbsp += amountToShift;
        } else {
            hbsp = 0 + (amountToShift - (hrst - hbsp));
        }
    }

    GBS::VDS_HB_ST::write(hbst);
    GBS::VDS_HB_SP::write(hbsp);
}

void shiftHorizontalLeft()
{
    shiftHorizontal(scaleHorizontalFine ? 1 : 4, true);
}

void shiftHorizontalRight()
{
    shiftHorizontal(scaleHorizontalFine ? 1 : 4, false);
}

// unused but may become useful
void shiftHorizontalLeftIF(uint8_t amount)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t IF_HB_ST2 = GBS::IF_HB_ST2::read() + amount;
    uint16_t IF_HB_SP2 = GBS::IF_HB_SP2::read() + amount;
    uint16_t PLLAD_MD = GBS::PLLAD_MD::read();

    if (rto->videoStandardInput <= 2) {
        GBS::IF_HSYNC_RST::write(PLLAD_MD / 2); // input line length from pll div
    } else if (rto->videoStandardInput <= 7) {
        GBS::IF_HSYNC_RST::write(PLLAD_MD);
    }
    uint16_t IF_HSYNC_RST = GBS::IF_HSYNC_RST::read();

    GBS::IF_LINE_SP::write(IF_HSYNC_RST + 1);

    // start
    if (IF_HB_ST2 < IF_HSYNC_RST) {
        GBS::IF_HB_ST2::write(IF_HB_ST2);
    } else {
        GBS::IF_HB_ST2::write(IF_HB_ST2 - IF_HSYNC_RST);
    }

    // stop
    if (IF_HB_SP2 < IF_HSYNC_RST) {
        GBS::IF_HB_SP2::write(IF_HB_SP2);
    } else {
        GBS::IF_HB_SP2::write((IF_HB_SP2 - IF_HSYNC_RST) + 1);
    }
}

// unused but may become useful
void shiftHorizontalRightIF(uint8_t amount)
{
    typedef TV5725<GBS_ADDR> GBS;
    int16_t IF_HB_ST2 = GBS::IF_HB_ST2::read() - amount;
    int16_t IF_HB_SP2 = GBS::IF_HB_SP2::read() - amount;
    uint16_t PLLAD_MD = GBS::PLLAD_MD::read();

    if (rto->videoStandardInput <= 2) {
        GBS::IF_HSYNC_RST::write(PLLAD_MD / 2); // input line length from pll div
    } else if (rto->videoStandardInput <= 7) {
        GBS::IF_HSYNC_RST::write(PLLAD_MD);
    }
    int16_t IF_HSYNC_RST = GBS::IF_HSYNC_RST::read();

    GBS::IF_LINE_SP::write(IF_HSYNC_RST + 1);

    if (IF_HB_ST2 > 0) {
        GBS::IF_HB_ST2::write(IF_HB_ST2);
    } else {
        GBS::IF_HB_ST2::write(IF_HSYNC_RST - 1);
    }

    if (IF_HB_SP2 > 0) {
        GBS::IF_HB_SP2::write(IF_HB_SP2);
    } else {
        GBS::IF_HB_SP2::write(IF_HSYNC_RST - 1);
    }
}

// modified to move VBSP, set VBST to VBSP-2
void shiftVertical(uint16_t amountToAdd, bool subtracting)
{
    typedef TV5725<GBS_ADDR> GBS;
    typedef GBS::Tie<GBS::VDS_VB_ST, GBS::VDS_VB_SP> Regs;
    uint16_t vrst = GBS::VDS_VSYNC_RST::read() - FrameSync::getSyncLastCorrection();
    uint16_t vbst = 0, vbsp = 0;
    int16_t newVbst = 0, newVbsp = 0;

    Regs::read(vbst, vbsp);
    newVbst = vbst;
    newVbsp = vbsp;

    if (subtracting) {
        newVbsp -= amountToAdd;
    } else {
        newVbsp += amountToAdd;
    }

    // handle the case where hbst or hbsp have been decremented below 0
    if (newVbst < 0) {
        newVbst = vrst + newVbst;
    }
    if (newVbsp < 0) {
        newVbsp = vrst + newVbsp;
    }

    // handle the case where vbst or vbsp have been incremented above vrstValue
    if (newVbst > (int16_t)vrst) {
        newVbst = newVbst - vrst;
    }
    if (newVbsp > (int16_t)vrst) {
        newVbsp = newVbsp - vrst;
    }

    // mod: newVbsp needs to be at least newVbst+2
    if (newVbsp < (newVbst + 2)) {
        newVbsp = newVbst + 2;
    }
    // mod: -= 2
    newVbst = newVbsp - 2;

    Regs::write(newVbst, newVbsp);
}

void shiftVerticalUp()
{
    shiftVertical(1, true);
}

void shiftVerticalDown()
{
    shiftVertical(1, false);
}

void shiftVerticalUpIF()
{
    typedef TV5725<GBS_ADDR> GBS;
    // -4 to allow variance in source line count
    uint8_t offset = rto->videoStandardInput == 2 ? 4 : 1;
    uint16_t sourceLines = GBS::VPERIOD_IF::read() - offset;
    // add an override for sourceLines, in case where the IF data is not available
    if ((GBS::GBS_OPTION_SCALING_RGBHV::read() == 1) && rto->videoStandardInput == 14) {
        sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    }
    int16_t stop = GBS::IF_VB_SP::read();
    int16_t start = GBS::IF_VB_ST::read();

    if (stop < (sourceLines - 1) && start < (sourceLines - 1)) {
        const int16_t vs = scaleHorizontalFine ? 1 : 2;
        stop += vs;
        start += vs;
    } else {
        start = 0;
        stop = 2;
    }
    GBS::IF_VB_SP::write(stop);
    GBS::IF_VB_ST::write(start);
    // v85: say what moved and where. The edge buttons print their value, these did not, so the only way to see the result
    // was to ask for the whole register dump after every press.
    SerialM.print(F("Image up: IF VB "));
    SerialM.print(GBS::IF_VB_ST::read());
    SerialM.print('/');
    SerialM.println(GBS::IF_VB_SP::read());
}

void shiftVerticalDownIF()
{
    typedef TV5725<GBS_ADDR> GBS;
    // -4 to allow variance in source line count
    uint8_t offset = rto->videoStandardInput == 2 ? 4 : 1;
    uint16_t sourceLines = GBS::VPERIOD_IF::read() - offset;
    // add an override for sourceLines, in case where the IF data is not available
    if ((GBS::GBS_OPTION_SCALING_RGBHV::read() == 1) && rto->videoStandardInput == 14) {
        sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    }
    int16_t stop = GBS::IF_VB_SP::read();
    int16_t start = GBS::IF_VB_ST::read();

    if (stop > 2 && start > 0) {
        const int16_t vs = scaleHorizontalFine ? 1 : 2;
        stop -= vs;
        start -= vs;
    } else {
        start = sourceLines - 2;
        stop = sourceLines;
    }
    GBS::IF_VB_SP::write(stop);
    GBS::IF_VB_ST::write(start);
    SerialM.print(F("Image down: IF VB "));
    SerialM.print(GBS::IF_VB_ST::read());
    SerialM.print('/');
    SerialM.println(GBS::IF_VB_SP::read());
}

void moveHS(uint16_t amountToAdd, bool subtracting)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t htotal = GBS::VDS_HSYNC_RST::read();
    if (htotal == 0)
        return; // safety
    uint16_t VDS_DIS_HB_ST = GBS::VDS_DIS_HB_ST::read();
    uint16_t newVDS_HS_ST = GBS::VDS_HS_ST::read();
    uint16_t newVDS_HS_SP = GBS::VDS_HS_SP::read();

    if (subtracting) {
        if ((newVDS_HS_ST - amountToAdd) > VDS_DIS_HB_ST) {
            newVDS_HS_ST -= amountToAdd;
            newVDS_HS_SP -= amountToAdd;
        } else
            SerialM.println(F("limit"));
    } else {
        if ((newVDS_HS_SP + amountToAdd) < htotal) {
            newVDS_HS_ST += amountToAdd;
            newVDS_HS_SP += amountToAdd;
        } else
            SerialM.println(F("limit"));
    }

    GBS::VDS_HS_ST::write(newVDS_HS_ST);
    GBS::VDS_HS_SP::write(newVDS_HS_SP);
}

void moveVS(uint16_t amountToAdd, bool subtracting)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
    if (vtotal == 0)
        return; // safety
    uint16_t VDS_DIS_VB_ST = GBS::VDS_DIS_VB_ST::read();
    uint16_t newVDS_VS_ST = GBS::VDS_VS_ST::read();
    uint16_t newVDS_VS_SP = GBS::VDS_VS_SP::read();

    if (subtracting) {
        if ((newVDS_VS_ST - amountToAdd) > VDS_DIS_VB_ST) {
            newVDS_VS_ST -= amountToAdd;
            newVDS_VS_SP -= amountToAdd;
        } else
            SerialM.println(F("limit"));
    } else {
        if ((newVDS_VS_SP + amountToAdd) < vtotal) {
            newVDS_VS_ST += amountToAdd;
            newVDS_VS_SP += amountToAdd;
        } else
            SerialM.println(F("limit"));
    }

    GBS::VDS_VS_ST::write(newVDS_VS_ST);
    GBS::VDS_VS_SP::write(newVDS_VS_SP);
}

void invertHS()
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t temp = GBS::VDS_HS_ST::read();
    GBS::VDS_HS_ST::write(GBS::VDS_HS_SP::read());
    GBS::VDS_HS_SP::write(temp);
    GBS::SP_HS_PROC_INV_REG::write(!GBS::SP_HS_PROC_INV_REG::read());
}

void invertVS()
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t temp = GBS::VDS_VS_ST::read();
    GBS::VDS_VS_ST::write(GBS::VDS_VS_SP::read());
    GBS::VDS_VS_SP::write(temp);
    GBS::SP_VS_PROC_INV_REG::write(!GBS::SP_VS_PROC_INV_REG::read());
}


// [patch fine] Canvas move left/right (keys 7 / 6). In fine mode every press moves the minimum step.
void moveCanvasHorizontal(bool right)
{
    typedef TV5725<GBS_ADDR> GBS;
    const uint16_t fine = scaleHorizontalFine ? 1 : 0;
    if (videoStandardInputIsPalNtscSd() && !rto->outModeHdBypass) {
        const uint16_t step = fine ? 2 : 8; // IF_HBIN_SP
        const uint16_t rstStep = fine ? 1 : 4;
        if (right) {
            if (GBS::IF_HBIN_SP::read() >= step + 2) {
                GBS::IF_HBIN_SP::write(GBS::IF_HBIN_SP::read() - step);
                if ((GBS::IF_HSYNC_RST::read() - rstStep) > ((GBS::PLLAD_MD::read() >> 1) + 5)) {
                    GBS::IF_HSYNC_RST::write(GBS::IF_HSYNC_RST::read() - rstStep);
                    GBS::IF_LINE_SP::write(GBS::IF_LINE_SP::read() - rstStep);
                }
            } else {
                SerialM.println(F("limit"));
            }
        } else {
            if (GBS::IF_HBIN_SP::read() < 0x150) {
                GBS::IF_HBIN_SP::write(GBS::IF_HBIN_SP::read() + step);
            } else {
                SerialM.println(F("limit"));
            }
        }
    } else if (!rto->outModeHdBypass) {
        const uint16_t step = fine ? 1 : 4;
        const uint16_t lim = GBS::IF_HSYNC_RST::read() - 0x30;
        uint16_t sp = GBS::IF_HB_SP2::read();
        uint16_t st = GBS::IF_HB_ST2::read();
        if (right) {
            sp = (sp >= step) ? sp - step : lim;
            st = (st >= step) ? st - step : lim;
        } else {
            sp = (sp < lim) ? sp + step : 0;
            st = (st < lim) ? st + step : 0;
        }
        GBS::IF_HB_SP2::write(sp);
        GBS::IF_HB_ST2::write(st);
        SerialM.print(F("IF_HB_ST2: "));
        SerialM.print(st, HEX);
        SerialM.print(F(" IF_HB_SP2: "));
        SerialM.println(sp, HEX);
    }
}
