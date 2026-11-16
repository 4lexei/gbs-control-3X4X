#ifndef SYNC_WATCHER_H
#define SYNC_WATCHER_H

#include <Arduino.h>
#include "../core/State.h"

// Sync watcher - main synchronization loop
void runSyncWatcher();
// v65: a fresh attempt at the auto-lock: let go of what is already fixed (coast, phase, clamp), start the stable count
// over and hold the next lock back for a moment, so a retry never inherits the broken state it was locked in.
void syncAutoLockRearm();
// v66: called from the main loop; while auto-retry is on it starts the next hands-free attempt.
void syncAutoRetryTick();
// v67: what the watcher would still have done in the next few passes - position the clamp and switch the scanlines on.
// Both are skipped when the auto-lock stops the watcher early (the clamp needs 4 stable reads, the scanlines 3), which is
// why an early lock came out with a colour cast and without scanlines.
void syncFinishSetup();
// v69: true while the watcher is stopped but a picture is up and sync has not been lost - whether it was stopped by the
// auto-lock or by hand. Stopping the watcher is meant to leave the INPUT sync alone; the output-side servos (frame time
// lock, best htotal, output rate steering) and the status bar have no reason to stop with it, and did.
bool syncHeldWithPicture();
void fastSogAdjust();
boolean snapToIntegralFrameRate(void);
boolean checkBoardPower();
void calibrateAdcOffset();

#endif // SYNC_WATCHER_H

