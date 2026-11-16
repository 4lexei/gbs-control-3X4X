#ifndef CONSOLE_MODES_H_
#define CONSOLE_MODES_H_

#include <Arduino.h>

// (after v107v) THE LAST MODE OF EACH CONSOLE, his request of 2026-11-14: "cada consola tiene que cargar el preset, perfil
// y modos que hayas usado con ella la última vez que estuvo conectada", with 50 Hz and 60 Hz kept apart.
// One record per console profile (1..17, not the unknown console) and per norm: the output mode (ours, one of Rama's or
// a personal preset with its slot), the shape and the width. DOLOR is never kept (it is a joke: 240 always comes back).
// Kept by itself whenever the mode on screen changes while the console is known; written back when the console (or its
// norm) changes, which costs one visible reload, because the console is only known once its signal has been measured.

void consoleModesLoad();            // boot, after the preferences: reads /cmode.bin and seeds the width of the last console
bool consoleModeCheck();            // the console or the norm may have changed: true if its last mode is going to be loaded
void consoleModeTick();             // main loop: keeps the mode on screen, writes the file, runs a pending load
uint16_t consoleModeLoadWidth(uint16_t def); // the NTSC width a load should place (320 / 256), def when none is pending
uint8_t consoleModePendingWidth();  // 0 none, 1 = 320 px, 2 = 256 px (integer1080Centered values)
void consoleModeLoadDone();         // end of doPostPresetLoadSteps: the pending width has been used

#endif // CONSOLE_MODES_H_
