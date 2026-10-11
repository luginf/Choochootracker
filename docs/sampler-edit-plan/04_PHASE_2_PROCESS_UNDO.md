# Phase 2 — PROCESS Box: DSP Operations + Undo (F3)

**Goal.** Implement the destructive sample-processing toolbox: Crop,
Normalize, Delete, Silence, Fade In, Fade Out — applied to the Phase-1
selection (or the whole sample when no selection), with a one-level undo.
DSP lives in a new UI-free engine module with doctest coverage; the screen
gains the PROCESS UI.

## 0. Engine module: `chipnomad_lib/synth/sample_ops.{h,cpp}`

Pure functions over `InstrumentSample`; no UI, no audio-manager calls —
**suspension of the audio callback stays the caller's (screen's) duty** so
the module is unit-testable and reusable (SCWF oscillators, future slicing
engine).

```c
// Result codes: 0 ok; nonzero = typed error (screen maps to message string)
int sampleOpCrop(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);
int sampleOpNormalize(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);
int sampleOpDelete(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);
int sampleOpSilence(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);
int sampleOpFade(InstrumentSample* s, uint32_t selStart, uint32_t selEnd, int fadeIn);
```

Argument convention: `selStart == selEnd` means "whole sample" (ops resolve
it internally to `[0, frameCount)`); all functions validate `frameCount`,
`data != NULL`, clamped ranges. Only `Delete`/`Crop` change length; the
others edit in place.

### 0.1 Operation specs (the contract the tests assert)

Common rules:

- Stereo: ops are frame-based; both channels of a frame are processed
  together (fade/normalize use the same gain for both channels to preserve
  the stereo image; normalize's peak is scanned across **both** channels).
- No denormals/float path: everything in int32/int64 intermediate math,
  clamped to int16.
- `sampleRate`, `channels`, `path`, playback params except those explicitly
  remapped below are untouched.

| Op | Buffer effect | Position remap |
|---|---|---|
| **Crop** | allocate `len = selEnd-selStart` frames; copy; free old | `start`/`end`: convert 0–255 → old frames → subtract `selStart` → clamp to new range → convert back. `slice` kept (region shrinks proportionally — slices follow loop region by design). Selection cleared. |
| **Normalize** | `peak = max |value|` over selection, both channels; if `peak > 0`: `gain = 32767/peak` (int64), scale+clamp selection in place | none; selection kept |
| **Delete** | allocate `frameCount - len`; copy `[0,selStart)` + `[selEnd,frameCount)`; free old | `start`/`end`: convert to old frames; frames ≥ `selEnd` shift by `-len`; frames inside `[selStart,selEnd)` clamp to `selStart`; convert back. Selection cleared. Reject if the selection is the whole sample (min 1 frame must remain). |
| **Silence** | zero every sample in selection (int16 0) | none; selection kept |
| **Fade In** | linear ramp `0 → 1` across selection multiplied per frame (gain = `i/len` in int32: `(v * i) / len`) | none; selection kept |
| **Fade Out** | mirror of Fade In | none; selection kept |

Notes for the agent:

- The 0–255 ↔ frame conversion helpers must be the **single shared** ones
  (Phase 0 extraction) — used by both the screen and `sample_ops`; put them
  in `sample_voice.h` next to `sampleSliceFrames` (already the home of such
  helpers) and reuse from both sides.
- After any op, the caller runs `sampleEditorNormalizeState()` (Phase 1).
- Keep the 64 MB loader cap in mind: ops never grow the buffer, only
  shrink or keep it — no new cap needed.

### 0.2 Undo (one level, per user request)

```c
typedef struct SampleUndo {
  int active;                 // 0 = empty
  int16_t* data;              // deep copy of the pre-op buffer
  InstrumentSample header;    // full struct copy EXCEPT `data` (frameCount, start, end, ...)
} SampleUndo;
```

- `sampleOpPrepareUndo(InstrumentSample* s, SampleUndo* slot)` — called
  **before** the first op of a user action; if a slot already holds data it
  is **overwritten** (depth-1 semantics; alternating GO/UNDO toggles the
  two states rather than growing history — document this).
- `sampleOpApplyUndo(InstrumentSample* s, SampleUndo* slot)` — under
  pause/resume: `free(s->data); s->data = slot->data; slot->data = old
  s->data;` swap headers too; slot stays active (toggle behavior).
- Slot lives in the editor screen module state (not the engine, not the
  project); freed on screen `setup()` and on new sample load.
- Undo of a Crop restores the pre-crop buffer — full-buffer copy is the
  price of simplicity; bounded by the 64 MB loader cap. Accepted
  (PortMaster targets ≥1 GB RAM; document for web build).

## 1. Screen UI (app-native PROCESS box)

Layout sketch (exact rows flexible, keep the grid conventions):

```
row 16  Process  Crop        (value: cycles with Edit+Left/Right through
                              CROP NORM DEL SIL F.IN F.OUT; Edit+Opt = none)
row 17  GO                   (tap → apply current op; message on error)
row 18  UNDO                 (tap → toggle undo; dim/empty when slot inactive)
```

Behavior contract:

- GO with no selection: applies to the whole sample (Normalize / Silence /
  Fades); **Crop/Delete require a selection** — tapping GO without one
  shows a `screenMessage` hint ("Select region first"), not a silent no-op.
- GO runs: `audioManager.pause()` → `sampleOpPrepareUndo()` → op →
  `audioManager.resume()` → `projectModified = 1` →
  `sampleEditorNormalizeState()` → preview repaint → success/error message.
  (`audioProjectDirty` propagation is automatic on the next key-down —
  see codebase analysis §3.)
- After Crop/Delete the selection is empty, view resets to 1:1 on the new
  full sample, and the zoom indicator updates.
- UNDO dimmed (scheme `textEmpty`) while the slot is inactive; after the
  first GO it lights up.
- Errors from ops surface via `screenMessage(MESSAGE_TIME_ERROR, ...)` —
  map typed results to short strings ("Select region first", "Delete whole
  sample not allowed").
- No extra confirm dialogs: GO is deliberate (2-key sequence) and undo
  exists; matches M8 immediacy and the app's minimal-dialog style.

## 2. Options considered

| Option | Verdict |
|---|---|
| A: Hand-rolled `sample_ops` (chosen) | ~250 lines total incl. tests; exact contract control; no build/deps impact on the 7 platform targets. |
| B: Pull a DSP lib (e.g. Rubberband, libsoxr, soundtouch — MIT-ish) | Rejected: enormous dependency for trim/gain/ramp primitives; build-time burden on PortMaster/Emscripten; license review overhead; overkill. |
| C: Float intermediate buffer per op | Rejected for v1: int32 intermediates are sufficient and avoid a second allocation; a float path matters only when chaining many ops (future, with the slicing engine). |

## 3. Files touched (expected, agent verifies)

- **New** `chipnomad_lib/synth/sample_ops.h/.cpp` — ops + undo + conversion
  helpers' new home (or helpers extend `sample_voice.h`).
- **New** `tracker/tests/test_sample_ops.cpp` — doctest suite.
- `tracker/src/screens/screen_sample_settings.cpp` — PROCESS rows, GO/UNDO
  handlers, error messages, pause/resume discipline.
- `tracker/Makefile.test` — nothing (wildcard discovery); verify
  `Makefile.common`/`Makefile.test` source globs pick the new lib file
  (they wildcard `chipnomad_lib/synth/*.cpp` — confirm, don't assume).
- `docs/USER_MANUAL.md` — PROCESS section.

## 4. Unit tests (`test_sample_ops.cpp`) — minimum set

1. Crop mono 1000-frame sample `[100..200)` → 100 frames; data == old
   `[100..200)`; start/end remapped to full range when they pointed at the
   selection.
2. Crop stereo — channel interleave preserved.
3. Delete middle region — lengths, join point continuity, start/end remap
   across the removed span.
4. Delete whole-sample selection → error code, buffer untouched.
5. Silence — zeros exactly the selection; nothing outside.
6. Normalize — known peak (e.g. 16384) scaled to ~32767 (±1 rounding);
   silent selection → no-op, no div-by-zero; stereo peak found in right
   channel scales left equally.
7. Fade in/out — boundary values (first/last frame), ramp monotonicity,
   out-of-selection untouched.
8. Undo prepare/apply toggle round-trip for a length-changing op
   (crop) and an in-place op (silence).
9. Whole-sample fallback when `selStart == selEnd`.
10. 0–255 → frame → remap → 0–255 round-trip helper: boundary values
    (0, 255) and inverted input.

## 5. Edge cases (manual)

- Process while the song plays the same instrument on a track: pause/
  resume covers it — verify no glitch/crash, and the next tick picks up
  the new buffer (audition with Edit+Play on the INSTRUMENT screen).
- Normalize a 64 MB sample on a handheld: brief UI stall is expected —
  verify it stays seconds, not minutes; if needed show a busy message
  before the op.
- Fade on a 2-frame selection; silence on 1-frame selection.
- Crop then UNDO then GO again with a different op — slot replacement
  semantics.
- Undo after loading a *different* sample (slot must be reset — no
  cross-sample restore).
- Repeat GO (Normalize twice): second pass is idempotent (gain ≈ 1.0,
  no drift/clipping).

## 6. Manual QA checklist (Phase 2)

1. Crop middle 50% of a stereo WAV; play the instrument — audio matches
   the cropped region; Start/End sensible; slice count still correct.
2. Delete a middle chunk — the join sounds clean (no click beyond
   waveform discontinuity); project still loads after save/reload (path
   unchanged; buffer rebuilt from WAV on next session — *expected:
   unsaved edit lost* until Phase 3 lands; test explicitly and document).
3. Silence / Fades on selection; Normalize whole sample (no selection).
4. GO without selection → hint message; GO for Crop/Delete without
   selection → hint, not whole-sample crop.
5. UNDO restores byte-identical audio after each op type.
6. Undo inactive at screen entry; after first GO it works; toggling twice
   returns to post-op state.
7. song playing during every op — no crash.
8. Tests green.

## 7. Commit

`sampler edit: sample process ops (crop/normalize/delete/silence/fade) with one-level undo`

## 8. Execution record (Phase 4 hardening pass)

Executed 2026-10-03 on Linux x86_64 desktop. Commit `9e52f92`.

### Deviations

- **D1 — Whole-sample fallback applies to all ops at the engine level;
  Crop/Delete are gated in the screen.** The plan said "GO without
  selection → whole sample for Normalize/Silence/Fades; Crop/Delete
  require a selection". Implemented exactly so, but the split lives in
  `settingsRunProcessOp()` (screen passes `0,0` for non-selection ops and
  refuses Crop/Delete with "Select region first"); `sample_ops` itself
  treats `selStart == selEnd` as whole-sample for every op. This keeps the
  engine contract uniform (verified by
  `test_sample_e2e.cpp` "Every op accepts the whole-sample fallback
  consistently") and the UX rule where it belongs.
- **D2 — Undo slot lives in the screen as `editorUndo`** (module state),
  freed in `setup()` and on new sample load — as planned. The undo slot is
  NOT freed on dialog round trips; the dirty flag travels through
  `pendingDirtyRestore` instead (Phase 3 mechanism).
- **D3 — Error message for `sampleOpErrorNoUndo`** is covered by the
  generic "Operation failed" fallback (the screen never calls ApplyUndo on
  an inactive slot — the UNDO cell is dimmed — so the code is unreachable
  in practice).
- **D4 — `test_bounce` buffer fix landed as a separate commit** (`06f6449`,
  "tests: fix undersized stereo render buffer in bounce stop-range test")
  because the undersized render buffer was found while running the suite
  during this phase, but the fix is unrelated to sample ops.

### Checklist status

Unit tests (§4 items 1–10): all covered by `test_sample_ops.cpp` (20
cases) plus the Phase 4 E2E suite. Manual items 1–8: executed on desktop
during Phase 2 (crop/delete audio verification, whole-sample normalize,
GO hints, undo round trips incl. toggle, playback running during ops).
Item 8: tests green — final Phase 4 state is 392 cases / 7,035,341
assertions, 0 failures, 0 warnings. Handheld 64 MB stall check (§5) not
executed on this machine — see Phase 4 execution record.
