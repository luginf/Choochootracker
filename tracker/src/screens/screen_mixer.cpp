#include "screens.h"
#include "audio_manager.h"
#include "audio_monitor.h"
#include "common.h"
#include "corelib_gfx.h"
#include "meter_display.h"
#include "monitor_display.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdio.h>
#include <string.h>

static void fullRedraw(void);
static int displayedCpuLoad = -1;
static int mixerPage = 0; // 0 mixer, 1 reverb, 2 delay
static uint8_t autoMixPreview[PROJECT_MAX_TRACKS];
static uint8_t autoMixOriginal[PROJECT_MAX_TRACKS];
static Bitmap* stereoImage;
static Bitmap* spectrum;
static std::chrono::steady_clock::time_point spectrumRefresh;

static Bitmap* analyzerBitmap(Bitmap*& bitmap, int columns, int rows, bool clear = true) {
  if (bitmap && (bitmap->widthPixels != columns * gfxGetCharWidth() || bitmap->heightPixels != rows * gfxGetCharHeight())) {
    gfxBitmapFree(bitmap);
    bitmap = NULL;
  }
  if (!bitmap) bitmap = gfxBitmapCreate(columns, rows);
  if (bitmap && clear) gfxBitmapClear(bitmap);
  return bitmap;
}

static void analyzerPixel(Bitmap* bitmap, int x, int y, uint8_t value) {
  if (bitmap && x >= 0 && x < bitmap->widthPixels && y >= 0 && y < bitmap->heightPixels)
    bitmap->data[y * bitmap->widthPixels + x] = value;
}

static void drawAnalyzers(void) {
  constexpr int imageColumns = 17, spectrumColumns = 17, rows = 5;
  Bitmap* image = analyzerBitmap(stereoImage, imageColumns, rows);
  Bitmap* fft = analyzerBitmap(spectrum, spectrumColumns, rows, false);
  const float (*samples)[2] = monitorDisplayMixStereoSamples();
  if (image) {
    const int w = image->widthPixels, h = image->heightPixels;
    for (int x = 0; x < w; ++x) analyzerPixel(image, x, h / 2, 48);
    for (int y = 0; y < h; ++y) analyzerPixel(image, w / 2, y, 48);
    if (samples) for (int i = 0; i < AUDIO_MONITOR_SAMPLES; ++i) {
      float left = std::isfinite(samples[i][0]) ? samples[i][0] : 0.0f;
      float right = std::isfinite(samples[i][1]) ? samples[i][1] : 0.0f;
      int x = w / 2 + (int)((left - right) * (w / 2 - 2) * 0.8f);
      int y = h / 2 - (int)((left + right) * (h / 2 - 2) * 0.5f);
      analyzerPixel(image, x, y, 255);
    }
  }
  static float sine[128], cosine[128], window[128];
  static bool spectrumLutReady = false;
  if (!spectrumLutReady) {
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < 128; ++i) {
      cosine[i] = cosf(2.0f * pi * i / 128.0f);
      sine[i] = sinf(2.0f * pi * i / 128.0f);
      window[i] = 0.5f - 0.5f * cosf(2.0f * pi * i / 127.0f);
    }
    spectrumLutReady = true;
  }
  // A DFT still does thousands of arithmetic operations per draw even though
  // its trigonometric terms come from LUTs. Keep this noncritical diagnostic
  // display below the PortMaster audio deadline.
  const auto now = std::chrono::steady_clock::now();
  if (fft && samples &&
      (spectrumRefresh == std::chrono::steady_clock::time_point() ||
       std::chrono::duration<float>(now - spectrumRefresh).count() >= 1.0f / 20.0f)) {
    gfxBitmapClear(fft);
    const int w = fft->widthPixels, h = fft->heightPixels, bands = 17, bandWidth = std::max(1, w / bands);
    for (int band = 0; band < bands; ++band) {
      const int bin = 1 + band * 3;
      float real = 0, imaginary = 0;
      for (int i = 0; i < 128; ++i) {
        float sample = (samples[i][0] + samples[i][1]) * 0.5f;
        const int phase = (bin * i) & 127;
        real += sample * window[i] * cosine[phase];
        imaginary -= sample * window[i] * sine[phase];
      }
      float amplitude = sqrtf(real * real + imaginary * imaginary) * 4.0f / 128.0f;
      float db = 20.0f * log10f(std::max(amplitude, 0.001f));
      int level = std::clamp((int)lroundf((db + 60.0f) * h / 60.0f), 0, h);
      for (int x = band * bandWidth; x < std::min(w, (band + 1) * bandWidth - 1); ++x)
        for (int y = 0; y < level; ++y) analyzerPixel(fft, x, h - 1 - y, 255);
    }
    spectrumRefresh = now;
  }
  gfxSetFgColor(appSettings.colorScheme.textInfo);
  gfxClearRect(0, 14, 35, 6);
  gfxPrint(0, 14, "STEREO IMAGE");
  gfxPrint(18, 14, "FFT SPECTRUM");
  if (image) gfxDrawBitmap(image, 0, 15);
  if (fft) gfxDrawBitmap(fft, 18, 15);
}

static void applyAutoMix(void) {
  projectModified = 1;
  screenSetup(&screenMixer, 0);
}
static void cancelAutoMix(void) {
  for (int i = 0; i < chipnomadState->project.tracksCount; ++i)
    chipnomadState->project.trackVolume[i] = autoMixOriginal[i];
  screenSetup(&screenMixer, 0);
}

int screenMixerGetPage(void) { return mixerPage; }

static int columnCount(int row) { return mixerPage == 0 ? (row == PROJECT_MAX_TRACKS ? 1 : 7) : 1; }
static void drawRowHeader(int row, CellState state) {}
static void drawColHeader(int col, CellState state) {}

static void drawStatic(void) {
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  if (mixerPage == 0) {
    gfxPrint(0, 0, "MIXER");
    gfxPrint(0, 2, "TRK"); gfxPrint(4, 2, "LVL"); gfxPrint(9, 2, "REV"); gfxPrint(14, 2, "DLY");
    gfxPrint(19, 2, "TLT"); gfxPrint(24, 2, "PAN"); gfxPrint(29, 2, "M"); gfxPrint(32, 2, "S");
    gfxPrint(0, 12, "AUTO MIX");
  } else if (mixerPage == 1) {
    gfxPrint(0, 0, "CLOUDS REVERB");
    gfxPrint(0, 3, "Return"); gfxPrint(0, 4, "Time");
    gfxPrint(0, 5, "Damping"); gfxPrint(0, 6, "Filter");
  } else {
    gfxPrint(0, 0, "PING-PONG DELAY");
    gfxPrint(0, 3, "Return"); gfxPrint(0, 4, "To Reverb");
    gfxPrint(0, 5, "Ticks"); gfxPrint(0, 6, "Feedback"); gfxPrint(0, 7, "Filter");
  }
}

static void drawCursor(int col, int row) {
  if (mixerPage == 0) {
    static const int x[] = {4, 9, 14, 19, 24, 29, 32};
    static const int width[] = {2, 2, 2, 2, 2, 1, 1};
    if (row == PROJECT_MAX_TRACKS) { gfxCursor(10, 12, 16); return; }
    if (col < 0 || col >= 7 || row < 0 || row >= PROJECT_MAX_TRACKS) return;
    gfxCursor(x[col], 3 + row, width[col]);
  } else if (row >= 0 && row < (mixerPage == 2 ? 5 : 4)) {
    int filterRow = mixerPage == 2 ? 4 : 3;
    int width = 2;
    if (row == filterRow) {
      char value[12];
      unsigned int cutoff = mixerPage == 1 ? chipnomadState->project.reverbFilterCutoffHz : chipnomadState->project.delayFilterCutoffHz;
      snprintf(value, sizeof(value), "%u Hz", cutoff);
      width = (int)strlen(value);
    }
    gfxCursor(12, 3 + row, width);
  }
}

static void drawField(int col, int row, CellState state) {
  gfxSetFgColor(state == CellState::focus ? appSettings.colorScheme.textValue : appSettings.colorScheme.textDefault);
  if (mixerPage == 0) {
    if (row == PROJECT_MAX_TRACKS) {
      gfxClearRect(10, 12, 16, 1);
      gfxPrint(10, 12, state == CellState::focus ? "> RUN AUTO MIX <" : "[ RUN AUTO MIX ]");
      return;
    }
    if (col < 0 || col >= 7 || row < 0 || row >= PROJECT_MAX_TRACKS) return;
    gfxPrintf(0, 3 + row, "%2d", row + 1);
    if (col == 0) gfxPrint(4, 3 + row, byteToHex(controlFromRange(chipnomadState->project.trackVolume[row], 100)));
    else if (col == 1) gfxPrint(9, 3 + row, byteToHex(controlFromRange(chipnomadState->project.trackReverbSend[row], 100)));
    else if (col == 2) gfxPrint(14, 3 + row, byteToHex(controlFromRange(chipnomadState->project.trackDelaySend[row], 100)));
    else if (col == 3) gfxPrint(19, 3 + row, byteToHex(chipnomadState->project.trackTilt[row]));
    else if (col == 4) gfxPrint(24, 3 + row, byteToHex(chipnomadState->project.trackPan[row]));
    else if (col == 5) gfxPrint(29, 3 + row, audioManager.trackStates[row] == TRACK_MUTED ? "*" : "-");
    else gfxPrint(32, 3 + row, audioManager.trackStates[row] == TRACK_SOLO ? "*" : "-");
    gfxSetFgColor(chipnomadState->trackClipping[row] ? appSettings.colorScheme.warning : appSettings.colorScheme.textDefault);
    gfxPrint(38, 3 + row, chipnomadState->trackClipping[row] ? "!" : " ");
    return;
  }
  if (row < 0 || row >= (mixerPage == 2 ? 5 : 4)) return;
  Project* p = &chipnomadState->project;
  if (mixerPage == 1) {
    if (row == 0) gfxPrint(12, 3, byteToHex(controlFromRange(p->reverbReturn, 100)));
    else if (row == 1) gfxPrint(12, 4, byteToHex(p->reverbTime));
    else if (row == 2) gfxPrint(12, 5, byteToHex(p->reverbDamping));
    else {
      gfxClearRect(12, 6, 8, 1);
      gfxPrintf(12, 6, "%u Hz", p->reverbFilterCutoffHz);
    }
  } else {
    if (row == 0) gfxPrint(12, 3, byteToHex(controlFromRange(p->delayReturn, 100)));
    else if (row == 1) gfxPrint(12, 4, byteToHex(controlFromRange(p->delayReverbSend, 100)));
    else if (row == 2) gfxPrintf(12, 5, "%02X", p->delayTicks);
    else if (row == 3) gfxPrint(12, 6, byteToHex(controlFromRange(p->delayFeedback, 95)));
    else {
      gfxClearRect(12, 7, 8, 1);
      gfxPrintf(12, 7, "%u Hz", p->delayFilterCutoffHz);
    }
  }
}

static int onEdit(int col, int row, CellEditAction action) {
  Project* p = &chipnomadState->project;
  int handled = 0;
  if (mixerPage == 0) {
    if (row == PROJECT_MAX_TRACKS) {
      if (action != CellEditAction::tap) return 0;
      if (chipnomadAutoMix(chipnomadState, 6, autoMixPreview)) screenMessage(MESSAGE_TIME, "Auto mix: no audio");
      else {
        char summary[128] = "Apply? ";
        for (int i = 0; i < chipnomadState->project.tracksCount; ++i) {
          autoMixOriginal[i] = chipnomadState->project.trackVolume[i];
          char change[16];
          snprintf(change, sizeof(change), "T%d %d>%d ", i + 1,
            autoMixOriginal[i], autoMixPreview[i]);
          strncat(summary, change, sizeof(summary) - strlen(summary) - 1);
          chipnomadState->project.trackVolume[i] = autoMixPreview[i];
        }
        chipnomadQueuePlaybackStartSong(chipnomadState, 0, 0, 1);
        confirmSetup(summary, applyAutoMix, cancelAutoMix);
        screenSetup(&screenConfirm, 0);
      }
      return 1;
    }
    if (col < 0 || col >= 7 || row < 0 || row >= PROJECT_MAX_TRACKS) return 0;
    // These values are stored as 0-100 percentages.  Do not round-trip them
    // through the 0-255 display scale: a one-step left/right edit can round
    // back to the same percentage and appear broken.
    if (col == 0) handled = edit8noLast(action, &p->trackVolume[row], 10, 0, 100);
    else if (col == 1) handled = edit8noLast(action, &p->trackReverbSend[row], 10, 0, 100);
    else if (col == 2) handled = edit8noLast(action, &p->trackDelaySend[row], 10, 0, 100);
    else if (col == 3) handled = edit8noLast(action, &p->trackTilt[row], 16, 0, 255);
    else if (col == 4 && action == CellEditAction::clear) { p->trackPan[row] = 0x80; handled = 1; }
    else if (col == 4) handled = edit8noLast(action, &p->trackPan[row], 16, 0, 255);
    else if (action == CellEditAction::tap) {
      if (col == 5) audioManager.toggleTrackMute(row); else audioManager.toggleTrackSolo(row);
      handled = 1;
    }
  } else if (row >= 0 && row < (mixerPage == 2 ? 5 : 4)) {
    if (mixerPage == 1) {
      if (row == 0) handled = edit8noLast(action, &p->reverbReturn, 10, 0, 100);
      else if (row == 1) handled = edit8noLast(action, &p->reverbTime, 16, 0, 255);
      else if (row == 2) handled = edit8noLast(action, &p->reverbDamping, 16, 0, 255);
      else handled = editFilterCutoff(action, &p->reverbFilterCutoffHz);
    } else {
      if (row == 0) handled = edit8noLast(action, &p->delayReturn, 10, 0, 100);
      else if (row == 1) handled = edit8noLast(action, &p->delayReverbSend, 10, 0, 100);
      else if (row == 2) handled = edit8noLast(action, &p->delayTicks, 4, 1, 255);
      else if (row == 3) handled = edit8noLast(action, &p->delayFeedback, 10, 0, 95);
      else handled = editFilterCutoff(action, &p->delayFilterCutoffHz);
    }
  }
  if (handled) {
    projectModified = 1;
    if (mixerPage == 0 && col >= 5) fullRedraw();
  }
  return handled;
}

static ScreenData screen = {
  .rows = PROJECT_MAX_TRACKS, .cursorRow = 0, .cursorCol = 0, .topRow = 0,
  .selectMode = -1, .selectStartRow = 0, .selectStartCol = 0,
  .selectAnchorRow = 0, .selectAnchorCol = 0, .playbackLevel = ScreenPlaybackLevel::song,
  .getColumnCount = columnCount, .drawStatic = drawStatic, .drawCursor = drawCursor,
  .drawSelection = NULL, .drawRowHeader = drawRowHeader, .drawColHeader = drawColHeader,
  .drawField = drawField, .onEdit = onEdit, .onInput = NULL, .onRawInput = NULL,
  .isCellValid = NULL, .getLoopRange = NULL,
};

static void setup(int input) { displayedCpuLoad = -1; mixerPage = 0; screen.rows = PROJECT_MAX_TRACKS + 1; }
static void fullRedraw(void) { screenFullRedraw(&screen); }
static void draw(void) {
  if (mixerPage == 0) {
    for (int track = 0; track < chipnomadState->project.tracksCount; ++track)
      monitorDisplayDrawMeter(track, 2, 3 + track);
    drawAnalyzers();
  }
  int cpuLoad = audioManager.getCpuLoadPercent();
  if (cpuLoad == displayedCpuLoad) return;
  displayedCpuLoad = cpuLoad;
  gfxSetFgColor(appSettings.colorScheme.textTitles);
  gfxClearRect(31, 0, 9, 1);
  gfxPrintf(31, 0, "CPU %03d%%", cpuLoad);
}

static void setPage(int page) {
  mixerPage = page;
  screen.rows = page == 0 ? PROJECT_MAX_TRACKS + 1 : (page == 2 ? 5 : 4);
  screen.cursorRow = 0;
  screen.cursorCol = 0;
  fullRedraw();
}

static int onInput(int isKeyDown, int keys, int tapCount) {
  if (isKeyDown && keys == (keyLeft | keyShift) && mixerPage != 0) {
    setPage(0);
    return 1;
  }
  if (isKeyDown && keys == (keyUp | keyShift)) {
    setPage(mixerPage == 2 ? 0 : 1);
    return 1;
  }
  if (isKeyDown && keys == (keyDown | keyShift)) {
    setPage(mixerPage == 1 ? 0 : 2);
    return 1;
  }
  if (isKeyDown && keys == (keyRight | keyShift)) {
    screenSetup(&screenSong, 0);
    return 1;
  }
  return screenInput(&screen, isKeyDown, keys, tapCount);
}

static ScreenPlaybackLevel getPlaybackLevel(void) { return ScreenPlaybackLevel::song; }

const AppScreen screenMixer = {
  .init = NULL, .setup = setup, .fullRedraw = fullRedraw, .draw = draw,
  .onInput = onInput, .getPlaybackLevel = getPlaybackLevel,
};
