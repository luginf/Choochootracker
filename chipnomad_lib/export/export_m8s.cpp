#include "export_m8s.h"
#include "../m8s_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char projectExportM8SError[48];

static int fail(const char* message) {
  snprintf(projectExportM8SError, sizeof(projectExportM8SError), "%s", message);
  return 1;
}

int projectExportM8S(Project* project, const char* templatePath, const char* outputPath) {
  if (!project || !templatePath || !outputPath) return fail("Invalid arguments");

  FILE* f = fopen(templatePath, "rb");
  if (!f) return fail("Cannot open M8 template");
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < M8S_MIN_FILE_SIZE || size > M8S_MAX_FILE_SIZE) {
    fclose(f);
    return fail("Template is not a valid M8 song");
  }
  uint8_t* data = (uint8_t*)malloc(size);
  size_t got = data ? fread(data, 1, size, f) : 0;
  fclose(f);
  if (!data || got != (size_t)size) {
    free(data);
    return fail("Cannot read M8 template");
  }

  int major = data[M8S_VERSION_MSB_OFFSET] & 0x0F;
  if (memcmp(data, M8S_MAGIC, strlen(M8S_MAGIC)) != 0 || major < 2 || major > 4) {
    free(data);
    return fail(major > 4 ? "Template version not supported" : "Template is not a valid M8 song");
  }

  // Phrases the M8 cannot hold: fail rather than silently drop notes.
  for (int row = 0; row < PROJECT_MAX_LENGTH; row++) {
    for (int t = 0; t < PROJECT_MAX_TRACKS; t++) {
      uint16_t chain = project->song[row][t];
      if (chain == EMPTY_VALUE_16 || chain >= PROJECT_MAX_CHAINS) continue;
      for (int s = 0; s < 16; s++) {
        uint16_t phrase = project->chains[chain].rows[s].phrase;
        if (phrase != EMPTY_VALUE_16 && phrase >= M8S_PHRASES) {
          free(data);
          return fail("Too many phrases for M8 (max 255)");
        }
      }
    }
  }

  // Tempo: the inverse of the importer (6 ticks per step, 4 steps per beat)
  float bpm = project->tickRate * 60.0f / (M8S_GROOVE_TICKS * M8S_STEPS_PER_BEAT);
  if (!(bpm == bpm)) bpm = 120.0f;
  if (bpm < 30.0f) bpm = 30.0f;
  if (bpm > 300.0f) bpm = 300.0f;
  memcpy(data + M8S_TEMPO_OFFSET, &bpm, sizeof(bpm));
  data[M8S_TRANSPOSE_OFFSET] = 0;

  memset(data + M8S_NAME_OFFSET, 0, M8S_NAME_SIZE);
  size_t titleLen = strlen(project->title);
  memcpy(data + M8S_NAME_OFFSET, project->title, titleLen < M8S_NAME_SIZE ? titleLen : M8S_NAME_SIZE);

  // Song rows
  for (int row = 0; row < M8S_SONG_ROWS; row++) {
    for (int t = 0; t < M8S_TRACKS; t++) {
      uint16_t chain = (row < PROJECT_MAX_LENGTH && t < PROJECT_MAX_TRACKS) ? project->song[row][t] : EMPTY_VALUE_16;
      data[M8S_SONG_OFFSET + row * M8S_TRACKS + t] = (chain < M8S_CHAINS && chain < PROJECT_MAX_CHAINS) ? (uint8_t)chain : M8S_EMPTY;
    }
  }

  // Chains
  for (int c = 0; c < M8S_CHAINS; c++) {
    for (int s = 0; s < M8S_CHAIN_STEPS; s++) {
      uint8_t* step = data + M8S_CHAINS_OFFSET + (c * M8S_CHAIN_STEPS + s) * 2;
      uint16_t phrase = c < PROJECT_MAX_CHAINS ? project->chains[c].rows[s].phrase : EMPTY_VALUE_16;
      if (phrase != EMPTY_VALUE_16 && phrase < M8S_PHRASES) {
        step[0] = (uint8_t)phrase;
        step[1] = project->chains[c].rows[s].transpose;
      } else {
        step[0] = M8S_EMPTY;
        step[1] = 0;
      }
    }
  }

  // Phrases. The importer's inverse: M8 value = pitch table index + 12.
  for (int ph = 0; ph < M8S_PHRASES; ph++) {
    for (int s = 0; s < M8S_PHRASE_STEPS; s++) {
      uint8_t* step = data + M8S_PHRASES_OFFSET + (ph * M8S_PHRASE_STEPS + s) * M8S_PHRASE_STEP_SIZE;
      memset(step, M8S_EMPTY, M8S_PHRASE_STEP_SIZE);
      for (int i = 0; i < 3; i++) step[3 + i * 2 + 1] = 0; // FX value 0 next to the empty FX command

      if (ph >= PROJECT_MAX_PHRASES) continue;
      const PhraseRow* row = &project->phrases[ph].rows[s];
      if (row->note == NOTE_OFF) {
        step[0] = M8S_NOTE_OFF;
      } else if (row->note != EMPTY_VALUE_8) {
        int note = row->note + M8S_INDEX_OFFSET;
        step[0] = (uint8_t)(note < 0 ? 0 : note > 127 ? 127 : note);
        if (row->volume != EMPTY_VALUE_16) step[1] = (uint8_t)(row->volume > PHRASE_VOLUME_MAX ? PHRASE_VOLUME_MAX : row->volume);
        if (row->instrument != EMPTY_VALUE_8 && row->instrument < M8S_INSTRUMENTS) step[2] = row->instrument;
      }
    }
  }

  FILE* out = fopen(outputPath, "wb");
  if (!out) {
    free(data);
    return fail("Cannot write M8 file");
  }
  size_t written = fwrite(data, 1, size, out);
  int closeResult = fclose(out);
  free(data);
  if (written != (size_t)size || closeResult != 0) return fail("Cannot write M8 file");
  return 0;
}
