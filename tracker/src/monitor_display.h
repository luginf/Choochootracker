#pragma once

// UI-owned telemetry; update once before drawing the current frame.
void monitorDisplayInit(void);
void monitorDisplayUpdate(void);
const float* monitorDisplayTrackSamples(int track);
const float* monitorDisplayMixSamples(void);
const float (*monitorDisplayMixStereoSamples(void))[2];
float monitorDisplayTrackPeak(int track, int channel);
