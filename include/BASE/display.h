#ifndef HOMM1_BASE_DISPLAY_H
#define HOMM1_BASE_DISPLAY_H

#if defined(HOMM1_PORT)
namespace platform {
extern int gCanvasWidth;
extern int gCanvasHeight;
}
#define LOGICAL_SCREEN_WIDTH (platform::gCanvasWidth)
#define LOGICAL_SCREEN_HEIGHT (platform::gCanvasHeight)
#else
enum LogicalScreenConstant {
    LOGICAL_SCREEN_WIDTH = 640,
    LOGICAL_SCREEN_HEIGHT = 480
};
#endif

enum PaletteFormatConstant {
    PALETTE_COLOR_COUNT = 256,
    COLOR_INDEX_MASK = 0xff
};

#define CLIENT_TO_GAME_X(x) (((x) * LOGICAL_SCREEN_WIDTH) / gMainWinScreenWidth)
#define CLIENT_TO_GAME_Y(y) (((y) * LOGICAL_SCREEN_HEIGHT) / gMainWinScreenHeight)

#endif
