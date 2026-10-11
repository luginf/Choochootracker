#include "sample_voice.h"

#include "../project_instruments.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void sampleStorePath(const char* path, InstrumentSample* sample) {
  char workingDirectory[1024];
  const char* storedPath = path;
  if (getcwd(workingDirectory, sizeof(workingDirectory))) {
    size_t length = strlen(workingDirectory);
#ifdef _WIN32
    if (strlen(path) > length && _strnicmp(path, workingDirectory, length) == 0 &&
#else
    if (strlen(path) > length && strncmp(path, workingDirectory, length) == 0 &&
#endif
        (path[length] == '/' || path[length] == '\\')) {
      storedPath = path + length + 1;
    }
  }
  strncpy(sample->path, storedPath, sizeof(sample->path) - 1);
  sample->path[sizeof(sample->path) - 1] = 0;
}

static float envelopeTime(uint8_t value) {
  float normalized = value / 255.0f;
  return normalized * normalized * 5.0f;
}

void SampleVoice::init(float outputSampleRate) {
  sample_ = nullptr;
  outputSampleRate_ = outputSampleRate;
  position_ = 0.0;
  step_ = 1.0;
  direction_ = 1;
  reverse_ = false;
  loopMode_ = 0;
  timeStretch_ = 1.0f;
  granular_ = false;
  grainExhausted_ = false;
  startFrame_ = 0;
  endFrame_ = 0;
  active_ = false;
  useStretch_ = false;
#if defined(PORTMASTER_BUILD) || defined(ANDROID_BUILD) || defined(WEB_BUILD)
  stretch_.init(outputSampleRate_, true);
#else
  stretch_.init(outputSampleRate_, false);
#endif
  post_.init(outputSampleRate_);
}

uint8_t sampleNormalizeSlice(uint8_t slice) {
  if (slice == 2 || slice == 4 || slice == 8 || slice == 16 || slice == 32) return slice;
  return 0;
}

void sampleSliceFrames(uint32_t frameCount, uint8_t sliceCount, uint8_t sliceIndex,
                       uint32_t* startFrame, uint32_t* endFrame) {
  if (!startFrame || !endFrame) return;
  if (frameCount == 0 || sliceCount == 0) {
    *startFrame = 0;
    *endFrame = frameCount;
    return;
  }
  if (sliceIndex >= sliceCount) sliceIndex = sliceCount - 1;
  *startFrame = (uint32_t)((uint64_t)sliceIndex * frameCount / sliceCount);
  *endFrame = (uint32_t)((uint64_t)(sliceIndex + 1) * frameCount / sliceCount);
  if (*endFrame <= *startFrame) *endFrame = *startFrame + 1;
  if (*endFrame > frameCount) *endFrame = frameCount;
}

void SampleVoice::configure(const InstrumentSample* sample, float pitchCents,
                            float gain, float speedPercent, uint8_t start, uint8_t end, uint8_t loopMode,
                            uint16_t cutoffHz, uint8_t resonance, int attack, int decay,
                            int sustain, int release, int envelopeShape, uint8_t sliceCount,
                            uint8_t sliceIndex, uint8_t stretchMode, float tickRateHz,
                            uint8_t speedAlgorithm) {
  sample_ = sample;
  post_.setGain(gain);
  if (!sample_ || !sample_->data || sample_->frameCount == 0) return;

  // Stretch mode bypasses the granular speedPercent path entirely: the
  // stretcher applies pitch via transpose and drives its own source cursor.
  useStretch_ = sliceCount == 0 &&
    (stretchMode != 0 || (speedAlgorithm == 1 && speedPercent != 0));
  if (useStretch_) {
    stretch_.configure(sample, stretchMode, speedPercent, tickRateHz, pitchCents / 100.0f, start, end);
  }

  uint32_t startFrame;
  uint32_t endFrame;
  sliceCount = sampleNormalizeSlice(sliceCount);
  if (sliceCount) {
    // When slicing is enabled, divide the LOOP REGION (start to end) into slices
    // This ensures slices follow the start/end markers
    uint32_t loopStartFrame = sampleMarkerToStartFrame(sample_->frameCount, start);
    uint32_t loopEndFrame = sampleMarkerToEndFrame(sample_->frameCount, end);
    if (loopStartFrame > loopEndFrame) {
      uint32_t swap = loopStartFrame;
      loopStartFrame = loopEndFrame;
      loopEndFrame = swap + 1;
    }
    uint32_t loopLength = loopEndFrame > loopStartFrame ? loopEndFrame - loopStartFrame : sample_->frameCount;

    uint32_t sliceStart, sliceEnd;
    sampleSliceFrames(loopLength, sliceCount, sliceIndex, &sliceStart, &sliceEnd);

    // Map slice to absolute frame positions within the loop region
    startFrame_ = loopStartFrame + sliceStart;
    endFrame_ = loopStartFrame + sliceEnd;
    reverse_ = false;
  } else {
    startFrame = sampleMarkerToStartFrame(sample_->frameCount, start);
    endFrame = sampleMarkerToEndFrame(sample_->frameCount, end);
    reverse_ = start > end;
    startFrame_ = reverse_ ? endFrame - 1 : startFrame;
    endFrame_ = reverse_ ? startFrame + 1 : endFrame;
  }
  if (endFrame_ <= startFrame_) endFrame_ = startFrame_ + 1;
  if (endFrame_ > sample_->frameCount) endFrame_ = sample_->frameCount;
  step_ = (sample_->sampleRate / outputSampleRate_) * pow(2.0, pitchCents / 1200.0);
  timeStretch_ = speedPercent / 100.0f;
  granular_ = fabsf(timeStretch_ - 1.0f) > 0.001f;
  loopMode_ = loopMode > 2 ? 0 : loopMode;
  grainSize_ = (uint32_t)(outputSampleRate_ * 0.040f);
  grainHop_ = grainSize_ / 2;
  post_.setFilter(sample_->filterEnabled != 0, sample_->filterCharacter, sample_->filterMode,
                  sample_->filterSlope24dB != 0, cutoffHz, resonance / 255.0f);
  post_.setEnvelope(true, envelopeTime(attack < 0 ? sample_->attack : attack),
                    envelopeTime(decay < 0 ? sample_->decay : decay),
                    (sustain < 0 ? sample_->sustain : sustain) / 255.0f,
                    envelopeTime(release < 0 ? sample_->release : release),
                    envelopeShape < 0 ? sample_->envelopeShape : envelopeShape);
}

void SampleVoice::noteOn() {
  if (!sample_ || !sample_->data || endFrame_ <= startFrame_) return;
  if (useStretch_) {
    // A new note always restarts the stretched playback from the top of the
    // source window: tracks are monophonic, so a retrigger must kill the
    // previous playhead and every note keeps its full stretch length.
    // (Stretch enabled mid-note WITHOUT a retrigger lazy-primes in render().)
    stretch_.noteOn();
    active_ = true;
    post_.noteOn(true);
    return;
  }
  position_ = startFrame_;
  direction_ = reverse_ ? -1 : 1;
  grainExhausted_ = false;
  grainPosition_[0] = reverse_ ? endFrame_ - 1 : startFrame_;
  grainPosition_[1] = grainPosition_[0] + direction_ * step_ * grainHop_ * timeStretch_;
  grainAge_[0] = grainHop_;
  grainAge_[1] = 0;
  nextGrainPosition_ = grainPosition_[1] + direction_ * step_ * grainHop_ * timeStretch_;
  active_ = true;
  post_.noteOn(true);
}

float SampleVoice::sampleAt(double position, int channel) const {
  uint32_t frame = (uint32_t)position;
  uint32_t next = direction_ > 0
    ? (frame + 1 < endFrame_ ? frame + 1 : frame)
    : (frame > startFrame_ ? frame - 1 : frame);
  int sourceChannel = sample_->channels == 1 ? 0 : channel;
  float a = sample_->data[frame * sample_->channels + sourceChannel] / 32768.0f;
  float b = sample_->data[next * sample_->channels + sourceChannel] / 32768.0f;
  return a + (b - a) * (float)(position - frame);
}

float SampleVoice::grainSampleAt(double position, int channel) const {
  const double first = startFrame_;
  const double length = endFrame_ - startFrame_;
  if (loopMode_ == 1) {
    position = fmod(position - first, length);
    if (position < 0.0) position += length;
    position += first;
  } else if (loopMode_ == 2) {
    double span = length > 1.0 ? length - 1.0 : 1.0;
    double period = span * 2.0;
    position = fmod(position - first, period);
    if (position < 0.0) position += period;
    position = first + (position <= span ? position : period - position);
  } else if (position < first || position >= endFrame_) {
    return 0.0f;
  }
  uint32_t frame = (uint32_t)position;
  uint32_t next = frame + 1 < endFrame_ ? frame + 1 : frame;
  int sourceChannel = sample_->channels == 1 ? 0 : channel;
  float a = sample_->data[frame * sample_->channels + sourceChannel] / 32768.0f;
  float b = sample_->data[next * sample_->channels + sourceChannel] / 32768.0f;
  return a + (b - a) * (float)(position - frame);
}

bool SampleVoice::advancePosition() {
  position_ += step_ * direction_;
  if (position_ >= startFrame_ && position_ < endFrame_) return true;
  if (loopMode_ == 0) return false;
  double first = startFrame_;
  double last = endFrame_ - 1.0;
  if (loopMode_ == 1) {
    double length = endFrame_ - startFrame_;
    while (position_ >= endFrame_) position_ -= length;
    while (position_ < startFrame_) position_ += length;
  } else if (position_ > last) {
    position_ = last - (position_ - last);
    direction_ = -1;
  } else {
    position_ = first + (first - position_);
    direction_ = 1;
  }
  return true;
}

void SampleVoice::noteOff() {
  if (active_) post_.noteOff();
}

void SampleVoice::kill() {
  active_ = false;
  stretch_.reset();
  post_.kill();
}

void SampleVoice::render(float* output, size_t frames) {
  memset(output, 0, frames * 2 * sizeof(float));
  if (!active_ || !sample_ || !sample_->data) return;

  if (useStretch_) {
    // Stretch enabled mid-note (per-tick configure, no new noteOn): prime now.
    if (!stretch_.primed()) stretch_.noteOn();
    if (!stretch_.process(output, frames)) { kill(); return; }
    // One-shot material: when the stretcher has drained its tail the note is
    // over, matching the plain path's behavior on source exhaustion.
    if (!stretch_.active()) { kill(); return; }
    for (size_t i = 0; i < frames; i++) {
      if (!post_.envelopeActive()) { kill(); break; }
      for (int channel = 0; channel < 2; channel++) {
        output[i * 2 + channel] = post_.process(output[i * 2 + channel], channel);
      }
    }
    if (!post_.envelopeActive()) kill();
    return;
  }

  for (size_t i = 0; i < frames; i++) {
    if (!post_.envelopeActive()) { kill(); break; }
    if (granular_) {
      for (int grain = 0; grain < 2; ++grain) {
        if (grainAge_[grain] >= grainSize_) {
          grainPosition_[grain] = nextGrainPosition_;
          nextGrainPosition_ += direction_ * step_ * grainHop_ * timeStretch_;
          grainAge_[grain] = 0;
          if (loopMode_ == 0 && nextGrainPosition_ >= endFrame_) grainExhausted_ = true;
        }
      }
      for (int channel = 0; channel < 2; ++channel) {
        float value = 0.0f;
        for (int grain = 0; grain < 2; ++grain) {
          float phase = (float)grainAge_[grain] / (float)(grainSize_ - 1);
          float window = sinf(3.14159265f * phase);
          value += grainSampleAt(grainPosition_[grain] + direction_ * step_ * grainAge_[grain], channel) * window * window;
        }
        output[i * 2 + channel] = post_.process(value, channel);
      }
      grainAge_[0]++;
      grainAge_[1]++;
      if (grainExhausted_ && loopMode_ == 0 &&
          grainPosition_[0] + step_ * grainAge_[0] >= endFrame_ &&
          grainPosition_[1] + step_ * grainAge_[1] >= endFrame_) { kill(); break; }
    } else {
      for (int channel = 0; channel < 2; channel++) {
        output[i * 2 + channel] = post_.process(sampleAt(position_, channel), channel);
      }
      if (!advancePosition()) { kill(); break; }
    }
    if (!post_.envelopeActive()) { kill(); break; }
  }
}

static uint16_t readU16(FILE* file, bool* ok) {
  uint8_t bytes[2];
  if (fread(bytes, 1, 2, file) != 2) { *ok = false; return 0; }
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static uint32_t readU32(FILE* file, bool* ok) {
  uint8_t bytes[4];
  if (fread(bytes, 1, 4, file) != 4) { *ok = false; return 0; }
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int sampleLoadWav16File(FILE* file, const char* path, InstrumentSample* sample,
                        char* error, size_t errorSize) {
  if (!file) { snprintf(error, errorSize, "Cannot open WAV"); return 1; }
  char id[4];
  bool ok = fread(id, 1, 4, file) == 4 && !memcmp(id, "RIFF", 4);
  (void)readU32(file, &ok);
  ok = ok && fread(id, 1, 4, file) == 4 && !memcmp(id, "WAVE", 4);
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t sampleRate = 0, dataSize = 0;
  long dataOffset = 0;

  while (ok && fread(id, 1, 4, file) == 4) {
    uint32_t size = readU32(file, &ok);
    if (!ok) break;
    long nextChunk = ftell(file) + size + (size & 1);
    if (!memcmp(id, "fmt ", 4) && size >= 16) {
      format = readU16(file, &ok);
      channels = readU16(file, &ok);
      sampleRate = readU32(file, &ok);
      (void)readU32(file, &ok);
      (void)readU16(file, &ok);
      bits = readU16(file, &ok);
    } else if (!memcmp(id, "data", 4)) {
      dataOffset = ftell(file);
      dataSize = size;
    }
    if (fseek(file, nextChunk, SEEK_SET) != 0) ok = false;
  }

  if (!ok || format != 1 || (channels != 1 && channels != 2) ||
      (bits != 8 && bits != 16 && bits != 24) || sampleRate < 1000 ||
      sampleRate > 192000 || !dataOffset) {
    snprintf(error, errorSize, "Need PCM8/16/24 mono/stereo WAV");
    return 1;
  }
  uint32_t bytesPerSample = bits / 8;
  uint32_t frames = dataSize / (channels * bytesPerSample);
  if (frames == 0 || dataSize > 64U * 1024U * 1024U) {
    snprintf(error, errorSize, "WAV empty or over 64 MB");
    return 1;
  }
  uint32_t sampleCount = frames * channels;
  int16_t* data = (int16_t*)malloc(sampleCount * sizeof(int16_t));
  if (!data || fseek(file, dataOffset, SEEK_SET) != 0) {
    free(data);
    snprintf(error, errorSize, "Cannot read WAV data");
    return 1;
  }
  if (bits == 16) {
    ok = fread(data, sizeof(int16_t), sampleCount, file) == sampleCount;
  } else if (bits == 8) {
    for (uint32_t i = 0; i < sampleCount; i++) {
      int value = fgetc(file);
      if (value == EOF) { ok = false; break; }
      data[i] = (int16_t)((value - 128) << 8);
    }
  } else {
    // 24-bit: three little-endian bytes per sample, sign-extended and
    // scaled down to 16-bit (arithmetic shift keeps the full-scale range).
    for (uint32_t i = 0; i < sampleCount; i++) {
      uint8_t bytes[3];
      if (fread(bytes, 1, 3, file) != 3) { ok = false; break; }
      int32_t value = (int32_t)(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16));
      if (value & 0x800000) value |= 0xFF000000;
      data[i] = (int16_t)(value >> 8);
    }
  }
  if (!ok) {
    free(data);
    snprintf(error, errorSize, "Cannot read WAV data");
    return 1;
  }
  free(sample->data);
  sample->data = data;
  sample->frameCount = frames;
  sample->sampleRate = sampleRate;
  sample->channels = (uint8_t)channels;
  sampleStorePath(path, sample);
  error[0] = 0;
  return 0;
}

int sampleLoadWav16(const char* path, InstrumentSample* sample,
                    char* error, size_t errorSize) {
  FILE* file = fopen(path, "rb");
  if (!file) { snprintf(error, errorSize, "Cannot open WAV"); return 1; }
  int result = sampleLoadWav16File(file, path, sample, error, errorSize);
  fclose(file);
  return result;
}

static void writeU16(FILE* file, uint16_t value, bool* ok) {
  uint8_t bytes[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
  if (fwrite(bytes, 1, 2, file) != 2) *ok = false;
}

static void writeU32(FILE* file, uint32_t value, bool* ok) {
  uint8_t bytes[4] = { (uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF),
                       (uint8_t)((value >> 16) & 0xFF), (uint8_t)(value >> 24) };
  if (fwrite(bytes, 1, 4, file) != 4) *ok = false;
}

int sampleSaveWav16(const InstrumentSample* sample, const char* path,
                    char* error, size_t errorSize) {
  if (!sample->data || sample->frameCount == 0) {
    snprintf(error, errorSize, "No sample to save");
    return 1;
  }
  if (strlen(path) > PROJECT_SAMPLE_PATH_LENGTH) {
    snprintf(error, errorSize, "Path too long");
    return 1;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    snprintf(error, errorSize, "Cannot create WAV");
    return 1;
  }

  const uint16_t channels = sample->channels >= 2 ? 2 : 1;
  const uint16_t bits = 16;
  const uint32_t dataBytes = (uint32_t)sample->frameCount * channels * 2;
  bool ok = true;
  fwrite("RIFF", 1, 4, file);
  writeU32(file, 36 + dataBytes, &ok);
  fwrite("WAVE", 1, 4, file);
  fwrite("fmt ", 1, 4, file);
  writeU32(file, 16, &ok);            // fmt chunk size
  writeU16(file, 1, &ok);             // PCM
  writeU16(file, channels, &ok);
  writeU32(file, sample->sampleRate, &ok);
  writeU32(file, sample->sampleRate * channels * 2, &ok); // byte rate
  writeU16(file, channels * 2, &ok);  // block align
  writeU16(file, bits, &ok);
  fwrite("data", 1, 4, file);
  writeU32(file, dataBytes, &ok);
  if (ok && fwrite(sample->data, sizeof(int16_t), (size_t)sample->frameCount * channels, file) !=
      (size_t)sample->frameCount * channels) {
    ok = false;
  }

  if (fclose(file) != 0) ok = false;
  if (!ok) {
    remove(path);
    snprintf(error, errorSize, "Cannot write WAV");
    return 1;
  }
  error[0] = 0;
  return 0;
}
