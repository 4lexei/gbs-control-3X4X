#ifndef COLOR_PRESETS_H
#define COLOR_PRESETS_H

#include <Arduino.h>
#include "../core/State.h"

// v62: one colour preset per console profile. A preset is a snapshot of everything the Color Correction
// panel touches (saturation, black level, peaking fine, white balance R / G / B, colour cast U / V and the
// three level cycles). The slots live in /colorv1.txt, so they survive a reboot, and the slot of the active
// profile is applied after every preset load.
// The number of slots is deliberately larger than the number of profiles we have today: adding a console
// means adding a profile, not changing this file.
// (after v107v) 18: one per console profile, the 15 without measurements included (slots 4..7 of an older file were
// never used, so they read as empty)
#define COLOR_PRESET_SLOTS 19 // (after v108e) 18 = «Otras»

// Called once at boot, after LittleFS is up.
void colorPresetsLoad();
// Snapshot the current picture into the slot of the active console profile and save it.
void colorPresetSaveCurrent();
// Write the slot of the active console profile to the chip (does nothing when the slot is empty).
void colorPresetApply();
// Forget the slot of the active console profile.
void colorPresetClear();
// Log which slots hold a preset.
void colorPresetStatus();
// (after v107v) one console's colour and filters to a file and back (see the .cpp)
void colorPresetExportActive();
void colorPresetImportFile();

#endif // COLOR_PRESETS_H
