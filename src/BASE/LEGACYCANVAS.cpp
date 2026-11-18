#include <H1/Ints.h>

#include <BASE/legacyCanvas.h>

#if defined(HOMM1_PORT)
#include <BASE/bitmap.h>
#include <BASE/heroWindowManager.h>
#include <PLATFORM/Platform.h>
#include <SOURCE/KB.h>
#include <SOURCE/kbwin.h>
#include <SOURCE/wingraph.h>

#include <cstring>
#include <memory>
#include <new>

namespace platform {
int gCanvasWidth = 640;
int gCanvasHeight = 480;
}

namespace {
int gLegacyCanvasDepth = 0;
}

struct LegacyCanvas::State {
    heroWindowManager* manager = gWindowManager;
    bitmap* screen = manager->m_screen;
    void* initWin = gInitWin;
    int canvasWidth = platform::gCanvasWidth;
    int canvasHeight = platform::gCanvasHeight;
    i32 screenWidth = gMainWinScreenWidth;
    i32 screenHeight = gMainWinScreenHeight;
    b8 iconClip = gIconClipOn;
    b32 computeExtent = gComputeExtent;
    b32 limitToExtent = gLimitToExtent;
    b32 saveBiggestExtent = gSaveBiggestExtent;
    i32 minExtentX = gMinExtentX;
    i32 minExtentY = gMinExtentY;
    i32 maxExtentX = gMaxExtentX;
    i32 maxExtentY = gMaxExtentY;
    bitmap canvas{BITMAP_TYPE_MEMORY, 640, 480};
};

LegacyCanvas::LegacyCanvas() {
    if (gLegacyCanvasDepth == 0 && gWindowManager != nullptr
        && gWindowManager->m_screen != nullptr
        && (platform::gCanvasWidth != 640 || platform::gCanvasHeight != 480)) {
        // Allocate before changing globals, so allocation failure leaves the
        // caller's screen and nesting state intact.
        auto state = std::make_unique<State>();
        if (state->canvas.m_pixels == nullptr)
            throw std::bad_alloc();
        std::memset(state->canvas.m_pixels, 0, 640 * 480);
        platform::BeginDisplayUpdate();
        state->manager->m_screen = &state->canvas;
        gInitWin = state->canvas.m_pixels;
        platform::gCanvasWidth = 640;
        platform::gCanvasHeight = 480;
        gMainWinScreenWidth = 640;
        gMainWinScreenHeight = 480;
        gIconClipOn = false;
        gComputeExtent = false;
        gLimitToExtent = false;
        gSaveBiggestExtent = false;
        platform::SetLegacyCanvas(true);
        platform::SetReferenceImage(state->canvas.m_pixels);
        m_state = state.release();
        platform::EndDisplayUpdate();
    }
    ++gLegacyCanvasDepth;
    m_entered = true;
}

void LegacyCanvas::Restore() {
    if (!m_entered)
        return;
    m_entered = false;
    --gLegacyCanvasDepth;
    if (m_state == nullptr)
        return;
    platform::BeginDisplayUpdate();
    m_state->manager->m_screen = m_state->screen;
    gInitWin = m_state->initWin;
    platform::gCanvasWidth = m_state->canvasWidth;
    platform::gCanvasHeight = m_state->canvasHeight;
    gMainWinScreenWidth = m_state->screenWidth;
    gMainWinScreenHeight = m_state->screenHeight;
    gIconClipOn = m_state->iconClip;
    gComputeExtent = m_state->computeExtent;
    gLimitToExtent = m_state->limitToExtent;
    gSaveBiggestExtent = m_state->saveBiggestExtent;
    gMinExtentX = m_state->minExtentX;
    gMinExtentY = m_state->minExtentY;
    gMaxExtentX = m_state->maxExtentX;
    gMaxExtentY = m_state->maxExtentY;
    platform::SetLegacyCanvas(false);
    platform::SetReferenceImage(static_cast<const u8*>(m_state->initWin));
    platform::EndDisplayUpdate();
    delete m_state;
    m_state = nullptr;
}
#else
LegacyCanvas::LegacyCanvas() = default;
void LegacyCanvas::Restore() {}
#endif

LegacyCanvas::~LegacyCanvas() {
    Restore();
}
