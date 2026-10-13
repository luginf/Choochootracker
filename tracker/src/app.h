#ifndef __APP_H__
#define __APP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "common.h"
#include "corelib/corelib_mainloop.h"
#include "corelib/corelib_input.h"

void appSetup(void);
void appCleanup(void);
void appDraw(void);
void appSetStickLiveMode(StickLiveMode mode);
void appOnEvent(MainLoopEventData eventData);
// Drop held-button state after changing the input mapping. A key-up event may
// otherwise resolve through the new mapping instead of the old one.
void appResetInputState(void);
// The physical input currently being dispatched to the active screen.
InputCode appCurrentInput(void);

// Audition a note live on an idle track while the song is playing the current
// one (key jazz recording over the playing loop). Off releases it.
void appMonitorNoteOn(uint8_t note, int instrument);
void appMonitorNoteOff(uint8_t note);

// Raw input callback for key mapping screen
extern void (*inputRawCallback)(InputCode input, int isDown);


#ifdef __cplusplus
}
#endif

#endif
