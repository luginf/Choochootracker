#include "doctest.h"

#include <import/import_midi.h>
#include <midi/smf_file.h>
#include <chipnomad_lib.h>

#include <cstdio>
#include <cstdlib>

TEST_SUITE("import_midi") {

TEST_CASE("imports notes on a single channel onto one track, quantized to the row grid") {
  SmfTrackWriter track;
  smfTrackInit(&track);
  smfTrackTempo(&track, 0, 500000); // 120 BPM
  smfTrackNoteOn(&track, 0, 0, 60, 100); // C4, channel 0
  smfTrackNoteOff(&track, 240, 0, 60, 0); // 480 PPQ / 4 rows-per-beat = 120 ticks/row -> 2 rows
  smfTrackEnd(&track, 0);

  const char* path = "/tmp/test_import_midi_note.mid";
  REQUIRE(smfWriteFile(path, 480, &track, 1) == 0);
  smfTrackFree(&track);

  Project p;
  REQUIRE(projectLoadMidi(&p, path) == 0);

  CHECK(p.song[0][0] != EMPTY_VALUE_16);
  int chainIdx = p.song[0][0];
  int phraseIdx = p.chains[chainIdx].rows[0].phrase;
  REQUIRE(phraseIdx != EMPTY_VALUE_16);

  // MIDI note 60 -> pitchTable index 48 (60 - 12), matching the 12TET layout
  // export_midi.cpp relies on (index N -> MIDI note 12+N).
  CHECK(p.phrases[phraseIdx].rows[0].note == 48);
  CHECK(p.phrases[phraseIdx].rows[0].instrument == 0);
  CHECK(p.phrases[phraseIdx].rows[1].note == EMPTY_VALUE_8); // held, no retrigger
  CHECK(p.phrases[phraseIdx].rows[2].note == NOTE_OFF);

  projectFree(&p);
  std::remove(path);
}

TEST_CASE("a retrigger on the same channel cuts the previous note without an explicit off") {
  SmfTrackWriter track;
  smfTrackInit(&track);
  smfTrackNoteOn(&track, 0, 0, 60, 100);
  smfTrackNoteOn(&track, 120, 0, 62, 100); // retrigger one row later, no note-off in between
  smfTrackNoteOff(&track, 120, 0, 62, 0);
  smfTrackEnd(&track, 0);

  const char* path = "/tmp/test_import_midi_retrigger.mid";
  REQUIRE(smfWriteFile(path, 480, &track, 1) == 0);
  smfTrackFree(&track);

  Project p;
  REQUIRE(projectLoadMidi(&p, path) == 0);

  int chainIdx = p.song[0][0];
  int phraseIdx = p.chains[chainIdx].rows[0].phrase;
  CHECK(p.phrases[phraseIdx].rows[0].note == 48); // MIDI 60
  CHECK(p.phrases[phraseIdx].rows[1].note == 50); // MIDI 62, cuts row 0's note directly
  CHECK(p.phrases[phraseIdx].rows[2].note == NOTE_OFF);

  projectFree(&p);
  std::remove(path);
}

TEST_CASE("a MIDI file with no notes is rejected") {
  SmfTrackWriter track;
  smfTrackInit(&track);
  smfTrackEnd(&track, 0);

  const char* path = "/tmp/test_import_midi_empty.mid";
  REQUIRE(smfWriteFile(path, 480, &track, 1) == 0);
  smfTrackFree(&track);

  Project p;
  CHECK(projectLoadMidi(&p, path) != 0);

  std::remove(path);
}

}
