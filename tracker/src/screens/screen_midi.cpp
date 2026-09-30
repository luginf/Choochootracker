#include "screen_midi.h"
#include "screen_settings.h"
#include "screen_midi_channel_map.h"
#include "common.h"
#include "corelib_gfx.h"
#include "corelib_input.h"
#include "screens.h"
#include "midi_io.h"
#include <string.h>

static int columnCount(int row) {
  return 1;
}

// isInput selects which port list to look up (input ports for MIDI In,
// output ports for MIDI Out). deviceIndex < 0, or out of range because a
// device was unplugged since it was selected, both show as "OFF".
static void midiDeviceLabel(int isInput, int deviceIndex, char* buffer, int bufferSize) {
  int count = isInput ? midiIoInputPortCount() : midiIoOutputPortCount();
  if (deviceIndex < 0 || deviceIndex >= count ||
      (isInput ? midiIoInputPortName(deviceIndex, buffer, bufferSize) : midiIoOutputPortName(deviceIndex, buffer, bufferSize)) != 0) {
    snprintf(buffer, bufferSize, "OFF");
  }
}

static void drawStatic(void) {
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  gfxPrint(0, 0, "MIDI");
}

static void drawCursor(int col, int row) {
  if ((row == 0 || row == 1) && col == 0) {
    gfxCursor(23, 2 + row, 20);
  } else if (row == 2 && col == 0) {
    gfxCursor(0, 4, 15);
  }
}

static void drawRowHeader(int row, CellState state) {
}

static void drawColHeader(int col, CellState state) {
}

static void drawField(int col, int row, CellState state) {
  if (row == 0 && col == 0) {
    gfxSetFgColor(appSettings.colorScheme.textDefault);
    gfxPrint(0, 2, "MIDI In");
    gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
    char name[24];
    midiDeviceLabel(1, appSettings.midiInputDevice, name, sizeof(name));
    gfxClearRect(23, 2, 24, 1);
    gfxPrint(23, 2, name);
  } else if (row == 1 && col == 0) {
    gfxSetFgColor(appSettings.colorScheme.textDefault);
    gfxPrint(0, 3, "MIDI Out");
    gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
    char name[24];
    midiDeviceLabel(0, appSettings.midiOutputDevice, name, sizeof(name));
    gfxClearRect(23, 3, 24, 1);
    gfxPrint(23, 3, name);
  } else if (row == 2 && col == 0) {
    gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
    gfxPrint(0, 5, "Channel mapping");
  }
}

static int onEdit(int col, int row, CellEditAction action) {
  if ((row == 0 || row == 1) && col == 0) {
    action = convertMultiAction(action);
    int direction = (action == CellEditAction::increase || action == CellEditAction::increaseBig) ? 1 :
                    (action == CellEditAction::decrease || action == CellEditAction::decreaseBig) ? -1 : 0;
    if (!direction) return 0;
    int isInput = row == 0;
    int count = isInput ? midiIoInputPortCount() : midiIoOutputPortCount();
    int* device = isInput ? &appSettings.midiInputDevice : &appSettings.midiOutputDevice;
    *device += direction;
    if (*device < -1) *device = count - 1;
    if (*device >= count) *device = -1;
    if (isInput) {
      if (*device < 0) midiIoCloseInput(); else if (midiIoOpenInput(*device) != 0) *device = -1;
    } else {
      if (*device < 0) midiIoCloseOutput(); else if (midiIoOpenOutput(*device) != 0) *device = -1;
    }
    return 1;
  } else if (row == 2 && col == 0 && action == CellEditAction::tap) {
    screenSetup(&screenMidiChannelMap, 0);
    return 0;
  }
  return 0;
}

static ScreenData screenMidiData = {
  .rows = 3,
  .cursorRow = 0,
  .cursorCol = 0,
  .topRow = 0,
  .selectMode = -1,
  .selectStartRow = 0,
  .selectStartCol = 0,
  .selectAnchorRow = 0,
  .selectAnchorCol = 0,
  .playbackLevel = ScreenPlaybackLevel::none,
  .getColumnCount = columnCount,
  .drawStatic = drawStatic,
  .drawCursor = drawCursor,
  .drawSelection = NULL,
  .drawRowHeader = drawRowHeader,
  .drawColHeader = drawColHeader,
  .drawField = drawField,
  .onEdit = onEdit,
  .onInput = NULL,
  .onRawInput = NULL,
  .isCellValid = NULL,
  .getLoopRange = NULL,
};

static void setup(int input) {
  screenMidiData.cursorRow = 0;
  screenMidiData.cursorCol = 0;
}

static void fullRedraw(void) {
  screenFullRedraw(&screenMidiData);
}

static void draw(void) {
}

static int inputScreenNavigation(int keys, int tapCount) {
  if (keys == keyOpt) {
    screenSetup(&screenSettings, 0);
    return 1;
  }
  return 0;
}

static int onInput(int isKeyDown, int keys, int tapCount) {
  if (inputScreenNavigation(keys, tapCount)) return 1;
  return screenInput(&screenMidiData, isKeyDown, keys, tapCount);
}

const AppScreen screenMidi = {
  .init = NULL,
  .setup = setup,
  .fullRedraw = fullRedraw,
  .draw = draw,
  .onInput = onInput,
  .getPlaybackLevel = NULL,
};
