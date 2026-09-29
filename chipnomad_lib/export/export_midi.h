#ifndef __CHIPNOMAD_LIB__EXPORT_EXPORT_MIDI_H__
#define __CHIPNOMAD_LIB__EXPORT_EXPORT_MIDI_H__

#include "../project.h"

// Exports a project's arrangement (notes, volume, tempo/groove) as a
// Standard MIDI File (one MIDI track/channel per tracker track, plus a
// conductor track carrying the tempo map).
//
// Known limitations (v1):
// - Per-row FX other than volume and the global groove (fxGGR) are not
//   translated: arpeggio, pitch slides, per-track groove (fxGRV), synth
//   parameters, sample tables, etc. have no MIDI equivalent and are ignored.
// - Song/chain arrangement jump effects (fxHOP, fxSNG, fxTHO, fxTXH...) are
//   not honored; the arrangement is walked linearly, chain row by chain row,
//   phrase row by phrase row.
// - Pitch mapping assumes the project's PitchTable is close to standard
//   12-TET (index N -> MIDI note 12+N, matching how the app itself builds
//   its default 12TET tables). Microtonal/custom tables are approximated to
//   the nearest semitone.
// - The instrument actually driving a note has no General MIDI equivalent;
//   each MIDI track gets a fixed program (0) and the tracker instrument name
//   is written as a text meta-event for reference only.
//
// Returns 0 on success. On failure, see smfFileError (chipnomad_lib/midi/smf_file.h).
int projectExportMidi(Project* project, const char* path);

#endif // __CHIPNOMAD_LIB__EXPORT_EXPORT_MIDI_H__
