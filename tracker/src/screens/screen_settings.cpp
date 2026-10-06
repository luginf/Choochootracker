#include "screen_settings.h"
#include "screen_keymapping.h"
#include "screen_midi.h"
#include "common.h"
#include "app.h"
#include "corelib_gfx.h"
#include "corelib_mainloop.h"
#include "screens.h"

static int columnCount(int) { return 1; }
static void setup(int) {}
static void draw(void) {}
static void drawStatic(void) { gfxSetFgColor(appSettings.colorScheme.textTitles); gfxPrint(0, 0, "SETTINGS"); }
static void drawCursor(int, int row) {
  if (row < 2) gfxCursor(23, 2 + row, 3);
  else if (row == 2) gfxCursor(23, 4, 6);
  else if (row < 8) { static const int widths[] = {4, 11, 6, 5, 8}; gfxCursor(0, 2 + row, widths[row - 3]); }
  else if (row == 9) gfxCursor(0, 18, 19);
}
static void noHeader(int, CellState) {}
static void drawField(int, int row, CellState state) {
  const ColorScheme cs = appSettings.colorScheme;
  gfxSetFgColor(cs.textDefault);
  if (row == 0) { gfxPrint(0, 2, "Repeat delay"); gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrintf(23, 2, "%03d", appSettings.keyRepeatDelay); }
  else if (row == 1) { gfxPrint(0, 3, "Repeat speed"); gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrintf(23, 3, "%03d", appSettings.keyRepeatSpeed); }
  else if (row == 2) { gfxPrint(0, 4, "Stick live mode"); gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(23, 4, appSettings.stickLiveMode == StickLiveMode::free ? "FREE  " : appSettings.stickLiveMode == StickLiveMode::toggle ? "TOGGLE" : "HOLD  "); }
  else if (row >= 3 && row <= 7) { static const char* labels[] = {"MIDI", "Key mapping", "Synths", "Mixer", "Graphics"}; gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(0, 2 + row, labels[row - 3]); }
  else if (row == 9) { gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(0, 18, "Quit ChooChooTracker"); }
}
static int onEdit(int, int row, CellEditAction action) {
  if (row == 0) { uint8_t value = appSettings.keyRepeatDelay; int handled = edit8noLast(action, &value, 4, 4, 30); if (handled) appSettings.keyRepeatDelay = value; return handled; }
  if (row == 1) { uint8_t value = appSettings.keyRepeatSpeed; int handled = edit8noLast(action, &value, 2, 1, 12); if (handled) appSettings.keyRepeatSpeed = value; return handled; }
  if (row == 2) { uint8_t value = (uint8_t)appSettings.stickLiveMode; int handled = edit8noLast(action, &value, 1, 0, 2); if (handled) appSetStickLiveMode((StickLiveMode)value); return handled; }
  if (action != CellEditAction::tap) return 0;
  if (row == 3) screenSetup(&screenMidi, 0);
  else if (row == 4) screenSetup(&screenKeyMapping, 0);
  else if (row == 5) screenSetup(&screenSynthSettings, 0);
  else if (row == 6) screenSetup(&screenMixerSettings, 0);
  else if (row == 7) screenSetup(&screenGraphicsSettings, 0);
  else if (row == 9) { mainLoopTriggerQuit(); return 1; }
  return 0;
}
static ScreenData data = {
  .rows = 10, .cursorRow = 0, .cursorCol = 0, .topRow = 0, .selectMode = -1,
  .selectStartRow = 0, .selectStartCol = 0, .selectAnchorRow = 0, .selectAnchorCol = 0,
  .playbackLevel = ScreenPlaybackLevel::none, .getColumnCount = columnCount,
  .drawStatic = drawStatic, .drawCursor = drawCursor, .drawSelection = NULL,
  .drawRowHeader = noHeader, .drawColHeader = noHeader, .drawField = drawField,
  .onEdit = onEdit, .onInput = NULL, .onRawInput = NULL, .isCellValid = NULL, .getLoopRange = NULL,
};
static void fullRedraw(void) { screenFullRedraw(&data); }
static int onInput(int isKeyDown, int keys, int taps) { if (keys == (keyUp | keyShift)) { screenSetup(&screenSong, 0); return 1; } return screenInput(&data, isKeyDown, keys, taps); }
static ScreenPlaybackLevel playbackLevel(void) { return ScreenPlaybackLevel::song; }
const AppScreen screenSettings = {NULL, setup, fullRedraw, draw, onInput, playbackLevel};
