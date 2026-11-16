#ifndef SCANLINES_H
#define SCANLINES_H

#include <Arduino.h>
#include "../core/State.h"

// Scanlines functions
void enableScanlines();
void disableScanlines();

// v92: put the RFF buffer back after a ResetSDRAM, or the scanlines come back scrambled. Does nothing when they are off.
void scanlinesAfterSdramReset();

#endif // SCANLINES_H

