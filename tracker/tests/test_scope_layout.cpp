#include "doctest.h"
#include "screens.h"
#include "corelib_gfx.h"
#include <initializer_list>

TEST_CASE("Modulation draw and touch rows agree in both waveform modes") {
  const uint8_t saved = appSettings.persistentWaveform;
  const int expected[2][16] = {
    {2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18},
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}
  };
  for (int enabled = 0; enabled < 2; ++enabled) {
    appSettings.persistentWaveform = enabled;
    CHECK(screenModulationRowY(-1) == -1);
    CHECK(screenModulationRowY(16) == -1);
    for (int row = 0; row < 16; ++row) {
      CHECK(screenModulationRowY(row) == expected[enabled][row]);
      CHECK(screenModulationRowAt(expected[enabled][row]) == row);
    }
    for (int y = 0; y < 20; ++y) {
      bool field = false;
      for (int row = 0; row < 16; ++row) field |= expected[enabled][row] == y;
      if (!field) CHECK(screenModulationRowAt(y) == -1);
    }
  }
  appSettings.persistentWaveform = saved;
}

TEST_CASE("Scope reserves rows only on the intended screens") {
  const AppScreen* saved = currentScreen;
  uint8_t oldEnabled = appSettings.persistentWaveform;
  appSettings.persistentWaveform = 0;
  currentScreen = &screenSong;
  CHECK(screenScopeRows(currentScreen) == 0);
  CHECK(screenVisibleRows() == 16);
  appSettings.persistentWaveform = 1;
  for (const AppScreen* screen : {&screenSong, &screenChain, &screenPhrase, &screenTable,
       &screenInstrument, &screenInstrumentPool, &screenMixer, &screenGroove}) {
    currentScreen = screen;
    CHECK(screenScopeRows(screen) == 2);
    CHECK(screenVisibleRows() == 14);
  }
  for (const AppScreen* screen : {&screenSettings, &screenProject, &screenAYWavetable, &screenTitle})
    CHECK(screenScopeRows(screen) == 0);
  currentScreen = &screenModulation;
  CHECK(screenVisibleRows() == 16);
  currentScreen = saved;
  appSettings.persistentWaveform = oldEnabled;
  gfxSetContentRowOffset(2);
  { ScreenOverlayCoordinates overlay; CHECK(gfxGetContentRowOffset() == 0); }
  CHECK(gfxGetContentRowOffset() == 2);
  gfxSetContentRowOffset(0);
}
