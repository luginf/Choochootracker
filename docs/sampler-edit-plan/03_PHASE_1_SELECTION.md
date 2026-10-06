# Phase 1 — Processing Selection Region (F2)

**Goal.** Add a selection region that is conceptually and functionally
independent from playback Start/End: it marks which part of the sample the
Phase-2 process tools will act on, is drawn over the waveform, and obeys the
same zoom/keyboard conventions established in Phase 0.

## 0. Semantics first (decision record)

| Concept | Storage | Purpose | Persistence |
|---|---|---|---|
| Start/End | `uint8_t` 0–255 in `InstrumentSample` (project format) | playback window (loop region, slicing) | saved in project + WAV-independent |
| Selection | **frames**, module state in the editor screen (NOT in `InstrumentSample`, NOT in project format) | process region | session-only; cleared on load/crop |

Rationale: the selection is an editor tool, not a sound parameter. Keeping
it out of the project format avoids touching serialization (`project_
instruments_io.cpp`) and the realtime snapshot; frame precision avoids
double quantization loss (0–255 → crop → 0–255 would compound rounding).
If the future slicing engine wants a persisted region, that will be its own
field — this plan must not pre-empt it.

Selection state:

```c
struct SampleSelection {
  uint32_t start;   // inclusive frame
  uint32_t end;     // exclusive frame; start == end ⇒ empty selection
  uint8_t  active;  // 0 = empty/none (process ops fall back to whole sample where sensible)
};
```

Normalization rule: `start < end` always (swap on the fly when edited
inverted, mirroring the existing start/end swap behavior).

## 1. UX specification

Layout addition (rows 12–13 slot in before Slice; exact rows flexible):

```
row 12  Sel.S   001024            (frame readout; "-" when empty)
row 13  Sel.E   002048            (frame readout; "-" when empty)
```

Interaction contract:

- **Set selection from loop region**: on the Sel.S/Sel.E rows,
  `CellEditAction::increase/decrease` (Edit+Left/Right) nudge the frame by
  ±1 (fine); `increaseBig/decreaseBig` (Edit+Up/Down) nudge by a coarse
  step that scales with sample length (e.g. `frameCount/64`, min 16) —
  coarse zooms the view out to 1:1 exactly as Start/End do.
- **Zoom anchoring**: while adjusting a selection handle, the Phase-0 zoom
  anchors on that handle (anchor values 3/4 in `SampleEditorView`).
- **Quick-set**: `CellEditAction::tap` (Edit) on Sel.S sets it to the
  current Start marker frame; on Sel.E sets it to the current End marker
  frame (fast "process the loop region" workflow, M8-like ergonomics
  without stealing keys).
- **Clear**: `CellEditAction::clear` (Edit+Opt) on either row empties the
  selection (both handles). `clear` on a row that is empty is a no-op.
- **Playback Start/End fields remain untouched** by selection edits.
- Readout shows frames (`001024`); when the view is 1:1 and the sample is
  short, frame numbers equal position — fine for v1. (Milliseconds are
  derivable; add `ms` in the readout row only if space allows, e.g.
  `001024 23ms` — optional polish.)

## 2. Rendering

- New bitmap layer `sampleSelectionBitmap` (same lifecycle as the marker
  bitmaps; rebuilt in `updateSamplePreview`):
  - Region fill: for each pixel column whose frame range overlaps
    `[sel.start, sel.end)`, draw a dim vertical line (shade 96) from top to
    bottom of the bitmap — reads as "highlighted band" behind the waveform.
  - Handles: bright vertical lines (shade 255) at the two mapped handle
    positions.
- Draw order becomes: waveform → selection band+handles → start/end
  markers → slice markers. Waveform inside the selection may additionally
  brighten (optional; the band alone is sufficient).
- Colors: draw the selection with a scheme color distinct from the yellow/
  orange markers (e.g. scheme `textInfo`); do not hard-code new RGB values
  if a scheme color fits (app-native rule).
- All positions map through the Phase-0 view window; handles outside the
  window are skipped.

## 3. State consistency helper (introduced now, reused by Phase 2)

`sampleEditorNormalizeState(sample, &selection, &view)` — single function
called after every mutation from Phase 2 onward:

- clamps selection to `[0, frameCount]`, swaps inverted, sets
  `active = start < end`;
- clamps view to `[0, frameCount]` and re-anchors if the anchor marker
  vanished (e.g. handle now empty → anchor=0, zoom out to full);
- repaints.

Phase 1 wires it after selection edits; Phase 2 reuses it after crop/
delete/undo.

## 4. Files touched (expected, agent verifies)

- `tracker/src/screens/screen_sample_settings.cpp` — selection state, two
  rows (+ column count / cursor / isCellValid updates), overlay layer,
  normalize helper, tap/clear handling.
- No engine changes. No project-format changes.

## 5. Edge cases

- Selection set while no sample loaded → rows display `-`, edits no-op.
- Handles equal (empty) → band not drawn; readout `-`.
- Selection outside the zoom window → still consistent; zoom-out shows it.
- Start/End moved after selection was made → selection unaffected (independence is the feature; document in manual).
- Slice markers + selection band + markers all visible simultaneously at ×8 zoom — verify no palette confusion (band dimmer than everything).
- Key repeat on a selection handle at frame 0 or `frameCount` — clamped, no wrap.
- Screen left/re-entered → selection resets to empty (documented session-only semantics).

## 6. Manual QA checklist (Phase 1)

1. Load stereo + mono samples; set Sel.S/Sel.E with fine and coarse steps;
   band and handles appear; readouts update.
2. Edit+Up coarse on a handle zooms view out to 1:1 (mirrors Start/End
   behavior); Edit+Left/Right zoom in anchored on the handle.
3. Tap on Sel.S/Sel.E copies Start/End frame values — verify against the
   yellow/orange marker positions.
4. Edit+Opt clears selection; band disappears; readouts show `-`.
5. Inverted edits (Sel.E < Sel.S) auto-normalize to the swap.
6. All five overlay layers coexist visually: waveform, selection band,
   handles, start/end markers, slice lines.
7. Zoom to ×16 on a 2-minute sample; adjust a handle off-window; window
   pans correctly.
8. Leave screen, return: selection empty again; no stale bitmap.
9. Tests green (`make -f Makefile.test -j4`).

## 7. Commit

`sampler edit: independent processing-selection region with zoom-aware handles`

## 8. Execution record (Phase 4 hardening pass)

Executed 2026-10-03 on Linux x86_64 desktop. Commit `dad36de` (combined with
Phase 0, see Phase 0 deviation D1).

### Deviations

- **D1 — Coarse step is `frameCount/64` min 16, as planned; no `ms` readout.**
  The optional milliseconds polish in the readout was not implemented (row
  width budget); frame readout is `%06u`.
- **D2 — Selection readout shows `-` on both handles when empty** (plan
  allowed per-handle display; both-handles is simpler and matches the
  shared-selection model).
- **D3 — Selection state struct named `SampleEditorSelection` with an
  `active` flag**, matching the plan's storage decision (frames, module
  state, not persisted).

### Checklist status

Items 1–8: executed on desktop during Phase 1 (band/handles/readouts,
coarse zoom-out, tap-to-copy, clear, inversion swap, overlay coexistence,
pan at deep zoom, re-entry reset). Item 9: tests green — final Phase 4
state is 392 cases / 7,035,341 assertions, 0 failures, 0 warnings.
