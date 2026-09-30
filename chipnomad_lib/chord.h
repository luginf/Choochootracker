#ifndef __CHIPNOMAD_LIB__CHORD_H__
#define __CHIPNOMAD_LIB__CHORD_H__

#include <stdint.h>

constexpr int CHORD_MAX_VOICES = 4;

int chordBuild(uint8_t root, uint8_t slot, uint8_t inversion, uint8_t pitchCount,
               uint8_t pitches[CHORD_MAX_VOICES]);
uint8_t chordMaxInversion(uint8_t slot);
const char* chordName(uint8_t slot);

#endif
