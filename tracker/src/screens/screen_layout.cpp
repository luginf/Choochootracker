#include "screen_layout.h"
#include "screens.h"
#include "corelib_gfx.h"

int screenScopeRows(const AppScreen* screen) {
  if (!appSettings.persistentWaveform) return 0;
  return screen == &screenSong || screen == &screenChain ||
    screen == &screenPhrase || screen == &screenTable ||
    screen == &screenInstrument || screen == &screenInstrumentPool ||
    screen == &screenModulation || screen == &screenMixer ||
    screen == &screenGroove ? 2 : 0;
}

int screenVisibleRows(void) {
  // Modulation has two compact blocks of eight logical fields, not a list.
  return currentScreen == &screenModulation ? 16 : 16 - screenScopeRows(currentScreen);
}

int screenModulationRowY(int row) {
  if (row < 0 || row >= 16) return -1;
  if (appSettings.persistentWaveform) return row + 1;
  return row < 8 ? row + 2 : row + 3;
}

int screenModulationRowAt(int y) {
  for (int row = 0; row < 16; ++row)
    if (screenModulationRowY(row) == y) return row;
  return -1;
}

ScreenOverlayCoordinates::ScreenOverlayCoordinates()
  : previous_(gfxGetContentRowOffset()) { gfxSetContentRowOffset(0); }
ScreenOverlayCoordinates::~ScreenOverlayCoordinates() { gfxSetContentRowOffset(previous_); }
