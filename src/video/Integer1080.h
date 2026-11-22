#ifndef INTEGER1080_H_
#define INTEGER1080_H_

#include <Arduino.h>

// [patch 1080p-integer] horizontal placement of the NTSC integer 1080p modes (uopt->integer1080: 3 = x4, 4 = x5).
// Every horizontal adjustment made here is tracked as a difference, so it can be undone exactly
// without touching what the user did afterwards with the move / border buttons.

// A new preset was loaded: forget all tracked adjustments.
void integer1080Reset();

// Undo the previous placement, then place the picture for sources that are 320 or 256 pixels wide.
//   x4: 320 px = 7 presses "move right"; 256 px = 4 presses right + 1 fine press right
//   x5: 320 px = HScale 818 + output window + 4 presses + 2 fine presses right; 256 px = 4 presses + 2 fine presses right
void integer1080Apply(uint16_t nativeWidth);

// [patch 720p-x3] horizontal placement of the NTSC 720p x3 mode for sources 320 / 256 px wide (picture 960 px, x=160..1119).
// Absolute values (round 2, estimated from the round 1 captures: the picture started at x=-12 with HB_SP 206 / HB_ST 2246,
// 0.688 px per unit -> +250 units for 320 px; the 256 px picture starts 10 px further right, so 15 units less).
void integer720Apply(uint16_t nativeWidth);

// v47: the 720p preset was loaded (stock ADC clock): set the IF line and place the picture with the saved hand-made presets.
void integer720Load();
// v62: 960p x4 (1280x960) and 480p x2 (720x480), whole pixels in 4:3
void integerLowApply(uint16_t nativeWidth);
void integerLowLoad();
// v62: 1 = Mega Drive, 2 = SNES / NES, 3 = PC Engine (PC Engine uses the Mega Drive values for now)
uint8_t activeConsoleProfile();
// v107p: 0 unknown, 1 Mega Drive, 2 SNES / NES, 3 PC Engine: whose picture memory is in use
// (after v107v) 4..17: the profiles without measurements (his list), chosen by hand; activeConsoleProfile() gives 1 for them
#define CONSOLE_PROFILE_COUNT 19
// (after v108e) his «Otras»: for whoever does not see his console in the list. A profile without measurements like
// 4..17, kept after them so every file of v108e (colours, sync states, edges, last modes) still reads the same.
#define CONSOLE_PROFILE_OTHERS 18
#define CONSOLE_PROFILE_FIRST_EMPTY 4
#define CONSOLE_PROFILE_DREAMCAST 13 // (after v107w) the first one Auto can recognise (rto->consoleDetected 3)
uint8_t settingsProfile();
// (after v107z) THE DREAMCAST'S OWN MODES, his measurements of 2026-11-16 with the 240p Test Suite. What the Dreamcast is
// sending, only while its profile is in use: 0 = not the Dreamcast profile, 1 = 240p, 2 = 288p, 3 = 480i, 4 = 576i,
// 5 = anything else (VGA). The interlace comes from the line count of the input formatter (even = 480i / 576i), latched
// so that one odd reading does not flip it.
uint8_t dcSignal();
void interlaceNote(uint16_t vperiodIf); // the detection's reading, every measurement
// (after v108b) the Dreamcast through VGA: our 1080p button loads Rama's 1920x1080 there (31 kHz, see applyPresets) and
// his measured «4:3» (the whole width) is written on top. True when the load is one for it.
bool dcVgaWanted();
void dcVgaLoad();      // at the load: notes Rama's ADC clock, then places
void dcVga1080Apply(); // the placement (again in the 700 ms pass): «4:3» or, with the ADC clock lowered, «16:9» 2 x 2
const char *consoleProfileName(uint8_t prof); // the name the status bar prints after "profile "
// v107q: true where the mode has three size steps (square, CRT, wide): the Mega Drive in PAL 1080p x4
bool sizeStepsThree();
bool sizeStepsTwoMd();
// (after v107q) the user's edges, kept per mode 30 s after the last press (15 s before v123j)
void edgesNote(uint8_t which, int16_t amount);
void edgesTick();
void edgesFlushNow();
void edgesResetMode();
void edgesDiscardPending();
// (after v123j) the picture position (Mover), kept per mode like the borders
void posNote();
void posDiscardPending();
// (after v107r) the three undo buttons: each puts back only its own group
void undoMove();
void undoScale();
void undoEdge();
void placeSnapFromCustom(); // (after v107r) the photo for the undo buttons, taken from a personal profile
void keepHbStWithDisplayEdge(); // in UserCommandHandler.cpp

// v50: the preset load resets the chip (resetDigital / ResetSDRAM) after the mode registers were written; a later register
// write (the scale button did it) used to clear the glitches. Writes the same mode again once, without moving HB_ST.
void integer720ReapplyTick();

// v51: true when the console profile is SNES / NES (chosen or detected), false for Mega Drive
bool integerIsSnesProfile();
bool integerIsPceProfile();

// v75: true when the "320 px" button should mean "fill the width" (SNES / NES only, not the PC Engine).
bool integerWidthButtonIsFill();

// v77: MHz for a PLL_MS value (0 for the feedback clock).
uint16_t memClockMhzFor(uint8_t pllMs);

// v84: write the saved SDRAM clock phase (does nothing when it is 0).
void applyMemPhase();

// v88: the memory slot of the mode on screen, 0..15, or 16 when no integer mode is on (see Integer1080.cpp).
uint8_t memSettingIndex();

// v88: write the SDRAM clock and phase the user saved FOR THIS MODE. True if anything was written (reset the SDRAM then).
bool applyMemSettings();

// v88: the name of the mode whose memory slot is in use ("PAL 1080p x4"), for the log.
const char *memModeName();

// v90: the SDRAM clock and phase that will actually be written for the mode on screen (the user's, else the mode's default).
void memEffective(uint8_t *clock, uint8_t *phase);
// v107m: the user's own clock / phase byte for the mode and size step on screen (nullptr outside the integer modes)
// (after v120c) the memory settings are per console now, in /memsetN.txt; "memoria de fábrica" clears the mode's own
void memSettingsEnsureLoaded();
void memSettingsSave();
void memSettingsResetMode();
uint8_t *memClockOwn();
// v107n: the "Líneas" row (224 / 240 or 239 / DOLOR)
bool linesDolorSlot(uint8_t slot);
void linesSetDolor(uint8_t slot); // (after v107q) DOLOR in RAM only; 0xFF = off
uint8_t linesModeEffective();
uint8_t linesAvailability();
bool linesDolorActive();
uint8_t *memPhaseOwn();

// v43: place the picture again for the current console profile (SNES / NES / Mega Drive).
void integerProfileRefresh();

// (after v108h) the Mega Drive's 1080p x4 «4:3» (uopt->integer1080Tv43) and the 256 / 320 clock change in place
bool md43Able();
bool md43NoWide(); // (after v120c) no «ancho» / «llenar» in any console that has a 4:3
bool tv43Active();
bool integer1080ClockLive(bool narrow);

#endif // INTEGER1080_H_
