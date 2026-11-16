#ifndef IMAGE_TUNING_H
#define IMAGE_TUNING_H

#include <Arduino.h>
#include "../core/State.h"

// [patch image-tuning] Optional picture sharpness controls. Level 1 of both means "stock firmware".

// Call after every preset load, once the preset's own peaking / step values are in place.
void applyImageTuning();
// v52: fine peaking (+/- 1 on both luma gains), black level stretch (+/- 2, switches the stage on), back to the preset values.
void nudgePeaking(int8_t delta);
void nudgeBlackLevel(int8_t delta);
// v107j: step response gain +/- 1 (0..15)
void nudgeStepGain(int8_t delta);
void resetImageFine();
// v64: the white balance / colour cast the preset loaded (captured the first time it is needed after a load).
// The colour presets store differences against this, not absolute values: the ADC gains are per-preset calibration.
void imageTuningWbBase(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *u, uint8_t *v);
// v54: white balance / colour cast, codes 'a'..'j' (see ImageTuning.cpp)
void imageCommand(char code);
// v72: ADC black level (the analog offset, before digitising). The Black level buttons act on VDS_BLEV, which is after the
// ADC, so on a source whose black arrives lifted (the user's PC Engine through the ODV) they cannot reach it: the black is
// already digitised as grey. These move ADC_ROFCTRL / GOFCTRL / BOFCTRL instead. channel: 0 = all three, 1 = R, 2 = G, 3 = B.
void nudgeAdcOffset(uint8_t channel, int8_t delta);
// v73: the ADC black level back to what the preset loaded (what Reset color and remove colour use).
void resetAdcOffset();
// v74: hue, a rotation of the chip's colour matrix. dir is +1 / -1, one step = 3 degrees, range +/- 63.
void nudgeHue(int8_t dir);
// v74: back to 0 degrees, keeping the saturation.
void resetHue();
// v74: saturation, which shares the same registers as the hue and so must go through the same writer.
// which: 0 = both axes, 1 = Pb/U, 2 = Pr/V.
void nudgeSaturation(uint8_t which, int8_t delta);
// v74: read / write the hue state for the colour presets.
void imageTuningHue(int8_t *step, uint8_t *u, uint8_t *v);
void imageTuningSetHue(int8_t step, uint8_t u, uint8_t v);
// v72: the ADC offsets saved in a colour preset are absolute (unlike the gains, nothing else moves them).
void imageTuningAdcOffsets(uint8_t *r, uint8_t *g, uint8_t *b);
// Cycle luma peaking strength: soft, stock, sharper, sharpest.
void cyclePeakLevel();
// Cycle step response strength (colour edge sharpness): off, stock, stronger, strongest.
void cycleStepLevel();
// Cycle the ADC low-pass filter: stock, 110, 70, 40 MHz (for noisy sources).
void cycleAdcFilterLevel();
// Move the ADC sampling phase by one of 32 steps (switches to manual mode).
void stepManualPhaseAdc(int8_t delta);
// Back to the automatic ADC phase.
void resetManualPhaseAdc();

// v107j: all the values above in one keyed line for the web status bar ("v pk=1 st=1 ..."); see the .cpp
void imageTuningReport(char *buf, size_t n);

#endif // IMAGE_TUNING_H
