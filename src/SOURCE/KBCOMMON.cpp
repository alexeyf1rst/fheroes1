#include <H1/Ints.h>

#include <SOURCE/kbwin.h>

#include <BASE/Misc.h>
#include <BASE/audio.h>
#include <BASE/heroWindow.h>
#include <BASE/inputManager.h>
#include <BASE/message.h>
#include <BASE/miscwin.h>
#include <BASE/soundmgr.h>
#include <SOURCE/KB.h>
#include <SOURCE/cursorTypes.h>
#include <SOURCE/wingraph.h>

#include <stdio.h>
#include <string.h>

const GameShortcut gGameShortcuts[GAME_SHORTCUT_COUNT] = {
    {SHORTCUT_MAP, INPUT_SCAN_S, "MapSave", localization::Tr("shortcut.map.save")},
    {SHORTCUT_MAP, INPUT_SCAN_M, "MapMove", localization::Tr("shortcut.map.move")},
    {SHORTCUT_MAP, INPUT_SCAN_E, "MapEndTurn", localization::Tr("shortcut.map.end")},
    {SHORTCUT_MAP, INPUT_SCAN_H, "MapHero", localization::Tr("shortcut.map.hero")},
    {SHORTCUT_MAP, INPUT_SCAN_T, "MapTown", localization::Tr("shortcut.map.town")},
    {SHORTCUT_MAP, INPUT_SCAN_C, "MapSpell", localization::Tr("shortcut.cast")},
    {SHORTCUT_MAP, INPUT_SCAN_K, "MapOverview", localization::Tr("shortcut.map.overview")},
    {SHORTCUT_BATTLE, INPUT_SCAN_A, "BattleAuto", localization::Tr("shortcut.battle.auto")},
    {SHORTCUT_BATTLE, INPUT_SCAN_Q, "BattleQuick", localization::Tr("shortcut.battle.quick")},
    {SHORTCUT_BATTLE, INPUT_SCAN_C, "BattleSpell", localization::Tr("shortcut.cast")},
    {SHORTCUT_BATTLE, INPUT_SCAN_T, "BattleOrder", localization::Tr("shortcut.battle.order")},
    {SHORTCUT_BATTLE, INPUT_SCAN_P, "BattleAutoSpells", localization::Tr("shortcut.battle.auto_spells")},
    {SHORTCUT_TOWN, INPUT_SCAN_W, "TownWell", localization::Tr("shortcut.town.well")},
    {SHORTCUT_TOWN, INPUT_SCAN_B, "TownBuild", localization::Tr("shortcut.town.build")},
    {SHORTCUT_TOWN, INPUT_SCAN_S, "TownMagic", localization::Tr("shortcut.town.magic")},
    {SHORTCUT_TOWN, INPUT_SCAN_X, "TownExchange", localization::Tr("shortcut.town.exchange")},
    {SHORTCUT_MAP, INPUT_SCAN_A, "MapAdventureOptions", localization::Tr("shortcut.map.adventure_options")},
    {SHORTCUT_MAP, INPUT_SCAN_F, "MapFileOptions", localization::Tr("shortcut.map.file_options")},
    {SHORTCUT_MAP, INPUT_SCAN_P, "MapPuzzle", localization::Tr("shortcut.map.puzzle")},
    {SHORTCUT_MAP, INPUT_SCAN_I, "MapScenario", localization::Tr("shortcut.map.scenario")},
    {SHORTCUT_MAP, INPUT_SCAN_D, "MapDig", localization::Tr("shortcut.map.dig")},
    {SHORTCUT_MAP, INPUT_SCAN_V, "MapViewWorld", localization::Tr("shortcut.map.view_world")},
    {SHORTCUT_MAP, INPUT_SCAN_L, "MapLoad", localization::Tr("shortcut.map.load")},
    {SHORTCUT_BATTLE, INPUT_SCAN_R, "BattleRetreat", localization::Tr("shortcut.battle.retreat")},
    {SHORTCUT_BATTLE, INPUT_SCAN_S, "BattleSurrender", localization::Tr("shortcut.battle.surrender")},
    {SHORTCUT_BATTLE, INPUT_SCAN_O, "BattleOptions", localization::Tr("shortcut.battle.options")},
    {SHORTCUT_TOWN, INPUT_SCAN_N, "TownShipyard", localization::Tr("shortcut.town.shipyard")},
    {SHORTCUT_TOWN, INPUT_SCAN_T, "TownThievesGuild", localization::Tr("shortcut.town.thieves_guild")},
    {SHORTCUT_TOWN, INPUT_SCAN_R, "TownTavern", localization::Tr("shortcut.town.tavern")},
    {SHORTCUT_WELL, INPUT_SCAN_M, "WellRecruitAll", localization::Tr("shortcut.town.recruit_all")},
    {SHORTCUT_ARMY, INPUT_SCAN_X, "ArmyExchange", localization::Tr("shortcut.town.exchange")},
    {SHORTCUT_ARMY, INPUT_SCAN_D, "ArmyDismiss", localization::Tr("shortcut.army.dismiss")},
};

char ShortcutLetter(i16 key) {
    const i16 keys[] = {INPUT_SCAN_A, INPUT_SCAN_B, INPUT_SCAN_C, INPUT_SCAN_D, INPUT_SCAN_E,
                       INPUT_SCAN_F, INPUT_SCAN_G, INPUT_SCAN_H, INPUT_SCAN_I, INPUT_SCAN_J,
                       INPUT_SCAN_K, INPUT_SCAN_L, INPUT_SCAN_M, INPUT_SCAN_N, INPUT_SCAN_O,
                       INPUT_SCAN_P, INPUT_SCAN_Q, INPUT_SCAN_R, INPUT_SCAN_S, INPUT_SCAN_T,
                       INPUT_SCAN_U, INPUT_SCAN_V, INPUT_SCAN_W, INPUT_SCAN_X, INPUT_SCAN_Y, INPUT_SCAN_Z};
    for (i16 index = 0; index < 26; ++index) {
        if (key == keys[index])
            return 'A' + index;
    }
    return '\0';
}

i16 ShortcutKey(i16 index) {
    return gConfig.shortcutKeys[index] ? gConfig.shortcutKeys[index] : gGameShortcuts[index].defaultKey;
}

i16 TranslateShortcut(i16 key, ShortcutContext context) {
    for (i16 index = 0; index < GAME_SHORTCUT_COUNT; ++index) {
        const GameShortcut& action = gGameShortcuts[index];
        if (action.context == context && ShortcutKey(index) == key)
            return action.defaultKey;
        if (action.context == context && action.defaultKey == key)
            return INPUT_SCAN_NONE;
    }
    return key;
}

bool CanBindShortcut(i16 index, i16 key) {
    const char letter = ShortcutLetter(key);
    if (!letter)
        return false;
    const ShortcutContext context = gGameShortcuts[index].context;
    const char* fixedKeys[] = {"ONQ", "HU", "I", "", ""};
    if (strchr(fixedKeys[context], letter))
        return false;
    for (i16 other = 0; other < GAME_SHORTCUT_COUNT; ++other) {
        if (other != index && gGameShortcuts[other].context == context
            && (gGameShortcuts[other].defaultKey == key || ShortcutKey(other) == key))
            return false;
    }
    return true;
}

i16 ArmyShortcutTap(const tag_message& message) {
    if (message.type != MESSAGE_KEY_UP || !(message.modifiers & MESSAGE_MODIFIER_TAP))
        return MESSAGE_MODIFIER_NONE;
    switch (message.keyCode) {
        case INPUT_SCAN_CONTROL: return MESSAGE_MODIFIER_CONTROL;
        case INPUT_SCAN_ALT: return MESSAGE_MODIFIER_ALT;
        case INPUT_SCAN_LEFT_SHIFT:
        case INPUT_SCAN_RIGHT_SHIFT: return MESSAGE_MODIFIER_LEFT_SHIFT;
    }
    return MESSAGE_MODIFIER_NONE;
}

// The host-independent part of kbwin.cpp: settings defaults, window text
// setup, assertions and token search. Both programs and both hosts link it.

void SetGameDefaults(void) {
    i32 i;

    gConfig.musicVolume = SOUND_VOLUME_100;
    gConfig.soundVolume = SOUND_VOLUME_100;
    gConfig.autosave = 1;
    gConfig.autoCombatSpells = 1;
    gConfig.autoResolveBattles = 1;
    gConfig.showCombatOrder = 0;
    gConfig.displayScaling = 0;
    memset(gConfig.shortcutKeys, 0, sizeof(gConfig.shortcutKeys));
    gConfig.showRoute = 1;
    gConfig.blackoutComputer = 0;
    for (i = CONFIG_EXECUTABLE_GAME; i < CONFIG_EXECUTABLE_COUNT; i++) {
        gConfig.gfx[i].showMenu = 1;
        gConfig.gfx[i].x = DEFAULT_WINDOW_ORIGIN;
        gConfig.gfx[i].y = DEFAULT_WINDOW_ORIGIN;
        if (gMainVideoModeWidth <= LOGICAL_SCREEN_WIDTH && gDDrawAttached) {
            gConfig.gfx[i].fullScreen = 1;
            gConfig.gfx[i].width = DEFAULT_SMALL_WINDOW_WIDTH;
            gConfig.gfx[i].height = DEFAULT_SMALL_WINDOW_HEIGHT;
        } else {
            gConfig.gfx[i].fullScreen = 1;
            gConfig.gfx[i].width = LOGICAL_SCREEN_WIDTH;
            gConfig.gfx[i].height = LOGICAL_SCREEN_HEIGHT;
        }
    }
    gConfig.blackoutComputer = 0;
    gConfig.currentMapOffset = 0;
    gConfig.firstMapOffset = Random(0, DEFAULT_MAP_OFFSET_LIMIT);
    gConfig.musicSource = SOUND_MUSIC_SOURCE_CD;
    gFirstTimeThrough = true;
    gConfig.walkSpeed = WALK_SPEED_CANTER;
}

void SetWinText(heroWindow* window, i16 id) {
    i32 i;
    tag_message msg;
#ifdef HOMM1_EDITOR
    for (i = 0; i < WINDOW_TEXT_EDITOR_ENTRY_COUNT; i++) {
#else
    for (i = 0; i < WINDOW_TEXT_ENTRY_COUNT; i++) {
#endif
        if (gWinSetup[i].windowId == id) {
            SET_WIDGET_MESSAGE(msg, WIDGET_COMMAND_SET_TEXT, gWinSetup[i].widgetId);
            msg.text = gWinSetupText[i];
            window->BroadcastMessage(msg);
        }
    }
}

void ProcessAssert(i32 condition, char* file, i32 line) {
    i32 unusedAssertWord;
    if (condition == 0) {
        sprintf(gText, "Assert statement failed in module %s, line %d.", file, line);
        KBErrorBox(gText, "Assert Failure");
        unusedAssertWord = 0;
        ShutDown(gText);
    }
}

char* FindToken(char* text, char token) {
    i32 pos;
    i32 len;

    len = strlen(text);
    for (pos = 0; pos < len; pos++) {
        if (text[pos] == token)
            return text + pos;
    }
    return NULL;
}

char* FindLastToken(char* text, char token) {
    i32 pos;
    i32 len;

    len = strlen(text);
    for (pos = len - 1; pos >= 0; pos--) {
        if (text[pos] == token)
            return text + pos;
    }
    return NULL;
}
