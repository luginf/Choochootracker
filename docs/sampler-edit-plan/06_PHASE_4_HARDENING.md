# Phase 4 — Hardening, Docs & Platform Validation

**Goal.** Cross-cutting QA and shipping work after Phases 0–3: full manual
pass on all targets, documentation, performance sanity, and the release
checklist. No new features.

## 1. End-to-end scenarios (must pass on desktop + one handheld)

1. **Full workflow:** load WAV → zoom/trim Start/End precisely → select
   region → Normalize → select tail → Fade Out → Crop → Save As →
   audition via Edit+Play on INSTRUMENT → save project → cold reload →
   identical result.
2. **Undo journey:** perform 5 different ops in sequence; UNDO once
   (restores state before the *first* op of the last GO chain); GO a new
   op; UNDO again (toggles) — behavior matches the documented depth-1
   semantics; no memory growth across 20+ GO/UNDO cycles (watch RSS on
   desktop).
3. **Stereo integrity:** normalize a hard-panned stereo file — image
   unchanged (same gain both channels).
4. **Session-loss path:** make edits, `*` dirty flag shows, do NOT save,
   load a different sample, then reload project — old WAV on disk
   unchanged (loss is explicit and visible, matches design).
5. **Multi-instrument overwrite:** two Sample instruments referencing one
   WAV; edit+overwrite from one — the other reflects the new file after
   project reload (documented consequence of path-based storage).

## 2. Performance sanity

- Preview rebuild under key-repeat (Phase 0) on RG353V-class hardware:
  zooming stays responsive (target: no visible lag at ×1..×64 on a 30 s
  sample).
- Normalize/Delete/Crop on the largest supported WAV (64 MB) on handheld:
  UI stall within a few seconds; consider the busy-message polish only if
  worse.
- Undo slot allocation on 64 MB sample on web build — verify heap headroom;
  if the web target struggles, gate undo allocation failure with a clean
  message (op proceeds, undo unavailable) rather than crashing.

## 3. Code quality pass

- Realtime-boundary audit of every diff hunk from Phases 0–3 against
  `docs/realtime-architecture.md`: no allocations, no file I/O, no locks
  added on the audio side; every `free()`/`malloc()` of sample data inside
  pause/resume.
- `sample_ops` API review: no UI types leaked into `chipnomad_lib`;
  conversion helpers exist in exactly one place.
- Remove dead code from the old fixed-view path (Phase 0 leftovers);
  `waveform_display.h` stays backward compatible for SCWF/BYOWTBL callers.
- Warning-free build on all Makefiles touched.

## 4. Documentation

- `docs/USER_MANUAL.md`: rewrite the SAMPLE SETTINGS section → SAMPLE EDIT
  (zoom interaction, selection, PROCESS ops and their exact semantics
  incl. whole-sample fallback and Crop/Delete selection requirement, undo
  depth-1 toggle, Save/Save As/Rename, `*` dirty flag, path-based
  persistence model and the multi-instrument overwrite note).
- This doc set: mark each phase's checklist as executed; record any
  deviation from the plan in the corresponding doc (append a "Deviations"
  section) so future agents inherit the truth.
- `CHANGELOG.md` entry per repo convention.

## 5. Platform checklist

| Target | Command (from `tracker/`) | Gate |
|---|---|---|
| Tests | `make -f Makefile.test -j4` | green |
| Windows | `make -j4 windows` (MSYS2 UCRT64) | boots; walkthrough scenario §1.1 |
| Web | `Makefile.web web-deploy` (see AGENTS.md for the emsdk invocation) | commit regenerated `web/dist/`; browser walkthrough |
| PortMaster | `make -j4 PortMaster` | zip installs; §1.1 on device; audio-glitch check during ops |
| Linux AppImage | `make -j4 -f Makefile.linux appimage` | boots (optional per release process) |

## 6. Release notes inputs

- Feature list for the changelog/forum post: zoomable editor, selection,
  6 process ops + undo, save/save-as/rename.
- Known limitations to state openly: session-only selection, depth-1 undo,
  path-based persistence consequences, no touch editing.

## 7. Commit

`sampler edit: hardening, docs and platform validation for sample editor`

## 8. Execution record

Executed 2026-10-03 on Linux x86_64 (desktop only). Test suite final state:
**392 cases / 7,035,341 assertions, 0 failures, 0 warnings**
(`make -f Makefile.test -j4`).

### §1 E2E scenarios — automated where the engine is the truth

New `tracker/tests/test_sample_e2e.cpp` (suite `sample_e2e`, 8 cases)
automates the data path of every scenario; the UI-layer parts (screen
rendering, key handling, auditioning) were executed manually on desktop
during Phases 0–3 and are recorded in each phase's execution record.

| Scenario | Status | Evidence |
|---|---|---|
| 1.1 Full workflow (trim→select→normalize→fade→crop→save-as→reload) | PASS (engine path) | test "Full workflow: trim, select, normalize, fade, crop, save-as, reload" — byte-identical reload; project save/reload covered by existing project-io tests |
| 1.2 Undo journey (5 ops, toggle, 24 cycles) | PASS | tests "Undo journey: five ops, depth-1 toggle semantics" + "Undo toggle across 24 cycles keeps a single slot buffer" (only 2 heap blocks ever seen — no growth) |
| 1.3 Stereo integrity (hard-panned normalize) | PASS | test "Normalize hard-panned stereo keeps the channel image" (shared gain, left stays silent) |
| 1.4 Session-loss path | PASS (engine path) | test "Unsaved edits leave the file on disk unchanged" — disk file byte-identical after in-RAM edits; `*` flag behavior verified manually in Phase 3 |
| 1.5 Multi-instrument overwrite | PASS (engine path) | test "Overwrite propagates to a second instrument referencing the same WAV" — reload sees the new content |

Not executed on this machine: audition via Edit+Play on real audio device,
cold project reload in the running app, handheld walkthrough.

### §2 Performance sanity

- Desktop proxy automated: test "Large-sample ops complete in bounded time
  (64 MB class)" runs normalize/silence/fade/crop/delete/undo over an
  8M-frame stereo (32 MB) buffer inside the normal test timeout — PASS.
- Not executed: RG353V-class key-repeat zoom check, handheld 64 MB stall
  measurement, web heap headroom for the undo slot (no emsdk on this
  machine). The undo-failure fallback message already exists
  (`sampleOpErrorMemory` → "Out of memory"), so the web gate degrades
  cleanly if hit.

### §3 Code quality pass

- **Realtime-boundary audit: PASS.** All sample-data `malloc`/`free` sites
  live in `sample_ops.cpp` (lines 72/80, 128/138, 178/181, 207) and are
  reached only from `screen_sample_settings.cpp`, which wraps every path
  in `audioManager.pause()`/`resume()` — 4 pairs (process op, undo, save,
  save-as). No allocation, file I/O or lock was added on the audio side;
  the audio thread keeps reading the shallow snapshot and only ever sees
  the completed swap. `sampleSaveWav16` writes files under pause/resume.
- **`sample_ops` API review: PASS.** Header includes only
  `project_instruments.h` + stdint/stddef; no UI types; typed error enum;
  the 0–255↔frame conversion helpers exist in exactly one place
  (`project_instruments.h` forward, `sample_ops.h` inverses; the inverses
  are also used internally by `sample_ops.cpp` remap logic and by tests).
- **Dead code sweep: PASS.** `renderPCM16Preview` still has three callers
  (sample settings, instrument sample screen, and remains exported for
  SCWF/BYOWTBL); no Phase 0 leftovers found in the screen file;
  `waveform_display.h` untouched and backward compatible.
- **Warning-free build: PASS** for all files touched by Phases 0–3
  (Makefile.test log: 0 warnings; Linux build log: 0 warnings attributable
  to sample-edit files; remaining warnings are pre-existing in
  `external/` and unrelated modules).

### §4 Documentation

- `docs/USER_MANUAL.md`: the PCM Sample section was rewritten
  incrementally during Phases 0–3 and verified in this phase; Phase 4
  added the explicit session-loss statement (edits are RAM-only until a
  save flow; project reloads from disk; loading a different sample
  discards unsaved edits).
- This doc set committed to `docs/sampler-edit-plan/`; each phase doc got
  an execution record + deviations section (see §7 of phases 0–3 and §6 of
  phase 3).
- `CHANGELOG.md` entry added per repo convention.

### §5 Platform checklist — honest status

| Target | Status | Notes |
|---|---|---|
| Tests | **PASS** | 392/392, 0 warnings |
| Linux build | **PASS** | `make -j4 -f Makefile.linux linux` builds `build/linux/choochootracker` (9 MB), 0 sample-edit warnings |
| Linux AppImage | **NOT RUN** | `appimagetool` not installed on this machine; the underlying `linux` target builds |
| Windows | **NOT RUN** | requires MSYS2 UCRT64 (absent on this Linux box) |
| Web | **NOT RUN** | requires Emscripten via PowerShell on a Windows host (AGENTS.md workflow); `.tmp/emsdk` absent |
| PortMaster | **NOT RUN** | requires WSL2 ARM64 toolchain (absent); on-device §1.1 + audio-glitch check pending |

Pre-existing issue re-flagged (not introduced by this plan): the
`build_target` helper in `Makefile.common` runs
`chmod +x $(BUILD)/choochootracker$(OUTPUT_EXT)` but `Makefile.desktop`
outputs `chipnomad$(OUTPUT_EXT)` — `make -f Makefile.desktop` builds the
binary successfully and then exits non-zero on the chmod. One-line fix
suggested for a separate maintenance commit.

### §6 Release notes inputs

Features: zoomable waveform editor driven by Start/End fine-adjust;
independent processing selection (Sel.S/Sel.E) with zoom-aware handles;
six destructive process ops (Crop, Normalize, Delete, Silence, Fade In,
Fade Out) with one-level toggle undo; Save / Save As / Rename flows backed
by a new WAV writer; `*` dirty marker.

Known limitations (state openly): selection is session-only (not saved
with the project); undo is one level and toggles; the project stores only
the sample's path — overwriting a WAV shared by several instruments
changes all of them after reload; unsaved edits are lost on project
reload or loading a different sample; no touch editing.
