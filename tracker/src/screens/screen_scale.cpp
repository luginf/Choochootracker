#include "screens.h"
#include "corelib_gfx.h"
#include "chipnomad_lib.h"
#include "common.h"
#include <cstring>

static const char* const noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static uint16_t activeMask() {
  Project* p = &chipnomadState->project;
  return p->scalePreset == scaleCustom ? p->scaleCustomMask : scalePresetMask(p->scalePreset);
}

static int noteBit(int note) {
  int bit = note - chipnomadState->project.scaleRoot;
  return bit < 0 ? bit + 12 : bit;
}

static void redraw(void);

static int columns(int row) {
  return row >= 2 && row <= 7 ? 3 : 1;
}

static void drawStatic(void) {
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  gfxPrint(0, 0, "SCALE / QUANTIZE");
  gfxSetFgColor(appSettings.colorScheme.textDefault);
  gfxPrint(0, 2, "Root");
  gfxPrint(0, 3, "Scale");
  gfxPrint(0, 5, "Tracks");
  gfxPrint(17, 5, "Notes");
}

static void drawCursor(int col, int row) {
  if (row == 0) gfxCursor(8, 2, 2);
  else if (row == 1) gfxCursor(8, 3, strlen(scalePresetName(chipnomadState->project.scalePreset)));
  else if (col == 0) gfxCursor(8, row + 4, 3);
  else if (col == 1) gfxCursor(20, row + 4, 3);
  else gfxCursor(31, row + 4, 3);
}

static void drawField(int col, int row, CellState state) {
  Project* p = &chipnomadState->project;
  gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
  if (row == 0) {
    gfxPrint(8, 2, noteNames[p->scaleRoot]);
  } else if (row == 1) {
    gfxClearRect(8, 3, 16, 1);
    gfxPrint(8, 3, scalePresetName(p->scalePreset));
  } else if (col == 0) {
    int track = row - 2;
    gfxPrintf(0, row + 4, "Track %d [%c]", track + 1, (p->scaleTracksMask & (1u << track)) ? 'x' : ' ');
  } else if (row <= 7) {
    int note = row - 2 + (col == 2 ? 6 : 0);
    uint16_t mask = activeMask();
    gfxPrintf(col == 1 ? 17 : 28, row + 4, "%2s [%c]", noteNames[note], (mask & (1u << noteBit(note))) ? 'x' : ' ');
  }
}

static int edit(int col, int row, CellEditAction action) {
  Project* p = &chipnomadState->project;
  int handled = 0;
  if (row == 0) {
    handled = edit8noLast(action, &p->scaleRoot, 1, 0, 11);
    if (handled) redraw();
  } else if (row == 1) {
    uint8_t preset = (uint8_t)p->scalePreset;
    handled = edit8noLast(action, &preset, 1, 0, scalePresetCount - 1);
    if (handled) p->scalePreset = (ScalePreset)preset;
    if (handled) redraw();
  } else if (col == 0) {
    int track = row - 2;
    if (action == CellEditAction::tap) {
      p->scaleTracksMask ^= 1u << track;
      handled = 1;
    } else {
      uint8_t enabled = (p->scaleTracksMask & (1u << track)) != 0;
      handled = edit8noLast(action, &enabled, 1, 0, 1);
      if (handled) {
        if (enabled) p->scaleTracksMask |= 1u << track;
        else p->scaleTracksMask &= ~(1u << track);
      }
    }
  } else if (row <= 7) {
    int note = row - 2 + (col == 2 ? 6 : 0);
    int bit = noteBit(note);
    uint8_t enabled = (activeMask() & (1u << bit)) != 0;
    if (action == CellEditAction::tap) { enabled ^= 1; handled = 1; }
    else handled = edit8noLast(action, &enabled, 1, 0, 1);
    if (handled) {
      if (p->scalePreset != scaleCustom) p->scaleCustomMask = activeMask();
      p->scalePreset = scaleCustom;
      if (enabled) p->scaleCustomMask |= 1u << bit;
      else p->scaleCustomMask &= ~(1u << bit);
      if (!p->scaleCustomMask) p->scaleCustomMask = 1;
      redraw();
    }
  }
  if (handled) {
    projectModified = 1;
    chipnomadQueuePlaybackScale(chipnomadState, p->scaleRoot, p->scalePreset);
  }
  return handled;
}

static void rowHeader(int row, CellState state) {}
static void colHeader(int col, CellState state) {}

static ScreenData data = {
  .rows = 10, .cursorRow = 0, .cursorCol = 0, .topRow = 0, .selectMode = -1,
  .selectStartRow = 0, .selectStartCol = 0, .selectAnchorRow = 0, .selectAnchorCol = 0,
  .playbackLevel = ScreenPlaybackLevel::none, .getColumnCount = columns,
  .drawStatic = drawStatic, .drawCursor = drawCursor, .drawSelection = NULL,
  .drawRowHeader = rowHeader, .drawColHeader = colHeader, .drawField = drawField,
  .onEdit = edit, .onInput = NULL, .onRawInput = NULL, .isCellValid = NULL, .getLoopRange = NULL,
};

static void setup(int input) {
  data.topRow = 0;
  if (data.cursorRow >= data.rows) {
    data.cursorRow = 0;
    data.cursorCol = 0;
  }
}
static void redraw(void) { screenFullRedraw(&data); }
static void fullRedraw(void) { screenFullRedraw(&data); }
static void draw(void) {}
static int onInput(int isKeyDown, int keys, int tapCount) {
  if (keys == keyOpt) { screenSetup(&screenProject, 0); return 1; }
  return screenInput(&data, isKeyDown, keys, tapCount);
}

const AppScreen screenScale = {
  .init = NULL, .setup = setup, .fullRedraw = fullRedraw, .draw = draw,
  .onInput = onInput,
  .getPlaybackLevel = []() { return ScreenPlaybackLevel::song; },
};
