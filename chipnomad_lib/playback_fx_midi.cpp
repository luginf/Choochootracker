#include "playback_internal.h"

// MC1-MC4: send a MIDI CC whose number is set per-instrument (see
// InstrumentMidi::ccNumber). A row FX only carries one 0-255 value, so this
// just records "slot N wants to send fx->fxValue" for chipnomad_lib.cpp's
// applyVoiceEvents() to actually send - that's the layer that knows about
// real MIDI I/O and has a wall-clock due time for it (see midi_io.h), which
// playback.cpp deliberately doesn't depend on.
static void initFX_MidiCC(PlaybackTrackState* track, PlaybackFXState* fx, int slot) {
  track->midiCCPending[slot] = 1;
  track->midiCCValue[slot] = fx->fxValue;
}

static void initFX_MC1(PlaybackState*, PlaybackTrackState* track, int, PlaybackFXState* fx, PlaybackTableState*, int) { initFX_MidiCC(track, fx, 0); }
static void initFX_MC2(PlaybackState*, PlaybackTrackState* track, int, PlaybackFXState* fx, PlaybackTableState*, int) { initFX_MidiCC(track, fx, 1); }
static void initFX_MC3(PlaybackState*, PlaybackTrackState* track, int, PlaybackFXState* fx, PlaybackTableState*, int) { initFX_MidiCC(track, fx, 2); }
static void initFX_MC4(PlaybackState*, PlaybackTrackState* track, int, PlaybackFXState* fx, PlaybackTableState*, int) { initFX_MidiCC(track, fx, 3); }

void registerFXHandlers_Midi(void) {
  fxHandlers[fxMC1] = (PlaybackFXHandler){initFX_MC1, NULL, NULL};
  fxHandlers[fxMC2] = (PlaybackFXHandler){initFX_MC2, NULL, NULL};
  fxHandlers[fxMC3] = (PlaybackFXHandler){initFX_MC3, NULL, NULL};
  fxHandlers[fxMC4] = (PlaybackFXHandler){initFX_MC4, NULL, NULL};
}
