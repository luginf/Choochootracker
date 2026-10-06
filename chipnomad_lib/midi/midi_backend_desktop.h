#ifndef __CHIPNOMAD_LIB__MIDI_BACKEND_DESKTOP_H__
#define __CHIPNOMAD_LIB__MIDI_BACKEND_DESKTOP_H__

#include "midi_router.h"

// The desktop (RtMidi-backed) MidiBackend, adapting midi_io.h's free
// functions to the router's function-pointer contract. Outside
// DESKTOP_BUILD, midi_io.h's own functions are already harmless no-ops, so
// this same backend instance works unchanged on every platform until a real
// Android/Web backend replaces it.
const MidiBackend* midiBackendDesktopGet(void);

#endif // __CHIPNOMAD_LIB__MIDI_BACKEND_DESKTOP_H__
