#include "doctest.h"
#include "chipnomad_lib.h"
#include "playback_internal.h"

#include <cstring>

TEST_SUITE("scale") {

TEST_CASE("preset masks include their expected scale notes") {
  CHECK(scalePresetMask(scaleMajor) == 0x0ab5);
  CHECK(scalePresetMask(scaleMinor) == 0x05ad);
  CHECK(scalePresetMask(scaleWholeTone) == 0x0555);
}

TEST_CASE("quantizer rounds down with root and octave wrap") {
  uint16_t major = scalePresetMask(scaleMajor);
  CHECK(scaleQuantizeNote(3, 0, major, 96) == 2);   // D# -> D in C major
  CHECK(scaleQuantizeNote(3, 2, major, 96) == 2);   // D# -> D in D major
  CHECK(scaleQuantizeNote(1, 1, major, 96) == 1);   // Root is always admitted
  CHECK(scaleQuantizeNote(0, 1, 1u << 11, 96) == 0); // clamp below the first matching note
}

TEST_CASE("SCL changes runtime scale without changing phrase data") {
  Project project;
  projectInit(&project);
  project.pitchTable.octaveSize = 12;
  project.pitchTable.length = 96;
  project.scaleApply = 1;
  project.scalePreset = scaleChromatic;

  PlaybackState state = {};
  playbackInit(&state, &project);
  state.tracks[0].mode = PlaybackMode::phraseRow;

  PhraseRow row = {};
  row.note = 3; // D#
  row.instrument = EMPTY_VALUE_8;
  row.volume = EMPTY_VALUE_16;
  for (int i = 0; i < 3; ++i) row.fx[i][0] = EMPTY_VALUE_8;
  row.fx[0][0] = fxSCL;
  row.fx[0][1] = 0x10; // Major, C

  readPhraseRowDirect(&state, 0, &row, 0);
  CHECK(row.note == 3);
  CHECK(state.scalePreset == scaleMajor);
  CHECK(state.scaleRoot == 0);
  CHECK(state.tracks[0].note.pitchBase == 2);
}

TEST_CASE("sliced sample notes skip scale quantization") {
  Project project;
  projectInit(&project);
  project.pitchTable.octaveSize = 12;
  project.pitchTable.length = 96;
  project.scaleApply = 1;
  project.scalePreset = scaleMajor;
  getInstrumentFunctions(InstrumentType::Sample).init(&project.instruments[0]);
  project.instruments[0].chip.sample.slice = 8;

  PlaybackState state = {};
  playbackInit(&state, &project);
  state.tracks[0].mode = PlaybackMode::phraseRow;

  PhraseRow row = {};
  row.note = 3;
  row.instrument = 0;
  row.volume = EMPTY_VALUE_16;
  for (int i = 0; i < 3; ++i) row.fx[i][0] = EMPTY_VALUE_8;
  readPhraseRowDirect(&state, 0, &row, 0);
  CHECK(state.tracks[0].note.pitchBase == 3);

  project.instruments[0].chip.sample.slice = 0;
  readPhraseRowDirect(&state, 0, &row, 0);
  CHECK(state.tracks[0].note.pitchBase == 2);
}

TEST_CASE("track scale mask leaves excluded tracks chromatic") {
  Project project;
  projectInit(&project);
  project.pitchTable.octaveSize = 12;
  project.pitchTable.length = 96;
  project.scaleApply = 1;
  project.scaleTracksMask = 0x01;
  project.scalePreset = scaleMajor;
  PlaybackState state = {};
  playbackInit(&state, &project);
  state.tracks[1].mode = PlaybackMode::phraseRow;
  PhraseRow row = {};
  row.note = 3; row.instrument = EMPTY_VALUE_8; row.volume = EMPTY_VALUE_16;
  for (int i = 0; i < 3; ++i) row.fx[i][0] = EMPTY_VALUE_8;
  readPhraseRowDirect(&state, 1, &row, 0);
  CHECK(state.tracks[1].note.pitchBase == 3);
}

TEST_CASE("lowest track SCL command wins during a playback frame") {
  Project project;
  projectInit(&project);
  project.pitchTable.octaveSize = 12;
  project.pitchTable.length = 96;
  PlaybackState state = {};
  playbackInit(&state, &project);
  state.scaleFXCommandSeen = 0;
  state.tracks[0].mode = state.tracks[1].mode = PlaybackMode::phraseRow;

  PhraseRow first = {}, second = {};
  for (int i = 0; i < 3; ++i) { first.fx[i][0] = EMPTY_VALUE_8; second.fx[i][0] = EMPTY_VALUE_8; }
  first.fx[0][0] = fxSCL; first.fx[0][1] = 0x10;
  second.fx[0][0] = fxSCL; second.fx[0][1] = 0x2b;
  readPhraseRowDirect(&state, 0, &first, 0);
  readPhraseRowDirect(&state, 1, &second, 0);
  CHECK(state.scalePreset == scaleMajor);
  CHECK(state.scaleRoot == 0);
}

} // TEST_SUITE
