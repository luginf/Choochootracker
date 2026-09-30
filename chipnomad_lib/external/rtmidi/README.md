# RtMidi

Version 1.0.8, vendored unmodified from ChooChooTracker's own
`luginf/LittleGPTracker` fork (`sources/Externals/RtMidi`), which has used
this exact copy across the same desktop targets ChooChooTracker builds for
(Linux/ALSA, macOS/CoreMIDI, Windows/WinMM).

Enabled per platform via compile-time defines, matching the upstream
project's own Makefiles:

- Linux: `-D__LINUX_ALSA__ -D__LINUX_ALSASEQ__`, link `-lasound`.
- macOS: `-D__MACOSX_CORE__`, link `-framework CoreMIDI -framework CoreAudio
  -framework CoreFoundation`.
- Windows: `-D__WINDOWS_MM__`, link `-lwinmm`.

See `chipnomad_lib/external/rtmidi/RtMidi.h` for the public API
(`RtMidiIn`/`RtMidiOut`) and `docs/build-notes.md` for how ChooChooTracker
wires this into its own MIDI I/O layer (`tracker/src/corelib/corelib_midi.h`).
