#include "doctest.h"
#include "corelib_file.h"
#include "common.h"

#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
namespace {
// Sets APPIMAGE for the duration of the test, restoring whatever it was
// (or clearing it) on scope exit - AppImages set this for the process they
// launch, and both fileGetDefaultDirectory() and initDefaultAppSettings()
// key off it to avoid pointing into the AppImage's read-only mount.
struct AppImageEnvGuard {
  const char* previous = getenv("APPIMAGE");
  AppImageEnvGuard() { setenv("APPIMAGE", "/tmp/whatever.AppImage", 1); }
  ~AppImageEnvGuard() {
    if (previous) setenv("APPIMAGE", previous, 1);
    else unsetenv("APPIMAGE");
  }
};

// Clears XDG_DATA_HOME for the duration of the test (restoring it after),
// so tests of the ~/.local/share fallback stay deterministic regardless of
// the environment they run in.
struct NoXdgDataHomeGuard {
  const char* previous = getenv("XDG_DATA_HOME");
  NoXdgDataHomeGuard() { unsetenv("XDG_DATA_HOME"); }
  ~NoXdgDataHomeGuard() {
    if (previous) setenv("XDG_DATA_HOME", previous, 1);
  }
};
}

TEST_SUITE("corelib_file default directory") {

TEST_CASE("APPIMAGE env var redirects to a writable per-user directory") {
  const char* home = getenv("HOME");
  REQUIRE(home != nullptr);
  NoXdgDataHomeGuard noXdg;
  AppImageEnvGuard guard;

  char buffer[4096];
  REQUIRE(fileGetDefaultDirectory(buffer, sizeof(buffer)) == 0);

  std::string expected = std::string(home) + "/.local/share/ChooChooTracker";
  CHECK(std::string(buffer) == expected);
}

TEST_CASE("APPIMAGE env var honors XDG_DATA_HOME when set") {
  AppImageEnvGuard guard;
  setenv("XDG_DATA_HOME", "/tmp/choochootracker-xdg-test", 1);

  char buffer[4096];
  REQUIRE(fileGetDefaultDirectory(buffer, sizeof(buffer)) == 0);

  CHECK(std::string(buffer) == "/tmp/choochootracker-xdg-test/ChooChooTracker");
  unsetenv("XDG_DATA_HOME");
}

TEST_CASE("APPIMAGE env var anchors default project/sample/theme paths") {
  const char* home = getenv("HOME");
  REQUIRE(home != nullptr);
  NoXdgDataHomeGuard noXdg;
  AppImageEnvGuard guard;

  initDefaultAppSettings();

  std::string base = std::string(home) + "/.local/share/ChooChooTracker";
  CHECK(std::string(appSettings.projectPath) == base + "/projects");
  CHECK(std::string(appSettings.samplePath) == base + "/samples");
  CHECK(std::string(appSettings.ayWavetablePath) == base + "/AY_wavetables");
  CHECK(std::string(appSettings.scwfPath) == base + "/waveforms");
  CHECK(std::string(appSettings.srWavetablePath) == base + "/SR_wavetables");
  CHECK(std::string(appSettings.pitchTablePath) == base + "/pitch-tables");
  CHECK(std::string(appSettings.instrumentPath) == base + "/instruments");
  CHECK(std::string(appSettings.themePath) == base + "/themes");
  CHECK(std::string(appSettings.fontFolderPath) == base + "/fonts");

  // Restore normal (non-AppImage) defaults for any test running after this
  // one in the same process.
  unsetenv("APPIMAGE");
  initDefaultAppSettings();
  CHECK(std::string(appSettings.projectPath) == "projects");
}

}

#endif
