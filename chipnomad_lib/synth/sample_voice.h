#ifndef CHOOCHOO_SAMPLE_VOICE_H
#define CHOOCHOO_SAMPLE_VOICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "voice_post_processor.h"
#include "stretch_processor.h"

struct InstrumentSample;

class SampleVoice {
 public:
  void init(float outputSampleRate);
  void configure(const InstrumentSample* sample, float pitchCents, float gain,
                 float speedPercent, uint8_t start, uint8_t end, uint8_t loopMode, uint16_t cutoffHz,
                 uint8_t resonance, int attack = -1, int decay = -1, int sustain = -1,
                 int release = -1, int envelopeShape = -1, uint8_t sliceCount = 0,
                 uint8_t sliceIndex = 0, uint8_t stretchMode = 0, float tickRateHz = 50.0f,
                 uint8_t speedAlgorithm = 0);
  void noteOn();
  void noteOff();
  void kill();
  void render(float* output, size_t frames);
  bool active() const { return active_; }
  float envelopeLevel() const { return post_.envelopeLevel(); }

 private:
  const InstrumentSample* sample_;
  float outputSampleRate_;
  double position_;
  double step_;
  int direction_;
  bool reverse_;
  uint8_t loopMode_;
  float timeStretch_;
  bool granular_;
  bool grainExhausted_;
  double grainPosition_[2];
  double nextGrainPosition_;
  uint32_t grainAge_[2];
  uint32_t grainSize_;
  uint32_t grainHop_;
  uint32_t startFrame_;
  uint32_t endFrame_;
  bool active_;
  StretchProcessor stretch_;
  bool useStretch_;
  bool advancePosition();
  float sampleAt(double position, int channel) const;
  float grainSampleAt(double position, int channel) const;
  VoicePostProcessor<> post_;
};

uint8_t sampleNormalizeSlice(uint8_t slice);
void sampleSliceFrames(uint32_t frameCount, uint8_t sliceCount, uint8_t sliceIndex,
                       uint32_t* startFrame, uint32_t* endFrame);
int sampleLoadWav16(const char* path, InstrumentSample* sample,
                    char* error, size_t errorSize);
int sampleLoadWav16File(FILE* file, const char* path, InstrumentSample* sample,
                        char* error, size_t errorSize);

// Writes the sample as an uncompressed 16-bit PCM WAV (44-byte RIFF header,
// little-endian fields written byte-wise so the code is endian-agnostic).
// Returns 0 on success, 1 on failure with a message in error. The sample is
// not modified; markers and path are the caller's business.
int sampleSaveWav16(const InstrumentSample* sample, const char* path,
                    char* error, size_t errorSize);

#endif
