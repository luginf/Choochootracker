#include "midi_io.h"

#ifdef DESKTOP_BUILD

#include "external/rtmidi/RtMidi.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>

static RtMidiIn* g_midiIn = NULL;
static RtMidiOut* g_midiOut = NULL;

// Program Change and Channel Pressure are 2-byte channel voice messages;
// every other one we send (Note On/Off, CC) is 3 bytes. Sending a spurious
// 3rd byte after a Program Change would be read as the start of an
// unrelated running-status data byte by some receivers.
static int channelMessageLength(uint8_t status) {
  uint8_t type = status & 0xf0;
  return (type == 0xC0 || type == 0xD0) ? 2 : 3;
}

uint64_t midiIoNowMicros(void) {
  return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

// See midi_io.h's midiIoScheduleMessage for why this queue+thread exist.
// Single producer (the audio thread), single consumer (drainThreadLoop), so
// this plain head/tail ring buffer needs no lock - same pattern as
// chipnomad_lib.cpp's own AudioCommandQueue.
struct ScheduledMidiMessage { uint8_t status, data1, data2; uint64_t dueMicros; };
static constexpr unsigned int kMidiOutQueueCapacity = 512;
static ScheduledMidiMessage g_midiOutQueue[kMidiOutQueueCapacity];
static std::atomic<unsigned int> g_midiOutHead{0};
static std::atomic<unsigned int> g_midiOutTail{0};
static std::atomic<bool> g_midiOutThreadRunning{false};
static std::thread* g_midiOutThread = NULL;

void midiIoScheduleMessage(uint8_t status, uint8_t data1, uint8_t data2, uint64_t dueMicros) {
  unsigned int head = g_midiOutHead.load(std::memory_order_relaxed);
  unsigned int next = (head + 1) % kMidiOutQueueCapacity;
  if (next == g_midiOutTail.load(std::memory_order_acquire)) return; // full: drop rather than block the audio thread
  g_midiOutQueue[head] = {status, data1, data2, dueMicros};
  g_midiOutHead.store(next, std::memory_order_release);
}

// Polls at ~1ms resolution - far tighter than an audio callback (which can
// represent tens of milliseconds of "musical time" computed all at once)
// without needing sample-accurate OS scheduling support that RtMidi doesn't
// offer.
static void midiOutDrainThreadLoop() {
  while (g_midiOutThreadRunning.load(std::memory_order_relaxed)) {
    unsigned int tail = g_midiOutTail.load(std::memory_order_relaxed);
    uint64_t now = midiIoNowMicros();
    while (tail != g_midiOutHead.load(std::memory_order_acquire) && g_midiOutQueue[tail].dueMicros <= now) {
      ScheduledMidiMessage msg = g_midiOutQueue[tail];
      if (g_midiOut) {
        std::vector<unsigned char> bytes = {msg.status, msg.data1, msg.data2};
        bytes.resize(channelMessageLength(msg.status));
        try { g_midiOut->sendMessage(&bytes); } catch (RtError&) {}
      }
      tail = (tail + 1) % kMidiOutQueueCapacity;
      g_midiOutTail.store(tail, std::memory_order_release);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

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
    g_midiOutHead.store(0, std::memory_order_relaxed);
    g_midiOutTail.store(0, std::memory_order_relaxed);
    g_midiOutThreadRunning.store(true, std::memory_order_relaxed);
    g_midiOutThread = new std::thread(midiOutDrainThreadLoop);
    return 0;
  } catch (RtError&) {
    delete g_midiOut;
    g_midiOut = NULL;
    return -1;
  }
}

void midiIoCloseOutput(void) {
  // Stop and join the drain thread before touching g_midiOut, so it can
  // never run against a port that's mid-close/deleted.
  if (g_midiOutThreadRunning.exchange(false, std::memory_order_relaxed)) {
    if (g_midiOutThread) { g_midiOutThread->join(); delete g_midiOutThread; g_midiOutThread = NULL; }
  }
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
  if (channelMessageLength(status) == 3) message.push_back(data2);
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
uint64_t midiIoNowMicros(void) { return 0; }
void midiIoScheduleMessage(uint8_t, uint8_t, uint8_t, uint64_t) {}

#endif
