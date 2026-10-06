# 00 — Master Plan: Sampler Edit Window

Project: ChooChooTracker, branch `sampler`. Target screen: the **SAMPLE
SETTINGS** screen (`tracker/src/screens/screen_sample_settings.cpp`), reached
from INSTRUMENT → SOURCE → EDIT. Goal: turn it into a complete in-tracker
sample editor — waveform inspection with responsive zoom, a processing
selection, a PROCESS toolbox with undo, and sample-file persistence
(Save / Save As / Rename) — without breaking the app's lightweight,
multi-platform (PortMaster / Web / Windows / Android) nature.

## 1. Goals and non-goals

**Goals (features, in priority order):**

1. **F1 — Responsive waveform feedback.** Fine adjustment (Edit+Left/Right)
   of Start/End (and later selection handles) zooms the waveform around the
   active marker so the user sees exactly what is being edited. Coarse
   adjustment (Edit+Up/Down) returns to the full-sample view.
2. **F2 — Processing selection.** A selection region that is independent of
   playback Start/End, drawn on the waveform, edited with the same key
   conventions, and consumed by the process tools.
3. **F3 — PROCESS box.** Operation picker (Crop, Normalize, Delete, Silence,
   Fade In, Fade Out) + GO button + one-level undo. Delete splices and joins
   the remaining parts; the rest behave as standard destructive edits.
4. **F4 — Sample persistence.** Save (overwrite the WAV the project
   references), Save As (new file via the existing folder browser with
   built-in overwrite confirm), Rename (file on disk + internal path).

**Non-goals (this plan):**

- Advanced slicing engine (roadmap item) — deferred. No data-model changes
  that preclude it, but no design work either.
- Non-destructive / realtime edits, sample rate conversion, resampling,
  reversing, time-stretch of the buffer (playback stretch already exists).
- Touch/mouse waveform editing (keyboard/gamepad only, like the rest of the
  app), stereo channel-specific editing (stereo is edited as frames).
- Any new third-party dependency.

## 2. Hard constraints (must-read for any agent)

These come from `AGENTS.md`, `docs/ARCHITECTURE.md`, and the code itself.
They are non-negotiable acceptance criteria for every phase:

1. **Realtime boundary.** The UI owns `chipnomadState->project`; the audio
   callback reads `audioProject`, a **shallow struct copy** of it
   (`AudioCommandQueue::publishProject` — see `chipnomad_lib.cpp`). The
   `InstrumentSample.data` pointer is therefore **shared** between UI and
   audio. Any change that frees or reallocates sample data must happen with
   the audio callback suspended:
   `audioManager.pause()` → mutate → `audioManager.resume()`. This is the
   established pattern (`onSampleLoaded` in `instrument_sample.cpp`,
   `previewSample` in `audio_manager.cpp`).
2. **Snapshot propagation.** After a screen edit, `appInput()` sets
   `audioProjectDirty = 1` (app.cpp:249) and the next UI tick publishes a
   fresh snapshot (`chipnomadQueueProjectRefresh`). No extra wiring is
   needed for parameter edits; buffer-replacing operations only need the
   pause/resume discipline above.
3. **40×20 character grid.** Everything fits the existing screen framework:
   `ScreenData` spreadsheet cursor, `drawStatic`/`drawField`/`drawCursor`,
   `CellEditAction` semantics. No popups, no mouse, no new widget toolkit.
4. **C++17, no exceptions in the audio path, no RTTI-dependent design.**
   Follow existing style: `static` module state in screen files, C-style
   strings where the codebase uses them, `snprintf` formatting.
5. **Tests.** `cd tracker && make -f Makefile.test -j4` must pass after every
   phase. DSP code goes into `chipnomad_lib` with doctest coverage; the
   Makefile auto-discovers `tests/test_*.cpp`.
6. **Docs.** Before any commit, update `docs/USER_MANUAL.md` (never in-app
   help). Commits are scoped: stage only files belonging to the task.
7. **Sample persistence reality.** Projects store the sample **by path
   only** and re-run `sampleLoadWav16()` on project load. In-RAM edits are
   volatile — F4 is what makes F2/F3 durable. The plan treats persistence as
   a first-class phase, not an afterthought.

## 3. Feature → phase map

| Phase | Features | New capability | Depends on |
|---|---|---|---|
| 0 | F1 | Editor screen foundation + zoomable waveform view engine | — |
| 1 | F2 | Selection region (state, UI, zoom interplay) | P0 |
| 2 | F3 | `sample_ops` DSP core + undo + PROCESS UI | P1 |
| 3 | F4 | WAV writer + Save/Save As/Rename flows | P2 (order matters: save after edits is meaningful) |
| 4 | — | Hardening, manual QA, docs, builds | P0–P3 |

## 4. Chronological rationale

The order is dependency-driven and optimized for agentic context
preservation:

- **P0 first** because everything visual (selection overlay, process
  readouts) renders through the zoomable view engine. Building it first
  means later phases never touch rendering math again.
- **P1 before P2** because process operations consume the selection; the
  PROCESS UI displays the region it will act on.
- **P2 before P3** because "save" is only meaningful once destructive edits
  exist; also the undo slot must be considered when saving (save does not
  clear undo).
- **P3 last among features** because it is pure additive I/O: no rendering
  or DSP rework. It also needs the most platform-quirk testing (Android SAF,
  web FS), so isolating it keeps earlier phases shippable.
- Each phase ends with: tests green, manual QA checklist executed on one
  desktop target, USER_MANUAL.md updated, one scoped commit. This gives
  clean rollback and lets a fresh agent session resume from docs + git log.

## 5. Architecture decisions at a glance

| Decision | Choice | Rationale |
|---|---|---|
| Zoom engine | View window `{viewStartFrame, viewEndFrame}` passed to existing `renderPCM16Preview` | The renderer already accepts an arbitrary frame range and does min/max per pixel column — zoom is a parameter, not a rewrite |
| Marker layers | Separate overlay `Bitmap`s per layer (existing pattern) | Current screen already composites waveform + start/end + slice markers as independently colored bitmaps; selection and zoom readouts follow |
| Selection storage | Absolute frame indices `uint32_t`, clamped, in module state | Editor-level concept needs frame precision; playback Start/End stay 0–255 normalized (project format unchanged) |
| DSP core | New `chipnomad_lib/synth/sample_ops.{h,cpp}`, pure functions on `InstrumentSample` | Unit-testable without UI, reusable later by SCWF/BYOWTBL oscillators and the slicing engine |
| Undo | Single slot: full-buffer deep copy + header copy; UNDO swaps | Simple, robust, bounded (loader caps WAV at 64 MB); user asked for one-level undo |
| WAV writer | Hand-rolled `sampleSaveWav16()` (RIFF/PCM16, ~100 lines) | No lib needed; matches the existing hand-rolled reader (`sampleLoadWav16`); zero deps keeps web/PortMaster builds unchanged |
| Process safety | `pause → build new buffer → free old → resume` + `projectModified = 1` | Matches `onSampleLoaded`; guarantees no dangling pointer in `audioProject` |
| Process UI | App-native rows: `Process: <op>` value + `GO` cell + `UNDO` cell | Follows the spreadsheet conventions (cycle values with Edit+arrows, tap to act) — no dialog boxes exist in this app |

## 6. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| Audio thread reads freed buffer after process op | Critical | Mandatory pause/resume pattern; add to every phase's acceptance criteria; manual QA on desktop with playback running |
| Overwrite destroys a WAV referenced by several instruments | High | Document in UX (confirm dialog lists filename); no reference counting in v1 — noted as future work |
| Start/End are 0–255 normalized; crop/delete remap loses precision | Medium | Remap by converting 0–255 → frames → applying offset → frames → 0–255; worst case ±1/255 of the pre-edit length; acceptable and documented |
| Normalize/fade on 64 MB sample freezes UI for 100 ms–2 s (handheld) | Medium | Acceptable for v1; show busy message before heavy op; do not move work to audio thread (violates architecture) |
| Selection/zoom state desync after crop/delete | Medium | Central `sampleEditorNormalizeState()` helper called after every mutation; covered by unit tests |
| Undo copy doubles memory (64 MB cap → 128 MB worst case) | Low | Accepted: PortMaster targets have ≥1 GB RAM; web build has heap limits — document and keep 64 MB loader cap |
| Web build (`web/dist/` checked in) forgotten after changes | Low | AGENTS.md already covers: regenerate via `Makefile.web web-deploy` when WASM sources change |
| Agent drifts into slicing-engine work | Low | Non-goals stated here and repeated in every phase prompt |

## 7. Definition of done (whole plan)

All phases merged on `sampler`; every manual checklist executed on Windows
(desktop) and one PortMaster device; doctest suite extended with
`test_sample_ops.cpp` (process core) and updated `test_sample_voice.cpp`
positions; `docs/USER_MANUAL.md` documents the new SAMPLE EDIT screen; web
bundle regenerated; no new dependencies in any Makefile.
