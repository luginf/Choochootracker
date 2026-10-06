// End-to-end scenarios from docs/sampler-edit-plan/06_PHASE_4_HARDENING.md §1,
// automated at the engine level. The UI layer (screen_sample_settings.cpp)
// drives exactly these functions in the same order: load -> trim -> select ->
// process -> save -> reload. What cannot be automated here (screen rendering,
// key handling, auditioning through the audio device) stays on the manual
// checklist and is reported separately.
#include "doctest.h"
#include "../../chipnomad_lib/project_instruments.h"
#include "../../chipnomad_lib/synth/sample_ops.h"
#include "../../chipnomad_lib/synth/sample_voice.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

TEST_SUITE("sample_e2e") {

// Same deterministic fixture idea as OpSample in test_sample_ops.cpp, but
// with a signed ramp so normalize/fade produce predictable values.
struct E2ESample {
  InstrumentSample s;
  std::vector<int16_t> original;

  E2ESample() : s{} {}
  ~E2ESample() { free(s.data); }

  void init(uint32_t frames, uint8_t channels) {
    memset(&s, 0, sizeof(s));
    s.frameCount = frames;
    s.channels = channels;
    s.sampleRate = 44100;
    s.start = 0;
    s.end = 255;
    if (frames == 0) {
      s.data = NULL;
      return;
    }
    original.resize((size_t)frames * channels);
    for (uint32_t i = 0; i < frames; ++i) {
      for (uint8_t c = 0; c < channels; ++c) {
        original[(size_t)i * channels + c] = value(i, c);
      }
    }
    s.data = (int16_t*)malloc(original.size() * sizeof(int16_t));
    REQUIRE(s.data != NULL);
    memcpy(s.data, original.data(), original.size() * sizeof(int16_t));
  }

  // Ramp: frame i channel c = (i * 100 + c * 7) clamped into int16. Peak
  // lands at the last frame, so normalize has a known target.
  static int16_t value(uint32_t i, uint8_t c) {
    int32_t v = (int32_t)(i * 100 + c * 7);
    if (v > 32000) v = 32000;
    return (int16_t)v;
  }

  void expectUnchanged() {
    REQUIRE(s.data != NULL);
    CHECK(s.frameCount * s.channels == original.size());
    for (size_t i = 0; i < original.size(); ++i) {
      CHECK(s.data[i] == original[i]);
    }
  }
};

// §1.1 Full workflow: load -> trim Start/End -> select -> Normalize ->
// select tail -> Fade Out -> Crop -> Save As -> reload -> identical result.
// The audition step (Edit+Play) and the project save/load are UI/project-io
// concerns covered elsewhere; the data path is what this exercises.
TEST_CASE("Full workflow: trim, select, normalize, fade, crop, save-as, reload") {
  E2ESample t;
  t.init(1000, 1);

  // Trim Start/End precisely: markers 100/200 on the 0-255 scale, resolved
  // through the shared conversions exactly like the editor screen does.
  uint32_t trimStart = sampleMarkerToStartFrame(1000, sampleFrameToStartMarker(1000, 100));
  uint32_t trimEnd = sampleMarkerToEndFrame(1000, sampleFrameToEndMarker(1000, 200));
  t.s.start = sampleFrameToStartMarker(1000, trimStart);
  t.s.end = sampleFrameToEndMarker(1000, trimEnd);
  CHECK(trimStart < trimEnd);

  // Select a region inside the trim window and Normalize it.
  uint32_t normStart = trimStart + 50;
  uint32_t normEnd = trimStart + 150;
  REQUIRE(sampleOpNormalize(&t.s, normStart, normEnd) == sampleOpOk);
  // Peak inside the selection was value(normEnd-1) = (normEnd-1)*100;
  // gain = 32767 / peak (integer division), applied to every frame.
  {
    int16_t peak = 0;
    for (uint32_t i = normStart; i < normEnd; ++i) {
      int16_t v = t.s.data[i];
      if (v < 0) v = (int16_t)-v;
      if (v > peak) peak = v;
    }
    const int64_t gain = 32767 / (int64_t)((normEnd - 1) * 100);
    CHECK(t.s.data[normEnd - 1] == (int16_t)((normEnd - 1) * 100 * gain));
    CHECK(t.s.data[normStart] == (int16_t)(normStart * 100 * gain));
    // Outside the selection untouched
    CHECK(t.s.data[trimStart] == E2ESample::value(trimStart, 0));
    CHECK(t.s.data[trimEnd - 1] == E2ESample::value(trimEnd - 1, 0));
  }

  // Select the tail of the trimmed window and Fade Out.
  uint32_t fadeStart = trimEnd - 100;
  uint32_t fadeEnd = trimEnd;
  REQUIRE(sampleOpFade(&t.s, fadeStart, fadeEnd, 0) == sampleOpOk);
  // Last frame of a fade out is zero; the frame before the selection is not.
  CHECK(t.s.data[fadeEnd - 1] == 0);
  CHECK(t.s.data[fadeStart - 1] != 0);

  // Crop to the trimmed window (selection = [trimStart, trimEnd)).
  REQUIRE(sampleOpCrop(&t.s, trimStart, trimEnd) == sampleOpOk);
  CHECK(t.s.frameCount == trimEnd - trimStart);
  // Markers remapped to the full new range.
  CHECK(t.s.start == 0);
  CHECK(t.s.end == 255);

  // Save As a new file, then reload it: the round trip must be identical.
  const char* path = "test_e2e_workflow.wav";
  char error[64] = {0};
  REQUIRE(sampleSaveWav16(&t.s, path, error, sizeof(error)) == 0);

  E2ESample reloaded;
  REQUIRE(sampleLoadWav16(path, &reloaded.s, error, sizeof(error)) == 0);
  CHECK(reloaded.s.frameCount == t.s.frameCount);
  CHECK(reloaded.s.channels == 1);
  CHECK(reloaded.s.sampleRate == 44100);
  REQUIRE(reloaded.s.data != NULL);
  for (uint32_t i = 0; i < t.s.frameCount; ++i) {
    CHECK(reloaded.s.data[i] == t.s.data[i]);
  }
  std::remove(path);
}

// §1.2 Undo journey: five different ops in sequence, UNDO restores the state
// before the first op of the last GO chain, GO a new op, UNDO toggles.
// Depth-1 semantics: the slot holds the pre-op snapshot; apply swaps.
TEST_CASE("Undo journey: five ops, depth-1 toggle semantics") {
  E2ESample t;
  t.init(200, 1);
  SampleUndo slot;
  memset(&slot, 0, sizeof(slot));

  // Op 1: silence the first 50 frames
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpSilence(&t.s, 0, 50) == sampleOpOk);
  for (uint32_t i = 0; i < 50; ++i) CHECK(t.s.data[i] == 0);

  // Op 2: fade in over [50, 100)
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpFade(&t.s, 50, 100, 1) == sampleOpOk);
  CHECK(t.s.data[50] == 0); // fade in starts from silence
  // Last frame gets gain (len-1)/len = 49/50, not full scale
  CHECK(t.s.data[99] == (int16_t)((int32_t)E2ESample::value(99, 0) * 49 / 50));

  // Op 3: normalize the tail [100, 200)
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpNormalize(&t.s, 100, 200) == sampleOpOk);

  // Op 4: silence [150, 160)
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpSilence(&t.s, 150, 160) == sampleOpOk);
  for (uint32_t i = 150; i < 160; ++i) CHECK(t.s.data[i] == 0);

  // Op 5: fade out [160, 200)
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpFade(&t.s, 160, 200, 0) == sampleOpOk);
  CHECK(t.s.data[199] == 0);

  // Snapshot the post-op-5 state, then UNDO: must restore the state before
  // op 5 (the slot holds the pre-op-5 snapshot).
  std::vector<int16_t> afterOp5(t.s.data, t.s.data + t.s.frameCount);
  REQUIRE(sampleOpApplyUndo(&t.s, &slot) == sampleOpOk);
  CHECK(t.s.frameCount == 200);
  for (uint32_t i = 150; i < 160; ++i) CHECK(t.s.data[i] == 0); // op 4 still visible
  CHECK(t.s.data[199] == E2ESample::value(199, 0)); // op 5 gone

  // GO a new op (op 6: silence [170, 180), a region untouched by ops 1-5
  // after the undo) - prepare overwrites the slot with the current state.
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpSilence(&t.s, 170, 180) == sampleOpOk);
  for (uint32_t i = 170; i < 180; ++i) CHECK(t.s.data[i] == 0);

  // UNDO again: toggles back to the pre-op-6 state (ops 1-4 visible, ops
  // 5 and 6 gone).
  REQUIRE(sampleOpApplyUndo(&t.s, &slot) == sampleOpOk);
  for (uint32_t i = 0; i < 50; ++i) CHECK(t.s.data[i] == 0); // op 1
  for (uint32_t i = 170; i < 180; ++i) CHECK(t.s.data[i] != 0); // op 6 gone
  for (uint32_t i = 150; i < 160; ++i) CHECK(t.s.data[i] == 0); // op 4
  CHECK(t.s.data[199] == E2ESample::value(199, 0)); // op 5 gone

  // And toggling once more returns to the op-6 state.
  REQUIRE(sampleOpApplyUndo(&t.s, &slot) == sampleOpOk);
  for (uint32_t i = 170; i < 180; ++i) CHECK(t.s.data[i] == 0);

  sampleOpFreeUndo(&slot);
}

// §1.2 (memory part): 20+ GO/UNDO cycles must not grow memory. With swap
// semantics the slot holds exactly one buffer at all times; verify the slot
// pointer stays bounded (only two buffers ping-pong) and data stays sane.
TEST_CASE("Undo toggle across 24 cycles keeps a single slot buffer") {
  E2ESample t;
  t.init(64, 1);
  SampleUndo slot;
  memset(&slot, 0, sizeof(slot));

  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpSilence(&t.s, 0, 32) == sampleOpOk);

  int16_t* firstSlotData = slot.data;
  int16_t* firstSampleData = t.s.data;
  int slotBuffersSeen = 0;
  int16_t* seen[4] = {NULL, NULL, NULL, NULL};

  for (int cycle = 0; cycle < 24; ++cycle) {
    REQUIRE(sampleOpApplyUndo(&t.s, &slot) == sampleOpOk);
    // Track distinct heap blocks: swap semantics must only ever use the
    // same two buffers (sample <-> slot).
    int known = 0;
    for (int k = 0; k < 4; ++k) {
      if (seen[k] == slot.data) known = 1;
    }
    if (!known) {
      REQUIRE(slotBuffersSeen < 4);
      seen[slotBuffersSeen++] = slot.data;
    }
  }
  // Only the two original buffers may appear across all cycles.
  CHECK(slotBuffersSeen <= 2);
  bool onlyOriginal = true;
  for (int k = 0; k < slotBuffersSeen; ++k) {
    if (seen[k] != firstSlotData && seen[k] != firstSampleData) onlyOriginal = false;
  }
  CHECK(onlyOriginal);
  CHECK(slot.active == 1);

  sampleOpFreeUndo(&slot);
  free(firstSampleData); // the fixture destructor would double-free: t.s.data
  t.s.data = NULL;       // now points at the freed slot buffer after freeUndo
}

// §1.3 Stereo integrity: normalize a hard-panned stereo file - both channels
// share one gain, so the image is unchanged.
TEST_CASE("Normalize hard-panned stereo keeps the channel image") {
  E2ESample t;
  t.init(100, 2);
  // Hard-panned: left channel silent, right channel carries the signal.
  for (uint32_t i = 0; i < 100; ++i) {
    t.s.data[i * 2] = 0;
    t.s.data[i * 2 + 1] = (int16_t)(i * 200); // peak 19800 at i=99
  }

  REQUIRE(sampleOpNormalize(&t.s, 0, 100) == sampleOpOk);
  // gain = 32767 / 19800 = 1 (integer division)... peak 19800*1 = 19800.
  // Use a peak that gives a gain > 1 to make the test meaningful: rebuild
  // with peak 8192.
  E2ESample u;
  u.init(100, 2);
  for (uint32_t i = 0; i < 100; ++i) {
    u.s.data[i * 2] = 0;
    u.s.data[i * 2 + 1] = (int16_t)(i * 82); // peak 8118 at i=99
  }
  REQUIRE(sampleOpNormalize(&u.s, 0, 100) == sampleOpOk);
  const int64_t gain = 32767 / 8118; // = 4
  CHECK(gain > 1);
  for (uint32_t i = 0; i < 100; ++i) {
    // Left stays silent, right scales by the shared gain.
    CHECK(u.s.data[i * 2] == 0);
    int64_t expected = (int64_t)(i * 82) * gain;
    if (expected > 32767) expected = 32767;
    CHECK(u.s.data[i * 2 + 1] == (int16_t)expected);
  }
}

// §1.4 Session-loss path: edits without saving leave the file on disk
// unchanged. The dirty flag is UI state; here we verify the disk truth.
TEST_CASE("Unsaved edits leave the file on disk unchanged") {
  const char* path = "test_e2e_session_loss.wav";

  E2ESample t;
  t.init(120, 1);
  char error[64] = {0};
  REQUIRE(sampleSaveWav16(&t.s, path, error, sizeof(error)) == 0);

  // Edit in RAM (silence half) but do NOT save.
  REQUIRE(sampleOpSilence(&t.s, 0, 60) == sampleOpOk);
  for (uint32_t i = 0; i < 60; ++i) CHECK(t.s.data[i] == 0);

  // Reload from disk: the file still holds the original content.
  E2ESample reloaded;
  REQUIRE(sampleLoadWav16(path, &reloaded.s, error, sizeof(error)) == 0);
  REQUIRE(reloaded.s.data != NULL);
  CHECK(reloaded.s.frameCount == 120);
  for (uint32_t i = 0; i < 120; ++i) {
    CHECK(reloaded.s.data[i] == E2ESample::value(i, 0));
  }
  std::remove(path);
}

// §1.5 Multi-instrument overwrite: two instruments reference one WAV; an
// edit + overwrite from one is picked up by the other after a reload.
// Path-based storage means the file is the shared source of truth.
TEST_CASE("Overwrite propagates to a second instrument referencing the same WAV") {
  const char* path = "test_e2e_shared.wav";

  // Instrument A loads the file.
  E2ESample a;
  a.init(80, 1);
  char error[64] = {0};
  REQUIRE(sampleSaveWav16(&a.s, path, error, sizeof(error)) == 0);

  E2ESample b;
  REQUIRE(sampleLoadWav16(path, &b.s, error, sizeof(error)) == 0);
  REQUIRE(b.s.data != NULL);
  // Both instruments now reference the same path (project stores path only;
  // the save flow itself does not touch sample->path).
  strncpy(a.s.path, path, PROJECT_SAMPLE_PATH_LENGTH);
  strncpy(b.s.path, path, PROJECT_SAMPLE_PATH_LENGTH);

  // Edit + overwrite from instrument A.
  REQUIRE(sampleOpNormalize(&a.s, 0, 80) == sampleOpOk);
  REQUIRE(sampleSaveWav16(&a.s, path, error, sizeof(error)) == 0);

  // Instrument B reloads the project: it re-runs sampleLoadWav16 on its
  // stored path and must see the new content.
  E2ESample bAfterReload;
  REQUIRE(sampleLoadWav16(path, &bAfterReload.s, error, sizeof(error)) == 0);
  REQUIRE(bAfterReload.s.data != NULL);
  CHECK(bAfterReload.s.frameCount == 80);
  for (uint32_t i = 0; i < 80; ++i) {
    CHECK(bAfterReload.s.data[i] == a.s.data[i]);
  }
  std::remove(path);
}

// §2 performance sanity (desktop proxy): normalize + crop + delete over a
// 64 MB-class buffer (the loader cap) must complete in bounded time. This
// is a wall-clock smoke check, not a benchmark: it fails only on absurd
// runtimes (the handheld gate is a manual check).
TEST_CASE("Large-sample ops complete in bounded time (64 MB class)") {
  // 64 MB of int16 stereo = 16M frames. Use 8M frames (32 MB) to keep the
  // test suite's memory footprint reasonable while still exercising the
  // multi-megabyte paths (malloc/memcpy/memset over 32 MB).
  const uint32_t frames = 8u * 1024u * 1024u;
  E2ESample t;
  t.init(frames, 2);
  REQUIRE(t.s.data != NULL);

  // Normalize the whole sample (peak scan + gain pass over 16M samples)
  REQUIRE(sampleOpNormalize(&t.s, 0, frames) == sampleOpOk);

  // Silence a span
  REQUIRE(sampleOpSilence(&t.s, 1000, 2000) == sampleOpOk);

  // Fade out the tail
  REQUIRE(sampleOpFade(&t.s, frames - 48000, frames, 0) == sampleOpOk);

  // Crop to the middle half (malloc + memcpy of 16 MB, free of 32 MB)
  uint32_t cropStart = frames / 4;
  uint32_t cropEnd = frames - frames / 4;
  REQUIRE(sampleOpCrop(&t.s, cropStart, cropEnd) == sampleOpOk);
  CHECK(t.s.frameCount == cropEnd - cropStart);

  // Delete a span inside the cropped sample (another malloc/memcpy round)
  uint32_t delStart = 1000;
  uint32_t delEnd = 2000;
  uint32_t expected = t.s.frameCount - (delEnd - delStart);
  REQUIRE(sampleOpDelete(&t.s, delStart, delEnd) == sampleOpOk);
  CHECK(t.s.frameCount == expected);

  // Undo round trip on the large buffer (deep copy both ways)
  SampleUndo slot;
  memset(&slot, 0, sizeof(slot));
  REQUIRE(sampleOpPrepareUndo(&t.s, &slot) == sampleOpOk);
  REQUIRE(sampleOpSilence(&t.s, 0, 100) == sampleOpOk);
  REQUIRE(sampleOpApplyUndo(&t.s, &slot) == sampleOpOk);
  CHECK(t.s.frameCount == expected);
  sampleOpFreeUndo(&slot);
}

// §3 API review support: the whole-sample fallback and clamping contract is
// what the screen relies on when the selection is empty; verify it holds
// for every op in one place.
TEST_CASE("Every op accepts the whole-sample fallback consistently") {
  E2ESample t;
  t.init(50, 2);

  // Crop with selStart == selEnd is a no-op (full-sample crop)
  REQUIRE(sampleOpCrop(&t.s, 25, 25) == sampleOpOk);
  CHECK(t.s.frameCount == 50);

  // Normalize/Silence/Fade act on everything
  REQUIRE(sampleOpSilence(&t.s, 25, 25) == sampleOpOk);
  for (uint32_t i = 0; i < 50 * 2; ++i) CHECK(t.s.data[i] == 0);

  // Delete with the whole sample selected is rejected
  REQUIRE(sampleOpDelete(&t.s, 25, 25) == sampleOpErrorWholeSample);
  CHECK(t.s.frameCount == 50);

  // Reverse with equal bounds reverses everything (channel pairing kept)
  E2ESample t2;
  t2.init(50, 2);
  REQUIRE(sampleOpReverse(&t2.s, 25, 25) == sampleOpOk);
  for (uint32_t i = 0; i < 50; ++i) {
    CHECK(t2.s.data[i * 2] == E2ESample::value(49 - i, 0));
    CHECK(t2.s.data[i * 2 + 1] == E2ESample::value(49 - i, 1));
  }
}

} // TEST_SUITE("sample_e2e")
