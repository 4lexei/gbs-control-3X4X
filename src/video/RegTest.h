#ifndef REG_TEST_H_
#define REG_TEST_H_

#include <Arduino.h>

// (after v111) Developer: chip registers nobody used yet, tried by hand (his request of 2026-11-17: the motion adaptive
// deinterlacer's MADPT_* bits and VDS_HALF_EN). Nothing is saved; a preset load writes them all back.
void regTestNext();            // the next register of the list
void regTestStep(int8_t dir);  // its value + 1 / - 1 (wraps round)
void regTestUndo();            // every register touched back to what it was before the first press
void regTestForget();          // called at preset load: the preset wrote them again
void regTestReport(char *buf, size_t n); // " rt=index:value:before:touched" for the data line

#endif
