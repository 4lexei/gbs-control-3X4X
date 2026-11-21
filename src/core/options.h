#ifndef _USER_H_
#define _USER_H_
using Ascii8 = uint8_t;
/// Output resolution requested by user, *given to* applyPresets().
enum PresetPreference : uint8_t {
    Output960P = 0,
    Output480P = 1,
    OutputCustomized = 2,
    Output720P = 3,
    Output1024P = 4,
    Output1080P = 5,
    OutputDownscale = 6,
    OutputBypass = 10,
};

// userOptions holds user preferences / customizations
struct userOptions
{
    // 0 - normal, 1 - x480/x576, 2 - customized, 3 - 1280x720, 4 - 1280x1024, 5 - 1920x1080,
    // 6 - downscale, 10 - bypass
    PresetPreference presetPreference;
    Ascii8 presetSlot;
    // [patch 1080p-integer] 0 = stock 1080p preset, 3 = integer 1080p x4 (full picture), 4 = integer 1080p x5 (crops ~8 game lines)
    uint8_t integer1080;
    uint8_t consoleProfile;   // v43: 0 = auto (by field rate), 1 = Mega Drive, 2 = SNES / NES (saved)
    uint8_t integer1080Narrow; // [patch 256-same-width] x4 + 256 px: ADC clock lowered to 80% so a 256 px pixel is as wide as a 320 px one (not saved)
    // [patch 720p-x3] 0 = stock 720p preset, 1 = integer 720p x3 (NTSC, lowered ADC clock). Saved since v40 (#27)
    // v47: 1 = 720p x3 (his presets: 4 columns for 256 px, 3 lines per line), 2 = 720p x5 (5 columns, 5 lines per line, crops to 144 lines)
    uint8_t integer720;
    uint8_t autoRetrySync; // v66: 0 = off, 1 = keep re-arming the auto-lock by itself every few seconds (hands-free attempts). Not saved
    uint8_t autoLockSync; // v64: 0 = off, 1..4 = pause the sync watcher by itself after 2 / 10 / 30 / 62 stable reads. Saved (#31)
    // v77: the SDRAM clock the x5 modes load, as a PLL_MS value (0 = use the per-profile default: 5 / 185 MHz for the PC
    // Engine, 7 / 129 MHz for the rest). The x5 modes need more memory than any other and 185 is a mild overclock, so a board
    // whose memory will not take it can be lowered with the "cycle ram" button and the choice is remembered. Saved (#32)
    uint8_t x5MemClock;
    // v84: SDRAM clock PHASE, 0..15: the four delay bits of bank 4 (MEM_INTER_DLYCELL_SEL, MEM_CLK_DLYCELL_SEL,
    // MEM_FBK_CLK_DLYCELL_SEL and MEM_PAD_CLK_INVERT). They do not change the frequency, they change WHEN the data is
    // sampled against the clock edge, which is what a few misplaced lines at an otherwise good frequency look like.
    // 0 = the values the preset loads. Saved (#33)
    uint8_t memPhase;
    // v86: the size step of a mode, 0 = square pixels, 1 = CRT shape, 2 = wide, 3 = fills the screen. The fill button cycles
    // it. Always four steps, never collapsed even when two come out nearly the same size: the cycle has to be predictable.
    // Used by the PAL presets, where the four steps are measured; NTSC still works off the width buttons and the fill flag.
    // Saved (#34)
    uint8_t sizeStep;
    // v88: the SDRAM clock and phase PER MODE. x5MemClock and memPhase above are a single byte each, which could only ever
    // remember one setting: the user runs 720p x5 at 185 MHz and PAL 1080p x4 at 162, and whichever he set last erased the
    // other. Worse, they were only ever re-written inside integer720ReapplyTick() under "integer720 == 2", so in 1080p
    // nothing was re-applied at all and every preset load brought back the garbage and the shift he had tuned away.
    // One slot per preset and mode: index = (PAL ? 8 : 0) + (1080p ? 4 : 0) + (x3 0, x5 1, x4 2, 16:9 3), the same numbering
    // the integer720 / integer1080 options already use. 0 = use the default for that mode. Saved (#35..#50 clock, #51..#66 phase)
    // v103: EXPERIMENT, off by default. Raises the ADC sampling of the PAL 720p x5 mode so the scaler has more input
    // samples to stretch from, which is the one thing the 1280x1024 preset (PLLAD_MD 3506 against 1458) does differently
    // where the user saw no glitches. 0 = stock, everything exactly as before; 1 = the raised clock. One toggle undoes it.
    uint8_t x5Oversample;
    uint8_t memClockSlot[16]; // v107m: no longer applied (one value for the whole mode was the bug); read once to migrate
    uint8_t memPhaseSlot[16];
    // v107m: the user's SDRAM clock and phase per mode AND size step. The measured defaults are per step (memDefaultFor), so
    // a choice stored for the whole mode overrode the measured pair of every other step: the user tuned one step and the
    // others started to glitch. 0 = the step's measured default. Saved (#68..#131 clock, #132..#195 phase)
    uint8_t memClockStep[16][4];
    uint8_t memPhaseStep[16][4];
    // v107n: the "Líneas" row of the Modo tab, per mode (same 16 slots): 0 = 224 lines (the mode as placed), 1 = 240 / 239
    // (the vertical window opened to show every line a PAL-adapted game draws), 2 = DOLOR (the whole PAL frame squashed
    // the way a PAL TV showed it, no borders, scanlines off). Only used in PAL, with the Mega Drive or the SNES, in 720p x3
    // and 1080p x4; DOLOR only where it is measured (Mega Drive, 720p x3 16:9 and 1080p x4). Saved (#196..#211)
    uint8_t linesMode[16];
    // v107o: the shape 720p x3 was last used in (0 = 4:3, 1 = 16:9), so its button in the Modo tab opens it the same way
    // again. 16:9 by default, today's normal screen. Saved (#212)
    uint8_t x3Wide;
    // (after v108h) his request of 2026-11-17: the Mega Drive's 1080p x4 in «4:3», for a 16:9 set switched to 4:3 by its
    // remote (5.33 columns per pixel, the picture 4/3 wider than the 16:9 one, same height). 0 = 16:9, 1 = 4:3. Saved (#213)
    uint8_t integer1080Tv43;

    uint8_t tolerantSync; // v63: 0 = stock, 1 = hold the preset through short sync dropouts (for sources the watcher keeps dropping). Saved (#30)
    uint8_t integerLow; // v62: low-res integer modes: 0 = off, 1 = 960p x4 (1280x960), 2 = 480p x2 (720x480). Saved (#29)
    uint8_t integer1080Wide; // v58: 1080p x4 / x5 "wide" fill state (1920 px, estimate). Not saved
    uint8_t integer720Fill; // v47: 720p x3 + 256 px: fill the screen width (5 columns per pixel, not whole-pixel perfect). Not saved
    uint8_t enableFrameTimeLock;
    uint8_t frameTimeLockMethod;
    uint8_t enableAutoGain;
    uint8_t wantScanlines;
    uint8_t wantOutputComponent;
    uint8_t deintMode;
    uint8_t wantVdsLineFilter;
    uint8_t wantPeaking;
    uint8_t wantTap6;
    uint8_t preferScalingRgbhv;
    uint8_t PalForce60;
    uint8_t disableExternalClockGenerator;
    uint8_t matchPresetSource;
    uint8_t wantStepResponse;
    uint8_t wantFullHeight;
    uint8_t enableCalibrationADC;
    uint8_t scanlineStrength;
    uint8_t reverseRotaryEncoderForOledMenu; // 0/1; default from REVERSE_ROTARY_ENCODER_FOR_OLED_MENU
    // [patch output-rate] Only used with an external clock generator (Si5351).
    // 0 = output follows the source refresh rate (stock behaviour)
    // 1 = hold 59.94 Hz (50.00 Hz for 50 Hz sources), 2 = hold 60.00 Hz (50.00 Hz for 50 Hz sources)
    uint8_t outputRateMode;
    uint32_t outputRateExact;
    uint8_t outputChosen; // (after v111f) 1 once he has picked any output (Rama's, ours or a personal preset): the red notice goes
    // [patch image-tuning] 0 = softer, 1 = stock, 2 = sharper, 3 = sharpest
    uint8_t peakLevel;
    // [patch image-tuning] 0 = off, 1 = stock, 2 = stronger, 3 = strongest (colour edge sharpness)
    uint8_t stepLevel;
    // [patch image-tuning] ADC low-pass: 0 = stock (preset value), 1 = 110 MHz, 2 = 70 MHz, 3 = 40 MHz
    uint8_t adcFilterLevel;
};


// runTimeOptions holds system variables
struct runTimeOptions
{
    uint32_t freqExtClockGen;
    uint8_t consoleDetected; // v43: 1 = SNES / NES detected from the field rate (0 = anything else); v62: 2 = PC Engine; (after v107w) 3 = Dreamcast
    uint8_t consoleIsMd;     // v107p: 1 = the field rate is one of the Mega Drive's (for the per-console picture memory)
    // v79: false from the moment a preset is loaded until the detection routine has run again on a stable signal. While it is
    // false the auto profile still holds the PREVIOUS console's value, so anything keyed on the profile (the colour preset,
    // the sync state) must wait instead of writing the wrong console's settings.
    bool consoleDetectIsFresh;
    uint16_t statLosses;  // v42: signal losses since boot (status bar)
    uint16_t statRetries; // v42: failed vsync measurements since boot (status bar)
    uint16_t noSyncCounter; // is always at least 1 when checking value in syncwatcher
    uint8_t presetVlineShift;
    uint8_t videoStandardInput; // 0 - unknown, 1 - NTSC like, 2 - PAL like, 3 480p NTSC, 4 576p PAL
    uint8_t phaseSP;
    uint8_t phaseADC;
    uint8_t currentLevelSOG;
    uint8_t thisSourceMaxLevelSOG;
    uint8_t syncLockFailIgnore;
    uint8_t applyPresetDoneStage;
    uint8_t continousStableCounter;
    uint8_t failRetryAttempts;
    uint8_t presetID;  // PresetID
    bool isCustomPreset;
    bool dcVgaActive; // (after v108b) the Dreamcast through VGA on Rama's 1080p, with its measured «4:3» on top
    uint8_t integer1080Centered; // [patch 1080p-integer] 0 = none, 1 = 320 px adjustment applied, 2 = 256 px adjustment applied
    unsigned long integer720ReapplyAt; // v50: millis() deadline to write the 720p mode again after the load finished (0 = none)
    bool syncPresetActive; // v70: a saved sync state is in force, so the recovery must not overwrite what it set
    bool integerLowActive; // v62: the 960p x4 / 480p x2 mode was loaded by applyPresets()
    bool integer720Active; // [patch 720p-x3] the integer 720p table was loaded by applyPresets()
    bool integer1080Active; // [patch 1080p-integer] the integer 1080p table was loaded by applyPresets()
    bool integer1080LfForced; // [patch 1080p-integer] x5 switched the line filter on by itself
    uint8_t integer1080LfPrev; // [patch 1080p-integer] line filter setting before x5
    uint8_t HPLLState;
    uint8_t medResLineCount;
    uint8_t osr;
    uint8_t notRecognizedCounter;
    bool isInLowPowerMode;
    bool clampPositionIsSet;
    bool coastPositionIsSet;
    bool phaseIsSet;
    bool inputIsYpBpR;
    bool syncWatcherEnabled;
    bool outModeHdBypass;
    bool printInfos;
    bool sourceDisconnected;
    bool webServerEnabled;
    bool webServerStarted;
    bool allowUpdatesOTA;
    bool enableDebugPings;
    bool autoBestHtotalEnabled;
    bool videoIsFrozen;
    bool forceRetime;
    bool motionAdaptiveDeinterlaceActive;
    bool deinterlaceAutoEnabled;
    bool scanlinesEnabled;
    bool boardHasPower;
    bool presetIsPalForce60;
    bool syncTypeCsync;
    bool isValidForScalingRGBHV;
    bool useHdmiSyncFix;
    bool extClockGenDetected;
};
// remember adc options across presets
struct adcOptions
{
    // If `uopt->enableAutoGain == 1` and we're not before/during
    // doPostPresetLoadSteps(), `adco->r_gain` must match `GBS::ADC_RGCTRL`.
    //
    // When we either set `uopt->enableAutoGain = 1` or call
    // `GBS::ADC_RGCTRL::write()`, we must either call
    // `GBS::ADC_RGCTRL::write(adco->r_gain)`, or set `adco->r_gain =
    // GBS::ADC_RGCTRL::read()`.
    uint8_t r_gain;
    uint8_t g_gain;
    uint8_t b_gain;
    uint8_t r_off;
    uint8_t g_off;
    uint8_t b_off;
};
#endif