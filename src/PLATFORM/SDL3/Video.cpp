#include "Internal.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

namespace platform {

namespace {

constexpr int kWidth = DISPLAY_WIDTH;
constexpr int kHeight = DISPLAY_HEIGHT;
// The canvas holds the chrome bar above the game image.
constexpr int kCanvasHeight = kHeight + CHROME_BAR_MAX;
// Presentation is coalesced to this interval unless forced: the game copies
// many small rectangles per frame, the way DirectDraw blits were cheap.
constexpr u32 kPresentInterval = 8;

SDL_Window* gWindow = nullptr;
SDL_Renderer* gRenderer = nullptr;
SDL_Texture* gTexture = nullptr;
bool gFullscreen = false;
DisplayScaling gScaling = DISPLAY_SCALING_FIT;

std::array<u8, kWidth * kHeight> gIndexed{};
std::array<u32, kWidth * kCanvasHeight> gPixels{};
std::array<u8, kWidth * kCanvasHeight> gChrome{};
std::array<u8, kWidth * kCanvasHeight> gChromeMask{};
int gBar = 0;

void ApplyLogicalPresentation() {
    if (!gRenderer)
        return;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_LETTERBOX;
    int width, height;
    if (gScaling == DISPLAY_SCALING_FILL && gFullscreen)
        mode = SDL_LOGICAL_PRESENTATION_STRETCH;
    else if (gScaling == DISPLAY_SCALING_INTEGER && SDL_GetRenderOutputSize(gRenderer, &width, &height)
        && width >= kWidth && height >= kHeight + gBar)
        mode = SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
    SDL_SetRenderLogicalPresentation(gRenderer, kWidth, kHeight + gBar, mode);
}
std::array<Color, PALETTE_SIZE> gPalette{};
std::array<u32, PALETTE_SIZE> gPalettePixels{};
bool gDirty = false;
int gDisplayUpdateDepth = 0;
SDL_Rect gDamage{};
SDL_Rect gLastCursorBounds{};
u32 gLastPresent = 0;
const u8* gReference = nullptr;

CursorImage gCursor;
bool gCursorSet = false;
int gCursorCount = 0;
int gPointerX = 0;
int gPointerY = 0;
bool gPointerInside = false;

bool Bit(const u8* mask, int x, int y) {
    return (mask[y * 4 + (x >> 3)] & (0x80 >> (x & 7))) != 0;
}

u32 Rgb(const Color& color) {
    return (static_cast<u32>(color.r) << 16) | (static_cast<u32>(color.g) << 8) | color.b;
}

SDL_Rect CursorBounds() {
    if (!gCursorSet || gCursorCount < 0 || !gPointerInside)
        return {};
    const SDL_Rect cursor = {gPointerX - gCursor.hotX, gPointerY + gBar - gCursor.hotY,
                             CURSOR_SIZE, CURSOR_SIZE};
    const SDL_Rect canvas = {0, 0, kWidth, kHeight + gBar};
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&cursor, &canvas, &visible))
        return {};
    return visible;
}

void InvalidatePixels(const SDL_Rect& rect) {
    if (!SDL_RectEmpty(&rect))
        SDL_GetRectUnion(&gDamage, &rect, &gDamage);
    gDirty = true;
}

void InvalidateCanvas() {
    gDamage = {0, 0, kWidth, kHeight + gBar};
    gLastCursorBounds = {};
    gDirty = true;
}

void DrawCursor() {
    for (int y = 0; y < CURSOR_SIZE; y++) {
        int screenY = gPointerY + gBar - gCursor.hotY + y;
        if (screenY < 0 || screenY >= kHeight + gBar)
            continue;
        for (int x = 0; x < CURSOR_SIZE; x++) {
            int screenX = gPointerX - gCursor.hotX + x;
            if (screenX < 0 || screenX >= kWidth)
                continue;
            u32& pixel = gPixels[static_cast<size_t>(screenY * kWidth + screenX)];
            bool andBit = Bit(gCursor.andMask, x, y);
            bool xorBit = Bit(gCursor.xorMask, x, y);
            if (andBit) {
                if (xorBit)
                    pixel ^= 0xFFFFFFu;
            } else if (gCursor.colorCursor) {
                pixel = gPalettePixels[gCursor.color[y * CURSOR_SIZE + x]];
            } else {
                pixel = xorBit ? 0xFFFFFFu : 0;
            }
        }
    }
}

void UpdateSystemCursor() {
    if (gWindow == nullptr)
        return;
    // The game's own pointer is drawn into the display; the host's arrow
    // stands in only when the game has none.
    if (gCursorSet || gCursorCount < 0)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}

int ChooseScale() {
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds;
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds))
        return 1;
    int scale = std::min((bounds.w - 32) / kWidth, (bounds.h - 64) / (kHeight + gBar));
    return std::max(1, std::min(scale, 3));
}

}  // namespace

namespace sdl {

SDL_Window* Window() {
    return gWindow;
}

SDL_Renderer* Renderer() {
    return gRenderer;
}

void SetPointer(int x, int y, bool inside) {
    if (x == gPointerX && y == gPointerY && inside == gPointerInside)
        return;
    gPointerX = x;
    gPointerY = y;
    gPointerInside = inside;
    if (gCursorSet && gCursorCount >= 0 && (inside || !SDL_RectEmpty(&gLastCursorBounds)))
        gDirty = true;
}

void PointerPosition(int& x, int& y, bool& inside) {
    x = gPointerX;
    y = gPointerY;
    inside = gPointerInside;
}

void WindowToDisplay(float windowX, float windowY, int& x, int& y, bool& inside) {
    float logicalX = windowX;
    float logicalY = windowY;
    if (gRenderer != nullptr)
        SDL_RenderCoordinatesFromWindow(gRenderer, windowX, windowY, &logicalX, &logicalY);
    x = static_cast<int>(logicalX);
    y = static_cast<int>(logicalY);
    y -= gBar;
    inside = x >= 0 && y >= -gBar && x < kWidth && y < kHeight;
    x = std::clamp(x, 0, kWidth - 1);
    y = std::clamp(y, -gBar, kHeight - 1);
}

void MarkDisplayDirty() {
    ApplyLogicalPresentation();
    gDirty = true;
}

}  // namespace sdl

bool OpenDisplay() {
    if (gWindow != nullptr)
        return true;
    StartupOptions defaults;
    int scale = ChooseScale();
    std::string requested = Environment("HOMM1_SCALE");
    if (!requested.empty())
        scale = std::max(1, std::atoi(requested.c_str()));
#ifdef __EMSCRIPTEN__
    // In a page the canvas keeps the image's own size and the page scales
    // it to the browser window (src/PLATFORM/Web/homm1.js). A resizable SDL
    // window would instead follow the canvas's shown size and letterbox the
    // image inside it.
    scale = 1;
    SDL_WindowFlags flags = 0;
#else
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    if (!SDL_CreateWindowAndRenderer(
            defaults.title, kWidth * scale, (kHeight + gBar) * scale, flags, &gWindow, &gRenderer)) {
        Log("cannot open a window: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(gRenderer, 0);
    ApplyLogicalPresentation();
    gTexture = SDL_CreateTexture(
        gRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kCanvasHeight);
    if (gTexture == nullptr) {
        Log("cannot create the display texture: %s", SDL_GetError());
        return false;
    }
    SetDisplayScaling(gScaling);
    if (gFullscreen)
        SDL_SetWindowFullscreen(gWindow, true);
    UpdateSystemCursor();
    InvalidateCanvas();
    Present(true);
    return true;
}

void CloseDisplay() {
    if (gTexture != nullptr)
        SDL_DestroyTexture(gTexture);
    if (gRenderer != nullptr)
        SDL_DestroyRenderer(gRenderer);
    if (gWindow != nullptr)
        SDL_DestroyWindow(gWindow);
    gTexture = nullptr;
    gRenderer = nullptr;
    gWindow = nullptr;
}

bool DisplayOpen() {
    return gWindow != nullptr;
}

void UpdateDisplay(
    const u8* pixels,
    int pitch,
    int sourceX,
    int sourceY,
    int width,
    int height,
    int destinationX,
    int destinationY
) {
    if (pixels == nullptr)
        return;
    // Clip against the display; the source is the game's 640x480 buffer, so
    // the same clip keeps the source in range.
    if (destinationX < 0) {
        sourceX -= destinationX;
        width += destinationX;
        destinationX = 0;
    }
    if (destinationY < 0) {
        sourceY -= destinationY;
        height += destinationY;
        destinationY = 0;
    }
    if (sourceX < 0) {
        destinationX -= sourceX;
        width += sourceX;
        sourceX = 0;
    }
    if (sourceY < 0) {
        destinationY -= sourceY;
        height += sourceY;
        sourceY = 0;
    }
    width = std::min({width, kWidth - destinationX, pitch - sourceX});
    height = std::min({height, kHeight - destinationY, kHeight - sourceY});
    if (width <= 0 || height <= 0)
        return;
    for (int row = 0; row < height; row++)
        std::memcpy(
            &gIndexed[static_cast<size_t>((destinationY + row) * kWidth + destinationX)],
            pixels + static_cast<size_t>((sourceY + row) * pitch + sourceX),
            static_cast<size_t>(width));
    InvalidatePixels({destinationX, destinationY + gBar, width, height});
    Present(false);
}

void SetPalette(const Color* colors, int first, int count) {
    std::array<bool, PALETTE_SIZE> changed{};
    int changes = 0;
    for (int i = 0; i < count && first + i < PALETTE_SIZE; i++) {
        const size_t index = static_cast<size_t>(first + i);
        const u32 pixel = Rgb(colors[i]);
        if (pixel == gPalettePixels[index])
            continue;
        gPalette[index] = colors[i];
        gPalettePixels[index] = pixel;
        changed[index] = true;
        ++changes;
    }
    if (changes == 0)
        return;
    if (changes > 32) {
        InvalidateCanvas();
    } else {
        // Palette cycling usually changes only water or a few decorations. Upload only their bounds.
        const size_t barPixels = static_cast<size_t>(gBar * kWidth);
        SDL_Rect damage{};
        for (int y = 0; y < kHeight + gBar; ++y) {
            int left = kWidth;
            int right = -1;
            for (int x = 0; x < kWidth; ++x) {
                const auto pixel = static_cast<size_t>(y * kWidth + x);
                const u8 index = pixel < barPixels || gChromeMask[pixel] ? gChrome[pixel] : gIndexed[pixel - barPixels];
                if (changed[index]) {
                    left = std::min(left, x);
                    right = x;
                }
            }
            if (right >= left) {
                const SDL_Rect row = {left, y, right - left + 1, 1};
                SDL_GetRectUnion(&damage, &row, &damage);
            }
        }
        if (!SDL_RectEmpty(&damage))
            InvalidatePixels(damage);
        if (gCursorSet && gCursor.colorCursor && !SDL_RectEmpty(&gLastCursorBounds))
            InvalidatePixels(gLastCursorBounds);
    }
    Present(false);
}

void Present(bool force) {
    if (gTexture == nullptr || !gDirty || gDisplayUpdateDepth > 0)
        return;
    u32 now = Ticks();
    if (!force && now - gLastPresent < kPresentInterval)
        return;
    gLastPresent = now;
    // Recompose both cursor positions to restore the pixels it covered.
    const SDL_Rect cursorBounds = CursorBounds();
    InvalidatePixels(gLastCursorBounds);
    InvalidatePixels(cursorBounds);
    gLastCursorBounds = cursorBounds;
    gDirty = false;
    if (!SDL_RectEmpty(&gDamage)) {
        const auto barPixels = static_cast<size_t>(gBar * kWidth);
        for (int y = gDamage.y; y < gDamage.y + gDamage.h; ++y) {
            const auto begin = static_cast<size_t>(y * kWidth + gDamage.x);
            const auto end = begin + static_cast<size_t>(gDamage.w);
            for (size_t pixel = begin; pixel < end; ++pixel)
                gPixels[pixel] = gPalettePixels[pixel < barPixels || gChromeMask[pixel] != 0
                                            ? gChrome[pixel]
                                            : gIndexed[pixel - barPixels]];
        }
        if (!SDL_RectEmpty(&cursorBounds))
            DrawCursor();
        SDL_UpdateTexture(gTexture, &gDamage, gPixels.data() + gDamage.y * kWidth + gDamage.x,
                          kWidth * static_cast<int>(sizeof(u32)));
        gDamage = {};
    }
    if (!gFullscreen || gScaling != DISPLAY_SCALING_FILL) {
        SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 255);
        SDL_RenderClear(gRenderer);
    }
    SDL_FRect source = {0, 0, static_cast<float>(kWidth), static_cast<float>(kHeight + gBar)};
    SDL_RenderTexture(gRenderer, gTexture, &source, nullptr);
    SDL_RenderPresent(gRenderer);
}

void BeginDisplayUpdate() {
    ++gDisplayUpdateDepth;
}

void EndDisplayUpdate() {
    --gDisplayUpdateDepth;
    Present(true);
}

void SetWindowSize(int width, int height) {
    if (gWindow == nullptr || gFullscreen || width <= 0 || height <= 0)
        return;
    int desktopWidth;
    int desktopHeight;
    DesktopSize(desktopWidth, desktopHeight);
    const double availableHeight = std::max(1, desktopHeight - 40);
    const double fullHeight = static_cast<double>(height) * (kHeight + gBar) / kHeight;
    const double fit = std::min({1.0, static_cast<double>(desktopWidth) / width, availableHeight / fullHeight});
    SDL_SetWindowSize(gWindow, std::max(1, static_cast<int>(width * fit)), std::max(1, static_cast<int>(fullHeight * fit)));
    ApplyLogicalPresentation();
    gDirty = true;
}

void DesktopSize(int& width, int& height) {
    SDL_Rect bounds = {0, 0, kWidth, kHeight};
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display != 0)
        SDL_GetDisplayUsableBounds(display, &bounds);
    width = bounds.w;
    height = bounds.h;
}

void SetChromeBar(int height) {
    height = std::clamp(height, 0, static_cast<int>(CHROME_BAR_MAX));
    if (height == gBar)
        return;
    int windowWidth = 0;
    int windowHeight = 0;
    if (gWindow != nullptr)
        SDL_GetWindowSize(gWindow, &windowWidth, &windowHeight);
    int oldBar = gBar;
    gBar = height;
    gChromeMask.fill(0);
    if (gRenderer != nullptr) {
        ApplyLogicalPresentation();
        if (!gFullscreen && windowHeight > 0) {
            int imageHeight = windowHeight * kHeight / (kHeight + oldBar);
            SDL_SetWindowSize(gWindow, windowWidth, imageHeight + gBar * imageHeight / kHeight);
        }
    }
    InvalidateCanvas();
}

int ChromeBar() {
    return gBar;
}

void UpdateChrome(const u8* pixels, const u8* mask) {
    size_t count = static_cast<size_t>(kWidth * (kHeight + gBar));
    std::memcpy(gChrome.data(), pixels, count);
    std::memcpy(gChromeMask.data(), mask, count);
    std::fill(gChromeMask.begin() + static_cast<std::ptrdiff_t>(count), gChromeMask.end(), u8{0});
    InvalidateCanvas();
}

void SetFullscreen(bool fullscreen) {
    gFullscreen = fullscreen;
    if (gWindow != nullptr)
        SDL_SetWindowFullscreen(gWindow, fullscreen);
    ApplyLogicalPresentation();
    gDirty = true;
}

void SetDisplayScaling(DisplayScaling scaling) {
    gScaling = scaling;
    ApplyLogicalPresentation();
    if (gTexture) {
        SDL_ScaleMode mode = scaling == DISPLAY_SCALING_SMOOTH ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
#if SDL_VERSION_ATLEAST(3, 4, 0)
        if (scaling == DISPLAY_SCALING_FILL)
            mode = SDL_SCALEMODE_PIXELART;
#endif
        if (!SDL_SetTextureScaleMode(gTexture, mode))
            SDL_SetTextureScaleMode(gTexture, SDL_SCALEMODE_NEAREST);
    }
    gDirty = true;
}

bool Fullscreen() {
    return gFullscreen;
}

const std::vector<DisplayResolution>& FullscreenResolutions() {
    static const auto resolutions = [] {
        std::vector<DisplayResolution> list;
        const SDL_DisplayID display = gWindow != nullptr ? SDL_GetDisplayForWindow(gWindow)
                                                          : SDL_GetPrimaryDisplay();
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, nullptr);
        if (modes != nullptr) {
            for (auto mode = modes; *mode != nullptr; ++mode) {
                const int width = (*mode)->w;
                const int height = (*mode)->h;
                if (width < kWidth || height < kHeight
                    || std::any_of(list.begin(), list.end(), [=](const DisplayResolution& entry) {
                        return entry.width == width && entry.height == height;
                    }))
                    continue;
                list.push_back({width, height});
            }
            SDL_free(modes);
        }
        return list;
    }();
    return resolutions;
}

bool SetFullscreenResolution(int width, int height) {
    if (gWindow == nullptr)
        return false;
    if (width == 0 && height == 0)
        return SDL_SetWindowFullscreenMode(gWindow, nullptr);
    SDL_DisplayMode mode;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(gWindow);
    return SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.0f, true, &mode)
        && mode.w == width && mode.h == height
        && SDL_SetWindowFullscreenMode(gWindow, &mode);
}

void CaptureDisplay(u8* rgb) {
    const int barPixels = gBar * kWidth;
    const int count = kWidth * (kHeight + gBar);
    for (int i = 0; i < count; i++) {
        u8 index = i < barPixels || gChromeMask[static_cast<size_t>(i)] != 0
                       ? gChrome[static_cast<size_t>(i)]
                       : gIndexed[static_cast<size_t>(i - barPixels)];
        const Color& color = gPalette[index];
        rgb[i * 3] = color.r;
        rgb[i * 3 + 1] = color.g;
        rgb[i * 3 + 2] = color.b;
    }
}

namespace {

bool WriteBmp(const char* hostPath, const std::vector<u8>& rgb, int height) {
    SDL_IOStream* file = SDL_IOFromFile(hostPath, "wb");
    if (file == nullptr)
        return false;
    const u32 rowBytes = kWidth * 3;
    const u32 imageBytes = rowBytes * static_cast<u32>(height);
    u8 header[54] = {'B', 'M'};
    auto put32 = [&header](int offset, u32 value) {
        for (int i = 0; i < 4; i++)
            header[offset + i] = static_cast<u8>(value >> (8 * i));
    };
    put32(2, 54 + imageBytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, kWidth);
    put32(22, static_cast<u32>(height));
    header[26] = 1;
    header[28] = 24;
    put32(34, imageBytes);
    bool ok = SDL_WriteIO(file, header, sizeof(header)) == sizeof(header);
    std::vector<u8> row(rowBytes);
    for (int y = height - 1; y >= 0 && ok; y--) {
        for (int x = 0; x < kWidth; x++) {
            const u8* source = &rgb[static_cast<size_t>((y * kWidth + x) * 3)];
            row[static_cast<size_t>(x * 3)] = source[2];
            row[static_cast<size_t>(x * 3 + 1)] = source[1];
            row[static_cast<size_t>(x * 3 + 2)] = source[0];
        }
        ok = SDL_WriteIO(file, row.data(), row.size()) == row.size();
    }
    return SDL_CloseIO(file) && ok;
}

}  // namespace

bool SaveDisplayBmp(const char* hostPath) {
    const int height = kHeight + gBar;
    std::vector<u8> rgb(static_cast<size_t>(kWidth * height * 3));
    CaptureDisplay(rgb.data());
    return WriteBmp(hostPath, rgb, height);
}

bool SaveReferenceBmp(const char* hostPath) {
    if (gReference == nullptr)
        return false;
    std::vector<u8> rgb(static_cast<size_t>(kWidth * kHeight * 3));
    for (size_t i = 0; i < gIndexed.size(); i++) {
        const Color& color = gPalette[gReference[i]];
        rgb[i * 3] = color.r;
        rgb[i * 3 + 1] = color.g;
        rgb[i * 3 + 2] = color.b;
    }
    return WriteBmp(hostPath, rgb, kHeight);
}

void SetReferenceImage(const u8* pixels) {
    gReference = pixels;
}

int CompareWithReference(int box[4]) {
    box[0] = kWidth;
    box[1] = kHeight;
    box[2] = -1;
    box[3] = -1;
    if (gReference == nullptr)
        return 0;
    int count = 0;
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x++) {
            size_t index = static_cast<size_t>(y * kWidth + x);
            if (gIndexed[index] == gReference[index])
                continue;
            count++;
            box[0] = std::min(box[0], x);
            box[1] = std::min(box[1], y);
            box[2] = std::max(box[2], x);
            box[3] = std::max(box[3], y);
        }
    }
    return count;
}

void SetCursorImage(const CursorImage* image) {
    gCursorSet = image != nullptr;
    if (image != nullptr)
        gCursor = *image;
    UpdateSystemCursor();
    gDirty = true;
}

void ShowCursor(bool show) {
    gCursorCount += show ? 1 : -1;
    UpdateSystemCursor();
    gDirty = true;
}

}  // namespace platform
