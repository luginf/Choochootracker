#include "export_midi.h"
#include "../midi/smf_file.h"
#include "../project_constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIDI_PPQ 480
#define MIDI_ROWS_PER_BEAT 4
#define MIDI_TICKS_PER_ROW (MIDI_PPQ / MIDI_ROWS_PER_BEAT)
#define MIDI_ROWS_PER_SONG_ROW (16 * 16) // 16 chain rows x 16 phrase rows
#define MIDI_DEFAULT_GROOVE_TICKS 6

// PitchTable index -> MIDI note. The app's own 12TET table generators
// (tracker/src/pitch_table_utils.cpp: calculatePitchTableAY/
// calculateLinearPitchTable12TET) lay out entry N as MIDI note 12+N; custom
// or microtonal tables are approximated with the same rule.
static uint8_t noteIndexToMidi(int noteIndex) {
  int midi = 12 + noteIndex;
  if (midi < 0) midi = 0;
  if (midi > 127) midi = 127;
  return (uint8_t)midi;
}

// PhraseRow.volume is a 0-15 value (see chipnomad_lib/playback.cpp, where
// 0x0f/15 is the "full volume" sentinel used for accent detection).
static uint8_t volumeToVelocity(uint8_t volume) {
  if (volume == EMPTY_VALUE_8 || volume > 15) volume = 15;
  int v = (volume * 127 + 7) / 15;
  if (v < 1) v = 1;
  if (v > 127) v = 127;
  return (uint8_t)v;
}

// Highest song row (0..PROJECT_MAX_LENGTH-1) referenced by any track, or -1
// if the arrangement is empty.
static int lastUsedSongRow(Project* p) {
  int last = -1;
  for (int row = 0; row < PROJECT_MAX_LENGTH; row++) {
    for (int t = 0; t < p->tracksCount; t++) {
      if (p->song[row][t] != EMPTY_VALUE_16) last = row;
    }
  }
  return last;
}

// Resolves the phrase row (if any) active at a given (songRow, chainRow,
// phraseRowIdx) position for one track, and its chain transpose. Returns
// NULL if no phrase is active there (silence/hold continuation).
static PhraseRow* resolveRow(Project* p, int track, int songRow, int chainRow, int phraseRowIdx, int8_t* outTranspose) {
  uint16_t chainIdx = p->song[songRow][track];
  if (chainIdx == EMPTY_VALUE_16) return NULL;
  ChainRow* cr = &p->chains[chainIdx].rows[chainRow];
  if (cr->phrase == EMPTY_VALUE_16) return NULL;
  *outTranspose = cr->transpose;
  return &p->phrases[cr->phrase].rows[phraseRowIdx];
}

// Advances a groove cursor by one row, mirroring the skip/wrap rule in
// playback.cpp (empty groove slots wrap back to the start of the table).
struct GrooveCursor {
  uint8_t grooveIdx;
  uint8_t grooveRow;
};

static uint8_t grooveCursorNext(Project* p, GrooveCursor* g) {
  Groove* groove = &p->grooves[g->grooveIdx];
  if (groove->speed[g->grooveRow] == EMPTY_VALUE_8) g->grooveRow = 0;
  uint8_t ticks = groove->speed[g->grooveRow];
  if (ticks == EMPTY_VALUE_8) ticks = MIDI_DEFAULT_GROOVE_TICKS;
  g->grooveRow++;
  if (g->grooveRow >= 16 || groove->speed[g->grooveRow] == EMPTY_VALUE_8) g->grooveRow = 0;
  return ticks;
}

// Scans a phrase row's fx slots for a global groove change (fxGGR).
static int phraseRowGlobalGroove(PhraseRow* row) {
  for (int i = 0; i < 3; i++) {
    if (row->fx[i][0] == fxGGR) return row->fx[i][1] & (PROJECT_MAX_GROOVES - 1);
  }
  return -1;
}

// Builds the tempo map (conductor track): walks every track at every
// absolute row looking for fxGGR, and emits a tempo meta-event each time the
// effective row duration changes.
static void writeConductorTrack(Project* p, SmfTrackWriter* conductor, int lastSongRow, int trackCount) {
  GrooveCursor groove = { 0, 0 };
  uint32_t lastEventTick = 0;
  int haveLast = 0;
  int lastTicksPerRow = -1;
  uint32_t absoluteRow = 0;

  for (int songRow = 0; songRow <= lastSongRow; songRow++) {
    for (int chainRow = 0; chainRow < 16; chainRow++) {
      for (int phraseRowIdx = 0; phraseRowIdx < 16; phraseRowIdx++) {
        for (int t = 0; t < trackCount; t++) {
          int8_t transpose;
          PhraseRow* row = resolveRow(p, t, songRow, chainRow, phraseRowIdx, &transpose);
          if (!row) continue;
          int newGroove = phraseRowGlobalGroove(row);
          if (newGroove >= 0) {
            groove.grooveIdx = (uint8_t)newGroove;
            groove.grooveRow = 0;
          }
        }

        uint8_t grooveTicks = grooveCursorNext(p, &groove);
        if (grooveTicks != lastTicksPerRow) {
          lastTicksPerRow = grooveTicks;
          double rowSeconds = (double)grooveTicks / (p->tickRate > 0 ? p->tickRate : 60.0);
          double quarterSeconds = rowSeconds * MIDI_ROWS_PER_BEAT;
          uint32_t microsecondsPerQuarter = (uint32_t)(quarterSeconds * 1000000.0);
          uint32_t tick = absoluteRow * MIDI_TICKS_PER_ROW;
          smfTrackTempo(conductor, haveLast ? tick - lastEventTick : tick, microsecondsPerQuarter);
          lastEventTick = tick;
          haveLast = 1;
        }
        absoluteRow++;
      }
    }
  }

  smfTrackEnd(conductor, absoluteRow * MIDI_TICKS_PER_ROW - lastEventTick);
}

static void writeNoteTrack(Project* p, SmfTrackWriter* track, int trackIndex, int lastSongRow) {
  uint8_t channel = trackIndex & 0x0f;
  smfTrackProgramChange(track, 0, channel, 0);

  // Track name = first instrument used on this track, for reference only.
  char trackName[64];
  snprintf(trackName, sizeof(trackName), "Track %d", trackIndex + 1);
  for (int songRow = 0; songRow <= lastSongRow; songRow++) {
    int8_t transpose;
    int found = 0;
    for (int chainRow = 0; chainRow < 16 && !found; chainRow++) {
      for (int phraseRowIdx = 0; phraseRowIdx < 16 && !found; phraseRowIdx++) {
        PhraseRow* row = resolveRow(p, trackIndex, songRow, chainRow, phraseRowIdx, &transpose);
        if (row && row->instrument != EMPTY_VALUE_8 && row->instrument < PROJECT_MAX_INSTRUMENTS) {
          snprintf(trackName, sizeof(trackName), "Track %d - %s", trackIndex + 1, p->instruments[row->instrument].name);
          found = 1;
        }
      }
    }
    if (found) break;
  }
  smfTrackText(track, 0, 0x03, trackName);

  uint32_t lastEventTick = 0;
  int currentNote = -1;
  uint32_t absoluteRow = 0;

  for (int songRow = 0; songRow <= lastSongRow; songRow++) {
    for (int chainRow = 0; chainRow < 16; chainRow++) {
      for (int phraseRowIdx = 0; phraseRowIdx < 16; phraseRowIdx++) {
        int8_t transpose = 0;
        PhraseRow* row = resolveRow(p, trackIndex, songRow, chainRow, phraseRowIdx, &transpose);
        uint32_t tick = absoluteRow * MIDI_TICKS_PER_ROW;

        if (row) {
          if (row->note == NOTE_OFF) {
            if (currentNote >= 0) {
              smfTrackNoteOff(track, tick - lastEventTick, channel, (uint8_t)currentNote, 0);
              lastEventTick = tick;
              currentNote = -1;
            }
          } else if (row->note != EMPTY_VALUE_8) {
            if (currentNote >= 0) {
              smfTrackNoteOff(track, tick - lastEventTick, channel, (uint8_t)currentNote, 0);
              lastEventTick = tick;
            }
            int effectiveNote = (int)row->note + transpose;
            uint8_t midiNote = noteIndexToMidi(effectiveNote);
            uint8_t velocity = volumeToVelocity(row->volume);
            smfTrackNoteOn(track, tick - lastEventTick, channel, midiNote, velocity);
            lastEventTick = tick;
            currentNote = midiNote;
          }
        }

        absoluteRow++;
      }
    }
  }

  uint32_t endTick = absoluteRow * MIDI_TICKS_PER_ROW;
  if (currentNote >= 0) {
    smfTrackNoteOff(track, endTick - lastEventTick, channel, (uint8_t)currentNote, 0);
    lastEventTick = endTick;
  }
  smfTrackEnd(track, endTick - lastEventTick);
}

int projectExportMidi(Project* project, const char* path) {
  int lastRow = lastUsedSongRow(project);
  if (lastRow < 0) {
    snprintf(smfFileError, sizeof(smfFileError), "Song is empty, nothing to export");
    return 1;
  }

  int trackCount = project->tracksCount;
  if (trackCount < 1) trackCount = 1;
  if (trackCount > PROJECT_MAX_TRACKS) trackCount = PROJECT_MAX_TRACKS;

  int midiTrackCount = trackCount + 1; // + conductor track
  SmfTrackWriter* tracks = (SmfTrackWriter*)malloc(sizeof(SmfTrackWriter) * midiTrackCount);
  for (int i = 0; i < midiTrackCount; i++) smfTrackInit(&tracks[i]);

  writeConductorTrack(project, &tracks[0], lastRow, trackCount);
  for (int t = 0; t < trackCount; t++) {
    writeNoteTrack(project, &tracks[t + 1], t, lastRow);
  }

  int result = smfWriteFile(path, MIDI_PPQ, tracks, midiTrackCount);

  for (int i = 0; i < midiTrackCount; i++) smfTrackFree(&tracks[i]);
  free(tracks);

  return result;
}
