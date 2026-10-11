# 07 — Phased Coding-Agent Prompts

Feed these sequentially. Each prompt assumes the doc set lives in the repo at
`docs/sampler-edit-plan/` (adjust paths if you place it elsewhere). After
each phase the agent must: run the tests, run the phase's manual QA
checklist items that are automatable, update `docs/USER_MANUAL.md`, and make
one scoped commit. If a session dies mid-phase, re-feed the same phase
prompt plus "continue from current git state".

---

## PRELUDE — include at the top of EVERY phase prompt

```text
You are working on ChooChooTracker (branch "sampler"), a portable C++17
music tracker. Repo rules live in AGENTS.md at the root; architecture
rules in docs/ARCHITECTURE.md and docs/realtime-architecture.md. You are
implementing one phase of the Sample Editor plan.

MANDATORY READING before writing code:
- AGENTS.md
- docs/sampler-edit-plan/00_MASTER_PLAN.md  (§2 hard constraints is binding)
- docs/sampler-edit-plan/01_CODEBASE_ANALYSIS.md  (the map; do not re-derive)
- The specific phase doc for your task (docs/sampler-edit-plan/0N_*.md)

NON-NEGOTIABLE CONSTRAINTS:
1. Realtime boundary: the audio callback reads a shallow snapshot of the
   project; InstrumentSample.data pointers are shared. Any malloc/free of
   sample data must be wrapped in audioManager.pause()/resume().
2. UI is a 40x20 character grid driven by ScreenData/CellEditAction — no
   popups, no mouse, no new widget toolkit. Follow the edit-action table in
   the codebase analysis (§5).
3. C++17, repo code style (static module state, snprintf, C-strings where
   used). No exceptions/allocations in the audio path.
4. No new third-party dependencies. No changes to the project save format.
5. Tests must pass: cd tracker && make -f Makefile.test -j4.
6. Update docs/USER_MANUAL.md before committing (never in-app help).
7. Stage only files belonging to this phase in the commit.
8. Do NOT implement anything from later phases, and do NOT touch the
   slicing engine (explicitly deferred in the master plan non-goals).

WORKFLOW:
1. Read the docs listed above, then the files named in your phase doc.
2. Implement the phase.
3. Run the unit tests; fix until green.
4. Walk through the phase doc's manual QA checklist; report results.
5. Update docs/USER_MANUAL.md and (if you deviated from the plan) append a
   "Deviations" section to the phase doc explaining why.
6. Commit with the message suggested at the end of the phase doc.

Report back: what you changed (files), test output summary, QA checklist
results with pass/fail per item, and any deviations from the plan.
```

---

## PHASE 0 — Editor foundation & responsive zoom

```text
[PRELUDE]

TASK: Phase 0 — implement Feature 1 exactly as specified in
docs/sampler-edit-plan/02_PHASE_0_EDITOR_FOUNDATION.md.

Summary (the phase doc is the contract; this is orientation only):
- Restructure screen_sample_settings.cpp into the SAMPLE EDIT layout.
- Add the SampleEditorView state (viewStart/viewEnd/anchor) and the two
  functions zoomToMarker()/zoomOutFull() with the anchor-relative zoom and
  edge-pan behavior described in the doc.
- Wire it to the Start/End rows: CellEditAction::increase/decrease zoom in
  anchored on the edited marker; increaseBig/decreaseBig snap the view
  back to 1:1.
- All existing overlays (start/end markers, slice lines, inactive greying)
  must render correctly through the view window; extract the single shared
  0-255 <-> frame conversion helper.
- Add the zoom readout row.
- No engine changes, no selection yet, no process ops yet.

ACCEPTANCE: the phase doc's Manual QA checklist (§5) passes and the test
suite is green. Commit message:
"sampler edit: zoomable waveform view driven by fine/coarse start-end adjustment"
```

---

## PHASE 1 — Processing selection region

```text
[PRELUDE]

TASK: Phase 1 — implement Feature 2 exactly as specified in
docs/sampler-edit-plan/03_PHASE_1_SELECTION.md.

Summary:
- Add the SampleSelection state (absolute frames, start<end normalized,
  active flag) as module state; NOT in InstrumentSample, NOT persisted.
- Add Sel.S / Sel.E rows: fine/coarse nudging with zoom anchoring on the
  handle (anchor values 3/4), tap = copy from Start/End marker,
  Edit+Opt = clear selection.
- Render the selection band + handle lines as a new bitmap layer; correct
  draw order vs existing layers; positions mapped through the Phase-0 view.
- Implement sampleEditorNormalizeState() (clamps, swap, anchor fixup,
  repaint) — Phase 2 will reuse it.

ACCEPTANCE: the phase doc's Manual QA checklist (§6) passes; tests green.
Commit message:
"sampler edit: independent processing-selection region with zoom-aware handles"
```

---

## PHASE 2 — PROCESS box, DSP core, undo

```text
[PRELUDE]

TASK: Phase 2 — implement Feature 3 exactly as specified in
docs/sampler-edit-plan/04_PHASE_2_PROCESS_UNDO.md.

Summary:
- New engine module chipnomad_lib/synth/sample_ops.{h,cpp}: sampleOpCrop/
  Normalize/Delete/Silence/Fade(+fadeIn flag) with the exact contract in
  the op table (§0.1), including start/end remap rules and the
  whole-sample fallback. Verify the test Makefile's source globs pick it up.
- New tracker/tests/test_sample_ops.cpp covering the minimum test set
  (§4) — write these tests, they are part of the deliverable.
- Depth-1 undo: sampleOpPrepareUndo/sampleOpApplyUndo (swap semantics,
  buffer deep copy + header copy), slot owned by the screen, reset on
  setup()/sample load.
- PROCESS UI rows (Process picker / GO / UNDO) in the editor screen; GO
  runs under audioManager.pause()/resume(), sets projectModified, calls
  sampleEditorNormalizeState(), repaints; typed errors mapped to
  screenMessage strings; Crop/Delete require a selection (hint message
  otherwise).

ACCEPTANCE: all unit tests from §4 pass; the phase doc's Manual QA
checklist (§6) passes. Commit message:
"sampler edit: sample process ops (crop/normalize/delete/silence/fade) with one-level undo"
```

---

## PHASE 3 — Save / Save As / Rename

```text
[PRELUDE]

TASK: Phase 3 — implement Feature 4 exactly as specified in
docs/sampler-edit-plan/05_PHASE_3_SAVE_RENAME.md.

Summary:
- sampleSaveWav16() in chipnomad_lib (beside sampleLoadWav16): standard
  44-byte RIFF/WAVE PCM16 writer, explicit little-endian writes, path
  length guard; plus a load->save->load round-trip doctest.
- Save (overwrite) row: confirmSetup guard naming the file, then
  pause -> write -> resume.
- Save As row: enterNameSetup -> fileBrowserSetupFolderMode (which already
  handles the overwrite confirm) -> write + update sample->path +
  appSettings.samplePath + projectModified.
- Rename row: enterNameSetup -> fileRename on disk -> update sample->path;
  instrument display name untouched.
- "*" dirty-to-disk flag on the filename readout, set by process ops,
  cleared by save/reload.

ACCEPTANCE: the phase doc's Manual QA checklist (§4) passes, including the
cold-reload persistence scenario; tests green; web/dist regenerated if you
touched WASM-relevant sources (AGENTS.md rule). Commit message:
"sampler edit: WAV writer plus save, save-as and rename flows for edited samples"
```

---

## PHASE 4 — Hardening

```text
[PRELUDE]

TASK: Phase 4 — execute docs/sampler-edit-plan/06_PHASE_4_HARDENING.md.

Summary:
- Run the end-to-end scenarios (§1) on desktop; report results.
- Realtime-boundary audit of all four phase diffs (§3).
- Performance sanity on the largest supported WAV (§2).
- Rewrite the SAMPLE SETTINGS section of docs/USER_MANUAL.md into SAMPLE
  EDIT covering every interaction shipped in Phases 0-3.
- Append "Deviations" sections to phase docs where reality diverged.
- Build the Windows and Web targets per AGENTS.md; PortMaster if a
  toolchain is available (report if not, do not fake it).
- CHANGELOG.md entry.

ACCEPTANCE: every checklist item in the phase doc reported pass/fail with
notes; no open critical issues. Commit message:
"sampler edit: hardening, docs and platform validation for sample editor"
```

---

## Session-resume template (any phase)

```text
[PRELUDE]

CONTINUATION: a previous session may have partially completed Phase N.
Before writing anything: git log --oneline -15, git status, git diff.
Determine the actual state, finish the remaining work of Phase N per its
doc, then complete the standard ACCEPTANCE steps. Do not redo work that is
already committed.
```
