#pragma once

// UI-owned telemetry; update once before drawing the current frame.
void monitorDisplayInit(void);
void monitorDisplayUpdate(void);
const float* monitorDisplayTrackSamples(int track);
const float* monitorDisplayMixSamples(void);
float monitorDisplayTrackPeak(int track);
