#include "screens.h"
#include "common.h"
#include "corelib_gfx.h"
#include "corelib/corelib_file.h"
#include "chipnomad_lib.h"
#include "screen_instrument.h"
#include "waveform_display.h"
#include "file_browser.h"
#include "screen_enter_name.h"
#include "synth/sample_voice.h"
#include "synth/sample_ops.h"
#include "audio_manager.h"
#include <stdio.h>
#include <string.h>

// Field geometry (32 columns; the shared instrument panel starts at x=34).
// Row labels sit at x=0; values start at x=9.
static constexpr int valueX = 9;
// Process op and File action share the widest field ("Normalize" = 9 chars)
static constexpr int opWidth = 9;
static constexpr int sliceWidth = 6;
static constexpr int goX = 19;
static constexpr int goWidth = 3;
static constexpr int undoX = 24;
static constexpr int undoWidth = 6;
static constexpr int fileGoX = 19;
static constexpr int fileGoWidth = 3;
// Region/Select rows carry two values: "START [] END []"
static constexpr int markerLabelX = 9;   // "START"
static constexpr int selValX = 15;
static constexpr int selValWidth = 6;
static constexpr int endLabelX = 22;     // "END"
static constexpr int selEndValX = 26;
// A blank row separates the preview from the field block; the File row
// stays above the message line (screen row 19).
static constexpr int previewRow = 3;
static constexpr int previewWidth = 32;
static constexpr int previewHeight = 8;
static constexpr int fieldRow0 = 12;
static const char* sliceLabels[] = {"Off", "2", "4", "8", "16", "32"};
static const char* speedAlgorithmLabels[] = {"Dirty", "Clean"};
static const uint8_t sliceValues[] = {0, 2, 4, 8, 16, 32};
static constexpr int sliceCount = 6;

// Process toolbox: one selected operation plus GO/UNDO buttons. The op is
// cycled with Edit+Left/Right; Edit+Opt clears it to "none".
static const char* processOpLabels[] = {"Crop", "Normalize", "Delete", "Silence", "Fade In", "Fade Out", "Reverse"};
static const char* processOpDoneMessages[] = {"Cropped", "Normalized", "Deleted", "Silenced", "Faded in", "Faded out", "Reversed"};
static constexpr int processOpCount = 7;
static int processOp; // index into processOpLabels, -1 = none

// File row action: 0 = Save (overwrite), 1 = Save As (new file). GO runs it.
static int fileAction;

// One-level undo slot for the process tools. Screen module state: never
// saved with the project, dropped when the screen is (re)entered.
static SampleUndo editorUndo;

// Set when the sample data in RAM differs from the file on disk. Cleared by
// the Save flows; shown as a '*' before the filename.
static int sampleDirtyToDisk;

// Dialog round trips re-enter the screen, which resets the session state.
// The dirty flag travels through this slot so a cancelled save or a rename
// cannot make unsaved changes look saved.
static int pendingDirtyRestore;

// File name (without extension) captured between the Save As name entry and
// the folder browser.
static char saveAsName[64];

static Bitmap* samplePreviewBitmap;
static Bitmap* sampleSliceMarkerBitmap;
static Bitmap* sampleStartMarkerBitmap;
static Bitmap* sampleEndMarkerBitmap;
static Bitmap* sampleSelectionBitmap;

static int sliceToIndex(uint8_t slice) {
  for (int i = 1; i < sliceCount; ++i) {
    if (sliceValues[i] == slice) return i;
  }
  return 0;
}

static const char* sampleFilename(const char* path) {
  const char* separator = strrchr(path, PATH_SEPARATOR);
  return separator ? separator + 1 : path;
}

static const char* shortSampleFilename(const char* path, size_t maxLength) {
  const char* name = sampleFilename(path);
  size_t length = strlen(name);
  return length > maxLength ? name + length - maxLength : name;
}

// Basename without extension: "dir/loop.wav" -> "loop". A name with no dot
// (or a leading dot) is returned unchanged.
static void sampleBasenameSansExt(const char* path, char* out, size_t outSize) {
  const char* name = sampleFilename(path);
  const char* dot = strrchr(name, '.');
  size_t length = dot && dot != name ? (size_t)(dot - name) : strlen(name);
  if (length >= outSize) length = outSize - 1;
  memcpy(out, name, length);
  out[length] = 0;
}

static InstrumentSample* currentSample(void) {
  return &chipnomadState->project.instruments[cInstrument].chip.sample;
}

static Bitmap* ensurePreviewBitmap(Bitmap** bitmap) {
  if (*bitmap && ((*bitmap)->widthChars != previewWidth || (*bitmap)->heightChars != previewHeight)) {
    gfxBitmapFree(*bitmap);
    *bitmap = NULL;
  }
  if (!*bitmap) *bitmap = gfxBitmapCreate(previewWidth, previewHeight);
  return *bitmap;
}

static uint8_t sampleSliceDivisions(const InstrumentSample* sample) {
  return sampleNormalizeSlice(sample->slice);
}

// Which marker the view window follows: 0 none, 1 Start, 2 End,
// 3/4 the selection handles.
enum {
  kViewAnchorNone = 0,
  kViewAnchorStart = 1,
  kViewAnchorEnd = 2,
  kViewAnchorSelStart = 3,
  kViewAnchorSelEnd = 4,
};

// View window into the sample: viewStart is the first visible frame,
// viewEnd one past the last visible frame (<= frameCount).
struct SampleEditorView {
  uint32_t viewStart;
  uint32_t viewEnd;
  int anchor;
};

static SampleEditorView editorView;

// Processing selection: the region the process tools (editor phase 2) act
// on. Independent from the playback Start/End markers and stored in frames,
// not 0-255 normalized values. Session-only editor state: never saved with
// the project; entering the screen seeds it with the playback Region span.
struct SampleEditorSelection {
  uint32_t start;  // inclusive frame
  uint32_t end;    // exclusive frame; start == end => empty
  uint8_t active;  // 0 = empty/none
};

static SampleEditorSelection editorSelection;

// Set while a fine adjustment (EDIT+LEFT/RIGHT) holds the zoomed view in;
// releasing EDIT drops the view back to the full sample. Session-only.
static int zoomHoldActive;

// Smallest zoomed window in frames
static constexpr uint32_t kMinViewSpan = 8;

// Fixed zoom span for fine adjustments: one second of audio, clamped to
// the sample length. Samples that fit inside the window keep the full 1:1
// view; longer ones always show the same time span, so the window stays
// readable on short one-shots and long recordings alike.
static uint32_t zoomSpan(const InstrumentSample* sample) {
  const uint32_t frameCount = sample->frameCount;
  uint32_t rate = sample->sampleRate;
  if (rate == 0) rate = 44100; // fresh samples: assume the default rate
  const uint64_t window = (uint64_t)rate;
  if (frameCount <= window) return frameCount; // whole sample fits: 1:1
  uint32_t span = (uint32_t)window;
  if (span < kMinViewSpan) span = kMinViewSpan;
  return span;
}

// Frame position of the view anchor: a playback marker (1 = Start, 2 = End,
// before the reverse-order swap - the zoom anchor follows the marker itself,
// not the active region) or a selection handle (3/4).
static uint32_t anchorFrame(const InstrumentSample* sample,
                            const SampleEditorSelection* selection, int anchor) {
  switch (anchor) {
    case kViewAnchorStart: return sampleMarkerToStartFrame(sample->frameCount, sample->start);
    case kViewAnchorEnd: return sampleMarkerToEndFrame(sample->frameCount, sample->end);
    case kViewAnchorSelStart: return selection->start;
    case kViewAnchorSelEnd: return selection->end;
    default: return 0;
  }
}

static void zoomOutFull(const InstrumentSample* sample, SampleEditorView* view) {
  view->viewStart = 0;
  view->viewEnd = sample->frameCount;
  view->anchor = kViewAnchorNone;
}

// Maps an absolute frame to a pixel column inside the view window. Returns
// -1 when the frame is outside the window. A frame exactly on viewEnd pins
// to the last column only when pinToEnd is set (the end marker is an
// exclusive bound drawn at its limit).
static int frameToPixel(uint32_t frame, const SampleEditorView* view, int width, int pinToEnd) {
  if (width <= 0 || view->viewEnd <= view->viewStart) return -1;
  if (frame < view->viewStart || frame > view->viewEnd) return -1;
  if (frame == view->viewEnd) return pinToEnd ? width - 1 : -1;
  int x = (int)((uint64_t)(frame - view->viewStart) * width / (view->viewEnd - view->viewStart));
  return x < width ? x : width - 1;
}

// Zooms to the fixed span (one second of audio) around markerFrame, keeping
// the marker at its relative position inside the window (the first zoom from
// the full view centers it). When the marker would sit at a window edge it
// pans just enough to keep a small margin, so repeated fine steps follow
// the marker without drift. The span never shrinks: every fine step shows
// the same window size.
static void zoomToMarker(const InstrumentSample* sample, SampleEditorView* view,
                         uint32_t markerFrame) {
  const uint32_t frameCount = sample->frameCount;
  if (frameCount == 0 || sample->data == NULL) {
    zoomOutFull(sample, view);
    return;
  }

  const uint32_t newSpan = zoomSpan(sample);
  if (newSpan >= frameCount) {
    zoomOutFull(sample, view);
    return;
  }

  const uint32_t span = view->viewEnd - view->viewStart;
  uint32_t viewStart;
  const int fullView = span == 0 || (view->viewStart == 0 && view->viewEnd == frameCount);
  if (fullView || markerFrame < view->viewStart || markerFrame > view->viewEnd) {
    // First zoom-in (or the marker left the window): center on the marker
    viewStart = markerFrame >= newSpan / 2 ? markerFrame - newSpan / 2 : 0;
  } else {
    // Keep the marker at its relative position inside the window
    const uint64_t relative = (uint64_t)(markerFrame - view->viewStart) * newSpan / span;
    viewStart = markerFrame >= relative ? markerFrame - (uint32_t)relative : 0;
  }

  // Pan so the marker stays visible with a margin at the hit edge
  const uint32_t margin = newSpan / 8 > 0 ? newSpan / 8 : 1;
  if (markerFrame < viewStart + margin) {
    viewStart = markerFrame >= margin ? markerFrame - margin : 0;
  } else if ((uint64_t)markerFrame + margin > (uint64_t)viewStart + newSpan) {
    viewStart = (uint64_t)markerFrame + margin > newSpan
      ? (uint32_t)((uint64_t)markerFrame + margin - newSpan) : 0;
  }

  if (viewStart + newSpan > frameCount) viewStart = frameCount - newSpan;
  view->viewStart = viewStart;
  view->viewEnd = viewStart + newSpan;
}

static void updateSamplePreview(const InstrumentSample* sample, const SampleEditorView* view,
                                const SampleEditorSelection* selection) {
  Bitmap* waveform = ensurePreviewBitmap(&samplePreviewBitmap);
  Bitmap* markers = ensurePreviewBitmap(&sampleSliceMarkerBitmap);
  Bitmap* startMarker = ensurePreviewBitmap(&sampleStartMarkerBitmap);
  Bitmap* endMarker = ensurePreviewBitmap(&sampleEndMarkerBitmap);
  Bitmap* selectionBand = ensurePreviewBitmap(&sampleSelectionBitmap);
  if (!waveform || !markers || !startMarker || !endMarker || !selectionBand) return;

  // Calculate actual start/end positions in frames
  uint32_t frameCount = sample->frameCount;
  uint32_t startFrame = sampleMarkerToStartFrame(frameCount, sample->start);
  uint32_t endFrame = sampleMarkerToEndFrame(frameCount, sample->end);
  if (startFrame > endFrame) { uint32_t swap = startFrame; startFrame = endFrame; endFrame = swap + 1; }

  // Determine if we should use start/end or full range
  uint8_t slices = sampleSliceDivisions(sample);

  // Render only the view window; markers and greying map through it
  renderPCM16Preview(waveform, sample->data, view->viewStart, view->viewEnd, sample->channels);

  int width = waveform->widthPixels;
  int height = waveform->heightPixels;
  uint32_t viewSpan = view->viewEnd - view->viewStart;

  // Create start marker bitmap (single vertical line); skipped when the
  // marker sits outside the view window
  gfxBitmapClear(startMarker);
  {
    int startX = frameToPixel(startFrame, view, width, 0);
    if (startX >= 0) {
      for (int y = 0; y < height; y++) {
        startMarker->data[y * width + startX] = 255;
      }
    }
  }
  // Create end marker bitmap (single vertical line). It is an exclusive
  // bound, so a marker on the window's right edge pins to the last column.
  gfxBitmapClear(endMarker);
  {
    int endX = frameToPixel(endFrame, view, width, 1);
    if (endX >= 0) {
      for (int y = 0; y < height; y++) {
        endMarker->data[y * width + endX] = 255;
      }
    }
  }
  // Adjust waveform brightness
  // - Active area (between start/end): waveform at 255 (light blue)
  // - Inactive area (before start, after end): ONLY waveform pixels greyed to 48, background stays 0
  // A column is active when its frame range intersects [startFrame, endFrame);
  // an empty region (Start == End) leaves every column inactive.
  {
    for (int x = 0; x < width; x++) {
      uint32_t columnStart = view->viewStart + (uint64_t)x * viewSpan / width;
      uint32_t columnEnd = view->viewStart + (uint64_t)(x + 1) * viewSpan / width;
      if (columnEnd > view->viewEnd) columnEnd = view->viewEnd;
      if (columnEnd > startFrame && columnStart < endFrame && endFrame > startFrame) continue;
      for (int y = 0; y < height; y++) {
        if (waveform->data[y * width + x] == 255) {
          // Inactive waveform pixel: dark gray
          waveform->data[y * width + x] = 48;
        }
        // Active waveform pixels stay at 255 (light blue)
        // Background pixels (0) stay at 0 in both areas
      }
    }
  }
  // Create slice markers
  gfxBitmapClear(markers);
  if (slices) {
    // When in slice mode, slice markers divide the loop region (start to end)
    // This ensures slices follow the start/end markers
    uint32_t loopLength = endFrame > startFrame ? (endFrame - startFrame) : frameCount;
    if (loopLength == 0) loopLength = frameCount;

    // Draw slice markers within the loop region; markers outside the view
    // window are skipped cleanly
    for (int i = 1; i < slices; ++i) {
      uint32_t position = startFrame + (uint32_t)((uint64_t)loopLength * i / slices);
      int x = frameToPixel(position, view, width, 0);
      if (x < 0) continue;
      for (int y = 0; y < height; ++y) markers->data[y * width + x] = 255;
    }
  }
  // Selection band + handles: dim fill over columns overlapping
  // [selection->start, selection->end), bright lines at the handles.
  // Positions map through the view window; handles outside are skipped.
  gfxBitmapClear(selectionBand);
  if (selection->active) {
    for (int x = 0; x < width; x++) {
      uint32_t columnStart = view->viewStart + (uint64_t)x * viewSpan / width;
      uint32_t columnEnd = view->viewStart + (uint64_t)(x + 1) * viewSpan / width;
      if (columnEnd > view->viewEnd) columnEnd = view->viewEnd;
      if (columnEnd <= selection->start || columnStart >= selection->end) continue;
      for (int y = 0; y < height; y++) selectionBand->data[y * width + x] = 96;
    }
    int selStartX = frameToPixel(selection->start, view, width, 0);
    if (selStartX >= 0) {
      for (int y = 0; y < height; y++) selectionBand->data[y * width + selStartX] = 255;
    }
    int selEndX = frameToPixel(selection->end, view, width, 1);
    if (selEndX >= 0) {
      for (int y = 0; y < height; y++) selectionBand->data[y * width + selEndX] = 255;
    }
  }
}

static void drawSamplePreview(void) {
  // Clear the waveform area and space for frame
  gfxClearRect(0, previewRow, previewWidth, previewHeight);
  // Draw the waveform with light blue color for active area
  if (samplePreviewBitmap) {
    gfxSetFgColor(0xADD8E6); // Light blue
    gfxDrawBitmap(samplePreviewBitmap, 0, previewRow);
  }
  // Draw the selection band + handles (scheme info color, distinct from the
  // yellow/orange playback markers)
  if (sampleSelectionBitmap) {
    gfxSetFgColor(appSettings.colorScheme.textInfo);
    gfxDrawBitmap(sampleSelectionBitmap, 0, previewRow);
  }
  // Draw start marker (yellow) - always shown
  if (sampleStartMarkerBitmap) {
    gfxSetFgColor(0xFFFF00); // Yellow
    gfxDrawBitmap(sampleStartMarkerBitmap, 0, previewRow);
  }
  // Draw end marker (orange) - always shown
  if (sampleEndMarkerBitmap) {
    gfxSetFgColor(0xFFA500); // Orange
    gfxDrawBitmap(sampleEndMarkerBitmap, 0, previewRow);
  }
  // Draw the slice markers on top
  if (sampleSliceMarkerBitmap) {
    gfxSetFgColor(appSettings.colorScheme.textDefault);
    gfxDrawBitmap(sampleSliceMarkerBitmap, 0, previewRow);
  }
}

static int settingsColumnCount(int row) {
  // Region/Select rows: START + END; Process row: op + GO + UNDO; File row:
  // action + GO
  if (row == 0 || row == 1) return 2;
  if (row == 4) return 3;
  if (row == 5) return 2;
  return 1;
}

// Filename row with the '*' dirty marker. Called from the static draw and
// after process ops so the marker appears without a full redraw.
static void drawFilenameRow(void) {
  InstrumentSample* sample = currentSample();
  gfxSetFgColor(appSettings.colorScheme.textDefault);
  gfxClearRect(0, 1, 32, 1);
  int nameX = 0;
  if (sampleDirtyToDisk) {
    gfxPrint(0, 1, "*");
    nameX = 1;
  }
  gfxPrint(nameX, 1, shortSampleFilename(sample->path, 32 - nameX));
}

static void settingsDrawStatic(void) {
  const ColorScheme cs = appSettings.colorScheme;
  InstrumentSample* sample = currentSample();
  gfxSetFgColor(cs.textTitles);
  gfxPrint(0, 0, "SAMPLE EDIT");
  drawFilenameRow();
  gfxSetFgColor(cs.textInfo);
  if (sample->data && sample->frameCount) {
    char formatText[24];
    snprintf(formatText, sizeof(formatText), "%u Hz %s", (unsigned)sample->sampleRate,
             sample->channels >= 2 ? "STEREO" : "MONO");
    gfxPrint(0, 2, formatText);
  }
  updateSamplePreview(sample, &editorView, &editorSelection);
  drawSamplePreview();
  gfxSetFgColor(cs.textDefault);
  gfxPrint(0, fieldRow0, "Region");
  gfxPrint(markerLabelX, fieldRow0, "START");
  gfxPrint(endLabelX, fieldRow0, "END");
  gfxPrint(0, fieldRow0 + 1, "Select");
  gfxPrint(markerLabelX, fieldRow0 + 1, "START");
  gfxPrint(endLabelX, fieldRow0 + 1, "END");
  gfxPrint(0, fieldRow0 + 2, "Slice");
  gfxPrint(0, fieldRow0 + 3, "Spd algo");
  gfxPrint(0, fieldRow0 + 4, "Process");
  gfxPrint(0, fieldRow0 + 5, "File");
}

static void settingsDrawCursor(int col, int row) {
  if (row == 0 || row == 1) {
    gfxCursor(col == 0 ? selValX : selEndValX, fieldRow0 + row, selValWidth);
  } else if (row == 4) {
    gfxCursor(col == 0 ? valueX : col == 1 ? goX : undoX, fieldRow0 + row,
              col == 0 ? opWidth : col == 1 ? goWidth : undoWidth);
  } else if (row == 5) {
    gfxCursor(col == 0 ? valueX : fileGoX, fieldRow0 + row, col == 0 ? opWidth : fileGoWidth);
  } else if (row == 3) {
    gfxCursor(valueX, fieldRow0 + row, sliceWidth);
  } else {
    gfxCursor(valueX, fieldRow0 + row, sliceWidth);
  }
}

static void settingsDrawRowHeader(int row, CellState state) {
  (void)row;
  (void)state;
}

static void settingsDrawColHeader(int col, CellState state) {
  (void)col;
  (void)state;
}

static void settingsDrawField(int col, int row, CellState state) {
  InstrumentSample* sample = currentSample();
  gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
  if (row == 4) {
    if (col == 0) {
      // Process op selector
      gfxClearRect(valueX, fieldRow0 + row, opWidth, 1);
      if (processOp < 0) gfxSetFgColor(appSettings.colorScheme.textEmpty);
      gfxPrint(valueX, fieldRow0 + row, processOp < 0 ? "-" : processOpLabels[processOp]);
    } else if (col == 1) {
      gfxClearRect(goX, fieldRow0 + row, goWidth, 1);
      gfxPrint(goX, fieldRow0 + row, "GO");
    } else {
      gfxClearRect(undoX, fieldRow0 + row, undoWidth, 1);
      // UNDO is inert without a prepared slot: dim it
      if (!editorUndo.active) gfxSetFgColor(appSettings.colorScheme.textEmpty);
      gfxPrint(undoX, fieldRow0 + row, "UNDO");
    }
    return;
  }
  if (row == 5) {
    // File action + GO. Save needs a file path, Save As needs sample data
    // (it can assign a path to a fresh sample).
    if (col == 0) {
      gfxClearRect(valueX, fieldRow0 + row, opWidth, 1);
      const char* label = fileAction == 0 ? "Save" : "Save As";
      if (fileAction == 0 && !sample->path[0]) gfxSetFgColor(appSettings.colorScheme.textEmpty);
      if (fileAction == 1 && (!sample->data || !sample->frameCount)) gfxSetFgColor(appSettings.colorScheme.textEmpty);
      gfxPrint(valueX, fieldRow0 + row, label);
    } else {
      gfxClearRect(fileGoX, fieldRow0 + row, fileGoWidth, 1);
      gfxPrint(fileGoX, fieldRow0 + row, "GO");
    }
    return;
  }
  if (row == 3) {
    gfxClearRect(valueX, fieldRow0 + row, sliceWidth, 1);
    gfxPrint(valueX, fieldRow0 + row,
             speedAlgorithmLabels[sample->speedAlgorithm <= 1 ? sample->speedAlgorithm : 0]);
    return;
  }
  if (row == 0 || row == 1) {
    // Region (hex markers) and Select (frame handles): two values per row
    const int x = col == 0 ? selValX : selEndValX;
    gfxClearRect(x, fieldRow0 + row, selValWidth, 1);
    if (row == 0) {
      gfxPrint(x, fieldRow0 + row, byteToHex(col == 0 ? sample->start : sample->end));
    } else {
      if (!editorSelection.active) {
        gfxPrint(x, fieldRow0 + row, "-");
      } else {
        uint32_t frame = col == 0 ? editorSelection.start : editorSelection.end;
        char text[16];
        snprintf(text, sizeof(text), "%06u", (unsigned)frame);
        gfxPrint(x, fieldRow0 + row, text);
      }
    }
    return;
  }
  // Row 2: Slice
  gfxClearRect(valueX, fieldRow0 + row, sliceWidth, 1);
  // Slice is inert while Stretch drives the duration: dim it.
  if (sample->stretchMode != 0) gfxSetFgColor(appSettings.colorScheme.textEmpty);
  gfxPrint(valueX, fieldRow0 + row, sliceLabels[sliceToIndex(sample->slice)]);
}

// Clamp the selection to the sample, swap inverted handles and update the
// active flag; clamp the view window and re-anchor it if its anchor marker
// vanished. Repaints the preview. Shared with the process tools (phase 2).
static void sampleEditorNormalizeState(const InstrumentSample* sample,
                                       SampleEditorSelection* selection,
                                       SampleEditorView* view) {
  const uint32_t frameCount = sample->frameCount;
  if (selection->start > frameCount) selection->start = frameCount;
  if (selection->end > frameCount) selection->end = frameCount;
  if (selection->start > selection->end) {
    uint32_t swap = selection->start;
    selection->start = selection->end;
    selection->end = swap;
  }
  selection->active = selection->start < selection->end;

  if (frameCount == 0) {
    zoomOutFull(sample, view);
  } else {
    if (view->viewEnd > frameCount) view->viewEnd = frameCount;
    if (view->viewStart >= view->viewEnd) zoomOutFull(sample, view);
    if (view->anchor != kViewAnchorNone) {
      // Drop the anchor when its marker no longer exists (empty selection,
      // or a playback marker that fell outside the sample)
      uint32_t frame = anchorFrame(sample, selection, view->anchor);
      if (view->anchor >= kViewAnchorSelStart && !selection->active) {
        view->anchor = kViewAnchorNone;
      } else if (frame < view->viewStart || frame > view->viewEnd) {
        view->anchor = kViewAnchorNone;
      }
    }
  }
  updateSamplePreview(sample, view, selection);
}

// Repaints everything a process op can invalidate: the preview, the
// marker/selection fields and the Process row.
static void settingsRepaintAfterOp(void) {
  drawFilenameRow();
  drawSamplePreview();
  for (int row = 0; row < 3; ++row) {
    settingsDrawField(0, row, CellState::normal);
    if (row < 2) settingsDrawField(1, row, CellState::normal);
  }
  settingsDrawField(0, 3, CellState::normal);
  settingsDrawField(0, 4, CellState::normal);
  settingsDrawField(1, 4, CellState::normal);
  settingsDrawField(2, 4, CellState::normal);
}

// Runs the selected process op on the current selection (or the whole
// sample when no selection is active). Pauses audio while the buffer is
// swapped or edited, prepares the one-level undo slot first.
static void settingsRunProcessOp(void) {
  InstrumentSample* sample = currentSample();
  if (processOp < 0) {
    screenMessage(MESSAGE_TIME, "Select operation");
    return;
  }
  // Crop and Delete reshape the sample: they need an explicit region.
  if ((processOp == 0 || processOp == 2) && !editorSelection.active) {
    screenMessage(MESSAGE_TIME, "Select region first");
    return;
  }

  uint32_t selStart = editorSelection.active ? editorSelection.start : 0;
  uint32_t selEnd = editorSelection.active ? editorSelection.end : 0;

  audioManager.pause();
  int result = sampleOpPrepareUndo(sample, &editorUndo);
  if (result == sampleOpOk) {
    switch (processOp) {
      case 0: result = sampleOpCrop(sample, selStart, selEnd); break;
      case 1: result = sampleOpNormalize(sample, selStart, selEnd); break;
      case 2: result = sampleOpDelete(sample, selStart, selEnd); break;
      case 3: result = sampleOpSilence(sample, selStart, selEnd); break;
      case 4: result = sampleOpFade(sample, selStart, selEnd, 1); break;
      case 5: result = sampleOpFade(sample, selStart, selEnd, 0); break;
      case 6: result = sampleOpReverse(sample, selStart, selEnd); break;
    }
  }
  audioManager.resume();

  if (result != sampleOpOk) {
    sampleOpFreeUndo(&editorUndo);
    const char* error = "Operation failed";
    switch (result) {
      case sampleOpErrorNoSample: error = "No sample loaded"; break;
      case sampleOpErrorRange: error = "Empty range"; break;
      case sampleOpErrorWholeSample: error = "Delete whole sample not allowed"; break;
      case sampleOpErrorMemory: error = "Out of memory"; break;
    }
    screenMessage(MESSAGE_TIME_ERROR, "%s", error);
    return;
  }

  projectModified = 1;
  sampleDirtyToDisk = 1;
  // Crop/Delete reshape the sample: drop the selection and zoom back out
  if (processOp == 0 || processOp == 2) {
    editorSelection.start = 0;
    editorSelection.end = 0;
    editorSelection.active = 0;
    zoomOutFull(sample, &editorView);
  }
  sampleEditorNormalizeState(sample, &editorSelection, &editorView);
  settingsRepaintAfterOp();
  screenMessage(MESSAGE_TIME, "%s", processOpDoneMessages[processOp]);
}

static void settingsRunUndo(void) {
  if (!editorUndo.active) return;
  InstrumentSample* sample = currentSample();
  audioManager.pause();
  int result = sampleOpApplyUndo(sample, &editorUndo);
  audioManager.resume();
  if (result != sampleOpOk) {
    screenMessage(MESSAGE_TIME_ERROR, "Undo failed");
    return;
  }
  projectModified = 1;
  zoomOutFull(sample, &editorView);
  sampleEditorNormalizeState(sample, &editorSelection, &editorView);
  settingsRepaintAfterOp();
  screenMessage(MESSAGE_TIME, "Undone");
}

// Return to the sample editor from a dialog. Re-entering the screen resets
// the session state, so the dirty flag travels through pendingDirtyRestore:
// keep it whenever the file on disk may still differ from the sample in RAM.
static void settingsReturnFromDialog(int keepDirty) {
  pendingDirtyRestore = keepDirty ? sampleDirtyToDisk : 0;
  screenSetup(&screenSampleSettings, cInstrument);
}

static void settingsCancelDialog(void) {
  settingsReturnFromDialog(1);
}

// Save: overwrite the WAV the sample was loaded from, after confirmation.
static void settingsDoSave(void) {
  InstrumentSample* sample = currentSample();
  char error[128];
  audioManager.pause();
  int result = sampleSaveWav16(sample, sample->path, error, sizeof(error));
  audioManager.resume();
  if (result != 0) {
    screenMessage(MESSAGE_TIME_ERROR, "%s", error);
    settingsReturnFromDialog(1);
    return;
  }
  screenMessage(MESSAGE_TIME, "Saved %s", shortSampleFilename(sample->path, 24));
  settingsReturnFromDialog(0);
}

static void settingsRunSave(void) {
  InstrumentSample* sample = currentSample();
  if (!sample->path[0]) return;
  char message[128];
  snprintf(message, sizeof(message), "Overwrite %s?", shortSampleFilename(sample->path, 48));
  confirmSetup(message, settingsDoSave, settingsCancelDialog);
  screenSetup(&screenConfirm, 0);
}

// Save As: pick a name, then a folder; the sample is written as
// <folder>/<name>.wav and the instrument points at the new file.
static void settingsSaveAsFolderSelected(const char* folderPath) {
  InstrumentSample* sample = currentSample();
  // folder + separator + name + ".wav" must fit the stored path length
  if (strlen(folderPath) + strlen(saveAsName) + 5 > PROJECT_SAMPLE_PATH_LENGTH) {
    screenMessage(MESSAGE_TIME_ERROR, "Path too long");
    settingsReturnFromDialog(1);
    return;
  }
  char newPath[PROJECT_SAMPLE_PATH_LENGTH + 2];
  snprintf(newPath, sizeof(newPath), "%s%s%s.wav", folderPath, PATH_SEPARATOR_STR, saveAsName);
  char error[128];
  audioManager.pause();
  int result = sampleSaveWav16(sample, newPath, error, sizeof(error));
  audioManager.resume();
  if (result != 0) {
    screenMessage(MESSAGE_TIME_ERROR, "%s", error);
    settingsReturnFromDialog(1);
    return;
  }
  strncpy(sample->path, newPath, PROJECT_SAMPLE_PATH_LENGTH);
  sample->path[PROJECT_SAMPLE_PATH_LENGTH] = 0;
  strncpy(appSettings.samplePath, folderPath, PATH_LENGTH);
  appSettings.samplePath[PATH_LENGTH] = 0;
  projectModified = 1;
  screenMessage(MESSAGE_TIME, "Saved %s", saveAsName);
  settingsReturnFromDialog(0);
}

static void settingsSaveAsNameEntered(const char* name) {
  strncpy(saveAsName, name, sizeof(saveAsName) - 1);
  saveAsName[sizeof(saveAsName) - 1] = 0;
  fileBrowserSetupFolderMode("SAVE SAMPLE", appSettings.samplePath, saveAsName, ".wav",
                             settingsSaveAsFolderSelected, settingsCancelDialog);
  screenSetup(&screenFileBrowser, 0);
}

static void settingsRunSaveAs(void) {
  InstrumentSample* sample = currentSample();
  if (!sample->data || !sample->frameCount) return;
  char initialName[25]; // the name entry field holds 24 characters
  sampleBasenameSansExt(sample->path, initialName, sizeof(initialName));
  enterNameSetup("SAVE SAMPLE", "File name:", initialName, settingsSaveAsNameEntered, settingsCancelDialog);
  screenSetup(&screenEnterName, 0);
}

static int settingsOnEdit(int col, int row, CellEditAction action) {
  InstrumentSample* sample = currentSample();
  int handled = 0;
  int marker = 0; // 1 = Start, 2 = End
  if (row == 3) {
    if (col) return 0;
    uint8_t algorithm = sample->speedAlgorithm <= 1 ? sample->speedAlgorithm : 0;
    handled = edit8noLast(action, &algorithm, 1, 0, 1);
    if (handled) {
      sample->speedAlgorithm = algorithm;
      projectModified = 1;
      settingsDrawField(0, 3, CellState::focus);
    }
    return handled;
  }
  if (row == 4) {
    if (col == 0) {
      // Cycle the operation; Edit+Opt clears it to none
      if (action == CellEditAction::clear) {
        if (processOp < 0) return 0;
        processOp = -1;
      } else if (action == CellEditAction::increase || action == CellEditAction::increaseBig) {
        processOp = processOp < 0 ? 0 : (processOp + 1) % processOpCount;
      } else if (action == CellEditAction::decrease || action == CellEditAction::decreaseBig) {
        processOp = processOp < 0 ? processOpCount - 1 : (processOp + processOpCount - 1) % processOpCount;
      } else if (action == CellEditAction::tap || action == CellEditAction::doubleTap) {
        processOp = processOp < 0 ? 0 : (processOp + 1) % processOpCount;
      } else {
        return 0;
      }
      settingsDrawField(0, 4, CellState::focus);
      return 1;
    }
    if (action != CellEditAction::tap && action != CellEditAction::doubleTap) return 0;
    if (col == 1) settingsRunProcessOp();
    else settingsRunUndo();
    return 1;
  }
  if (row == 5) {
    if (col == 0) {
      // Cycle the action (any edit key toggles); Edit+Opt resets it to Save
      if (action == CellEditAction::clear) {
        if (fileAction == 0) return 0;
        fileAction = 0;
      } else if (action == CellEditAction::tap || action == CellEditAction::doubleTap ||
                 action == CellEditAction::increase || action == CellEditAction::decrease ||
                 action == CellEditAction::increaseBig || action == CellEditAction::decreaseBig) {
        fileAction = fileAction == 0 ? 1 : 0;
      } else {
        return 0;
      }
      settingsDrawField(0, 5, CellState::focus);
      return 1;
    }
    if (action != CellEditAction::tap && action != CellEditAction::doubleTap) return 0;
    if (fileAction == 0) settingsRunSave();
    else settingsRunSaveAs();
    return 1;
  }
  if (row == 0) {
    // Region row: the playback Start/End markers, stored on the sample as
    // normalised 00-FF values. Fine steps move one unit, coarse steps 16;
    // edit8noLast already steps by one, so only the coarse clamping is
    // spelled out here (same shape as the Select row below).
    action = convertMultiAction(action);
    uint8_t* value = col == 0 ? &sample->start : &sample->end;
    if (action == CellEditAction::tap) {
      handled = 1;
    } else if (action == CellEditAction::clear) {
      if (*value != 0) handled = 1;
      *value = 0;
    } else if (action == CellEditAction::increase || action == CellEditAction::decrease ||
               action == CellEditAction::increaseBig || action == CellEditAction::decreaseBig) {
      const int fine = action == CellEditAction::increase || action == CellEditAction::decrease;
      const int up = action == CellEditAction::increase || action == CellEditAction::increaseBig;
      const uint8_t step = fine ? 1 : 16;
      uint8_t next;
      if (up) next = *value > 255 - step ? 255 : (uint8_t)(*value + step);
      else next = *value < step ? 0 : (uint8_t)(*value - step);
      if (next != *value) {
        *value = next;
        handled = 1;
      }
    }
    marker = col == 0 ? kViewAnchorStart : kViewAnchorEnd;
  } else if (row == 1) {
    // Select row: processing-selection handles. Fine steps move fifteen
    // frames and zoom onto the handle; coarse steps jump frameCount/64 (min 16)
    // and return to the full-sample view. Tap copies the matching Region
    // marker position; clear empties the whole selection. Start/End are
    // untouched.
    const uint32_t frameCount = sample->frameCount;
    if (frameCount == 0) return 0;
    uint32_t* handle = col == 0 ? &editorSelection.start : &editorSelection.end;
    if (action == CellEditAction::tap) {
      uint32_t markerPos = col == 0 ? sampleMarkerToStartFrame(frameCount, sample->start)
                                    : sampleMarkerToEndFrame(frameCount, sample->end);
      if (editorSelection.active && *handle == markerPos) return 0;
      *handle = markerPos;
      handled = 1;
    } else if (action == CellEditAction::clear) {
      if (!editorSelection.active && editorSelection.start == 0 && editorSelection.end == 0) return 0;
      editorSelection.start = 0;
      editorSelection.end = 0;
      handled = 1;
    } else {
      // Fine steps move fifteen frames; coarse steps jump frameCount/64 (min 16)
      uint32_t step = 15;
      if (action == CellEditAction::increaseBig || action == CellEditAction::decreaseBig) {
        step = frameCount / 64;
        if (step < 16) step = 16;
      }
      if (action == CellEditAction::increase || action == CellEditAction::increaseBig) {
        uint64_t next = (uint64_t)*handle + step;
        if (next > frameCount) next = frameCount;
        if (next == *handle) return 0;
        *handle = (uint32_t)next;
      } else if (action == CellEditAction::decrease || action == CellEditAction::decreaseBig) {
        uint32_t next = *handle > step ? *handle - step : 0;
        if (next == *handle) return 0;
        *handle = next;
      } else {
        return 0;
      }
      handled = 1;
    }
    if (handled) {
      if (action == CellEditAction::increase || action == CellEditAction::decrease) {
        editorView.anchor = col == 0 ? kViewAnchorSelStart : kViewAnchorSelEnd;
        zoomToMarker(sample, &editorView, *handle);
        zoomHoldActive = 1;
      } else if (action == CellEditAction::increaseBig || action == CellEditAction::decreaseBig ||
                 action == CellEditAction::clear) {
        zoomOutFull(sample, &editorView);
        zoomHoldActive = 0;
      }
      sampleEditorNormalizeState(sample, &editorSelection, &editorView);
      drawSamplePreview();
      // Repaint both handle readouts: normalization may have swapped them
      settingsDrawField(0, 1, CellState::focus);
      settingsDrawField(1, 1, CellState::focus);
    }
    return handled;
  } else if (row == 2) {
    // Slice is inert while Stretch drives the duration.
    if (sample->stretchMode != 0) return 0;
    uint8_t index = (uint8_t)sliceToIndex(sample->slice);
    handled = edit8noLast(action, &index, 1, 0, sliceCount - 1);
    if (handled) {
      sample->slice = sliceValues[index];
      // Stretch and Slice are mutually exclusive: enabling one disables the other.
      if (sample->slice) sample->stretchMode = 0;
      projectModified = 1;
      updateSamplePreview(sample, &editorView, &editorSelection);
      drawSamplePreview();
    }
    return handled;
  }
  if (handled) {
    projectModified = 1;
    // Fine steps zoom onto the edited marker so the waveform shows exactly
    // what is being adjusted; coarse steps (and clear) return to the
    // full-sample view. The zoom holds while EDIT stays down
    // (zoomHoldActive); EDIT release drops back to the full view.
    if (action == CellEditAction::increase || action == CellEditAction::decrease) {
      editorView.anchor = marker;
      zoomToMarker(sample, &editorView, anchorFrame(sample, &editorSelection, marker));
      zoomHoldActive = 1;
    } else if (action == CellEditAction::increaseBig || action == CellEditAction::decreaseBig ||
               action == CellEditAction::clear) {
      zoomOutFull(sample, &editorView);
      zoomHoldActive = 0;
    }
    updateSamplePreview(sample, &editorView, &editorSelection);
    drawSamplePreview();
  }
  return handled;
}

// Slice is inert while Stretch drives the duration: skip it in navigation.
// Save needs a file path, Save As needs sample data.
static int settingsIsCellValid(int col, int row) {
  InstrumentSample* sample = currentSample();
  if (row == 2 && sample->stretchMode != 0) return 0;
  if (row == 5 && col == 0) {
    return fileAction == 0 ? sample->path[0] != 0 : (sample->data != NULL && sample->frameCount > 0);
  }
  return 1;
}

static ScreenData screenSampleSettingsData = {
  .rows = 6,
  .cursorRow = 0,
  .cursorCol = 0,
  .topRow = 0,
  .selectMode = -1,
  .selectStartRow = 0,
  .selectStartCol = 0,
  .selectAnchorRow = 0,
  .selectAnchorCol = 0,
  .playbackLevel = ScreenPlaybackLevel::none,
  .getColumnCount = settingsColumnCount,
  .drawStatic = settingsDrawStatic,
  .drawCursor = settingsDrawCursor,
  .drawSelection = NULL,
  .drawRowHeader = settingsDrawRowHeader,
  .drawColHeader = settingsDrawColHeader,
  .drawField = settingsDrawField,
  .onEdit = settingsOnEdit,
  .onInput = NULL,
  .onRawInput = NULL,
  .isCellValid = settingsIsCellValid,
  .getLoopRange = NULL,
};

static void setup(int input) {
  if (input != -1) cInstrument = input;
  // Entering the screen always starts at the full-sample view. The
  // selection is seeded with the playback Region span - the whole sample
  // with the default markers - so the process tools act on the region out
  // of the box. Both are session-only editor state, re-derived on every
  // entry; the undo slot is dropped too - undo never survives leaving the
  // screen. The dirty flag is restored from pendingDirtyRestore when a
  // dialog round trip re-enters.
  InstrumentSample* sample = currentSample();
  sampleOpFreeUndo(&editorUndo);
  zoomOutFull(sample, &editorView);
  editorSelection.start = sampleMarkerToStartFrame(sample->frameCount, sample->start);
  editorSelection.end = sampleMarkerToEndFrame(sample->frameCount, sample->end);
  editorSelection.active = 1;
  sampleEditorNormalizeState(sample, &editorSelection, &editorView);
  zoomHoldActive = 0;
  sampleDirtyToDisk = pendingDirtyRestore;
  pendingDirtyRestore = 0;
}

static void fullRedraw(void) {
  // The cursor persists across screens: if it is parked on Slice while Stretch
  // is active, move it up so it never rests on a disabled cell.
  if (screenSampleSettingsData.cursorRow == 2 && currentSample()->stretchMode != 0) {
    screenSampleSettingsData.cursorRow = 1;
  }
  // Same for the File row: the previous instrument may have left the cursor
  // on an action the current sample cannot use.
  if (screenSampleSettingsData.cursorRow == 4 &&
      !settingsIsCellValid(screenSampleSettingsData.cursorCol, 4)) {
    screenSampleSettingsData.cursorRow = 1;
  }
  screenFullRedraw(&screenSampleSettingsData);
}

static void draw(void) {
}

static int inputScreenNavigation(int keys) {
  if (keys == keyOpt || keys == (keyLeft | keyShift)) {
    screenSetup(&screenInstrument, cInstrument);
    return 1;
  }
  if (keys == (keyRight | keyShift)) {
    screenSetup(&screenTable, cInstrument);
    return 1;
  }
  if (keys == (keyDown | keyShift)) {
    screenSetup(&screenInstrumentPool, cInstrument);
    return 1;
  }
  if (keys == (keyUp | keyShift)) {
    screenSetup(&screenModulation, cInstrument);
    return 1;
  }
  return 0;
}

static int onInput(int isKeyDown, int keys, int tapCount) {
  // EDIT release ends a zoom hold: fine adjustments zoom in while EDIT is
  // down, and letting go of it returns to the full-sample view. Key-up
  // events carry the still-held buttons, so EDIT counts as released only
  // when its bit is gone (releasing another key while EDIT is held keeps
  // the zoom).
  if (zoomHoldActive && !isKeyDown && !(keys & keyEdit)) {
    zoomHoldActive = 0;
    InstrumentSample* sample = currentSample();
    if (editorView.viewStart != 0 || editorView.viewEnd != sample->frameCount) {
      zoomOutFull(sample, &editorView);
      updateSamplePreview(sample, &editorView, &editorSelection);
      drawSamplePreview();
    }
  }
  if (inputScreenNavigation(keys)) return 1;
  return screenInput(&screenSampleSettingsData, isKeyDown, keys, tapCount);
}

static ScreenPlaybackLevel getPlaybackLevel(void) {
  return ScreenPlaybackLevel::phrase;
}

const AppScreen screenSampleSettings = {
  .init = NULL,
  .setup = setup,
  .fullRedraw = fullRedraw,
  .draw = draw,
  .onInput = onInput,
  .getPlaybackLevel = getPlaybackLevel,
};
