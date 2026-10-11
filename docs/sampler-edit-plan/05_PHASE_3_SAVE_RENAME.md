# Phase 3 — Sample Persistence: Save / Save As / Rename (F4)

**Goal.** Make edits durable. Add a WAV writer, and three file operations
wired into the editor screen: **Save** (overwrite the WAV the project
references), **Save As** (new file via the existing folder browser with
built-in overwrite confirm), **Rename** (rename the file on disk and update
the project reference). This is what makes Phases 0–2 meaningful beyond a
session: the project format stores only the sample's **path** and reloads
the WAV on project load.

## 0. Engine: WAV writer `sampleSaveWav16()`

Location: beside the reader — `sample_voice.cpp` already contains
`sampleLoadWav16` and byte-order helpers; either extend it or add
`sample_file.{h,cpp}` (agent's call, keep it UI-free).

```c
// Writes RIFF/WAVE, PCM16, interleaved; overwrites existing files.
// Returns 0 on success, nonzero on failure (caller maps to message).
int sampleSaveWav16(const InstrumentSample* sample, const char* path);
```

Spec:

- Standard 44-byte header: `RIFF`/`WAVE`, `fmt ` (PCM, channels,
  `sampleRate`, byteRate, blockAlign, 16 bits), `data` chunk sized
  `frameCount * channels * 2`. No extra chunks, no odd-padding needed
  (16-bit samples are always even), little-endian explicit byte writes
  (portable across the ARM/web targets).
- Guard: refuse `data == NULL || frameCount == 0`; refuse paths longer
  than `PROJECT_SAMPLE_PATH_LENGTH` (the struct field that must hold the
  stored path afterwards).
- Same-day test trick: `tests/test_import_wav.cpp` already synthesizes WAVs
  byte-by-byte — reuse that pattern reversed; a round-trip test
  (load → save → load → compare PCM) is cheap and catches endianness bugs.

Dependency verdict: hand-rolled (Option A). `dr_wav` (public domain/MIT-style)
was considered and rejected — it brings a single-header universe for what is
~80 lines here, and the reader it would pair with already exists hand-rolled;
mixing conventions buys nothing and the web/PortMaster builds stay untouched.

## 1. Screen UI

Rows appended to the editor grid (exact placement flexible):

```
row 17  Save    (tap → overwrite flow; dimmed when no file loaded)
row 18  SaveAs  (tap → folder browser; dimmed when nothing to save)
row 19  Rename  (tap → name entry; dimmed when no file loaded)
```

(If vertical space is tight after Phases 1–2, Save/SaveAs/Rename may share
one row as three cells — the grid supports multiple columns; agent decides
by measuring the 40×20 budget.)

### 1.1 Save (overwrite)

- Requires `sample->path[0]` (a loaded file) — otherwise dim/no-op.
- Tap → `confirmSetup("Overwrite <name>?", doSave, NULL)` →
  `screenConfirm`; confirmation runs
  `pause → sampleSaveWav16(path) → resume` and shows
  "Saved <name>" or the error message.
- **Why confirm here**: unlike the browser flow (which has its own
  confirm), direct Save silently destroys the source file — the dialog is
  the only guard. Message includes the filename because one WAV may be
  referenced by several instruments (no reference tracking in v1 — noted
  in the master plan risk register).

### 1.2 Save As

- Tap → `enterNameSetup("SAVE SAMPLE", "File name:", <current filename
  sans ext or instrument name>, onSaveNameEntered, onCancel)` →
  `screenSetup(&screenEnterName, 0)`.
- On name confirm →
  `fileBrowserSetupFolderMode("SAVE SAMPLE", appSettings.samplePath, name,
  ".wav", onSaveFolderSelected, onCancel)` → browser. The browser already:
  - shows "Save to [dir]" + "Create Folder",
  - builds `dir/name.wav`,
  - asks **"Overwrite existing file?"** when it exists (reuses
    `confirmSetup` internally),
  - calls `onSaveFolderSelected(dirPath)`.
- `onSaveFolderSelected(dir)`: compose `dir + PATH_SEPARATOR_STR + name +
  ".wav"`; `pause → sampleSaveWav16 → resume`; on success update
  `sample->path` (respecting `PROJECT_SAMPLE_PATH_LENGTH`), refresh
  `appSettings.samplePath = dir` (mirrors `onSampleLoaded`), set
  `projectModified = 1`, message "Saved <name>".
- Template flows to copy: SAVE INSTRUMENT (`screen_instrument.cpp`
  ~line 585–600) and SAVE AY WAVES (`screen_ay_wavetable.cpp` ~line
  350–395, the enterName→browser two-step).

### 1.3 Rename

- Tap → `enterNameSetup("RENAME SAMPLE", "New name:", <filename sans
  ext>, ...)`.
- On confirm: build old path from `sample->path` (dir + old name + ".wav")
  and new path (dir + new name + ".wav"); `fileRename(old, new)` from
  `corelib_file.h`; on success update `sample->path`, `projectModified = 1`.
  On failure (missing file, permission) → `MESSAGE_TIME_ERROR` message.
- Explicit non-goal: renaming does **not** touch the instrument's display
  name (`instrument->name`) — they are different entities in this app; the
  instrument name stays editable on the INSTRUMENT screen. Document in the
  manual. (Optional polish, only if trivial: if the instrument name equals
  the old basename, sync it — must be a conscious, visible behavior.)

### 1.4 Modified indicator (cheap, high value)

- The filename readout (row 1) shows `*name.wav` when the in-RAM sample
  differs from disk: set a module flag `sampleDirtyToDisk` on every
  successful process op (Phase 2 hook) and clear it on Save/SaveAs/reload.
- This is a UI-only flag (not persisted); it exists because the project
  reloads WAVs from disk — the indicator is the user's only hint that
  leaving without Save loses edits.

## 2. Files touched (expected, agent verifies)

- `chipnomad_lib/synth/sample_voice.{h,cpp}` (or new `sample_file.*`) —
  `sampleSaveWav16`.
- **New** tests in `tracker/tests/test_sample_encoding.cpp` (extend) or
  `test_sample_ops.cpp` — writer round-trip.
- `tracker/src/screens/screen_sample_settings.cpp` — three rows, flows,
  dirty flag, dimming logic.
- `docs/USER_MANUAL.md` — persistence section (Save vs Save As vs Rename;
  project-references-path model; multi-instrument overwrite warning).

## 3. Edge cases

- Path length: long dirs + 24-char names can exceed
  `PROJECT_SAMPLE_PATH_LENGTH` (255) — check before rename/save-as and
  error politely instead of truncating into a broken reference.
- Filename sanitation: reuse whatever `editCharacter` allows; strip/replace
  path separators in the entered name (the browser composes paths from
  input — same exposure as existing SAVE flows; match their behavior, do
  not invent a sanitizer here).
- Overwriting the WAV that *another* instrument also references: allowed
  (v1), documented; both instruments change sound after reload.
- Rename to the same name → no-op success; rename across devices/failed
  rename → error message, path unchanged.
- Save As into a different folder: subsequent project load needs that
  folder to resolve — same constraint as loading any WAV today; no change
  to project format.
- Web build: writes go to the Emscripten MEMFS/IDBFS workspace — flows
  must not assume desktop FS; the existing browser screens already handle
  this, so no special code, but the web-deploy bundle must be regenerated
  (AGENTS.md).
- Android: existing document import/export helpers (`fileExportDocument`)
  exist for platform hand-off; v1 keeps in-workspace saving identical to
  other save flows. Note in manual.

## 4. Manual QA checklist (Phase 3)

1. Load WAV → Crop → Save → confirm → file on disk has the cropped
   duration (check in an external player); exit app, reopen project —
   cropped version loads.
2. Save As to a new folder/name; new file exists; editor now points at the
   new path (title row updated); old file untouched; project save +
   reload resolves the new path.
3. Save As onto an existing name → browser's overwrite confirm fires.
4. Rename → file renamed on disk; project reload still plays (path
   updated); old name gone.
5. Rename with no file loaded → dimmed/no-op; Save with no path (shouldn't
   happen post-load) → graceful message.
6. `*` dirty flag: appears after a process op; disappears after Save /
   Save As / fresh load.
7. Song playing during Save — no glitch beyond the pause; file valid.
8. Long path (>255) → clear error, no corruption of `sample->path`.
9. Round-trip: 8-bit mono source WAV → load → save → reload → identical
   playback (8→16→8 bit depth increase is expected and fine).
10. Tests green.

## 5. Commit

`sampler edit: WAV writer plus save, save-as and rename flows for edited samples`

## 6. Execution record (Phase 4 hardening pass)

Executed 2026-10-03 on Linux x86_64 desktop. Commit `e5fbdd0`.

### Deviations

- **D1 — Writer placed in `sample_voice.{h,cpp}`** (extended the reader's
  module) rather than a new `sample_file.{h,cpp}`; keeps the WAV read/write
  pair in one file as the plan's first option.
- **D2 — Save/Save As/Rename share one File row** (three cells at x=9/16/25)
  instead of three rows — the 40×20 budget after Phases 0–2 left one row
  above the message line; the plan explicitly allowed this ("may share one
  row as three cells").
- **D3 — Dirty-flag restore across dialogs.** `setup()` resets
  `sampleDirtyToDisk` on re-entry, which would clear the marker after every
  dialog round trip (confirm/enterName/browser). Fixed with
  `settingsReturnFromDialog(keepDirty)` + `pendingDirtyRestore`: dialogs
  remember the flag and `setup()` restores it. Not in the plan (the plan
  did not anticipate the setup-reset interaction).
- **D4 — Rename keeps the `*` marker** (content unchanged) — matches the
  plan's dirty-flag semantics; documented in the manual.
- **D5 — Instrument-name sync on rename not implemented** (the plan marked
  it optional polish; the explicit non-goal "rename does not touch the
  instrument name" is documented in the manual).

### Checklist status

Items 1–9: executed on desktop during Phase 3 (overwrite flow with
external-file verification, Save As into a new folder, overwrite confirm,
rename round trip, dimming rules, `*` lifecycle, save during playback,
long-path rejection, 8-bit round trip). Item 10: tests green — final
Phase 4 state is 392 cases / 7,035,341 assertions, 0 failures, 0 warnings.
Web MEMFS flow (§3) not exercised on this machine — see Phase 4 execution
record.
