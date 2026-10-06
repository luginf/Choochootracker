#include "chord.h"
#include <algorithm>

struct ChordDefinition {
  const char* name;
  uint8_t count;
  int8_t intervals[CHORD_MAX_VOICES];
};

static const ChordDefinition chords[16] = {
  {"Major",    3, {0, 4, 7, 0}},  {"Minor",    3, {0, 3, 7, 0}},
  {"Dim",      3, {0, 3, 6, 0}},  {"Aug",      3, {0, 4, 8, 0}},
  {"Sus2",     3, {0, 2, 7, 0}},  {"Sus4",     3, {0, 5, 7, 0}},
  {"Power",    2, {0, 7, 0, 0}},  {"Maj7",     4, {0, 4, 7, 11}},
  {"Min7",     4, {0, 3, 7, 10}}, {"Dom7",     4, {0, 4, 7, 10}},
  {"Min7b5",   4, {0, 3, 6, 10}}, {"Dim7",     4, {0, 3, 6, 9}},
  {"Add9",     4, {0, 4, 7, 14}}, {"MinAdd9",  4, {0, 3, 7, 14}},
  {"Maj9",     4, {0, 4, 11, 14}}, {"Min9",    4, {0, 3, 10, 14}},
};

const char* chordName(uint8_t slot) {
  return slot < 16 ? chords[slot].name : "Invalid";
}

uint8_t chordMaxInversion(uint8_t slot) {
  return slot < 16 ? chords[slot].count - 1 : 0;
}

int chordBuild(uint8_t root, uint8_t slot, uint8_t inversion, uint8_t pitchCount,
               uint8_t pitches[CHORD_MAX_VOICES]) {
  if (slot >= 16 || pitchCount == 0) return 0;
  const ChordDefinition& chord = chords[slot];
  int values[CHORD_MAX_VOICES];
  for (int i = 0; i < chord.count; ++i) values[i] = root + chord.intervals[i];

  int steps = std::min((int)inversion, (int)chordMaxInversion(slot));
  for (int step = 0; step < steps; ++step) {
    std::sort(values, values + chord.count);
    values[0] += 12;
  }
  std::sort(values, values + chord.count);
  for (int i = 0; i < chord.count; ++i) {
    values[i] = std::max(0, std::min((int)pitchCount - 1, values[i]));
    pitches[i] = (uint8_t)values[i];
  }
  return chord.count;
}
