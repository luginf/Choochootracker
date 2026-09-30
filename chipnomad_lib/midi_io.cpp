#include "midi_io.h"

#ifdef DESKTOP_BUILD

#include "external/rtmidi/RtMidi.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static RtMidiIn* g_midiIn = NULL;
static RtMidiOut* g_midiOut = NULL;

int midiIoAvailable(void) { return 1; }

template <typename Device>
static int portCount(Device* probe) {
  try {
    return (int)probe->getPortCount();
  } catch (RtError&) {
    return 0;
  }
}

int midiIoInputPortCount(void) {
  RtMidiIn probe;
  return portCount(&probe);
}

int midiIoInputPortName(int index, char* buffer, int bufferSize) {
  try {
    RtMidiIn probe;
    std::string name = probe.getPortName((unsigned int)index);
    snprintf(buffer, bufferSize, "%s", name.c_str());
    return 0;
  } catch (RtError&) {
    return -1;
  }
}

int midiIoOutputPortCount(void) {
  RtMidiOut probe;
  return portCount(&probe);
}

int midiIoOutputPortName(int index, char* buffer, int bufferSize) {
  try {
    RtMidiOut probe;
    std::string name = probe.getPortName((unsigned int)index);
    snprintf(buffer, bufferSize, "%s", name.c_str());
    return 0;
  } catch (RtError&) {
    return -1;
  }
}

int midiIoOpenInput(int portIndex) {
  midiIoCloseInput();
  try {
    g_midiIn = new RtMidiIn();
    g_midiIn->openPort((unsigned int)portIndex, "ChooChooTracker In");
    // We poll for note/CC/program-change messages only; sysex and realtime
    // clock/active-sensing bytes would just be extra queue entries to skip.
    g_midiIn->ignoreTypes(true, true, true);
    return 0;
  } catch (RtError&) {
    delete g_midiIn;
    g_midiIn = NULL;
    return -1;
  }
}

void midiIoCloseInput(void) {
  if (!g_midiIn) return;
  g_midiIn->closePort();
  delete g_midiIn;
  g_midiIn = NULL;
}

int midiIoIsInputOpen(void) { return g_midiIn != NULL; }

int midiIoPollInput(uint8_t* outStatus, uint8_t* outData1, uint8_t* outData2) {
  if (!g_midiIn) return 0;
  std::vector<unsigned char> message;
  // Skip anything that isn't a plain 2 or 3-byte channel message (sysex,
  // clock, etc.) rather than misreading its bytes as note/CC data.
  for (;;) {
    g_midiIn->getMessage(&message);
    if (message.empty()) return 0;
    if (message.size() == 2 || message.size() == 3) break;
  }
  *outStatus = message[0];
  *outData1 = message[1];
  *outData2 = message.size() == 3 ? message[2] : 0;
  return 1;
}

int midiIoOpenOutput(int portIndex) {
  midiIoCloseOutput();
  try {
    g_midiOut = new RtMidiOut();
    g_midiOut->openPort((unsigned int)portIndex, "ChooChooTracker Out");
    return 0;
  } catch (RtError&) {
    delete g_midiOut;
    g_midiOut = NULL;
    return -1;
  }
}

void midiIoCloseOutput(void) {
  if (!g_midiOut) return;
  g_midiOut->closePort();
  delete g_midiOut;
  g_midiOut = NULL;
}

int midiIoIsOutputOpen(void) { return g_midiOut != NULL; }

void midiIoSendMessage(uint8_t status, uint8_t data1, uint8_t data2) {
  if (!g_midiOut) return;
  std::vector<unsigned char> message;
  message.push_back(status);
  message.push_back(data1);
  message.push_back(data2);
  try {
    g_midiOut->sendMessage(&message);
  } catch (RtError&) {
    // Device unplugged mid-stream or similar: drop the message, keep playing.
  }
}

#else // !DESKTOP_BUILD

int midiIoAvailable(void) { return 0; }
int midiIoInputPortCount(void) { return 0; }
int midiIoInputPortName(int, char*, int) { return -1; }
int midiIoOutputPortCount(void) { return 0; }
int midiIoOutputPortName(int, char*, int) { return -1; }
int midiIoOpenInput(int) { return -1; }
void midiIoCloseInput(void) {}
int midiIoIsInputOpen(void) { return 0; }
int midiIoPollInput(uint8_t*, uint8_t*, uint8_t*) { return 0; }
int midiIoOpenOutput(int) { return -1; }
void midiIoCloseOutput(void) {}
int midiIoIsOutputOpen(void) { return 0; }
void midiIoSendMessage(uint8_t, uint8_t, uint8_t) {}

#endif
