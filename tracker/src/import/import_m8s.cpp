#include <chipnomad_lib.h>
#include <project_utils.h>
#include "import_m8s.h"
#include "import_common.h"
#include <m8s_format.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copyName(char* dest, size_t destSize, const uint8_t* src, size_t srcSize) {
  size_t len = 0;
  while (len < srcSize && src[len] != 0 && src[len] != M8S_EMPTY && len < destSize - 1) {
    dest[len] = (char)src[len];
    len++;
  }
  dest[len] = 0;
}

int projectLoadM8S(Project* project, const char* path) {
  if (!project || !path) return 1;

  FILE* f = fopen(path, "rb");
  if (!f) {
    snprintf(projectFileError, 40, "Cannot open M8 file");
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < M8S_MIN_FILE_SIZE || size > M8S_MAX_FILE_SIZE) {
    fclose(f);
    snprintf(projectFileError, 40, "Not a valid M8 song");
    return 1;
  }
  uint8_t* data = (uint8_t*)malloc(size);
  size_t got = data ? fread(data, 1, size, f) : 0;
  fclose(f);
  if (!data || got != (size_t)size) {
    free(data);
    snprintf(projectFileError, 40, "Cannot read M8 file");
    return 1;
  }

  int major = data[M8S_VERSION_MSB_OFFSET] & 0x0F;
  if (memcmp(data, M8S_MAGIC, strlen(M8S_MAGIC)) != 0 || major < 2 || major > 4) {
    free(data);
    snprintf(projectFileError, 40, major > 4 ? "M8 version not supported" : "Not a valid M8 song");
    return 1;
  }

  Project p;
  projectInitAY(&p);

  float bpm;
  memcpy(&bpm, data + M8S_TEMPO_OFFSET, sizeof(bpm));
  if (!(bpm >= 30.0f && bpm <= 300.0f)) bpm = 120.0f;
  double stepSeconds = 60.0 / bpm / M8S_STEPS_PER_BEAT;
  p.tickRate = (float)(M8S_GROOVE_TICKS / stepSeconds);

  copyName(p.title, sizeof(p.title), data + M8S_NAME_OFFSET, M8S_NAME_SIZE);

  // Song: one chain index per track, 0xFF empty.
  for (int row = 0; row < M8S_SONG_ROWS && row < PROJECT_MAX_LENGTH; row++) {
    for (int t = 0; t < M8S_TRACKS && t < PROJECT_MAX_TRACKS; t++) {
      uint8_t chain = data[M8S_SONG_OFFSET + row * M8S_TRACKS + t];
      p.song[row][t] = (chain < M8S_CHAINS && chain < PROJECT_MAX_CHAINS) ? chain : EMPTY_VALUE_16;
    }
  }

  // Chains: (phrase, transpose) pairs. Transpose is a signed semitone offset.
  for (int c = 0; c < M8S_CHAINS && c < PROJECT_MAX_CHAINS; c++) {
    chainClear(&p.chains[c]);
    for (int s = 0; s < M8S_CHAIN_STEPS; s++) {
      const uint8_t* step = data + M8S_CHAINS_OFFSET + (c * M8S_CHAIN_STEPS + s) * 2;
      if (step[0] < M8S_PHRASES) {
        p.chains[c].rows[s].phrase = step[0];
        p.chains[c].rows[s].transpose = step[1];
      }
    }
  }

  // Phrases: note, velocity, instrument, then three FX pairs (ignored).
  // An M8 note value is a MIDI note number (0 displays as C-1), while pitch
  // table index N is MIDI note 12+N (same rule as import_midi.cpp), so the
  // index is the M8 value minus 12. The lowest octave (sub-audio) clamps to 0.
  uint8_t instrumentUsed[M8S_INSTRUMENTS] = {0};
  for (int ph = 0; ph < M8S_PHRASES && ph < PROJECT_MAX_PHRASES; ph++) {
    phraseClear(&p.phrases[ph]);
    for (int s = 0; s < M8S_PHRASE_STEPS; s++) {
      const uint8_t* step = data + M8S_PHRASES_OFFSET + (ph * M8S_PHRASE_STEPS + s) * M8S_PHRASE_STEP_SIZE;
      PhraseRow* dest = &p.phrases[ph].rows[s];
      initEmptyPhraseRow(dest);

      uint8_t note = step[0];
      if (note == M8S_EMPTY) continue;
      if (note >= M8S_NOTE_OFF) {
        dest->note = NOTE_OFF;
        continue;
      }
      int index = note >= 12 ? note - 12 : 0;
      if (p.pitchTable.length > 0 && index >= p.pitchTable.length) index = p.pitchTable.length - 1;
      dest->note = (uint8_t)index;
      if (step[1] != M8S_EMPTY) dest->volume = step[1] > PHRASE_VOLUME_MAX ? PHRASE_VOLUME_MAX : step[1];
      if (step[2] < M8S_INSTRUMENTS && step[2] < PROJECT_MAX_INSTRUMENTS) {
        dest->instrument = step[2];
        instrumentUsed[step[2]] = 1;
      }
    }
  }

  // Instruments: every referenced slot gets a default AY instrument named
  // after the M8 instrument, so notes are audible right away.
  for (int i = 0; i < M8S_INSTRUMENTS && i < PROJECT_MAX_INSTRUMENTS; i++) {
    if (!instrumentUsed[i]) continue;
    Instrument* inst = &p.instruments[i];
    getInstrumentFunctions(InstrumentType::AY1).init(inst);
    inst->type = InstrumentType::AY1;
    const uint8_t* src = data + M8S_INSTRUMENTS_OFFSET + i * M8S_INSTRUMENT_SIZE;
    char name[M8S_INSTRUMENT_NAME_SIZE + 1];
    copyName(name, sizeof(name), src + 1, M8S_INSTRUMENT_NAME_SIZE);
    if (name[0]) snprintf(inst->name, sizeof(inst->name), "%s", name);
    else snprintf(inst->name, sizeof(inst->name), "M8 %02X", i);
  }

  free(data);
  projectFree(project);
  *project = p;
  return 0;
}
