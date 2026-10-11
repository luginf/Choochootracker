#ifndef __CHIPNOMAD_LIB__M8S_FORMAT_H__
#define __CHIPNOMAD_LIB__M8S_FORMAT_H__

// Layout of a Dirtywave M8 song file (.m8s), firmware 2.x - 4.x. The song,
// chain, phrase and instrument tables sit at the same fixed offsets in all
// these versions (see also AlexCharlton/m8-files). Only what the importer and
// the exporter touch is described here.

#define M8S_MAGIC "M8VERSION"
#define M8S_HEADER_SIZE 14
#define M8S_VERSION_LSB_OFFSET 10
#define M8S_VERSION_MSB_OFFSET 11
#define M8S_DIRECTORY_SIZE 128
#define M8S_TRANSPOSE_OFFSET (M8S_HEADER_SIZE + M8S_DIRECTORY_SIZE)
#define M8S_TEMPO_OFFSET (M8S_TRANSPOSE_OFFSET + 1) // little-endian float, BPM
#define M8S_NAME_OFFSET (M8S_TEMPO_OFFSET + 5)
#define M8S_NAME_SIZE 12
#define M8S_SONG_OFFSET 0x2EE
#define M8S_PHRASES_OFFSET 0xAEE
#define M8S_CHAINS_OFFSET 0x9A5E
#define M8S_INSTRUMENTS_OFFSET 0x13A3E
#define M8S_SONG_ROWS 256
#define M8S_TRACKS 8
#define M8S_PHRASES 255
#define M8S_PHRASE_STEPS 16
#define M8S_PHRASE_STEP_SIZE 9 // note, velocity, instrument, 3 x (fx command, fx value)
#define M8S_CHAINS 255
#define M8S_CHAIN_STEPS 16
#define M8S_INSTRUMENTS 128
#define M8S_INSTRUMENT_SIZE 215
#define M8S_INSTRUMENT_NAME_SIZE 12
#define M8S_EMPTY 0xFF
#define M8S_NOTE_OFF 0x80
#define M8S_MIN_FILE_SIZE (M8S_INSTRUMENTS_OFFSET + M8S_INSTRUMENTS * M8S_INSTRUMENT_SIZE)
#define M8S_MAX_FILE_SIZE (4 * 1024 * 1024)
#define M8S_STEPS_PER_BEAT 4
#define M8S_GROOVE_TICKS 6 // the app's default groove (see project.cpp projectInit)

#endif // __CHIPNOMAD_LIB__M8S_FORMAT_H__
