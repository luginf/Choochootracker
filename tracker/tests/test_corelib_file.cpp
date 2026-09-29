#include "doctest.h"
#include "corelib_file.h"

#include <cstdlib>
#include <cstring>
#include <string>

TEST_SUITE("corelib_file default directory") {

TEST_CASE("APPIMAGE env var redirects to a writable per-user directory") {
  // AppImages mount read-only, so resolving next to the executable (the
  // normal desktop behavior) would be unwritable. The AppImage runtime
  // always sets APPIMAGE for the process it launches - simulate that.
  const char* previousAppImage = getenv("APPIMAGE");
  const char* home = getenv("HOME");
  REQUIRE(home != nullptr);

  setenv("APPIMAGE", "/tmp/whatever.AppImage", 1);

  char buffer[4096];
  REQUIRE(fileGetDefaultDirectory(buffer, sizeof(buffer)) == 0);

  std::string expected = std::string(home) + "/.local/share/ChooChooTracker";
  CHECK(std::string(buffer) == expected);

  if (previousAppImage) {
    setenv("APPIMAGE", previousAppImage, 1);
  } else {
    unsetenv("APPIMAGE");
  }
}

}
