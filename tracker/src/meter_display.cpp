#include "meter_display.h"
#include "monitor_display.h"
#include "common.h"
#include "corelib_gfx.h"
#include <algorithm>
#include <cmath>

static Bitmap* meter;

static Bitmap* sized(Bitmap*& bitmap, int columns, int rows) {
  if (bitmap && (bitmap->widthPixels != columns * gfxGetCharWidth() ||
                 bitmap->heightPixels != rows * gfxGetCharHeight())) {
    gfxBitmapFree(bitmap);
    bitmap = nullptr;
  }
  if (!bitmap) bitmap = gfxBitmapCreate(columns, rows);
  if (bitmap) gfxBitmapClear(bitmap);
  return bitmap;
}

int monitorMeterHeight(float peak, int height) {
  if (!std::isfinite(peak) || peak <= 0 || height <= 0) return 0;
  float db = 20.0f * log10f(peak);
  return std::max(0, std::min(height, (int)ceilf((db + 48.0f) * height / 48.0f)));
}

void monitorDisplayDrawMeter(int track, int col, int row) {
  if (track < 0 || track >= PROJECT_MAX_TRACKS || !sized(meter, 1, 1)) return;
  const auto cs = appSettings.colorScheme;
  gfxSetBgColor(cs.background);
  gfxClearRect(col, row, 1, 1);
  const int w = meter->widthPixels, h = meter->heightPixels;
  const int pixels = monitorMeterHeight(monitorDisplayTrackPeak(track), h - 2);
  for (int band = 0; band < 3; ++band) {
    gfxBitmapClear(meter);
    for (int level = 0; level < pixels; ++level) {
      int zone = level >= (h - 2) * 15 / 16 ? 2 : level >= (h - 2) * 3 / 4 ? 1 : 0;
      if (zone != band) continue;
      for (int x = w / 3; x < w - w / 3; ++x)
        meter->data[(h - 2 - level) * w + x] = 255;
    }
    gfxSetFgColor(band == 2 ? 0xff0000 : band == 1 ? cs.textTitles : cs.textInfo);
    gfxDrawBitmap(meter, col, row);
  }
}
