#include "monitor_display.h"
#include "audio_monitor.h"
#include "common.h"
#include <algorithm>
#include <cstring>

static AudioMonitorSnapshot snapshot;
static float meterPeaks[PROJECT_MAX_TRACKS];

void monitorDisplayInit(void) {
  snapshot = {};
  memset(meterPeaks, 0, sizeof(meterPeaks));
}

void monitorDisplayUpdate(void) {
  if (!chipnomadState || !chipnomadState->audioMonitor) return;
  const bool fresh = chipnomadState->audioMonitor->receive(snapshot);
  for (int t = 0; t < PROJECT_MAX_TRACKS; ++t)
    meterPeaks[t] = std::max(meterPeaks[t] * 0.88f, fresh ? snapshot.peaks[t] : 0.0f);
}

const float* monitorDisplayTrackSamples(int track) {
  return track >= 0 && track < PROJECT_MAX_TRACKS ? snapshot.tracks[track] : nullptr;
}

const float* monitorDisplayMixSamples(void) { return snapshot.mix; }
float monitorDisplayTrackPeak(int track) {
  return track >= 0 && track < PROJECT_MAX_TRACKS ? meterPeaks[track] : 0;
}
