#include "smf_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char smfFileError[128];

// --- Writing ---

void smfTrackInit(SmfTrackWriter* track) {
  track->capacity = 256;
  track->data = (uint8_t*)malloc(track->capacity);
  track->length = 0;
}

void smfTrackFree(SmfTrackWriter* track) {
  free(track->data);
  track->data = NULL;
  track->length = 0;
  track->capacity = 0;
}

static void smfTrackReserve(SmfTrackWriter* track, uint32_t extra) {
  if (track->length + extra <= track->capacity) return;
  while (track->length + extra > track->capacity) track->capacity *= 2;
  track->data = (uint8_t*)realloc(track->data, track->capacity);
}

static void smfTrackWriteByte(SmfTrackWriter* track, uint8_t b) {
  smfTrackReserve(track, 1);
  track->data[track->length++] = b;
}

static void smfTrackWriteBytes(SmfTrackWriter* track, const uint8_t* bytes, uint32_t len) {
  smfTrackReserve(track, len);
  memcpy(track->data + track->length, bytes, len);
  track->length += len;
}

static void smfTrackWriteVarLen(SmfTrackWriter* track, uint32_t value) {
  uint8_t buffer[5];
  int count = 0;
  buffer[count++] = value & 0x7f;
  value >>= 7;
  while (value > 0) {
    buffer[count++] = (value & 0x7f) | 0x80;
    value >>= 7;
  }
  // Bytes were built least-significant-group-first; write them out reversed.
  for (int i = count - 1; i >= 0; i--) smfTrackWriteByte(track, buffer[i]);
}

void smfTrackNoteOn(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t channel, uint8_t note, uint8_t velocity) {
  smfTrackWriteVarLen(track, deltaTicks);
  smfTrackWriteByte(track, 0x90 | (channel & 0x0f));
  smfTrackWriteByte(track, note & 0x7f);
  smfTrackWriteByte(track, velocity & 0x7f);
}

void smfTrackNoteOff(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t channel, uint8_t note, uint8_t velocity) {
  smfTrackWriteVarLen(track, deltaTicks);
  smfTrackWriteByte(track, 0x80 | (channel & 0x0f));
  smfTrackWriteByte(track, note & 0x7f);
  smfTrackWriteByte(track, velocity & 0x7f);
}

void smfTrackProgramChange(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t channel, uint8_t program) {
  smfTrackWriteVarLen(track, deltaTicks);
  smfTrackWriteByte(track, 0xc0 | (channel & 0x0f));
  smfTrackWriteByte(track, program & 0x7f);
}

void smfTrackTempo(SmfTrackWriter* track, uint32_t deltaTicks, uint32_t microsecondsPerQuarter) {
  smfTrackWriteVarLen(track, deltaTicks);
  smfTrackWriteByte(track, 0xff);
  smfTrackWriteByte(track, 0x51);
  smfTrackWriteByte(track, 0x03);
  smfTrackWriteByte(track, (microsecondsPerQuarter >> 16) & 0xff);
  smfTrackWriteByte(track, (microsecondsPerQuarter >> 8) & 0xff);
  smfTrackWriteByte(track, microsecondsPerQuarter & 0xff);
}

void smfTrackText(SmfTrackWriter* track, uint32_t deltaTicks, uint8_t metaType, const char* text) {
  uint32_t len = (uint32_t)strlen(text);
  smfTrackWriteVarLen(track, deltaTicks);
  smfTrackWriteByte(track, 0xff);
  smfTrackWriteByte(track, metaType);
  smfTrackWriteVarLen(track, len);
  smfTrackWriteBytes(track, (const uint8_t*)text, len);
}

void smfTrackEnd(SmfTrackWriter* track, uint32_t deltaTicks) {
  smfTrackWriteVarLen(track, deltaTicks);
  smfTrackWriteByte(track, 0xff);
  smfTrackWriteByte(track, 0x2f);
  smfTrackWriteByte(track, 0x00);
}

static void writeUint16BE(FILE* f, uint16_t value) {
  fputc((value >> 8) & 0xff, f);
  fputc(value & 0xff, f);
}

static void writeUint32BE(FILE* f, uint32_t value) {
  fputc((value >> 24) & 0xff, f);
  fputc((value >> 16) & 0xff, f);
  fputc((value >> 8) & 0xff, f);
  fputc(value & 0xff, f);
}

int smfWriteFile(const char* path, uint16_t ppq, SmfTrackWriter* tracks, int trackCount) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    snprintf(smfFileError, sizeof(smfFileError), "Could not open '%s' for writing", path);
    return 1;
  }

  fwrite("MThd", 1, 4, f);
  writeUint32BE(f, 6);
  writeUint16BE(f, trackCount > 1 ? 1 : 0);
  writeUint16BE(f, (uint16_t)trackCount);
  writeUint16BE(f, ppq);

  for (int i = 0; i < trackCount; i++) {
    fwrite("MTrk", 1, 4, f);
    writeUint32BE(f, tracks[i].length);
    fwrite(tracks[i].data, 1, tracks[i].length, f);
  }

  fclose(f);
  return 0;
}

// --- Reading ---

static void noteListAdd(SmfNoteEventList* list, uint64_t absoluteTick, SmfEventType type, uint8_t note, uint8_t velocity) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 64 : list->capacity * 2;
    list->events = (SmfNoteEvent*)realloc(list->events, list->capacity * sizeof(SmfNoteEvent));
  }
  SmfNoteEvent* e = &list->events[list->count++];
  e->absoluteTick = absoluteTick;
  e->type = type;
  e->note = note;
  e->velocity = velocity;
}

static void tempoListAdd(SmfTempoChangeList* list, uint64_t absoluteTick, uint32_t microsecondsPerQuarter) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    list->changes = (SmfTempoChange*)realloc(list->changes, list->capacity * sizeof(SmfTempoChange));
  }
  SmfTempoChange* c = &list->changes[list->count++];
  c->absoluteTick = absoluteTick;
  c->microsecondsPerQuarter = microsecondsPerQuarter;
}

void smfFreeReadResult(SmfReadResult* result) {
  for (int i = 0; i < SMF_MAX_CHANNELS; i++) {
    free(result->channels[i].events);
    result->channels[i].events = NULL;
    result->channels[i].count = 0;
    result->channels[i].capacity = 0;
  }
  free(result->tempoChanges.changes);
  result->tempoChanges.changes = NULL;
  result->tempoChanges.count = 0;
  result->tempoChanges.capacity = 0;
}

static int readUint16BE(FILE* f, uint16_t* out) {
  int hi = fgetc(f), lo = fgetc(f);
  if (hi == EOF || lo == EOF) return 1;
  *out = (uint16_t)((hi << 8) | lo);
  return 0;
}

static int readUint32BE(FILE* f, uint32_t* out) {
  int b0 = fgetc(f), b1 = fgetc(f), b2 = fgetc(f), b3 = fgetc(f);
  if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF) return 1;
  *out = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
  return 0;
}

// Reads a variable-length quantity from a memory buffer, advancing *pos.
static uint32_t readVarLen(const uint8_t* data, uint32_t length, uint32_t* pos) {
  uint32_t value = 0;
  while (*pos < length) {
    uint8_t b = data[(*pos)++];
    value = (value << 7) | (b & 0x7f);
    if (!(b & 0x80)) break;
  }
  return value;
}

int smfReadFile(const char* path, SmfReadResult* result) {
  memset(result, 0, sizeof(*result));

  FILE* f = fopen(path, "rb");
  if (!f) {
    snprintf(smfFileError, sizeof(smfFileError), "Could not open '%s'", path);
    return 1;
  }

  char chunkId[5] = {0};
  if (fread(chunkId, 1, 4, f) != 4 || strcmp(chunkId, "MThd") != 0) {
    snprintf(smfFileError, sizeof(smfFileError), "Not a MIDI file (missing MThd)");
    fclose(f);
    return 1;
  }

  uint32_t headerLength;
  uint16_t format, trackCount, division;
  if (readUint32BE(f, &headerLength) || readUint16BE(f, &format) ||
      readUint16BE(f, &trackCount) || readUint16BE(f, &division)) {
    snprintf(smfFileError, sizeof(smfFileError), "Truncated MIDI header");
    fclose(f);
    return 1;
  }
  if (division & 0x8000) {
    snprintf(smfFileError, sizeof(smfFileError), "SMPTE time division is not supported");
    fclose(f);
    return 1;
  }
  result->ppq = division;
  // Skip any extra header bytes some writers add beyond the standard 6.
  fseek(f, headerLength - 6, SEEK_CUR);

  for (int t = 0; t < trackCount; t++) {
    if (fread(chunkId, 1, 4, f) != 4) {
      snprintf(smfFileError, sizeof(smfFileError), "Truncated MIDI file (missing track %d)", t);
      fclose(f);
      smfFreeReadResult(result);
      return 1;
    }
    uint32_t trackLength;
    if (readUint32BE(f, &trackLength)) {
      snprintf(smfFileError, sizeof(smfFileError), "Truncated MIDI file (track %d length)", t);
      fclose(f);
      smfFreeReadResult(result);
      return 1;
    }
    if (strcmp(chunkId, "MTrk") != 0) {
      // Unknown chunk type: skip it entirely.
      fseek(f, trackLength, SEEK_CUR);
      continue;
    }

    uint8_t* data = (uint8_t*)malloc(trackLength);
    if (fread(data, 1, trackLength, f) != trackLength) {
      snprintf(smfFileError, sizeof(smfFileError), "Truncated MIDI file (track %d data)", t);
      free(data);
      fclose(f);
      smfFreeReadResult(result);
      return 1;
    }

    uint32_t pos = 0;
    uint64_t absoluteTick = 0;
    uint8_t runningStatus = 0;
    while (pos < trackLength) {
      absoluteTick += readVarLen(data, trackLength, &pos);
      if (pos >= trackLength) break;

      uint8_t status = data[pos];
      if (status & 0x80) {
        pos++;
        runningStatus = status;
      } else {
        status = runningStatus;
      }

      uint8_t hiNibble = status & 0xf0;
      uint8_t channel = status & 0x0f;

      if (status == 0xff) {
        // Meta event: type byte, then a varlen length, then the payload.
        uint8_t metaType = data[pos++];
        uint32_t len = readVarLen(data, trackLength, &pos);
        if (metaType == 0x51 && len == 3) {
          uint32_t microsecondsPerQuarter = ((uint32_t)data[pos] << 16) | ((uint32_t)data[pos + 1] << 8) | data[pos + 2];
          tempoListAdd(&result->tempoChanges, absoluteTick, microsecondsPerQuarter);
        }
        pos += len;
      } else if (status == 0xf0 || status == 0xf7) {
        // Sysex event: a varlen length, then the payload.
        uint32_t len = readVarLen(data, trackLength, &pos);
        pos += len;
      } else if (hiNibble == 0x90 || hiNibble == 0x80) {
        uint8_t note = data[pos++];
        uint8_t velocity = data[pos++];
        // A "note on" with velocity 0 is conventionally a note off.
        SmfEventType type = (hiNibble == 0x90 && velocity > 0) ? SmfEventType::noteOn : SmfEventType::noteOff;
        noteListAdd(&result->channels[channel], absoluteTick, type, note, velocity);
      } else if (hiNibble == 0xc0 || hiNibble == 0xd0) {
        pos += 1; // Program change / channel pressure: one data byte.
      } else {
        pos += 2; // Note aftertouch, control change, pitch bend: two data bytes.
      }
    }

    free(data);
  }

  fclose(f);
  return 0;
}
