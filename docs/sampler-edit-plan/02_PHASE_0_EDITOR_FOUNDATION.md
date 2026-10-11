# Phase 0 — Editor Foundation & Responsive Waveform Zoom (F1)

**Goal.** Restructure the SAMPLE SETTINGS screen into a sample-editing
surface and add the zoomable waveform view engine, wired to the existing
Start/End fields: fine adjustment zooms in around the marker being edited,
coarse adjustment snaps back to the full-sample view. This phase delivers
Feature 1 and the rendering foundation that Phases 1–2 build on.

## 0. UX specification (app-native)

Screen remains `screenSampleSettings` (same entry point from INSTRUMENT →
SOURCE → EDIT). Proposed layout (40×20, adjust freely within the grid):

```
row 0   SAMPLE EDIT                        (title)
row 1   <filename.wav>                     (info; "*"-prefix reserved for P3 modified flag)
row 2   44100 Hz  STEREO  123456 fr        (format readout, textInfo color)
rows 4-10   waveform view (32×7)           (grew by 1 row from 6; optional)
row 11  VIEW 1:1  /  ZOOM x8  [s..e]       (zoom indicator, non-cursor text)
row 13  Start   00..FF                     (existing)
row 14  End     00..FF                     (existing)
row 15  Slice   Off 2 4 8 16 32            (existing; keep stretch interlock)
```

Interaction contract:

- Cursor on **Start** or **End** (value column), `CellEditAction::increase/
  decrease` (Edit+Left/Right, fine): value changes by 1 step **and** the
  waveform zooms to a window centered on (or anchored at) that marker.
  Repeated fine steps (key repeat) keep following the marker.
- `CellEditAction::increaseBig/decreaseBig` (Edit+Up/Down, coarse): value
  changes by the existing big step (16) and **the view resets to the full
  sample** (1:1). This matches the requested "up/down = zoom out to original
  view".
- Zoom level indicator updates live. When the view is 1:1 the indicator
  shows `VIEW 1:1` (or the sample length readout); when zoomed it shows the
  visible span in frames (e.g. `ZOOM 1024 fr`), which doubles as feedback
  for "exactly what is being selected".
- Slice markers, start/end markers and the greyed inactive region all
  render correctly at any zoom (positions mapped through the view window).
- Navigation to Slice / other rows does not zoom (zoom only follows the two
  playback markers in this phase; selection handles join in Phase 1).

## 1. Architecture

### 1.1 View state (module-static, like today's bitmaps)

```c
struct SampleEditorView {
  uint32_t viewStart;   // first visible frame
  uint32_t viewEnd;     // one past last visible frame (<= frameCount)
  int      anchor;      // which marker drives the window: 0=none,1=start,2=end,3=selStart,4=selEnd
};
```

Lives in `screen_sample_settings.cpp` as `static SampleEditorView editorView;`
reset in `setup()` (fresh sample → full view).

### 1.2 Zoom algorithm (step outlines for the agent)

- `zoomToMarker(sample, view, markerFrame, factor)`:
  1. Compute target span `span = clamp((viewEnd-viewStart) / factor,
     MIN_SPAN, frameCount)`; `MIN_SPAN` ≈ 8 frames (and never < the marker
     quantum — see edge cases).
  2. Anchor: keep `markerFrame` at the same **relative position** inside the
     window as before the zoom (equivalent to M8 behavior and avoids drift
     when repeatedly nudging). First zoom-in centers the marker:
     `viewStart = markerFrame - span/2`.
  3. Clamp to `[0, frameCount]`; if `markerFrame` would leave the window
     (marker moved past an edge via fine steps), **pan**: shift the window
     just enough to keep the marker at the hit edge (keep ~1/8 margin).
- `zoomOutFull(view)` sets `viewStart=0; viewEnd=frameCount; anchor=0`.
- Fine/coarse mapping inside `settingsOnEdit()` rows Start/End: after a
  successful `edit8noLast`, call `zoomToMarker(..., anchor = marker)` for
  increase/decrease, `zoomOutFull()` for increaseBig/decreaseBig.
- Marker frame comes from the existing 0–255 → frame conversion helper
  (extract the duplicated conversion in `updateSamplePreview()` /
  `SampleVoice::configure()` into one shared inline in the screen file or
  `sample_voice.h` — one place only, do not fork the formula).

### 1.3 Rendering changes

- `updateSamplePreview(sample, view)` gains the view parameter (or an
  overload): waveform rendered from `renderPCM16Preview(waveform, data,
  view.viewStart, view.viewEnd, channels)` — **no renderer change needed**;
  inactive-region greying and marker/slice pixel positions switch from
  `frame * width / frameCount` to `frameMapped = (frame - viewStart) *
  width / (viewEnd - viewStart)`, skipping out-of-window markers.
- Add a compact `drawViewReadout()` line (row 11) with the zoom indicator.
- Repaint cost: the preview bitmap is ≤ 32 chars wide (≤ 256 px); a full
  rebuild per key repeat is fine on all targets (existing code already
  rebuilds per edit). No caching needed in v1.

## 2. Options considered

| Option | Verdict |
|---|---|
| A: Parameterize the existing `renderPCM16Preview` window (chosen) | The renderer already accepts start/end frames; zoom is view math only. Zero risk to other screens (SCWF/BYOWTBL previews keep calling it with full range). |
| B: Min/max precomputation cache (levels pyramid) | Rejected for v1: at 32 chars × 256 px the direct scan is microseconds; a cache adds invalidation complexity for no visible gain. Note as future optimization for the slicing engine's waveform work. |
| C: Third-party waveform lib (Peaks.js etc.) | Rejected outright — web-only libs, wrong rendering paradigm (canvas/DOM), not usable from the 8-bit `Bitmap` pipeline. No C/C++ candidate fits; MIT license moot. |

## 3. Files touched (expected, agent verifies)

- `tracker/src/screens/screen_sample_settings.cpp` — view state, zoom
  functions, `settingsOnEdit` wiring, layout rows, readout.
- `tracker/src/screens/screen_sample_settings.cpp` only — no engine changes
  in this phase (view math is UI-side).
- `docs/USER_MANUAL.md` — document the zoom interaction.

## 4. Edge cases (test each)

- Empty instrument (no file): fields inert; zoom functions must no-op on
  `frameCount == 0` / `data == NULL` (current code already guards
  `updateSamplePreview` via `renderPCM16Preview` checks — keep it).
- Very short samples (`frameCount < MIN_SPAN`): view = full sample always;
  zoom indicator shows 1:1.
- `start == end` markers and inverted order (existing swap logic) — zoom
  anchor must follow the marker actually being edited, not the "active
  region".
- Zoom-in at sample start/end extremes: window clamps to `[0, frameCount]`
  without jitter (anchor-relative math must clamp after computing offsets).
- Key repeat burst: hold Edit+Right for 2 s — no missed repaints, no
  unbounded zoom (span floors at MIN_SPAN), cursor stays on the field.
- Slice markers visible when zoomed (loop region may be off-screen — skip
  their pixels cleanly).
- Slice/stretch interlock unaffected; Slice edit does not zoom.

## 5. Manual QA checklist (Phase 0)

1. Load a 10+ second stereo WAV; cursor on Start; hold Edit+Right — value
   counts up, waveform progressively zooms around the start marker, zoom
   readout shows shrinking span.
2. Tap Edit+Up several times — value jumps by 16, view snaps back to 1:1.
3. Same for End marker, both directions.
4. Marker walks off-window with fine steps → window pans, marker stays
   visible at edge margin.
5. Slice = 8: slice lines land at correct loop-region positions at 1:1 and
   at ×8 zoom; inactive greying correct in both views.
6. Start > End inversion still behaves as before (swap path).
7. With playback running (song playing on another track), edit Start/End —
   no audio glitch/crash (this phase doesn't touch buffers, but establish
   the habit).
8. Leave to INSTRUMENT and return — view resets to 1:1 for the fresh
   `setup()`.
9. No file loaded → screen renders, fields inert, no crash.
10. `make -f Makefile.test -j4` green.

## 6. Commit

One commit, suggested message:
`sampler edit: zoomable waveform view driven by fine/coarse start-end adjustment`

## 7. Execution record (Phase 4 hardening pass)

Executed 2026-10-03 on Linux x86_64 desktop. Commit `dad36de` (combined with
Phase 1, see deviation D1).

### Deviations

- **D1 — Phases 0 and 1 shipped as one commit.** The zoom engine and the
  selection region were implemented in the same editing session and landed
  together in `dad36de` ("sampler edit: zoomable waveform view and
  processing selection") instead of two commits. Scope of each phase was
  still delivered separately and is reviewable in the single diff.
- **D2 — Marker→frame conversion helpers live in `project_instruments.h`,
  not `sample_voice.h`.** The plan suggested `sample_voice.h`; the helpers
  (`sampleMarkerToStartFrame` / `sampleMarkerToEndFrame`) were placed in
  `project_instruments.h` next to the `InstrumentSample` definition because
  both the screen and `sample_voice.cpp`/`stretch_processor.cpp` already
  include it. Single-place rule kept (commit `9ba2cff` extracted them from
  duplicated code).
- **D3 — Zoom span floor is marker-quantum aware.** `MIN_SPAN` is not a
  fixed 8 frames: `zoomToMarker` floors the span at
  `max(kMinViewSpan=8, frameCount/255 + 1)` so the window always shows the
  frames a fine step can actually cross. Slightly different from the plan's
  "≈8 frames" sketch, strictly better behavior.
- **D4 — Preview block moved up one row.** The field block starts at row 11
  (not 13) so the File row fits above the message line; the preview is
  7 rows tall at row 3. Layout sketch rows were adjusted accordingly.

### Checklist status

Items 1–8: executed on desktop during Phase 0 (behavior verified; zoom
anchor, pan, clamp and reset all match the contract). Item 9 (no file →
inert screen) verified. Item 10: tests green — final Phase 4 state is
392 cases / 7,035,341 assertions, 0 failures, 0 warnings. Items requiring
a handheld device (none in this phase's list) were not executed here.
