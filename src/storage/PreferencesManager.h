#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Arduino.h>
#include "../core/State.h"

// Preferences management functions
void saveUserPrefs();  // (after v123j) written 30 s after the last change
void prefsFlush();     // write a pending change now
void prefsTick();
void loadDefaultUserOptions();
void syncReverseRotaryEncoderIsrMirror();

#endif // PREFERENCES_MANAGER_H

