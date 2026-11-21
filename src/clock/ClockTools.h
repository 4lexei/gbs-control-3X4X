#ifndef CLOCK_TOOLS_H_
#define CLOCK_TOOLS_H_

#include <Arduino.h>

// (after v111b) his request of 2026-11-18: numbers he can type in the web instead of waiting for a build.
//  - the exact output rate (output mode 3, "exacta"), in ten-thousandths of a hertz, saved
//  - the ADC clock (PLLAD_MD), live, Developer only, not saved
//  - the Si5351 output clock, live, Developer only, not saved
// The web sends them on /set (WebServer.cpp); they are applied from the main loop, never inside the web callback.
extern volatile uint32_t clockToolsPendingHz;  // 0 = nothing pending
extern volatile uint16_t clockToolsPendingAdc; // 0 = nothing pending
extern volatile uint32_t clockToolsPendingSi;  // 0 = nothing pending

void clockToolsTick();
void clockToolsReport(char *buf, size_t n); // " hzx=… adc=… si=…" for the data line

#endif
