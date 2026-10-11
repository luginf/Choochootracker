#ifndef __IMPORT_M8S_H__
#define __IMPORT_M8S_H__

#include "chipnomad_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

// Imports a Dirtywave M8 song (.m8s) as a new project. Only the structure
// and the notes are carried over: song rows, chains (with transpose) and
// phrases (note, velocity, instrument number) keep the same indices as in
// the M8 file; notes keep their real pitch (M8 note value = MIDI note). Every instrument used by a phrase becomes a default AY
// instrument carrying the M8 instrument's name - M8 instruments (FM,
// macrosynth, sampler...) have no equivalent here, so the user is expected
// to pick real sounds afterward. Effects, tables, grooves, mixer and the
// instruments' own parameters are ignored. The tempo is converted to the
// tick rate with the app's default 6-tick groove.
// The destination must be initialized with projectInit/projectInitAY first.
// Success releases its previous instrument data; failure leaves the
// destination unchanged and owned by the caller.
int projectLoadM8S(Project* project, const char* path);

#ifdef __cplusplus
}
#endif

#endif
