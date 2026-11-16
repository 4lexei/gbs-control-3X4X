#ifndef VIDEO_SCALING_H
#define VIDEO_SCALING_H

#include <Arduino.h>
#include "../core/State.h"

// Video scaling functions
// [patch fine-scale] true = horizontal scale moves in steps of 1 (default steps of 2 skip every other value)
extern bool scaleHorizontalFine;
// [patch perfect-scale] returns true if the exact target value was reached
bool scaleHorizontalTo(uint16_t target);
void scaleHorizontal(uint16_t amountToScale, bool subtracting);
void scaleVertical(uint16_t amountToScale, bool subtracting);
bool scaleVerticalTo(uint16_t target);

#endif // VIDEO_SCALING_H

