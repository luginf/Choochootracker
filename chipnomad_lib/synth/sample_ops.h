#ifndef CHOOCHOO_SAMPLE_OPS_H
#define CHOOCHOO_SAMPLE_OPS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "project_instruments.h"

// Destructive sample-processing operations for the sample editor (and
// reusable by SCWF oscillators / a future slicing engine). Pure functions
// over InstrumentSample: no UI, no audio-manager calls - suspending the
// audio callback is the caller's duty.
//
// Argument convention: selStart == selEnd means "whole sample" (resolved to
// [0, frameCount) internally); inverted ranges are swapped; bounds are
// clamped to [0, frameCount]. Only Delete/Crop change the buffer length;
// the others edit in place. Ops never touch sample->path; the editor
// selection is screen state and is cleared by the caller where noted.

// Result codes: 0 ok; nonzero = typed error (the screen maps them to
// message strings).
enum {
  sampleOpOk = 0,
  sampleOpErrorNoSample = 1,    // no buffer or zero frames
  sampleOpErrorRange = 2,       // empty range after clamping
  sampleOpErrorWholeSample = 3, // delete would leave no frames
  sampleOpErrorMemory = 4,      // allocation failure
  sampleOpErrorNoUndo = 5,      // undo slot is empty
};

// Inverses of the shared sampleMarkerToStartFrame / sampleMarkerToEndFrame
// conversions (project_instruments.h). The mapping is quantized, so these
// return the marker whose forward frame is closest to (and for the end
// marker not before) the given frame.
static inline uint8_t sampleFrameToStartMarker(uint32_t frameCount, uint32_t frame) {
  if (frameCount < 2) return 0;
  if (frame >= frameCount - 1) return 255;
  return (uint8_t)(((uint64_t)frame * 255 + (frameCount - 1) / 2) / (frameCount - 1));
}

static inline uint8_t sampleFrameToEndMarker(uint32_t frameCount, uint32_t frame) {
  if (frameCount == 0 || frame == 0) return 0;
  if (frame >= frameCount) return 255;
  uint64_t e = ((uint64_t)frame * 256 + frameCount - 1) / frameCount;
  if (e <= 1) return 0;
  return (uint8_t)(e - 1 > 255 ? 255 : e - 1);
}

// Keep [selStart, selEnd) of the sample, dropping everything before and
// after. Playback start/end markers are remapped through the shared
// 0-255 <-> frame conversions. A full-sample crop is a no-op.
int sampleOpCrop(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);

// Scale the selection so its peak reaches full scale (32767). The peak is
// scanned across both channels; both channels of a frame share one gain so
// the stereo image is preserved. A silent selection is a no-op.
int sampleOpNormalize(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);

// Remove [selStart, selEnd) and join the tails. Rejects a selection that
// covers the whole sample (at least one frame must remain). Markers inside
// the removed span clamp to selStart; markers after it shift by the removed
// length.
int sampleOpDelete(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);

// Zero every sample in the selection.
int sampleOpSilence(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);

// Linear ramp across the selection: fadeIn multiplies frame i by i/len,
// fade out by (len-1-i)/len. Both channels share the per-frame gain.
int sampleOpFade(InstrumentSample* s, uint32_t selStart, uint32_t selEnd, int fadeIn);

// Reverse the frame order of the selection (play it backwards). In-place
// frame swaps with no allocation; both channels of a frame move together so
// the stereo image is preserved. Length and markers are unchanged.
int sampleOpReverse(InstrumentSample* s, uint32_t selStart, uint32_t selEnd);

// One-level undo. The slot lives in the editor screen module state, not in
// the engine or the project. Depth-1 semantics: preparing again overwrites
// the slot, and applying swaps current state with the slot contents, so
// alternating GO/UNDO toggles the two states rather than growing history.
typedef struct SampleUndo {
  int active;              // 0 = empty
  int16_t* data;           // deep copy of the pre-op buffer
  InstrumentSample header; // full struct copy EXCEPT data
} SampleUndo;

// Deep-copies the sample into the slot (overwriting any previous contents).
// Call before the first op of a user action.
int sampleOpPrepareUndo(InstrumentSample* s, SampleUndo* slot);

// Swaps the sample with the slot contents (buffer pointers and headers).
// The slot stays active: the next apply toggles back. Run under audio
// pause/resume - the callback may be reading the buffer.
int sampleOpApplyUndo(InstrumentSample* s, SampleUndo* slot);

// Releases the slot's buffer (screen setup / new sample load).
void sampleOpFreeUndo(SampleUndo* slot);

#endif
