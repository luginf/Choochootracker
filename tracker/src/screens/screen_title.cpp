#include <stdio.h>
#include <string.h>
#include "common.h"
#include "screens.h"
#include "screen_project.h"
#include "corelib_gfx.h"
#include "corelib_file.h"

static GfxImage *sky, *scene, *foreground, *viaduct, *train, *logo;
static int frame;
static int selected;
static int continueAvailable;

// Pixel scrolling stays crisp, but must advance often enough to look fluid at
// the fixed 60 Hz title-screen cadence.
enum {
  SKY_SCROLL_FRAMES = 13,
  SCENE_SCROLL_FRAMES = 8,
  VIADUCT_SCROLL_FRAMES = 5,
  FOREGROUND_SCROLL_FRAMES = 3,
  TRAIN_SCROLL_FRAMES = 5,
};

static void unload(void) {
  gfxImageFree(sky); sky = NULL;
  gfxImageFree(scene); scene = NULL;
  gfxImageFree(foreground); foreground = NULL;
  gfxImageFree(viaduct); viaduct = NULL;
  gfxImageFree(train); train = NULL;
  gfxImageFree(logo); logo = NULL;
}

static GfxImage* load(const char* name) {
  char path[PATH_LENGTH + 64];
#ifdef ANDROID_BUILD
  char workspace[PATH_LENGTH];
  if (fileGetDefaultDirectory(workspace, sizeof(workspace)) == 0) {
    snprintf(path, sizeof(path), "%s/title/%s", workspace, name);
    return gfxImageLoadBMP(path);
  }
#endif
  snprintf(path, sizeof(path), "title/%s", name);
  return gfxImageLoadBMP(path);
}

static void init(void) {
  unload();
  sky = load("snes_sky.bmp");
  scene = load("snes_scene.bmp");
  foreground = load("snes_foreground.bmp");
  viaduct = load("snes_viaduct.bmp");
  train = load("snes_train.bmp");
  logo = load("snes_logo.bmp");
}

static int canContinue(void) {
  if (autosaveLoadFailed) return 0;
#ifdef WEB_BUILD
  return 1;
#else
  FILE* autosave = fopen(getAutosavePath(), "rb");
  if (!autosave) return 0;
  fclose(autosave);
  return 1;
#endif
}

static void setup(int input) {
  frame = 0;
  continueAvailable = canContinue();
  selected = continueAvailable ? 0 : 1;
}
static void fullRedraw(void) {}

static void drawWrapped(const GfxImage* image, int offset, int y) {
  const int width = gfxImageWidth(image);
  if (!width) return;
  offset %= width;
  int x = 0;
  while (x < 256) {
    int part = width - offset;
    if (part > 256 - x) part = 256 - x;
    gfxImageDrawCrop(image, offset, 0, part, gfxImageHeight(image), x, y);
    x += part;
    offset = 0;
  }
}

static void drawMenu(void) {
  const char* items[] = {"CONTINUE", "OPEN", "NEW"};
  for (int i = 0; i < 3; i++) {
    const int textX = (32 - (int)strlen(items[i])) / 2;
    gfxSetFgColor(i == 0 && !continueAvailable ? 0x777777 :
      i == selected ? 0xffd665 : 0xf2e6c7);
    gfxTitlePrint(textX - 2, 6 + i, i == selected && ((frame / 20) & 1) == 0 ? ">" : " ");
    gfxTitlePrint(textX, 6 + i, items[i]);
  }
}

// screenDraw() skips its normal gfxPrint-based message banner while the
// title screen is active (it renders through gfxTitle* calls instead), so an
// error set via screenMessage() - e.g. picking an invalid file through OPEN,
// which returns here - would otherwise vanish silently. Show it here too.
// The autosave-specific case takes priority: it existed but could not be
// parsed, CONTINUE was disabled above (see canContinue()), and the fallback
// demo project is loaded instead of the user's real work - too important to
// let a later, shorter-lived message push off screen.
static void drawTitleMessage(void) {
  if (autosaveLoadFailed) {
    const char* lines[] = {"AUTOSAVE FAILED TO LOAD", "DEMO PROJECT OPENED INSTEAD"};
    gfxSetFgColor(appSettings.colorScheme.warning);
    for (int i = 0; i < 2; i++) {
      const int textX = (32 - (int)strlen(lines[i])) / 2;
      gfxTitlePrint(textX, 10 + i, lines[i]);
    }
    return;
  }
  const char* message = screenGetActiveMessage();
  if (!message || !message[0]) return;
  gfxSetFgColor(appSettings.colorScheme.warning);
  int textX = (32 - (int)strlen(message)) / 2;
  if (textX < 0) textX = 0;
  gfxTitlePrint(textX, 10, message);
}

static void draw(void) {
  gfxTitleBegin();
  drawWrapped(sky, frame / SKY_SCROLL_FRAMES, 0);
  // The scenery around the viaduct remains a separate, intermediate plane.
  drawWrapped(scene, frame / SCENE_SCROLL_FRAMES, 112);
  int vibration = frame % 173 < 6 ? (frame & 1 ? -1 : 1) : 0;
  if (train) {
    const int trainWidth = gfxImageWidth(train);
    const int trainX = (8 + frame / TRAIN_SCROLL_FRAMES + trainWidth) % (256 + trainWidth) - trainWidth;
    gfxImageDrawCrop(train, 0, 0, trainWidth, gfxImageHeight(train), trainX, 133 + vibration);
  }
  drawWrapped(viaduct, frame / VIADUCT_SCROLL_FRAMES, 152);
  drawWrapped(foreground, frame / FOREGROUND_SCROLL_FRAMES, 136);
  if (logo) gfxImageDrawCrop(logo, 0, 0, gfxImageWidth(logo), gfxImageHeight(logo), 52, 24);
  drawMenu();
  drawTitleMessage();
  if (frame < 30) gfxTitleFadeBlack((uint8_t)(255 - frame * 255 / 30));
  gfxTitlePresent();
  frame++;
}

static int onInput(int isKeyDown, int keys, int tapCount) {
  if (!isKeyDown) return 1;
  if (keys == keyUp && selected > 0) { selected--; return 1; }
  if (keys == keyDown && selected < 2) { selected++; return 1; }
  // Start is a second confirm key on the title screen only.
  if (keys != keyEdit && keys != keyPlay) return 1;
  gfxTitleEnd();
  if (selected == 0 && continueAvailable) { unload(); screenSetup(&screenSong, 0); }
  else if (selected == 1) {
#ifdef WEB_BUILD
    projectOpenFromScreenAtPath(&screenTitle, "/projects");
#else
    projectOpenFromScreen(&screenTitle);
#endif
  }
  else { unload(); projectCreateNewFromScreen(&screenSong); }
  return 1;
}

const AppScreen screenTitle = {
  .init = init,
  .setup = setup,
  .fullRedraw = fullRedraw,
  .draw = draw,
  .onInput = onInput,
  .getPlaybackLevel = NULL,
};
