# Dirtywave M8 songs (.m8s)

ChooChooTracker can import an M8 song and export a limited M8 song. Only the
song structure and the notes are handled: the M8 has instruments, tables,
mixer and effect settings that have no equivalent here. This page describes
what is read, what is written, and the file layout, for anyone maintaining the
code or checking a conversion.

- Import: `tracker/src/import/import_m8s.cpp` (`projectLoadM8S`), reached from
  **Project > Load**.
- Export: `chipnomad_lib/export/export_m8s.cpp` (`projectExportM8S`), reached
  from the **M8S** row of the Export screen. That row exists in desktop builds
  only (`DESKTOP_BUILD`): picking a template file is impractical on mobile,
  web and handheld targets. The export code itself is compiled everywhere.
  The import is available on every platform.
- Shared layout constants: `chipnomad_lib/m8s_format.h`.
- Tests: `tracker/tests/test_import_m8s.cpp`.

## What is converted

| M8 | ChooChooTracker |
| --- | --- |
| Song rows (256 x 8 tracks) | Song rows, same row and track, same chain number |
| Chains (255 x 16 steps) | Chains, same number; phrase and transpose per step |
| Phrases (255 x 16 steps) | Phrases, same number; note, velocity, instrument number |
| Song name (12 chars) | Project title |
| Tempo (BPM) | Tick rate, assuming the default 6-tick groove: `tickRate = 2.5 x BPM` (120 BPM is 48 ticks/s) |
| Note | Pitch table index `note - 12` (see below) |
| Note off (`0x80` and above) | Note off |
| Velocity (`0xFF` = empty) | Phrase volume, clamped to 127 |
| Instrument | Instrument slot with the same number (import: see below) |

Not converted, in either direction: phrase FX, tables, grooves, scales, EQ,
mixer, effect settings, MIDI mapping, and every instrument parameter.

### Pitch

An M8 note value is a MIDI note number (value 0 is displayed as `C-1`).
Pitch table index N of the default tables is MIDI note 12 + N, the same rule
`import_midi.cpp` and `export_midi.cpp` use. So notes keep their real pitch:
import subtracts 12, export adds 12. M8 notes below 12 (under 16 Hz) clamp to
the lowest index.

## Import

- Accepts firmware 2.x to 4.x. Other versions, bad magic or a short file are
  rejected with an on-screen message.
- Every instrument referenced by a phrase becomes a default AY instrument in
  the same slot, named after the M8 instrument (or `M8 xx` when it has no
  name). Pick real sounds afterward.
- The song-level transpose of the M8 is ignored.

## Export

The M8 file is built from a **template**: an existing `.m8s` picked by the
user. The song rows, chains, phrases, tempo, song transpose (reset to 0) and
title of the template are overwritten; every other byte, including all
instruments, is copied untouched. This keeps the file loadable without
reimplementing every M8 section. The template is remembered for the session;
**EDIT + OPT** on the row forgets it.

- Phrase FX columns are written empty.
- Instrument numbers are exported as they are and play whatever the template
  holds in those slots.
- The M8 has 255 phrases: a song whose chains reference phrase 255 or higher
  fails to export rather than losing notes.
- An exported file has not been verified on real M8 hardware.

## File layout used

All offsets are from the start of the file and are the same in firmware 2.x
to 4.x for the parts below.

| Offset | Size | Content |
| --- | --- | --- |
| `0x00` | 10 | `M8VERSION\0` |
| `0x0A` | 2 | Version: byte 0 = `(minor << 4) \| patch`, byte 1 low nibble = major |
| `0x0E` | 128 | Song directory (text) |
| `0x8E` | 1 | Song transpose |
| `0x8F` | 4 | Tempo, little-endian float, BPM |
| `0x93` | 1 | Quantize |
| `0x94` | 12 | Song name |
| `0x2EE` | 2048 | Song: 256 rows x 8 tracks, chain number, `0xFF` empty |
| `0xAEE` | 36720 | Phrases: 255 x 16 steps x 9 bytes (note, velocity, instrument, 3 x FX command + value) |
| `0x9A5E` | 8160 | Chains: 255 x 16 steps x 2 bytes (phrase, transpose; `0xFF` phrase = empty). Transpose is a signed semitone offset |
| `0x13A3E` | 27520 | Instruments: 128 x 215 bytes; byte 0 is the type, the name starts at byte 1 (12 chars) |

Empty note, velocity and instrument are `0xFF`. An empty FX command is `0xFF`.

## References

The layout was worked out from sample files and checked against two
independent parsers:

- [AlexCharlton/m8-files](https://github.com/AlexCharlton/m8-files) (Rust):
  offsets, version decoding, step layout, 215-byte instruments.
- [whitlockjc/m8-js](https://github.com/whitlockjc/m8-js) (JavaScript).

[laamaa/m8c](https://github.com/laamaa/m8c) is a client for the M8 hardware
over USB and does not read song files.
