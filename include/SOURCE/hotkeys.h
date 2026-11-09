#ifndef HOMM1_SOURCE_HOTKEYS_H
#define HOMM1_SOURCE_HOTKEYS_H
#include <H1/Ints.h>

enum ShortcutContext {
    SHORTCUT_MAP, SHORTCUT_BATTLE, SHORTCUT_TOWN, SHORTCUT_ARMY, SHORTCUT_WELL, SHORTCUT_CONTEXT_COUNT
};
enum { GAME_SHORTCUT_COUNT = 32 };

struct GameShortcut {
    ShortcutContext context;
    i16 defaultKey;
    const char* setting;
    const char* label;
};

extern const GameShortcut gGameShortcuts[GAME_SHORTCUT_COUNT];
i16 ShortcutKey(i16 index);
char ShortcutLetter(i16 key);
i16 TranslateShortcut(i16 key, ShortcutContext context);
bool CanBindShortcut(i16 index, i16 key);
struct tag_message;
i16 ArmyShortcutTap(const tag_message& message);
void ConfigureShortcut(i16 index);

#endif
