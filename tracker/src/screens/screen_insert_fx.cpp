#include "chipnomad_lib.h"
#include "corelib_gfx.h"
#include "screens.h"
#include "selection_popup.h"
#include "utils.h"
#include "audio_manager.h"
#include <stdio.h>

static void fullRedraw();
static int onInput(int, int, int);
static int moduleButtonDown;
static int popupSlot;
static int displayedCpuLoad = -1;
static int columns(int) { return 2; }
static int y(int row) { return 3 + row + (row >= 5 ? 2 : 0); }
static int parameter(int col, int row) { return col * 4 + row % 5 - 1; }
static InsertConfig& config(int row) {
  return chipnomadState->project.trackInserts[*pSongTrack][row / 5];
}
static int valid(int col, int row) {
  return row >= 0 && row < 10 && col >= 0 && col < 2 &&
         (row % 5 == 0 || parameter(col, row) < insertDescriptor(config(row).module).count);
}
static void drawCpuLoad(int cpuLoad) {
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  gfxClearRect(31, 0, 9, 1);
  gfxPrintf(31, 0, "CPU %03d%%", cpuLoad);
}
static void drawStatic() {
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  gfxPrintf(0, 0, "TRACK %d - INSERT FX", *pSongTrack + 1);
  drawCpuLoad(audioManager.getCpuLoadPercent());
}
static void hint(int col, int row) {
  auto& c = config(row);
  int p = parameter(col, row);
  if (row % 5) {
    char decoded[32];
    insertDescribe(decoded, sizeof(decoded), c.module, p, c.values[p]);
    screenMessage(0, "F%d%d %s: %s", row / 5 + 1, p + 1,
                  insertDescriptor(c.module).parameters[p].name, decoded);
  } else
    screenMessage(0, "TF%d %s", row / 5 + 1,
                  col ? "Bypass retains settings" : "Select insert module");
}
static void cursor(int col, int row) {
  gfxCursor(row % 5 == 0 ? (col ? 27 : 4) : col * 17 + 12, y(row),
            row % 5 == 0 ? (col ? 3 : 11) : 2);
  hint(col, row);
}
static void field(int col, int row, CellState) {
  auto& c = config(row);
  gfxSetFgColor(appSettings.colorScheme.textDefault);
  int x = col * 17;
  gfxClearRect(x, y(row), 17, 1);
  if (row % 5 == 0) {
    if (!col)
      gfxPrintf(0, y(row), "TF%d %-11s", row / 5 + 1, insertDescriptor(c.module).name);
    else
      gfxPrintf(17, y(row), "Bypass    %s", c.bypass ? "ON " : "OFF");
  } else if (valid(col, row)) {
    int p = parameter(col, row);
    gfxPrintf(x, y(row), "%d %-8s", p + 1, insertDescriptor(c.module).parameters[p].label);
    gfxSetFgColor(appSettings.colorScheme.textValue);
    gfxPrintf(x + 12, y(row), "%02X", c.values[p]);
  }
}
static int edit(int col, int row, CellEditAction action) {
  auto& c = config(row);
  int handled = 0;
  if (row % 5 == 0) {
    uint8_t v = col ? c.bypass : c.module;
    handled = edit8noLast(action, &v, 1, 0, col ? 1 : insertModuleCount - 1);
    if (handled) {
      if (col)
        c.bypass = v;
      else if (c.module != v)
        insertSelect(&c, v);
    }
  } else {
    int p = parameter(col, row);
    uint8_t v = c.values[p];
    const auto& a = insertDescriptor(c.module).parameters[p];
    handled = edit8noLast(action, &v, a.mapping == InsertMapping::discrete ? 1 : 16, 0,
                          a.mapping == InsertMapping::discrete ? (int)a.maximum : 255);
    if (handled) insertEdit(&c, p, v);
  }
  if (handled) {
    projectModified = 1;
    fullRedraw();
  }
  return handled;
}
static void noHeader(int, CellState) {}
static ScreenData data = {.rows = 10,
                          .cursorRow = 0,
                          .cursorCol = 0,
                          .topRow = 0,
                          .selectMode = -1,
                          .selectStartRow = 0,
                          .selectStartCol = 0,
                          .selectAnchorRow = 0,
                          .selectAnchorCol = 0,
                          .playbackLevel = ScreenPlaybackLevel::phrase,
                          .getColumnCount = columns,
                          .drawStatic = drawStatic,
                          .drawCursor = cursor,
                          .drawSelection = nullptr,
                          .drawRowHeader = noHeader,
                          .drawColHeader = noHeader,
                          .drawField = field,
                          .onEdit = edit,
                          .onInput = onInput,
                          .onRawInput = nullptr,
                          .isCellValid = valid,
                          .getLoopRange = nullptr};
static void fullRedraw() {
  if (!valid(data.cursorCol, data.cursorRow)) {
    data.cursorRow = (data.cursorRow / 5) * 5;
    data.cursorCol = 0;
  }
  screenFullRedraw(&data);
}
static void selected(int module) {
  insertSelect(&chipnomadState->project.trackInserts[*pSongTrack][popupSlot], module);
  projectModified = 1;
  screenSetup(&screenInsertFX, -1);
}
static void cancelled() { screenSetup(&screenInsertFX, -1); }

static const SelectionItem dynamics[] = {
  {"Compressor", insertCompressor, nullptr, 0, nullptr},
  {"OTT", insertOTT, nullptr, 0, nullptr},
};
static const SelectionItem drive[] = {
  {"Distortion", insertDistortion, nullptr, 0, nullptr},
  {"Saturation", insertSaturation, nullptr, 0, nullptr},
  {"Bitcrusher", insertBitcrusher, nullptr, 0, nullptr},
  {"Destruction", insertDestruction, nullptr, 0, nullptr},
};
static const SelectionItem stereo[] = {
  {"Doubler", insertDoubler, nullptr, 0, nullptr},
  {"Chorus", insertChorus, nullptr, 0, nullptr},
  {"Flanger", insertFlanger, nullptr, 0, nullptr},
  {"Phaser", insertPhaser, nullptr, 0, nullptr},
  {"Rotary", insertRotary, nullptr, 0, nullptr},
};
static const SelectionItem tape[] = {
  {"TAPESCAM", insertTape, nullptr, 0, nullptr},
};
static const SelectionItem insertCategories[] = {
  {"OFF", insertOff, nullptr, 0, nullptr},
  {"Dynamics", -1, dynamics, (int)(sizeof(dynamics) / sizeof(dynamics[0])), nullptr},
  {"Drive", -1, drive, (int)(sizeof(drive) / sizeof(drive[0])), nullptr},
  {"Stereo", -1, stereo, (int)(sizeof(stereo) / sizeof(stereo[0])), nullptr},
  {"Tape", -1, tape, (int)(sizeof(tape) / sizeof(tape[0])), nullptr},
};
static int onInput(int down, int keys, int taps) {
  if (data.cursorRow % 5 == 0 && data.cursorCol == 0) {
    auto input = popupEditInput(down, keys, &moduleButtonDown);
    if (input == PopupEditInput::hold) return 1;
    if (input == PopupEditInput::cycle) {
      auto& c = config(data.cursorRow);
      insertSelect(&c,
                   (c.module + ((keys & keyLeft) ? insertModuleCount - 1 : 1)) % insertModuleCount);
      projectModified = 1;
      fullRedraw();
      return 1;
    }
    if (input == PopupEditInput::open) {
      popupSlot = data.cursorRow / 5;
      screenMessage(0, "");
      char popupTitle[32];
      snprintf(popupTitle, sizeof(popupTitle), "INSERT MODULE CPU %03d%%",
               audioManager.getCpuLoadPercent());
      selectionPopupSetup(popupTitle, insertCategories,
                          (int)(sizeof(insertCategories) / sizeof(insertCategories[0])),
                          config(data.cursorRow).module,
                          selected, cancelled, true);
      screenSetup(&screenSelectionPopup, 0);
      return 1;
    }
  }
  if (down) {
    if (keys == (keyShift | keyDown)) {
      screenMessage(0, "");
      screenSetup(&screenModulation, -1);
      return 1;
    }
    if (keys == (keyOpt | keyLeft) || keys == (keyOpt | keyRight)) {
      int t = *pSongTrack + (keys & keyRight ? 1 : -1);
      if (t >= 0 && t < chipnomadState->project.tracksCount) {
        *pSongTrack = t;
        fullRedraw();
      }
      return 1;
    }
  }
  return screenInput(&data, down, keys, taps);
}
static void init() {}
static void setup(int) {
  moduleButtonDown = 0;
  displayedCpuLoad = -1;
  if (*pSongTrack >= chipnomadState->project.tracksCount) *pSongTrack = 0;
}
static void draw() {
  // Releasing all buttons clears untimed app messages. Restore the selected
  // field's context here, leaving nonempty notices alone until they expire.
  if (!screenGetActiveMessage()[0]) hint(data.cursorCol, data.cursorRow);
  int cpuLoad = audioManager.getCpuLoadPercent();
  if (cpuLoad != displayedCpuLoad) {
    displayedCpuLoad = cpuLoad;
    drawCpuLoad(cpuLoad);
  }
}
static ScreenPlaybackLevel level() { return ScreenPlaybackLevel::phrase; }
const AppScreen screenInsertFX = {init, setup, fullRedraw, draw, onInput, level};
