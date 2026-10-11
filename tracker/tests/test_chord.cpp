#include "doctest.h"
#include "chipnomad_lib.h"
#include "playback_internal.h"

TEST_SUITE("chord") {

TEST_CASE("fixed chord palette and inversions") {
  uint8_t pitches[CHORD_MAX_VOICES] = {};
  CHECK(chordBuild(36, 0, 0, 96, pitches) == 3);
  CHECK(pitches[0] == 36);
  CHECK(pitches[1] == 40);
  CHECK(pitches[2] == 43);
  CHECK(chordBuild(36, 0, 1, 96, pitches) == 3);
  CHECK(pitches[0] == 40);
  CHECK(pitches[1] == 43);
  CHECK(pitches[2] == 48);
  CHECK(chordBuild(36, 0, 0x04, 96, pitches) == 3); // Drop-2.
  CHECK(pitches[0] == 28);
  CHECK(pitches[1] == 36);
  CHECK(pitches[2] == 43);
  CHECK(chordBuild(36, 0, 0x08, 96, pitches) == 3); // Drop-3.
  CHECK(pitches[0] == 24);
  CHECK(pitches[1] == 40);
  CHECK(pitches[2] == 43);
  CHECK(chordBuild(36, 7, 0x0d, 96, pitches) == 4); // First inversion, drop-2 and drop-3.
  CHECK(pitches[0] == 31);
  CHECK(pitches[1] == 35);
  CHECK(pitches[2] == 40);
  CHECK(pitches[3] == 48);
  uint32_t voicings[16] = {};
  for (uint8_t inversion = 0; inversion < 16; ++inversion) {
    CHECK(chordBuild(36, 0, inversion, 96, pitches) == 3);
    voicings[inversion] = pitches[0] | (pitches[1] << 8) | (pitches[2] << 16);
    for (uint8_t earlier = 0; earlier < inversion; ++earlier)
      CHECK(voicings[inversion] != voicings[earlier]);
  }
  CHECK(chordBuild(36, 7, 0, 96, pitches) == 4);
  CHECK(pitches[3] == 47);
  CHECK(chordMaxInversion(0) == 2);
  CHECK(chordMaxInversion(7) == 3);
  for (uint8_t slot = 0; slot < 16; ++slot) {
    int count = chordBuild(36, slot, 0, 96, pitches);
    CHECK(count >= 2);
    CHECK(count <= CHORD_MAX_VOICES);
  }
  CHECK(chordBuild(36, 0, 0x0f, 96, pitches) == 3);
  CHECK(pitches[0] == 36);
  CHECK(chordBuild(0, 0, 0x0f, 96, pitches) == 3);
  CHECK(pitches[0] == 0);
}

TEST_CASE("CRD is same-row only and quantizes every chord note") {
  Project project;
  projectInit(&project);
  project.pitchTable.octaveSize = 12;
  project.pitchTable.length = 96;
  project.scaleApply = 1;
  project.scalePreset = scaleMinor;
  PlaybackState state = {};
  playbackInit(&state, &project);
  state.tracks[0].mode = PlaybackMode::phraseRow;
  PhraseRow row = {};
  row.note = 36; row.instrument = EMPTY_VALUE_8; row.volume = EMPTY_VALUE_16;
  for (int i = 0; i < 3; ++i) row.fx[i][0] = EMPTY_VALUE_8;
  row.fx[0][0] = fxCRD; row.fx[0][1] = 0x00;
  readPhraseRowDirect(&state, 0, &row, 0);
  CHECK(row.note == 36);
  CHECK(state.tracks[0].chordVoiceCount == 3);
  CHECK(state.tracks[0].chordPitchBase[0] == 36);
  CHECK(state.tracks[0].chordPitchBase[1] == 39);
  CHECK(state.tracks[0].chordPitchBase[2] == 43);

  row.note = 38; row.fx[0][0] = EMPTY_VALUE_8;
  readPhraseRowDirect(&state, 0, &row, 0);
  CHECK(state.tracks[0].chordVoiceCount == 1);
  CHECK(state.tracks[0].chordPitchBase[0] == 38);
}

TEST_CASE("CRD is ignored by AY instruments") {
  Project project;
  projectInit(&project);
  project.pitchTable.octaveSize = 12;
  project.pitchTable.length = 96;
  project.instruments[0].type = InstrumentType::AY1;
  PlaybackState state = {};
  playbackInit(&state, &project);
  state.tracks[0].mode = PlaybackMode::phraseRow;
  PhraseRow row = {};
  row.note = 36; row.instrument = 0; row.volume = EMPTY_VALUE_16;
  for (int i = 0; i < 3; ++i) row.fx[i][0] = EMPTY_VALUE_8;
  row.fx[0][0] = fxCRD; row.fx[0][1] = 0x00;
  readPhraseRowDirect(&state, 0, &row, 0);
  CHECK(state.tracks[0].chordVoiceCount == 1);
}

TEST_CASE("STA aliases SST") {
  Project project;
  projectInit(&project);
  PlaybackState state = {};
  playbackInit(&state, &project);
  uint8_t fx[] = {fxSTA, 0x42};
  initFX(&state, 0, fx, NULL, 0);
  CHECK(state.tracks[0].note.fx[fxSST].isOn);
  CHECK(state.tracks[0].note.fx[fxSST].fxValue == 0x42);
  CHECK(!state.tracks[0].note.fx[fxSTA].isOn);
}

} // TEST_SUITE
