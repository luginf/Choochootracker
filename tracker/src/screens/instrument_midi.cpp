#include "screen_instrument.h"
#include "corelib_gfx.h"
#include "utils.h"

// A MIDI-out instrument has no synth voice of its own: triggering a note
// sends a MIDI Note On/Off on the chosen channel to whatever device is
// selected in Settings, instead of rendering audio (see
// chipnomad_lib/midi_io.h and chipnomad_lib.cpp's applyVoiceEvents()).
// The only per-instrument parameter is which of the 16 MIDI channels to use.

static int columns(int row) { return row < 3 ? instrumentCommonColumnCount(row) : 1; }

static void drawStatic(void) {
  instrumentCommonDrawStatic();
  gfxSetFgColor(appSettings.colorScheme.textDefault);
  gfxPrint(0, 6, "Channel");
}

static void drawCursor(int col, int row) {
  if (row < 3) { instrumentCommonDrawCursor(col, row); return; }
  gfxCursor(11, 6, 2);
}

static void drawField(int col, int row, CellState state) {
  if (row < 3) { instrumentCommonDrawField(col, row, state); return; }
  InstrumentMidi* m = &chipnomadState->project.instruments[cInstrument].chip.midi;
  gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
  gfxClearRect(11, 6, 3, 1);
  gfxPrintf(11, 6, "%02d", m->channel + 1);
}

static int onEdit(int col, int row, CellEditAction action) {
  if (row < 3) return instrumentCommonOnEdit(col, row, action);
  InstrumentMidi* m = &chipnomadState->project.instruments[cInstrument].chip.midi;
  int ok = edit8noLast(action, &m->channel, 1, 0, 15);
  if (ok) { projectModified = 1; screenFullRedraw(&screenInstrumentMidi); }
  return ok;
}

ScreenData screenInstrumentMidi = {
  .rows = 4, .cursorRow = 0, .cursorCol = 0, .topRow = 0,
  .selectMode = -1, .selectStartRow = 0, .selectStartCol = 0, .selectAnchorRow = 0, .selectAnchorCol = 0,
  .playbackLevel = ScreenPlaybackLevel::none,
  .getColumnCount = columns,
  .drawStatic = drawStatic,
  .drawCursor = drawCursor,
  .drawSelection = NULL,
  .drawRowHeader = NULL,
  .drawColHeader = NULL,
  .drawField = drawField,
  .onEdit = onEdit,
  .onInput = NULL,
  .onRawInput = NULL,
  .isCellValid = NULL,
  .getLoopRange = NULL,
};
