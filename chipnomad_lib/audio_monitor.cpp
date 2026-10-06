#include "audio_monitor.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

AudioMonitor::AudioMonitor() { reserve(4096); }
AudioMonitor::~AudioMonitor() { free(scratch_); }

bool AudioMonitor::reserve(int frames) {
  if (frames <= 0) return false;
  if (frames <= capacity_) return scratch_ != nullptr;
  if ((size_t)frames > std::numeric_limits<size_t>::max() / (2 * PROJECT_MAX_TRACKS * sizeof(float))) return false;
  float* memory = (float*)calloc((size_t)frames * 2 * PROJECT_MAX_TRACKS, sizeof(float));
  if (!memory) return false;
  free(scratch_);
  scratch_ = memory;
  capacity_ = frames;
  return true;
}

void AudioMonitor::beginRender() { memset(peaks_, 0, sizeof(peaks_)); }
void AudioMonitor::beginChunk(int frames) {
  chunkFrames_ = scratch_ && frames > 0 && frames <= capacity_ ? frames : 0;
  if (chunkFrames_) memset(scratch_, 0, (size_t)frames * 2 * PROJECT_MAX_TRACKS * sizeof(float));
}
void AudioMonitor::add(int track, int sampleIndex, float value) {
  if (chunkFrames_ && track >= 0 && track < PROJECT_MAX_TRACKS && sampleIndex >= 0 && sampleIndex < chunkFrames_ * 2)
    scratch_[track * chunkFrames_ * 2 + sampleIndex] += value;
}

void AudioMonitor::finishChunk(const float* stereo, int frames, int sampleRate) {
  // About 21 ms of history at any sample rate, across tick/FLFO chunk edges.
  const int stride = std::max(1, sampleRate / 12000);
  for (int frame = 0; frame < frames; ++frame) {
    const bool capture = decimation_ == 0;
    for (int track = 0; track < PROJECT_MAX_TRACKS; ++track) {
      const float* p = frame < chunkFrames_ ? scratch_ + track * chunkFrames_ * 2 + frame * 2 : nullptr;
      float left = p ? p[0] : 0, right = p ? p[1] : 0;
      peaks_[track] = std::max(peaks_[track], std::max(fabsf(left), fabsf(right)));
      if (capture) tracks_[track][cursor_] = (left + right) * 0.5f;
    }
    if (capture) {
      mix_[cursor_] = (stereo[frame * 2] + stereo[frame * 2 + 1]) * 0.5f;
      cursor_ = (cursor_ + 1) % AUDIO_MONITOR_SAMPLES;
    }
    decimation_ = (decimation_ + 1) % stride;
  }
}

void AudioMonitor::publish() {
  int previous = published_.exchange(-1, std::memory_order_acq_rel);
  if (previous >= 0) {
    int expected = 1;
    slots_[previous].state.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
  }
  for (int slot = 0; slot < 3; ++slot) {
    int expected = 0;
    if (!slots_[slot].state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) continue;
    auto& data = slots_[slot].data;
    for (int i = 0; i < AUDIO_MONITOR_SAMPLES; ++i) {
      int source = (cursor_ + i) % AUDIO_MONITOR_SAMPLES;
      data.mix[i] = mix_[source];
      for (int t = 0; t < PROJECT_MAX_TRACKS; ++t) data.tracks[t][i] = tracks_[t][source];
    }
    memcpy(data.peaks, peaks_, sizeof(peaks_));
    slots_[slot].state.store(1, std::memory_order_release);
    published_.store(slot, std::memory_order_release);
    return;
  }
}

bool AudioMonitor::receive(AudioMonitorSnapshot& result) {
  int slot = published_.load(std::memory_order_acquire);
  if (slot < 0) return false;
  int expected = 1;
  if (!slots_[slot].state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) return false;
  int published = slot;
  published_.compare_exchange_strong(published, -1, std::memory_order_acq_rel);
  result = slots_[slot].data;
  slots_[slot].state.store(0, std::memory_order_release);
  return true;
}
