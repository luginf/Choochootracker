#include "corelib_gfx.h"

static int contentRowOffset = 0;
void gfxSetContentRowOffset(int rows) { contentRowOffset = rows; }
int gfxGetContentRowOffset(void) { return contentRowOffset; }
