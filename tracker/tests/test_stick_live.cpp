#include "doctest.h"
#include "common.h"
#include "app.h"
#include "app_ui_mock.h"
#include "chipnomad_lib_live_stick.h"
#include "corelib_file.h"
#include "project_utils.h"

#include <chrono>
#include <cstring>
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
    char settingsDirectory[PATH_LENGTH];
    if (fileGetDefaultDirectory(settingsDirectory, sizeof(settingsDirectory)) == 0)
      realSettingsPath = std::string(settingsDirectory) + "/settings.txt";
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

TEST_CASE_FIXTURE(StickLiveFixture, "Shift horizontal navigation exits branch screens to the spine") {
  struct Route { const AppScreen *from, *left, *right; };
  const Route routes[] = {
    {&screenProject,&screenMixer,&screenChain},
    {&screenSettings,&screenMixer,&screenChain},
    {&screenSynthSettings,&screenMixer,&screenChain},
    {&screenMixerSettings,&screenMixer,&screenChain},
    {&screenGraphicsSettings,&screenMixer,&screenChain},
    {&screenTrackVisuals,&screenMixer,&screenChain},
    {&screenMidi,&screenMixer,&screenChain},
    {&screenMidiChannelMap,&screenMixer,&screenChain},
    {&screenMidiCC,&screenMixer,&screenChain},
    {&screenGroove,&screenChain,&screenInstrument},
    {&screenModulation,&screenPhrase,&screenTable},
    {&screenInsertFX,&screenPhrase,&screenTable},
    {&screenAYWavetable,&screenInstrument,&screenTable},
  };
  for (const auto& route : routes) for (int direction : {keyLeft,keyRight}) {
    currentScreen = route.from;
    motion(keyShift,true);
    motion(direction,true);
    const auto* destination = direction == keyLeft ? route.left : route.right;
    CHECK(currentScreen == destination);
    motion(direction,false);
    CHECK(currentScreen == destination); // releasing direction while still holding Shift
    motion(keyShift,false);
    CHECK(currentScreen == destination);
  }
}

TEST_SUITE("Stick live") {
TEST_CASE_FIXTURE(StickLiveFixture, "FM motion records one native command without a selector") {
  auto& row=chipnomadState->project.phrases[0].rows[0];
  phraseClear(&chipnomadState->project.phrases[0]);
  REQUIRE(chipnomadMotionPushEvent({0,0,fxOMU,7,0}));
  appOnEvent({MainLoopEvent::tick});
  CHECK(row.fx[2][0]==fxOMU);CHECK(row.fx[2][1]==7);
  CHECK(row.fx[0][0]==EMPTY_VALUE_8);CHECK(row.fx[1][0]==EMPTY_VALUE_8);
  REQUIRE(chipnomadMotionPushEvent({0,0,fxOMU,9,0}));
  appOnEvent({MainLoopEvent::tick});CHECK(row.fx[2][1]==9);
  REQUIRE(chipnomadMotionPushEvent({0,0,fxOMU,0,1}));
  appOnEvent({MainLoopEvent::tick});CHECK(row.fx[2][0]==EMPTY_VALUE_8);
}

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
    REQUIRE(screenSettings.draw != nullptr);
    REQUIRE(screenSettings.setup != nullptr);
    REQUIRE(screenSynthSettings.draw != nullptr);
    REQUIRE(screenSynthSettings.setup != nullptr);
    REQUIRE(screenMixerSettings.draw != nullptr);
    REQUIRE(screenMixerSettings.setup != nullptr);
    REQUIRE(screenGraphicsSettings.draw != nullptr);
    REQUIRE(screenGraphicsSettings.setup != nullptr);
  screenSettings.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  CHECK(mockScreenData->rows == 10);
  auto* screen = mockScreenData;
  CHECK(screen->getColumnCount(2) == 1);
  screen->drawField(0, 2, CellState::focus);
  CHECK(std::string(mockGfxCells[4], 15) == "Stick live mode");
  CHECK(std::string(mockGfxCells[4] + 23, 6) == "HOLD  ");
  REQUIRE(screen->onEdit(0, 2, CellEditAction::increase) == 1);
  screen->drawField(0, 2, CellState::focus);
  CHECK(std::string(mockGfxCells[4] + 23, 6) == "TOGGLE");
  REQUIRE(screen->onEdit(0, 2, CellEditAction::increase) == 1);
  screen->drawField(0, 2, CellState::focus);
  CHECK(std::string(mockGfxCells[4] + 23, 6) == "FREE  ");
  CHECK(chipnomadLiveStickIsEnabled());
  REQUIRE(screen->onEdit(0, 2, CellEditAction::decrease) == 1);
  input(keyboardLive, true);
  input(keyboardLive, false);
  screen->onEdit(0, 2, CellEditAction::tap); // unchanged mode must not clear a latch
  CHECK(chipnomadLiveStickIsEnabled());
  REQUIRE(screen->onEdit(0, 2, CellEditAction::decrease) == 1);
  CHECK_FALSE(chipnomadLiveStickIsEnabled());
  screen->drawField(0, 2, CellState::focus);
  CHECK(std::string(mockGfxCells[4] + 23, 6) == "HOLD  ");
  screen->drawCursor(0, 2);
  CHECK(mockCursorX == 23);
  CHECK(mockCursorY == 4);
  CHECK(mockCursorWidth == 6);

  screen->drawField(0, 8, CellState::focus);
  CHECK(std::string(mockGfxCells[10], 14) == "Support report");
  screen->drawCursor(0, 8);
  CHECK(mockCursorX == 0);
  CHECK(mockCursorY == 10);
  CHECK(mockCursorWidth == 14);

  const char* labels[] = {"MIDI", "Key mapping", "Synths", "Mixer", "Graphics", "Quit ChooChooTracker"};
  const int lines[] = {5, 6, 7, 8, 9, 18};
  const int widths[] = {4, 11, 6, 5, 8, 19};
  const AppScreen* destinations[] = {&screenMidi, &screenKeyMapping, &screenSynthSettings, &screenMixerSettings, &screenGraphicsSettings};
  mockQuitTriggered = 0;
  for (int i = 0; i < 6; ++i) {
    int row = i < 5 ? 3 + i : 9;
    screen->drawField(0, row, CellState::focus);
    CHECK(std::string(mockGfxCells[lines[i]], std::string(labels[i]).size()) == labels[i]);
    screen->drawCursor(0, row);
    CHECK(mockCursorX == 0);
    CHECK(mockCursorY == lines[i]);
    CHECK(mockCursorWidth == widths[i]);
    screen->onEdit(0, row, CellEditAction::tap);
    if (i < 5) {
      CHECK(currentScreen == destinations[i]);
      CHECK_FALSE(mockQuitTriggered);
    }
  }
  CHECK(mockQuitTriggered);
}
}

TEST_SUITE("MIDI settings") {

TEST_CASE_FIXTURE(StickLiveFixture, "screenMidi shows device rows and links to channel mapping") {
  screenMidi.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  CHECK(mockScreenData->rows == 4);
  auto* screen = mockScreenData;

  screen->drawField(0, 0, CellState::focus);
  CHECK(std::string(mockGfxCells[2], 7) == "MIDI In");
  CHECK(std::string(mockGfxCells[2] + 10, 3) == "OFF");
  screen->drawCursor(0, 0);
  CHECK(mockCursorX == 10);
  CHECK(mockCursorY == 2);
  CHECK(mockCursorWidth == 3); // "OFF"
  CHECK(screen->onEdit(0, 0, CellEditAction::increase) == 1); // handled, wraps straight back to OFF (no ports)
  CHECK(appSettings.midiInputDevice == -1);

  screen->drawField(0, 1, CellState::focus);
  CHECK(std::string(mockGfxCells[3], 8) == "MIDI Out");
  CHECK(std::string(mockGfxCells[3] + 10, 3) == "OFF");

  screen->drawField(0, 2, CellState::focus);
  CHECK(std::string(mockGfxCells[5], 15) == "Channel mapping");
  screen->onEdit(0, 2, CellEditAction::tap);
  CHECK(currentScreen == &screenMidiChannelMap);

  currentScreen = &screenMidi;
  screen->drawField(0, 3, CellState::focus);
  CHECK(std::string(mockGfxCells[6], 15) == "MIDI CC mapping");
  screen->onEdit(0, 3, CellEditAction::tap);
  CHECK(currentScreen == &screenMidiCC);

  currentScreen = &screenMidi;
  CHECK(screenMidi.onInput(1, keyOpt, 0) == 1);
  CHECK(currentScreen == &screenSettings);
}

TEST_CASE_FIXTURE(StickLiveFixture, "screenMidi shows NOT FOUND for a configured device that isn't currently available") {
  strncpy(appSettings.midiInputDeviceName, "Some USB Keyboard", MIDI_DEVICE_NAME_LENGTH);
  appSettings.midiInputDevice = -1; // Not resolved to a live port (none attached under test).

  screenMidi.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  mockScreenData->drawField(0, 0, CellState::focus);
  CHECK(std::string(mockGfxCells[2] + 10, 9) == "NOT FOUND");
}

TEST_CASE_FIXTURE(StickLiveFixture, "MIDI device names round-trip through settings save/load") {
  strncpy(appSettings.midiInputDeviceName, "Keystation Mini", MIDI_DEVICE_NAME_LENGTH);
  strncpy(appSettings.midiOutputDeviceName, "FluidSynth virtual port", MIDI_DEVICE_NAME_LENGTH);
  REQUIRE(settingsSave() == 0);

  initDefaultAppSettings();
  CHECK(appSettings.midiInputDeviceName[0] == '\0');

  REQUIRE(settingsLoad() == 0);
  CHECK(std::string(appSettings.midiInputDeviceName) == "Keystation Mini");
  CHECK(std::string(appSettings.midiOutputDeviceName) == "FluidSynth virtual port");
}

TEST_CASE_FIXTURE(StickLiveFixture, "screenMidiChannelMap cycles channel-to-instrument assignment") {
  screenMidiChannelMap.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  CHECK(mockScreenData->rows == MIDI_CHANNEL_COUNT);
  auto* screen = mockScreenData;

  screen->drawField(0, 0, CellState::focus);
  CHECK(std::string(mockGfxCells[2], 5) == "CH 01");
  CHECK(std::string(mockGfxCells[2] + 23, 3) == "OFF");

  REQUIRE(screen->onEdit(0, 0, CellEditAction::increase) == 1);
  CHECK(appSettings.midiChannelInstrument[0] == 0);
  screen->drawField(0, 0, CellState::focus);
  std::string expected = std::string(byteToHex(0)) + ": " + instrumentName(&chipnomadState->project, 0);
  CHECK(std::string(mockGfxCells[2] + 23, expected.size()) == expected);

  REQUIRE(screen->onEdit(0, 0, CellEditAction::decrease) == 1);
  CHECK(appSettings.midiChannelInstrument[0] == -1); // back to OFF
  REQUIRE(screen->onEdit(0, 0, CellEditAction::decrease) == 1);
  CHECK(appSettings.midiChannelInstrument[0] == PROJECT_MAX_INSTRUMENTS - 1); // wraps the other way

  currentScreen = &screenMidiChannelMap;
  CHECK(screenMidiChannelMap.onInput(1, keyOpt, 0) == 1);
  CHECK(currentScreen == &screenMidi);
}

TEST_CASE_FIXTURE(StickLiveFixture, "midiChannelInstrument round-trips through settings save/load and clamps garbage") {
  appSettings.midiChannelInstrument[0] = 5;
  appSettings.midiChannelInstrument[15] = 42;
  REQUIRE(settingsSave() == 0);

  initDefaultAppSettings();
  CHECK(appSettings.midiChannelInstrument[0] == -1);

  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.midiChannelInstrument[0] == 5);
  CHECK(appSettings.midiChannelInstrument[15] == 42);
  for (int i = 1; i < 15; i++) CHECK(appSettings.midiChannelInstrument[i] == -1);

  // Out-of-range values (from a hand-edited or corrupted settings.txt) clamp to OFF.
  // Written to fileGetDefaultDirectory()'s own resolved path rather than a bare
  // relative "settings.txt": that call resolves the running test binary's own
  // executable directory (see fileGetDefaultDirectory()'s desktop fallback),
  // not this fixture's temp cwd, so a relative write here would silently miss
  // the file settingsLoad() actually reads.
  char defaultDir[PATH_LENGTH];
  REQUIRE(fileGetDefaultDirectory(defaultDir, sizeof(defaultDir)) == 0);
  std::string settingsPath = std::string(defaultDir) + PATH_SEPARATOR_STR + "settings.txt";
  std::ofstream(settingsPath) << "midiChannelInstrument: 999,-5,3\n";
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.midiChannelInstrument[0] == -1);
  CHECK(appSettings.midiChannelInstrument[1] == -1);
  CHECK(appSettings.midiChannelInstrument[2] == 3);
}

}

TEST_CASE_FIXTURE(StickLiveFixture, "Persistent waveform is opt-in and its Settings toggle persists") {
  CHECK(appSettings.persistentWaveform == 0);
  screenGraphicsSettings.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  auto* screen = mockScreenData;
  int modified = projectModified;
  CHECK(screen->rows == 6);
  screen->drawField(0, 2, CellState::focus);
  CHECK(std::string(mockGfxCells[4] + 23, 3) == "OFF");
  REQUIRE(screen->onEdit(0, 2, CellEditAction::tap) == 1);
  CHECK(appSettings.persistentWaveform == 1);
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.persistentWaveform == 1);
  REQUIRE(screen->onEdit(0, 2, CellEditAction::tap) == 1);
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.persistentWaveform == 0);
  CHECK(projectModified == modified);
}

TEST_CASE_FIXTURE(StickLiveFixture, "Personal visual settings coexist and survive saving either option") {
  mockQuitTriggered = 0;
  appSettings.trackVisuals[0].mode = TrackVisualMode::audio;
  screenGraphicsSettings.fullRedraw();
  REQUIRE(mockScreenData != nullptr);
  auto* settings = mockScreenData;
  REQUIRE(settings->onEdit(0, 2, CellEditAction::tap) == 1);
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.persistentWaveform == 1);
  CHECK(appSettings.trackVisuals[0].mode == TrackVisualMode::audio);
  settings->onEdit(0, 3, CellEditAction::tap);
  CHECK(currentScreen == &screenTrackVisuals);
  CHECK_FALSE(mockQuitTriggered);
  appSettings.trackVisuals[1].mode = TrackVisualMode::audio;
  REQUIRE(settingsSave() == 0);
  REQUIRE(settingsLoad() == 0);
  CHECK(appSettings.persistentWaveform == 1);
  CHECK(appSettings.trackVisuals[1].mode == TrackVisualMode::audio);
}
