# 01 — Codebase Analysis for the Sampler Editor

Everything an implementing agent needs to know about the existing code that
touches (or is touched by) the sampler edit window. File references are
relative to the repo root. Line numbers refer to the `sampler` branch as of
the plan's writing (commit `fe2de5f`) and may drift — re-locate by symbol
name, not by number.

## 1. Two-layer structure

- `chipnomad_lib/` — engine: project model, sequencer, voices. **No UI
  includes.** This is where DSP (process ops) and any sample file helpers
  shared by screens live. Reaches the UI only through `chipnomad_lib.h`
  functions and `ChipNomadState`.
- `tracker/src/` — application: screens (`screens/`), SDL/audio glue
  (`audio_manager.cpp`), rendering helpers (`waveform_display.cpp`),
  platform cores under `corelib/`.

The screen under expansion: `tracker/src/screens/screen_sample_settings.cpp`
(= `screenSampleSettings`, title "SAMPLE SETTINGS"). Its parent screen:
`tracker/src/screens/instrument_sample.cpp` (= `screenInstrumentSample`,
title "Sample" — hosts Load/EDIT entry, Pitch, Stretch, Loop, Speed and the
shared voice-post (filter/ADSR) fields).

## 2. Data model

`chipnomad_lib/project_instruments.h`:

```c
struct InstrumentSample : InstrumentVoicePostSettings {
  char path[PROJECT_SAMPLE_PATH_LENGTH + 1]; // 255, the WAV this sample came from
  uint32_t sampleRate;
  uint32_t frameCount;      // frames per channel
  uint8_t  channels;        // 1 or 2
  int16_t* data;            // interleaved PCM16, malloc'd; owned by the project
  int8_t   pitch;
  uint16_t speedPercent;    // 100 = normal
  uint8_t  start;           // 0..255 normalized playback start  (256th notation)
  uint8_t  end;             // 0..255 normalized playback end    (255 = last frame)
  uint8_t  loopMode;        // 0 off, 1 loop, 2 ping-pong
  uint8_t  slice;           // 0 off, else 2/4/8/16/32 divisions of the loop region
  uint8_t  stretchMode;     // 0 off, 1..8 beats/bars; mutually exclusive with slice
};
```

Key facts an editor must respect:

- **Start/End are 0–255 normalized**, not frames. Frame mapping is
  `startFrame = start * (frameCount-1) / 255`,
  `endFrame = end==255 ? frameCount : (end+1) * frameCount / 256`
  (see `updateSamplePreview()` in `screen_sample_settings.cpp`). They swap
  if inverted. Any buffer-mutating op must remap them.
- `slice` divides the **loop region** (start..end), not the whole file.
- `stretchMode != 0` disables `slice` (and vice versa) — enforced in both
  screens' edit handlers.
- Sample buffers are `malloc`'d in `sampleLoadWav16()`
  (`chipnomad_lib/synth/sample_voice.cpp`) and freed by
  `freeSampleInstrument()` (`chipnomad_lib/project_instruments.cpp`).
- Instruments live in `chipnomadState->project.instruments[cInstrument]`;
  `cInstrument` is defined in `screen_instrument.h/.cpp`.
- SCWF (`InstrumentSCWF`) and BYOWTBL embed two `InstrumentSample`
  oscillators and reuse the same loader — future reuse path for `sample_ops`,
  out of scope for v1.

## 3. Realtime boundary (why pause/resume exists)

`chipnomad_lib.cpp`:

- `AudioCommandQueue::publishProject()` shallow-copies the whole `Project`
  struct into a snapshot slot; the audio thread adopts it at the next tick
  (`applyProject`). Pointers — including `InstrumentSample.data` — are
  **shared**, not deep-copied.
- `chipnomadQueueProjectRefresh(state)` (line ~638) publishes a new snapshot;
  the UI calls it from `app.cpp`'s tick handler when `audioProjectDirty` is
  set. `appInput()` sets `audioProjectDirty = 1` after **every** handled
  key-down (app.cpp:249), so parameter edits propagate automatically.
- Consequence: freeing/reallocating `sample->data` while audio runs leaves
  the audio snapshot holding a dangling pointer. **Every buffer mutation
  must be wrapped in `audioManager.pause()` / `audioManager.resume()`.**
  Reference implementation: `onSampleLoaded()` in
  `instrument_sample.cpp`:

```c
audioManager.pause();
sampleLoadWav16(path, &instrument->chip.sample, error, sizeof(error));
audioManager.resume();
```

`audioManager` (`tracker/src/audio_manager.h/.cpp`) exposes `pause()`,
`resume()`, `previewSample(path)`, `stopSamplePreview()`; `pause()` suspends
the platform callback (`audioPause(1)`), so mutation under pause is safe.

## 4. The current SAMPLE SETTINGS screen

`screen_sample_settings.cpp` (≈340 lines) — read it fully before starting.

Layout (40×20 grid): row 0 title `SAMPLE SETTINGS`; row 2 filename (32
chars); rows 4–9 waveform preview (32 chars × 6 chars bitmap); rows 11–13
fields `Start` / `End` / `Slice` (value column `valueX = 9`, width 7).
Navigation: `keyOpt` or Shift+Left → back to INSTRUMENT; Shift+Right →
TABLE; Shift+Up → MODULATION; Shift+Down → INSTRUMENT POOL
(`inputScreenNavigation()`).

Rendering internals (the pattern to extend):

- Four persistent `Bitmap*` layers: `samplePreviewBitmap` (waveform),
  `sampleStartMarkerBitmap`, `sampleEndMarkerBitmap` (vertical lines),
  `sampleSliceMarkerBitmap`. Each is cleared and rebuilt in
  `updateSamplePreview()` on every change.
- Waveform: `renderPCM16Preview(bitmap, data, startFrame, endFrame,
  channels)` in `waveform_display.cpp` — per pixel column, min/max of the
  frames in that column's range, drawn as a vertical line of 255s. **It
  already accepts an arbitrary frame window → zooming = passing a sub-range,
  no renderer changes strictly required.**
- Inactive region greying: after render, waveform pixels outside
  start..end are darkened to 48 (background stays 0).
- Draw order in `drawSamplePreview()`: waveform (light blue `0xADD8E6`),
  start marker (yellow `0xFFFF00`), end marker (orange `0xFFA500`), slice
  markers (scheme `textDefault`). Bitmaps are 8-bit grayscale; color comes
  from `gfxSetFgColor()` at draw time.
- The preview is only repainted by explicit calls to
  `updateSamplePreview()` + `drawSamplePreview()` from `drawStatic()` and
  after edits — there is no per-frame animation on this screen.

## 5. Input conventions (critical for F1)

`tracker/src/screens/screens.h` + `screens.cpp`:

- Screens are `ScreenData` spreadsheets: `rows`, `cursorRow/Col`,
  `getColumnCount(row)`, `drawField`, `drawCursor`, `onEdit(col, row,
  CellEditAction)`, optional `onInput`/`isCellValid`.
- **Edit semantics** (`inputNormalMode()` in screens.cpp, ~line 500):

| Keys | Action | Meaning |
|---|---|---|
| Edit+Right | `CellEditAction::increase` | fine +1 |
| Edit+Left | `CellEditAction::decrease` | fine −1 |
| Edit+Up | `CellEditAction::increaseBig` | coarse +N |
| Edit+Down | `CellEditAction::decreaseBig` | coarse −N |
| Edit (tap) | `tap` | open browser/screen/value |
| Edit+Opt | `clear` | reset value |

- Key repeat (Settings: `keyRepeatDelay`, `keyRepeatSpeed`) re-sends the
  same `keys` bitmask; zoom/selection code must be idempotent under rapid
  repeats and cheap enough to repaint the preview per repeat.
- Arrow keys alone move the cursor (never reach `onEdit`); `onInput` hook
  can intercept raw combos before the grid (used by
  `instrument_sample.cpp` `onInput` with `popupEditInput`).
- Value edit helpers: `edit8noLast`, `editSigned8`, `editNormalized16`,
  `editCharacter`/`charEditInput` (text), all declared in `screens.h`.
- `screenFullRedraw(&screenData)` repaints everything; partial updates are
  done by calling `drawField`/`drawSamplePreview` directly, as the current
  screen does.

## 6. File I/O and reusable flows (critical for F3/F4)

- **File browser** (`tracker/src/screens/file_browser.{h,cpp}`):
  - Load mode: `fileBrowserSetupWithPreview(title, ext, startPath,
    onFile, onCancel, onPreview)` — used for loading WAVs today.
  - **Save/folder mode** (built and battle-tested):
    `fileBrowserSetupFolderMode(title, startPath, filename, ext,
    onFolderSelected, onCancel)` — shows "Save to [dir]" + "Create Folder",
    constructs `dir/filename+ext`, and **already asks
    `confirmSetup("Overwrite existing file?")` when the target exists**
    (file_browser.cpp ~line 459–477). The callback receives the directory.
  - `fileBrowserGetAdjacentPath()` — Load+Left/Right cycles files in a
    folder (see `loadAdjacentSample()` in `instrument_sample.cpp`).
- **Name entry**: `enterNameSetup(title, prompt, initialName, onConfirmed,
  onCancelled)` (`screen_enter_name.{h,cpp}`) → `screenEnterName`; used by
  SAVE AY WAVES / SAVE THEME flows.
- **Confirm dialog**: `confirmSetup(message, confirmCb, cancelCb)` +
  `screenSetup(&screenConfirm, 0)` (`screen_confirm.cpp`).
- **corelib_file.h**: `fileRename(old, new)`, `fileDelete(path)`,
  `fileDirectoryExists`, `fileCreateDirectory(Recursive)`,
  `fileListDirectory`, `PATH_SEPARATOR(_STR)`.
- **Settings paths** (`tracker/src/common.h` `AppSettings`):
  `samplePath` (last WAV folder, updated on load in
  `onSampleLoaded`), `projectPath`, `instrumentPath`, etc. Use
  `appSettings.samplePath` as the browser start folder.
- **Existing save-flow templates to copy**: SAVE INSTRUMENT
  (`screen_instrument.cpp` ~line 585–600: builds filename, folder-mode
  browser, then serializes in the callback), SAVE AY WAVES
  (`screen_ay_wavetable.cpp` ~line 350–395: enterName → folder browser).

## 7. Sample pipeline & persistence

- Load: `sampleLoadWav16(path, sample, err, errSize)` — PCM8/16,
  mono/stereo, 1 kHz–192 kHz, **64 MB max data size**; converts 8-bit to
  16-bit; stores path via `sampleStorePath()` (relative to CWD when
  possible).
- **Project format** (`chipnomad_lib/project_instruments_io.cpp`,
  `loadInstrumentSample`): stores `- Sample path: ...`, pitch, speed,
  start, end, loop, slice, stretch, volume, voice-post. **PCM data is not
  embedded** — on project load the WAV is re-read from `sample->path`.
  Missing file ⇒ silent empty sample. ⇒ *Every edit must be written to the
  WAV on disk (overwrite or Save As) or it dies with the session.*
- Playback: `SampleVoice::configure()` (`sample_voice.cpp`) — consumes
  start/end/slice/loop/stretch; slicing divides the loop region
  (`sampleSliceFrames`); `playbackPreviewNote` (queued via
  `chipnomadQueuePlaybackPreviewNote`) auditions the instrument on a track
  — the natural way to preview edits (INSTRUMENT screen already does this
  via Edit+Play).
- `projectModified` (tracker `common.h`) marks the project dirty; set it
  after every edit.

## 8. Tests

- Framework: **doctest**, single runner (`tests/test_main.cpp`), sources
  auto-discovered by `tracker/Makefile.test` (`tests/test_*.cpp`).
- Existing relevant suites: `test_sample_voice.cpp`,
  `test_import_wav.cpp` (builds WAV files byte-by-byte in-test — same trick
  reverses for writer tests), `test_sample_encoding.cpp`,
  `test_stretch_processor.cpp`.
- Engine modules (`chipnomad_lib`) are UI-free and thus directly testable —
  the process core lands there.

## 9. Gap analysis → what the editor needs that does not exist yet

| Gap | Blocks | Where it will live |
|---|---|---|
| Zoomable view (view window + anchor state) | F1 | `screen_sample_settings.cpp` (module state) + small renderer helper |
| Selection state + overlay layer | F2 | same screen file (+ 1 bitmap layer) |
| DSP ops + frame remap + undo slot | F3 | **new** `chipnomad_lib/synth/sample_ops.{h,cpp}` + `tracker/tests/test_sample_ops.cpp` |
| PROCESS UI (picker, GO, UNDO rows) | F3 | `screen_sample_settings.cpp` |
| WAV writer `sampleSaveWav16()` | F4 | `chipnomad_lib/synth/sample_voice.cpp` (beside the reader) or new `sample_file.{h,cpp}` |
| Save / Save As / Rename flows + modified flag | F4 | `screen_sample_settings.cpp` reusing browser/enterName/confirm |
| Preview repaint budget under key repeat | F1 | cap preview rebuild rate or keep per-pixel scan (32-col min/max is cheap: ≤256 px wide) |
