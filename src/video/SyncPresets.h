#ifndef SYNC_PRESETS_H
#define SYNC_PRESETS_H

#include <Arduino.h>
#include "../core/State.h"

// v66: one sync state per console profile. On a source whose sync is only just good enough (the user's PC Engine through
// an ODV composite-to-component transcoder) the picture only locks in a particular state, which the watcher reaches by
// chance while it recovers: a SOG slicing level it walked up to, the coast of the recovery path left unfixed, a halved
// pulse-ignore. Finding it again was a lottery of manual retries, so it can now be captured the moment it looks right and
// written back with one press (and after every preset load).
// Same slot numbering as the colour presets: the active console profile (1 = Mega Drive, 2 = SNES / NES, 3 = PC Engine).
// (after v107v) 18, one per console profile (see COLOR_PRESET_SLOTS)
#define SYNC_PRESET_SLOTS 19 // (after v108e) 18 = «Otras»

// Called once at boot, after LittleFS is up.
void syncPresetsLoad();
// Capture the current sync state into the slot of the active console profile and save it.
void syncPresetCaptureNow();
// Write the slot of the active console profile to the chip and stop the watcher there (does nothing when the slot is empty).
void syncPresetApply(bool quiet);
// Forget the slot of the active console profile.
void syncPresetClear();
// True when the active console profile has a sync state saved.
bool syncPresetAvailable();
// Write the user's known-good PC Engine state (measured on v64) without having to win the lottery first.
void syncPresetApplyOdvRecipe();
// v68: print the saved state of the active profile, so it can be copied somewhere outside the board.
void syncPresetPrintSaved();

#endif // SYNC_PRESETS_H
