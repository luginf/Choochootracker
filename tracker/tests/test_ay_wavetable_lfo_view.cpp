#include "doctest.h"
#include "common.h"
#include "corelib_file.h"
#include "waveform_display.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

TEST_SUITE("AY wavetable LFO view") {

TEST_CASE("linear LFO preview maps AY wavetable endpoints and zero") {
  CHECK(ayWavetableLfoPreviewLevel(0) == 0);
  CHECK(ayWavetableLfoPreviewLevel(15) == 255);
  CHECK(ayWavetableLfoPreviewLevel(7) < 128);
  CHECK(ayWavetableLfoPreviewLevel(8) > 128);
}

TEST_CASE("linear LFO preview draws a zero guide") {
  uint8_t pixels[32 * 16] = {};
  uint8_t wavetable[32] = {};
  Bitmap bitmap = {1, 1, 32, 16, pixels, nullptr};

  renderAYWavetableLfoPreview(&bitmap, wavetable);
  for (int x = 0; x < bitmap.widthPixels; ++x) CHECK(pixels[(bitmap.heightPixels / 2) * bitmap.widthPixels + x] == 64);
  for (int x = 0; x < bitmap.widthPixels; ++x) CHECK(pixels[(bitmap.heightPixels - 1) * bitmap.widthPixels + x] == 255);
}

TEST_CASE("AY wavetable LFO view setting survives save and load") {
  // settingsSave()/settingsLoad() resolve their path from the running
  // executable's own directory (see corelib_file.cpp), not the process cwd,
  // so isolate this test by targeting that real resolved path directly
  // rather than fs::current_path() (which settingsSave/Load no longer
  // consult).
  char defaultDir[PATH_LENGTH];
  REQUIRE(fileGetDefaultDirectory(defaultDir, sizeof(defaultDir)) == 0);
  std::string settingsPath = std::string(defaultDir) + "/settings.txt";
  std::filesystem::remove(settingsPath);

  initDefaultAppSettings();
  CHECK(appSettings.ayWavetableLfoView == 0);
  appSettings.ayWavetableLfoView = 1;
  REQUIRE(settingsSave() == 0);
  initDefaultAppSettings();
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.ayWavetableLfoView == 1);

  std::ofstream(settingsPath) << "screenWidth: 640\n";
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.ayWavetableLfoView == 0);

  std::filesystem::remove(settingsPath);
}

}
