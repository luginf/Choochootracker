#ifndef __CHIPNOMAD_LIB__MIDI_IO_H__
#define __CHIPNOMAD_LIB__MIDI_IO_H__

#include <stdint.h>

// Realtime MIDI I/O (RtMidi-backed, see external/rtmidi). Only built with
// RtMidi on desktop builds (DESKTOP_BUILD): elsewhere every function is a
// harmless no-op that reports "nothing available"/"nothing sent", so
// callers never need their own #ifdef DESKTOP_BUILD.
//
// This is deliberately a thin, poll-based wrapper (no callbacks) to match
// how the rest of the engine is driven from a single tick, and it only
// tracks one open input port and one open output port at a time - a synth
// keyboard for auditioning sounds, and a device to drive from the tracker,
// not a multi-port MIDI patchbay.

// 1 if this build can actually talk to MIDI hardware, 0 otherwise.
int midiIoAvailable(void);

// --- Discovery ---
int midiIoInputPortCount(void);
// Returns 0 on success (name written to buffer, NUL-terminated), -1 on failure.
int midiIoInputPortName(int index, char* buffer, int bufferSize);
int midiIoOutputPortCount(void);
int midiIoOutputPortName(int index, char* buffer, int bufferSize);

// --- Input ---
// Opens portIndex, closing whatever input port was previously open. Returns
// 0 on success, -1 on failure (invalid index, device gone, driver error).
int midiIoOpenInput(int portIndex);
void midiIoCloseInput(void);
int midiIoIsInputOpen(void);
// Pops one pending channel message (note on/off, CC, program change...).
// Sysex and system realtime bytes are ignored/dropped. Returns 1 and fills
// outStatus/outData1/outData2 (outData2 is 0 for 2-byte messages) if a
// message was waiting, 0 if the queue is empty. Call in a loop (e.g. once
// per frame) to drain everything queued since the last poll.
int midiIoPollInput(uint8_t* outStatus, uint8_t* outData1, uint8_t* outData2);

// --- Output ---
int midiIoOpenOutput(int portIndex);
void midiIoCloseOutput(void);
int midiIoIsOutputOpen(void);
// Sends immediately, bypassing the scheduling queue below. Fine for one-off
// use; the audio thread should use midiIoScheduleMessage instead (see it for
// why).
void midiIoSendMessage(uint8_t status, uint8_t data1, uint8_t data2);

// Monotonic microsecond clock shared by producers (the audio thread, see
// below) and midiIoScheduleMessage's drain thread, so "due" comparisons use
// one consistent clock. Returns 0 outside desktop builds.
uint64_t midiIoNowMicros(void);

// Queues a MIDI message to be sent at dueMicros (midiIoNowMicros() scale),
// returned immediately without touching the OS/driver. One audio callback
// can compute several tracker rows' worth of events for what will actually
// play out over tens of milliseconds; sending them with midiIoSendMessage()
// right away from the audio thread would fire them all in a burst (however
// many rows landed in that callback) and then send nothing until the next
// callback - audibly irregular timing on the receiving device. A dedicated
// low-latency thread (started when the output port opens) drains this queue
// and calls midiIoSendMessage() only once each entry's due time arrives, so
// the audio thread only ever enqueues (cheap, no blocking OS call) and MIDI
// pacing is decoupled from the audio buffer size.
void midiIoScheduleMessage(uint8_t status, uint8_t data1, uint8_t data2, uint64_t dueMicros);

#endif // __CHIPNOMAD_LIB__MIDI_IO_H__
