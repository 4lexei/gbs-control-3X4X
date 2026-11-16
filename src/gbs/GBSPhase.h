#ifndef GBS_PHASE_H
#define GBS_PHASE_H

#include <Arduino.h>
#include "../core/State.h"

// GBS phase adjustment functions
void advancePhase();
void movePhaseThroughRange();
void setAndLatchPhaseSP();
void setAndLatchPhaseADC();
// [patch image-tuning] -1 = automatic, 0..31 = fixed ADC sampling phase chosen by the user
extern int8_t manualPhaseADC;
void nudgeMD();
void updateSpDynamic(boolean withCurrentVideoModeCheck);
void updateCoastPosition(boolean autoCoast);
void updateClampPosition();
void togglePhaseAdjustUnits();
void setAndUpdateSogLevel(uint8_t level);

#endif // GBS_PHASE_H

