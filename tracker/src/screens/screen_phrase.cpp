#include "screens.h"
#include "common.h"
#include "corelib_gfx.h"
#include "utils.h"
#include "chipnomad_lib.h"
#include "project_utils.h"
#include "copy_paste.h"
#include "help.h"

static int phraseIdx = 0;
static PhraseRow *phraseRows = NULL;
static int isFxEdit = 0;

static uint8_t lastNote = 48;
static uint8_t lastInstrument = 0;

static uint8_t lastVolume = 15;
static uint8_t lastFX[2] = {0, 0};

static int getColumnCount(int row);
static void drawStatic(void);
static void drawField(int col, int row, CellState state);
static void drawRowHeader(int row, CellState state);
static void drawColHeader(int col, CellState state);
static void drawCursor(int col, int row);
static void drawSelection(int col1, int row1, int col2, int row2);
static int onEdit(int col, int row, CellEditAction action);
static LoopRange getLoopRange(void);

static int columnX[] = {3, 7, 10, 13, 16, 19, 22, 25, 28, 31};

static ScreenData screen = {
  .rows = 16,
  .cursorRow = 0,
  .cursorCol = 0,
  .topRow = 0,
  .selectMode = 0,
  .selectStartRow = 0,
  .selectStartCol = 0,
  .selectAnchorRow = 0,
  .selectAnchorCol = 0,
  .playbackLevel = ScreenPlaybackLevel::none,
  .getColumnCount = getColumnCount,
  .drawStatic = drawStatic,
  .drawCursor = drawCursor,
  .drawSelection = drawSelection,
  .drawRowHeader = drawRowHeader,
  .drawColHeader = drawColHeader,
  .drawField = drawField,
  .onEdit = onEdit,
  .onInput = NULL,
  .onRawInput = NULL,
  .isCellValid = NULL,
  .getLoopRange = getLoopRange,
};

static void init(void) {
  lastNote = 48;
  lastInstrument = 0;
  lastVolume = 15;
  lastFX[0] = 0;
  lastFX[1] = 0;
  screen.cursorRow = 0;
  screen.cursorCol = 0;
  screen.topRow = 0;
  screen.selectMode = 0;
  screen.selectStartRow = 0;
  screen.selectStartCol = 0;
  screen.selectAnchorRow = 0;
  screen.selectAnchorCol = 0;
  isFxEdit = 0;
}

static void setup(int input) {
  phraseIdx = chipnomadState->project.chains[chipnomadState->project.song[*pSongRow][*pSongTrack]].rows[*pChainRow].phrase;
  phraseRows = chipnomadState->project.phrases[phraseIdx].rows;
  screen.selectMode = 0;
  isFxEdit = 0;
}

///////////////////////////////////////////////////////////////////////////////
//
// Drawing functions
//

static int getColumnCount(int row) {
  return 9;
}

static void drawStatic(void) {
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  gfxPrintf(0, 0, "PHRASE %03X%c", phraseIdx, isPhraseUsedElsewhere(&chipnomadState->project, phraseIdx, chipnomadState->project.song[*pSongRow][*pSongTrack], *pChainRow) ? '*' : ' ');
}

static void fullRedraw(void) {
  screenFullRedraw(&screen);
}

static void drawField(int col, int row, CellState state) {
  int x = columnX[col];
  int y = 3 + row;
  if (col == 0) {
    // Note
    uint8_t value = phraseRows[row].note;
    setCellColor(state, value == EMPTY_VALUE_8, 1);
    gfxPrint(x, y, noteName(&chipnomadState->project, value));
  } else if (col == 1 || col == 2) {
    // Instrument and volume
    uint8_t value = (col == 1) ? phraseRows[row].instrument : phraseRows[row].volume;
    setCellColor(state, value == EMPTY_VALUE_8, 1);
    gfxPrint(x, y, byteToHexOrEmpty(value));
  } else if (col == 3 || col == 5 || col == 7) {
    // FX name
    uint8_t fx = phraseRows[row].fx[(col - 3) / 2][0];
    setCellColor(state, fx == EMPTY_VALUE_8, 1);
    gfxPrint(x, y, fxNames[fx].name);
  } else if (col == 4 || col == 6 || col == 8) {
    // FX value
    uint8_t value = phraseRows[row].fx[(col - 4) / 2][1];
    setCellColor(state, 0, phraseRows[row].fx[(col - 3) / 2][0] != EMPTY_VALUE_8);
    gfxPrint(x, y, byteToHex(value));
  }
}

static void drawRowHeader(int row, CellState state) {
  const ColorScheme cs = appSettings.colorScheme;
  gfxSetFgColor((state == CellState::focus) ? cs.textDefault : ((row & 3) == 0 ? cs.textValue : cs.textInfo));
  gfxPrintf(1, 3 + row, "%X", row);
}

static void drawColHeader(int col, CellState state) {
  const ColorScheme cs = appSettings.colorScheme;
  gfxSetFgColor((state == CellState::focus) ? cs.textDefault : cs.textInfo);

  switch (col) {
    case 0:
      gfxPrint(3, 2, "N");
      break;
    case 1:
      gfxPrint(7, 2, "I");
      break;
    case 2:
      gfxPrint(10, 2, "V");
      break;
    case 3:
    case 4:
      gfxPrint(13, 2, "FX1");
      break;
    case 5:
    case 6:
      gfxPrint(19, 2, "FX2");
      break;
    case 7:
    case 8:
      gfxPrint(25, 2, "FX3");
      break;
    default:
      break;
  }
}

static void drawCursor(int col, int row) {
  int width = 2;
  if (col == 0 || col == 3 || col == 5 || col == 7) width = 3;
  gfxCursor(col == 0 ? 3 : 4 + col * 3, 3 + row, width);
}

static void drawSelection(int col1, int row1, int col2, int row2) {
  int x = columnX[col1];
  int w = columnX[col2 + 1] - x - 1;
  int y = 3 + row1;
  int h = row2 - row1 + 1;
  if (col2 == 3 || col2 == 5 || col2 == 7) w++;
  gfxRect(x, y, w, h);
}

static void draw(void) {
  if (isFxEdit) return;

  gfxClearRect(0, 3, 1, 16);
  gfxSetFgColor(appSettings.colorScheme.textInfo);
  gfxPrint(0, 3 + *pChainRow, "<");

  gfxClearRect(2, 3, 1, 16);
  const PlaybackTrackState* track = &chipnomadGetPlaybackStatus(chipnomadState)->tracks[*pSongTrack];
  if (track->mode != PlaybackMode::stopped && track->mode != PlaybackMode::phraseRow && track->songRow != EMPTY_VALUE_16) {
    // Chain row
    if (*pSongRow == track->songRow) {
      gfxSetFgColor(appSettings.colorScheme.playMarkers);
      gfxPrint(0, 3 + track->chainRow, "<");
    }

    // Phrase row
    int playingPhrase = chipnomadState->project.chains[chipnomadState->project.song[track->songRow][*pSongTrack]].rows[track->chainRow].phrase;
    if (playingPhrase == phraseIdx) {
      int row = track->phraseRow;
      if (row >= 0 && row < 16) {
        gfxSetFgColor(appSettings.colorScheme.playMarkers);
        gfxPrint(2, 3 + row, ">");
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Input handling
//

// Preview a row's note through the audio engine, same as any note edit does.
static void triggerRowPreview(int row) {
  if (chipnomadGetPlaybackStatus(chipnomadState)->isPlaying &&
      chipnomadGetPlaybackStatus(chipnomadState)->tracks[*pSongTrack].mode != PlaybackMode::phraseRow) {
    return;
  }
  PhraseRow* previewSource = &phraseRows[row];
  if (previewSource->note != EMPTY_VALUE_8 && previewSource->note != NOTE_OFF && previewSource->instrument == EMPTY_VALUE_8) {
    PhraseRow previewRow = *previewSource;
    previewRow.instrument = lookupInstrument(&chipnomadState->project, *pSongRow, *pChainRow, row, *pSongTrack);
    chipnomadQueuePlaybackStartPhraseRow(chipnomadState, *pSongTrack, &previewRow);
  } else {
    chipnomadQueuePlaybackStartPhraseRow(chipnomadState, *pSongTrack, previewSource);
  }
}

static int editCell(int col, int row, CellEditAction action) {
  int handled = 0;
  uint8_t maxVolume = 15;

  if (col == 0) {
    // Note
    if (action == CellEditAction::clear && phraseRows[row].note == EMPTY_VALUE_8) {
      // Insert OFF
      phraseRows[row].note = NOTE_OFF;
      phraseRows[row].instrument = EMPTY_VALUE_8;
      phraseRows[row].volume = EMPTY_VALUE_8;
      handled = 1;
    } else if (action == CellEditAction::clear) {
      // Clear note
      handled = edit8withLimit(action, &phraseRows[row].note, &lastNote, chipnomadState->project.pitchTable.octaveSize, chipnomadState->project.pitchTable.length - 1);
      edit8withLimit(action, &phraseRows[row].instrument, &lastInstrument, 16, PROJECT_MAX_INSTRUMENTS - 1);
      edit8withLimit(action, &phraseRows[row].volume, &lastVolume, 16, maxVolume);
    } else if (action == CellEditAction::tap && phraseRows[row].note == EMPTY_VALUE_8) {
      phraseRows[row].note = lastNote;
      phraseRows[row].instrument = lastInstrument;
      phraseRows[row].volume = lastVolume;
      handled = 1;
    } else if (phraseRows[row].note != NOTE_OFF) {
      handled = edit8withLimit(action, &phraseRows[row].note, &lastNote, chipnomadState->project.pitchTable.octaveSize, chipnomadState->project.pitchTable.length - 1);
      if (handled) {
        if (phraseRows[row].instrument != EMPTY_VALUE_8) lastInstrument = phraseRows[row].instrument;
        if (phraseRows[row].volume != EMPTY_VALUE_8) lastVolume = phraseRows[row].volume;
      }
    }
    if (handled) {
      drawField(1, row, CellState::normal);
      drawField(2, row, CellState::normal);
    }
  } else if (col == 1) {
    // Instrument
    if (action == CellEditAction::doubleTap) {
      uint8_t nextInstrument = findEmptyInstrument(&chipnomadState->project, 0);
      if (nextInstrument != EMPTY_VALUE_8) {
        phraseRows[row].instrument = nextInstrument;
        handled = 1;
      }
    } else {
      handled = edit8withLimit(action, &phraseRows[row].instrument, &lastInstrument, 16, PROJECT_MAX_INSTRUMENTS - 1);
    }
    uint8_t instrument = phraseRows[row].instrument;
    if (handled && instrument != EMPTY_VALUE_8) {
      screenMessage(0, "%s: %s", byteToHex(instrument), instrumentName(&chipnomadState->project, instrument));
    }
  } else if (col == 2) {
    // Volume
    handled = edit8withLimit(action, &phraseRows[row].volume, &lastVolume, 16, maxVolume);
  } else if (col == 3 || col == 5 || col == 7) {
    // FX
    int fxIdx = (col - 3) / 2;
    // Get instrument number from current phrase row or traverse back
    uint8_t instrumentNum = lookupInstrument(&chipnomadState->project, *pSongRow, *pChainRow, row, *pSongTrack);
    int result = editFX(action, phraseRows[row].fx[fxIdx], lastFX, 0, instrumentNum);
    if (result == 2) {
      drawField(col + 1, row, CellState::normal);
      handled = 1;
    } else if (result == 1) {
      isFxEdit = 1;
      handled = 0;
    }
  } else if (col == 4 || col == 6 || col == 8) {
    // FX value
    int fxIdx = (col - 4) / 2;
    if (phraseRows[row].fx[fxIdx][0] != EMPTY_VALUE_8) {
      uint8_t instrumentNum = lookupInstrument(&chipnomadState->project, *pSongRow, *pChainRow, row, *pSongTrack);
      handled = editFXValue(action, phraseRows[row].fx[fxIdx], lastFX, 0, instrumentNum);
    }
  }

  if (handled) triggerRowPreview(screen.cursorRow);

  return handled;
}

static int onEdit(int col, int row, CellEditAction action) {
  int handled = 0;

  int startCol, startRow, endCol, endRow;
  getSelectionBounds(&screen, &startCol, &startRow, &endCol, &endRow);

  if (action == CellEditAction::switchSelection) {
    return switchPhraseSelectionMode(&screen);
  } else if (action == CellEditAction::multiIncrease || action == CellEditAction::multiDecrease) {
    if (!isSingleColumnSelection(&screen)) return 0;
    handled = applyMultiEdit(startCol, startRow, endCol, endRow, action, editCell);
  } else if (action == CellEditAction::multiIncreaseBig || action == CellEditAction::multiDecreaseBig) {
    // Check if full width selection (all columns)
    if (startCol == 0 && endCol == 8) {
      // Rotation mode
      int direction = (action == CellEditAction::multiIncreaseBig) ? -1 : 1;
      applyPhraseRotation(phraseIdx, startRow, endRow, direction);
      fullRedraw();
      handled = 1;
    } else if (isSingleColumnSelection(&screen)) {
      // Single column: big increase/decrease or FX selection
      if (startCol == 3 || startCol == 5 || startCol == 7) {
        // FX type column: show FX selection
        int fxIdx = (startCol - 3) / 2;
        // Get instrument index from current phrase row or traverse back
        uint8_t instrumentNum = lookupInstrument(&chipnomadState->project, *pSongRow, *pChainRow, screen.cursorRow, *pSongTrack);
        fxEditFullDraw(phraseRows[screen.cursorRow].fx[fxIdx][0], instrumentNum, 0);
        isFxEdit = 1;
      } else {
        // Regular big increase/decrease for note, volume, instrument, FX value
        handled = applyMultiEdit(startCol, startRow, endCol, endRow, action, editCell);
      }
    }
  } else if (action == CellEditAction::copy) {
    copyPhrase(phraseIdx, startCol, startRow, endCol, endRow, 0);
    handled = 1;
  } else if (action == CellEditAction::cut) {
    copyPhrase(phraseIdx, startCol, startRow, endCol, endRow, 1);
    handled = 1;
  } else if (action == CellEditAction::paste) {
    const int rowsPasted = pastePhrase(phraseIdx, col, row);
    if (rowsPasted > 0) {
      // Move cursor below pasted data, or to last row if paste extends to end
      int newRow = row + rowsPasted;
      if (newRow > 15) newRow = 15;
      screen.cursorRow = newRow;
    }
    fullRedraw();
    handled = 1;
  } else if (action == CellEditAction::shallowClone) {
    // Handle instrument column cloning
    if (startCol == 1 && endCol == 1) {
      int distinctCount = cloneInstrumentsInPhrase(phraseIdx, startRow, endRow);
      if (distinctCount == 0) {
        screenMessage(MESSAGE_TIME, "No empty instruments");
      }
      screenMessage(MESSAGE_TIME, "Cloned %d instrument%s", distinctCount, distinctCount == 1 ? "" : "s");
      handled = 1;
    }
  } else {
    handled = editCell(col, row, action);
  }

  if (handled) projectModified = 1;
  return handled;
}

static int inputScreenNavigation(int keys, int tapCount) {
  if (keys == (keyRight | keyShift)) {
    // To Instrument/Phrase screen
    int table = -1;
    if (screen.cursorCol > 2) {
      // If we currently on the table command, go to this table
      int fxIdx = (screen.cursorCol - 3) / 2;
      uint8_t fxType = phraseRows[screen.cursorRow].fx[fxIdx][0];
      uint8_t fxValue = phraseRows[screen.cursorRow].fx[fxIdx][1];
      if ((fxType == fxTBL || fxType == fxTBX) && fxValue != 0xff) {
        table = fxValue;
      }
    }

    if (table >= 0) {
      screenSetup(&screenTable, table | 0x1000);
    } else {
      int instrument = 0;
      for (int row = screen.cursorRow; row >= 0; row--) {
        if (phraseRows[row].instrument != EMPTY_VALUE_8) {
          instrument = phraseRows[row].instrument;
          break;
        }
      }
      screenSetup(&screenInstrument, instrument);
    }
    return 1;
  } else if (keys == (keyLeft | keyShift)) {
    // To Chain screen
    screenSetup(&screenChain, -1);
    return 1;
  } else if (keys == (keyUp | keyShift)) {
    // To Groove screen
    int groove = 0;
    if (screen.cursorCol > 2) {
      // If we currently on the groove command, go to this groove
      int fxIdx = (screen.cursorCol - 3) / 2;
      uint8_t fxType = phraseRows[screen.cursorRow].fx[fxIdx][0];
      if (fxType == fxGRV || fxType == fxGGR) {
        groove = phraseRows[screen.cursorRow].fx[fxIdx][1] & (PROJECT_MAX_GROOVES - 1);
      }
    }
    screenSetup(&screenGroove, groove);
    return 1;
  } else if (keys == (keyLeft | keyOpt)) {
    // Previous track
    if (*pSongTrack == 0) return 1;
    uint16_t chain = chipnomadState->project.song[*pSongRow][*pSongTrack - 1];
    if (chain != EMPTY_VALUE_16 && !chainIsEmpty(&chipnomadState->project, chain)) {
      *pSongTrack -= 1;
      while (chipnomadState->project.chains[chain].rows[*pChainRow].phrase == EMPTY_VALUE_16) {
        *pChainRow -= 1;
        if (*pChainRow == 0) break;
      }
      setup(-1);
      fullRedraw();
    }
    return 1;
  } else if (keys == (keyRight | keyOpt)) {
    // Next track
    if (*pSongTrack == chipnomadState->project.tracksCount - 1) return 1;
    uint16_t chain = chipnomadState->project.song[*pSongRow][*pSongTrack + 1];
    if (chain != EMPTY_VALUE_16 && !chainIsEmpty(&chipnomadState->project, chain)) {
      *pSongTrack += 1;
      while (chipnomadState->project.chains[chain].rows[*pChainRow].phrase == EMPTY_VALUE_16) {
        *pChainRow -= 1;
        if (*pChainRow == 0) break;
      }
      setup(-1);
      fullRedraw();
    }
    return 1;
  } else if ((keys == (keyUp | keyOpt)) || (keys == keyUp && screen.cursorRow == 0)) {
    // Previous phrase in the chain
    if (*pChainRow == 0) return 1;
    if (chipnomadState->project.chains[chipnomadState->project.song[*pSongRow][*pSongTrack]].rows[*pChainRow - 1].phrase != EMPTY_VALUE_16) {
      *pChainRow -= 1;
      if (keys == keyUp) screen.cursorRow = 15;
      setup(-1);
      chipnomadQueuePlaybackQueuePhrase(chipnomadState, *pSongTrack, *pSongRow, *pChainRow);
      fullRedraw();
    }
    return 1;
  } else if (keys == (keyDown | keyOpt) || (keys == keyDown && screen.cursorRow == 15)) {
    // Next phrase in the chain
    if (*pChainRow == 15) return 1;
    if (chipnomadState->project.chains[chipnomadState->project.song[*pSongRow][*pSongTrack]].rows[*pChainRow + 1].phrase != EMPTY_VALUE_16) {
      *pChainRow += 1;
      if (keys == keyDown) screen.cursorRow = 0;
      setup(-1);
      chipnomadQueuePlaybackQueuePhrase(chipnomadState, *pSongTrack, *pSongRow, *pChainRow);
      fullRedraw();
    }
    return 1;
  }
  return 0;
}

static int onInput(int isKeyDown, int keys, int tapCount) {
  if (isFxEdit) {
    int fxIdx = (screen.cursorCol - 3) / 2;
    int result = fxEditInput(keys, tapCount, phraseRows[screen.cursorRow].fx[fxIdx], lastFX);
    if (result) {
      isFxEdit = 0;

      // If in selection mode and on FX type column, fill selection with selected FX
      if (screen.selectMode == 1 && (screen.cursorCol == 3 || screen.cursorCol == 5 || screen.cursorCol == 7)) {
        int startCol, startRow, endCol, endRow;
        getSelectionBounds(&screen, &startCol, &startRow, &endCol, &endRow);

        if (isSingleColumnSelection(&screen)) {
          uint8_t selectedFX = phraseRows[screen.cursorRow].fx[fxIdx][0];
          for (int r = startRow; r <= endRow; r++) {
            phraseRows[r].fx[fxIdx][0] = selectedFX;
          }
        }
      }

      fullRedraw();
    }
    return 1;
  }

  if (screen.selectMode == 0 && inputScreenNavigation(keys, tapCount)) return 1;
  return screenInput(&screen, isKeyDown, keys, tapCount);
}

static LoopRange getLoopRange(void) {
  LoopRange range = {0};
  if (screen.selectMode == 1) {
    int startCol, startRow, endCol, endRow;
    getSelectionBounds(&screen, &startCol, &startRow, &endCol, &endRow);
    range.enabled = 1;
    range.level = 2;
    range.startSongRow = *pSongRow;
    range.startChainRow = *pChainRow;
    range.startPhraseRow = startRow;
    range.endSongRow = *pSongRow;
    range.endChainRow = *pChainRow;
    range.endPhraseRow = endRow;
  }
  return range;
}

static ScreenPlaybackLevel getPlaybackLevel(void) {
  return ScreenPlaybackLevel::phrase;
}

///////////////////////////////////////////////////////////////////////////////
//
// Key jazz (desktop only): type notes directly on the QWERTY keyboard,
// like m8c (https://github.com/laamaa/m8c). Toggled with Esc. While active,
// this takes over the note keys entirely (they overlap with Edit/Opt/Motion
// on this screen), so Esc again is needed to get those back.
//

#ifdef DESKTOP_BUILD

static int keyJazzEnabled = 0;
static uint8_t keyJazzBaseNote = 48;

static uint8_t keyJazzClampNote(int note) {
  int maxNote = chipnomadState->project.pitchTable.length - 1;
  if (note < 0) return 0;
  if (note > maxNote) return (uint8_t)maxNote;
  return (uint8_t)note;
}

// The selection's column/row bounds if one is active, else the note+
// instrument+volume "bundle" (columns 0-2) at the cursor row - copy/cut
// treat a single note as those 3 columns together, matching how typing
// and Delete/Backspace already fill/clear them as one unit.
static void keyJazzGetActiveRange(int* startCol, int* startRow, int* endCol, int* endRow) {
  if (screen.selectMode) {
    getSelectionBounds(&screen, startCol, startRow, endCol, endRow);
  } else {
    *startCol = 0;
    *endCol = 2;
    *startRow = *endRow = screen.cursorRow;
  }
}

static void keyJazzClearColumn(int row, int col) {
  if (col == 0) phraseRows[row].note = EMPTY_VALUE_8;
  else if (col == 1) phraseRows[row].instrument = EMPTY_VALUE_8;
  else if (col == 2) phraseRows[row].volume = EMPTY_VALUE_8;
  // FX columns are out of scope for key jazz.
}

static void keyJazzSetColumn(int row, int col, uint8_t value) {
  if (col == 0) phraseRows[row].note = value;
  else if (col == 1) phraseRows[row].instrument = value;
  else if (col == 2) phraseRows[row].volume = value;
}

static uint8_t keyJazzGetColumn(int row, int col) {
  if (col == 0) return phraseRows[row].note;
  if (col == 1) return phraseRows[row].instrument;
  if (col == 2) return phraseRows[row].volume;
  return EMPTY_VALUE_8;
}

// Removes one column's value at startRow and shifts the rows below it (in
// that same column only) up to fill the gap, clearing the last row.
// Mirrors what Delete does for a whole row, scoped to a single column.
static void keyJazzShiftColumnUp(int col, int startRow, int count) {
  for (int r = startRow; r <= 15 - count; r++) keyJazzSetColumn(r, col, keyJazzGetColumn(r + count, col));
  for (int r = 16 - count; r <= 15; r++) keyJazzClearColumn(r, col);
}

static void keyJazzClearRow(int row, int includeFx) {
  phraseRows[row].note = EMPTY_VALUE_8;
  phraseRows[row].instrument = EMPTY_VALUE_8;
  phraseRows[row].volume = EMPTY_VALUE_8;
  if (includeFx) {
    for (int i = 0; i < 3; i++) {
      phraseRows[row].fx[i][0] = EMPTY_VALUE_8;
      phraseRows[row].fx[i][1] = 0;
    }
  }
}

int phraseKeyJazzHandleRawKey(InputCode input, int isDown) {
  if (input.deviceType != InputDeviceType::keyboard) return 0;

  if (inputIsKeyJazzToggle(input)) {
    if (isDown && !isFxEdit) {
      keyJazzEnabled = !keyJazzEnabled;
      if (keyJazzEnabled) {
        uint8_t currentNote = phraseRows[screen.cursorRow].note;
        uint16_t octaveSize = chipnomadState->project.pitchTable.octaveSize;
        uint8_t reference = (currentNote != EMPTY_VALUE_8 && currentNote != NOTE_OFF) ? currentNote : lastNote;
        keyJazzBaseNote = octaveSize > 0 ? (reference / octaveSize) * octaveSize : reference;
        screenMessage(MESSAGE_TIME, "KEY JAZZ ON (Esc to exit)");
      } else {
        screen.selectMode = 0;
        screenMessage(MESSAGE_TIME, "KEY JAZZ OFF");
      }
      fullRedraw();
    }
    return 1;
  }

  if (!keyJazzEnabled) return 0;

  if (inputIsShiftKey(input)) return 1; // Swallow: see inputIsShiftKey's doc comment

  int arrowDir = inputArrowKeyDirection(input);
  if (arrowDir != 0) {
    if (inputIsShiftHeld()) {
      if (isDown && !screen.selectMode) {
        screen.selectStartRow = screen.cursorRow;
        screen.selectStartCol = screen.cursorCol;
        screen.selectAnchorRow = screen.cursorRow;
        screen.selectAnchorCol = screen.cursorCol;
        screen.selectMode = 1;
      }
    } else if (screen.selectMode) {
      // A plain arrow (Shift released) collapses the selection, like a
      // regular text editor, instead of silently continuing to extend it.
      if (isDown) {
        screen.selectMode = 0;
        fullRedraw();
      }
    }
    return 0; // Let normal cursor movement happen (and extend/render the selection)
  }

  if (inputIsCtrlHeld()) {
    if (inputIsSaveKey(input)) {
      if (isDown) {
        projectSave(&chipnomadState->project, getAutosavePath());
        screenMessage(MESSAGE_TIME, "KEY JAZZ: project saved");
      }
      return 1;
    }
    if (inputIsCopyKey(input) || inputIsCutKey(input)) {
      if (isDown) {
        int startCol, startRow, endCol, endRow;
        keyJazzGetActiveRange(&startCol, &startRow, &endCol, &endRow);
        int isCut = inputIsCutKey(input);
        copyPhrase(phraseIdx, startCol, startRow, endCol, endRow, isCut);
        int count = endRow - startRow + 1;
        screenMessage(MESSAGE_TIME, "KEY JAZZ: %s %d row%s", isCut ? "cut" : "copied", count, count == 1 ? "" : "s");
        if (isCut) {
          screen.selectMode = 0;
          fullRedraw();
        }
      }
      return 1;
    }
    if (inputIsPasteKey(input)) {
      if (isDown) {
        int rowsPasted = pastePhrase(phraseIdx, screen.cursorCol, screen.cursorRow);
        if (rowsPasted > 0) {
          screenMessage(MESSAGE_TIME, "KEY JAZZ: pasted %d row%s", rowsPasted, rowsPasted == 1 ? "" : "s");
          fullRedraw();
        }
      }
      return 1;
    }
    return 0; // Other Ctrl+key combos: not our concern
  }

  if (inputIsDeleteKey(input)) {
    // Delete removes the whole row(s) (every column, not just the
    // selection's columns) and shifts the rest of the phrase up to fill
    // the gap; the cursor stays on the same row index.
    if (isDown) {
      int startRow, endRow;
      if (screen.selectMode) {
        int startCol, endCol;
        getSelectionBounds(&screen, &startCol, &startRow, &endCol, &endRow);
      } else {
        startRow = endRow = screen.cursorRow;
      }
      int count = endRow - startRow + 1;
      for (int r = startRow; r <= 15 - count; r++) phraseRows[r] = phraseRows[r + count];
      for (int r = 16 - count; r <= 15; r++) keyJazzClearRow(r, 1);
      screen.cursorRow = startRow;
      screen.selectMode = 0;
      fullRedraw();
    }
    return 1;
  }

  if (inputIsBackspaceKey(input)) {
    // Backspace is narrower than Delete: it only touches the current
    // column (or the selection's actual columns), not the whole row. It
    // removes the element(s) at the cursor/selection itself (not the row
    // above) and shifts whatever is below, in that same column, up to
    // fill the gap - the column equivalent of what Delete does per row.
    if (isDown) {
      int startCol, startRow, endCol, endRow;
      if (screen.selectMode) {
        getSelectionBounds(&screen, &startCol, &startRow, &endCol, &endRow);
      } else {
        startCol = endCol = screen.cursorCol;
        startRow = endRow = screen.cursorRow;
      }
      int count = endRow - startRow + 1;
      for (int c = startCol; c <= endCol; c++) keyJazzShiftColumnUp(c, startRow, count);
      screen.cursorRow = startRow;
      screen.selectMode = 0;
      fullRedraw();
    }
    return 1;
  }

  if (inputIsInsertKey(input)) {
    if (isDown) {
      int row = screen.cursorRow;
      if (row < 15) applyPhraseRotation(phraseIdx, row, 15, 1);
      keyJazzClearRow(row, 1);
      fullRedraw();
    }
    return 1;
  }

  int octaveDelta = inputKeyJazzOctaveDelta(input);
  if (octaveDelta != 0) {
    if (isDown) {
      uint16_t octaveSize = chipnomadState->project.pitchTable.octaveSize;
      keyJazzBaseNote = keyJazzClampNote(keyJazzBaseNote + octaveDelta * (int)octaveSize);
      screenMessage(MESSAGE_TIME, "KEY JAZZ octave: %s", chipnomadState->project.pitchTable.noteNames[keyJazzBaseNote]);
    }
    return 1;
  }

  int offset = inputKeyJazzNoteOffset(input);
  if (offset < 0) return 0; // Not a note key: let normal input handle it (arrows, Shift, Play...)

  if (isDown) {
    int row = screen.cursorRow;
    phraseRows[row].note = keyJazzClampNote(keyJazzBaseNote + offset);
    if (phraseRows[row].instrument == EMPTY_VALUE_8) phraseRows[row].instrument = lastInstrument;
    if (phraseRows[row].volume == EMPTY_VALUE_8) phraseRows[row].volume = lastVolume;
    lastNote = phraseRows[row].note;
    triggerRowPreview(row);
    drawField(0, row, CellState::normal);
    drawField(1, row, CellState::normal);
    drawField(2, row, CellState::normal);
    if (row < 15) {
      screen.cursorRow = row + 1;
      fullRedraw();
    }
  }
  return 1;
}

#endif // DESKTOP_BUILD

const AppScreen screenPhrase = {
  .init = init,
  .setup = setup,
  .fullRedraw = fullRedraw,
  .draw = draw,
  .onInput = onInput,
  .getPlaybackLevel = getPlaybackLevel
};

LoopRange phraseScreenGetLoopRange(void) {
  return getLoopRange();
}
