#ifndef __CHIPNOMAD_LIB__MIDI_SMF_FILE_H__
#define __CHIPNOMAD_LIB__MIDI_SMF_FILE_H__

#include <stdint.h>

// Minimal Standard MIDI File (SMF) reader/writer. No knowledge of Project:
// this only deals in raw ticks/channels/notes, so it can be reused outside
// the cct<->midi converter.

#define SMF_MAX_CHANNELS 16

extern char smfFileError[128];

// --- Writing ---
//
// Build one SmfTrackWriter per MIDI track, add events in increasing time
// order (each call's deltaTicks is relative to the previous event on that
// same track), then pass all tracks to smfWriteFile().

struct SmfTrackWriter {
  uint8_t* data;
  uint32_t length;
  uint32_t capacity;
};

void smfTrackInit(SmfTrackWriter* track);
void smfTrackFree(SmfTrackWriter* track);

void smfTrackNoteOn(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t channel, uint8_t note, uint8_t velocity);
void smfTrackNoteOff(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t channel, uint8_t note, uint8_t velocity);
void smfTrackProgramChange(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t channel, uint8_t program);
void smfTrackTempo(SmfTrackWriter* track, uint32_t deltaTicks, uint32_t microsecondsPerQuarter);
void smfTrackText(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t metaType, const char* text);
void smfTrackEnd(SmfTrackWriter* track, uint32_t deltaTicks);

// Writes a format-0 file if trackCount == 1, format-1 otherwise. Returns 0 on
// success, non-zero (with smfFileError set) on failure.
int smfWriteFile(const char* path, uint16_t ppq, SmfTrackWriter* tracks, int trackCount);

// --- Reading ---
//
// Events are bucketed by MIDI channel (0-15), regardless of how many track
// chunks the source file used (format 0 or 1) - that's what matters for
// mapping onto tracker tracks. Tempo meta events are global, so they are
// collected separately in absolute-tick order.

enum class SmfEventType : uint8_t { noteOn, noteOff };

struct SmfNoteEvent {
  uint64_t absoluteTick;
  SmfEventType type;
  uint8_t note;
  uint8_t velocity;
};

struct SmfNoteEventList {
  SmfNoteEvent* events;
  int count;
  int capacity;
};

struct SmfTempoChange {
  uint64_t absoluteTick;
  uint32_t microsecondsPerQuarter;
};

struct SmfTempoChangeList {
  SmfTempoChange* changes;
  int count;
  int capacity;
};

struct SmfReadResult {
  uint16_t ppq;
  SmfNoteEventList channels[SMF_MAX_CHANNELS];
  SmfTempoChangeList tempoChanges;
};

// Returns 0 on success (with smfFileError set on failure).
int smfReadFile(const char* path, SmfReadResult* result);
void smfFreeReadResult(SmfReadResult* result);

#endif // __CHIPNOMAD_LIB__MIDI_SMF_FILE_H__
