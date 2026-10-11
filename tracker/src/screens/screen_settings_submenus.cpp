#include "screen_color_theme.h"
#include "app.h"
#include "corelib_file.h"
#include "corelib_font.h"
#include "corelib_gfx.h"
#include "file_browser.h"
#include "screens.h"
#include "waveform_display.h"
#include <string.h>

static int columnCount(int) { return 1; }
static void noHeader(int, CellState) {}
static void setup(int) {}
static void draw(void) {}

static void synthStatic(void) { gfxSetFgColor(appSettings.colorScheme.textTitles); gfxPrint(0, 0, "SYNTHS"); }
static void synthCursor(int, int row) { gfxCursor(23, 2 + row, row == 0 ? 6 : row == 1 ? 3 : row == 2 ? 6 : 4); }
static void synthField(int, int row, CellState state) {
  const ColorScheme cs = appSettings.colorScheme;
  gfxSetFgColor(cs.textDefault);
  if (row == 0) {
    static const char* names[] = {"LOW   ", "MEDIUM", "HIGH  ", "BEST  "};
    gfxPrint(0, 2, "AY Quality"); gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(23, 2, names[appSettings.quality]);
  } else if (row == 1) {
    gfxPrint(0, 3, "AY Sample dithering"); gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(23, 3, appSettings.aySampleDithering ? "ON " : "OFF");
  } else {
    static const char* labels[] = {"Braids BITS", "Braids DRFT", "Braids SIGN"};
    static const char* bits[] = {"2 BIT ", "3 BIT ", "4 BIT ", "6 BIT ", "8 BIT ", "12 BIT", "16 BIT"};
    static const char* levels[] = {"OFF ", "LOW ", "MED ", "HIGH", "MAX "};
    gfxPrint(0, 2 + row, labels[row - 2]); gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault);
    gfxPrint(23, 2 + row, row == 2 ? bits[appSettings.braidsBits] : levels[row == 3 ? appSettings.braidsDrift : appSettings.braidsSignature]);
  }
}
static int synthEdit(int, int row, CellEditAction action) {
  if (row == 0) { int handled = edit8noLast(action, (uint8_t*)&appSettings.quality, 1, 0, 3); if (handled) chipnomadSetQuality(chipnomadState, (ChipNomadQuality)appSettings.quality); return handled; }
  if (row == 1) { int handled = edit8noLast(action, (uint8_t*)&appSettings.aySampleDithering, 1, 0, 1); if (handled && chipnomadState) chipnomadState->aySampleDithering = appSettings.aySampleDithering; return handled; }
  uint8_t value = (uint8_t)(row == 2 ? appSettings.braidsBits : row == 3 ? appSettings.braidsDrift : appSettings.braidsSignature);
  int handled = edit8noLast(action, &value, 1, 0, row == 2 ? 6 : 4);
  if (handled) {
    if (row == 2) appSettings.braidsBits = value;
    else if (row == 3) appSettings.braidsDrift = value;
    else appSettings.braidsSignature = value;
  }
  if (handled) chipnomadSetBraidsSettings(chipnomadState, appSettings.braidsBits, appSettings.braidsDrift, appSettings.braidsSignature, appSettings.braidsSignatureSeed);
  return handled;
}
static ScreenData synthData = {5, 0, 0, 0, -1, 0, 0, 0, 0, ScreenPlaybackLevel::none, columnCount, synthStatic, synthCursor, NULL, noHeader, noHeader, synthField, synthEdit, NULL, NULL, NULL, NULL};

static void mixerStatic(void) { gfxSetFgColor(appSettings.colorScheme.textTitles); gfxPrint(0, 0, "MIXER"); }
static void mixerCursor(int, int row) { gfxCursor(23, 2 + row, 4); }
static void mixerField(int, int row, CellState state) {
  const ColorScheme cs = appSettings.colorScheme;
  gfxSetFgColor(cs.textDefault); gfxPrint(0, 2 + row, row == 0 ? "Mix volume" : "Tilt pivot");
  gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault);
  if (row == 0) gfxPrintf(23, 2, "%03d%%", (int)(appSettings.mixVolume * 100.0f + .5f)); else gfxPrintf(23, 3, "%u Hz", chipnomadState->project.tiltPivotHz);
}
static int mixerEdit(int, int row, CellEditAction action) {
  if (row == 0) { uint8_t value = (uint8_t)(appSettings.mixVolume * 100.0f + .5f); int handled = edit8noLast(action, &value, 10, 1, 100); if (handled) { appSettings.mixVolume = (float)value / 100.0f; if (chipnomadState) chipnomadState->mixVolume = appSettings.mixVolume; } return handled; }
  int handled = edit16withMinMax(action, &chipnomadState->project.tiltPivotHz, 250, 250, 4000); if (handled) projectModified = 1; return handled;
}
static ScreenData mixerData = {2, 0, 0, 0, -1, 0, 0, 0, 0, ScreenPlaybackLevel::none, columnCount, mixerStatic, mixerCursor, NULL, noHeader, noHeader, mixerField, mixerEdit, NULL, NULL, NULL, NULL};

static void graphicsStatic(void) { gfxSetFgColor(appSettings.colorScheme.textTitles); gfxPrint(0, 0, "GRAPHICS"); }
static void graphicsCursor(int, int row) { gfxCursor(row == 2 || row >= 4 ? 23 : 0, 2 + row, row == 0 ? 16 : row == 1 ? 9 : row == 2 ? 3 : row == 3 ? 13 : row == 4 ? 6 : 8); }
static void graphicsField(int, int row, CellState state) {
  const ColorScheme cs = appSettings.colorScheme;
  if (row == 0) {
    gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(0, 2, "Edit color theme");
  } else if (row == 1) {
    gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(0, 3, "Load font");
  } else if (row == 2) {
    gfxSetFgColor(cs.textDefault); gfxPrint(0, 4, "Persistent waveform");
    gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(23, 4, appSettings.persistentWaveform ? "ON " : "OFF");
  } else if (row == 3) {
    gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrint(0, 5, "Track visuals");
  } else if (row == 4) {
    gfxSetFgColor(cs.textDefault); gfxPrint(0, 6, "Waveform FPS");
    gfxSetFgColor(state == CellState::focus ? cs.textValue : cs.textDefault); gfxPrintf(23, 6, "%02d FPS", appSettings.waveformRefreshHz);
  } else if (row == 5) {
    gfxSetFgColor(cs.textDefault); gfxPrint(0, 7, "Renderer");
    gfxSetFgColor(cs.textInfo); gfxPrint(23, 7, gfxGetRendererType());
  }
}
static void fontLoaded(const char* path) {
  Font* font = fontLoad(path);
  if (font) {
    fontSetCurrent(font); gfxReloadFont(); strncpy(appSettings.fontPath, path, PATH_LENGTH); appSettings.fontPath[PATH_LENGTH] = 0;
    const char* separator = strrchr(path, PATH_SEPARATOR);
    if (separator) { int len = separator - path; if (len > 0 && len < PATH_LENGTH) { strncpy(appSettings.fontFolderPath, path, len); appSettings.fontFolderPath[len] = 0; } }
    screenMessage(MESSAGE_TIME, "Loaded: %s", font->name);
  } else screenMessage(MESSAGE_TIME, "Failed to load font");
  screenSetup(&screenGraphicsSettings, 0);
}
static void fontCancelled(void) { screenSetup(&screenGraphicsSettings, 0); }
static int graphicsEdit(int, int row, CellEditAction action) {
  if (row == 2) {
    uint8_t value = appSettings.persistentWaveform;
    if (action == CellEditAction::tap || action == CellEditAction::doubleTap) value ^= 1;
    else if (!edit8noLast(action, &value, 1, 0, 1)) return 0;
    appSettings.persistentWaveform = value;
    if (settingsSave() != 0) screenMessage(MESSAGE_TIME, "Could not save waveform setting");
    return 1;
  }
  if (row == 4) {
    uint8_t value = (uint8_t)appSettings.waveformRefreshHz;
    if (!edit8noLast(action, &value, 5, 1, 60)) return 0;
    appSettings.waveformRefreshHz = value;
    if (settingsSave() != 0) screenMessage(MESSAGE_TIME, "Could not save waveform setting");
    return 1;
  }
  if (row == 5 || action != CellEditAction::tap) return 0;
  if (row == 0) screenSetup(&screenColorTheme, 0);
  else if (row == 1) { fileBrowserSetup("LOAD FONT", ".cnfont", appSettings.fontFolderPath, fontLoaded, fontCancelled); screenSetup(&screenFileBrowser, 0); }
  else if (row == 3) screenSetup(&screenTrackVisuals, 0);
  return 0;
}
static int graphicsCellValid(int, int) { return 1; }
static ScreenData graphicsData = {6, 0, 0, 0, -1, 0, 0, 0, 0, ScreenPlaybackLevel::none, columnCount, graphicsStatic, graphicsCursor, NULL, noHeader, noHeader, graphicsField, graphicsEdit, NULL, NULL, graphicsCellValid, NULL};

static int menuInput(ScreenData* data, int isKeyDown, int keys, int taps) { if (keys == keyOpt) { screenSetup(&screenSettings, 0); return 1; } return screenInput(data, isKeyDown, keys, taps); }
static void synthRedraw(void) { screenFullRedraw(&synthData); }
static void mixerRedraw(void) { screenFullRedraw(&mixerData); }
static void graphicsRedraw(void) { screenFullRedraw(&graphicsData); }
static int synthInput(int down, int keys, int taps) { return menuInput(&synthData, down, keys, taps); }
static int mixerInput(int down, int keys, int taps) { return menuInput(&mixerData, down, keys, taps); }
static int graphicsInput(int down, int keys, int taps) { return menuInput(&graphicsData, down, keys, taps); }
static ScreenPlaybackLevel playbackLevel(void) { return ScreenPlaybackLevel::song; }
const AppScreen screenSynthSettings = {NULL, setup, synthRedraw, draw, synthInput, playbackLevel};
const AppScreen screenMixerSettings = {NULL, setup, mixerRedraw, draw, mixerInput, playbackLevel};
const AppScreen screenGraphicsSettings = {NULL, setup, graphicsRedraw, draw, graphicsInput, playbackLevel};
