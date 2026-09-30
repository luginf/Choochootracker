#include "doctest.h"
#include "common.h"
#include "app.h"
#include "app_ui_mock.h"
#include "chipnomad_lib_live_stick.h"
#include "corelib_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
const InputCode keyboardLive = {InputDeviceType::keyboard, 1001};
const InputCode gamepadLive = {InputDeviceType::gamepad, 1002};
const InputCode logicalLive = {InputDeviceType::logical, keyMotionLive};

void input(InputCode code, bool down) {
  MainLoopEventData event = {};
  event.type = down ? MainLoopEvent::keyDown : MainLoopEvent::keyUp;
  event.data.input = code;
  appOnEvent(event);
}
void motion(int key, bool down) { input({InputDeviceType::logical, key}, down); }
char indicator() {
  // Isolate the status indicator from waveform rendering (audio is mocked).
  int tracks = chipnomadState->project.tracksCount;
  chipnomadState->project.tracksCount = 0;
  appDraw();
  chipnomadState->project.tracksCount = tracks;
  return mockGfxCells[19][39];
}

struct StickLiveFixture {
  AppSettings savedSettings = appSettings;
  ChipNomadState* savedState = chipnomadState;
  std::filesystem::path originalPath = std::filesystem::current_path();
  std::filesystem::path testPath = std::filesystem::temp_directory_path() /
    ("choochootracker-stick-live-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  // settingsSave()/settingsLoad() resolve their path from the running
  // executable's own directory (see corelib_file.cpp), not the process cwd,
  // so chdir'ing into testPath does not isolate settings.txt between test
  // cases. Track and clean up the real resolved path directly instead.
  std::string realSettingsPath = [] {
    char defaultDir[PATH_LENGTH];
    if (fileGetDefaultDirectory(defaultDir, sizeof(defaultDir)) != 0) return std::string();
    return std::string(defaultDir) + "/settings.txt";
  }();

  StickLiveFixture() {
    std::filesystem::create_directories(testPath);
    std::filesystem::current_path(testPath);
    if (!realSettingsPath.empty()) std::filesystem::remove(realSettingsPath);
    initDefaultAppSettings();
    appSettings.keyMapping.keyMotionLive[0] = keyboardLive;
    appSettings.keyMapping.keyMotionLive[1] = gamepadLive;
    inputRawCallback = nullptr;
    appSetup();
    currentScreen = &screenSong;
  }
  ~StickLiveFixture() {
    inputRawCallback = nullptr;
    appCleanup();
    chipnomadState = savedState;
    appSettings = savedSettings;
    chipnomadSetLiveStickEnabled(0);
    chipnomadSetMotionRecordMode(0, 0);
    std::filesystem::current_path(originalPath);
    std::filesystem::remove_all(testPath);
    if (!realSettingsPath.empty()) std::filesystem::remove(realSettingsPath);
  }
};
}

TEST_SUITE("Stick live") {
TEST_CASE_FIXTURE(StickLiveFixture, "HOLD and TOGGLE use keyboard, gamepad and logical presses") {
  for (InputCode code : {keyboardLive, gamepadLive, logicalLive}) {
    appSetStickLiveMode(StickLiveMode::hold);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
    input(code, true);
    CHECK(chipnomadLiveStickIsEnabled());
    CHECK(indicator() == '~');
    input(code, true);
    input(code, false);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
    CHECK(indicator() == ' ');

    appSetStickLiveMode(StickLiveMode::toggle);
    input(code, true);
    input(code, true); // repeat/duplicate down
    CHECK(chipnomadLiveStickIsEnabled());
    input(code, false);
    input(code, false); // duplicate up
    CHECK(chipnomadLiveStickIsEnabled());
    CHECK(indicator() == '~');
    input(code, true);
    input(code, true);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
    input(code, false);
    CHECK(indicator() == ' ');
  }
}

TEST_CASE_FIXTURE(StickLiveFixture, "overlapping input delivery toggles only once until all sources release") {
  appSetStickLiveMode(StickLiveMode::toggle);
  input(keyboardLive, true);
  input(gamepadLive, true);
  input(logicalLive, true);
  CHECK(chipnomadLiveStickIsEnabled());
  input(keyboardLive, false);
  input(gamepadLive, true); // still held after the keyboard duplicate released
  input(gamepadLive, false);
  input(logicalLive, false);
  CHECK(chipnomadLiveStickIsEnabled());
  input(gamepadLive, true);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  input(gamepadLive, false);
}

TEST_CASE_FIXTURE(StickLiveFixture, "PortMaster controller duplicate cannot stick after leaving key mapping") {
  const InputCode controllerDuplicate = {InputDeviceType::gamepad, 9001};
  const InputCode keyboardUp = {InputDeviceType::keyboard, 9002};
  appSettings.keyMapping.keyUp[0] = keyboardUp;

  // Knulli/PortMaster can emit an unmapped SDL controller event alongside a
  // gptokeyb key. Before the fix it was recorded on Key Mapping, but its
  // release was ignored after switching to Settings, poisoning later input.
  currentScreen = &screenKeyMapping;
  input(controllerDuplicate, true);
  currentScreen = &screenSong;
  input(controllerDuplicate, false);

  mockLastInputKeys = 0;
  input(keyboardUp, true);
  CHECK(mockLastInputKeys == keyUp);
  input(keyboardUp, false);
}

TEST_CASE_FIXTURE(StickLiveFixture, "FREE keeps Stick live active regardless of input") {
  appSetStickLiveMode(StickLiveMode::free);
  CHECK(chipnomadLiveStickIsEnabled());
  CHECK(indicator() == '~');
  for (InputCode code : {keyboardLive, gamepadLive, logicalLive}) {
    input(code, true);
    input(code, false);
    CHECK(chipnomadLiveStickIsEnabled());
  }
  motion(keyMotionRecord, true);
  CHECK(indicator() == '*');
  motion(keyMotionRecord, false);
  CHECK(chipnomadLiveStickIsEnabled());
  CHECK(indicator() == '~');
  appSetStickLiveMode(StickLiveMode::toggle);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
}

TEST_CASE_FIXTURE(StickLiveFixture, "mode changes clear the latch and reconcile held input") {
  appSetStickLiveMode(StickLiveMode::toggle);
  input(keyboardLive, true);
  input(keyboardLive, false);
  appSetStickLiveMode(StickLiveMode::hold);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  input(keyboardLive, true);
  appSetStickLiveMode(StickLiveMode::toggle);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  input(keyboardLive, true); // a repeat after switching is not a new press
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  input(keyboardLive, false);
  input(keyboardLive, true);
  CHECK(chipnomadLiveStickIsEnabled());
  appSetStickLiveMode(StickLiveMode::hold);
  CHECK(chipnomadLiveStickIsEnabled()); // still physically held
  input(keyboardLive, false);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  appSetStickLiveMode(StickLiveMode::toggle);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
}

TEST_CASE_FIXTURE(StickLiveFixture, "Record and Erase remain momentary with indicator priority") {
  for (StickLiveMode mode : {StickLiveMode::hold, StickLiveMode::toggle, StickLiveMode::free}) {
    appSetStickLiveMode(mode);
    for (int key : {keyMotionRecord, keyMotionErase}) {
      motion(key, true);
      CHECK(chipnomadLiveStickIsEnabled());
      CHECK(chipnomadMotionMode() == (key == keyMotionRecord ? 1 : 2));
      CHECK(indicator() == (key == keyMotionRecord ? '*' : 'x'));
      motion(key, false);
      CHECK(chipnomadLiveStickIsEnabled() == (mode == StickLiveMode::free));
      CHECK(chipnomadMotionMode() == 0);
    }
    input(keyboardLive, true);
    if (mode == StickLiveMode::toggle) input(keyboardLive, false);
    motion(keyMotionRecord, true);
    CHECK(indicator() == '*');
    chipnomadSetMotionRecordOverflow();
    CHECK(indicator() == '!');
    motion(keyMotionErase, true);
    CHECK(indicator() == 'x');
    motion(keyMotionErase, false);
    CHECK(indicator() == '!');
    motion(keyMotionRecord, false);
    CHECK(chipnomadLiveStickIsEnabled());
    CHECK(indicator() == '~');
    input(keyboardLive, false);
  }
}

TEST_CASE_FIXTURE(StickLiveFixture, "mode changes and toggling off respect Record and Erase") {
  for (int key : {keyMotionRecord, keyMotionErase}) {
    appSetStickLiveMode(StickLiveMode::hold);
    motion(key, true);
    appSetStickLiveMode(StickLiveMode::toggle);
    CHECK(chipnomadLiveStickIsEnabled());
    input(keyboardLive, true);
    input(keyboardLive, false);
    input(keyboardLive, true); // toggle off while recording/erasing
    input(keyboardLive, false);
    CHECK(chipnomadLiveStickIsEnabled());
    motion(key, false);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
    motion(key, true);
    input(keyboardLive, true);
    input(keyboardLive, false);
    appSetStickLiveMode(StickLiveMode::hold); // clear a live latch during Record/Erase
    CHECK(chipnomadLiveStickIsEnabled());
    motion(key, false);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
  }
}

TEST_CASE_FIXTURE(StickLiveFixture, "key mapping capture suppresses live actions even if callback clears itself") {
  appSetStickLiveMode(StickLiveMode::toggle);
  for (InputCode code : {keyboardLive, gamepadLive, logicalLive}) {
    inputRawCallback = [](InputCode, int) { inputRawCallback = nullptr; };
    input(code, true);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
    input(code, false);
    CHECK_FALSE(chipnomadLiveStickIsEnabled());
  }
  input(keyboardLive, true);
  input(keyboardLive, false);
  inputRawCallback = [](InputCode, int) {};
  input(keyboardLive, true);
  input(keyboardLive, false);
  CHECK(chipnomadLiveStickIsEnabled());
}

TEST_CASE_FIXTURE(StickLiveFixture, "normal exit saves mode but restart clears the active latch") {
  appSetStickLiveMode(StickLiveMode::toggle);
  input(keyboardLive, true);
  input(keyboardLive, false);
  appOnEvent({MainLoopEvent::exit, {}});
  appCleanup();
  chipnomadState = nullptr;
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.stickLiveMode == StickLiveMode::toggle);
  appSetup();
  currentScreen = &screenSong;
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  CHECK(indicator() == ' ');
  appSetStickLiveMode(StickLiveMode::hold);
  REQUIRE(settingsSave() == 0);
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.stickLiveMode == StickLiveMode::hold);
  appSetStickLiveMode(StickLiveMode::free);
  REQUIRE(settingsSave() == 0);
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.stickLiveMode == StickLiveMode::free);
}

TEST_CASE_FIXTURE(StickLiveFixture, "absent, older and invalid settings default to HOLD") {
  REQUIRE(settingsLoad() != 0); // no settings file yet
  CHECK(appSettings.stickLiveMode == StickLiveMode::hold);
  for (const char* content : {"screenWidth: 640\n", "stickLiveMode: INVALID\n",
       "stickLiveMode: 1\n", "stickLiveMode: TOGGLEjunk\n", "stickLiveMode: \n"}) {
    appSettings.stickLiveMode = StickLiveMode::toggle;
    std::ofstream(realSettingsPath) << content;
    REQUIRE(settingsLoad() == 0);
    CHECK(appSettings.stickLiveMode == StickLiveMode::hold);
  }
}

TEST_CASE_FIXTURE(StickLiveFixture, "Settings row, padded value, cursor and subsequent actions align") {
  screenSettings.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  CHECK(mockScreenData->rows == 16);
  auto* screen = mockScreenData;
  CHECK(screen->getColumnCount(9) == 1);
  screen->drawField(0, 9, CellState::focus);
  CHECK(std::string(mockGfxCells[11], 15) == "Stick live mode");
  CHECK(std::string(mockGfxCells[11] + 23, 6) == "HOLD  ");
  REQUIRE(screen->onEdit(0, 9, CellEditAction::increase) == 1);
  screen->drawField(0, 9, CellState::focus);
  CHECK(std::string(mockGfxCells[11] + 23, 6) == "TOGGLE");
  REQUIRE(screen->onEdit(0, 9, CellEditAction::increase) == 1);
  screen->drawField(0, 9, CellState::focus);
  CHECK(std::string(mockGfxCells[11] + 23, 6) == "FREE  ");
  CHECK(chipnomadLiveStickIsEnabled());
  REQUIRE(screen->onEdit(0, 9, CellEditAction::decrease) == 1);
  input(keyboardLive, true);
  input(keyboardLive, false);
  screen->onEdit(0, 9, CellEditAction::tap); // unchanged mode must not clear a latch
  CHECK(chipnomadLiveStickIsEnabled());
  REQUIRE(screen->onEdit(0, 9, CellEditAction::decrease) == 1);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  screen->drawField(0, 9, CellState::focus);
  CHECK(std::string(mockGfxCells[11] + 23, 6) == "HOLD  ");
  screen->drawCursor(0, 9);
  CHECK(mockCursorX == 23);
  CHECK(mockCursorY == 11);
  CHECK(mockCursorWidth == 6);

  // MIDI In/Out rows: no real device attached under test, so they stay OFF.
  screen->drawField(0, 10, CellState::focus);
  CHECK(std::string(mockGfxCells[12], 7) == "MIDI In");
  CHECK(std::string(mockGfxCells[12] + 23, 3) == "OFF");
  screen->drawCursor(0, 10);
  CHECK(mockCursorX == 23);
  CHECK(mockCursorY == 12);
  CHECK(mockCursorWidth == 20);
  CHECK(screen->onEdit(0, 10, CellEditAction::increase) == 1); // handled, but wraps straight back to OFF (no ports)
  CHECK(appSettings.midiInputDevice == -1);
  screen->drawField(0, 11, CellState::focus);
  CHECK(std::string(mockGfxCells[13], 8) == "MIDI Out");
  CHECK(std::string(mockGfxCells[13] + 23, 3) == "OFF");
  screen->drawCursor(0, 11);
  CHECK(mockCursorX == 23);
  CHECK(mockCursorY == 13);
  CHECK(mockCursorWidth == 20);

  const char* labels[] = {"Key mapping", "Load font", "Edit color theme", "Quit ChooChooTracker"};
  const int lines[] = {14, 15, 16, 19};
  const int widths[] = {11, 9, 16, 19}; // preserve existing action cursor widths
  const AppScreen* destinations[] = {&screenKeyMapping, &screenFileBrowser, &screenColorTheme};
  mockQuitTriggered = 0;
  for (int i = 0; i < 4; ++i) {
    screen->drawField(0, 12 + i, CellState::focus);
    CHECK(std::string(mockGfxCells[lines[i]], std::string(labels[i]).size()) == labels[i]);
    screen->drawCursor(0, 12 + i);
    CHECK(mockCursorX == 0);
    CHECK(mockCursorY == lines[i]);
    CHECK(mockCursorWidth == widths[i]);
    screen->onEdit(0, 12 + i, CellEditAction::tap);
    if (i < 3) {
      CHECK(currentScreen == destinations[i]);
      CHECK_FALSE(mockQuitTriggered);
    }
  }
  CHECK(mockQuitTriggered);
  CHECK(std::string(mockBrowserTitle) == "LOAD FONT");
  CHECK(std::string(mockBrowserExtension) == ".cnfont");
}
}
