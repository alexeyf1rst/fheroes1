// No game assets required. Exercise the legacy input queue through its real
// platform entry point, including event bursts received after a slow frame.
#include <H1/Ints.h>
#include <BASE/border.h>
#include <BASE/bitmap.h>
#include <BASE/legacyCanvas.h>
#include <BASE/heroWindowManager.h>
#include <PLATFORM/Platform.h>
#include <SOURCE/wingraph.h>
#include <BASE/heroWindow.h>
#include <BASE/iconWidget.h>
#include <BASE/inputManager.h>
#include <BASE/textWidget.h>
#include <SOURCE/KB.h>
#include <SOURCE/advManager.h>
#include <SOURCE/kbwin.h>
#include "../../src/PLATFORM/SDL3/Internal.h"
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void Expect(bool value, const char* label) {
    std::printf("%s %s\n", value ? "ok" : "FAIL", label);
    failures += !value;
}
void Post(u32 type, int x, int y, u32 time, i32 delta = 0) {
    MouseMessageHandler(nullptr, type, static_cast<u32>(delta), (y << 16) | x, time);
}
tag_message Pop() {
    auto& input = *gInputManager;
    if (input.m_readIndex == input.m_writeIndex)
        return {};
    const auto event = input.m_eventRing[input.m_readIndex];
    input.m_readIndex = (input.m_readIndex + 1) % INPUT_EVENT_RING_CAPACITY;
    return event;
}
platform::Event NativeMouse(float x, float y, u32 type = SDL_EVENT_MOUSE_MOTION) {
    SDL_Event source{};
    source.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        source.motion.x = x;
        source.motion.y = y;
    } else {
        source.button.x = x;
        source.button.y = y;
        source.button.button = SDL_BUTTON_LEFT;
    }
    // A previous SDL_PollEvent may leave its poll sentinel queued. Remove
    // it before each isolated injected event so it cannot end the next poll
    // before that event is read.
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    const bool pushed = SDL_PushEvent(&source);
    platform::Event event;
    const bool received = platform::PollEvent(event);
    std::printf("mouse host %.0f,%.0f push=%d received=%d type=%d local=%d,%d native=%d,%d chrome=%d SDL=%s\n",
                x, y, pushed, received, static_cast<int>(event.type), event.x, event.y,
                event.nativeX, event.nativeY, event.onChrome, SDL_GetError());
    return event;
}
void CheckRightClick(widget& item) {
    auto* window = new heroWindow;
    item.m_owner = window;
    item.m_width = item.m_height = 30;
    tag_message down{};
    down.type = MESSAGE_RIGHT_BUTTON_DOWN;
    down.x = down.y = 10;
    item.Main(down);
    Expect(down.type == MESSAGE_WIDGET && down.command == WIDGET_NOTIFY_SELECT
           && (down.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON), "right press remains a right press");
    tag_message up{};
    up.type = MESSAGE_RIGHT_BUTTON_UP;
    item.Main(up);
    Expect(up.type == MESSAGE_WIDGET && up.command == WIDGET_NOTIFY_DESELECT
           && (up.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON), "right release cannot trigger a left-click action");
}
}

int main() {
    gMainWinScreenWidth = 640;
    gMainWinScreenHeight = 480;
    gInputManager = new inputManager;
    gInputManager->Open(1);
    Post(INPUT_MESSAGE_LEFT_DOWN, 500, 190, 100);
    for (int i = 0; i < 1000; ++i)
        Post(INPUT_MESSAGE_MOUSE_MOVE, 500 + i % 3, 191, 101 + i);
    Post(INPUT_MESSAGE_LEFT_UP, 502, 191, 1200);
    Post(INPUT_MESSAGE_LEFT_DOWN, 501, 191, 1400);
    Post(INPUT_MESSAGE_LEFT_UP, 501, 191, 1450);
    Expect(Pop().type == MESSAGE_LEFT_BUTTON_DOWN, "1000 motions do not evict the first click");
    const auto motion = Pop();
    Expect(motion.type == MESSAGE_MOUSE_MOVE && motion.mouseTimestamp == 1100,
           "a motion burst retains its final position and time");
    Expect(Pop().type == MESSAGE_LEFT_BUTTON_UP, "first release survives a motion burst");
    Expect(Pop().type == MESSAGE_LEFT_BUTTON_DOWN, "second press survives a motion burst");
    Expect(Pop().type == MESSAGE_LEFT_BUTTON_UP && Pop().type == MESSAGE_NONE,
           "button order is intact with no stale motion backlog");
    Post(INPUT_MESSAGE_MOUSE_WHEEL, 500, 230, 2000, -12);
    Post(INPUT_MESSAGE_MOUSE_WHEEL, 500, 230, 2010, -18);
    Expect(Pop().value == -30, "fractional wheel deltas are preserved when coalesced");
    Post(INPUT_MESSAGE_MOUSE_WHEEL, 500, 230, 2100, 120);
    Post(INPUT_MESSAGE_MOUSE_WHEEL, 500, 230, 2110, -120);
    Expect(Pop().value == 120 && Pop().value == -120, "wheel direction changes remain separate");

    advManager adventure;
    tag_message click{};
    click.mouseTimestamp = 3000;
    Expect(!adventure.LocatorDoubleClick(9, 3, click), "first marker click only selects");
    click.mouseTimestamp += 450;
    Expect(adventure.LocatorDoubleClick(9, 3, click), "second marker click opens after 450 ms");
    click.mouseTimestamp += 100;
    Expect(!adventure.LocatorDoubleClick(9, 3, click), "opening consumes the double-click pair");
    click.mouseTimestamp += 100;
    Expect(!adventure.LocatorDoubleClick(9, 4, click), "nearby heroes are not the same double click");
    adventure.ResetLocatorInput();
    Expect(!adventure.LocatorDoubleClick(9, 4, click), "focus reset prevents a stale double click");
    click.modifiers = MESSAGE_MODIFIER_ALT;
    Expect(!adventure.LocatorDoubleClick(9, 4, click), "reorder modifier does not open the hero");
    auto* source = new bitmap(BITMAP_TYPE_MEMORY, 640, 3);
    auto* destination = new bitmap(BITMAP_TYPE_MEMORY, 1280, 4);
    for (int i = 0; i < 640 * 3; ++i) source->m_pixels[i] = i / 640 + 1;
    for (int i = 0; i < 1280 * 4; ++i) destination->m_pixels[i] = 0;
    source->CopyTo(destination, 800, 1, 10, 0, 20, 3);
    Expect(destination->m_pixels[1280 + 800] == 1
           && destination->m_pixels[2560 + 800] == 2
           && destination->m_pixels[3840 + 819] == 3
           && destination->m_pixels[1280 + 799] == 0,
           "legacy bitmap copies preserve source and HD destination row pitches");
    gWindowManager = new heroWindowManager;
    auto* nativeScreen = new bitmap(BITMAP_TYPE_MEMORY, 1280, 800);
    nativeScreen->m_pixels[0] = 77;
    gWindowManager->m_screen = nativeScreen;
    gInitWin = nativeScreen->m_pixels;
    platform::SetNativeResolution(1280, 800);
    platform::gCanvasWidth = gMainWinScreenWidth = 1280;
    platform::gCanvasHeight = gMainWinScreenHeight = 800;
    Expect(SDL_Init(SDL_INIT_EVENTS), "SDL event subsystem initializes for native mouse tests");
    platform::sdl::SetPointer(330, 170, true);
    {
        LegacyCanvas legacy;
        int enteredX, enteredY;
        bool enteredInside;
        platform::sdl::PointerPosition(enteredX, enteredY, enteredInside);
        Expect(enteredX == 330 && enteredY == 170 && enteredInside,
               "entering a legacy scope preserves native pointer storage");
        platform::MousePosition(enteredX, enteredY);
        Expect(enteredX == 10 && enteredY == 10,
               "entering a legacy scope updates game pointer queries");
        platform::SetChromeBar(20);
        const auto centered = NativeMouse(330, 190);
        Expect(centered.type == platform::Event::MOUSE_MOVE && centered.x == 10 && centered.y == 10
               && centered.nativeX == 330 && centered.nativeY == 170 && !centered.onChrome,
               "native mouse coordinates translate to the centered legacy canvas once");
        const auto topMargin = NativeMouse(330, 179);
        const auto leftMargin = NativeMouse(319, 190);
        const auto bottomMargin = NativeMouse(330, 660);
        Expect(topMargin.y == -1 && !topMargin.onChrome
               && leftMargin.x == -1 && !leftMargin.onChrome
               && bottomMargin.y == 480 && !bottomMargin.onChrome,
               "black margins remain outside the legacy canvas without becoming chrome");
        const auto chrome = NativeMouse(330, 10);
        Expect(chrome.x == 330 && chrome.y == -10 && chrome.onChrome
               && chrome.nativeX == 330 && chrome.nativeY == -10,
               "actual menu chrome retains native coordinates");
        Expect(NativeMouse(330, 179, SDL_EVENT_MOUSE_BUTTON_DOWN).type == platform::Event::NONE,
               "a press in the upper margin cannot activate game or menu controls");
        const auto marginRelease = NativeMouse(330, 179, SDL_EVENT_MOUSE_BUTTON_UP);
        Expect(marginRelease.type == platform::Event::MOUSE_UP && marginRelease.y == -1
               && !marginRelease.onChrome,
               "a release in the upper margin can end a game gesture");
        NativeMouse(330, 190);
        int pointerX, pointerY;
        bool pointerInside;
        platform::sdl::PointerPosition(pointerX, pointerY, pointerInside);
        Expect(pointerX == 330 && pointerY == 170 && pointerInside,
               "SDL pointer storage retains native coordinates inside a legacy scope");
        platform::MousePosition(pointerX, pointerY);
        Expect(pointerX == 10 && pointerY == 10,
               "game pointer queries return legacy-local coordinates");
        platform::SetChromeBar(0);
        Expect(platform::LegacyCanvas() && platform::gCanvasWidth == 640
               && platform::gCanvasHeight == 480 && gWindowManager->m_screen != nativeScreen,
               "legacy screens use an isolated original-size framebuffer");
        {
            LegacyCanvas nested;
            nested.Restore();
            Expect(platform::LegacyCanvas(), "closing a nested screen retains its parent canvas");
        }
        legacy.Restore();
        int restoredX, restoredY;
        platform::MousePosition(restoredX, restoredY);
        Expect(restoredX == 330 && restoredY == 170,
               "leaving a legacy scope preserves the native pointer location");
        Expect(!platform::LegacyCanvas() && gWindowManager->m_screen == nativeScreen
               && platform::gCanvasWidth == 1280 && gMainWinScreenHeight == 800
               && nativeScreen->m_pixels[0] == 77,
               "closing a legacy screen restores native dimensions and untouched map pixels");
    }
    CheckRightClick(*new textWidget);
    CheckRightClick(*new iconWidget);
    CheckRightClick(*new border);
    std::fflush(stdout);
    // These asset-free widgets deliberately have no resource manager.
    std::_Exit(failures ? 1 : 0);
}
