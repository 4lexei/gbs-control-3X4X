#ifndef GBS_VIDEO_PROCESSING_H
#define GBS_VIDEO_PROCESSING_H

#include <Arduino.h>
#include "../core/State.h"

// GBS video processing functions
void setAdcGain(uint8_t gain);
// (after v111h) the gain he saved for this console: kept in flash, one slot per console profile, and locked while it is set
void autoGainStoreLoad();
uint8_t autoGainSavedValue(); // 0 = nothing saved for the console in front of us
bool autoGainLocked();
void autoGainSaveCurrent();
void autoGainForget();
void autoGainApplySaved();
void setAdcParametersGainAndOffset();
void applyYuvPatches();
void applyRGBPatches();
void OutputComponentOrVGA();
void applyComponentColorMixing();
void toggleIfAutoOffset();
void runAutoGain();

#endif // GBS_VIDEO_PROCESSING_H

