#ifndef __APP_H__
#define __APP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "corelib/corelib_mainloop.h"
#include "corelib/corelib_input.h"

void appSetup(void);
void appCleanup(void);
void appDraw(void);
void appSetStickLiveMode(StickLiveMode mode);
void appOnEvent(MainLoopEventData eventData);
// Clears the MIDI-in legato/last-note-priority held-note stack. Call
// whenever the input device is closed, changed, or reopened, so a note held
// across the change can't leave a virtual note stuck on (see
// MainLoopEvent::tick's MIDI-in handling in app.cpp).
void appMidiInResetHeldNotes(void);

// Raw input callback for key mapping screen
extern void (*inputRawCallback)(InputCode input, int isDown);


#ifdef __cplusplus
}
#endif

#endif
