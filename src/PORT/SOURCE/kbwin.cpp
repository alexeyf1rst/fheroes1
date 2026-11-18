// The native host of the game: the counterpart of src/SOURCE/kbwin.cpp,
// which is the Windows window, message loop, registry settings and CD probe.
// Everything here translates between the game's host functions (kbwin.h,
// mouseManager.h) and the platform layer; game logic stays in the shared
// units.

#include <H1/Ints.h>
#ifndef HOMM1_EDITOR
#include <BASE/executive.h>
#include <SOURCE/advManager.h>
#include <SOURCE/combatManager.h>
#endif

#include <SOURCE/kbwin.h>

#include <BASE/Misc.h>
#include <BASE/audio.h>
#include <BASE/heroWindow.h>
#include <BASE/heroWindowManager.h>
#include <BASE/goldButton.h>
#include <BASE/inputManager.h>
#include <BASE/message.h>
#include <BASE/miscwin.h>
#include <BASE/mouseManager.h>
#include <BASE/soundmgr.h>
#include <SOURCE/KB.h>
#include <SOURCE/cursorTypes.h>
#include <SOURCE/dialogTypes.h>
#include <BASE/textWidget.h>
#include <SOURCE/wingraph.h>
#ifdef HOMM1_EDITOR
#include <EDITOR/EDITOR.h>
#endif

#include <PLATFORM/File.h>
#include <PLATFORM/GameData.h>
#include <PLATFORM/Help.h>
#include <PLATFORM/Net.h>
#include <PLATFORM/Platform.h>

#include "../PortHost.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

i32 gForegroundApp = 1;
KBMenu gAppMenu = NULL;
i32 gClosingApp = 0;
i32 gUnusedWindowCount = 0;
#ifdef HOMM1_EDITOR
char gAppName[] = localization::Tr("editor.window.app_name");
char gTitle[] = localization::Tr("editor.window.title");
#else
char gAppName[] = localization::Tr("window.gAppName");
char gTitle[] = localization::Tr("window.gTitle");
#endif
i32 gUnusedWindowValue = -1;
i32 gMainWinScreenHeight = LOGICAL_SCREEN_HEIGHT;
i32 gMainWinScreenWidth = LOGICAL_SCREEN_WIDTH;
KBMenu gCurrentMenu = NULL;
i32 gTempX;
i32 gTempY;
i32 gTempValue;
char gCommandLine[KBWIN_COMMAND_LINE_CLEAR_SIZE];

namespace {

bool NativeDisplayMode() { return platform::gLogicalWidth != 640 || platform::gLogicalHeight != 480; }

struct NativePreset { int width; int height; };
const NativePreset kNativePresets[] = {{960, 600}, {1280, 800}, {1440, 900}};
int gSelectedNativePreset = 1;
std::filesystem::path NativeResolutionPath() {
    return std::filesystem::u8path(platform::ConfigDirectory()) / "native-resolution.txt";
}

enum DisplaySettingsWidgetId {
    DISPLAY_SETTINGS_MODE = 17001,
    DISPLAY_SETTINGS_PREVIOUS = 17002,
    DISPLAY_SETTINGS_RESOLUTION = 17003,
    DISPLAY_SETTINGS_NEXT = 17004,
    DISPLAY_SETTINGS_SCALING = 17005,
    DISPLAY_SETTINGS_CLOSE = 17006
};

struct DisplaySettingsState {
    heroWindow* window = nullptr;
    goldButton* mode = nullptr;
    goldButton* resolution = nullptr;
    goldButton* scaling = nullptr;
    textWidget* nativeStatus = nullptr;
};

DisplaySettingsState* gDisplaySettings = nullptr;

class DisplaySettingsBackdrop : public widget {
public:
    DisplaySettingsBackdrop() : widget(0, 0, 532, 328, WIDGET_ID_NONE, WIDGET_KIND_NONE) {}

    void Draw() override {
        const i32 x = m_owner->m_posX + m_x;
        const i32 y = m_owner->m_posY + m_y;
        const i32 frame = goldButton::MatchGold(47, 31, 8, true);
        const i32 dark = goldButton::MatchGold(10, 8, 6, true);
        const i32 inset = goldButton::MatchGold(31, 21, 10, true);
        bitmap* screen = gWindowManager->m_screen;
        FillBitmapArea(screen, x, y, m_width, m_height, frame);
        FillBitmapArea(screen, x + 3, y + 3, m_width - 6, m_height - 6, dark);
        FillBitmapArea(screen, x + 6, y + 6, m_width - 12, m_height - 12, inset);
        FillBitmapArea(screen, x + 12, y + 43, m_width - 24, 1, frame);
        FillBitmapArea(screen, x + 20, y + 224, m_width - 40, 1, frame);
    }

    i16 Main(tag_message& message) override { return widget::Main(message); }
};

std::string DisplayResolutionLabel() {
    if (gConfig.fullscreenWidth == 0 || gConfig.fullscreenHeight == 0)
        return localization::Tr("menu.fullscreen_resolution.desktop");
    return std::to_string(gConfig.fullscreenWidth) + " x " + std::to_string(gConfig.fullscreenHeight);
}

void RefreshDisplaySettings() {
    if (gDisplaySettings == nullptr)
        return;
    const char* mode = CURRENT_GRAPHICS_CONFIG.fullScreen
        ? localization::Tr("menu.display.fullscreen") : localization::Tr("menu.display.windowed");
    std::string modeLabel = std::string(localization::Tr("menu.display.mode")) + ": " + mode;
    gDisplaySettings->mode->SetText(const_cast<char*>(modeLabel.c_str()));
    if (NativeDisplayMode()) {
        const auto& preset = kNativePresets[gSelectedNativePreset];
        std::string label = std::string(localization::Tr("menu.display.native_selected")) + ": "
            + std::to_string(preset.width) + " x " + std::to_string(preset.height);
        gDisplaySettings->nativeStatus->SetText(const_cast<char*>(label.c_str()));
        return;
    }
    std::string resolutionLabel = std::string(localization::Tr("menu.display.resolution")) + ": "
        + DisplayResolutionLabel();
    gDisplaySettings->resolution->SetText(const_cast<char*>(resolutionLabel.c_str()));
    const i32 selected = std::clamp(gConfig.displayScaling, 0, 3);
    const char* scaleLabel = selected == platform::DISPLAY_SCALING_INTEGER
        ? localization::Tr("menu.scaling.integer")
        : selected == platform::DISPLAY_SCALING_SMOOTH ? localization::Tr("menu.scaling.smooth")
        : selected == platform::DISPLAY_SCALING_FILL ? localization::Tr("menu.scaling.fill")
                                                      : localization::Tr("menu.scaling.fit");
    std::string scalingLabel = std::string(localization::Tr("menu.display.scaling")) + ": "
        + scaleLabel;
    gDisplaySettings->scaling->SetText(const_cast<char*>(scalingLabel.c_str()));
}

i16 DisplaySettingsDialogHandler(tag_message& message) {
    if (message.type == MESSAGE_KEY_DOWN
        && (message.keyCode == INPUT_SCAN_ESCAPE || message.keyCode == INPUT_SCAN_ENTER)) {
        message.type = MESSAGE_WIDGET;
        message.id = DISPLAY_SETTINGS_CLOSE;
        FINISH_DIALOG_MESSAGE(message);
        return MESSAGE_DISPATCH_FORWARD;
    }
    if (message.type != MESSAGE_WIDGET || message.command != WIDGET_NOTIFY_DESELECT
        || (message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON))
        return MESSAGE_DISPATCH_CONTINUE;
    switch (message.id) {
        case DISPLAY_SETTINGS_MODE:
            AppMenuCommand(KBWIN_MENU_FULLSCREEN);
            break;
        case DISPLAY_SETTINGS_PREVIOUS:
        case DISPLAY_SETTINGS_NEXT: {
            const auto& modes = platform::FullscreenResolutions();
            i32 selected = 0;
            for (size_t i = 0; i < modes.size(); ++i) {
                if (modes[i].width == gConfig.fullscreenWidth && modes[i].height == gConfig.fullscreenHeight) {
                    selected = static_cast<i32>(i) + 1;
                    break;
                }
            }
            const i32 count = static_cast<i32>(std::min<size_t>(modes.size(), KBWIN_MENU_RESOLUTION_CHOICES)) + 1;
            selected = (selected + (message.id == DISPLAY_SETTINGS_NEXT ? 1 : count - 1)) % count;
            AppMenuCommand(KBWIN_MENU_RESOLUTION_FIRST + selected);
            break;
        }
        case DISPLAY_SETTINGS_SCALING: {
            static const i32 commands[] = {
                KBWIN_MENU_SCALE_FIT, KBWIN_MENU_SCALE_INTEGER, KBWIN_MENU_SCALE_SMOOTH, KBWIN_MENU_SCALE_FILL
            };
            const i32 next = (std::clamp(gConfig.displayScaling, 0, 3) + 1) % 4;
            AppMenuCommand(commands[next]);
            break;
        }
        case DISPLAY_SETTINGS_CLOSE:
            FINISH_DIALOG_MESSAGE(message);
            return MESSAGE_DISPATCH_FORWARD;
        default:
            return MESSAGE_DISPATCH_CONTINUE;
    }
    RefreshDisplaySettings();
    gDisplaySettings->window->DrawWindow();
    return MESSAGE_DISPATCH_CONSUME;
}

void ShowDisplaySettings() {
    heroWindow window((LOGICAL_SCREEN_WIDTH - 532) / 2, (LOGICAL_SCREEN_HEIGHT - 328) / 2,
                      532, 328, WINDOW_FLAG_SAVE_BACKGROUND | WINDOW_FLAG_OWNS_WIDGETS);
    window.AddWidget(new DisplaySettingsBackdrop(), WINDOW_Z_ORDER_BOTTOM);
    auto* title = new textWidget(16, 14, 500, 24, strdup(localization::Tr("menu.display.settings")),
                                 "smalfont.fnt", TEXT_WIDGET_PLAIN_COLOR, 17000, WIDGET_KIND_TEXT);
    title->m_color = goldButton::MatchGold(57, 43, 8, true);
    window.AddWidget(title, WINDOW_Z_ORDER_BOTTOM);
    auto* mode = new goldButton(28, 58, 476, 34, DISPLAY_SETTINGS_MODE, "");
    window.AddWidget(mode, WINDOW_Z_ORDER_APPEND);
    DisplaySettingsState state;
    state.window = &window;
    state.mode = mode;
    if (NativeDisplayMode()) {
        state.nativeStatus = new textWidget(28, 110, 476, 24, strdup(""),
            "smalfont.fnt", TEXT_WIDGET_PLAIN_COLOR, 17104, WIDGET_KIND_TEXT);
        state.nativeStatus->m_color = goldButton::MatchGold(57, 43, 8, true);
        window.AddWidget(state.nativeStatus, WINDOW_Z_ORDER_APPEND);
        window.AddWidget(new textWidget(28, 150, 476, 24,
            strdup(localization::Tr("menu.display.mac13")), "smalfont.fnt",
            TEXT_WIDGET_PLAIN_COLOR, 17105, WIDGET_KIND_TEXT), WINDOW_Z_ORDER_APPEND);
        window.AddWidget(new textWidget(28, 184, 476, 24,
            strdup(localization::Tr("menu.display.native_crisp")), "smalfont.fnt",
            TEXT_WIDGET_PLAIN_COLOR, 17106, WIDGET_KIND_TEXT), WINDOW_Z_ORDER_APPEND);
    } else {
        window.AddWidget(new goldButton(28, 110, 36, 32, DISPLAY_SETTINGS_PREVIOUS, "", goldButton::Left), WINDOW_Z_ORDER_APPEND);
        state.resolution = new goldButton(68, 110, 396, 32, DISPLAY_SETTINGS_RESOLUTION, "");
        window.AddWidget(state.resolution, WINDOW_Z_ORDER_APPEND);
        window.AddWidget(new goldButton(468, 110, 36, 32, DISPLAY_SETTINGS_NEXT, "", goldButton::Right), WINDOW_Z_ORDER_APPEND);
        state.scaling = new goldButton(28, 162, 476, 34, DISPLAY_SETTINGS_SCALING, "");
        window.AddWidget(state.scaling, WINDOW_Z_ORDER_APPEND);
    }
    window.AddWidget(new goldButton(198, 264, 136, 34, DISPLAY_SETTINGS_CLOSE,
                                   localization::Tr("menu.display.close")), WINDOW_Z_ORDER_APPEND);
    gDisplaySettings = &state;
    RefreshDisplaySettings();
    gWindowManager->DoDialog(&window, DisplaySettingsDialogHandler, false);
    gDisplaySettings = nullptr;
}

// ---------------------------------------------------------------- settings

// The original kept its settings in the registry under
// HKLM\SOFTWARE\Buka\3DO\Heroes of Might and Magic Platinum\1.000. The port
// keeps the same values, by the same names, in a text file.
std::string SettingsPath() {
    return platform::ConfigDirectory() + "heroes.cfg";
}

std::map<std::string, std::string> LoadSettings() {
    std::map<std::string, std::string> values;
    // The path is a host path (UTF-8); the file layer opens it on every host.
    std::string text;
    i32 file = FileOpen(SettingsPath().c_str(), FILE_OPEN_READ);
    if (file != FILE_INVALID) {
        char buffer[4096];
        i32 moved;
        while ((moved = FileRead(file, buffer, sizeof(buffer))) > 0)
            text.append(buffer, static_cast<size_t>(moved));
        FileClose(file);
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t split = line.find('=');
        if (split == std::string::npos || line.empty() || line[0] == '#')
            continue;
        values[line.substr(0, split)] = line.substr(split + 1);
    }
    return values;
}

void ReadSetting(const std::map<std::string, std::string>& values, const char* name, i32& value) {
    auto found = values.find(name);
    if (found != values.end())
        value = static_cast<i32>(std::strtol(found->second.c_str(), nullptr, 10));
}

void ReadSetting(
    const std::map<std::string, std::string>& values,
    const char* name,
    char* text,
    size_t capacity
) {
    auto found = values.find(name);
    if (found == values.end() || capacity == 0)
        return;
    std::snprintf(text, capacity, "%s", found->second.c_str());
}

struct SettingField {
    const char* name;
    i32* value;
};

std::vector<SettingField> IntegerSettings() {
    std::vector<SettingField> fields = {
        {"HMM1 MusicVolume", &gConfig.musicVolume},
        {"HMM1 FXVolume", &gConfig.soundVolume},
        {"HMM1 WalkSpeed", &gConfig.walkSpeed},
        {"HMM1 ShowRoute", &gConfig.showRoute},
        {"HMM1 BlackoutComputer", &gConfig.blackoutComputer},
        {"HMM1 SoundQuality", &gConfig.musicSource},
        {"HMM1 DirectConnectComPort", &gConfig.comPort[CONFIG_CONNECTION_DIRECT]},
        {"HMM1 DirectConnectBaudRate", &gConfig.baudRate[CONFIG_CONNECTION_DIRECT]},
        {"HMM1 ModemComPort", &gConfig.comPort[CONFIG_CONNECTION_MODEM]},
        {"HMM1 ModemBaudRate", &gConfig.baudRate[CONFIG_CONNECTION_MODEM]},
        {"HMM1 UseAutosave", &gConfig.autosave},
        {"HMM1 AutoCombatSpells", &gConfig.autoCombatSpells},
        {"HMM1 CombatAnimationSpeed", &gConfig.combatAnimationSpeed},
        {"HMM1 AutoResolveBattles", &gConfig.autoResolveBattles},
        {"HMM1 ShowCombatGrid", &gConfig.showCombatGrid},
        {"HMM1 CombatHealthBars", &gConfig.combatHealthBars},
        {"HMM1 ShowCombatRange", &gConfig.showCombatRange},
        {"HMM1 HideCombatDamage", &gConfig.hideCombatDamage},
        {"HMM1 ShowCombatOrder", &gConfig.showCombatOrder},
        {"HMM1 DisplayScaling", &gConfig.displayScaling},
        {"HMM1 FullscreenWidth", &gConfig.fullscreenWidth},
        {"HMM1 FullscreenHeight", &gConfig.fullscreenHeight},
        {"HMM1 FirstMapOffset", &gConfig.firstMapOffset},
        {"HMM1 CurrentMapOffset", &gConfig.currentMapOffset},
        {"HMM1 GameShowMenu", &gConfig.gfx[CONFIG_EXECUTABLE_GAME].showMenu},
        {"HMM1 GameWindowXLeft", &gConfig.gfx[CONFIG_EXECUTABLE_GAME].x},
        {"HMM1 GameWindowYTop", &gConfig.gfx[CONFIG_EXECUTABLE_GAME].y},
        {"HMM1 GameWindowWidth", &gConfig.gfx[CONFIG_EXECUTABLE_GAME].width},
        {"HMM1 GameWindowHeight", &gConfig.gfx[CONFIG_EXECUTABLE_GAME].height},
        {"HMM1 GameFullScreen", &gConfig.gfx[CONFIG_EXECUTABLE_GAME].fullScreen},
        {"HMM1 EditorShowMenu", &gConfig.gfx[CONFIG_EXECUTABLE_EDITOR].showMenu},
        {"HMM1 EditorWindowXLeft", &gConfig.gfx[CONFIG_EXECUTABLE_EDITOR].x},
        {"HMM1 EditorWindowYTop", &gConfig.gfx[CONFIG_EXECUTABLE_EDITOR].y},
        {"HMM1 EditorWindowWidth", &gConfig.gfx[CONFIG_EXECUTABLE_EDITOR].width},
        {"HMM1 EditorWindowHeight", &gConfig.gfx[CONFIG_EXECUTABLE_EDITOR].height},
        {"HMM1 EditorFullScreen", &gConfig.gfx[CONFIG_EXECUTABLE_EDITOR].fullScreen},
    };
    for (i16 index = 0; index < GAME_SHORTCUT_COUNT; ++index)
        fields.push_back({gGameShortcuts[index].setting, &gConfig.shortcutKeys[index]});
    return fields;
}

// ---------------------------------------------------------------- game data

std::string gCdRoot;

// ---------------------------------------------------------------- messages

void PostKey(u32 message, const platform::Event& event) {
    u32 virtualKey = event.returnKey ? INPUT_VIRTUAL_KEY_RETURN : 0;
    i32 messageData = static_cast<i32>((static_cast<u32>(event.scanCode) << 16)
                                      | (event.arrowKey ? INPUT_MESSAGE_EXTENDED_KEY : 0u) | 1u);
    KeyboardMessageHandler(NULL, message, virtualKey, messageData);
}

void PostMouse(u32 message, const platform::Event& event, u32 keyFlags = 0) {
    i32 messageData =
        static_cast<i32>((static_cast<u32>(event.y) << 16) | (static_cast<u32>(event.x) & 0xffff));
    MouseMessageHandler(NULL, message, keyFlags, messageData, event.timestamp);
}

// The original's WM_CLOSE: confirm leaving an unsaved game, then end.
void CloseRequested() {
    static bool gInClose = false;
    if (gInClose)
        return;
    gInClose = true;
    if (GameUnsaved() != 0) {
        NormalDialog(localization::Tr("adventure.confirm_quit"), NORMAL_DIALOG_TYPE_YES_NO);
        if (gWindowManager->m_dialogResult != NORMAL_DIALOG_CONFIRM) {
            gInClose = false;
            return;
        }
    }
    gClosingApp = 1;
    ShutDown(NULL);
}

void Dispatch(const platform::Event& event) {
#ifndef HOMM1_EDITOR
    static bool pendingMapPress = false;
    static platform::Event mapPress;
    const bool onAdventure = gAdvManager && gExec && gExec->m_managerListTail == gAdvManager && !gInDialog;
    if (pendingMapPress) {
        if (!onAdventure || event.type == platform::Event::FOCUS_LOST
            || event.type == platform::Event::KEY_DOWN || event.type == platform::Event::QUIT
            || event.type == platform::Event::MOUSE_WHEEL
            || (event.type == platform::Event::MOUSE_DOWN && event.button != platform::Event::BUTTON_LEFT)) {
            gAdvManager->EndMapDrag();
            pendingMapPress = false;
            platform::CaptureMouse(false);
        } else if (event.type == platform::Event::MOUSE_MOVE) {
            if (gAdvManager->DragMap(event.x, event.y))
                return;
        } else if (event.type == platform::Event::MOUSE_UP && event.button == platform::Event::BUTTON_LEFT) {
            const bool dragged = gAdvManager->DragMap(event.x, event.y);
            gAdvManager->EndMapDrag();
            pendingMapPress = false;
            platform::CaptureMouse(false);
            if (!dragged) {
                PostMouse(mapPress.doubleClick ? INPUT_MESSAGE_LEFT_DOUBLE : INPUT_MESSAGE_LEFT_DOWN, mapPress);
                PostMouse(INPUT_MESSAGE_LEFT_UP, event);
            }
            return;
        }
    }
#endif

    if (MenuHandleEvent(event))
        return;
    // The game image ends at the menu bar.
    if ((event.type == platform::Event::MOUSE_MOVE || event.type == platform::Event::MOUSE_DOWN
         || event.type == platform::Event::MOUSE_UP || event.type == platform::Event::MOUSE_WHEEL)
        && event.onChrome)
        return;
#ifndef HOMM1_EDITOR
    if (onAdventure && event.type == platform::Event::MOUSE_DOWN && event.button == platform::Event::BUTTON_LEFT
        && gAdvManager->BeginMapDrag(event.x, event.y)) {
        mapPress = event;
        pendingMapPress = true;
        platform::CaptureMouse(true);
        return;
    }
#endif
    switch (event.type) {
        case platform::Event::QUIT:
            CloseRequested();
            break;
        case platform::Event::KEY_DOWN:
            PostKey(INPUT_MESSAGE_KEY_DOWN, event);
            break;
        case platform::Event::KEY_UP:
            PostKey(INPUT_MESSAGE_KEY_UP, event);
            break;
        case platform::Event::MOUSE_WHEEL:
#ifndef HOMM1_EDITOR
            if (event.y >= 0 && event.wheelY != 0.0f) {
                const i32 delta = static_cast<i32>(std::lround(event.wheelY * INPUT_WHEEL_STEP));
                const i32 wheelDelta = delta != 0 ? delta : (event.wheelY > 0.0f ? 1 : -1);
                PostMouse(INPUT_MESSAGE_MOUSE_WHEEL, event, static_cast<u32>(wheelDelta));
            }
#endif
            break;
        case platform::Event::MOUSE_MOVE:
            PostMouse(INPUT_MESSAGE_MOUSE_MOVE, event);
            break;
        case platform::Event::MOUSE_DOWN:
            if (event.button == platform::Event::BUTTON_LEFT)
                PostMouse(
                    event.doubleClick ? INPUT_MESSAGE_LEFT_DOUBLE : INPUT_MESSAGE_LEFT_DOWN,
                    event);
            else
                PostMouse(
                    event.doubleClick ? INPUT_MESSAGE_RIGHT_DOUBLE : INPUT_MESSAGE_RIGHT_DOWN,
                    event);
            break;
        case platform::Event::MOUSE_UP:
            PostMouse(
                event.button == platform::Event::BUTTON_LEFT ? INPUT_MESSAGE_LEFT_UP
                                                             : INPUT_MESSAGE_RIGHT_UP,
                event);
            break;
        case platform::Event::FOCUS_GAINED:
            gForegroundApp = 1;
            break;
        case platform::Event::FOCUS_LOST:
            gForegroundApp = 0;
#ifndef HOMM1_EDITOR
            if (gAdvManager)
                gAdvManager->ResetLocatorInput();
#endif
            if (gInputManager) {
                gInputManager->m_tapKey = INPUT_SCAN_NONE;
                gInputManager->m_modifiers = MESSAGE_MODIFIER_NONE;
            }
            break;
        default:
            break;
    }
}

// The original's 10 ms WM_TIMER: palette cycling and, in the editor, the
// status line timeout.
void RunTimer() {
    static i32 gLastTimerTick = 0;
    static i32 gLastCycleTick = 0;
    i32 now = KBTickCount();
    if (now - gLastTimerTick < KBWIN_TIMER_INTERVAL)
        return;
    gLastTimerTick = now;
    if (now - gLastCycleTick > KBWIN_CYCLE_INTERVAL) {
        gLastCycleTick = now;
        CycleColors();
    }
#ifdef HOMM1_EDITOR
    if (gStatusTextClearTime && now > gStatusTextClearTime)
        ClearStatusText();
#endif
}

}  // namespace

// ---------------------------------------------------------------- entry

bool KBStartHost(const char* dataRoot, const char* gameArguments, i32 fullScreen) {
    const std::string nativeSize = platform::Environment("HOMM1_NATIVE_SIZE");
    if (!nativeSize.empty()) {
        int width = 0, height = 0;
        if (nativeSize == "auto") {
            width = 1280;
            height = 800;
            std::ifstream input(NativeResolutionPath());
            std::string saved;
            input >> saved;
            for (int i = 0; i < 3; ++i) {
                const auto& preset = kNativePresets[i];
                if (saved == std::to_string(preset.width) + "x" + std::to_string(preset.height)) {
                    width = preset.width;
                    height = preset.height;
                    gSelectedNativePreset = i;
                }
            }
        } else if (std::sscanf(nativeSize.c_str(), "%dx%d", &width, &height) != 2) {
            return false;
        }
        if (!platform::SetNativeResolution(width, height))
            return false;
        for (int i = 0; i < 3; ++i)
            if (kNativePresets[i].width == width && kNativePresets[i].height == height)
                gSelectedNativePreset = i;
        platform::gCanvasWidth = width;
        platform::gCanvasHeight = height;
        gMainWinScreenWidth = width;
        gMainWinScreenHeight = height;
    }
    platform::StartupOptions options;
    if (!platform::Startup(options))
        return false;
    std::vector<std::string> searched;
    std::string gameRoot = platform::FindGameRoot(dataRoot != NULL ? dataRoot : "", searched);
    if (gameRoot.empty()) {
        // Every folder tried, one per line, in the message's (UTF-8) text.
        std::string folders;
        for (const std::string& folder : searched)
            folders += "\n    " + folder;
        std::string format = platform::ToUtf8(localization::Tr("startup.data.not_found"));
        std::vector<char> text(format.size() + folders.size() + 1);
        std::snprintf(text.data(), text.size(), format.c_str(), folders.c_str());
        platform::ShowMessageUtf8(platform::ToUtf8(gTitle), text.data());
        return false;
    }
    FileSetRoot(gameRoot.c_str());
    gCdRoot = platform::FindCdRoot(gameRoot);
    platform::Log("game data: %s", gameRoot.c_str());

    memset(gCommandLine, 0, KBWIN_COMMAND_LINE_CLEAR_SIZE);
    strncpy(gCommandLine, gameArguments, KBWIN_COMMAND_LINE_LIMIT);
    if (EarlySetup() == 0)
        return false;
    if (fullScreen >= 0)
        CURRENT_GRAPHICS_CONFIG.fullScreen = fullScreen;
    srand(static_cast<u32>(KBTickCount()));
    InitGraphics();
    if (NativeDisplayMode()) {
        gConfig.fullscreenWidth = 0;
        gConfig.fullscreenHeight = 0;
    }
    if (!platform::SetFullscreenResolution(gConfig.fullscreenWidth, gConfig.fullscreenHeight)) {
        gConfig.fullscreenWidth = 0;
        gConfig.fullscreenHeight = 0;
    }
    platform::SetFullscreen(CURRENT_GRAPHICS_CONFIG.fullScreen != 0);
    if (platform::Environment("HOMM1_SCALE").empty())
        platform::SetWindowSize(CURRENT_GRAPHICS_CONFIG.width, CURRENT_GRAPHICS_CONFIG.height);
    return true;
}

i32 KBRunProgram(int argc, char** argv) {
    std::string dataRoot;
    std::string gameArguments;
    i32 fullScreen = -1;
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        if (argument == "--data" && i + 1 < argc) {
            dataRoot = argv[++i];
        } else if (argument == "--window") {
            fullScreen = 0;
        } else if (argument == "--fullscreen") {
            fullScreen = 1;
        } else if (argument == "--port" && i + 1 < argc) {
            long port = std::strtol(argv[++i], nullptr, 10);
            if (port > 0 && port < 65536)
                platform::net::CurrentSettings().port = static_cast<u16>(port);
        } else if (argument == "--join" && i + 1 < argc) {
            platform::net::CurrentSettings().join = argv[++i];
        } else if (argument == "--help") {
            std::printf(
                "usage: %s [--data DIR] [--window|--fullscreen] [--port N] [--join ADDRESS[:PORT]]\n"
                "          [OPTIONS]\n"
                "--port and --join: network, modem and direct-connection play over TCP\n"
                "(the host listens on the port, default %d; a guest joins the address).\n"
                "OPTIONS are the original's switches, e.g. /I0 to skip the intro.\n",
                argv[0], static_cast<int>(platform::net::DEFAULT_PORT));
            return 0;
        } else {
            if (!gameArguments.empty())
                gameArguments += ' ';
            gameArguments += argument;
        }
    }
    platform::net::LoadSettingsFromEnvironment();
    if (!KBStartHost(dataRoot.c_str(), gameArguments.c_str(), fullScreen))
        return 1;
    oldmain();
    ShutDown(NULL);
    return 0;
}

// ---------------------------------------------------------------- loop

i32 AppIdle(void) {
    return 1;
}

void AppExit(void) {
    MenuShutdown();
    CleanUpWinGraphics();
    CleanUpMenus();
    platform::Shutdown();
}

void Process1WindowsMessage(void) {
    static u32 gLastCall = 0;
    platform::PumpMusic();
    platform::Event event;
    bool handled = false;
    while (platform::PollEvent(event)) {
        Dispatch(event);
        handled = true;
    }
    RunTimer();
    // The original blocked in GetMessage for its next 10 ms timer tick every
    // 150 ms; its waits are otherwise busy loops around this call. A call
    // that follows the previous one within two milliseconds is such a loop.
    // Use the scheduler's sleep here: very short SDL event waits can spin on Windows.
    u32 now = platform::Ticks();
    if (!handled && now - gLastCall < 2)
        platform::Sleep(2);
    gLastCall = now;
    platform::Present(false);
}

i32 KBTickCount(void) {
    return static_cast<i32>(platform::Ticks());
}

// ---------------------------------------------------------------- window

void ResizeWindow(i32 x, i32 y, i32 width, i32 height) {
    if (CURRENT_GRAPHICS_CONFIG.fullScreen != 0)
        return;
    if (x != KBWIN_KEEP_POSITION)
        CURRENT_GRAPHICS_CONFIG.x = x;
    if (y != KBWIN_KEEP_POSITION)
        CURRENT_GRAPHICS_CONFIG.y = y;
    CURRENT_GRAPHICS_CONFIG.width = width;
    CURRENT_GRAPHICS_CONFIG.height = height;
    platform::SetWindowSize(width, height);
    UpdateDfltMenu(gAppMenu);
    WritePrefs();
}

i32 AppMenuCommand(i32 command) {
    if (command == KBWIN_MENU_DISPLAY_SETTINGS) {
        ShowDisplaySettings();
        return 0;
    }
    const auto& resolutions = platform::FullscreenResolutions();
    const i32 resolution = command - KBWIN_MENU_RESOLUTION_FIRST;
    if (resolution >= 0 && resolution <= KBWIN_MENU_RESOLUTION_CHOICES
        && static_cast<size_t>(resolution) <= resolutions.size()) {
        const auto mode = resolution == 0 ? platform::DisplayResolution{} : resolutions[resolution - 1];
        if (platform::SetFullscreenResolution(mode.width, mode.height)) {
            gConfig.fullscreenWidth = mode.width;
            gConfig.fullscreenHeight = mode.height;
            UpdateDfltMenu(gAppMenu);
            WritePrefs();
        }
        return 0;
    }
#ifndef HOMM1_EDITOR
    const auto& packs = platform::ScenarioPacks();
    if (command >= KBWIN_MENU_MAP_PACK_FIRST && command <= KBWIN_MENU_MAP_PACK_FIRST + static_cast<i32>(packs.size())) {
        const i32 choice = command - KBWIN_MENU_MAP_PACK_FIRST;
        platform::SetScenarioPack(choice == 0 ? std::string() : packs[static_cast<size_t>(choice - 1)]);
        UpdateDfltMenu(gAppMenu);
        WritePrefs();
        return 0;
    }
    if (command >= KBWIN_MENU_SHORTCUT_FIRST && command < KBWIN_MENU_SHORTCUT_FIRST + GAME_SHORTCUT_COUNT) {
        if (!gInDialog) {
            ConfigureShortcut(command - KBWIN_MENU_SHORTCUT_FIRST);
            UpdateDfltMenu(gAppMenu);
        }
        return 0;
    }
#endif
    switch (command) {
#ifndef HOMM1_EDITOR
        case KBWIN_MENU_BATTLE_SPEED_FIRST:
        case KBWIN_MENU_BATTLE_SPEED_FIRST + 1:
        case KBWIN_MENU_BATTLE_SPEED_FIRST + 2:
        case KBWIN_MENU_BATTLE_SPEED_FIRST + 3:
            gConfig.combatAnimationSpeed = command - KBWIN_MENU_BATTLE_SPEED_FIRST + 1;
            UpdateDfltMenu(gAppMenu);
            WritePrefs();
            break;
        case KBWIN_MENU_AUTO_RESOLVE:
            gConfig.autoResolveBattles = !gConfig.autoResolveBattles;
            UpdateDfltMenu(gAppMenu);
            WritePrefs();
            break;
#endif
        case KBWIN_MENU_ABOUT:
            platform::ShowMessage(gTitle, MenuAboutText().c_str());
            break;
        case KBWIN_MENU_HELP:
            OpenHelp();
            break;
        case KBWIN_MENU_SIZE_640_480:
            ResizeWindow(KBWIN_KEEP_POSITION, KBWIN_KEEP_POSITION, LOGICAL_SCREEN_WIDTH,
                         LOGICAL_SCREEN_HEIGHT);
            break;
        case KBWIN_MENU_SIZE_800_600:
            ResizeWindow(KBWIN_KEEP_POSITION, KBWIN_KEEP_POSITION, KBWIN_WIDTH_800,
                         KBWIN_HEIGHT_600);
            break;
        case KBWIN_MENU_SIZE_1024_768:
            ResizeWindow(KBWIN_KEEP_POSITION, KBWIN_KEEP_POSITION, KBWIN_WIDTH_1024,
                         KBWIN_HEIGHT_768);
            break;
        case KBWIN_MENU_SIZE_1280_960:
            ResizeWindow(KBWIN_KEEP_POSITION, KBWIN_KEEP_POSITION, 1280, 960);
            break;
        case KBWIN_MENU_SIZE_1280_1024:
            ResizeWindow(KBWIN_KEEP_POSITION, KBWIN_KEEP_POSITION, KBWIN_WIDTH_1280,
                         KBWIN_HEIGHT_1024);
            break;
        case KBWIN_MENU_FULLSCREEN:
            SetFullScreenStatus(1 - CURRENT_GRAPHICS_CONFIG.fullScreen);
            break;
        case KBWIN_MENU_SCALE_FIT:
        case KBWIN_MENU_SCALE_INTEGER:
        case KBWIN_MENU_SCALE_SMOOTH:
        case KBWIN_MENU_SCALE_FILL:
            gConfig.displayScaling = command == KBWIN_MENU_SCALE_FILL ? platform::DISPLAY_SCALING_FILL
                                                                    : command - KBWIN_MENU_SCALE_FIT;
            platform::SetDisplayScaling(static_cast<platform::DisplayScaling>(gConfig.displayScaling));
            UpdateDfltMenu(gAppMenu);
            WritePrefs();
            break;
        default:
            return HandleAppSpecificMenuCommands(command);
    }
    return 0;
}

// ---------------------------------------------------------------- help

// The original opened its help book with WinHelp(".\\HELP\\HEROES.HLP",
// HELP_FINDER), the contents page; both programs share it. Current hosts have
// no WinHelp, so the port converts the book and its contents file to one HTML
// document in its settings folder (again only when they change) and shows
// that page in the browser.
namespace {

const char kHelpBook[] = ".\\HELP\\HEROES.HLP";
const char kHelpContents[] = ".\\HELP\\HEROES.CNT";

bool ResolveExisting(const char* path, std::string& hostPath) {
    char resolved[FILE_PATH_CAPACITY];
    if (!FileExists(path) || !FileResolve(path, FILE_OPEN_READ, resolved, sizeof(resolved)))
        return false;
    hostPath = resolved;
    return true;
}

}  // namespace

bool HelpAvailable() {
    std::string hostPath;
    return ResolveExisting(kHelpBook, hostPath);
}

void OpenHelp() {
    std::string book;
    if (!ResolveExisting(kHelpBook, book)) {
        platform::Log("help is not available: no HELP\\HEROES.HLP in the game data");
        return;
    }
    std::string contents;
    ResolveExisting(kHelpContents, contents);
    std::string page;
    std::string error;
    platform::help::Labels labels;
    labels.contents = platform::ToUtf8(localization::Tr("help.contents"));
    labels.index = platform::ToUtf8(localization::Tr("help.index"));
    if (!platform::help::PrepareHelp(book, contents, platform::ConfigDirectory() + "help/", page,
                                     error, labels)) {
        platform::Log("help: %s", error.c_str());
        platform::ShowMessage(gTitle, "The help file HELP\\HEROES.HLP could not be read.");
        return;
    }
    platform::Log("help: %s", page.c_str());
    if (!platform::OpenDocument(page))
        platform::ShowMessage(gTitle, "The help could not be shown in the browser.");
}

// ---------------------------------------------------------------- menus

void KBChangeMenu(KBMenu menu) {
    if (menu == NULL)
        menu = gCurrentMenu;
    else
        gCurrentMenu = menu;
    gAppMenu = menu;
    if (CURRENT_GRAPHICS_CONFIG.showMenu && menu != NULL) {
        UpdateDfltMenu(menu);
        UpdateAppSpecificMenus(menu);
    }
    MenuRefresh();
}

void SetMenuStatus(b32 showMenu) {
    if (CURRENT_GRAPHICS_CONFIG.fullScreen && showMenu)
        return;
    CURRENT_GRAPHICS_CONFIG.showMenu = showMenu;
    KBChangeMenu(NULL);
    WritePrefs();
}

void SetNoDialogMenus(i32 menusEnabled) {
    static i32 gNoDialogMenusOn = 0;
    if (gNoDialogMenusOn && !menusEnabled)
        return;
    if (!gNoDialogMenusOn && menusEnabled)
        return;
    if (!gAppMenu)
        return;
    gNoDialogMenusOn = 1 - menusEnabled;
    SetMenus(gAppMenu, menusEnabled);
}

// ---------------------------------------------------------------- services

void KBErrorBox(const char* text, const char* title) {
    platform::ShowMessage(title, text);
}

void KBRequestClose(void) {
    CloseRequested();
}

void KBReleaseInstance(void) {}

void KBCaptureMouse(void) {
    platform::CaptureMouse(true);
}

void KBReleaseMouse(void) {
    platform::CaptureMouse(false);
}

void KBCursorPosition(i32* x, i32* y) {
    int pointerX;
    int pointerY;
    platform::MousePosition(pointerX, pointerY);
    *x = pointerX;
    *y = pointerY;
}

void KBShowSystemCursor(i32 visible) {
    platform::ShowCursor(visible != 0);
}

// ---------------------------------------------------------------- settings

void ReadPrefs(void) {
    std::map<std::string, std::string> values = LoadSettings();
    if (values.find("HMM1 MusicVolume") == values.end()) {
        memset(&gConfig, 0, sizeof(gConfig));
        SetGameDefaults();
        // The port starts in a window; F4 switches to full screen.
        for (i32 i = 0; i < CONFIG_EXECUTABLE_COUNT; i++)
            gConfig.gfx[i].fullScreen = 0;
            WritePrefs();
        return;
    }
    gConfig.autoResolveBattles = 1;
    if (const auto found = values.find("HMM1 MapPack"); found != values.end())
        platform::SetScenarioPack(found->second);
    for (const SettingField& field : IntegerSettings())
        ReadSetting(values, field.name, *field.value);
    for (i16 index = 0; index < GAME_SHORTCUT_COUNT; ++index) {
        if (gConfig.shortcutKeys[index] && !CanBindShortcut(index, gConfig.shortcutKeys[index]))
            gConfig.shortcutKeys[index] = 0;
    }
    if (gConfig.displayScaling < platform::DISPLAY_SCALING_FIT
        || gConfig.displayScaling > platform::DISPLAY_SCALING_FILL)
        gConfig.displayScaling = platform::DISPLAY_SCALING_FIT;
    if (NativeDisplayMode()) {
        gConfig.displayScaling = platform::DISPLAY_SCALING_FIT;
        gConfig.fullscreenWidth = 0;
        gConfig.fullscreenHeight = 0;
    }
    platform::SetDisplayScaling(static_cast<platform::DisplayScaling>(gConfig.displayScaling));
    ReadSetting(values, "HMM1 ModemInitString", gConfig.modemInitString,
                sizeof(gConfig.modemInitString));
    SetVolumes(gConfig.soundVolume, gConfig.musicVolume);
    SetMusicSource(gConfig.musicSource != 0);
}

void WritePrefs(void) {
    UpdateSystemOptionsMenu();
    std::ostringstream text;
    text << "# Heroes of Might and Magic settings (the original's registry values)\n";
    for (const SettingField& field : IntegerSettings())
        text << field.name << '=' << *field.value << '\n';
    text << "HMM1 MapPack=" << platform::SelectedScenarioPack() << '\n';
    text << "HMM1 ModemInitString=" << gConfig.modemInitString << '\n';
    std::string contents = text.str();
    FileReplace(SettingsPath().c_str(), contents.data(), static_cast<i32>(contents.size()));
}

// ---------------------------------------------------------------- CD

const std::string& CdRoot() {
    return gCdRoot;
}

i32 KBIsCDDrive(i32) {
    return 0;
}

void KBBeep(void) {}

// The original looked for its CD's music tracks on a CD drive; the port
// looks for the Tracks folder (see FindCdRoot) and otherwise plays the
// installed digital music from SOUND\.
i32 SetupCDDrive(void) {
    if (!FileExists("DATA\\HEROES.AGG"))
        return CD_SETUP_NO_DATA;
    if (gCdRoot.empty() && gConfig.musicSource == SOUND_MUSIC_SOURCE_CD) {
        gConfig.musicSource = SOUND_MUSIC_SOURCE_DIGITAL;
        SetMusicSource(SOUND_MUSIC_SOURCE_DIGITAL);
    }
    return CD_SETUP_READY;
}
