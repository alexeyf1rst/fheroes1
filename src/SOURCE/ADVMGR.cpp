#include <H1/Ints.h>
#include <BASE/legacyCanvas.h>
#include <PLATFORM/Platform.h>

#include <BASE/audio.h>
#include <BASE/goldButton.h>
#include <algorithm>
#include <BASE/backdropWidget.h>
#include <BASE/baseManager.h>
#include <BASE/bitmap.h>
#include <BASE/BITS.h>
#include <BASE/bmap2.h>
#include <BASE/executive.h>
#include <BASE/font.h>
#include <BASE/heroWindow.h>
#include <BASE/heroWindowManager.h>
#include <BASE/icon.h>
#include <BASE/Icon2b.h>
#include <BASE/Icond2b.h>
#include <BASE/Iconm2b.h>
#include <BASE/iconWidget.h>
#include <BASE/inputManager.h>
#include <BASE/message.h>
#include <BASE/Misc.h>
#include <BASE/miscwin.h>
#include <BASE/mouseManager.h>
#include <BASE/resourceManager.h>
#include <BASE/sample.h>
#include <BASE/soundmgr.h>
#include <BASE/textWidget.h>
#include <BASE/TILE.h>
#include <BASE/tileset.h>
#include <BASE/widget.h>
#include <SOURCE/advManager.h>
#include <SOURCE/appMenu.h>
#include <SOURCE/armyGroup.h>
#include <SOURCE/campaignTypes.h>
#include <SOURCE/combatManager.h>
#include <SOURCE/EVENTS.h>
#include <SOURCE/fileRequester.h>
#include <SOURCE/game.h>
#include <SOURCE/hero.h>
#include <SOURCE/highScoreManager.h>
#include <SOURCE/KB.h>
#include <SOURCE/kbwin.h>
#include <SOURCE/mapCell.h>
#include <SOURCE/mapObjectTypes.h>
#include <SOURCE/NOOPT.h>
#include <SOURCE/philAI.h>
#include <SOURCE/playerData.h>
#include <SOURCE/REMOTE.h>
#include <SOURCE/remoteRecords.h>
#include <SOURCE/searchArray.h>
#include <SOURCE/town.h>
#include <SOURCE/wingraph.h>

#include <iterator>
#include <optional>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

i32 s_drawCloudFrame;
u16 s_drawGroundTile;
b8 s_drawFlipCloud;
u8 s_drawTileset;
b32 s_drawCovered;
i32 s_drawStoneTile;

// Adventure sprites clip to the map rectangle, which can end partway through a cell.
extern "C" u8 gDimPalette[256];
void DrawAdventureIcon(icon* source, bitmap* destination, i32 x, i32 y, i32 frame,
                       i32 offsetMode, bool flipped, bool dim) {
    if (!source || frame < 0 || frame >= source->m_frameCount
        || static_cast<u32>(frame + 1) * sizeof(IconEntry) > source->m_dataSize)
        return;
    const IconEntry entry = source->m_frames[frame];
    const int adjust = offsetMode ? (entry.x >> 1) - ((entry.x - (entry.x >> 1)) >> 1) : entry.x;
    const int startX = flipped ? x - adjust : x + adjust;
    int column = 0;
    int row = y + entry.y;
    const int right = std::min<int>(destination->m_width, ADVENTURE_VIEWPORT_WIDTH);
    const int bottom = std::min<int>(destination->m_height, ADVENTURE_VIEWPORT_HEIGHT);
    for (u32 at = entry.srcOffset; at < source->m_dataSize;) {
        const u8 command = source->m_data[at++];
        if (command & 0x80) {
            if (command == 0x80)
                return;
            column += command & 0x7f;
        } else if (command == 0) {
            column = 0;
            if (++row >= bottom)
                return;
        } else {
            for (int i = 0; i < command; ++i, ++column) {
                if (!dim && at >= source->m_dataSize)
                    return;
                const u8 color = dim ? 0 : source->m_data[at++];
                const int pixelX = startX + (flipped ? -column : column);
                if (row >= 0 && row < bottom && pixelX >= 0 && pixelX < right) {
                    u8& pixel = destination->m_pixels[row * destination->m_width + pixelX];
                    pixel = dim ? gDimPalette[pixel] : color;
                }
            }
        }
    }
}

namespace {

void AdventureTileToBitmap(tileset* tiles, u32 tile, bitmap* destination, i32 x, i32 y) {
    const int width = std::min<int>(CELL_PIXELS, ADVENTURE_VIEWPORT_WIDTH - x);
    const int height = std::min<int>(CELL_PIXELS, ADVENTURE_VIEWPORT_HEIGHT - y);
    if (x < 0 || y < 0 || width <= 0 || height <= 0)
        return;
    if (width == CELL_PIXELS && height == CELL_PIXELS) {
        TileToBitmap(tiles, tile, destination, x, y);
        return;
    }
    bitmap cell(BITMAP_TYPE_MEMORY, CELL_PIXELS, CELL_PIXELS);
    TileToBitmap(tiles, tile, &cell, 0, 0);
    cell.CopyTo(destination, x, y, 0, 0, width, height);
}

void AdventureFlipDimIconToBitmap(icon* ic, bitmap* bmp, i32 x, i32 y, i32 frame, i32 offsetMode) {
    DrawAdventureIcon(ic, bmp, x, y, frame, offsetMode, true, true);
}

void AdventureDimIconToBitmap(icon* ic, bitmap* bmp, i32 x, i32 y, i32 frame, i32 offsetMode) {
    DrawAdventureIcon(ic, bmp, x, y, frame, offsetMode, false, true);
}

void AdventureFlipClippedIconToBitmap(icon* ic, bitmap* bmp, i32 x, i32 y, i32 frame, i32 offsetMode) {
    DrawAdventureIcon(ic, bmp, x, y, frame, offsetMode, true, false);
}

void AdventureClippedIconToBitmap(icon* ic, bitmap* bmp, i32 x, i32 y, i32 frame, i32 offsetMode) {
    DrawAdventureIcon(ic, bmp, x, y, frame, offsetMode, false, false);
}

void AdventureFlipIconToBitmap(icon* ic, bitmap* bmp, i32 x, i32 y, i32 frame, i32 offsetMode) {
    DrawAdventureIcon(ic, bmp, x, y, frame, offsetMode, true, false);
}

void AdventureIconToBitmap(icon* ic, bitmap* bmp, i32 x, i32 y, i32 frame, i32 offsetMode) {
    DrawAdventureIcon(ic, bmp, x, y, frame, offsetMode, false, false);
}

constexpr int kAdventureIndicators = 3000;
constexpr int kLocatorStatsId = 3010;
constexpr int kRadarHeroHitRadius = 3;
constexpr u32 kLocatorDoubleClickTime = 650;
constexpr u32 kLocatorWheelGestureTime = 260;

// The 16:10 Lab layout uses the original 32-pixel rows and adds three more.
constexpr int kLocatorTop = 176;
constexpr int kLocatorRowHeight = 32;
constexpr int kLabLocatorRows = 7;
constexpr int kExtraTownLocator = 150;
constexpr int kLabLocatorExtension = (kLabLocatorRows - LOCATOR_VISIBLE_COUNT) * kLocatorRowHeight;

int LocatorExtension() {
    return LOGICAL_SCREEN_WIDTH == 960 && LOGICAL_SCREEN_HEIGHT == 600 ? kLabLocatorExtension : 0;
}

int LocatorRows() {
    return LOCATOR_VISIBLE_COUNT + LocatorExtension() / kLocatorRowHeight;
}

int TownLocatorButton(int slot) {
    return slot < LOCATOR_VISIBLE_COUNT ? ADVENTURE_CONTROL_TOWN_LOCATOR_1 + slot
                                       : kExtraTownLocator + slot - LOCATOR_VISIBLE_COUNT;
}

int TownLocatorSlot(int id) {
    for (int slot = 0; slot < LocatorRows(); ++slot)
        if (id == TownLocatorButton(slot))
            return slot;
    return -1;
}

int HeroLocatorSlot(int id) {
    const int offset = id - ADVENTURE_CONTROL_HERO_LOCATOR_1;
    if (offset >= 0 && offset % HERO_LOCATOR_WIDGET_STRIDE == 0
        && offset / HERO_LOCATOR_WIDGET_STRIDE < LocatorRows())
        return offset / HERO_LOCATOR_WIDGET_STRIDE;
    return -1;
}

int StatusY() {
    return BOTTOM_VIEW_PANEL_Y + LocatorExtension();
}

void ExtendAdventureLocators(heroWindow& window) {
    if (LocatorExtension() == 0)
        return;

    heroWindow templateWindow(0, 0, "adv_wind.bin");
    for (widget* item = templateWindow.m_widgetListTail; item;) {
        widget* previous = item->m_prev;
        const int id = item->m_id;
        const int heroOffset = id - HERO_LOCATOR_WIDGET_BASE;
        const int townSlot = id - ADVENTURE_CONTROL_TOWN_LOCATOR_1;
        const int highlightSlot = id - TOWN_LOCATOR_HIGHLIGHT_FIRST;
        constexpr int extraRows = kLabLocatorRows - LOCATOR_VISIBLE_COUNT;
        int newId = WIDGET_ID_NONE;
        if (heroOffset >= 0 && heroOffset < extraRows * HERO_LOCATOR_WIDGET_STRIDE)
            newId = id + LOCATOR_VISIBLE_COUNT * HERO_LOCATOR_WIDGET_STRIDE;
        else if (townSlot >= 0 && townSlot < extraRows)
            newId = TownLocatorButton(townSlot + LOCATOR_VISIBLE_COUNT);
        else if (highlightSlot >= 0 && highlightSlot < extraRows)
            newId = id + LOCATOR_VISIBLE_COUNT;
        if (newId != WIDGET_ID_NONE) {
            templateWindow.RemoveWidget(item);
            item->m_id = newId;
            item->m_x += LOGICAL_SCREEN_WIDTH - 640;
            item->m_y += LOCATOR_VISIBLE_COUNT * kLocatorRowHeight;
            window.AddWidget(item, WINDOW_Z_ORDER_APPEND);
        }
        item = previous;
    }
}


bool OwnHeroVisible(const hero& subject) {
    return !gAllBlack && MAP_CELL_IN_BOUNDS(subject.m_x, subject.m_y)
           && (gGame->m_mapExtra[subject.m_x][subject.m_y] & gCurPlayerBit);
}

class HeroLocatorStats : public textWidget {
public:
    explicit HeroLocatorStats(i16 slot)
        : textWidget(LOGICAL_SCREEN_WIDTH - 147, 178 + slot * 32, 39, 24, strdup(""), "smalfont.fnt",
                     TEXT_WIDGET_PLAIN_COLOR, kLocatorStatsId + slot, WIDGET_KIND_TEXT),
          m_slot(slot) {
        m_flags &= ~WIDGET_FLAG_ENABLED;
    }

    void Draw() override {
        const i32 index = gCurPlayerData->m_heroLocatorPage + m_slot;
        if (gAllBlack || !gThisNetHumanPlayer[gCurPlayer]
            || index < 0 || index >= gCurPlayerData->m_heroCount)
            return;
        hero* subject = gGame->GetHero(gCurPlayerData->m_heroIds[index]);
        char text[24];
        if (subject->m_eventFlags & HERO_EVENT_WAITING) {
            snprintf(text, sizeof(text), "%s", localization::Tr("hero.waiting.badge"));
            m_font->DrawString(text, m_x - 1, m_y + 12, m_color);
        }
        snprintf(text, sizeof(text), localization::Tr("hero.level.short"), subject->GetLevel(subject->m_experience));
        DrawRightAligned(text, m_y);
        snprintf(text, sizeof(text), "%d", subject->GetMovementPoints());
        DrawRightAligned(text, m_y + 13);
    }

private:
    void DrawRightAligned(char* text, i16 y) {
        const i16 x = m_x + m_width - m_font->LineWidth(text);
        m_font->DrawString(text, x - 1, y, 0);
        m_font->DrawString(text, x + 1, y, 0);
        m_font->DrawString(text, x + 1, y + 1, 0);
        m_font->DrawString(text, x, y, m_color);
    }

    i16 m_slot;
};

bool ReorderLocator(i8* ids, i8& page, i32 index, i16 modifiers) {
    if (gRemoteOn || !(modifiers & (MESSAGE_MODIFIER_SHIFT_KEYS | MESSAGE_MODIFIER_ALT)))
        return false;
    if (modifiers & MESSAGE_MODIFIER_DOUBLE_CLICK)
        return true;
    const i32 target = modifiers & MESSAGE_MODIFIER_SHIFT_KEYS ? 0 : std::max(0, index - 1);
    std::rotate(ids + target, ids + index, ids + index + 1);
    page = std::min<i32>(page, target);
    return true;
}

class AdventureIndicators : public textWidget {
public:
    AdventureIndicators() : textWidget(0, 0, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT, strdup(""), "smalfont.fnt",
                                        TEXT_WIDGET_PLAIN_COLOR, kAdventureIndicators, WIDGET_KIND_TEXT) {
        m_flags &= ~WIDGET_FLAG_ENABLED;
    }
    void Draw() override {
        if (!gThisNetHumanPlayer[gCurPlayer] || gAllBlack)
            return;
        const int crossShadow = goldButton::MatchGold(12, 3, 1, true);
        const int crossColor = goldButton::MatchGold(60, 12, 7, true);
        for (widget* item = m_owner->m_widgetListHead; item; item = item->m_next) {
            const int slot = TownLocatorSlot(item->m_id);
            if (slot < 0 || gCurPlayerData->m_townLocatorPage + slot >= gCurPlayerData->m_townCount)
                continue;
            const int townId = gCurPlayerData->m_townIds[gCurPlayerData->m_townLocatorPage + slot];
            if (townId == GAME_TOWN_NONE || !BitTest(gGame->m_townBuiltToday, townId))
                continue;
            const int x = item->m_x + item->m_width - 12;
            const int y = item->m_y + item->m_height - 12;
            for (int i = 0; i < 9; ++i) {
                FillBitmapArea(gWindowManager->m_screen, x + i, y + i, 3, 3, crossShadow);
                FillBitmapArea(gWindowManager->m_screen, x + 8 - i, y + i, 3, 3, crossShadow);
            }
            for (int i = 0; i < 9; ++i) {
                FillBitmapArea(gWindowManager->m_screen, x + 1 + i, y + 1 + i, 1, 1, crossColor);
                FillBitmapArea(gWindowManager->m_screen, x + 9 - i, y + 1 + i, 1, 1, crossColor);
            }
        }
    }
};

class MovementPreview : public textWidget {
public:
    MovementPreview() : textWidget(BOTTOM_VIEW_PANEL_X, StatusY(),
        BOTTOM_VIEW_PANEL_WIDTH, BOTTOM_VIEW_PANEL_HEIGHT, strdup(""), "smalfont.fnt",
        TEXT_WIDGET_PLAIN_COLOR, BOTTOM_VIEW_DRAW_LAST_WIDGET, WIDGET_KIND_TEXT) {
        m_flags &= ~WIDGET_FLAG_ENABLED;
    }
    void Draw() override {
        const int cost = gAdvManager->m_previewCost;
        if (cost < 0)
            return;
        const int available = gAdvManager->m_previewAvailable;
        char label[180];
        const int days = gAdvManager->m_previewDays;
        const char* today = gAdvManager->m_previewMeeting ? localization::Tr("adventure.route.meet_today")
                                                        : localization::Tr("adventure.route.today");
        if (days == 0)
            snprintf(label, sizeof(label), localization::Tr("adventure.route.cost"), today,
                     available * HERO_MOVEMENT_POINT_SCALE, gAdvManager->m_previewRemaining * HERO_MOVEMENT_POINT_SCALE);
        else
            snprintf(label, sizeof(label), localization::Tr("adventure.route.later"), days,
                     available * HERO_MOVEMENT_POINT_SCALE, cost * HERO_MOVEMENT_POINT_SCALE);
        auto* stone = gResourceManager->GetIcon("stonback.icn");
        stone->DrawToBuffer(m_x, m_y, 0, ICON_DRAW_NORMAL, ICON_DRAW_OFFSET_FULL);
        gResourceManager->Dispose(stone);
        m_font->DrawBoundedString(label, m_x + 4, m_y + 12, m_width - 8, m_height - 12, 1, FONT_ALIGN_CENTER);
    }
};

class SelectedHeroStats : public textWidget {
public:
    SelectedHeroStats() : textWidget(LOGICAL_SCREEN_WIDTH - 156, StatusY() + 36, 51, 13, strdup(""), "smalfont.fnt",
                                    TEXT_WIDGET_PLAIN_COLOR, BOTTOM_VIEW_TEXT_ID_2,
                                    WIDGET_KIND_TEXT) {
        m_flags &= ~WIDGET_FLAG_ENABLED;
    }

    void Draw() override {
        const i8 id = gCurPlayerData->CurrentHero();
        if (id == HERO_ID_NONE || gAllBlack)
            return;
        hero* subject = gGame->GetHero(id);
        char text[24];
        snprintf(text, sizeof(text), localization::Tr("hero.level.short"), subject->GetLevel(subject->m_experience));
        m_font->DrawBoundedString(text, m_x, m_y, m_width, m_font->m_height,
                                  m_color, FONT_ALIGN_CENTER);
    }
};
}

advManager::advManager(void) {
    i32 i;

    m_groundTiles = NULL;
    m_radarIcon = NULL;
    m_mapOriginX = 0;
    m_mapOriginY = 0;
    m_scrollOffsetX = 0;
    m_scrollOffsetY = 0;
    m_animationFrame = 0;
    m_flagFrameCounter = 0;
    m_pendingCommand = ADVMGR_COMMAND_NONE;
    m_cursorActive = false;
    m_identifyHeroActive = false;
    m_drawHeroShadows = true;
    m_adventureBorder = NULL;
    for (i = 0; i < ADVMGR_OBJECT_ICON_COUNT; i++)
        m_objectIcons[i] = NULL;
    for (i = 0; i < ADVMGR_HERO_ICON_COUNT; i++)
        m_heroIcons[i] = NULL;
    for (i = 0; i < ADVMGR_PLAYER_COLOR_COUNT; i++) {
        m_flagIcons[i] = NULL;
        m_boatFlagIcons[i] = NULL;
    }
    for (i = 0; i < ADVMGR_ENVIRONMENT_SOUND_COUNT; i++)
        m_loopingSamples[i] = NULL;
    for (i = 0; i < ADVMGR_CURSOR_SAMPLE_COUNT; i++)
        m_cursorSamples[i] = NULL;
    m_radarIcon = NULL;
    m_cloudOverlayIcon = NULL;
    m_shadowIcon = NULL;
    m_groundTiles = NULL;
    m_cloudTiles = NULL;
    m_stoneTiles = NULL;
    m_adventureWindow = NULL;
    m_routeMap = NULL;
    m_heroMobilized = false;
    m_unusedTownContextLocked = false;
    gShowIt = true;
    m_combatMonsterX = COMBAT_MONSTER_CELL_NONE;
    m_combatMonsterY = COMBAT_MONSTER_CELL_NONE;
    m_animationPhases[ANIMATION_PHASE_COLUMN_0] = ANIMATION_PHASE_COLUMN_0_INITIAL;
    m_animationPhases[ANIMATION_PHASE_COLUMN_1] = ANIMATION_PHASE_COLUMN_1_INITIAL;
    m_animationPhases[ANIMATION_PHASE_COLUMN_2] = ANIMATION_PHASE_COLUMN_2_INITIAL;
    m_animationPhases[ANIMATION_PHASE_COLUMN_3] = ANIMATION_PHASE_COLUMN_3_INITIAL;
    m_mapData = gGame->GetWorldMapData();
    gMapX = 0;
    gMapY = 0;
    m_cursorFrameCount = 0;
    m_cursorCycle = CURSOR_CYCLE_STOPPED;
    m_cursorTurning = 0;
    // Compared before it is first set (SetHeroContext); none yet, so the
    // first hero's terrain starts its music.
    m_currentTerrain = TERRAIN_INVALID;
}

advManager::~advManager() {}

i16 advManager::Open(i16 id) {
    b32 savedShowIt;
    b32 firstTime;
    i32 oldPlayerVal;
    i32 oldVolume;
    i32 i;

    platform::BeginDisplayUpdate();
    firstTime = true;
    m_previewCost = -1;
    ResetLocatorInput();
    gCurBottomView = BOTTOM_VIEW_NONE;
    m_heroesLogoShown = false;
    gShowIt = false;
    m_adventureBorder = NULL;
    for (i = 0; i < ADVMGR_BOTTOM_VIEW_WIDGET_COUNT; i++) {
        m_bottomViewIconWidgets[i] = NULL;
        m_bottomViewTextWidgets[i] = NULL;
    }
    if (m_adventureWindow == NULL) {
        m_adventureWindow = new heroWindow(0, 0, "adv_wind.bin");
        if (m_adventureWindow == NULL)
            MemError();
        m_adventureWindow->m_winWidth = LOGICAL_SCREEN_WIDTH;
        m_adventureWindow->m_winHeight = LOGICAL_SCREEN_HEIGHT;
        for (widget* item = m_adventureWindow->m_widgetListHead; item; item = item->m_next) {
            if (item->m_id == ADVENTURE_CONTROL_MAP_VIEW) {
                item->m_width = ADVENTURE_VIEWPORT_WIDTH;
                item->m_height = ADVENTURE_VIEWPORT_HEIGHT;
            } else if (item->m_x >= 480) {
                item->m_x += LOGICAL_SCREEN_WIDTH - 640;
                if (item->m_y >= 304 || item->m_id == ADVENTURE_CONTROL_HERO_PAGE_NEXT
                    || item->m_id == ADVENTURE_CONTROL_TOWN_PAGE_NEXT)
                    item->m_y += LocatorExtension();
                if (item->m_id == ADVENTURE_CONTROL_HERO_SCROLL || item->m_id == ADVENTURE_CONTROL_TOWN_SCROLL)
                    item->m_height += LocatorExtension();
            }
        }
        ExtendAdventureLocators(*m_adventureWindow);
        m_heroScrollKnob = new iconWidget(
            HERO_LOCATOR_KNOB_X,
            LOCATOR_KNOB_Y,
            LOCATOR_KNOB_WIDTH,
            LOCATOR_KNOB_HEIGHT,
            "scroll.icn",
            LOCATOR_KNOB_FRAME,
            ICON_DRAW_NORMAL,
            ADVENTURE_CONTROL_HERO_KNOB,
            ICON_WIDGET_DRAW,
            1
        );
        if (m_heroScrollKnob == NULL)
            MemError();
        m_adventureWindow->AddWidget(m_heroScrollKnob, WINDOW_Z_ORDER_APPEND);
        m_townScrollKnob = new iconWidget(
            TOWN_LOCATOR_KNOB_X,
            LOCATOR_KNOB_Y,
            LOCATOR_KNOB_WIDTH,
            LOCATOR_KNOB_HEIGHT,
            "scroll.icn",
            LOCATOR_KNOB_FRAME,
            ICON_DRAW_NORMAL,
            ADVENTURE_CONTROL_TOWN_KNOB,
            ICON_WIDGET_DRAW,
            1
        );
        if (m_townScrollKnob == NULL)
            MemError();
        m_adventureWindow->AddWidget(m_townScrollKnob, WINDOW_Z_ORDER_APPEND);
        m_panelBackdrops[0] = new backdropWidget(LOGICAL_SCREEN_WIDTH - 160, kLocatorTop, 56, 128 + LocatorExtension(), WIDGET_ID_NONE, 32);
        if (m_panelBackdrops[0] == NULL)
            MemError();
        m_panelBackdrops[1] = new backdropWidget(LOGICAL_SCREEN_WIDTH - 88, kLocatorTop, 56, 128 + LocatorExtension(), WIDGET_ID_NONE, 32);
        if (m_panelBackdrops[1] == NULL)
            MemError();
        m_panelBackdrops[2] = new backdropWidget(LOGICAL_SCREEN_WIDTH - 101, 194, 10, 92 + LocatorExtension(), WIDGET_ID_NONE, 32);
        if (m_panelBackdrops[2] == NULL)
            MemError();
        m_panelBackdrops[3] = new backdropWidget(LOGICAL_SCREEN_WIDTH - 29, 194, 10, 92 + LocatorExtension(), WIDGET_ID_NONE, 32);
        if (m_panelBackdrops[3] == NULL)
            MemError();
        m_panelBackdrops[4] = new backdropWidget(LOGICAL_SCREEN_WIDTH - 160, 320 + LocatorExtension(), 144, 144, WIDGET_ID_NONE, 32);
        if (m_panelBackdrops[4] == NULL)
            MemError();
        for (i = 0; i < ADVMGR_PANEL_ICON_COUNT; i++)
            m_adventureWindow->AddWidget(m_panelBackdrops[i], WINDOW_Z_ORDER_APPEND);
        m_adventureWindow->AddWidget(new AdventureIndicators(), WINDOW_Z_ORDER_APPEND);
        for (i16 slot = 0; slot < LocatorRows(); ++slot)
            m_adventureWindow->AddWidget(new HeroLocatorStats(slot), WINDOW_Z_ORDER_APPEND);
        m_movementPreview = new MovementPreview();
        m_adventureWindow->AddWidget(m_movementPreview, WINDOW_Z_ORDER_APPEND);
    }
    if (gThisNetHumanPlayer[gCurPlayer])
        gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
    else
        gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_WAIT);
    if (m_routeMap == NULL) {
        m_routeMap = new i8[MAP_CELL_GRID_SIZE * MAP_CELL_GRID_SIZE];
        if (m_routeMap == NULL)
            MemError();
    }
    m_routeShown = false;
    gWindowManager->AddWindow(m_adventureWindow, WINDOW_Z_ORDER_BOTTOM, true);
    if (m_groundTiles == NULL)
        m_groundTiles = gResourceManager->GetTileset("ground32.til");
    if (m_cloudTiles == NULL)
        m_cloudTiles = gResourceManager->GetTileset("clof32.til");
    if (m_stoneTiles == NULL)
        m_stoneTiles = gResourceManager->GetTileset("ston.til");
    if (m_cloudOverlayIcon == NULL)
        m_cloudOverlayIcon = gResourceManager->GetIcon("clop32.icn");
    if (m_objectIcons[TILESET_OBJ32_00] == NULL)
        m_objectIcons[TILESET_OBJ32_00] = gResourceManager->GetIcon("obj32-00.icn");
    if (m_objectIcons[TILESET_OBJ32_01] == NULL)
        m_objectIcons[TILESET_OBJ32_01] = gResourceManager->GetIcon("obj32-01.icn");
    if (m_objectIcons[TILESET_OBJ32_02] == NULL)
        m_objectIcons[TILESET_OBJ32_02] = gResourceManager->GetIcon("obj32-02.icn");
    if (m_objectIcons[TILESET_OBJ32_03] == NULL)
        m_objectIcons[TILESET_OBJ32_03] = gResourceManager->GetIcon("obj32-03.icn");
    if (m_objectIcons[TILESET_OBJ32_04] == NULL)
        m_objectIcons[TILESET_OBJ32_04] = gResourceManager->GetIcon("obj32-04.icn");
    if (m_objectIcons[TILESET_OBJ32_05] == NULL)
        m_objectIcons[TILESET_OBJ32_05] = gResourceManager->GetIcon("obj32-05.icn");
    if (m_objectIcons[TILESET_OBJ32_06] == NULL)
        m_objectIcons[TILESET_OBJ32_06] = gResourceManager->GetIcon("obj32-06.icn");
    if (m_objectIcons[TILESET_OBJ32_07] == NULL)
        m_objectIcons[TILESET_OBJ32_07] = gResourceManager->GetIcon("obj32-07.icn");
    if (m_objectIcons[TILESET_MTN32] == NULL)
        m_objectIcons[TILESET_MTN32] = gResourceManager->GetIcon("mtn32.icn");
    if (m_objectIcons[TILESET_TREE32] == NULL)
        m_objectIcons[TILESET_TREE32] = gResourceManager->GetIcon("tree32.icn");
    if (m_objectIcons[TILESET_TOWN32] == NULL)
        m_objectIcons[TILESET_TOWN32] = gResourceManager->GetIcon("town32.icn");
    if (m_objectIcons[TILESET_RSRC32] == NULL)
        m_objectIcons[TILESET_RSRC32] = gResourceManager->GetIcon("rsrc32.icn");
    if (m_objectIcons[TILESET_MONS32] == NULL)
        m_objectIcons[TILESET_MONS32] = gResourceManager->GetIcon("mons32.icn");
    if (m_objectIcons[TILESET_ART32] == NULL)
        m_objectIcons[TILESET_ART32] = gResourceManager->GetIcon("art32.icn");
    if (m_objectIcons[TILESET_FLAG32] == NULL)
        m_objectIcons[TILESET_FLAG32] = gResourceManager->GetIcon("flag32.icn");
    if (m_objectIcons[TILESET_RESSMALL] == NULL)
        m_objectIcons[TILESET_RESSMALL] = gResourceManager->GetIcon("ressmall.icn");
    if (m_objectIcons[TILESET_HOURGLAS] == NULL)
        m_objectIcons[TILESET_HOURGLAS] = gResourceManager->GetIcon("hourglas.icn");
    if (m_objectIcons[TILESET_ROUTE] == NULL)
        m_objectIcons[TILESET_ROUTE] = gResourceManager->GetIcon("route.icn");
    if (m_objectIcons[TILESET_SMCREST] == NULL)
        m_objectIcons[TILESET_SMCREST] = gResourceManager->GetIcon("smcrest.icn");
    if (m_objectIcons[TILESET_STONBACK] == NULL)
        m_objectIcons[TILESET_STONBACK] = gResourceManager->GetIcon("stonback.icn");
    if (m_objectIcons[TILESET_MINIMON] == NULL)
        m_objectIcons[TILESET_MINIMON] = gResourceManager->GetIcon("minimon.icn");
    if (m_heroIcons[ADVMGR_HERO_ICON_KNIGHT] == NULL)
        m_heroIcons[ADVMGR_HERO_ICON_KNIGHT] = gResourceManager->GetIcon("kngt32.icn");
    if (m_heroIcons[ADVMGR_HERO_ICON_BARBARIAN] == NULL)
        m_heroIcons[ADVMGR_HERO_ICON_BARBARIAN] = gResourceManager->GetIcon("barb32.icn");
    if (m_heroIcons[ADVMGR_HERO_ICON_SORCERESS] == NULL)
        m_heroIcons[ADVMGR_HERO_ICON_SORCERESS] = gResourceManager->GetIcon("sorc32.icn");
    if (m_heroIcons[ADVMGR_HERO_ICON_WARLOCK] == NULL)
        m_heroIcons[ADVMGR_HERO_ICON_WARLOCK] = gResourceManager->GetIcon("wrlk32.icn");
    if (m_heroIcons[ADVMGR_HERO_ICON_BOAT] == NULL)
        m_heroIcons[ADVMGR_HERO_ICON_BOAT] = gResourceManager->GetIcon("boat32.icn");
    gLoadingMonoIcon = true;
    if (m_shadowIcon == NULL)
        m_shadowIcon = gResourceManager->GetIcon("shadow32.icn");
    gLoadingMonoIcon = false;
    if (m_flagIcons[PLAYER_COLOR_BLUE] == NULL)
        m_flagIcons[PLAYER_COLOR_BLUE] = gResourceManager->GetIcon("b-flag32.icn");
    if (m_flagIcons[PLAYER_COLOR_GREEN] == NULL)
        m_flagIcons[PLAYER_COLOR_GREEN] = gResourceManager->GetIcon("g-flag32.icn");
    if (m_flagIcons[PLAYER_COLOR_RED] == NULL)
        m_flagIcons[PLAYER_COLOR_RED] = gResourceManager->GetIcon("r-flag32.icn");
    if (m_flagIcons[PLAYER_COLOR_YELLOW] == NULL)
        m_flagIcons[PLAYER_COLOR_YELLOW] = gResourceManager->GetIcon("y-flag32.icn");
    if (m_boatFlagIcons[PLAYER_COLOR_BLUE] == NULL)
        m_boatFlagIcons[PLAYER_COLOR_BLUE] = gResourceManager->GetIcon("b-bflg32.icn");
    if (m_boatFlagIcons[PLAYER_COLOR_GREEN] == NULL)
        m_boatFlagIcons[PLAYER_COLOR_GREEN] = gResourceManager->GetIcon("g-bflg32.icn");
    if (m_boatFlagIcons[PLAYER_COLOR_RED] == NULL)
        m_boatFlagIcons[PLAYER_COLOR_RED] = gResourceManager->GetIcon("r-bflg32.icn");
    if (m_boatFlagIcons[PLAYER_COLOR_YELLOW] == NULL)
        m_boatFlagIcons[PLAYER_COLOR_YELLOW] = gResourceManager->GetIcon("y-bflg32.icn");
    gLoadingMonoIcon = true;
    if (m_radarIcon == NULL)
        m_radarIcon = gResourceManager->GetIcon("radar.icn");
    gLoadingMonoIcon = false;
    for (i = 0; i < ADVMGR_ENVIRONMENT_SOUND_COUNT; i++)
        m_loopingSamples[i] = NULL;
    for (i = 0; i < ADVMGR_ACTIVE_SOUND_COUNT; i++) {
        m_activeSounds[i].soundId = MAP_SOUND_NONE;
        m_activeSounds[i].distance = ENVIRONMENT_SOUND_FAR_DISTANCE;
        m_activeSoundMask = 0;
    }
    GetCursorSampleSet(gConfig.walkSpeed);
    if (!gThisNetHumanPlayer[gCurPlayer]) {
        gGame->TurnOnAIMusic();
        SetNoDialogMenus(0);
    } else {
        SetNoDialogMenus(1);
    }
    gTimers[ADVENTURE_FRAME_TIMER_SLOT] = KBTickCount() + TIMER_DELAY;
    m_messageTypeMask = MESSAGE_KEY_DOWN | MESSAGE_KEY_UP | MESSAGE_MOUSE_MOVE
                        | MESSAGE_LEFT_BUTTON_DOWN | MESSAGE_RIGHT_BUTTON_DOWN | 0x100
                        | MESSAGE_WIDGET | MESSAGE_MOUSE_WHEEL;
    gMouseManager->NewUpdate(true);
    oldVolume = gConfig.soundVolume;
    if (gConfig.soundVolume != SOUND_VOLUME_OFF)
        gConfig.soundVolume = SOUND_VOLUME_LAST;
    SetInitialMapOrigin();
    gShowIt = gThisNetHumanPlayer[gCurPlayer];
    gMouseManager->SetColorMice(false);
    oldPlayerVal = gCurPlayer;
    savedShowIt = gShowIt;
    gCurPlayer = gCurWatchPlayer;
    gCurPlayerData = &gGame->m_players[gCurPlayer];
    gShowIt = true;
    RedrawAdvScreen(true);
    gCurPlayer = oldPlayerVal;
    gShowIt = savedShowIt;
    gCurPlayerData = &gGame->m_players[gCurPlayer];
    if (!gThisNetHumanPlayer[gCurPlayer])
        gGame->ShowComputerScreen();
    gMouseManager->ReallyShowPointer();
    KBChangeMenu(gAdventureMenu);
    gWindowManager->FadeScreen(WINDOW_FADE_IN, WINDOW_FADE_SHORT, gPalette);
    gBottomViewOverride = BOTTOM_VIEW_NONE;
    gConfig.soundVolume = oldVolume;
    SetVolumes(gConfig.soundVolume, gConfig.musicVolume);
    m_messageMask = BASE_MANAGER_ACCEPT_ADVENTURE;
    m_priority = id;
    m_active = 1;
    strcpy(m_name, "advManager");
    platform::EndDisplayUpdate();
    return BASE_MANAGER_SUCCESS;
}

void advManager::Close(void) {
    i16 index;

    delete m_cameraFrame;
    m_cameraFrame = nullptr;
    m_mapDragging = false;
    m_dragX = m_dragY = -1;
    ClearBottomView();
    gMouseManager->SetPointer(MOUSE_INVALID_CURSOR_FRAME);
    StopAllAudio();
    if (m_adventureBorder) {
        free(m_adventureBorder);
        m_adventureBorder = NULL;
    }
    if (gAdvDisposeLevel <= ADV_DISPOSE_OBJECT_ICONS_LAST) {
        for (index = 0; index < ADVMGR_OBJECT_ICON_COUNT; index++) {
            if (m_objectIcons[index])
                gResourceManager->Dispose(m_objectIcons[index]);
            m_objectIcons[index] = NULL;
        }
    }
    if (gAdvDisposeLevel <= ADV_DISPOSE_NONE) {
        gResourceManager->Dispose(m_radarIcon);
        m_radarIcon = NULL;
        gResourceManager->Dispose(m_cloudOverlayIcon);
        m_cloudOverlayIcon = NULL;
        for (index = 0; index < ADVMGR_HERO_ICON_COUNT; index++) {
            gResourceManager->Dispose(m_heroIcons[index]);
            m_heroIcons[index] = NULL;
        }
        gResourceManager->Dispose(m_shadowIcon);
        m_shadowIcon = NULL;
        for (index = 0; index < ADVMGR_PLAYER_COLOR_COUNT; index++) {
            gResourceManager->Dispose(m_flagIcons[index]);
            m_flagIcons[index] = NULL;
            gResourceManager->Dispose(m_boatFlagIcons[index]);
            m_boatFlagIcons[index] = NULL;
        }
        gResourceManager->Dispose(m_groundTiles);
        m_groundTiles = NULL;
        gResourceManager->Dispose(m_cloudTiles);
        m_cloudTiles = NULL;
        gResourceManager->Dispose(m_stoneTiles);
        m_stoneTiles = NULL;
    }
    for (index = 0; index < ADVMGR_ENVIRONMENT_SOUND_COUNT; index++) {
        if (m_loopingSamples[index])
            gResourceManager->Dispose(m_loopingSamples[index]);
        m_loopingSamples[index] = NULL;
    }
    for (index = 0; index < ADVMGR_CURSOR_SAMPLE_COUNT; index++) {
        if (m_cursorSamples[index])
            gResourceManager->Dispose(m_cursorSamples[index]);
        m_cursorSamples[index] = NULL;
    }
    gWindowManager->RemoveWindow(m_adventureWindow);
    delete m_adventureWindow;
    m_adventureWindow = NULL;
    if (m_routeMap)
        delete[] m_routeMap;
    m_routeMap = NULL;
    gCurBottomView = BOTTOM_VIEW_NONE;
    m_active = 0;
}

void advManager::GetCursorSampleSet(i32 sampleSet) {
    if (sampleSet >= 1)
        sampleSet = CURSOR_SAMPLE_FAST_SET;
    i8 suffixSample[ADVMGR_CURSOR_SAMPLE_COUNT] =
        {0, 3, 5, 3, 4, 5, 6};
    for (i32 index = TERRAIN_WATER; index < TERRAIN_COUNT; ++index) {
        sprintf(gText, "wsnd%1d%1d.82M", sampleSet, suffixSample[index]);
        m_cursorSamples[index] = gResourceManager->GetSample(gText);
        m_cursorSamples[index]->m_playbackData.volume = SAMPLE_VOLUME_FULL;
    }
}

class mapCell* advManager::DoAdvCommand(void) {
    i16 pathIndex;
    b8 adjacentMonster;
    b32 outOfMobility;
    b8 hover;
    tag_message evt;
    mapCell* cellPtr;
    b32 routeWasShown;
    hero* selectedHero;
    town* viewTown;
    b8 userStopRequested;

    cellPtr = NULL;
    // The commands that do not move a hero (town view, ...) also run with no
    // current hero (-1); the original took the address of the hero before
    // the table for them, unused.
    selectedHero = gCurPlayerData->m_currentHero != HERO_ID_NONE
                       ? gGame->GetHero(gCurPlayerData->m_currentHero)
                       : NULL;
    // A move or occupied-town command set while a hero was selected can
    // outlive the selection (the hover that set it is not repeated); the
    // original then moved hero -1. It is dropped.
    if (selectedHero == NULL
        && (m_pendingCommand == ADVMGR_COMMAND_MOVE_TO
            || m_pendingCommand == ADVMGR_COMMAND_CONTINUE_ROUTE
            || m_pendingCommand == ADVMGR_COMMAND_HERO_VIEW
            || m_pendingCommand == ADVMGR_COMMAND_OCCUPIED_TOWN_VIEW))
        m_pendingCommand = ADVMGR_COMMAND_NONE;
    if (gCurPlayerData->m_currentTown == GAME_TOWN_NONE && m_pendingCommand == ADVMGR_COMMAND_TOWN_VIEW)
        m_pendingCommand = ADVMGR_COMMAND_NONE;
    userStopRequested = false;
    hover = false;
    switch (m_pendingCommand) {
        case ADVMGR_COMMAND_MOVE_TO:
            selectedHero->m_destinationX = m_commandTargetX;
            selectedHero->m_destinationY = m_commandTargetY;
            goto continue_route;
        case ADVMGR_COMMAND_CONTINUE_ROUTE:
        continue_route:
            gSearchArray->BuildPath(
                selectedHero->m_x,
                selectedHero->m_y,
                selectedHero->m_destinationX,
                selectedHero->m_destinationY,
                SEARCH_UNLIMITED_COST
            );
            if (gSearchArray->m_pathLength > 0) {
                routeWasShown = m_routeShown;
                MobilizeCurrHero(true);
                if (gConfig.showRoute || routeWasShown)
                    ShowRoute(false, 0, false);
                else if (m_routeShown && m_pendingCommand != ADVMGR_COMMAND_CONTINUE_ROUTE)
                    HideRoute(true, false, true);
                gMouseManager->ReallyHidePointer();
                gInputManager->Flush();
                for (pathIndex = gSearchArray->m_pathLength - 1; pathIndex >= 0; pathIndex--) {
                    cellPtr = MoveHero(
                        gSearchArray->m_directions[pathIndex],
                        pathIndex == 0,
                        &gTriggerX,
                        &gTriggerY,
                        &outOfMobility,
                        false,
                        &adjacentMonster
                    );
                    UpdateHeroLocator(LOCATOR_SLOT_CURRENT_HERO, true, true);
                    if (cellPtr)
                        break;
                    if (outOfMobility || adjacentMonster)
                        goto movement_done;
                    evt = gInputManager->GetEvent();
                    while (evt.type) {
                        if (evt.type == MESSAGE_KEY_DOWN || evt.type == MESSAGE_LEFT_BUTTON_DOWN
                            || evt.type == MESSAGE_RIGHT_BUTTON_DOWN
                            || evt.type == MESSAGE_WIDGET) {
                            userStopRequested = true;
                            StopCursor(true);
                            goto movement_done;
                        }
                        Process1WindowsMessage();
                        evt = gInputManager->GetEvent();
                    }
                }
            movement_done:
                if ((pathIndex <= 0 && selectedHero->m_x == selectedHero->m_destinationX
                     && selectedHero->m_y == selectedHero->m_destinationY)
                    || (userStopRequested && !gConfig.showRoute) || cellPtr)
                    HideRoute(true, true, true);
                else if (m_pendingCommand == ADVMGR_COMMAND_CONTINUE_ROUTE || gConfig.showRoute)
                    ShowRoute(false, 1, true);
                gMouseManager->ReallyShowPointer();
                UpdBottomView(true, true, true);
                if (cellPtr) {
                    StopCursor(true);
                    DoEvent(cellPtr, gTriggerX, gTriggerY);
                    cellPtr = NULL;
                }
                Reseed(0, 0);
                hover = true;
                CheckDimHero();
            }
            break;
        case ADVMGR_COMMAND_OCCUPIED_TOWN_VIEW:
            DemobilizeCurrHero();
            gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
            viewTown = gGame->GetTown(selectedHero->m_locationMetadata);
            viewTown->View();
            cellPtr = NULL;
            break;
        case ADVMGR_COMMAND_TOWN_VIEW:
            DemobilizeCurrHero();
            gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
            cellPtr = GetCell(
                gGame->GetTown(gCurPlayerData->m_currentTown)->m_x,
                gGame->GetTown(gCurPlayerData->m_currentTown)->m_y
            );
            gGame->GetTown(gCurPlayerData->m_currentTown)->View();
            cellPtr = NULL;
            break;
        case ADVMGR_COMMAND_HERO_VIEW:
            gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
            gGame->GetHero(gCurPlayerData->m_currentHero)->HeroView(false);
            RedrawAdvScreen(true);
            gWindowManager->FadeScreen(WINDOW_FADE_IN, WINDOW_FADE_SHORT, NULL);
            break;
        case ADVMGR_COMMAND_SELECT_HERO:
            SetHeroContext(
                GetCell(m_mapOriginX + m_hoverCellX, m_mapOriginY + m_hoverCellY)->m_objectMetadata,
                false
            );
            break;
        case ADVMGR_COMMAND_SELECT_TOWN:
            SetTownContext(
                GetCell(m_mapOriginX + m_hoverCellX, m_mapOriginY + m_hoverCellY)->m_objectMetadata
            );
            break;
        case ADVMGR_COMMAND_NONE:
            break;
    }
    m_pendingCommand = ADVMGR_COMMAND_NONE;
    m_hoverCellX = m_hoverCellY = CURSOR_INVALID_POSITION;
    if (hover)
        ForceNewHover();
    return cellPtr;
}

i32 gLastScrollTime = 0;
i32 gSandAnim = 0;
i32 gLastHourGlassUpdateTime = 0;
i32 gTriggerX = 0;
i32 gTriggerY = 0;
i32 gCurBottomView = BOTTOM_VIEW_NONE;
i32 gCurHourGlassPhase = 0;
b32 gForceUpdate = false;
i32 gUnusedAdvCount = 0;

i16 advManager::Main(struct tag_message& message) {
    static i32 gCheatSeq = 0;
    i32 yPos;
    i32 xPos;
    i32 result;
    mapCell* location;
    hero* curHero;
    i32 townIndex;
    i32 amount;
    i32 quit;
    b32 outOfMobility;
    b8 adjacentMonster;
    i32 newHelpText;
    i32 orient;

    if (m_mapDragging)
        return MESSAGE_DISPATCH_CONSUME;
    if (gTimers[ADVENTURE_FRAME_TIMER_SLOT] < KBTickCount() && ComboDraw(true))
        UpdateScreen(true, false);
    if (gGameOver) {
        message.type = MESSAGE_EXECUTIVE;
        message.executiveCommand = EXECUTIVE_COMMAND_TERMINATE_LOOP;
        return MESSAGE_DISPATCH_FORWARD;
    }
    if (!gHumanPlayer[gCurPlayer] && (!gRemoteOn || gThisGamePos == gHostGamePos)) {
        gPhilAI->DoAI(gCurPlayer);
        gGame->NextPlayer();
        return MESSAGE_DISPATCH_CONSUME;
    }
    CheckHandleNet();
    if (!gThisNetHumanPlayer[gCurPlayer])
        return CheckHandleNetPlayerWait(message, false);
    if (m_dragX < 0 && gScreenScroll && gForegroundApp)
        CheckScreenScroll();
    UpdateMovementPreview();
    if (!(message.type & m_messageTypeMask)) {
        if (message.type) {
            message.type = MESSAGE_NONE;
            return MESSAGE_DISPATCH_FORWARD;
        }
        return MESSAGE_DISPATCH_CONTINUE;
    }
    if (!gNoSound && gConfig.musicVolume && gForceSwitchMusic > 0
        && KBTickCount() - gForceSwitchMusic > FORCED_MUSIC_DELAY
        && GetCurrentTrack() == MUSIC_TRACK_NETWORK_TURN) {
        gForceSwitchMusic = FORCED_MUSIC_IDLE;
        PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
    }
    result = MESSAGE_DISPATCH_CONSUME;
    quit = 0;
    location = NULL;
    if (message.type) {
        switch (message.type) {
            case MESSAGE_MOUSE_WHEEL:
                ScrollLocators(message);
                break;
            case MESSAGE_WIDGET:
                switch (message.command) {
                    case WIDGET_COMMAND_HOVER:
                        result = ProcessHover(&message);
                        break;
                    case WIDGET_NOTIFY_DESELECT:
                        if (!(message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON))
                            result = ProcessDeSelect(message.id, &quit, &location);
                        break;
                    case WIDGET_NOTIFY_SELECT:
                        result = ProcessSelect(&message, &location);
                        break;
                    case WIDGET_NOTIFY_RIGHT_CLICK:
                        newHelpText = ADVENTURE_HELP_NONE;
                        switch (message.id) {
                            case ADVENTURE_CONTROL_NEXT_HERO:
                                newHelpText = ADVENTURE_HELP_NEXT_HERO;
                                break;
                            case ADVENTURE_CONTROL_CONTINUE_ROUTE:
                                newHelpText = ADVENTURE_HELP_CONTINUE_ROUTE;
                                break;
                            case ADVENTURE_CONTROL_OVERVIEW:
                                newHelpText = ADVENTURE_HELP_OVERVIEW;
                                break;
                            case ADVENTURE_CONTROL_END_TURN:
                                newHelpText = ADVENTURE_HELP_END_TURN;
                                break;
                            case ADVENTURE_CONTROL_ADVENTURE_OPTIONS:
                                newHelpText = ADVENTURE_HELP_ADVENTURE_OPTIONS;
                                break;
                            case ADVENTURE_CONTROL_GAME_OPTIONS:
                                newHelpText = ADVENTURE_HELP_GAME_OPTIONS;
                                break;
                        }
                        if (newHelpText >= ADVENTURE_HELP_FIRST)
                            NormalDialog(gAdvMenuHelp[newHelpText], NORMAL_DIALOG_TYPE_QUICK_VIEW);
                        break;
                }
                break;
            case MESSAGE_KEY_DOWN:
                orient = MAP_DIRECTION_NONE;
                if (gCurPlayerData->CurrentHero() != HERO_ID_NONE)
                    curHero = gGame->GetHero(gCurPlayerData->m_currentHero);
                else
                    curHero = NULL;
                if (gDebugLevel < ADVENTURE_DEBUG_KEYS_LEVEL_MIN
                    && (message.keyCode == INPUT_SCAN_F3 || message.keyCode == INPUT_SCAN_F4
                        || message.keyCode == INPUT_SCAN_F5 || message.keyCode == INPUT_SCAN_F6
                        || message.keyCode == INPUT_SCAN_F7 || message.keyCode == INPUT_SCAN_F8
                        || message.keyCode == INPUT_SCAN_F9 || message.keyCode == INPUT_SCAN_F10
                        || message.keyCode == INPUT_SCAN_F11 || message.keyCode == INPUT_SCAN_F12))
                    break;
                switch (TranslateShortcut(message.keyCode, SHORTCUT_MAP)) {
                    case INPUT_SCAN_F2:
                        if (gRemoteOn)
                            PopNetBox(NULL);
                        break;
                    case INPUT_SCAN_F3:
                        gGame->m_playerDead[1] = true;
                        gGame->m_playerDead[2] = true;
                        gGame->m_playerDead[3] = true;
                        CheckEndGame(true);
                        break;
                    case INPUT_SCAN_F5:
                        gGame->m_playerDead[0] = true;
                        CheckEndGame(false);
                        break;
                    case INPUT_SCAN_F6:
                        if (curHero) {
                            for (amount = 0; amount < HERO_SPELL_SLOT_COUNT; amount++)
                                curHero->AddSpell(
                                    amount,
                                    CHEAT_SPELL_CHARGES,
                                    false
                                );
                        }
                        break;
                    case INPUT_SCAN_F7:
                        if (curHero)
                            GiveExperience(curHero, CHEAT_EXPERIENCE_AMOUNT, true);
                        break;
                    case INPUT_SCAN_F8:
                        if (curHero) {
                            gGame->GiveArmy(
                                &curHero->m_army,
                                CREATURE_DRAGON,
                                1,
                                ARMY_GROUP_EMPTY_SLOT
                            );
                            gGame->GiveArmy(
                                &curHero->m_army,
                                CREATURE_TROLL,
                                1,
                                ARMY_GROUP_EMPTY_SLOT
                            );
                        }
                        break;
                    case INPUT_SCAN_F9:
                        for (amount = 0; amount < RESOURCE_COUNT;
                             amount++) {
                            gCurPlayerData->m_resources[amount] +=
                                (amount == RESOURCE_GOLD
                                     ? CHEAT_GOLD_AMOUNT
                                     : CHEAT_RESOURCE_AMOUNT);
                        }
                        break;
                    case INPUT_SCAN_F11:
                        if (curHero)
                            curHero->m_remainingMobility = CHEAT_MOBILITY;
                        break;
                    case INPUT_SCAN_F12:
                        gGame->SetVisibility(
                            CHEAT_REVEAL_CENTER,
                            CHEAT_REVEAL_CENTER,
                            gCurPlayer,
                            CHEAT_REVEAL_RADIUS
                        );
                        UpdateRadar(true, false);
                        CompleteDraw(false);
                        UpdateScreen(false, false);
                        break;
                    case INPUT_SCAN_0:
                        amount = 0;
                        goto processCheatDigit;
                    case INPUT_SCAN_1:
                        amount = 1;
                        goto processCheatDigit;
                    case INPUT_SCAN_2:
                        amount = 2;
                        goto processCheatDigit;
                    case INPUT_SCAN_3:
                        amount = 3;
                        goto processCheatDigit;
                    case INPUT_SCAN_4:
                        amount = 4;
                        goto processCheatDigit;
                    case INPUT_SCAN_5:
                        amount = 5;
                        goto processCheatDigit;
                    case INPUT_SCAN_6:
                        amount = 6;
                        goto processCheatDigit;
                    case INPUT_SCAN_7:
                        amount = 7;
                        goto processCheatDigit;
                    case INPUT_SCAN_8:
                        amount = 8;
                        goto processCheatDigit;
                    case INPUT_SCAN_9:
                        amount = 9;
                        goto processCheatDigit;
                    processCheatDigit:
                        gCheatSeq =
                            gCheatSeq * CHEAT_SEQUENCE_RADIX % CHEAT_SEQUENCE_MODULUS + amount;
                        if (gCheatSeq == CHEAT_REVEAL_MAP) {
                            gGame->SetVisibility(
                                CHEAT_REVEAL_CENTER,
                                CHEAT_REVEAL_CENTER,
                                0,
                                CHEAT_REVEAL_RADIUS
                            );
                            gGame->SetVisibility(
                                CHEAT_REVEAL_CENTER,
                                CHEAT_REVEAL_CENTER,
                                1,
                                CHEAT_REVEAL_RADIUS
                            );
                            gGame->SetVisibility(
                                CHEAT_REVEAL_CENTER,
                                CHEAT_REVEAL_CENTER,
                                2,
                                CHEAT_REVEAL_RADIUS
                            );
                            gGame->SetVisibility(
                                CHEAT_REVEAL_CENTER,
                                CHEAT_REVEAL_CENTER,
                                3,
                                CHEAT_REVEAL_RADIUS
                            );
                            Reseed(0, 0);
                            UpdateRadar(true, false);
                            CompleteDraw(false);
                            UpdateScreen(false, false);
                        }
                        break;
                    case INPUT_SCAN_ESCAPE:
                        break;
                    case INPUT_SCAN_M:
                        result = ProcessDeSelect(ADVENTURE_CONTROL_CONTINUE_ROUTE, &quit, &location);
                        break;
                    case INPUT_SCAN_E:
                        result = ProcessDeSelect(ADVENTURE_CONTROL_END_TURN, &quit, &location);
                        break;
                    case INPUT_SCAN_A:
                        result = ProcessDeSelect(ADVENTURE_CONTROL_ADVENTURE_OPTIONS, &quit, &location);
                        break;
                    case INPUT_SCAN_F:
                    case INPUT_SCAN_O:
                        result = ProcessDeSelect(ADVENTURE_CONTROL_GAME_OPTIONS, &quit, &location);
                        break;
                    case INPUT_SCAN_K:
                        result = ProcessDeSelect(ADVENTURE_CONTROL_OVERVIEW, &quit, &location);
                        break;
                    case INPUT_SCAN_NUMPAD_8:
                        if (message.modifiers & (MESSAGE_MODIFIER_CONTROL_KEYS | MESSAGE_MODIFIER_ARROW))
                            ScreenScroll(MAP_DIRECTION_NORTH, false);
                        else
                            orient = MAP_DIRECTION_NORTH;
                        break;
                    case INPUT_SCAN_NUMPAD_9:
                        if (message.modifiers & MESSAGE_MODIFIER_CONTROL_KEYS)
                            ScreenScroll(MAP_DIRECTION_NORTH_EAST, false);
                        else
                            orient = MAP_DIRECTION_NORTH_EAST;
                        break;
                    case INPUT_SCAN_NUMPAD_6:
                        if (message.modifiers & (MESSAGE_MODIFIER_CONTROL_KEYS | MESSAGE_MODIFIER_ARROW))
                            ScreenScroll(MAP_DIRECTION_EAST, false);
                        else
                            orient = MAP_DIRECTION_EAST;
                        break;
                    case INPUT_SCAN_NUMPAD_3:
                        if (message.modifiers & MESSAGE_MODIFIER_CONTROL_KEYS)
                            ScreenScroll(MAP_DIRECTION_SOUTH_EAST, false);
                        else
                            orient = MAP_DIRECTION_SOUTH_EAST;
                        break;
                    case INPUT_SCAN_NUMPAD_2:
                        if (message.modifiers & (MESSAGE_MODIFIER_CONTROL_KEYS | MESSAGE_MODIFIER_ARROW))
                            ScreenScroll(MAP_DIRECTION_SOUTH, false);
                        else
                            orient = MAP_DIRECTION_SOUTH;
                        break;
                    case INPUT_SCAN_NUMPAD_1:
                        if (message.modifiers & MESSAGE_MODIFIER_CONTROL_KEYS)
                            ScreenScroll(MAP_DIRECTION_SOUTH_WEST, false);
                        else
                            orient = MAP_DIRECTION_SOUTH_WEST;
                        break;
                    case INPUT_SCAN_NUMPAD_4:
                        if (message.modifiers & (MESSAGE_MODIFIER_CONTROL_KEYS | MESSAGE_MODIFIER_ARROW))
                            ScreenScroll(MAP_DIRECTION_WEST, false);
                        else
                            orient = MAP_DIRECTION_WEST;
                        break;
                    case INPUT_SCAN_NUMPAD_7:
                        if (message.modifiers & MESSAGE_MODIFIER_CONTROL_KEYS)
                            ScreenScroll(MAP_DIRECTION_NORTH_WEST, false);
                        else
                            orient = MAP_DIRECTION_NORTH_WEST;
                        break;
                    case INPUT_SCAN_C:
                        CheckCastSpell();
                        break;
                    case INPUT_SCAN_D:
                        ProcessSearch(ADVMGR_SEARCH_VIEW_CENTER, ADVMGR_SEARCH_VIEW_CENTER);
                        break;
                    case INPUT_SCAN_P:
                        ViewPuzzle();
                        break;
                    case INPUT_SCAN_V:
                        ViewWorld(SPELL_VIEW_ALL, false, false);
                        break;
                    case INPUT_SCAN_N:
                        amount = MAIN_MENU_NEW_GAME;
                        strcpy(gText, localization::Tr("adventure.confirm_restart"));
                        goto confirmGameCommand;
                    case INPUT_SCAN_L:
                        amount = MAIN_MENU_LOAD_GAME;
                        strcpy(gText, localization::Tr("adventure.confirm_load"));
                        goto confirmGameCommand;
                    case INPUT_SCAN_Q:
                        amount = MAIN_MENU_QUIT;
                        strcpy(gText, localization::Tr("adventure.confirm_quit"));
                        goto confirmGameCommand;
                    confirmGameCommand:
                        quit = 1;
                        NormalDialog(gText, NORMAL_DIALOG_TYPE_YES_NO);
                        if (gWindowManager->m_dialogResult == NORMAL_DIALOG_CANCEL)
                            quit = 0;
                        else
                            gGameCommand = amount;
                        break;
                    case INPUT_SCAN_S:
                        SaveGame();
                        break;
                    case INPUT_SCAN_I:
                        if (gGame->m_campaignType > 0)
                            gGame->ShowCampaignInfo(gGame->m_campaignScenario, true, 0);
                        else
                            gGame->ShowScenInfo();
                        break;
                    case INPUT_SCAN_T:
                        // The original tested >= 0 and, for a player without
                        // towns, selected the stale or empty (-1) first entry.
                        if (gCurPlayerData->m_townCount > 0) {
                            if (gCurPlayerData->CurrentTown() == GAME_TOWN_NONE) {
                                townIndex = gCurPlayerData->m_townIds[0];
                            } else {
                                townIndex = 0;
                                for (amount = 0; amount < gCurPlayerData->m_townCount; amount++) {
                                    if (gCurPlayerData->CurrentTown()
                                        == gCurPlayerData->m_townIds[amount]) {
                                        if (amount == gCurPlayerData->m_townCount - 1)
                                            townIndex = gCurPlayerData->m_townIds[0];
                                        else
                                            townIndex = gCurPlayerData->m_townIds[amount + 1];
                                    }
                                }
                            }
                            SetTownContext(townIndex);
                        }
                        break;
                    case INPUT_SCAN_H:
                        SetHeroContext(gCurPlayerData->NextHero(0), false);
                        break;
                    case INPUT_SCAN_ENTER:
                        if (gCurPlayerData->CurrentTown() != GAME_TOWN_NONE) {
                            m_pendingCommand = ADVMGR_COMMAND_TOWN_VIEW;
                            DoAdvCommand();
                        } else if (gCurPlayerData->CurrentHero() != HERO_ID_NONE) {
                            m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                            DoAdvCommand();
                        }
                        break;
                }
                if (gCurPlayerData->m_currentHero != HERO_ID_NONE
                    && orient >= MAP_DIRECTION_FIRST) {
                    HideRoute(true, true, true);
                    gMouseManager->ReallyHidePointer();
                    location = MoveHero(
                        orient,
                        true,
                        &gTriggerX,
                        &gTriggerY,
                        &outOfMobility,
                        false,
                        &adjacentMonster
                    );
                    UpdateHeroLocator(LOCATOR_SLOT_CURRENT_HERO, true, true);
                    if (location) {
                        StopCursor(true);
                        DoEvent(location, gTriggerX, gTriggerY);
                        location = NULL;
                    }
                    Reseed(0, 0);
                    ForceNewHover();
                    UpdBottomView(true, true, true);
                    CheckDimHero();
                    gMouseManager->ReallyShowPointer();
                }
                break;
        }
    }
    if (location)
        DoEvent(location, gTriggerX, gTriggerY);
    if (gGameOver || quit == 1 || gMenuCommand != APP_MENU_NONE) {
        message.type = MESSAGE_EXECUTIVE;
        message.executiveCommand = EXECUTIVE_COMMAND_TERMINATE_LOOP;
        return MESSAGE_DISPATCH_FORWARD;
    }
    return result;
}

void advManager::Reseed(i32, i32) {
    gSeedingValid = false;
}

i32 advManager::ProcessSelect(struct tag_message* message, class mapCell** eventCell) {
    i32 locatorIndex;
    i32 listIndex;
    i16 mouseX;
    i16 pickedObject;
    i16 mouseY;
    i16 objectIdIndex;
    b32 visible;
    mapCell* theCell;
    b8 mobileResult;
    hero* currentHero;
    if (message->modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
        m_lastLocatorControl = 0;
        m_lastLocatorObject = -1;
    }

    const int heroSlot = HeroLocatorSlot(message->id);
    const int townSlot = TownLocatorSlot(message->id);
    const bool isLocator = heroSlot >= 0 || townSlot >= 0;
    if (!isLocator && message->id != ADVENTURE_CONTROL_RADAR)
        m_lastLocatorControl = 0;
    m_locatorWheels[0] = {};
    m_locatorWheels[1] = {};
    visible = true;
    const int control = heroSlot >= 0 ? ADVENTURE_CONTROL_HERO_LOCATOR_1
                      : townSlot >= 0 ? ADVENTURE_CONTROL_TOWN_LOCATOR_1 : message->id;
    switch (control) {
        case ADVENTURE_CONTROL_HERO_LOCATOR_1: {
            locatorIndex = heroSlot;
            listIndex = gCurPlayerData->m_heroLocatorPage + locatorIndex;
            if (listIndex < 0 || listIndex >= gCurPlayerData->m_heroCount)
                break;
            pickedObject = gCurPlayerData->m_heroIds[listIndex];
            const bool openHero = LocatorDoubleClick(message->id, pickedObject, *message);
            if (message->modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
                HeroQuickView(
                    pickedObject,
                    locatorIndex,
                    QUICK_VIEW_AT_LOCATOR,
                    QUICK_VIEW_AT_LOCATOR
                );
            } else if (message->modifiers & MESSAGE_MODIFIER_CONTROL_KEYS) {
                if (!(message->modifiers & MESSAGE_MODIFIER_DOUBLE_CLICK))
                    ToggleHeroWaiting(pickedObject);
            } else if (ReorderLocator(gCurPlayerData->m_heroIds, gCurPlayerData->m_heroLocatorPage,
                                      listIndex, message->modifiers)) {
                UpdateHeroLocators(true, true);
            } else {
                if (pickedObject != gCurPlayerData->CurrentHero()) {
                    HideRoute(true, false, true);
                    SetHeroContext(pickedObject, false);
                }
                if (openHero) {
                    m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                    DoAdvCommand();
                }
            }
            break;
        }
        case ADVENTURE_CONTROL_TOWN_LOCATOR_1: {
            locatorIndex = townSlot;
            listIndex = gCurPlayerData->m_townLocatorPage + locatorIndex;
            if (listIndex < 0 || listIndex >= gCurPlayerData->m_townCount)
                break;
            pickedObject = gCurPlayerData->m_townIds[listIndex];
            const bool openTown = LocatorDoubleClick(message->id, pickedObject, *message);
            if (message->modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
                TownQuickView(
                    pickedObject,
                    locatorIndex,
                    QUICK_VIEW_AT_LOCATOR,
                    QUICK_VIEW_AT_LOCATOR
                );
            } else if (ReorderLocator(gCurPlayerData->m_townIds, gCurPlayerData->m_townLocatorPage,
                                      listIndex, message->modifiers)) {
                UpdateTownLocators(true, true);
            } else {
                if (pickedObject != gCurPlayerData->CurrentTown()) {
                    HideRoute(true, false, true);
                    SetTownContext(pickedObject);
                }
                if (openTown) {
                    m_pendingCommand = ADVMGR_COMMAND_TOWN_VIEW;
                    *eventCell = DoAdvCommand();
                }
            }
            break;
        }
        case ADVENTURE_CONTROL_HERO_PAGE_PREVIOUS:
            if (gCurPlayerData->m_heroLocatorPage > 0) {
                gCurPlayerData->m_heroLocatorPage--;
                UpdateHeroLocators(true, true);
            }
            break;
        case ADVENTURE_CONTROL_HERO_PAGE_NEXT:
            if (gCurPlayerData->m_heroLocatorPage + LocatorRows()
                < gCurPlayerData->m_heroCount) {
                gCurPlayerData->m_heroLocatorPage++;
                UpdateHeroLocators(true, true);
            }
            break;
        case ADVENTURE_CONTROL_HERO_KNOB:
            DragLocatorKnob(false, message->mouseY);
            break;
        case ADVENTURE_CONTROL_HERO_SCROLL:
            gCurPlayerData->m_heroLocatorPage = std::clamp(
                (message->mouseY - LOCATOR_SCROLL_MOUSE_BASE_Y)
                    * std::max(0, gCurPlayerData->m_heroCount - (LocatorRows() - 1))
                    / (LOCATOR_SCROLL_MOUSE_SPAN + LocatorExtension()),
                0, std::max(0, gCurPlayerData->m_heroCount - LocatorRows()));
            UpdateHeroLocators(true, true);
            break;
        case ADVENTURE_CONTROL_TOWN_KNOB:
            DragLocatorKnob(true, message->mouseY);
            break;
        case ADVENTURE_CONTROL_TOWN_SCROLL:
            gCurPlayerData->m_townLocatorPage = std::clamp(
                (message->mouseY - LOCATOR_SCROLL_MOUSE_BASE_Y)
                    * std::max(0, gCurPlayerData->m_townCount - (LocatorRows() - 1))
                    / (LOCATOR_SCROLL_MOUSE_SPAN + LocatorExtension()),
                0, std::max(0, gCurPlayerData->m_townCount - LocatorRows()));
            UpdateTownLocators(true, true);
            break;
        case ADVENTURE_CONTROL_TOWN_PAGE_PREVIOUS:
            if (gCurPlayerData->m_townLocatorPage > 0) {
                gCurPlayerData->m_townLocatorPage--;
                UpdateTownLocators(true, true);
            }
            break;
        case ADVENTURE_CONTROL_TOWN_PAGE_NEXT:
            if (gCurPlayerData->m_townLocatorPage + LocatorRows()
                < gCurPlayerData->m_townCount) {
                gCurPlayerData->m_townLocatorPage++;
                UpdateTownLocators(true, true);
            }
            break;
        case ADVENTURE_CONTROL_MAP_VIEW:
            // Selection must use the click's position even when mouse motion
            // was coalesced, or another event has already moved the pointer.
            m_hoverCellX = m_hoverCellY = CURSOR_INVALID_POSITION;
            ProcessHover(message);
            if (m_hoverCellX == CURSOR_INVALID_POSITION || m_hoverCellY == CURSOR_INVALID_POSITION)
                break;
            // At the map's edge the view shows the border beyond it. The
            // original looked such a cell up in the visibility table outside
            // the grid; it now counts as unexplored, so a right click shows
            // the border text and a left click does nothing.
            if (m_mapOriginX + m_hoverCellX < 0 || m_mapOriginY + m_hoverCellY < 0
                || m_mapOriginX + m_hoverCellX >= MAP_CELL_GRID_SIZE
                || m_mapOriginY + m_hoverCellY >= MAP_CELL_GRID_SIZE)
                visible = false;
            else if (!(gGame->m_mapExtra[m_mapOriginX + m_hoverCellX][m_mapOriginY + m_hoverCellY]
                       & gCurPlayerBit))
                visible = false;
            theCell = GetCell(m_mapOriginX + m_hoverCellX, m_mapOriginY + m_hoverCellY);
            if (message->modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
                if (!visible) {
                    QuickInfo(m_hoverCellX, m_hoverCellY);
                } else {
                    if (m_hoverCellX == ADVENTURE_VIEW_CENTER_X && m_hoverCellY == ADVENTURE_VIEW_CENTER_Y
                        && gCurPlayerData->CurrentHero() != HERO_ID_NONE && m_heroMobilized) {
                        pickedObject = MAP_OBJECT_TRIGGER(MAP_OBJECT_HERO);
                        objectIdIndex = gCurPlayerData->CurrentHero();
                    } else {
                        pickedObject = theCell->m_triggerType & MAP_TRIGGER_TYPE_MASK;
                        objectIdIndex = theCell->m_objectMetadata;
                    }
                    switch (MAP_PASSIVE_OBJECT(pickedObject)) {
                        case MAP_OBJECT_HERO:
                            mouseX = m_hoverCellX * CELL_PIXELS - m_cameraOffsetX - HERO_QUICK_VIEW_X_OFFSET;
                            if (mouseX < BORDER_EDGE_SIZE)
                                mouseX = BORDER_EDGE_SIZE;
                            if (mouseX + HERO_QUICK_VIEW_WIDTH > BORDER_MIDDLE_END_X)
                                mouseX = HERO_QUICK_VIEW_RIGHT_X;
                            mouseY = m_hoverCellY * CELL_PIXELS - m_cameraOffsetY - HERO_QUICK_VIEW_Y_OFFSET;
                            if (mouseY < BORDER_EDGE_SIZE)
                                mouseY = BORDER_EDGE_SIZE;
                            if (mouseY + HERO_QUICK_VIEW_HEIGHT > BORDER_MIDDLE_END_Y)
                                mouseY = HERO_QUICK_VIEW_BOTTOM_Y;
                            HeroQuickView(objectIdIndex, QUICK_VIEW_NO_LOCATOR, mouseX, mouseY);
                            break;
                        case MAP_OBJECT_TOWN:
                            mouseX = m_hoverCellX * CELL_PIXELS - m_cameraOffsetX - TOWN_QUICK_VIEW_X_OFFSET;
                            if (mouseX < BORDER_EDGE_SIZE)
                                mouseX = BORDER_EDGE_SIZE;
                            if (mouseX + TOWN_QUICK_VIEW_WIDTH > BORDER_MIDDLE_END_X)
                                mouseX = TOWN_QUICK_VIEW_RIGHT_X;
                            mouseY = m_hoverCellY * CELL_PIXELS - m_cameraOffsetY - TOWN_QUICK_VIEW_Y_OFFSET;
                            if (mouseY < BORDER_EDGE_SIZE)
                                mouseY = BORDER_EDGE_SIZE;
                            if (mouseY + TOWN_QUICK_VIEW_HEIGHT > BORDER_MIDDLE_END_Y)
                                mouseY = TOWN_QUICK_VIEW_BOTTOM_Y;
                            TownQuickView(objectIdIndex, QUICK_VIEW_NO_LOCATOR, mouseX, mouseY);
                            break;
                        default:
                            if (gGame->m_mapExtra[m_mapOriginX + m_hoverCellX]
                                                 [m_mapOriginY + m_hoverCellY]
                                & gCurPlayerBit)
                                QuickInfo(m_hoverCellX, m_hoverCellY);
                            break;
                    }
                }
            } else if (visible) {
                if ((message->modifiers & MESSAGE_MODIFIER_SHIFT_KEYS)
                    && MAP_TRIGGER_OBJECT(theCell->m_triggerType) == MAP_OBJECT_HERO
                    && gGame->GetHero(theCell->m_objectMetadata)->m_owner == gCurPlayer) {
                    if (theCell->m_objectMetadata != gCurPlayerData->CurrentHero()) {
                        HideRoute(true, false, true);
                        SetHeroContext(theCell->m_objectMetadata, false);
                    }
                    break;
                }
                currentHero = NULL;
                mobileResult = false;
                if (gCurPlayerData->m_currentHero != HERO_ID_NONE) {
                    currentHero = gGame->GetHero(gCurPlayerData->m_currentHero);
                    mobileResult = gGame->IsMobile(currentHero->m_id);
                }
                if (currentHero) {
                    if (m_hoverCellX == ADVENTURE_VIEW_CENTER_X && m_hoverCellY == ADVENTURE_VIEW_CENTER_Y
                        && gCurPlayerData->CurrentHero() != HERO_ID_NONE && m_heroMobilized) {
                        m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                        DoAdvCommand();
                    } else if ((!mobileResult
                                || (message->modifiers & MESSAGE_MODIFIER_CONTROL_KEYS)
                                || (gConfig.showRoute
                                    && (currentHero->m_destinationX != m_commandTargetX
                                        || currentHero->m_destinationY != m_commandTargetY)))
                               && gSearchArray->m_cells[m_commandTargetX][m_commandTargetY]
                                      .visited) {
                        currentHero->m_destinationX = m_commandTargetX;
                        currentHero->m_destinationY = m_commandTargetY;
                        ShowRoute(true, 1, true);
                    } else {
                        *eventCell = DoAdvCommand();
                    }
                } else {
                    pickedObject = theCell->m_triggerType & MAP_TRIGGER_TYPE_MASK;
                    objectIdIndex = theCell->m_objectMetadata;
                    if (MAP_PASSIVE_OBJECT(pickedObject) == MAP_OBJECT_HERO) {
                        if (objectIdIndex == gCurPlayerData->CurrentHero()) {
                            m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                            DoAdvCommand();
                        } else if (gGame->GetHero(objectIdIndex)->m_owner == gCurPlayer) {
                            SetHeroContext(objectIdIndex, false);
                        }
                    }
                    if (MAP_PASSIVE_OBJECT(pickedObject) == MAP_OBJECT_TOWN) {
                        if (objectIdIndex == gCurPlayerData->CurrentTown()) {
                            m_pendingCommand = ADVMGR_COMMAND_TOWN_VIEW;
                            *eventCell = DoAdvCommand();
                        } else if (gGame->GetTown(objectIdIndex)->m_owner == gCurPlayer) {
                            SetTownContext(objectIdIndex);
                        }
                    }
                }
            }
            break;
        case ADVENTURE_CONTROL_RADAR: {
            if (message->modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
                NormalDialog(
                    localization::Tr("adventure.world_map_help"),
                    NORMAL_DIALOG_TYPE_QUICK_VIEW
                );
                break;
            }
            const i16 pressX = message->mouseX;
            const i16 pressY = message->mouseY;
            const i8 heroId = HeroAtRadarPoint(pressX, pressY);
            auto panToRadar = [this](i16 x, i16 y) {
                PanMap(
                    (std::clamp<i16>(x, RADAR_LEFT, RADAR_RIGHT - 1) - RADAR_LEFT)
                            / RADAR_CELL_PIXELS - ADVENTURE_VIEW_CENTER_X,
                    (std::clamp<i16>(y, RADAR_TOP, RADAR_BOTTOM - 1) - RADAR_TOP)
                            / RADAR_CELL_PIXELS - ADVENTURE_VIEW_CENTER_Y
                );
            };
            const i8 townId = TownAtRadarPoint(pressX, pressY);
            const bool heroMarker = heroId != HERO_ID_NONE && (townId == GAME_TOWN_NONE
                || ((pressX - RADAR_LEFT) / RADAR_CELL_PIXELS == gGame->GetHero(heroId)->m_x
                    && (pressY - RADAR_TOP) / RADAR_CELL_PIXELS == gGame->GetHero(heroId)->m_y));
            const bool townSelected = !heroMarker && townId != GAME_TOWN_NONE;
            if (!townSelected && !heroMarker)
                m_lastLocatorControl = 0;
            bool panning = !heroMarker && !townSelected;
            if (panning)
                panToRadar(pressX, pressY);
            while (gForegroundApp) {
                Process1WindowsMessage();
                tag_message input = gInputManager->GetEvent();
                if (input.type == MESSAGE_LEFT_BUTTON_UP) {
                    if (!panning && heroMarker && HeroAtRadarPoint(input.x, input.y) == heroId) {
                        const bool open = LocatorDoubleClick(ADVENTURE_CONTROL_RADAR, heroId, *message);
                        if (gCurPlayerData->CurrentHero() != heroId)
                            SetHeroContext(heroId, false);
                        if (open) {
                            m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                            DoAdvCommand();
                        }
                    } else if (!panning && townSelected && TownAtRadarPoint(input.x, input.y) == townId) {
                        const bool open = LocatorDoubleClick(ADVENTURE_CONTROL_RADAR, GAME_HERO_COUNT + townId, *message);
                        if (gCurPlayerData->CurrentTown() != townId)
                            SetTownContext(townId);
                        if (open) {
                            m_pendingCommand = ADVMGR_COMMAND_TOWN_VIEW;
                            *eventCell = DoAdvCommand();
                        }
                    }
                    break;
                }
                if (input.type == MESSAGE_MOUSE_MOVE) {
                    gMouseManager->Main(input);
                    panning = panning || abs(input.x - pressX) > kRadarHeroHitRadius
                                      || abs(input.y - pressY) > kRadarHeroHitRadius;
                    if (panning) {
                        m_lastLocatorControl = 0;
                        panToRadar(input.x, input.y);
                    }
                } else if (input.type == MESSAGE_MOUSE_WHEEL) {
                    ScrollLocators(input);
                } else if (input.type != MESSAGE_NONE) {
                    // A new press/key means the original gesture was interrupted.
                    // Hand it back instead of swallowing it in a nested drag loop.
                    m_lastLocatorControl = 0;
                    gInputManager->PutBackEvent(input);
                    break;
                }
            }
            tag_message clear{};
            SET_WIDGET_MESSAGE(clear, WIDGET_COMMAND_CLEAR_FLAGS, ADVENTURE_CONTROL_RADAR);
            clear.value = WIDGET_FLAG_SELECTED;
            m_adventureWindow->BroadcastMessage(clear);
            break;
        }
        default:
            break;
    }
    if ((message->modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON)
        && message->id >= BOTTOM_VIEW_DRAW_FIRST_WIDGET
        && message->id <= BOTTOM_VIEW_DRAW_LAST_WIDGET)
        NormalDialog(localization::Tr("adventure.status_help"), NORMAL_DIALOG_TYPE_QUICK_VIEW);
    return MESSAGE_DISPATCH_CONSUME;
}

i32 advManager::ProcessDeSelect(
    i16 control,
    i32* result,
    class mapCell** eventCell
) {
    switch (control) {
        case ADVENTURE_CONTROL_CONTINUE_ROUTE:
            m_pendingCommand = ADVMGR_COMMAND_CONTINUE_ROUTE;
            *eventCell = DoAdvCommand();
            break;
        case ADVENTURE_CONTROL_ADVENTURE_OPTIONS:
            AdvPanel();
            break;
        case ADVENTURE_CONTROL_GAME_OPTIONS:
            *result = ControlPanel();
            break;
        case ADVENTURE_CONTROL_END_TURN:
            if (gCurPlayerData->HasMobileHero()) {
                NormalDialog(
                    localization::Tr("adventure.confirm_end_turn"),
                    NORMAL_DIALOG_TYPE_YES_NO
                );
                if (gWindowManager->m_dialogResult == NORMAL_DIALOG_CANCEL)
                    break;
            }
            gGame->NextPlayer();
            break;
        case ADVENTURE_CONTROL_NEXT_HERO:
            HideRoute(true, false, true);
            SetHeroContext(gCurPlayerData->NextHero(1), false);
            break;
        case ADVENTURE_CONTROL_OVERVIEW: {
            const i16 target = gGame->Overview();
            RedrawAdvScreen(true);
            gWindowManager->FadeScreen(WINDOW_FADE_IN, WINDOW_FADE_SHORT, NULL);
            if (target >= game::OVERVIEW_TOWN_TARGET) {
                SetTownContext(target - game::OVERVIEW_TOWN_TARGET);
                m_pendingCommand = ADVMGR_COMMAND_TOWN_VIEW;
                *eventCell = DoAdvCommand();
            } else if (target != game::OVERVIEW_NO_TARGET) {
                SetHeroContext(target, false);
                m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                DoAdvCommand();
            }
            break;
        }
    }
    if (control >= BOTTOM_VIEW_DRAW_FIRST_WIDGET && control <= BOTTOM_VIEW_DRAW_LAST_WIDGET) {
        if (gBottomViewOverride == BOTTOM_VIEW_KINGDOM)
            gBottomViewOverride = BOTTOM_VIEW_NEW_TURN;
        else if (gBottomViewOverride != BOTTOM_VIEW_NONE)
            gBottomViewOverride = BOTTOM_VIEW_NONE;
        else if (gCurBottomView == BOTTOM_VIEW_KINGDOM)
            gBottomViewOverride = BOTTOM_VIEW_NEW_TURN;
        else
            gBottomViewOverride = BOTTOM_VIEW_KINGDOM;
        gBottomViewOverrideEndTime = KBTickCount() + 3000;
        UpdBottomView(true, true, true);
    }
    return MESSAGE_DISPATCH_CONSUME;
}

b32 advManager::ProcessSearch(i32 x, i32 y) {
    class sample* sample = NULL;
    class hero* currentHero;
    i32 pl;
    mapCell* cellPtr;
    tag_message evt;
    i32 gaveArtifact;

    // The search key works without a selected hero too; the original then
    // searched with hero -1, the bytes before the hero table.
    if (gCurPlayerData->m_currentHero == HERO_ID_NONE)
        return 1;
    currentHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    if (currentHero->m_remainingMobility != currentHero->m_mobility) {
        NormalDialog(localization::Tr("adventure.search.requires_full_day"), NORMAL_DIALOG_TYPE_OK);
        return true;
    }
    MobilizeCurrHero(false);
    CompleteDraw(false);
    UpdateScreen(false, false);
    if (x == ADVMGR_SEARCH_VIEW_CENTER) {
        x = m_mapOriginX + ADVENTURE_VIEW_CENTER_X;
        y = m_mapOriginY + ADVENTURE_VIEW_CENTER_Y;
    }
    cellPtr = GetCell(x, y);
    if (cellPtr->m_objectIndex != MAP_CELL_NO_FRAME
        || cellPtr->m_overlayIndex != MAP_CELL_NO_FRAME) {
        NormalDialog(localization::Tr("adventure.search.clear_ground"), NORMAL_DIALOG_TYPE_OK);
        return true;
    }
    if (cellPtr->m_tileIndex < MAP_CELL_TILES_PER_TERRAIN) {
        NormalDialog(localization::Tr("adventure.search.on_land"), NORMAL_DIALOG_TYPE_OK);
        return true;
    }
    if (gHumanPlayer[gCurPlayer])
        sample = LoadPlaySample("DIGSOUND.82M");
    if (cellPtr->m_objectIndex == MAP_CELL_NO_FRAME) {
        cellPtr->m_objectTileset = TILESET_OBJ32_07;
        cellPtr->m_objectIndex = DIG_HOLE_FRAME;
        cellPtr->m_flags |= MAP_CELL_OBJECT_SHADOW_ONLY;
    }
    CompleteDraw(false);
    UpdateScreen(false, false);
    GrabScreen();

    if (gGame->m_ultimateArtifactX == x && gGame->m_ultimateArtifactY == y
        && gGame->m_ultimateArtifactId != ARTIFACT_NONE) {
        gaveArtifact = GiveArtifact(currentHero, gGame->m_ultimateArtifactId);
        if (gaveArtifact == GIVE_ARTIFACT_NO_SLOT) {
            NormalDialog(
                localization::Tr("adventure.search.no_artifact_room"),
                NORMAL_DIALOG_TYPE_OK,
                0x61,
                0x28
            );
        } else {
            if (gHumanPlayer[gCurPlayer]) {
                EventSound(MAP_OBJECT_ULTIMATE_ARTIFACT, 0);
                sprintf(
                    gText,
                    "%s%s",
                    localization::Tr("adventure.search.found_prefix"),
                    gArtifactNames[gGame->m_ultimateArtifactId]
                );
                if (gGame->m_campaignType > 0
                    && gGame->m_campaignScenario == CAMPAIGN_SCENARIO_EYE_OF_GOROS) {
                    sprintf(gText, localization::Tr("adventure.search.eye_of_goros_found"));
                    NormalDialog(gText, NORMAL_DIALOG_TYPE_OK, 0xb1, 0x1c);
                } else {
                    NormalDialog(gText, NORMAL_DIALOG_TYPE_OK, 0xb1, 0x1c);
                    currentHero->ViewArtifact(gGame->m_ultimateArtifactId, false);
                }
                PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
            } else if (gGame->m_campaignType > 0
                       && gGame->m_campaignScenario == CAMPAIGN_SCENARIO_EYE_OF_GOROS) {
                sprintf(gText, localization::Tr("adventure.search.eye_of_goros_enemy"));
                NormalDialog(gText, NORMAL_DIALOG_TYPE_OK, 0xb1, 0x1c);
            }
            gGame->m_ultimateArtifactId = ARTIFACT_NONE;
        }
    } else if (gHumanPlayer[gCurPlayer]) {
        NormalDialog(
            localization::Tr("adventure.search.nothing_here"),
            NORMAL_DIALOG_TYPE_OK,
            0x61,
            0x28
        );
    }
    if (gHumanPlayer[gCurPlayer])
        WaitSample(sample);
    for (pl = 0; pl < gGame->m_playerCount; pl++)
        ComputeUALoc(pl);
    currentHero->m_remainingMobility = 0;
    UpdBottomView(true, true, true);
    CheckDimHero();
    Reseed(0, 0);
    CheckEndGame(false);
    return true;
}

i32 advManager::ProcessHover(struct tag_message* message) {
    i8 trigType;
    i32 baseFrame;
    i32 nDays;
    hero* curHero;
    mapCell* hoverCell;
    town* hoverTown;
    i16 yPos;
    i16 xPos;
    i16 heroPosY;
    i16 heroPosX;

    switch (message->id) {
        case ADVENTURE_CONTROL_MAP_VIEW:
            if (!MapCellAtScreenPoint(message->mouseX, message->mouseY, xPos, yPos)) {
                m_hoverCellX = m_hoverCellY = CURSOR_INVALID_POSITION;
                m_pendingCommand = ADVMGR_COMMAND_NONE;
                gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                return MESSAGE_DISPATCH_CONSUME;
            }
            if (m_hoverCellX != xPos || m_hoverCellY != yPos) {
                m_pendingCommand = ADVMGR_COMMAND_NONE;
                m_hoverCellX = xPos;
                m_hoverCellY = yPos;
                m_commandTargetX = m_mapOriginX + xPos;
                m_commandTargetY = m_mapOriginY + yPos;
                if (m_commandTargetX < 0 || m_commandTargetY < 0
                    || m_commandTargetX > MAP_CELL_GRID_SIZE - 1
                    || m_commandTargetY > MAP_CELL_GRID_SIZE - 1
                    || !(gGame->m_mapExtra[m_commandTargetX][m_commandTargetY] & gCurPlayerBit)) {
                    gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                    return MESSAGE_DISPATCH_CONSUME;
                }
                hoverCell = GetCell(m_commandTargetX, m_commandTargetY);
                if (gCurPlayerData->m_currentHero == HERO_ID_NONE) {
                    if (MAP_TRIGGER_OBJECT(hoverCell->m_triggerType) == MAP_OBJECT_TOWN
                        && gGame->GetTown(hoverCell->m_objectMetadata)->m_owner == gCurPlayer) {
                        gMouseManager->SetPointer(ADVENTURE_POINTER_TOWN);
                        m_pendingCommand = ADVMGR_COMMAND_TOWN_VIEW;
                        return MESSAGE_DISPATCH_CONSUME;
                    } else if (MAP_TRIGGER_OBJECT(hoverCell->m_triggerType) == MAP_OBJECT_HERO
                               && gGame->GetHero(hoverCell->m_objectMetadata)->m_owner
                                      == gCurPlayer) {
                        gMouseManager->SetPointer(ADVENTURE_POINTER_HERO);
                        m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                        return MESSAGE_DISPATCH_CONSUME;
                    } else {
                        gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                        return MESSAGE_DISPATCH_CONSUME;
                    }
                } else {
                    curHero = gGame->GetHero(gCurPlayerData->m_currentHero);
                    heroPosX = curHero->m_x - m_mapOriginX;
                    heroPosY = curHero->m_y - m_mapOriginY;
                    if (xPos == heroPosX && yPos == heroPosY) {
                        gMouseManager->SetPointer(ADVENTURE_POINTER_HERO);
                        m_pendingCommand = ADVMGR_COMMAND_HERO_VIEW;
                        return MESSAGE_DISPATCH_CONSUME;
                    }
                    if (hoverCell->m_secondaryTrigger & MAP_CELL_SECONDARY_BLOCKED) {
                        if (MAP_TRIGGER_OBJECT(hoverCell->m_triggerType) == MAP_OBJECT_TOWN) {
                            hoverTown = gGame->GetTown(hoverCell->m_objectMetadata);
                            if (hoverTown->m_owner == gCurPlayer && m_commandTargetY >= 1
                                && m_commandTargetY < MAP_CELL_GRID_SIZE - 1
                                && (MAP_TRIGGER_OBJECT(
                                        GetCell(m_commandTargetX, m_commandTargetY - 1)
                                            ->m_triggerType
                                    ) == MAP_OBJECT_TOWN
                                    || MAP_TRIGGER_OBJECT(
                                           GetCell(m_commandTargetX, m_commandTargetY - 1)
                                               ->m_secondaryTrigger
                                       ) == MAP_OBJECT_TOWN)
                                && (MAP_TRIGGER_OBJECT(
                                        GetCell(m_commandTargetX, m_commandTargetY + 1)
                                            ->m_triggerType
                                    ) == MAP_OBJECT_TOWN
                                    || MAP_TRIGGER_OBJECT(
                                           GetCell(m_commandTargetX, m_commandTargetY + 1)
                                               ->m_secondaryTrigger
                                       ) == MAP_OBJECT_TOWN)) {
                                gMouseManager->SetPointer(ADVENTURE_POINTER_TOWN);
                                m_pendingCommand = ADVMGR_COMMAND_SELECT_TOWN;
                                return MESSAGE_DISPATCH_CONSUME;
                            }
                        }
                        gSearchArray->m_pathLength = 0;
                        gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                        return MESSAGE_DISPATCH_CONSUME;
                    }
                    if (!((m_cursorType == ADVMGR_HERO_ICON_BOAT
                           || hoverCell->m_tileIndex >= MAP_CELL_TILES_PER_TERRAIN
                           || hoverCell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)
                           || hoverCell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_SHIP)
                           || hoverCell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_SHIPWRECK))
                          && (m_cursorType != ADVMGR_HERO_ICON_BOAT
                              || hoverCell->m_tileIndex < MAP_CELL_TILES_PER_TERRAIN
                              || hoverCell->m_triggerType
                                     == MAP_OBJECT_TRIGGER(MAP_OBJECT_COAST)))) {
                        gSearchArray->m_pathLength = 0;
                        gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                        return MESSAGE_DISPATCH_CONSUME;
                    }
                    SeedTo(m_commandTargetX, m_commandTargetY);
                    if (gSearchArray->m_cells[m_commandTargetX][m_commandTargetY].visited) {
                        const RouteForecast forecast = ForecastRoute(*curHero, m_commandTargetX, m_commandTargetY);
                        nDays = forecast.days < 0 ? ADVENTURE_POINTER_DAY_LAST
                            : std::min<i32>(forecast.days, ADVENTURE_POINTER_DAY_LAST);
                        baseFrame = nDays * ADVENTURE_POINTER_DAY_STRIDE;
                        switch (MAP_TRIGGER_OBJECT(hoverCell->m_triggerType)) {
                            case MAP_OBJECT_SHIP:
                                if (m_cursorType != ADVMGR_HERO_ICON_BOAT) {
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_SAIL);
                                    m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                } else {
                                    gMouseManager->SetPointer(baseFrame);
                                }
                                break;
                            case MAP_OBJECT_COAST:
                                if (m_cursorType == ADVMGR_HERO_ICON_BOAT)
                                    gMouseManager->SetPointer(
                                        baseFrame + ADVENTURE_POINTER_DISEMBARK
                                    );
                                else if (gMapExtra[m_commandTargetX][m_commandTargetY]
                                         & MAP_EXTRA_MONSTER_ADJACENT)
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_ATTACK);
                                else
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_MOVE);
                                m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                break;
                            case MAP_OBJECT_MONSTER:
                                gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_ATTACK);
                                m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                break;
                            case MAP_OBJECT_HERO:
                                if (gGame->GetHero(hoverCell->m_objectMetadata)->m_owner
                                    != gCurPlayer) {
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_ATTACK);
                                    m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                } else {
                                    gMouseManager->SetPointer(
                                        baseFrame + ADVENTURE_POINTER_SELECT_HERO
                                    );
                                    m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                }
                                break;
                            case MAP_OBJECT_TOWN:
                                hoverTown = gGame->GetTown(hoverCell->m_objectMetadata);
                                if ((hoverCell->m_triggerType & MAP_TRIGGER_EVENT)
                                    && hoverTown->m_owner != gCurPlayer
                                    && hoverTown->HasGarrison()) {
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_ATTACK);
                                    m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                    break;
                                }
                                goto defaultHover;
                            default:
                            defaultHover:
                                trigType = MAP_TRIGGER_OBJECT(hoverCell->m_triggerType);
                                if ((gMapExtra[m_commandTargetX][m_commandTargetY]
                                     & MAP_EXTRA_MONSTER_ADJACENT)
                                    && m_cursorType != ADVMGR_HERO_ICON_BOAT
                                    && trigType != MAP_OBJECT_SKELETON
                                    && trigType != MAP_OBJECT_TREASURE_CHEST
                                    && trigType != MAP_OBJECT_CAMPFIRE
                                    && trigType != MAP_OBJECT_ANCIENT_LAMP
                                    && trigType != MAP_OBJECT_RESOURCE
                                    && trigType != MAP_OBJECT_ARTIFACT) {
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_ATTACK);
                                } else if (hoverCell->m_triggerType & MAP_TRIGGER_EVENT) {
                                    if (m_cursorType != ADVMGR_HERO_ICON_BOAT) {
                                        switch (MAP_TRIGGER_OBJECT(hoverCell->m_triggerType)) {
                                            case MAP_OBJECT_ALCHEMIST_LAB:
                                            case MAP_OBJECT_SIGNPOST:
                                            case MAP_OBJECT_SKELETON:
                                            case MAP_OBJECT_DAEMON_CAVE:
                                            case MAP_OBJECT_TREASURE_CHEST:
                                            case MAP_OBJECT_FAERIE_RING:
                                            case MAP_OBJECT_CAMPFIRE:
                                            case MAP_OBJECT_FOUNTAIN:
                                            case MAP_OBJECT_GAZEBO:
                                            case MAP_OBJECT_ANCIENT_LAMP:
                                            case MAP_OBJECT_GRAVEYARD:
                                            case MAP_OBJECT_STRAW_HUT:
                                            case MAP_OBJECT_HOUSE:
                                            case MAP_OBJECT_CABIN:
                                            case MAP_OBJECT_DWARF_LOG_CABIN:
                                            case MAP_OBJECT_PEASANT_LOG_CABIN:
                                            case MAP_OBJECT_INN_1:
                                            case MAP_OBJECT_INN_2:
                                            case MAP_OBJECT_INN_3:
                                            case MAP_OBJECT_INN_4:
                                            case MAP_OBJECT_DRAGON_CITY:
                                            case MAP_OBJECT_LIGHTHOUSE:
                                            case MAP_OBJECT_WATERWHEEL:
                                            case MAP_OBJECT_MINE:
                                            case MAP_OBJECT_OBELISK:
                                            case MAP_OBJECT_OASIS:
                                            case MAP_OBJECT_RESOURCE:
                                            case MAP_OBJECT_SAWMILL:
                                            case MAP_OBJECT_RANKING_SHRINE:
                                            case MAP_OBJECT_SPELL_SHRINE:
                                            case MAP_OBJECT_SHIPWRECK:
                                            case MAP_OBJECT_STATUE:
                                            case MAP_OBJECT_DESERT_TENT:
                                            case MAP_OBJECT_TOWN:
                                            case MAP_OBJECT_STONE_LITHS:
                                            case MAP_OBJECT_WAGON_CAMP:
                                            case MAP_OBJECT_WELL:
                                            case MAP_OBJECT_WHIRLPOOL:
                                            case MAP_OBJECT_WINDMILL:
                                            case MAP_OBJECT_OAK_TREE:
                                            case MAP_OBJECT_MEGALITH:
                                            case MAP_OBJECT_ARTIFACT:
                                                gMouseManager->SetPointer(
                                                    baseFrame + ADVENTURE_POINTER_ACTION
                                                );
                                                break;
                                            default:
                                                if (gMapExtra[m_commandTargetX][m_commandTargetY]
                                                    & MAP_EXTRA_MONSTER_ADJACENT)
                                                    gMouseManager->SetPointer(
                                                        baseFrame + ADVENTURE_POINTER_ATTACK
                                                    );
                                                else
                                                    gMouseManager->SetPointer(
                                                        baseFrame + ADVENTURE_POINTER_MOVE
                                                    );
                                                break;
                                        }
                                    } else {
                                        switch (MAP_TRIGGER_OBJECT(hoverCell->m_triggerType)) {
                                            case MAP_OBJECT_BUOY:
                                            case MAP_OBJECT_WHIRLPOOL:
                                                gMouseManager->SetPointer(
                                                    nDays + ADVENTURE_POINTER_WATER_ACTION
                                                );
                                                break;
                                            default:
                                                gMouseManager->SetPointer(
                                                    baseFrame + ADVENTURE_POINTER_SAIL
                                                );
                                                break;
                                        }
                                    }
                                } else if (m_cursorType == ADVMGR_HERO_ICON_BOAT) {
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_SAIL);
                                } else {
                                    gMouseManager->SetPointer(baseFrame + ADVENTURE_POINTER_MOVE);
                                }
                                m_pendingCommand = ADVMGR_COMMAND_MOVE_TO;
                                break;
                        }
                        return MESSAGE_DISPATCH_CONSUME;
                    } else {
                        gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                        return MESSAGE_DISPATCH_CONSUME;
                    }
                }
            }
            break;
        default:
            if (!(gMouseManager->m_cursorFrame >= HOVER_SCROLL_FRAME_FIRST
                  && gMouseManager->m_cursorFrame < HOVER_SCROLL_FRAME_END && MouseInScrollZone()))
                gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
            return MESSAGE_DISPATCH_CONSUME;
    }
    return MESSAGE_DISPATCH_CONSUME;
}

advManager::RouteForecast advManager::ForecastRoute(hero& subject, i16 targetX, i16 targetY) {
    RouteForecast forecast;
    forecast.remaining = subject.m_remainingMobility;
    i16 x = subject.m_x;
    i16 y = subject.m_y;
    gSearchArray->BuildPath(x, y, targetX, targetY, SEARCH_UNLIMITED_COST);
    if (gSearchArray->m_pathLength <= 0) {
        forecast.days = -1;
        return forecast;
    }
    for (int index = gSearchArray->m_pathLength - 1; index >= 0; --index) {
        const i8 direction = gSearchArray->m_directions[index];
        const i32 terrain = CELL_TERRAIN(GetCell(x, y));
        const i32 diagonal = direction & MAP_DIRECTION_DIAGONAL_BIT;
        i32 stepCost = CalcTerrainCost(terrain, diagonal, forecast.remaining, subject.m_heroClass);
        if (forecast.remaining < stepCost) {
            forecast.remaining = subject.CalcMobility();
            if (gGame->m_campaignType > 0
                && x == gCampaignScenarios[gGame->m_campaignScenario].victoryTownX
                && y == gCampaignScenarios[gGame->m_campaignScenario].victoryTownY)
                forecast.remaining = 0;
            stepCost = CalcTerrainCost(terrain, diagonal, forecast.remaining, subject.m_heroClass);
            if (forecast.remaining < stepCost) {
                forecast.days = -1;
                return forecast;
            }
            ++forecast.days;
        }
        forecast.cost += stepCost;
        x += gNormalDirTable[direction].x;
        y += gNormalDirTable[direction].y;
        forecast.remaining = MobilityAfterStep(forecast.remaining, stepCost,
                                                CELL_TERRAIN(GetCell(x, y)), subject.m_heroClass);
    }
    return forecast;
}

void advManager::UpdateMovementPreview() {
    RouteForecast forecast;
    forecast.days = -1;
    int available = 0;
    bool meeting = false;
    if ((gInputManager->m_modifiers & MESSAGE_MODIFIER_ALT) && gForegroundApp
        && !gAllBlack && !gHeroMoving && gSeedingValid
        && m_pendingCommand == ADVMGR_COMMAND_MOVE_TO
        && gCurPlayerData->CurrentHero() != HERO_ID_NONE) {
        i16 x, y;
        gMouseManager->MouseCoords(x, y);
        i16 cellX, cellY;
        if (MapCellAtScreenPoint(x, y, cellX, cellY)
            && cellX + m_mapOriginX == m_commandTargetX
            && cellY + m_mapOriginY == m_commandTargetY
            && MAP_CELL_IN_BOUNDS(m_commandTargetX, m_commandTargetY)) {
            const auto& target = gSearchArray->m_cells[m_commandTargetX][m_commandTargetY];
            if (target.visited) {
                hero& subject = *gGame->GetHero(gCurPlayerData->CurrentHero());
                forecast = ForecastRoute(subject, m_commandTargetX, m_commandTargetY);
                available = subject.m_remainingMobility;
                const mapCell* destination = GetCell(m_commandTargetX, m_commandTargetY);
                meeting = MAP_TRIGGER_OBJECT(destination->m_triggerType) == MAP_OBJECT_HERO
                    && destination->m_objectMetadata < GAME_HERO_COUNT
                    && gGame->GetHero(destination->m_objectMetadata)->m_owner == gCurPlayer;
            }
        }
    }
    const i32 cost = forecast.days < 0 ? -1 : forecast.cost;
    if (cost == m_previewCost && available == m_previewAvailable && forecast.days == m_previewDays
        && forecast.remaining == m_previewRemaining && meeting == m_previewMeeting)
        return;
    m_previewCost = cost;
    m_previewAvailable = available;
    m_previewRemaining = forecast.remaining;
    m_previewDays = forecast.days;
    m_previewMeeting = meeting;
    m_adventureWindow->DrawWindow(0, BOTTOM_VIEW_DRAW_FIRST_WIDGET, BOTTOM_VIEW_DRAW_LAST_WIDGET);
    gWindowManager->UpdateScreenRegion(BOTTOM_VIEW_PANEL_X, StatusY(),
                                       BOTTOM_VIEW_PANEL_WIDTH, BOTTOM_VIEW_PANEL_HEIGHT);
}

void advManager::UpdateScreen(b8 cursorUpdate, b8 forceUpdate) {
    if (!forceUpdate && !gShowIt) {
        if (gTimers[ADVENTURE_FRAME_TIMER_SLOT] < KBTickCount())
            gTimers[ADVENTURE_FRAME_TIMER_SLOT] = KBTickCount() + TIMER_DELAY;
        return;
    }
    m_adventureWindow->DrawWindow(0, kAdventureIndicators, kAdventureIndicators);
    gMouseManager
        ->SaveAndDraw(gWindowManager->m_screen, m_scrollOffsetX, m_scrollOffsetY, cursorUpdate);
    PollSound();
    gScrollX = m_scrollOffsetX;
    gScrollY = m_scrollOffsetY;
    if (gLimitUpdMinX == UPDATE_NONE)
        BlitBitmapToScreen(
            gWindowManager->m_screen,
            BORDER_EDGE_SIZE,
            BORDER_EDGE_SIZE,
            ADVENTURE_VIEWPORT_INNER_WIDTH,
            ADVENTURE_VIEWPORT_INNER_HEIGHT,
            BORDER_EDGE_SIZE,
            BORDER_EDGE_SIZE
        );
    else
        BlitBitmapToScreen(
            gWindowManager->m_screen,
            gLimitUpdMinX,
            gLimitUpdMinY,
            gLimitUpdMaxX - gLimitUpdMinX,
            gLimitUpdMaxY - gLimitUpdMinY,
            gLimitUpdMinX,
            gLimitUpdMinY
        );
    gScrollY = 0;
    gScrollX = gScrollY;
    PollSound();
    if (gTimers[ADVENTURE_FRAME_TIMER_SLOT] < KBTickCount()) {
        ++m_animationFrame;
        if (m_animationFrame >= UPDATE_FRAME_CYCLE)
            m_animationFrame = 0;
        gTimers[ADVENTURE_FRAME_TIMER_SLOT] = KBTickCount() + TIMER_DELAY;
        if (m_animationFrame == UPDATE_FRAME_STEP_1 || m_animationFrame == UPDATE_FRAME_STEP_3
            || m_animationFrame == UPDATE_FRAME_STEP_5) {
            ++m_animationPhases[ANIMATION_PHASE_COLUMN_1];
            m_animationPhases[ANIMATION_PHASE_COLUMN_1] %= UPDATE_ANIMATION_PHASES;
            ++m_animationPhases[ANIMATION_PHASE_COLUMN_3];
            m_animationPhases[ANIMATION_PHASE_COLUMN_3] %= UPDATE_ANIMATION_PHASES;
        } else {
            ++m_animationPhases[ANIMATION_PHASE_COLUMN_0];
            m_animationPhases[ANIMATION_PHASE_COLUMN_0] %= UPDATE_ANIMATION_PHASES;
            ++m_animationPhases[ANIMATION_PHASE_COLUMN_2];
            m_animationPhases[ANIMATION_PHASE_COLUMN_2] %= UPDATE_ANIMATION_PHASES;
        }
    }
    gLimitUpdMinX = UPDATE_NONE;
    gMouseManager->RestoreUnderlying();
    Process1WindowsMessage();
}

void advManager::CompleteDraw(i16 originX, i16 originY, b32 forceDraw) {
    i32 drawX;
    i32 drawY;

    PollSound();
    if (!forceDraw && !gShowIt)
        return;

    gLimitUpdMinX = UPDATE_NONE;
    m_previousOriginX = m_mapOriginX;
    m_previousOriginY = m_mapOriginY;
    if (gAllBlack)
        m_mapOriginX = m_mapOriginY = 0;
    m_comboHeroDrawn = false;
    m_forceCompleteDraw = false;

    for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++)
        DrawCell(
            originX + drawX,
            originY,
            drawX,
            0,
            ADVMGR_DRAW_GROUND | ADVMGR_DRAW_OBJECT,
            false,
            forceDraw
        );
    for (drawY = 1; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++)
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++)
            DrawCell(
                originX + drawX,
                originY + drawY,
                drawX,
                drawY,
                ADVMGR_DRAW_GROUND,
                false,
                forceDraw
            );

    for (drawY = 1; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
        PollSound();
        if (m_cursorDirection > MAP_DIRECTION_UNMIRRORED_LAST) {
            for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++)
                DrawCell(
                    originX + drawX,
                    originY + drawY - 1,
                    drawX,
                    drawY - 1,
                    ADVMGR_DRAW_OVERLAY | ADVMGR_DRAW_HERO,
                    false,
                    forceDraw
                );
        } else {
            for (drawX = ADVENTURE_VIEWPORT_CELLS_X - 1; drawX >= 0; drawX--)
                DrawCell(
                    originX + drawX,
                    originY + drawY - 1,
                    drawX,
                    drawY - 1,
                    ADVMGR_DRAW_OVERLAY | ADVMGR_DRAW_HERO,
                    false,
                    forceDraw
                );
        }
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++)
            DrawCell(
                originX + drawX,
                originY + drawY,
                drawX,
                drawY,
                ADVMGR_DRAW_OBJECT,
                false,
                forceDraw
            );
    }

    for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++)
        DrawCell(
            originX + drawX,
            originY + ADVENTURE_VIEWPORT_CELLS_Y - 1,
            drawX,
            ADVENTURE_VIEWPORT_CELLS_Y - 1,
            ADVMGR_DRAW_OVERLAY | ADVMGR_DRAW_HERO,
            false,
            forceDraw
        );
    for (drawY = 0; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++)
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++)
            DrawCell(
                originX + drawX,
                originY + drawY,
                drawX,
                drawY,
                ADVMGR_DRAW_CLOUD,
                false,
                forceDraw
            );

    PollSound();
    UpdBottomView(false, true, true);
    if (!gNoBorder && !gAllBlack && (m_mapDragging || HasCameraOffset())) {
        if (!m_cameraFrame)
            m_cameraFrame = new bitmap(BITMAP_TYPE_MEMORY, ADVENTURE_VIEWPORT_WIDTH, ADVENTURE_VIEWPORT_HEIGHT);
        gWindowManager->m_screen->CopyTo(m_cameraFrame, 0, 0, 0, 0,
                                        ADVENTURE_VIEWPORT_WIDTH, ADVENTURE_VIEWPORT_HEIGHT);
        DrawCameraFrame();
    }
    DrawAdventureBorder();
    if (gAllBlack) {
        m_mapOriginX = m_previousOriginX;
        m_mapOriginY = m_previousOriginY;
    }
}

void advManager::CompleteDraw(b32 forceDraw) {
    CompleteDraw(m_mapOriginX, m_mapOriginY, forceDraw);
}

i32 advManager::GetCloudLookup(i32 x, i32 y) {
    i32 cloudMask = 0;

    if (x < 1)
        cloudMask |= CLOUD_WEST_EDGE;
    else if (x >= MAP_CELL_GRID_SIZE - 1)
        cloudMask |= CLOUD_EAST_EDGE;
    if (y < 1)
        cloudMask |= CLOUD_NORTH_EDGE;
    else if (y >= MAP_CELL_GRID_SIZE - 1)
        cloudMask |= CLOUD_SOUTH_EDGE;
    if (cloudMask == 0) {
        if ((gGame->m_mapExtra[x][y - 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_NORTH;
        if ((gGame->m_mapExtra[x + 1][y] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_EAST;
        if ((gGame->m_mapExtra[x][y + 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_SOUTH;
        if ((gGame->m_mapExtra[x - 1][y] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_WEST;
        if ((gGame->m_mapExtra[x + 1][y - 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_NORTH_EAST;
        if ((gGame->m_mapExtra[x + 1][y + 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_SOUTH_EAST;
        if ((gGame->m_mapExtra[x - 1][y + 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_SOUTH_WEST;
        if ((gGame->m_mapExtra[x - 1][y - 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_NORTH_WEST;
    } else {
        if ((cloudMask & CLOUD_NORTH) == 0
            && (gGame->m_mapExtra[x][y - 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_NORTH;
        if ((cloudMask & CLOUD_EAST) == 0
            && (gGame->m_mapExtra[x + 1][y] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_EAST;
        if ((cloudMask & CLOUD_SOUTH) == 0
            && (gGame->m_mapExtra[x][y + 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_SOUTH;
        if ((cloudMask & CLOUD_WEST) == 0
            && (gGame->m_mapExtra[x - 1][y] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_WEST;
        if ((cloudMask & CLOUD_NORTH_EAST) == 0
            && (gGame->m_mapExtra[x + 1][y - 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_NORTH_EAST;
        if ((cloudMask & CLOUD_SOUTH_EAST) == 0
            && (gGame->m_mapExtra[x + 1][y + 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_SOUTH_EAST;
        if ((cloudMask & CLOUD_SOUTH_WEST) == 0
            && (gGame->m_mapExtra[x - 1][y + 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_SOUTH_WEST;
        if ((cloudMask & CLOUD_NORTH_WEST) == 0
            && (gGame->m_mapExtra[x - 1][y - 1] & gCurWatchPlayerBit) == 0)
            cloudMask |= CLOUD_NORTH_WEST;
    }
    return gCloudType[cloudMask];
}

void advManager::DrawCell(
    i16 mapX,
    i16 mapY,
    i16 screenX,
    i16 screenY,
    i8 drawMask,
    b8 drawingPuzzle,
    b8 forceDraw
) {
    i32 heroYOffset;
    i8 playerColor;
    b8 showHero;
    hero* occupyingHero;
    i8 heroIcon;
    i8 heroFrame;
    i32 savedSuppressed;
    mapCell* drawnCell;
    i16 cellPixelY;
    i16 cellPixelX;

    if (!forceDraw && !gShowIt)
        return;
    cellPixelX = screenX << CELL_PIXEL_SHIFT;
    cellPixelY = screenY << CELL_PIXEL_SHIFT;
    drawnCell = GetCell(mapX, mapY);
    if (!gAllBlack
        && (mapX < 0 || mapY < 0 || mapX >= MAP_CELL_GRID_SIZE || mapY >= MAP_CELL_GRID_SIZE)) {
        s_drawStoneTile = STONE_TILE_NONE;
        if (mapX == STONE_BORDER_LOW) {
            if (mapY == STONE_BORDER_LOW)
                s_drawStoneTile = STONE_TILE_TOP_LEFT;
            else if (mapY == MAP_CELL_GRID_SIZE)
                s_drawStoneTile = STONE_TILE_BOTTOM_LEFT;
            else if (mapY >= 0 && mapY < MAP_CELL_GRID_SIZE)
                s_drawStoneTile = ((mapY + STONE_PATTERN_COORDINATE_SHIFT) & CLOUD_VARIANT_MASK)
                                  + STONE_TILE_LEFT_BASE;
        } else if (mapX == MAP_CELL_GRID_SIZE) {
            if (mapY == STONE_BORDER_LOW)
                s_drawStoneTile = STONE_TILE_TOP_RIGHT;
            else if (mapY == MAP_CELL_GRID_SIZE)
                s_drawStoneTile = STONE_TILE_BOTTOM_RIGHT;
            else if (mapY >= 0 && mapY < MAP_CELL_GRID_SIZE)
                s_drawStoneTile = ((mapY + STONE_PATTERN_COORDINATE_SHIFT) & CLOUD_VARIANT_MASK)
                                  + STONE_TILE_RIGHT_BASE;
        } else if (mapY == STONE_BORDER_LOW) {
            if (mapX >= 0 && mapX < MAP_CELL_GRID_SIZE)
                s_drawStoneTile = ((mapX + STONE_PATTERN_COORDINATE_SHIFT) & CLOUD_VARIANT_MASK)
                                  + STONE_TILE_TOP_BASE;
        } else if (mapY == MAP_CELL_GRID_SIZE && mapX >= 0 && mapX < MAP_CELL_GRID_SIZE) {
            s_drawStoneTile = ((mapX + STONE_PATTERN_COORDINATE_SHIFT) & CLOUD_VARIANT_MASK)
                              + STONE_TILE_BOTTOM_BASE;
        }
        if (s_drawStoneTile == STONE_TILE_NONE)
            s_drawStoneTile =
                (mapX + STONE_PATTERN_COORDINATE_SHIFT) % CLOUD_VARIANTS
                + ((mapY + STONE_PATTERN_COORDINATE_SHIFT) % CLOUD_VARIANTS) * CLOUD_VARIANTS;
        AdventureTileToBitmap(
            m_stoneTiles,
            s_drawStoneTile,
            gWindowManager->m_screen,
            cellPixelX,
            cellPixelY
        );
        return;
    } else {
        if (!((!gAllBlack && (gGame->m_mapExtra[mapX][mapY] & gCurWatchPlayerBit))
              || drawingPuzzle)) {
            s_drawCovered = true;
            if (gAllBlack)
                s_drawCloudFrame = 0;
            else
                s_drawCloudFrame = GetCloudLookup(mapX, mapY);
            if (s_drawCloudFrame == 0) {
                if (drawMask & ADVMGR_DRAW_CLOUD)
                    AdventureTileToBitmap(
                        m_cloudTiles,
                        (mapX + mapY) & CLOUD_VARIANT_MASK,
                        gWindowManager->m_screen,
                        cellPixelX,
                        cellPixelY
                    );
                return;
            }
            if (s_drawCloudFrame >= CLOUD_FLIPPED_FRAME_BASE) {
                s_drawFlipCloud = true;
                s_drawCloudFrame -= CLOUD_FLIPPED_FRAME_BASE;
            } else {
                s_drawFlipCloud = false;
            }
            if ((s_drawCloudFrame == CLOUD_X_ALTERNATE_FRAME_1
                 || s_drawCloudFrame == CLOUD_X_ALTERNATE_FRAME_2)
                && (mapX & 1))
                s_drawCloudFrame++;
            if (s_drawCloudFrame == CLOUD_Y_ALTERNATE_FRAME && (mapY & 1))
                s_drawCloudFrame++;
        } else {
            s_drawCovered = false;
        }
    }
    if (drawMask & ADVMGR_DRAW_CLOUD) {
        if (s_drawCovered) {
            if (s_drawFlipCloud)
                AdventureFlipIconToBitmap(
                    m_cloudOverlayIcon,
                    gWindowManager->m_screen,
                    cellPixelX + CELL_LAST_PIXEL,
                    cellPixelY,
                    s_drawCloudFrame - 1,
                    ICON_DRAW_OFFSET_FULL
                );
            else
                AdventureIconToBitmap(
                    m_cloudOverlayIcon,
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY,
                    s_drawCloudFrame - 1,
                    ICON_DRAW_OFFSET_FULL
                );
        } else if (m_routeShown && ADVMGR_ROUTE_AT(mapX, mapY)) {
            if (ADVMGR_ROUTE_AT(mapX, mapY) & ROUTE_CELL_FLIPPED)
                AdventureFlipIconToBitmap(
                    m_objectIcons[TILESET_ROUTE],
                    gWindowManager->m_screen,
                    cellPixelX + CELL_LAST_PIXEL,
                    cellPixelY + ROUTE_DRAW_Y_OFFSET,
                    (ADVMGR_ROUTE_AT(mapX, mapY) & ROUTE_CELL_FRAME_MASK) - 1,
                    ICON_DRAW_OFFSET_FULL
                );
            else
                AdventureIconToBitmap(
                    m_objectIcons[TILESET_ROUTE],
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY + ROUTE_DRAW_Y_OFFSET,
                    (ADVMGR_ROUTE_AT(mapX, mapY) & ROUTE_CELL_FRAME_MASK) - 1,
                    ICON_DRAW_OFFSET_FULL
                );
        }
        return;
    }
    if (drawMask & ADVMGR_DRAW_GROUND) {
        s_drawGroundTile = drawnCell->m_flags;
        s_drawGroundTile <<= MAP_CELL_GROUND_FLIP_SHIFT;
        s_drawGroundTile |= drawnCell->m_tileIndex & 0xff;
        AdventureTileToBitmap(
            m_groundTiles,
            s_drawGroundTile,
            gWindowManager->m_screen,
            cellPixelX,
            cellPixelY
        );
        if (drawnCell->m_flags & MAP_CELL_OBJECT_SHADOW_ONLY) {
            s_drawTileset =
                (drawnCell->m_objectTileset & MAP_CELL_TILESET_MASK);
            if (!drawingPuzzle || s_drawTileset != TILESET_OBJ32_07
                || drawnCell->m_objectIndex != DIG_HOLE_FRAME)
                AdventureIconToBitmap(
                    m_objectIcons[s_drawTileset],
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY,
                    drawnCell->m_objectIndex,
                    ICON_DRAW_OFFSET_FULL
                );
        }
    }
    if (drawMask & ADVMGR_DRAW_OBJECT) {
        if (!(drawnCell->m_flags & MAP_CELL_OBJECT_SHADOW_ONLY)
            && drawnCell->m_objectIndex != MAP_CELL_NO_FRAME) {
            s_drawTileset =
                (drawnCell->m_objectTileset & MAP_CELL_TILESET_MASK);
            if (s_drawTileset != TILESET_MONS32) {
                AdventureIconToBitmap(
                    m_objectIcons[s_drawTileset],
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY,
                    drawnCell->m_objectIndex,
                    ICON_DRAW_OFFSET_FULL
                );
                if (drawnCell->m_flags & MAP_CELL_OBJECT_ANIMATED)
                    AdventureIconToBitmap(
                        m_objectIcons[s_drawTileset],
                        gWindowManager->m_screen,
                        cellPixelX,
                        cellPixelY,
                        drawnCell->m_objectIndex + m_animationFrame + 1,
                        ICON_DRAW_OFFSET_FULL
                    );
            }
        }
        if (drawnCell->m_flags & MAP_CELL_OBJECT_EXTRA)
            AdventureIconToBitmap(
                m_objectIcons[(drawnCell->m_objectTileset >> MAP_CELL_EXTRA_TILESET_SHIFT)],
                gWindowManager->m_screen,
                cellPixelX,
                cellPixelY,
                drawnCell->m_extraFrame,
                ICON_DRAW_OFFSET_FULL
            );
    }
    if (drawMask & ADVMGR_DRAW_HERO) {
        showHero = false;
        occupyingHero = NULL;
        if (!(drawnCell->m_flags & MAP_CELL_OBJECT_SHADOW_ONLY)
            && drawnCell->m_objectIndex != MAP_CELL_NO_FRAME) {
            s_drawTileset =
                (drawnCell->m_objectTileset & MAP_CELL_TILESET_MASK);
            if (s_drawTileset == TILESET_MONS32
                && drawnCell->m_objectIndex <= CREATURE_LAST) {
                if (mapX == m_combatMonsterX && mapY == m_combatMonsterY) {
                    if (m_combatMonsterFacingLeft)
                        AdventureFlipIconToBitmap(
                            m_objectIcons[TILESET_MINIMON],
                            gWindowManager->m_screen,
                            cellPixelX + 36,
                            cellPixelY - MONSTER_DRAW_Y_OFFSET,
                            drawnCell->m_objectIndex * MONSTER_FRAME_STRIDE
                                + MONSTER_FACING_FRAME_BASE,
                            ICON_DRAW_OFFSET_FULL
                        );
                    else
                        AdventureIconToBitmap(
                            m_objectIcons[TILESET_MINIMON],
                            gWindowManager->m_screen,
                            cellPixelX,
                            cellPixelY - MONSTER_DRAW_Y_OFFSET,
                            drawnCell->m_objectIndex * MONSTER_FRAME_STRIDE
                                + MONSTER_FACING_FRAME_BASE,
                            ICON_DRAW_OFFSET_FULL
                        );
                } else {
                    ClipIconToBitmap(
                        m_objectIcons[TILESET_MINIMON],
                        gWindowManager->m_screen,
                        cellPixelX,
                        cellPixelY - MONSTER_DRAW_Y_OFFSET,
                        drawnCell->m_objectIndex * MONSTER_FRAME_STRIDE
                            + m_animationPhases[mapX & (ADVMGR_ANIMATION_PHASE_COUNT - 1)],
                        ICON_DRAW_OFFSET_FULL,
                        0,
                        0,
                        ADVENTURE_VIEWPORT_WIDTH,
                        ADVENTURE_VIEWPORT_HEIGHT
                    );
                }
            }
        }
        if (drawnCell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_SHIP)) {
            playerColor = PLAYER_COLOR_NONE;
            heroIcon = ADVMGR_HERO_ICON_BOAT;
            heroFrame = GetCursorBaseFrame(gGame->m_boats[drawnCell->m_objectMetadata].direction);
            showHero = true;
            heroYOffset = HERO_BOAT_Y_OFFSET;
        } else {
            heroYOffset = 0;
            if (drawnCell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)) {
                occupyingHero = gGame->GetHero(drawnCell->m_objectMetadata);
                // A hero without an owner has no flag; the original read the
                // colour from before the player table (and drew a flag icon
                // from wherever it pointed).
                playerColor = (occupyingHero->IsEmbarked() || occupyingHero->m_owner < 0
                                  ? PLAYER_COLOR_NONE
                                  : (gGame->m_players[occupyingHero->m_owner].m_color));
                heroIcon = occupyingHero->IsEmbarked() ? static_cast<i8>(ADVMGR_HERO_ICON_BOAT)
                                                       : occupyingHero->m_heroClass;
                heroFrame = GetCursorBaseFrame(occupyingHero->m_direction);
                showHero = true;
                if (occupyingHero->IsEmbarked())
                    heroYOffset = HERO_BOAT_Y_OFFSET;
            }
        }
        if (showHero) {
            if (heroFrame & HERO_FRAME_MIRROR_FLAG) {
                if (screenX == 0 || screenY <= 1 || screenX == ADVENTURE_VIEWPORT_CELLS_X - 1
                    || screenY == ADVENTURE_VIEWPORT_CELLS_Y - 1) {
                    AdventureFlipClippedIconToBitmap(
                        m_heroIcons[heroIcon],
                        gWindowManager->m_screen,
                        cellPixelX + CELL_PIXELS,
                        cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                        heroFrame & HERO_FRAME_INDEX_MASK,
                        ICON_DRAW_OFFSET_FULL
                    );
                    if (playerColor != PLAYER_COLOR_NONE)
                        AdventureFlipClippedIconToBitmap(
                            m_flagIcons[playerColor],
                            gWindowManager->m_screen,
                            cellPixelX + CELL_PIXELS,
                            cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                            heroFrame & HERO_FRAME_INDEX_MASK,
                            ICON_DRAW_OFFSET_FULL
                        );
                } else {
                    if (m_drawHeroShadows && heroIcon != ADVMGR_HERO_ICON_BOAT)
                        AdventureFlipDimIconToBitmap(
                            m_shadowIcon,
                            gWindowManager->m_screen,
                            cellPixelX + CELL_PIXELS,
                            cellPixelY + CELL_LAST_PIXEL,
                            heroFrame & HERO_FRAME_INDEX_MASK,
                            ICON_DRAW_OFFSET_FULL
                        );
                    AdventureFlipIconToBitmap(
                        m_heroIcons[heroIcon],
                        gWindowManager->m_screen,
                        cellPixelX + CELL_PIXELS,
                        cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                        heroFrame & HERO_FRAME_INDEX_MASK,
                        ICON_DRAW_OFFSET_FULL
                    );
                    if (playerColor != PLAYER_COLOR_NONE)
                        AdventureFlipIconToBitmap(
                            m_flagIcons[playerColor],
                            gWindowManager->m_screen,
                            cellPixelX + CELL_PIXELS,
                            cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                            heroFrame & HERO_FRAME_INDEX_MASK,
                            ICON_DRAW_OFFSET_FULL
                        );
                }
            } else if (screenX == 0 || screenY <= 1 || screenX == ADVENTURE_VIEWPORT_CELLS_X - 1
                       || screenY == ADVENTURE_VIEWPORT_CELLS_Y - 1) {
                AdventureClippedIconToBitmap(
                    m_heroIcons[heroIcon],
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                    heroFrame,
                    ICON_DRAW_OFFSET_FULL
                );
                if (playerColor != PLAYER_COLOR_NONE)
                    AdventureClippedIconToBitmap(
                        m_flagIcons[playerColor],
                        gWindowManager->m_screen,
                        cellPixelX,
                        cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                        heroFrame & HERO_FRAME_INDEX_MASK,
                        ICON_DRAW_OFFSET_FULL
                    );
            } else {
                if (m_drawHeroShadows && heroIcon != ADVMGR_HERO_ICON_BOAT)
                    AdventureDimIconToBitmap(
                        m_shadowIcon,
                        gWindowManager->m_screen,
                        cellPixelX,
                        cellPixelY + CELL_LAST_PIXEL,
                        heroFrame,
                        ICON_DRAW_OFFSET_FULL
                    );
                AdventureIconToBitmap(
                    m_heroIcons[heroIcon],
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                    heroFrame,
                    ICON_DRAW_OFFSET_FULL
                );
                if (playerColor != PLAYER_COLOR_NONE)
                    AdventureIconToBitmap(
                        m_flagIcons[playerColor],
                        gWindowManager->m_screen,
                        cellPixelX,
                        cellPixelY + CELL_PIXELS - 1 + heroYOffset,
                        heroFrame & HERO_FRAME_INDEX_MASK,
                        ICON_DRAW_OFFSET_FULL
                    );
            }
        }
        if (m_cursorActive && (drawnCell->m_flags & MAP_CELL_HERO_CURSOR) && !m_comboHeroDrawn
            && mapX == m_mapOriginX + ADVENTURE_VIEW_CENTER_X
            && mapY == m_mapOriginY + ADVENTURE_VIEW_CENTER_Y) {
            DrawCursor();
            m_comboHeroDrawn = true;
        }
    }
    if (drawMask & ADVMGR_DRAW_OVERLAY) {
        if (drawnCell->m_overlayIndex != MAP_CELL_NO_FRAME) {
            s_drawTileset =
                (drawnCell->m_overlayTileset & MAP_CELL_TILESET_MASK);
            AdventureIconToBitmap(
                m_objectIcons[s_drawTileset],
                gWindowManager->m_screen,
                cellPixelX,
                cellPixelY,
                drawnCell->m_overlayIndex,
                ICON_DRAW_OFFSET_FULL
            );
            if (drawnCell->m_flags & MAP_CELL_OVERLAY_ANIMATED)
                AdventureIconToBitmap(
                    m_objectIcons[s_drawTileset],
                    gWindowManager->m_screen,
                    cellPixelX,
                    cellPixelY,
                    drawnCell->m_overlayIndex + m_animationFrame + 1,
                    ICON_DRAW_OFFSET_FULL
                );
        }
        if (drawnCell->m_flags & MAP_CELL_OVERLAY_EXTRA)
            AdventureIconToBitmap(
                m_objectIcons[(drawnCell->m_overlayTileset >> MAP_CELL_EXTRA_TILESET_SHIFT)],
                gWindowManager->m_screen,
                cellPixelX,
                cellPixelY,
                drawnCell->m_extraFrame,
                ICON_DRAW_OFFSET_FULL
            );
    }
}

mapCell* advManager::GetCell(i16 x, i16 y) {
    if (x < 0 || y < 0 || x >= MAP_CELL_GRID_SIZE || y >= MAP_CELL_GRID_SIZE)
        return m_mapData[0];
    else
        return &m_mapData[x][y];
}

void advManager::UpdateRadar(b8 updateScreen, b32 partial) {
    i16 y;
    i32 minX;
    i32 maxY;
    i16 x;
    i16 color;
    i16 theOwner;
    i32 maxX;
    i32 minY;
    mapCell* cellPtrItem;

    if (!partial) {
        minX = 0;
        minY = 0;
        maxX = MAP_CELL_GRID_SIZE - 1;
        maxY = MAP_CELL_GRID_SIZE - 1;
    } else {
        minX = m_mapOriginX - 1;
        minY = m_mapOriginY - 1;
        maxX = m_mapOriginX + ADVENTURE_VIEWPORT_CELLS_X;
        maxY = m_mapOriginY + ADVENTURE_VIEWPORT_CELLS_Y;
        if (minX < 0)
            minX = 0;
        if (minY < 0)
            minY = 0;
        if (maxX > MAP_CELL_GRID_SIZE - 1)
            maxX = MAP_CELL_GRID_SIZE - 1;
        if (maxY > MAP_CELL_GRID_SIZE - 1)
            maxY = MAP_CELL_GRID_SIZE - 1;
    }

    if (!gThisNetHumanPlayer[gCurPlayer])
        return;

    gAdvManager->m_heroesLogoShown = false;
    for (x = minX; x <= maxX; x++) {
        for (y = minY; y <= maxY; y++) {
            if (!(gGame->m_mapExtra[x][y] & gCurPlayerBit)) {
                m_radarIcon->FillToBuffer(
                    x * RADAR_CELL_PIXELS + RADAR_LEFT,
                    y * RADAR_CELL_PIXELS + RADAR_TOP,
                    0,
                    RADAR_UNEXPLORED_COLOR,
                    ICON_DRAW_NORMAL,
                    ICON_DRAW_OFFSET_FULL
                );
                continue;
            }
            cellPtrItem = &m_mapData[x][y];
            if (MAP_TRIGGER_OBJECT(cellPtrItem->m_triggerType) == MAP_OBJECT_HERO) {
                theOwner = gGame->m_heroOwners[cellPtrItem->m_objectMetadata];
                if (theOwner == gCurPlayer)
                    color = gRadarOwnerColor
                        [theOwner < 0
                             ? PLAYER_COLOR_NEUTRAL
                             : (gGame->m_players[theOwner].m_color)];
                else
                    color = gRadarTerrainColor[CELL_TERRAIN(cellPtrItem)];
            } else {
                switch (
                    (cellPtrItem->m_objectTileset & MAP_CELL_TILESET_MASK)
                ) {
                    case TILESET_TOWN32:
                        theOwner = gGame->m_townOwners[cellPtrItem->m_objectMetadata];
                        color = gRadarOwnerColor
                            [theOwner < 0
                                 ? PLAYER_COLOR_NEUTRAL
                                 : (gGame->m_players[theOwner].m_color)];
                        break;
                    case TILESET_RSRC32:
                        switch (cellPtrItem->m_triggerType) {
                            case MAP_OBJECT_TRIGGER(MAP_OBJECT_ALCHEMIST_LAB):
                            case MAP_OBJECT_TRIGGER(MAP_OBJECT_MINE):
                            case MAP_OBJECT_TRIGGER(MAP_OBJECT_SAWMILL):
                            case MAP_EVENT_TRIGGER(MAP_OBJECT_ALCHEMIST_LAB):
                            case MAP_EVENT_TRIGGER(MAP_OBJECT_MINE):
                            case MAP_EVENT_TRIGGER(MAP_OBJECT_SAWMILL):
                                theOwner = gGame->m_mineOwners[cellPtrItem->m_objectMetadata];
                                color = gRadarOwnerColor
                                    [theOwner < 0
                                         ? PLAYER_COLOR_NEUTRAL
                                         : (gGame->m_players[theOwner].m_color)];
                                break;
                            default:
                                color = gRadarTerrainColor[CELL_TERRAIN(cellPtrItem)];
                                break;
                        }
                        break;
                    case TILESET_MTN32:
                    case TILESET_TREE32:
                        color = gRadarTerrainColor[CELL_TERRAIN(cellPtrItem)] + RADAR_TERRAIN_SHADE;
                        break;
                    default:
                        color = gRadarTerrainColor[CELL_TERRAIN(cellPtrItem)];
                        break;
                }
            }
            m_radarIcon->FillToBuffer(
                x * RADAR_CELL_PIXELS + RADAR_LEFT,
                y * RADAR_CELL_PIXELS + RADAR_TOP,
                0,
                color,
                ICON_DRAW_NORMAL,
                ICON_DRAW_OFFSET_FULL
            );
        }
    }
    const int viewLeft = RADAR_LEFT + m_mapOriginX * RADAR_CELL_PIXELS;
    const int viewTop = RADAR_TOP + m_mapOriginY * RADAR_CELL_PIXELS;
    const int viewRight = viewLeft + ADVENTURE_VIEWPORT_CELLS_X * RADAR_CELL_PIXELS - 1;
    const int viewBottom = viewTop + ADVENTURE_VIEWPORT_CELLS_Y * RADAR_CELL_PIXELS - 1;
    auto radarLine = [](int x, int y, int width, int height) {
        const int right = std::min(x + width, RADAR_RIGHT);
        const int bottom = std::min(y + height, static_cast<int>(RADAR_BOTTOM));
        x = std::max(x, RADAR_LEFT);
        y = std::max(y, static_cast<int>(RADAR_TOP));
        if (x < right && y < bottom)
            FillBitmapArea(gWindowManager->m_screen, x, y, right - x, bottom - y, RADAR_VIEWPORT_COLOR);
    };
    radarLine(viewLeft, viewTop, viewRight - viewLeft + 1, 1);
    radarLine(viewLeft, viewBottom, viewRight - viewLeft + 1, 1);
    radarLine(viewLeft, viewTop, 1, viewBottom - viewTop + 1);
    radarLine(viewRight, viewTop, 1, viewBottom - viewTop + 1);
    const i32 markerColor = goldButton::MatchGold(63, 54, 15, true);
    for (i32 i = 0; i < gCurPlayerData->m_heroCount; ++i) {
        const hero& subject = *gGame->GetHero(gCurPlayerData->m_heroIds[i]);
        if (!OwnHeroVisible(subject) || subject.m_x + 1 < minX || subject.m_x - 1 > maxX
            || subject.m_y + 1 < minY || subject.m_y - 1 > maxY)
            continue;
        const i32 x = RADAR_LEFT + subject.m_x * RADAR_CELL_PIXELS;
        const i32 y = RADAR_TOP + subject.m_y * RADAR_CELL_PIXELS;
        const i32 left = std::max(x - 1, RADAR_LEFT + minX * RADAR_CELL_PIXELS);
        const i32 top = std::max(y - 1, RADAR_TOP + minY * RADAR_CELL_PIXELS);
        FillBitmapArea(gWindowManager->m_screen, left, top,
                       std::min(x + 3, RADAR_LEFT + (maxX + 1) * RADAR_CELL_PIXELS) - left,
                       std::min(y + 3, RADAR_TOP + (maxY + 1) * RADAR_CELL_PIXELS) - top, 0);
        FillBitmapArea(gWindowManager->m_screen, x, y, RADAR_CELL_PIXELS, RADAR_CELL_PIXELS, markerColor);
    }
    if (updateScreen)
        gWindowManager->UpdateScreenRegion(
            minX * RADAR_CELL_PIXELS + RADAR_LEFT,
            minY * RADAR_CELL_PIXELS + RADAR_TOP,
            (maxX - minX + 1) * RADAR_CELL_PIXELS,
            (maxY - minY + 1) * RADAR_CELL_PIXELS
        );
}

i8 advManager::HeroAtRadarPoint(i16 x, i16 y) {
    if (x < RADAR_LEFT || x >= RADAR_RIGHT || y < RADAR_TOP || y >= RADAR_BOTTOM)
        return HERO_ID_NONE;

    i8 closest = HERO_ID_NONE;
    i32 bestDistance = kRadarHeroHitRadius * kRadarHeroHitRadius + 1;
    for (i32 i = 0; i < gCurPlayerData->m_heroCount; ++i) {
        const hero& subject = *gGame->GetHero(gCurPlayerData->m_heroIds[i]);
        if (!OwnHeroVisible(subject))
            continue;
        const i32 markerX = RADAR_LEFT + subject.m_x * RADAR_CELL_PIXELS;
        const i32 markerY = RADAR_TOP + subject.m_y * RADAR_CELL_PIXELS;
        const i32 dx = x - std::clamp<i32>(x, markerX, markerX + RADAR_CELL_PIXELS - 1);
        const i32 dy = y - std::clamp<i32>(y, markerY, markerY + RADAR_CELL_PIXELS - 1);
        const i32 distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            closest = subject.m_id;
        }
    }
    return closest;
}

i8 advManager::TownAtRadarPoint(i16 x, i16 y) {
    if (gAllBlack || x < RADAR_LEFT || x >= RADAR_RIGHT || y < RADAR_TOP || y >= RADAR_BOTTOM)
        return GAME_TOWN_NONE;
    const i32 mapX = (x - RADAR_LEFT) / RADAR_CELL_PIXELS;
    const i32 mapY = (y - RADAR_TOP) / RADAR_CELL_PIXELS;
    if (!(gGame->m_mapExtra[mapX][mapY] & gCurPlayerBit))
        return GAME_TOWN_NONE;
    const mapCell& cell = m_mapData[mapX][mapY];
    for (i32 index = 0; index < gCurPlayerData->m_townCount; ++index) {
        const i8 townId = gCurPlayerData->m_townIds[index];
        const town* subject = gGame->GetTown(townId);
        if (mapX == subject->m_x && mapY == subject->m_y)
            return townId;
        if ((cell.m_objectTileset & MAP_CELL_TILESET_MASK) == TILESET_TOWN32
            && cell.m_objectMetadata == townId && abs(mapX - subject->m_x) <= TOWN_FOOTPRINT_LEFT
            && mapY <= subject->m_y && mapY >= subject->m_y - TOWN_FOOTPRINT_TOP)
            return townId;
    }
    return GAME_TOWN_NONE;
}

// The name of an object type for the quick info. The table has 63 entries,
// but map cells carry types up to 127; the original read on into the tables
// the linker placed after it (the 36 town names, then the event texts), and
// so showed those. The same text is chosen here without leaving the tables.
static char* QuickInfoObjectName(i32 type) {
    if (type < 63)
        return gObjectNames[type];
    if (type < 63 + GAME_TOWN_COUNT)
        return gTownNames[type - 63];
    return gEventText[type - 63 - GAME_TOWN_COUNT];
}

// Large adventure objects may have several graphic cells and only
// one cell that runs the visit event.
static const mapCell& VisitEventCell(const mapCell& cell, int x, int y) {
    if ((cell.m_triggerType & MAP_TRIGGER_EVENT) || cell.m_triggerType == MAP_OBJECT_NONE)
        return cell;
    const int trigger = MAP_EVENT_TRIGGER(MAP_TRIGGER_OBJECT(cell.m_triggerType));
    const int tileset = cell.m_objectTileset & MAP_CELL_TILESET_MASK;
    for (int distance = 1; distance <= 3; ++distance) {
        for (int dy = -distance; dy <= distance; ++dy) {
            const int dx = distance - abs(dy);
            for (int side = -1; side <= 1; side += 2) {
                const int nearX = x + side * dx;
                const int nearY = y + dy;
                if (!MAP_CELL_IN_BOUNDS(nearX, nearY))
                    continue;
                const mapCell& neighbor = gGame->m_map[nearX][nearY];
                if (neighbor.m_triggerType == trigger
                    && (neighbor.m_objectTileset & MAP_CELL_TILESET_MASK) == tileset)
                    return neighbor;
            }
        }
    }
    return cell;
}

// Use the same state as each object's event handler, including when a
// temporary bonus expires or a weekly reward is replenished.
static std::optional<bool> ObjectVisitStatus(const mapCell& cell, const hero* selectedHero) {
    if (!(cell.m_triggerType & MAP_TRIGGER_EVENT))
        return std::nullopt;
    switch (MAP_TRIGGER_OBJECT(cell.m_triggerType)) {
        case MAP_OBJECT_WINDMILL:
            return cell.m_objectMetadata > WINDMILL_RESOURCE_LAST;
        case MAP_OBJECT_WATERWHEEL:
            return cell.m_objectMetadata == MAP_EVENT_DATA_EMPTY;
        case MAP_OBJECT_OBELISK:
            if (cell.m_objectMetadata > 0 && cell.m_objectMetadata <= sizeof(gGame->m_obeliskVisitors))
                return (gGame->m_obeliskVisitors[cell.m_objectMetadata - 1] & gCurPlayerBit) != 0;
            return std::nullopt;
        default:
            break;
    }
    if (!selectedHero)
        return std::nullopt;
    switch (MAP_TRIGGER_OBJECT(cell.m_triggerType)) {
        case MAP_OBJECT_BUOY:
            return (selectedHero->m_eventFlags & HERO_EVENT_BUOY) != 0;
        case MAP_OBJECT_FAERIE_RING:
            return (selectedHero->m_eventFlags & HERO_EVENT_FAERIE_RING) != 0;
        case MAP_OBJECT_FOUNTAIN:
            return (selectedHero->m_eventFlags & HERO_EVENT_FOUNTAIN) != 0;
        case MAP_OBJECT_OASIS:
            return (selectedHero->m_eventFlags & HERO_EVENT_OASIS) != 0;
        case MAP_OBJECT_STATUE:
            return (selectedHero->m_eventFlags & HERO_EVENT_STATUE) != 0;
        case MAP_OBJECT_GAZEBO:
            return (selectedHero->m_visitedSites & (1u << (cell.m_objectMetadata & 31))) != 0;
        case MAP_OBJECT_SPELL_SHRINE: {
            const int spell = cell.m_objectMetadata - MAP_EVENT_SPELL_OFFSET;
            if (spell < SPELL_FIRST || spell >= SPELL_COUNT)
                return std::nullopt;
            return std::find(std::begin(selectedHero->m_spells), std::end(selectedHero->m_spells), spell)
                != std::end(selectedHero->m_spells);
        }
        default:
            return std::nullopt;
    }
}

void advManager::QuickInfo(i16 cellX, i16 cellY) {
    mapCell* currentCell;
    i16 posX;
    heroWindow* infoWindow;
    i16 posY;
    char savedTextLocal[200];
    tag_message message;
    i16 quickInfoShowFlag;

    quickInfoShowFlag = 1;
    currentCell = NULL;
    posX = cellX * CELL_PIXELS - QUICK_INFO_X_OFFSET;
    if (posX < BORDER_EDGE_SIZE)
        posX = BORDER_EDGE_SIZE;
    if (posX + QUICK_INFO_WIDTH > BORDER_MIDDLE_END_X)
        posX = QUICK_INFO_RIGHT_X;
    posY = cellY * CELL_PIXELS - QUICK_INFO_Y_OFFSET;
    if (posY < BORDER_EDGE_SIZE)
        posY = BORDER_EDGE_SIZE;
    if (posY + QUICK_INFO_HEIGHT > BORDER_MIDDLE_END_Y)
        posY = QUICK_INFO_BOTTOM_Y;

    infoWindow = new heroWindow(posX, posY, "qwikinfo.bin");
    if (!infoWindow)
        MemError();

    if (m_mapOriginX + cellX < 0 || m_mapOriginX + cellX >= MAP_CELL_GRID_SIZE
        || m_mapOriginY + cellY < 0 || m_mapOriginY + cellY >= MAP_CELL_GRID_SIZE) {
        sprintf(gText, "\n\n%s", localization::Tr("adventure.quick_info.border"));
    } else {
        currentCell = GetCell(m_mapOriginX + cellX, m_mapOriginY + cellY);
        if (!(gGame->m_mapExtra[m_mapOriginX + cellX][m_mapOriginY + cellY] & gCurPlayerBit)) {
            sprintf(gText, "\n\n%s", localization::Tr("adventure.quick_info.uncharted"));
        } else {
            switch (MAP_TRIGGER_OBJECT(currentCell->m_triggerType)) {
                case MAP_OBJECT_ARTIFACT:
                    sprintf(gText, "\n\n%s", localization::Tr("adventure.quick_info.artifact"));
                    break;
                case MAP_OBJECT_NONE:
                case MAP_OBJECT_COAST:
                case MAP_OBJECT_SHADOW:
                    sprintf(gText, "\n\n%s", gTerrainNames[CELL_TERRAIN(currentCell)]);
                    break;
                case MAP_OBJECT_MINE:
                    sprintf(
                        gText,
                        "\n\n%s",
                        gMineNames[gGame->m_mines[currentCell->m_objectMetadata].type]
                    );
                    break;
                // A resource or monster cell whose picture is not one of
                // theirs (some shipped maps have such cells) indexed the name
                // tables far past their end; it gets the object type's name.
                case MAP_OBJECT_RESOURCE:
                    if (currentCell->m_objectIndex < RESOURCE_PILE_OBJECT_BASE
                        || currentCell->m_objectIndex >= RESOURCE_PILE_OBJECT_BASE + RESOURCE_COUNT) {
                        sprintf(gText, "\n\n%s", QuickInfoObjectName(MAP_OBJECT_RESOURCE));
                        break;
                    }
                    sprintf(
                        gText,
                        "\n\n%s",
                        gResourceNames[(currentCell->m_objectIndex - RESOURCE_PILE_OBJECT_BASE)]
                    );
                    break;
                case MAP_OBJECT_RESOURCE_SHADOW:
                    if (currentCell->m_objectIndex + 2 >= RESOURCE_COUNT) {
                        sprintf(gText, "\n\n%s", QuickInfoObjectName(MAP_OBJECT_RESOURCE));
                        break;
                    }
                    sprintf(gText, "\n\n%s", gResourceNames[(currentCell->m_objectIndex + 2)]);
                    break;
                case MAP_OBJECT_MONSTER:
                    if (currentCell->m_objectIndex >= CREATURE_COUNT) {
                        sprintf(gText, "\n\n%s", QuickInfoObjectName(MAP_OBJECT_MONSTER));
                        break;
                    }
                    sprintf(
                        gText,
                        "\n\n%s %s",
                        GetArmySizeName(
                            currentCell->m_objectMetadata & MONSTER_COUNT_MASK,
                            ARMY_SIZE_NAME_SENTENCE
                        ),
                        gArmyNamesPlural[currentCell->m_objectIndex]
                    );
                    break;
                default:
                    sprintf(
                        gText,
                        "\n\n%s",
                        QuickInfoObjectName(currentCell->m_triggerType & MAP_TRIGGER_TYPE_MASK)
                    );
                    break;
            }
            const int heroId = gCurPlayerData->CurrentHero();
            const hero* selectedHero = heroId == HERO_ID_NONE ? nullptr : gGame->GetHero(heroId);
            const mapCell& visitCell = VisitEventCell(*currentCell, m_mapOriginX + cellX, m_mapOriginY + cellY);
            if (const auto visited = ObjectVisitStatus(visitCell, selectedHero)) {
                const size_t used = strlen(gText);
                if (used < sizeof(savedTextLocal))
                    snprintf(gText + used, sizeof(savedTextLocal) - used, "\n%s",
                             *visited ? localization::Tr("adventure.visit.yes")
                                      : localization::Tr("adventure.visit.no"));
            }
        }
    }

    strcpy(savedTextLocal, gText);
    if (gDebugLevel > 0 && currentCell)
        sprintf(
            gText,
            "otile%d oi%d ot%d ei%d fl%d %s X%d Y%d",
            currentCell->m_objectTileset,
            currentCell->m_objectIndex,
            currentCell->m_triggerType,
            currentCell->m_objectMetadata,
            currentCell->m_flags & MAP_CELL_OBJECT_SHADOW_ONLY,
            savedTextLocal,
            m_mapOriginX + cellX,
            m_mapOriginY + cellY
        );
    SET_WIDGET_MESSAGE(message, WIDGET_COMMAND_SET_TEXT, 1);
    message.text = gText;
    infoWindow->BroadcastMessage(message);
    GrabScreen();
    gWindowManager->AddWindow(infoWindow, WINDOW_Z_ORDER_APPEND, true);
    gMouseManager->HideSystemCursor();
    QuickViewWait();
    gWindowManager->RemoveWindow(infoWindow);
    delete infoWindow;
    gMouseManager->ShowSystemCursor();
}

void advManager::UpdateHeroLocator(i32 locatorSlot, b8 drawWindow, b8 updateScreen) {
    i32 widgetBase;
    i8 whichHero;
    i32 curHero;
    tag_message message;
    i32 i;
    hero* heroPtr;
    i32 moveFrm;

    if (!gThisNetHumanPlayer[gCurPlayer])
        return;
    if (locatorSlot == LOCATOR_SLOT_CURRENT_HERO) {
        curHero = gCurPlayerData->CurrentHero();
        if (curHero == HERO_ID_NONE)
            return;
        for (i = 0; i < LocatorRows(); i++) {
            if (gCurPlayerData->m_heroLocatorPage + i < gCurPlayerData->m_heroCount
                && curHero == gCurPlayerData->m_heroIds[gCurPlayerData->m_heroLocatorPage + i])
                locatorSlot = i;
        }
        if (locatorSlot == LOCATOR_SLOT_CURRENT_HERO)
            return;
    }
    widgetBase = locatorSlot * HERO_LOCATOR_WIDGET_STRIDE + HERO_LOCATOR_WIDGET_BASE;
    message.type = MESSAGE_WIDGET;
    whichHero = gCurPlayerData->m_heroLocatorPage + locatorSlot < gCurPlayerData->m_heroCount
        ? gCurPlayerData->m_heroIds[gCurPlayerData->m_heroLocatorPage + locatorSlot] : HERO_ID_NONE;
    message.command = WIDGET_COMMAND_SET_COLOR;
    message.id = widgetBase + HERO_LOCATOR_HIGHLIGHT;
    message.value = (whichHero == gCurPlayerData->m_currentHero
                     && gCurPlayerData->m_currentHero != HERO_ID_NONE && !gAllBlack)
                        ? LOCATOR_HIGHLIGHT_COLOR
                        : 0;
    m_adventureWindow->BroadcastMessage(message);
    if (whichHero == HERO_ID_NONE || gAllBlack) {
        message.id = widgetBase + HERO_LOCATOR_BUTTON;
        message.command = WIDGET_COMMAND_SET_FRAME;
        message.value = locatorSlot % LOCATOR_VISIBLE_COUNT;
        m_adventureWindow->BroadcastMessage(message);
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.value = WIDGET_FLAG_DRAW;
        for (i = 0; i <= HERO_LOCATOR_BUTTON - 1; i++) {
            message.id = widgetBase + i;
            m_adventureWindow->BroadcastMessage(message);
        }
    } else {
        heroPtr = gGame->GetHero(whichHero);
        message.id = widgetBase + HERO_LOCATOR_BUTTON;
        message.command = WIDGET_COMMAND_SET_FRAME;
        message.value = LOCATOR_FRAME_HERO;
        m_adventureWindow->BroadcastMessage(message);
        message.command = WIDGET_COMMAND_SET_FLAGS;
        message.value = WIDGET_FLAG_ENABLED | WIDGET_FLAG_DRAW;
        for (i = 0; i <= HERO_LOCATOR_HIGHLIGHT; i++) {
            message.id = widgetBase + i;
            m_adventureWindow->BroadcastMessage(message);
        }
        moveFrm = gGame->IsMobile(whichHero)
                      ? std::clamp((heroPtr->GetMovementPoints() + 50) / 100, 0, 25)
                      : 0;
        message.id = widgetBase + HERO_LOCATOR_MOBILITY;
        message.command = WIDGET_COMMAND_SET_FRAME;
        message.value = moveFrm;
        m_adventureWindow->BroadcastMessage(message);
        message.id = widgetBase + HERO_LOCATOR_PORTRAIT;
        message.command = WIDGET_COMMAND_SET_FRAME;
        message.value = whichHero;
        m_adventureWindow->BroadcastMessage(message);
        message.id = widgetBase + 3;
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.value = WIDGET_FLAG_ENABLED | WIDGET_FLAG_DRAW;
        m_adventureWindow->BroadcastMessage(message);
        message.id = widgetBase + 4;
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.value = WIDGET_FLAG_ENABLED | WIDGET_FLAG_DRAW;
        m_adventureWindow->BroadcastMessage(message);
    }
    if (drawWindow) {
        m_adventureWindow->DrawWindow(0, widgetBase, widgetBase + HERO_LOCATOR_HIGHLIGHT);
        m_adventureWindow->DrawWindow(0, kLocatorStatsId + locatorSlot, kLocatorStatsId + locatorSlot);
        if (updateScreen)
            gWindowManager->UpdateScreenRegion(LOGICAL_SCREEN_WIDTH - 159, locatorSlot * 32 + 177, 54, 30);
    }
}

void advManager::UpdateHeroLocators(b8 drawWindow, b8 updateScreen) {
    i32 locatorSlot;

    if (!gThisNetHumanPlayer[gCurPlayer])
        return;

    const int lastPage = std::max(0, gCurPlayerData->m_heroCount - LocatorRows());
    auto& page = gCurPlayerData->m_heroLocatorPage;
    page = std::clamp<int>(page, 0, lastPage);
    for (locatorSlot = 0; locatorSlot < LocatorRows(); ++locatorSlot)
        UpdateHeroLocator(locatorSlot, false, false);

    const int span = LOCATOR_HERO_SCROLL_SPAN + LocatorExtension();
    m_heroScrollKnob->m_y = LOCATOR_SCROLL_BASE_Y + (lastPage ? page * span / lastPage : span / 2);
    if (drawWindow)
        DrawLocators(updateScreen);
}

void advManager::DrawLocators(bool updateScreen) {
    // Only redraw widgets belonging to the two lists, in their usual Z order.
    // The full adventure window also contains the map and status panel.
    tag_message draw{};
    SET_WIDGET_MESSAGE(draw, WIDGET_COMMAND_DRAW, WIDGET_ID_NONE);
    for (widget* item = m_adventureWindow->m_widgetListTail; item; item = item->m_prev) {
        // These backdrop widgets only upload pixels. Upload the lists once
        // after their portraits, highlights and scrollbar knobs are drawn.
        if (std::find(std::begin(m_panelBackdrops), std::end(m_panelBackdrops), item) != std::end(m_panelBackdrops))
            continue;
        if (item->m_id == kAdventureIndicators
            || (item->m_x >= LOGICAL_SCREEN_WIDTH - 160 && item->m_x + item->m_width <= LOGICAL_SCREEN_WIDTH - 16
                && item->m_y >= 176 && item->m_y + item->m_height <= 304 + LocatorExtension()))
            item->Main(draw);
    }
    if (updateScreen)
        gWindowManager->UpdateScreenRegion(LOGICAL_SCREEN_WIDTH - 160, kLocatorTop, 144, 128 + LocatorExtension());
}

void advManager::ResetLocatorInput() {
    m_lastLocatorControl = 0;
    m_lastLocatorObject = -1;
    m_locatorWheels[0] = {};
    m_locatorWheels[1] = {};
}

bool advManager::LocatorDoubleClick(i16 control, i16 object, const tag_message& message) {
    const bool modified = message.modifiers
        & (MESSAGE_MODIFIER_RIGHT_BUTTON | MESSAGE_MODIFIER_SHIFT_KEYS | MESSAGE_MODIFIER_ALT
           | MESSAGE_MODIFIER_CONTROL_KEYS);
    const bool open = !modified && control == m_lastLocatorControl && object == m_lastLocatorObject
                      && message.mouseTimestamp - m_lastLocatorClick <= kLocatorDoubleClickTime;
    m_lastLocatorControl = modified || open ? 0 : control;
    m_lastLocatorObject = modified || open ? -1 : object;
    m_lastLocatorClick = message.mouseTimestamp;
    return open;
}

void advManager::ScrollLocators(const tag_message& message) {
    if (gAllBlack)
        return;

    for (int column = 0; column < 2; ++column) {
        const auto& portraits = *m_panelBackdrops[column];
        const auto& scrollbar = *m_panelBackdrops[column + 2];
        if (message.x < portraits.m_x || message.x >= scrollbar.m_x + scrollbar.m_width
            || message.y < portraits.m_y || message.y >= portraits.m_y + portraits.m_height)
            continue;

        const bool town = column == 1;
        i8& page = town ? gCurPlayerData->m_townLocatorPage : gCurPlayerData->m_heroLocatorPage;
        const i32 count = town ? gCurPlayerData->m_townCount : gCurPlayerData->m_heroCount;
        LocatorWheel& wheel = m_locatorWheels[column];
        m_locatorWheels[1 - column] = {};
        if (message.mouseTimestamp - wheel.timestamp > kLocatorWheelGestureTime
            || (wheel.total > 0 && message.value < 0) || (wheel.total < 0 && message.value > 0))
            wheel = {};
        wheel.timestamp = message.mouseTimestamp;
        wheel.total += message.value;
        // Respond on the first fractional event, then advance once per full notch.
        const i32 steps = wheel.total > 0 ? (wheel.total + INPUT_WHEEL_STEP - 1) / INPUT_WHEEL_STEP
                                         : (wheel.total - INPUT_WHEEL_STEP + 1) / INPUT_WHEEL_STEP;
        const i32 lastPage = std::max(0, count - LocatorRows());
        const i32 nextPage = std::clamp(page - (steps - wheel.applied), 0, lastPage);
        wheel.applied = steps;
        if (nextPage == page)
            return;
        m_lastLocatorControl = 0;
        m_lastLocatorObject = -1;
        page = nextPage;
        if (town)
            UpdateTownLocators(true, true);
        else
            UpdateHeroLocators(true, true);
        return;
    }
}

void advManager::UpdateTownLocators(b8 drawWindow, b8 updateScreen) {
    i16 i;
    i8 whichTown;
    tag_message msg;

    if (!gThisNetHumanPlayer[gCurPlayer])
        return;
    const int lastPage = std::max(0, gCurPlayerData->m_townCount - LocatorRows());
    auto& page = gCurPlayerData->m_townLocatorPage;
    page = std::clamp<int>(page, 0, lastPage);
    msg.type = MESSAGE_WIDGET;
    for (i = 0; i < LocatorRows(); i++) {
        whichTown = page + i < gCurPlayerData->m_townCount ? gCurPlayerData->m_townIds[page + i] : GAME_TOWN_NONE;
        msg.command = WIDGET_COMMAND_SET_COLOR;
        msg.id = i + TOWN_LOCATOR_HIGHLIGHT_FIRST;
        msg.value = (gCurPlayerData->m_currentTown != GAME_TOWN_NONE
                     && whichTown == gCurPlayerData->m_currentTown && !gAllBlack)
                        ? LOCATOR_HIGHLIGHT_COLOR
                        : 0;
        m_adventureWindow->BroadcastMessage(msg);
        msg.id = TownLocatorButton(i);
        if (whichTown == GAME_TOWN_NONE || gAllBlack) {
            msg.command = WIDGET_COMMAND_SET_FRAME;
            msg.value = i % LOCATOR_VISIBLE_COUNT + LOCATOR_FRAME_EMPTY_TOWN_FIRST;
            m_adventureWindow->BroadcastMessage(msg);
            msg.command = WIDGET_COMMAND_CLEAR_FLAGS;
            msg.value = WIDGET_FLAG_ENABLED;
            m_adventureWindow->BroadcastMessage(msg);
        } else {
            msg.command = WIDGET_COMMAND_SET_FLAGS;
            msg.value = WIDGET_FLAG_ENABLED;
            m_adventureWindow->BroadcastMessage(msg);
            msg.command = WIDGET_COMMAND_SET_FRAME;
            msg.value = (gGame->GetTown(whichTown)->m_type)
                        + LOCATOR_FRAME_TOWN_FIRST;
            if (gGame->GetTown(whichTown)->m_buildings
                & (1 << BUILDING_SLOT_CASTLE))
                msg.value += LOCATOR_FRAME_CASTLE_OFFSET;
            m_adventureWindow->BroadcastMessage(msg);
        }
    }
    const int span = LOCATOR_TOWN_SCROLL_SPAN + LocatorExtension();
    m_townScrollKnob->m_y = LOCATOR_SCROLL_BASE_Y + (lastPage ? page * span / lastPage : span / 2);
    if (drawWindow)
        DrawLocators(updateScreen);
}

void advManager::UpdBottomView(b8 forceUpdate, b8 drawWindow, b8 updateScreen) {
    b8 updated;

    updated = false;
    gForceUpdate = forceUpdate;
    if (gBottomViewOverride == BOTTOM_VIEW_OVERRIDE_DISABLED)
        return;

    if (gBottomViewOverride > BOTTOM_VIEW_NONE) {
        if (KBTickCount() > gBottomViewOverrideEndTime) {
            gBottomViewOverride = BOTTOM_VIEW_NONE;
        } else {
            switch (gBottomViewOverride) {
                case BOTTOM_VIEW_NEW_TURN:
                    updated = UpdBottomViewNewTurn();
                    break;
                case BOTTOM_VIEW_KINGDOM:
                    updated = UpdBottomViewKingdom();
                    break;
                case BOTTOM_VIEW_RESOURCE:
                    updated = UpdBottomViewResMsg();
                    break;
            }
            goto update_bottom_view;
        }
    }

    if (!gThisNetHumanPlayer[gCurPlayer] || gAllBlack)
        updated = UpdBottomViewEnemyTurn();
    else if (gCurPlayerData->CurrentHero() == HERO_ID_NONE)
        updated = UpdBottomViewKingdom();
    else
        updated = UpdBottomViewHero();

update_bottom_view:
    if (updated && m_movementPreview) {
        m_adventureWindow->RemoveWidget(m_movementPreview);
        m_adventureWindow->AddWidget(m_movementPreview, WINDOW_Z_ORDER_APPEND);
    }
    if (updated && drawWindow) {
        m_adventureWindow
            ->DrawWindow(0, BOTTOM_VIEW_DRAW_FIRST_WIDGET, BOTTOM_VIEW_DRAW_LAST_WIDGET);
        if (updateScreen)
            gWindowManager->UpdateScreenRegion(
                BOTTOM_VIEW_PANEL_X,
                StatusY(),
                BOTTOM_VIEW_PANEL_WIDTH,
                BOTTOM_VIEW_PANEL_HEIGHT
            );
    }
    forceUpdate = gForceUpdate;
}

void advManager::ClearBottomView(void) {
    i32 widgetIndex;

    if (gCurBottomView == BOTTOM_VIEW_NONE)
        return;

    for (widgetIndex = 0; widgetIndex < ADVMGR_BOTTOM_VIEW_WIDGET_COUNT; ++widgetIndex) {
        if (m_bottomViewIconWidgets[widgetIndex] != NULL) {
            m_adventureWindow->RemoveWidget(m_bottomViewIconWidgets[widgetIndex]);
            delete m_bottomViewIconWidgets[widgetIndex];
        }
        if (m_bottomViewTextWidgets[widgetIndex] != NULL) {
            m_adventureWindow->RemoveWidget(m_bottomViewTextWidgets[widgetIndex]);
            delete m_bottomViewTextWidgets[widgetIndex];
        }
        m_bottomViewIconWidgets[widgetIndex] = NULL;
        m_bottomViewTextWidgets[widgetIndex] = NULL;
    }
    gCurBottomViewEnemy = BOTTOM_VIEW_NO_ENEMY;
    gCurBottomView = BOTTOM_VIEW_NONE;
    gLastAnimFrame = BOTTOM_VIEW_NO_ANIMATION;
}

b8 advManager::UpdBottomViewEnemyTurn(void) {
    static i32 gLastSandAnimTime = 0;
    static i32 gLastNewSandAnimTime = 0;
    b8 updated;
    tag_message msg;

    updated = false;
    msg.type = MESSAGE_WIDGET;
    if (gCurBottomView != BOTTOM_VIEW_ENEMY_TURN) {
        updated = true;
        gForceUpdate = true;
        ClearBottomView();
        gCurBottomView = BOTTOM_VIEW_ENEMY_TURN;

        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND] = new iconWidget(
            BOTTOM_VIEW_PANEL_X,
            StatusY(),
            BOTTOM_VIEW_PANEL_WIDTH,
            BOTTOM_VIEW_PANEL_HEIGHT,
            "stonback.icn",
            0,
            ICON_DRAW_NORMAL,
            BOTTOM_VIEW_BACKGROUND_ID,
            ICON_WIDGET_DRAW,
            1
        );
        if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND])
            MemError();
        m_adventureWindow->AddWidget(
            m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND],
            ENEMY_TURN_BACKGROUND_Z
        );

        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND] = new iconWidget(
            ENEMY_TURN_HOURGLASS_X,
            (ENEMY_TURN_HOURGLASS_Y + LocatorExtension()),
            ENEMY_TURN_HOURGLASS_WIDTH,
            ENEMY_TURN_HOURGLASS_HEIGHT,
            "hourglas.icn",
            0,
            ICON_DRAW_NORMAL,
            BOTTOM_VIEW_FOREGROUND_ID,
            ICON_WIDGET_DRAW,
            1
        );
        if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND])
            MemError();
        m_adventureWindow->AddWidget(
            m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND],
            ENEMY_TURN_HOURGLASS_Z
        );
    }

    if (gForceUpdate || KBTickCount() - gLastSandAnimTime > ENEMY_TURN_ANIMATION_DELAY) {
        gLastSandAnimTime = KBTickCount();
        gLastAnimFrame = m_animationFrame;
        if (KBTickCount() - gLastNewSandAnimTime > ENEMY_TURN_ANIMATION_DELAY) {
            gLastNewSandAnimTime = KBTickCount();
            gSandAnim++;
            if (gSandAnim >= ENEMY_TURN_SAND_FRAME_LIMIT)
                gSandAnim = ENEMY_TURN_SAND_RESTART_FRAME;
            updated = true;
            if (m_bottomViewIconWidgets[ENEMY_TURN_SAND_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST]) {
                msg.command = WIDGET_COMMAND_SET_FRAME;
                msg.id = ENEMY_TURN_SAND_ID;
                msg.value = gSandAnim + ENEMY_TURN_SAND_FRAME_OFFSET;
                m_adventureWindow->BroadcastMessage(msg);
            } else {
                m_bottomViewIconWidgets[ENEMY_TURN_SAND_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST] =
                    new iconWidget(
                        ENEMY_TURN_ANIMATION_X,
                        (ENEMY_TURN_ANIMATION_Y + LocatorExtension()),
                        ENEMY_TURN_ANIMATION_WIDTH,
                        ENEMY_TURN_ANIMATION_HEIGHT,
                        "hourglas.icn",
                        gSandAnim + ENEMY_TURN_SAND_FRAME_OFFSET,
                        ICON_DRAW_NORMAL,
                        ENEMY_TURN_SAND_ID,
                        ICON_WIDGET_DRAW,
                        1
                    );
                if (!m_bottomViewIconWidgets[ENEMY_TURN_SAND_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST])
                    MemError();
                m_adventureWindow->AddWidget(
                    m_bottomViewIconWidgets[ENEMY_TURN_SAND_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST],
                    ENEMY_TURN_SAND_Z
                );
            }
        }
    }

    if (gForceUpdate || gCurBottomViewEnemy != gCurPlayer) {
        updated = true;
        gCurBottomViewEnemy = gCurPlayer;
        if (gCurBottomViewEnemy != gCurPlayer)
            gCurHourGlassPhase = 0;
        if (m_bottomViewIconWidgets[ENEMY_TURN_CREST_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST]) {
            msg.command = WIDGET_COMMAND_SET_FRAME;
            msg.id = ENEMY_TURN_CREST_ID;
            msg.value = (gGame->GetPlayerColor(gCurPlayer));
            m_adventureWindow->BroadcastMessage(msg);
        } else {
            m_bottomViewIconWidgets[ENEMY_TURN_CREST_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST] =
                new iconWidget(
                    ENEMY_TURN_CREST_X,
                    (ENEMY_TURN_ANIMATION_Y + LocatorExtension()),
                    ENEMY_TURN_ANIMATION_WIDTH,
                    ENEMY_TURN_ANIMATION_HEIGHT,
                    "brcrest.icn",
                    (gGame->GetPlayerColor(gCurPlayer)),
                    ICON_DRAW_NORMAL,
                    ENEMY_TURN_CREST_ID,
                    ICON_WIDGET_DRAW,
                    1
                );
            if (!m_bottomViewIconWidgets[ENEMY_TURN_CREST_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST])
                MemError();
            m_adventureWindow->AddWidget(
                m_bottomViewIconWidgets[ENEMY_TURN_CREST_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST],
                ENEMY_TURN_CREST_Z
            );
        }
    }

    if (gForceUpdate || gCurHourGlassPhase < gLastHourGlassPhase || gLastHourGlassPhase < 0
        || (gCurHourGlassPhase > gLastHourGlassPhase
            && KBTickCount() - gLastHourGlassUpdateTime >= ENEMY_TURN_PHASE_DELAY)) {
        updated = true;
        gLastHourGlassPhase = gCurHourGlassPhase;
        gLastHourGlassUpdateTime = KBTickCount();
        if (m_bottomViewIconWidgets[ENEMY_TURN_PHASE_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST]) {
            msg.command = WIDGET_COMMAND_SET_FRAME;
            msg.id = ENEMY_TURN_PHASE_ID;
            msg.value = gCurHourGlassPhase + ENEMY_TURN_PHASE_FRAME_OFFSET;
            m_adventureWindow->BroadcastMessage(msg);
        } else {
            m_bottomViewIconWidgets[ENEMY_TURN_PHASE_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST] =
                new iconWidget(
                    ENEMY_TURN_ANIMATION_X,
                    (ENEMY_TURN_ANIMATION_Y + LocatorExtension()),
                    ENEMY_TURN_ANIMATION_WIDTH,
                    ENEMY_TURN_ANIMATION_HEIGHT,
                    "hourglas.icn",
                    gCurHourGlassPhase + ENEMY_TURN_PHASE_FRAME_OFFSET,
                    ICON_DRAW_NORMAL,
                    ENEMY_TURN_PHASE_ID,
                    ICON_WIDGET_DRAW,
                    1
                );
            if (!m_bottomViewIconWidgets[ENEMY_TURN_PHASE_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST])
                MemError();
            m_adventureWindow->AddWidget(
                m_bottomViewIconWidgets[ENEMY_TURN_PHASE_SLOT + ADVMGR_BOTTOM_VIEW_ICON_FIRST],
                ENEMY_TURN_PHASE_Z
            );
        }
    }
    return updated;
}

b8 advManager::UpdBottomViewNewTurn(void) {
    i32 frameIndex;
    i32 month;
    char* week;
    char* day;

    frameIndex = 0;
    if (!gForceUpdate && gCurBottomView == BOTTOM_VIEW_NEW_TURN)
        return false;

    ClearBottomView();
    gCurBottomView = BOTTOM_VIEW_NEW_TURN;
    if (gGame->m_day == 1 && (gGame->m_month != 1 || gGame->m_week != 1 || gGame->m_day != 1))
        frameIndex = gGame->m_week;

    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND] = new iconWidget(
        BOTTOM_VIEW_PANEL_X,
        StatusY(),
        BOTTOM_VIEW_BACKGROUND_WIDTH,
        BOTTOM_VIEW_PANEL_HEIGHT,
        "stonback.icn",
        0,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_BACKGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND] = new iconWidget(
        BOTTOM_VIEW_PANEL_X,
        StatusY(),
        BOTTOM_VIEW_PANEL_WIDTH,
        BOTTOM_VIEW_PANEL_HEIGHT,
        "sunmoon.icn",
        frameIndex,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_FOREGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    week = static_cast<char*>(malloc(BOTTOM_VIEW_TEXT_BUFFER_SIZE));
    sprintf(
        week,
        "%s: %d  %s: %d",
        localization::Tr("calendar.month.label"),
        gGame->m_month,
        localization::Tr("calendar.week.label"),
        gGame->m_week
    );
    m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT] = new textWidget(
        NEW_TURN_DATE_TEXT_X,
        (NEW_TURN_WEEK_TEXT_Y + LocatorExtension()),
        NEW_TURN_DATE_TEXT_WIDTH,
        NEW_TURN_WEEK_TEXT_HEIGHT,
        week,
        "smalfont.fnt",
        TEXT_WIDGET_PLAIN_COLOR,
        BOTTOM_VIEW_TEXT_ID,
        WIDGET_KIND_TEXT
    );
    if (!m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT],
        WINDOW_Z_ORDER_APPEND
    );

    day = static_cast<char*>(malloc(BOTTOM_VIEW_TEXT_BUFFER_SIZE));
    sprintf(day, "%s: %d", localization::Tr("calendar.day.label"), gGame->m_day);
    m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT] = new textWidget(
        NEW_TURN_DATE_TEXT_X,
        (NEW_TURN_DAY_TEXT_Y + LocatorExtension()),
        NEW_TURN_DATE_TEXT_WIDTH,
        NEW_TURN_DAY_TEXT_HEIGHT,
        day,
        "bigfont.fnt",
        TEXT_WIDGET_PLAIN_COLOR,
        BOTTOM_VIEW_TEXT_ID,
        WIDGET_KIND_TEXT
    );
    if (!m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT],
        WINDOW_Z_ORDER_APPEND
    );
    return true;
}

b8 advManager::UpdBottomViewResMsg(void) {
    font* smFont;
    char* countText;
    i32 iconHeight;
    i32 lineCntNo;
    i32 textY;
    i32 iconWidth;
    char* messageText;

    if (!gForceUpdate && gCurBottomView == BOTTOM_VIEW_RESOURCE)
        return false;

    ClearBottomView();
    gCurBottomView = BOTTOM_VIEW_RESOURCE;
    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND] = new iconWidget(
        BOTTOM_VIEW_PANEL_X,
        StatusY(),
        BOTTOM_VIEW_BACKGROUND_WIDTH,
        BOTTOM_VIEW_PANEL_HEIGHT,
        "stonback.icn",
        0,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_BACKGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    textY = 0;
    if (gBottomViewResource < RESOURCE_FIRST) {
        textY = RESOURCE_VIEW_MULTILINE_HEIGHT;
        smFont = gResourceManager->GetFont("smalfont.fnt");
        lineCntNo = smFont->LineLength(gBottomViewText, BOTTOM_VIEW_PANEL_WIDTH);
        gResourceManager->Dispose(smFont);
        textY -= lineCntNo * RESOURCE_VIEW_LINE_HEIGHT;
    }
    messageText = static_cast<char*>(malloc(strlen(gBottomViewText) + 1));
    snprintf(messageText, sizeof(messageText), "%s", gBottomViewText);
    m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT] = new textWidget(
        BOTTOM_VIEW_PANEL_X,
        textY + (RESOURCE_VIEW_TEXT_BASE_Y + LocatorExtension()),
        BOTTOM_VIEW_PANEL_WIDTH,
        RESOURCE_VIEW_TEXT_HEIGHT,
        messageText,
        "smalfont.fnt",
        TEXT_WIDGET_PLAIN_COLOR,
        BOTTOM_VIEW_TEXT_ID,
        WIDGET_KIND_TEXT
    );
    if (!m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT],
        WINDOW_Z_ORDER_APPEND
    );

    if (gBottomViewResource >= RESOURCE_FIRST) {
        if (gBottomViewResource == RESOURCE_GOLD) {
            iconWidth = RESOURCE_VIEW_GOLD_WIDTH;
            iconHeight = RESOURCE_VIEW_GOLD_HEIGHT;
        } else {
            iconWidth = RESOURCE_VIEW_ICON_WIDTH;
            iconHeight = RESOURCE_VIEW_ICON_HEIGHT;
        }
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND] = new iconWidget(
            (BOTTOM_VIEW_PANEL_WIDTH - iconWidth) / 2 + BOTTOM_VIEW_PANEL_X,
            (RESOURCE_VIEW_ICON_BOTTOM + LocatorExtension()) - iconHeight - RESOURCE_VIEW_ICON_BOTTOM_PADDING,
            iconWidth,
            iconHeight,
            "resource.icn",
            gBottomViewResource,
            ICON_DRAW_NORMAL,
            BOTTOM_VIEW_FOREGROUND_ID,
            ICON_WIDGET_DRAW,
            1
        );
        if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND])
            MemError();
        m_adventureWindow->AddWidget(
            m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND],
            WINDOW_Z_ORDER_APPEND
        );

        countText = static_cast<char*>(malloc(BOTTOM_VIEW_COUNT_BUFFER_SIZE));
        sprintf(countText, "%d", gBottomViewResourceQty);
        m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_COUNT_TEXT] = new textWidget(
            RESOURCE_VIEW_COUNT_X,
            (RESOURCE_VIEW_COUNT_Y + LocatorExtension()),
            RESOURCE_VIEW_COUNT_WIDTH,
            RESOURCE_VIEW_COUNT_HEIGHT,
            countText,
            "smalfont.fnt",
            TEXT_WIDGET_PLAIN_COLOR,
            BOTTOM_VIEW_TEXT_ID_2,
            WIDGET_KIND_TEXT
        );
        if (!m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_COUNT_TEXT])
            MemError();
        m_adventureWindow->AddWidget(
            m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_COUNT_TEXT],
            WINDOW_Z_ORDER_APPEND
        );
    }
    return true;
}

b8 advManager::UpdBottomViewKingdom(void) {
    i32 numVillage;
    i32 i;
    i32 nCastles;
    i8 rowY[KINGDOM_VIEW_ENTRY_COUNT];
    u8 textX[KINGDOM_VIEW_ENTRY_COUNT];
    char* countText[KINGDOM_VIEW_ENTRY_COUNT];

    if (!gForceUpdate && gCurBottomView == BOTTOM_VIEW_KINGDOM)
        return false;

    ClearBottomView();
    gCurBottomView = BOTTOM_VIEW_KINGDOM;
    rowY[RESOURCE_WOOD] = KINGDOM_VIEW_RESOURCE_TEXT_Y;
    rowY[RESOURCE_MERCURY] = KINGDOM_VIEW_RESOURCE_TEXT_Y;
    rowY[RESOURCE_ORE] = KINGDOM_VIEW_RESOURCE_TEXT_Y;
    rowY[RESOURCE_SULFUR] = KINGDOM_VIEW_RESOURCE_TEXT_Y;
    rowY[RESOURCE_CRYSTAL] = KINGDOM_VIEW_RESOURCE_TEXT_Y;
    rowY[RESOURCE_GEMS] = KINGDOM_VIEW_RESOURCE_TEXT_Y;
    rowY[RESOURCE_GOLD] = KINGDOM_VIEW_GOLD_TEXT_Y;
    rowY[KINGDOM_VIEW_CASTLE_ENTRY] = KINGDOM_VIEW_TOP_ROW_Y;
    rowY[KINGDOM_VIEW_VILLAGE_ENTRY] = KINGDOM_VIEW_TOP_ROW_Y;
    textX[RESOURCE_WOOD] = KINGDOM_VIEW_WOOD_TEXT_X;
    textX[RESOURCE_MERCURY] = KINGDOM_VIEW_MERCURY_TEXT_X;
    textX[RESOURCE_ORE] = KINGDOM_VIEW_ORE_TEXT_X;
    textX[RESOURCE_SULFUR] = KINGDOM_VIEW_SULFUR_TEXT_X;
    textX[RESOURCE_CRYSTAL] = KINGDOM_VIEW_CRYSTAL_TEXT_X;
    textX[RESOURCE_GEMS] = KINGDOM_VIEW_GEMS_TEXT_X;
    textX[RESOURCE_GOLD] = KINGDOM_VIEW_GOLD_TEXT_X;
    textX[KINGDOM_VIEW_CASTLE_ENTRY] = KINGDOM_VIEW_CASTLE_TEXT_X;
    textX[KINGDOM_VIEW_VILLAGE_ENTRY] = KINGDOM_VIEW_VILLAGE_TEXT_X;
    numVillage = 0;
    nCastles = 0;

    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND] = new iconWidget(
        BOTTOM_VIEW_PANEL_X,
        StatusY(),
        BOTTOM_VIEW_BACKGROUND_WIDTH,
        BOTTOM_VIEW_PANEL_HEIGHT,
        "stonback.icn",
        0,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_BACKGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND] = new iconWidget(
        KINGDOM_VIEW_ICON_X,
        (KINGDOM_VIEW_ICON_Y + LocatorExtension()),
        BOTTOM_VIEW_PANEL_WIDTH,
        BOTTOM_VIEW_PANEL_HEIGHT,
        "ressmall.icn",
        0,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_FOREGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    for (i = 0; i < gCurPlayerData->m_townCount; i++) {
        if (gGame->m_castleRecs[gCurPlayerData->m_townIds[i]].m_buildings
            & (1 << BUILDING_SLOT_CASTLE))
            nCastles++;
        else
            numVillage++;
    }

    for (i = 0; i < KINGDOM_VIEW_ENTRY_COUNT; i++) {
        countText[i] = static_cast<char*>(malloc(BOTTOM_VIEW_COUNT_BUFFER_SIZE));
        if (i < KINGDOM_VIEW_CASTLE_ENTRY)
            sprintf(
                countText[i],
                "%d",
                gCurPlayerData->m_resources[i]
            );
        else if (i == KINGDOM_VIEW_CASTLE_ENTRY)
            sprintf(countText[i], "%d", nCastles);
        else
            sprintf(countText[i], "%d", numVillage);
        m_bottomViewTextWidgets[i] = new textWidget(
            textX[i] + KINGDOM_VIEW_TEXT_X_BASE,
            rowY[i] + (KINGDOM_VIEW_TEXT_Y_BASE + LocatorExtension()),
            KINGDOM_VIEW_TEXT_WIDTH,
            KINGDOM_VIEW_TEXT_HEIGHT,
            countText[i],
            "smalfont.fnt",
            TEXT_WIDGET_PLAIN_COLOR,
            i + BOTTOM_VIEW_TEXT_ID,
            WIDGET_KIND_TEXT
        );
        if (!m_bottomViewTextWidgets[i])
            MemError();
        m_adventureWindow->AddWidget(m_bottomViewTextWidgets[i], WINDOW_Z_ORDER_APPEND);
    }
    return true;
}

b8 advManager::UpdBottomViewHero(void) {
    i8 creatureType;
    i16 slotNumPos;
    char* countStrData[ARMY_GROUP_SLOT_COUNT];
    i32 j;
    hero* curHero;
    i32 y;
    i32 iconDrawX;
    i16 nStacks;
    i16 crestFrame;
    char* heroNameCopy;
    i32 labelDrawX;

    if (!gForceUpdate && gCurBottomView == BOTTOM_VIEW_HERO)
        return false;

    ClearBottomView();
    gCurBottomView = BOTTOM_VIEW_HERO;
    curHero = gGame->GetHero(gCurPlayerData->CurrentHero());
    nStacks = 0;

    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND] = new iconWidget(
        BOTTOM_VIEW_PANEL_X,
        StatusY(),
        BOTTOM_VIEW_PANEL_WIDTH,
        BOTTOM_VIEW_PANEL_HEIGHT,
        "stonback.icn",
        0,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_BACKGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_BACKGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    crestFrame = (gCurPlayerData->Color()) * HERO_CLASS_COUNT
                 + curHero->m_heroClass;
    m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND] = new iconWidget(
        LOGICAL_SCREEN_WIDTH - 145,
        StatusY() + 3,
        25,
        25,
        "smcrest.icn",
        crestFrame,
        ICON_DRAW_NORMAL,
        BOTTOM_VIEW_FOREGROUND_ID,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewIconWidgets[ADVMGR_BOTTOM_VIEW_FOREGROUND],
        WINDOW_Z_ORDER_APPEND
    );

    heroNameCopy = static_cast<char*>(malloc(sizeof(curHero->m_shortName)));
    strcpy(heroNameCopy, curHero->m_shortName);
    heroNameCopy[sizeof(curHero->m_shortName) - 1] = 0;
    m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT] = new textWidget(
        BOTTOM_VIEW_PANEL_X - 5,
        StatusY() + 26,
        66,
        12,
        heroNameCopy,
        "smalfont.fnt",
        TEXT_WIDGET_PLAIN_COLOR,
        BOTTOM_VIEW_TEXT_ID,
        WIDGET_KIND_TEXT
    );
    if (!m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT])
        MemError();
    m_adventureWindow->AddWidget(
        m_bottomViewTextWidgets[ADVMGR_BOTTOM_VIEW_TEXT],
        WINDOW_Z_ORDER_APPEND
    );

    for (j = 0; j < ARMY_GROUP_SLOT_COUNT; j++) {
        if (curHero->m_army.m_creatureTypes[j] != CREATURE_NONE)
            nStacks++;
    }
    if (nStacks) {
        slotNumPos = 0;
        for (j = 0; j < ARMY_GROUP_SLOT_COUNT; j++) {
            creatureType = curHero->m_army.m_creatureTypes[j];
            if (creatureType != CREATURE_NONE) {
                countStrData[slotNumPos] = static_cast<char*>(malloc(BOTTOM_HERO_LABEL_BYTES));
                sprintf(countStrData[slotNumPos], "%d", curHero->m_army.m_creatureCounts[j]);
                y = slotNumPos <= 2 ? 38 : 3;
                if (slotNumPos == 0) {
                    iconDrawX = nStacks <= 2 ? 77 : 101;
                } else if (slotNumPos == 1) {
                    iconDrawX = nStacks == BOTTOM_HERO_TWO_STACKS ? 28 : 52;
                } else if (slotNumPos == BOTTOM_HERO_SLOT_THIRD) {
                    iconDrawX = 3;
                } else if (slotNumPos == BOTTOM_HERO_SLOT_FOURTH) {
                    iconDrawX = nStacks == BOTTOM_HERO_FOUR_STACKS ? 77 : 101;
                } else {
                    iconDrawX = 52;
                }
                m_bottomViewIconWidgets[slotNumPos + ADVMGR_BOTTOM_VIEW_ICON_FIRST] =
                    new iconWidget(
                        iconDrawX + BOTTOM_VIEW_PANEL_X,
                        y + StatusY(),
                        BOTTOM_HERO_ICON_WIDTH,
                        BOTTOM_HERO_ICON_HEIGHT,
                        "mons32.icn",
                        creatureType,
                        ICON_DRAW_NORMAL,
                        slotNumPos + BOTTOM_HERO_FIRST_ICON_ID,
                        ICON_WIDGET_DRAW,
                        1
                    );
                if (!m_bottomViewIconWidgets[slotNumPos + ADVMGR_BOTTOM_VIEW_ICON_FIRST])
                    MemError();
                if (gMons32Width[creatureType] < 28 && strlen(countStrData[slotNumPos]) <= 2)
                    labelDrawX = iconDrawX + 30;
                else
                    labelDrawX = iconDrawX + gMons32Width[creatureType] + 2;
                m_bottomViewTextWidgets[slotNumPos + ADVMGR_BOTTOM_VIEW_HERO_TEXT_FIRST] =
                    new textWidget(
                        labelDrawX + BOTTOM_VIEW_PANEL_X,
                        y + StatusY() + 22,
                        strlen(countStrData[slotNumPos]) * BOTTOM_HERO_CHARACTER_WIDTH,
                        BOTTOM_HERO_LABEL_HEIGHT,
                        countStrData[slotNumPos],
                        "smalfont.fnt",
                        TEXT_WIDGET_PLAIN_COLOR,
                        slotNumPos + BOTTOM_HERO_FIRST_TEXT_ID,
                        WIDGET_KIND_TEXT
                    );
                if (!m_bottomViewTextWidgets[slotNumPos + ADVMGR_BOTTOM_VIEW_HERO_TEXT_FIRST])
                    MemError();
                m_adventureWindow->AddWidget(
                    m_bottomViewIconWidgets[slotNumPos + ADVMGR_BOTTOM_VIEW_ICON_FIRST],
                    WINDOW_Z_ORDER_APPEND
                );
                m_adventureWindow->AddWidget(
                    m_bottomViewTextWidgets[slotNumPos + ADVMGR_BOTTOM_VIEW_HERO_TEXT_FIRST],
                    WINDOW_Z_ORDER_APPEND
                );
                slotNumPos++;
            }
        }
    }
    constexpr int statsSlot = ADVMGR_BOTTOM_VIEW_HERO_TEXT_FIRST + ARMY_GROUP_SLOT_COUNT;
    m_bottomViewTextWidgets[statsSlot] = new SelectedHeroStats();
    m_adventureWindow->AddWidget(m_bottomViewTextWidgets[statsSlot], WINDOW_Z_ORDER_APPEND);
    return true;
}

void advManager::HeroQuickView(i8 heroId, i8 locatorSlot, i16 windowX, i16 windowY) {
    i16 creatureCount;
    i16 armyWidth;
    i16 leftEdge;
    i16 creatureY;
    i16 iconWidth;
    i16 creatureIconHeight;
    textWidget* sizeWidgets[ARMY_GROUP_SLOT_COUNT];
    i16 enableFlag;
    i16 portId;
    i16 statId;
    i16 playerColorWidget;
    heroWindow* win;
    hero* targetHero;
    char* labels[ARMY_GROUP_SLOT_COUNT];
    i16 ii;
    i16 savedY;
    i16 oldX;
    iconWidget* stackIcons[ARMY_GROUP_SLOT_COUNT];
    tag_message msg;

    armyWidth = HERO_QUICK_ARMY_AREA_WIDTH;
    leftEdge = ARMY_QUICK_AREA_LEFT;
    creatureY = HERO_QUICK_DETAILED_CREATURE_Y;
    iconWidth = ARMY_QUICK_ICON_SIZE;
    creatureIconHeight = ARMY_QUICK_ICON_SIZE;
    enableFlag = 1;
    portId = QUICK_VIEW_PORTRAIT;
    statId = QUICK_VIEW_STAT_FIRST;
    playerColorWidget = QUICK_VIEW_FLAG;
    msg.type = MESSAGE_WIDGET;
    if (heroId == HERO_ID_NONE)
        return;
    targetHero = gGame->GetHero(heroId);
    if (targetHero->m_owner == gCurPlayer || m_identifyHeroActive == true) {
        if (windowX == QUICK_VIEW_AT_LOCATOR) {
            windowX = HERO_QUICK_DEFAULT_WINDOW_X + LOGICAL_SCREEN_WIDTH - 640;
            windowY = locatorSlot * HERO_QUICK_LOCATOR_ROW_HEIGHT + HERO_QUICK_LOCATOR_BASE_Y;
        }
        win = new heroWindow(windowX, windowY, "qhero0.bin");
        if (!win)
            MemError();
        SetWinText(win, WINDOW_TEXT_HERO_QUICK_VIEW);
    } else {
        win = new heroWindow(windowX, windowY, "qhero1.bin");
        if (!win)
            MemError();
    }

    msg.command = WIDGET_COMMAND_SET_FRAME;
    msg.id = QUICK_VIEW_PORTRAIT;
    msg.value = targetHero->m_id;
    win->BroadcastMessage(msg);
    msg.command = WIDGET_COMMAND_SET_FRAME;
    msg.id = QUICK_VIEW_FLAG;
    msg.value = (gGame->m_players[targetHero->m_owner].Color())
                * QUICK_VIEW_FLAG_COLOR_STRIDE;
    win->BroadcastMessage(msg);
    msg.id++;
    msg.value++;
    win->BroadcastMessage(msg);
    const bool showDetails = targetHero->m_owner == gCurPlayer || m_identifyHeroActive;
    if (showDetails) {
        snprintf(gText, 200, localization::Tr("hero.quick.name_with_level"),
                 targetHero->m_name, targetHero->GetLevel(targetHero->m_experience));
    } else {
        snprintf(gText, 200, "%s", targetHero->m_name);
    }
    msg.command = WIDGET_COMMAND_SET_TEXT;
    msg.id = QUICK_VIEW_NAME;
    msg.text = gText;
    win->BroadcastMessage(msg);
    if (targetHero->m_owner == gCurPlayer)
        targetHero->AddAdventureStats(*win, 42, 103, 96, false);

    creatureCount = 0;
    for (ii = 0; ii < ARMY_GROUP_SLOT_COUNT; ii++) {
        if (targetHero->m_army.m_creatureTypes[ii] != CREATURE_NONE)
            creatureCount++;
    }

    if (targetHero->m_owner == gCurPlayer || m_identifyHeroActive == true) {
        for (ii = 0; ii < HERO_PRIMARY_STAT_COUNT; ii++) {
            sprintf(gText, "%d", targetHero->m_primaryStats[ii]);
            msg.id = ii + QUICK_VIEW_STAT_FIRST;
            msg.text = gText;
            win->BroadcastMessage(msg);
        }
        if (creatureCount) {
            i16 armyStart;
            i16 idx;
            i8 monster;

            armyStart = (HERO_QUICK_ARMY_AREA_WIDTH - creatureCount * ARMY_QUICK_ICON_SIZE) / 2
                        + ARMY_QUICK_AREA_LEFT;
            idx = 0;
            for (ii = 0; ii < creatureCount; ii++) {
                while (targetHero->m_army.m_creatureTypes[idx] == CREATURE_NONE)
                    idx++;
                monster = targetHero->m_army.m_creatureTypes[idx];
                if (monster != CREATURE_NONE) {
                    stackIcons[ii] = new iconWidget(
                        armyStart + ii * ARMY_QUICK_ICON_SIZE,
                        HERO_QUICK_DETAILED_CREATURE_Y + (targetHero->m_owner == gCurPlayer ? 6 : 0),
                        ARMY_QUICK_ICON_SIZE,
                        ARMY_QUICK_ICON_SIZE,
                        "mons32.icn",
                        monster,
                        ICON_DRAW_NORMAL,
                        WIDGET_ID_NONE,
                        ICON_WIDGET_DRAW,
                        1
                    );
                    if (!stackIcons[ii])
                        MemError();
                    labels[ii] = static_cast<char*>(malloc(HERO_QUICK_ARMY_LABEL_CAPACITY));
                    sprintf(labels[ii], "%d", targetHero->m_army.m_creatureCounts[idx]);
                    sizeWidgets[ii] = new textWidget(
                        armyStart + ii * ARMY_QUICK_ICON_SIZE,
                        HERO_QUICK_DETAILED_LABEL_Y,
                        ARMY_QUICK_ICON_SIZE,
                        ARMY_QUICK_LABEL_HEIGHT,
                        labels[ii],
                        "smalfont.fnt",
                        TEXT_WIDGET_PLAIN_COLOR,
                        WIDGET_ID_NONE,
                        WIDGET_KIND_TEXT
                    );
                    if (!sizeWidgets[ii])
                        MemError();
                    win->AddWidget(stackIcons[ii], WINDOW_Z_ORDER_APPEND);
                    win->AddWidget(sizeWidgets[ii], WINDOW_Z_ORDER_APPEND);
                }
                idx++;
            }
        }
    } else if (creatureCount) {
        i16 rowY;
        i16 topRow;
        i16 bottomRowCount;
        i8 monster;
        i16 idx;
        i16 stride;
        i16 armyStart;

        rowY = HERO_QUICK_VAGUE_FIRST_ROW_Y;
        switch (creatureCount) {
            case ARMY_QUICK_ONE_STACK:
            case ARMY_QUICK_TWO_STACKS:
            case ARMY_QUICK_THREE_STACKS:
                rowY += ARMY_QUICK_FIRST_ROW_SHIFT;
                topRow = creatureCount;
                bottomRowCount = 0;
                break;
            case ARMY_QUICK_FOUR_STACKS:
                topRow = ARMY_QUICK_FIRST_ROW_COUNT;
                bottomRowCount = 2;
                break;
            default:
                topRow = ARMY_QUICK_FIRST_ROW_COUNT;
                bottomRowCount = 3;
                break;
        }
        idx = 0;
        stride = HERO_QUICK_ARMY_AREA_WIDTH / topRow;
        armyStart = (stride - ARMY_QUICK_ICON_SIZE) / 2 + ARMY_QUICK_AREA_LEFT;
        for (ii = 0; ii < topRow; ii++) {
            while (targetHero->m_army.m_creatureTypes[idx] == CREATURE_NONE)
                idx++;
            monster = targetHero->m_army.m_creatureTypes[idx];
            stackIcons[ii] = new iconWidget(
                armyStart + stride * ii,
                rowY,
                ARMY_QUICK_ICON_SIZE,
                ARMY_QUICK_ICON_SIZE,
                "mons32.icn",
                monster,
                ICON_DRAW_NORMAL,
                WIDGET_ID_NONE,
                ICON_WIDGET_DRAW,
                1
            );
            if (!stackIcons[ii])
                MemError();
            labels[ii] = static_cast<char*>(malloc(ARMY_QUICK_SIZE_LABEL_CAPACITY));
            strcpy(
                labels[ii],
                GetArmySizeName(targetHero->m_army.m_creatureCounts[idx], ARMY_SIZE_NAME_TITLE)
            );
            sizeWidgets[ii] = new textWidget(
                stride * ii + ARMY_QUICK_AREA_LEFT,
                rowY + ARMY_QUICK_ICON_BASELINE,
                stride,
                ARMY_QUICK_LABEL_HEIGHT,
                labels[ii],
                "smalfont.fnt",
                TEXT_WIDGET_PLAIN_COLOR,
                WIDGET_ID_NONE,
                WIDGET_KIND_TEXT
            );
            if (!sizeWidgets[ii])
                MemError();
            win->AddWidget(stackIcons[ii], WINDOW_Z_ORDER_APPEND);
            win->AddWidget(sizeWidgets[ii], WINDOW_Z_ORDER_APPEND);
            idx++;
        }
        if (bottomRowCount) {
            stride = HERO_QUICK_ARMY_AREA_WIDTH / bottomRowCount;
            armyStart = (stride - ARMY_QUICK_ICON_SIZE) / 2 + ARMY_QUICK_AREA_LEFT;
            rowY += ARMY_QUICK_SECOND_ROW_SHIFT;
            for (ii = topRow; ii < topRow + bottomRowCount; ii++) {
                while (targetHero->m_army.m_creatureTypes[idx] == CREATURE_NONE)
                    idx++;
                monster = targetHero->m_army.m_creatureTypes[idx];
                stackIcons[ii] = new iconWidget(
                    armyStart + stride * (ii - ARMY_QUICK_FIRST_ROW_COUNT),
                    rowY,
                    ARMY_QUICK_ICON_SIZE,
                    ARMY_QUICK_ICON_SIZE,
                    "mons32.icn",
                    monster,
                    ICON_DRAW_NORMAL,
                    WIDGET_ID_NONE,
                    ICON_WIDGET_DRAW,
                    1
                );
                if (!stackIcons[ii])
                    MemError();
                labels[ii] = static_cast<char*>(malloc(ARMY_QUICK_SIZE_LABEL_CAPACITY));
                strcpy(
                    labels[ii],
                    GetArmySizeName(targetHero->m_army.m_creatureCounts[idx], ARMY_SIZE_NAME_TITLE)
                );
                sizeWidgets[ii] = new textWidget(
                    stride * (ii - ARMY_QUICK_FIRST_ROW_COUNT) + ARMY_QUICK_AREA_LEFT,
                    rowY + ARMY_QUICK_ICON_BASELINE,
                    stride,
                    ARMY_QUICK_LABEL_HEIGHT,
                    labels[ii],
                    "smalfont.fnt",
                    TEXT_WIDGET_PLAIN_COLOR,
                    WIDGET_ID_NONE,
                    WIDGET_KIND_TEXT
                );
                if (!sizeWidgets[ii])
                    MemError();
                win->AddWidget(stackIcons[ii], WINDOW_Z_ORDER_APPEND);
                win->AddWidget(sizeWidgets[ii], WINDOW_Z_ORDER_APPEND);
                idx++;
            }
        }
    }

    oldX = m_mapOriginX;
    savedY = m_mapOriginY;
    m_mapOriginX = targetHero->m_x - ADVENTURE_VIEW_CENTER_X;
    m_mapOriginY = targetHero->m_y - ADVENTURE_VIEW_CENTER_Y;
    UpdateRadar(true, false);
    GrabScreen();
    gWindowManager->AddWindow(win, WINDOW_Z_ORDER_APPEND, true);
    gMouseManager->HideSystemCursor();
    QuickViewWait();
    gWindowManager->RemoveWindow(win);
    delete win;
    gMouseManager->ShowSystemCursor();
    m_mapOriginX = oldX;
    m_mapOriginY = savedY;
    UpdateRadar(true, false);
    CompleteDraw(false);
    UpdateScreen(false, false);
    if (msg.type == MESSAGE_LEFT_BUTTON_DOWN && targetHero->m_owner == gCurPlayer)
        SetHeroContext(targetHero->m_id, false);
}

char* advManager::GetArmySizeName(i16 armySize, i8 grammar) {
    if (gDebugLevel > 0) {
        sprintf(gArmySizeName, "%d", armySize);
        return gArmySizeName;
    }
    if (armySize < static_cast<i32>(ARMY_FEW_LIMIT))
        return gArmySizeNames[static_cast<i32>(ARMY_SIZE_FEW)][static_cast<i32>(grammar)];
    if (armySize < static_cast<i32>(ARMY_SEVERAL_LIMIT))
        return gArmySizeNames[static_cast<i32>(ARMY_SIZE_SEVERAL)][static_cast<i32>(grammar)];
    if (armySize < static_cast<i32>(ARMY_PACK_LIMIT))
        return gArmySizeNames[static_cast<i32>(ARMY_SIZE_PACK)][static_cast<i32>(grammar)];
    if (armySize < static_cast<i32>(ARMY_LOTS_LIMIT))
        return gArmySizeNames[static_cast<i32>(ARMY_SIZE_LOTS)][static_cast<i32>(grammar)];
    if (armySize < static_cast<i32>(ARMY_HORDE_LIMIT))
        return gArmySizeNames[static_cast<i32>(ARMY_SIZE_HORDE)][static_cast<i32>(grammar)];
    return gArmySizeNames[static_cast<i32>(ARMY_SIZE_ZOUNDS)][static_cast<i32>(grammar)];
}

void advManager::TownQuickView(i8 townId, i8 locatorSlot, i16 windowX, i16 windowY) {
    i16 creatureCount;
    i16 creatureIconHeight;
    i16 on;
    heroWindow* window;
    town* townPtr;
    i16 playerColorWidget;
    i16 oldX;
    i32 informationLevel;
    i16 oldY;
    i16 armyIndex;
    i16 leftEdge;
    i16 creatureIconWidth;
    i16 faceWidget;
    tag_message message;
    i16 armyAreaWidth;

    armyAreaWidth = TOWN_QUICK_ARMY_AREA_WIDTH;
    leftEdge = ARMY_QUICK_AREA_LEFT;
    creatureIconWidth = ARMY_QUICK_ICON_SIZE;
    creatureIconHeight = ARMY_QUICK_ICON_SIZE;
    on = 1;
    faceWidget = QUICK_VIEW_PORTRAIT;
    playerColorWidget = QUICK_VIEW_FLAG;
    if (townId == GAME_TOWN_NONE)
        return;
    townPtr = gGame->GetTown(townId);
    if (windowX == QUICK_VIEW_AT_LOCATOR) {
        windowX = TOWN_QUICK_DEFAULT_WINDOW_X + LOGICAL_SCREEN_WIDTH - 640;
        windowY = TOWN_QUICK_DEFAULT_WINDOW_Y;
    }
    window = new heroWindow(windowX, windowY, "qtown1.bin");
    if (!window)
        MemError();
    if (townPtr->m_owner == gCurPlayer) {
        informationLevel = TOWN_QUICK_INFORMATION_EXACT;
    } else {
        informationLevel = gGame->GetNumThievesGuilds(gCurPlayer);
        if (informationLevel > TOWN_QUICK_INFORMATION_THIEVES_LAST)
            informationLevel = TOWN_QUICK_INFORMATION_THIEVES_LAST;
    }
    SetWinText(window, WINDOW_TEXT_TOWN_QUICK_VIEW);

    creatureCount = 0;
    SET_WIDGET_MESSAGE(message, WIDGET_COMMAND_SET_FRAME, QUICK_VIEW_PORTRAIT);
    message.value = townPtr->m_type + TOWN_QUICK_TYPE_FRAME_BASE;
    if (gGame->GetTown(townId)->m_buildings & (1 << BUILDING_SLOT_CASTLE))
        message.value += TOWN_QUICK_CASTLE_FRAME_OFFSET;
    window->BroadcastMessage(message);
    if (townPtr->m_owner == GAME_PLAYER_NONE) {
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.id = QUICK_VIEW_FLAG;
        message.value = WIDGET_FLAG_DRAW;
        window->BroadcastMessage(message);
        message.id++;
        window->BroadcastMessage(message);
    } else {
        message.command = WIDGET_COMMAND_SET_FRAME;
        message.id = QUICK_VIEW_FLAG;
        message.value = (gGame->m_players[townPtr->m_owner].Color())
                        * QUICK_VIEW_FLAG_COLOR_STRIDE;
        window->BroadcastMessage(message);
        message.id++;
        message.value++;
        window->BroadcastMessage(message);
    }
    sprintf(gText, "%s", GetTownName(townPtr->m_id));
    message.command = WIDGET_COMMAND_SET_TEXT;
    message.id = QUICK_VIEW_NAME;
    message.text = gText;
    window->BroadcastMessage(message);

    creatureCount = 0;
    for (armyIndex = 0; armyIndex < ARMY_GROUP_SLOT_COUNT; armyIndex++) {
        if (townPtr->m_army.m_creatureTypes[armyIndex] != CREATURE_NONE)
            creatureCount++;
    }

    if (!informationLevel || !creatureCount) {
        char* garrisonStr;
        textWidget* garrisonWidget;

        garrisonStr = static_cast<char*>(malloc(TOWN_QUICK_EMPTY_LABEL_CAPACITY));
        if (!informationLevel)
            sprintf(garrisonStr, localization::Tr("adventure.quick.unknown"));
        else
            sprintf(garrisonStr, localization::Tr("adventure.quick.none"));
        garrisonWidget = new textWidget(
            TOWN_QUICK_EMPTY_LABEL_X,
            TOWN_QUICK_EMPTY_LABEL_Y,
            TOWN_QUICK_EMPTY_LABEL_WIDTH,
            ARMY_QUICK_LABEL_HEIGHT,
            garrisonStr,
            "smalfont.fnt",
            TEXT_WIDGET_PLAIN_COLOR,
            WIDGET_ID_NONE,
            WIDGET_KIND_TEXT
        );
        if (!garrisonWidget)
            MemError();
        window->AddWidget(garrisonWidget, WINDOW_Z_ORDER_APPEND);
    } else {
        i16 wIndex;
        textWidget* sizeWidgets[ARMY_GROUP_SLOT_COUNT];
        i8 monster;
        iconWidget* stackIcons[ARMY_GROUP_SLOT_COUNT];
        i16 bottomRowCount;
        i16 stride;
        b8 armySlot;
        char* troopNames[ARMY_GROUP_SLOT_COUNT];
        i32 fiveShift;
        i16 basePos;
        i16 curY;
        i8 creatureSlot;
        i16 topRow;

        curY = TOWN_QUICK_FIRST_ROW_Y;
        switch (creatureCount) {
            case ARMY_QUICK_ONE_STACK:
            case ARMY_QUICK_TWO_STACKS:
            case ARMY_QUICK_THREE_STACKS:
                curY += ARMY_QUICK_FIRST_ROW_SHIFT;
                topRow = creatureCount;
                bottomRowCount = 0;
                break;
            case ARMY_QUICK_FOUR_STACKS:
                topRow = ARMY_QUICK_FIRST_ROW_COUNT;
                bottomRowCount = 2;
                break;
            default:
                topRow = ARMY_QUICK_FIRST_ROW_COUNT;
                bottomRowCount = 3;
                break;
        }
        armySlot = false;
        wIndex = 0;
        creatureSlot = 0;
        stride = TOWN_QUICK_ARMY_AREA_WIDTH / topRow;
        basePos = (stride - ARMY_QUICK_ICON_SIZE) / 2 + ARMY_QUICK_AREA_LEFT;
        fiveShift = 0;
        for (armyIndex = 0; armyIndex < topRow; armyIndex++) {
            if (creatureCount == ARMY_GROUP_SLOT_COUNT) {
                fiveShift =
                    armyIndex == 0 ? ARMY_QUICK_FIVE_STACK_X_SHIFT : -ARMY_QUICK_FIVE_STACK_X_SHIFT;
            }
            while (townPtr->m_army.m_creatureTypes[creatureSlot] == CREATURE_NONE)
                creatureSlot++;
            monster = townPtr->m_army.m_creatureTypes[creatureSlot];
            stackIcons[wIndex] = new iconWidget(
                basePos + stride * wIndex + fiveShift,
                curY,
                ARMY_QUICK_ICON_SIZE,
                ARMY_QUICK_ICON_SIZE,
                "mons32.icn",
                monster,
                ICON_DRAW_NORMAL,
                WIDGET_ID_NONE,
                ICON_WIDGET_DRAW,
                1
            );
            if (!stackIcons[wIndex])
                MemError();
            troopNames[wIndex] = static_cast<char*>(malloc(ARMY_QUICK_SIZE_LABEL_CAPACITY));
            if (informationLevel == TOWN_QUICK_INFORMATION_EXACT)
                sprintf(troopNames[wIndex], "%d", townPtr->m_army.m_creatureCounts[creatureSlot]);
            else if (informationLevel == TOWN_QUICK_INFORMATION_ESTIMATES)
                strcpy(
                    troopNames[wIndex],
                    GetArmySizeName(
                        townPtr->m_army.m_creatureCounts[creatureSlot],
                        ARMY_SIZE_NAME_TITLE
                    )
                );
            else
                strcpy(troopNames[wIndex], "???");
            sizeWidgets[wIndex] = new textWidget(
                basePos + stride * wIndex + fiveShift - ARMY_QUICK_TEXT_X_ADJUSTMENT,
                curY + ARMY_QUICK_ICON_BASELINE,
                ARMY_QUICK_TEXT_WIDTH,
                ARMY_QUICK_LABEL_HEIGHT,
                troopNames[wIndex],
                "smalfont.fnt",
                TEXT_WIDGET_PLAIN_COLOR,
                WIDGET_ID_NONE,
                WIDGET_KIND_TEXT
            );
            if (!sizeWidgets[wIndex])
                MemError();
            window->AddWidget(stackIcons[wIndex], WINDOW_Z_ORDER_APPEND);
            window->AddWidget(sizeWidgets[wIndex], WINDOW_Z_ORDER_APPEND);
            wIndex++;
            creatureSlot++;
        }
        if (bottomRowCount) {
            stride = TOWN_QUICK_ARMY_AREA_WIDTH / bottomRowCount;
            basePos = (stride - ARMY_QUICK_ICON_SIZE) / 2 + ARMY_QUICK_AREA_LEFT;
            curY += ARMY_QUICK_SECOND_ROW_SHIFT;
            for (armyIndex = topRow; armyIndex < topRow + bottomRowCount; armyIndex++) {
                while (townPtr->m_army.m_creatureTypes[creatureSlot] == CREATURE_NONE)
                    creatureSlot++;
                monster = townPtr->m_army.m_creatureTypes[creatureSlot];
                stackIcons[wIndex] = new iconWidget(
                    basePos + stride * (wIndex - topRow),
                    curY,
                    ARMY_QUICK_ICON_SIZE,
                    ARMY_QUICK_ICON_SIZE,
                    "mons32.icn",
                    monster,
                    ICON_DRAW_NORMAL,
                    WIDGET_ID_NONE,
                    ICON_WIDGET_DRAW,
                    1
                );
                if (!stackIcons[wIndex])
                    MemError();
                troopNames[wIndex] = static_cast<char*>(malloc(ARMY_QUICK_SIZE_LABEL_CAPACITY));
                if (informationLevel == TOWN_QUICK_INFORMATION_EXACT)
                    sprintf(
                        troopNames[wIndex],
                        "%d",
                        townPtr->m_army.m_creatureCounts[creatureSlot]
                    );
                else if (informationLevel == TOWN_QUICK_INFORMATION_ESTIMATES)
                    strcpy(
                        troopNames[wIndex],
                        GetArmySizeName(
                            townPtr->m_army.m_creatureCounts[creatureSlot],
                            ARMY_SIZE_NAME_TITLE
                        )
                    );
                else
                    strcpy(troopNames[wIndex], "???");
                sizeWidgets[wIndex] = new textWidget(
                    basePos + stride * (wIndex - topRow) - ARMY_QUICK_TEXT_X_ADJUSTMENT,
                    curY + ARMY_QUICK_ICON_BASELINE,
                    ARMY_QUICK_TEXT_WIDTH,
                    ARMY_QUICK_LABEL_HEIGHT,
                    troopNames[wIndex],
                    "smalfont.fnt",
                    TEXT_WIDGET_PLAIN_COLOR,
                    WIDGET_ID_NONE,
                    WIDGET_KIND_TEXT
                );
                if (!sizeWidgets[wIndex])
                    MemError();
                window->AddWidget(stackIcons[wIndex], WINDOW_Z_ORDER_APPEND);
                window->AddWidget(sizeWidgets[wIndex], WINDOW_Z_ORDER_APPEND);
                wIndex++;
                creatureSlot++;
            }
        }
    }

    GrabScreen();
    gWindowManager->AddWindow(window, WINDOW_Z_ORDER_APPEND, true);
    oldX = m_mapOriginX;
    oldY = m_mapOriginY;
    m_mapOriginX = townPtr->m_x - ADVENTURE_VIEW_CENTER_X;
    m_mapOriginY = townPtr->m_y - ADVENTURE_VIEW_CENTER_Y;
    UpdateRadar(true, false);
    gMouseManager->HideSystemCursor();
    QuickViewWait();
    gWindowManager->RemoveWindow(window);
    delete window;
    gMouseManager->ShowSystemCursor();
    m_mapOriginX = oldX;
    m_mapOriginY = oldY;
    UpdateRadar(true, false);
    CompleteDraw(false);
    UpdateScreen(false, false);
    if (message.type == MESSAGE_LEFT_BUTTON_DOWN && townPtr->m_owner == gCurPlayer)
        SetTownContext(townPtr->m_id);
}

void advManager::RedrawAdvScreen(b32 update) {
    if (!gShowIt)
        return;
    bitmap legacyBorder(BITMAP_TYPE_MEMORY, 640, 480);
    gResourceManager->GetBackdrop("bord.bmp", &legacyBorder);
    bitmap* screen = gWindowManager->m_screen;
    FillBitmapArea(screen, 0, 0, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT, 0);
    for (int y = 0; y < LOGICAL_SCREEN_HEIGHT; ++y) {
        const int sourceY = y < 16 ? y : y >= LOGICAL_SCREEN_HEIGHT - 16
            ? 464 + y - (LOGICAL_SCREEN_HEIGHT - 16) : 16 + (y - 16) % 448;
        const int extension = LocatorExtension();
        const int bottom = LOGICAL_SCREEN_HEIGHT - 16;
        int sidebarY = y;
        if (y >= bottom)
            sidebarY = 464 + y - bottom;
        else if (y >= 464 + extension)
            sidebarY = 400 + (y - 464 - extension) % 48;
        else if (y >= 280 + extension)
            sidebarY = y - extension;
        else if (y >= 280)
            sidebarY = 208 + (y - 280) % kLocatorRowHeight;
        legacyBorder.CopyTo(screen, ADVENTURE_VIEWPORT_WIDTH, y, 480, sidebarY, 160, 1);
        legacyBorder.CopyTo(screen, 0, y, 0, sourceY, 16, 1);
        legacyBorder.CopyTo(screen, BORDER_MIDDLE_END_X, y, 464, sourceY, 16, 1);
        if (y < 16 || y >= LOGICAL_SCREEN_HEIGHT - 16) {
            for (int x = 16; x < BORDER_MIDDLE_END_X; x += 448)
                legacyBorder.CopyTo(screen, x, y, 16, sourceY,
                                    std::min(448, BORDER_MIDDLE_END_X - x), 1);
        }
    }
    if (LocatorExtension() != 0) {
        // Extend the stone surface beneath the original status art to the frame.
        auto* stone = gResourceManager->GetIcon("stonback.icn");
        stone->DrawToBuffer(BOTTOM_VIEW_PANEL_X, StatusY(), 0, ICON_DRAW_NORMAL, ICON_DRAW_OFFSET_FULL);
        gResourceManager->Dispose(stone);
        const int top = StatusY() + BOTTOM_VIEW_PANEL_HEIGHT;
        screen->CopyTo(screen, BOTTOM_VIEW_PANEL_X, top, BOTTOM_VIEW_PANEL_X, StatusY(),
                       BOTTOM_VIEW_PANEL_WIDTH, LOGICAL_SCREEN_HEIGHT - 16 - top);
    }
    SaveAdventureBorder();
    UpdateHeroLocators(false, false);
    UpdateTownLocators(false, false);
    UpdBottomView(true, false, false);
    m_adventureWindow->DrawWindow(0);
    if (update)
        gWindowManager->UpdateScreenRegion(0, 0, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    UpdateRadar(update, false);
    CompleteDraw(m_mapOriginX, m_mapOriginY, false);
    if (update)
        UpdateScreen(false, false);
}

void advManager::DeactivateCurrTown(void) {
    gCurPlayerData->m_currentTown = GAME_TOWN_NONE;
}

void advManager::DeactivateCurrHero(void) {
    DemobilizeCurrHero();
    gCurPlayerData->m_currentHero = HERO_ID_NONE;
}

void advManager::MobilizeCurrHero(b32 update) {
    if (gCurPlayerData->m_currentHero == HERO_ID_NONE)
        return;
    if (m_heroMobilized && m_cursorActive)
        return;
    m_heroMobilized = false;
    SetHeroContext(gCurPlayerData->m_currentHero, update);
}

void advManager::DemobilizeCurrHero(bool redraw) {
    if (gCurPlayerData->m_currentHero == HERO_ID_NONE)
        return;
    if (!m_heroMobilized)
        return;

    m_heroMobilized = false;
    hero* heroPointer = gGame->GetHero(gCurPlayerData->m_currentHero);
    StopCursor(true);
    heroPointer->m_x = m_mapOriginX + m_cursorMapX;
    heroPointer->m_y = m_mapOriginY + m_cursorMapY;
    mapCell* cell = GetCell(heroPointer->m_x, heroPointer->m_y);
    heroPointer->m_locationType = cell->m_triggerType;
    heroPointer->m_locationMetadata = cell->m_objectMetadata;
    heroPointer->m_direction = m_cursorDirection;
    if (m_cursorType == ADVMGR_HERO_ICON_BOAT)
        heroPointer->m_eventFlags |= HERO_EVENT_EMBARKED;
    cell->m_triggerType = MAP_EVENT_TRIGGER(MAP_OBJECT_HERO);
    cell->m_objectMetadata = heroPointer->m_id;
    cell->m_flags &= ~MAP_CELL_HERO_CURSOR;
    m_cursorActive = false;
    if (redraw) {
        CompleteDraw(m_mapOriginX, m_mapOriginY, false);
        UpdateScreen(false, false);
    }
}

void advManager::SetTownContext(i8 townId) {
    i16 index;
    i8 townNo;
    i8 wasVisible;
    town* townPointer;

    platform::BeginDisplayUpdate();
    DeactivateCurrHero();
    wasVisible = gMouseManager->IsVis();
    gMouseManager->ReallyHidePointer();
    gCurPlayerData->m_currentTown = townId;
    townPointer = gGame->GetTown(gCurPlayerData->m_currentTown);
    m_cameraOffsetX = m_cameraOffsetY = 0;
    m_mapOriginX = townPointer->m_x - ADVENTURE_VIEW_CENTER_X;
    m_mapOriginY = townPointer->m_y - ADVENTURE_VIEW_CENTER_Y;
    townNo = 0;
    for (index = 0; index < gCurPlayerData->m_townCount; index++) {
        if (gCurPlayerData->m_townIds[index] == townId)
            townNo = index;
    }
    if (townNo < gCurPlayerData->m_townLocatorPage)
        gCurPlayerData->m_townLocatorPage = townNo;
    else if (townNo > gCurPlayerData->m_townLocatorPage + (LocatorRows() - 1))
        gCurPlayerData->m_townLocatorPage = townNo - (LocatorRows() - 1);
    UpdateHeroLocators(false, false);
    UpdateTownLocators(false, false);
    DrawLocators(true);
    HideRoute(false, false, true);
    UpdBottomView(true, true, true);
    UpdateRadar(true, false);
    CompleteDraw(m_mapOriginX, m_mapOriginY, false);
    UpdateScreen(false, false);
    SetEnvironmentOrigin(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y, 1);
    townNo = (CELL_TERRAIN(GetCell(townPointer->m_x, townPointer->m_y)));
    if (townNo != m_currentTerrain) {
        m_currentTerrain = townNo;
        PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
    }
    if (wasVisible)
        gMouseManager->ReallyShowPointer();
    gInputManager->m_forceMouseMove = true;
    m_hoverCellX = 0;
    platform::EndDisplayUpdate();
}

void advManager::SetHeroContext(i8 heroId, b8 update) {
    i8 wasVisible;
    i8 curHeroSlot;
    i16 index;
    mapCell* currCell;
    hero* heroPtr;

    if (heroId == HERO_ID_NONE)
        return;
    platform::BeginDisplayUpdate();
    wasVisible = gMouseManager->IsVis();
    gMouseManager->ReallyHidePointer();
    DeactivateCurrTown();
    HideRoute(false, false, true);
    DeactivateCurrHero();
    m_heroMobilized = true;
    gCurPlayerData->m_currentHero = heroId;
    heroPtr = gGame->GetHero(gCurPlayerData->m_currentHero);
    m_cameraOffsetX = m_cameraOffsetY = 0;
    m_mapOriginX = heroPtr->m_x - ADVENTURE_VIEW_CENTER_X;
    m_mapOriginY = heroPtr->m_y - ADVENTURE_VIEW_CENTER_Y;
    m_cursorMapX = ADVENTURE_VIEW_CENTER_X;
    m_cursorMapY = ADVENTURE_VIEW_CENTER_Y;
    m_previousCursorMapX = m_previousCursorMapY = CURSOR_CELL_NONE;
    m_cursorType = heroPtr->IsEmbarked()
                       ? static_cast<i8>(ADVMGR_HERO_ICON_BOAT)
                       : heroPtr->m_heroClass;
    m_cursorDirection = heroPtr->m_direction;
    m_cursorFrame = GetCursorBaseFrame(m_cursorDirection);
    currCell = GetCell(heroPtr->m_x, heroPtr->m_y);
    currCell->m_flags |= MAP_CELL_HERO_CURSOR;
    gGame->RestoreCell(
        heroPtr->m_x,
        heroPtr->m_y,
        heroPtr->m_locationType,
        heroPtr->m_locationMetadata,
        NULL,
        4
    );
    curHeroSlot = 0;
    for (index = 0; index < gCurPlayerData->m_heroCount; index++) {
        if (gCurPlayerData->m_heroIds[index] == heroId)
            curHeroSlot = index;
    }
    if (curHeroSlot < gCurPlayerData->m_heroLocatorPage)
        gCurPlayerData->m_heroLocatorPage = curHeroSlot;
    else if (curHeroSlot > gCurPlayerData->m_heroLocatorPage + (LocatorRows() - 1))
        gCurPlayerData->m_heroLocatorPage = curHeroSlot - (LocatorRows() - 1);
    UpdateHeroLocators(false, false);
    UpdateTownLocators(false, false);
    DrawLocators(true);
    if (!update && (m_active == 1 || gThisNetHumanPlayer[gCurPlayer])) {
        Reseed(0, 0);
        SeedTo(heroPtr->m_destinationX, heroPtr->m_destinationY);
        ShowRoute(false, 0, !update);
    }
    UpdBottomView(true, true, true);
    m_cursorActive = true;
    UpdateRadar(true, false);
    CompleteDraw(m_mapOriginX, m_mapOriginY, false);
    UpdateScreen(false, false);
    SetEnvironmentOrigin(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y, 1);
    curHeroSlot = (CELL_TERRAIN(currCell));
    if (curHeroSlot != m_currentTerrain) {
        m_currentTerrain = curHeroSlot;
        PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
    }
    if (!gHeroMoving) {
        if (wasVisible)
            gMouseManager->ReallyShowPointer();
        gInputManager->m_forceMouseMove = true;
        m_hoverCellX = 0;
    }
    platform::EndDisplayUpdate();
}

void advManager::DragLocatorKnob(bool town, i16 pressY) {
    iconWidget& knob = *(town ? m_townScrollKnob : m_heroScrollKnob);
    i8& page = town ? gCurPlayerData->m_townLocatorPage : gCurPlayerData->m_heroLocatorPage;
    const int count = town ? gCurPlayerData->m_townCount : gCurPlayerData->m_heroCount;
    const int lastPage = std::max(0, count - LocatorRows());
    const int span = (town ? LOCATOR_TOWN_SCROLL_SPAN : LOCATOR_HERO_SCROLL_SPAN) + LocatorExtension();
    const auto update = [this, town]() {
        if (town)
            UpdateTownLocators(true, true);
        else
            UpdateHeroLocators(true, true);
    };
    if (lastPage == 0) {
        knob.m_flags &= ~WIDGET_FLAG_SELECTED;
        return;
    }

    const int grabOffset = pressY - knob.m_y;
    gMouseManager->SetCursorShape(4);
    while (gForegroundApp) {
        Process1WindowsMessage();
        const tag_message input = gInputManager->GetEvent();
        if (IS_BUTTON_RELEASE_MESSAGE(input.type))
            break;
        if (input.type == MESSAGE_MOUSE_MOVE) {
            const int position = std::clamp(input.y - grabOffset - LOCATOR_SCROLL_BASE_Y, 0, span);
            const int nextPage = (position * lastPage + span / 2) / span;
            if (nextPage != page) {
                page = nextPage;
                update();
            }
        } else if (input.type == MESSAGE_MOUSE_WHEEL) {
            ScrollLocators(input);
        } else if (input.type != MESSAGE_NONE) {
            gInputManager->PutBackEvent(input);
            break;
        }
    }
    gMouseManager->SetCursorShape(6);
    knob.m_flags &= ~WIDGET_FLAG_SELECTED;
    update();
}

static bool PuzzleExitRequested() {
    for (tag_message input = gInputManager->GetEvent(); input.type; input = gInputManager->GetEvent()) {
        if (input.type == MESSAGE_KEY_DOWN
            && (input.keyCode == INPUT_SCAN_ESCAPE || input.keyCode == INPUT_SCAN_ENTER))
            return true;
        if (input.type == MESSAGE_LEFT_BUTTON_UP && gWindowManager->m_windowListTail) {
            const heroWindow& window = *gWindowManager->m_windowListTail;
            for (const widget* item = window.m_widgetListHead; item; item = item->m_next) {
                if (item->m_id == DIALOG_BUTTON_0
                    && WIDGET_CONTAINS_LOCAL_POINT(*item, input.x - window.m_posX, input.y - window.m_posY))
                    return true;
            }
        }
    }
    return false;
}

void advManager::ViewPuzzle(const u8* previouslyRevealed) {
    LegacyCanvas canvas;
    i32 puzzleX;
    i32 puzzleY;
    icon* puzzleIcn;
    heroWindow* puzzleWindow;
    i16 j;

    const u8* revealed = gCurPlayerData->m_puzzlePiecesRemoved;
    const bool animate = previouslyRevealed && memcmp(previouslyRevealed, revealed,
                                                      PLAYER_PUZZLE_PIECE_STORAGE_SIZE) != 0;
    PlayMusic(MUSIC_TRACK_PUZZLE);
    gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
    puzzleIcn = gResourceManager->GetIcon("puzzle.icn");
    puzzleWindow = new heroWindow(RADAR_LEFT, RADAR_TOP, "viewpuzl.bin");
    if (!puzzleWindow)
        MemError();
    gWindowManager->AddWindow(puzzleWindow, WINDOW_Z_ORDER_APPEND, true);

    if (gCurPlayerData->CountPuzzlePiecesRemoved() > 0) {
        puzzleX = gGame->m_ultimateArtifactX - 7;
        puzzleY = gGame->m_ultimateArtifactY - 7;
        i32 xOff = 0;
        i32 yOff = 0;
        xOff = (gGame->m_ultimateArtifactX + gGame->m_ultimateArtifactY) % PUZZLE_ALIGNMENT_DIVISOR - 1;
        yOff = (gGame->m_ultimateArtifactX * PUZZLE_Y_ADJUST_X_FACTOR
                + gGame->m_ultimateArtifactY * PUZZLE_Y_ADJUST_Y_FACTOR)
                   % PUZZLE_ALIGNMENT_DIVISOR
               - 1;
        if ((gGame->m_ultimateArtifactX + gGame->m_ultimateArtifactY) % PUZZLE_ALIGNMENT_DIVISOR == 1) {
            if (xOff > 0)
                xOff++;
            else if (xOff < 0)
                xOff--;
        } else if ((gGame->m_ultimateArtifactX + gGame->m_ultimateArtifactY) % PUZZLE_PARITY_DIVISOR
                   == 1) {
            if (yOff > 0)
                yOff++;
            else if (yOff < 0)
                yOff--;
        }
        puzzleX += xOff;
        puzzleY += yOff;
        PuzzleDraw(puzzleX, puzzleY, gGame->m_ultimateArtifactX, gGame->m_ultimateArtifactY);

    }

    for (j = 0; j < PUZZLE_PIECE_COUNT; ++j) {
        if (!BitTest(animate ? previouslyRevealed : revealed, j))
            puzzleIcn->DrawToBuffer(0, 0, j, ICON_DRAW_NORMAL, ICON_DRAW_OFFSET_FULL);
    }
    constexpr int size = 448;
    gWindowManager->UpdateScreenRegion(BORDER_EDGE_SIZE, BORDER_EDGE_SIZE, size, size);
    bool closed = false;
    if (animate) {
        gWindowManager->SaveFizzleSource(BORDER_EDGE_SIZE, BORDER_EDGE_SIZE, size, size);
        PuzzleDraw(puzzleX, puzzleY, gGame->m_ultimateArtifactX, gGame->m_ultimateArtifactY);
        for (j = 0; j < PUZZLE_PIECE_COUNT; ++j) {
            if (!BitTest(revealed, j))
                puzzleIcn->DrawToBuffer(0, 0, j, ICON_DRAW_NORMAL, ICON_DRAW_OFFSET_FULL);
        }
        closed = gWindowManager->FizzleForward(BORDER_EDGE_SIZE, BORDER_EDGE_SIZE, size, size,
                                               PUZZLE_FIZZLE_TIME, PuzzleExitRequested);
    }
    gResourceManager->Dispose(puzzleIcn);
    if (closed) {
        gWindowManager->RemoveWindow(puzzleWindow);
    } else {
        puzzleWindow->SetConfirmButton(DIALOG_BUTTON_0, true);
        gWindowManager->DoDialog(puzzleWindow, EventWindowHandler, false);
    }
    delete puzzleWindow;
    canvas.Restore();
    CompleteDraw(m_mapOriginX, m_mapOriginY, false);
    UpdateScreen(false, false);
    UpdateRadar(true, false);
    PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
}

void advManager::PuzzleDraw(i32 left, i32 top, i32 markX, i32 markY) {
    i32 y;
    mapCell* cell;
    i32 x;
    u8 tileset;
    i16 screenX;
    i16 rowPixelY;

    for (y = 0; y < 15; y++) {
        for (x = 0; x < 15; x++) {
            DrawCell(left + x, top + y, x, y, ADVMGR_DRAW_GROUND, true, false);
            screenX = x * CELL_PIXELS;
            rowPixelY = y * CELL_PIXELS;
            cell = GetCell(left + x, top + y);
            if (!(cell->m_flags & MAP_CELL_OBJECT_SHADOW_ONLY)
                && cell->m_objectIndex != MAP_CELL_NO_FRAME) {
                tileset = (cell->m_objectTileset & MAP_CELL_TILESET_MASK);
                switch (tileset) {
                    case TILESET_OBJ32_04:
                    case TILESET_MTN32:
                    case TILESET_TREE32:
                        IconToBitmap(
                            m_objectIcons[tileset],
                            gWindowManager->m_screen,
                            screenX,
                            rowPixelY,
                            cell->m_objectIndex,
                            ICON_DRAW_OFFSET_FULL
                        );
                        break;
                    default:
                        break;
                }
            }
            if (cell->m_overlayIndex != MAP_CELL_NO_FRAME) {
                tileset =
                    (cell->m_overlayTileset & MAP_CELL_TILESET_MASK);
                switch (tileset) {
                    case TILESET_OBJ32_04:
                    case TILESET_MTN32:
                    case TILESET_TREE32:
                        IconToBitmap(
                            m_objectIcons[tileset],
                            gWindowManager->m_screen,
                            screenX,
                            rowPixelY,
                            cell->m_overlayIndex,
                            ICON_DRAW_OFFSET_FULL
                        );
                        break;
                    default:
                        break;
                }
            }
            if (left + x == markX && top + y == markY)
                IconToBitmap(
                    m_objectIcons[TILESET_ROUTE],
                    gWindowManager->m_screen,
                    screenX,
                    rowPixelY + ROUTE_DRAW_Y_OFFSET,
                    ROUTE_CELL_DESTINATION - 1,
                    ICON_DRAW_OFFSET_FULL
                );
        }
    }
    DrawAdventureBorder();
}

void advManager::CastSpell(i8 spell) {
    hero* caster;
    i32 guardianCount;

    if (gCurPlayerData->CurrentHero() != HERO_ID_NONE)
        caster = gGame->GetHero(gCurPlayerData->m_currentHero);
    else
        caster = NULL;

    switch (spell) {
        case SPELL_VIEW_MINES:
        case SPELL_VIEW_RESOURCES:
        case SPELL_VIEW_ARTIFACTS:
        case SPELL_VIEW_TOWNS:
        case SPELL_VIEW_HEROES:
        case SPELL_VIEW_ALL:
            ViewWorld(spell, true, spell == SPELL_VIEW_ALL);
            break;
        case SPELL_IDENTIFY_HERO:
            m_identifyHeroActive = true;
            NormalDialog(
                localization::Tr("adventure.spell.identify_hero"),
                NORMAL_DIALOG_TYPE_OK,
                0x61,
                0x91
            );
            break;
        case SPELL_SUMMON_BOAT:
            SummonBoat();
            break;
        case SPELL_DIMENSION_DOOR:
        case SPELL_TOWN_GATE:
            if (caster->m_remainingMobility == 0) {
                NormalDialog(localization::Tr("adventure.spell.too_tired"), NORMAL_DIALOG_TYPE_OK);
                return;
            }
            if (caster->m_remainingMobility < SPELL_TRAVEL_MOBILITY_COST)
                caster->m_remainingMobility = 0;
            else
                caster->m_remainingMobility -= SPELL_TRAVEL_MOBILITY_COST;
            UpdateHeroLocator(LOCATOR_SLOT_CURRENT_HERO, true, true);
            if (spell == SPELL_DIMENSION_DOOR)
                DimensionDoor();
            else
                TownGate();
            break;
        default:
            break;
    }

    if (spell != SPELL_DIMENSION_DOOR && spell != SPELL_TOWN_GATE)
        gGame->GetHero(gCurPlayerData->m_currentHero)->UseSpell(spell);
}

namespace {
constexpr int kWorldSize = 448;
constexpr int kWorldZoomMin = 1;
constexpr int kWorldZoomMax = 4;
int gWorldZoom = kWorldZoomMin;

struct WorldMapView {
    bitmap& image;
    int left = 0;
    int top = 0;
    int pressX = -1;
    int pressY = -1;
    int startLeft = 0;
    int startTop = 0;
    int wheel = 0;

    static bool Contains(int x, int y) {
        return x >= BORDER_EDGE_SIZE && y >= BORDER_EDGE_SIZE
            && x < BORDER_EDGE_SIZE + kWorldSize && y < BORDER_EDGE_SIZE + kWorldSize;
    }

    void Draw() {
        const int last = kWorldSize - (kWorldSize + gWorldZoom - 1) / gWorldZoom;
        left = std::clamp(left, 0, last);
        top = std::clamp(top, 0, last);
        auto& screen = *gWindowManager->m_screen;
        gMouseManager->ReallyHidePointer();
        for (int y = 0; y < kWorldSize; ++y) {
            u8* out = screen.m_pixels + (y + BORDER_EDGE_SIZE) * screen.m_width + BORDER_EDGE_SIZE;
            const u8* source = image.m_pixels + (top + y / gWorldZoom) * image.m_width + left;
            for (int x = 0; x < kWorldSize; ++x)
                out[x] = source[x / gWorldZoom];
        }
        gWindowManager->UpdateScreenRegion(BORDER_EDGE_SIZE, BORDER_EDGE_SIZE, kWorldSize, kWorldSize);
        gMouseManager->ReallyShowPointer();
    }

    void Zoom(int value, int x, int y) {
        value = std::clamp(value, kWorldZoomMin, kWorldZoomMax);
        if (value == gWorldZoom)
            return;
        x -= BORDER_EDGE_SIZE;
        y -= BORDER_EDGE_SIZE;
        left += x / gWorldZoom - x / value;
        top += y / gWorldZoom - y / value;
        gWorldZoom = value;
        pressX = -1;
        Draw();
    }
};

WorldMapView* gWorldMapView = nullptr;
constexpr i16 kWorldZoomOut = 0x79a0;
constexpr i16 kWorldZoomIn = 0x79a1;

i16 WorldMapHandler(tag_message& message) {
    auto& view = *gWorldMapView;
    constexpr int center = BORDER_EDGE_SIZE + kWorldSize / 2;
    if (message.type == MESSAGE_MOUSE_WHEEL && view.Contains(message.x, message.y)) {
        view.wheel += message.value;
        view.Zoom(gWorldZoom + view.wheel / INPUT_WHEEL_STEP, message.x, message.y);
        view.wheel %= INPUT_WHEEL_STEP;
    } else if (message.type == MESSAGE_LEFT_BUTTON_DOWN && view.Contains(message.x, message.y)) {
        view.pressX = message.x;
        view.pressY = message.y;
        view.startLeft = view.left;
        view.startTop = view.top;
    } else if (message.type == MESSAGE_LEFT_BUTTON_UP) {
        view.pressX = -1;
    } else if (message.type == MESSAGE_MOUSE_MOVE && view.pressX >= 0) {
        view.left = view.startLeft + (view.pressX - message.x) / gWorldZoom;
        view.top = view.startTop + (view.pressY - message.y) / gWorldZoom;
        view.Draw();
    } else if (message.type == MESSAGE_WIDGET && message.command == WIDGET_NOTIFY_DESELECT
               && (message.id == kWorldZoomOut || message.id == kWorldZoomIn)) {
        view.Zoom(gWorldZoom + (message.id == kWorldZoomOut ? -1 : 1), center, center);
    } else {
        return TrueFalseDialogHandler(message);
    }
    return MESSAGE_DISPATCH_CONSUME;
}
}

void advManager::ViewWorld(
    i8 spellType,
    b8 drawAllObjects,
    b8 drawAllTerrains
) {
    LegacyCanvas canvas;
    icon* flags;
    hero* currHero;
    i8 tileset;
    i8 flip;
    icon* lettersIcons;
    i16 index;
    heroWindow* win;
    u16 mask;
    i16 x;
    i16 owner;
    mapCell* cell;
    icon* mapTilesets[VIEW_WORLD_TILESET_COUNT];
    i16 i;
    i16 y;
    i16 screenX;
    icon* spheres;
    i16 rowPixelY;
    icon* ground;

    gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
    mask = (1 << TILESET_MTN32) | (1 << TILESET_TREE32);
    if (spellType == SPELL_VIEW_TOWNS || spellType == SPELL_VIEW_ALL)
        mask |= (1 << TILESET_TOWN32);
    ground = gResourceManager->GetIcon("ground6.icn");
    flags = gResourceManager->GetIcon("flag6.icn");
    spheres = gResourceManager->GetIcon("spheres.icn");
    lettersIcons = gResourceManager->GetIcon("letters.icn");
    currHero = NULL;
    for (i = 0; i < VIEW_WORLD_TILESET_COUNT; i++)
        mapTilesets[i] = NULL;
    mapTilesets[TILESET_TREE32] = gResourceManager->GetIcon("tree6.icn");
    mapTilesets[TILESET_MTN32] = gResourceManager->GetIcon("mtn6.icn");
    mapTilesets[TILESET_TOWN32] = gResourceManager->GetIcon("town6.icn");
    if (gCurPlayerData->CurrentHero() != HERO_ID_NONE)
        currHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    FillBitmapArea(
        gWindowManager->m_screen,
        BORDER_EDGE_SIZE,
        BORDER_EDGE_SIZE,
        448,
        448,
        0
    );

    for (y = 0; y < MAP_CELL_GRID_SIZE; y++) {
        for (x = MAP_CELL_GRID_SIZE - 1; x >= 0; x--) {
            cell = GetCell(x, y);
            if ((gGame->m_mapExtra[x][y] & gCurPlayerBit) || drawAllTerrains
                || (spellType == SPELL_VIEW_TOWNS
                    && MAP_TRIGGER_OBJECT(cell->m_triggerType) == MAP_OBJECT_TOWN)) {
                flip = ICON_DRAW_NORMAL;
                screenX = x * VIEW_WORLD_CELL_PIXELS + VIEW_WORLD_ORIGIN;
                rowPixelY = y * VIEW_WORLD_CELL_PIXELS + VIEW_WORLD_ORIGIN;
                index = cell->m_tileIndex / (1 << VIEW_WORLD_GROUND_TILE_SHIFT);
                if (cell->m_flags & MAP_CELL_GROUND_FLIP_HORIZONTAL)
                    flip = ICON_DRAW_FLIPPED;
                if (cell->m_flags & MAP_CELL_GROUND_FLIP_VERTICAL)
                    index += VIEW_WORLD_GROUND_FLIPPED_FRAMES;
                ground->DrawToBuffer(
                    screenX + (flip == ICON_DRAW_FLIPPED ? VIEW_WORLD_CELL_PIXELS - 1 : 0),
                    rowPixelY,
                    index,
                    flip,
                    ICON_DRAW_OFFSET_FULL
                );
                if (cell->m_objectIndex != MAP_CELL_NO_FRAME) {
                    tileset = (cell->m_objectTileset & MAP_CELL_TILESET_MASK);
                    // The small trees have fewer frames than the map's
                    // trees (98 against 101); the original drew the last
                    // three from a frame entry past the table.
                    if (((1 << tileset) & mask)
                        && cell->m_objectIndex < mapTilesets[tileset]->m_frameCount)
                        mapTilesets[tileset]->DrawToBuffer(
                            screenX,
                            rowPixelY,
                            cell->m_objectIndex,
                            ICON_DRAW_NORMAL,
                            ICON_DRAW_OFFSET_FULL
                        );
                }
            }
        }
        for (x = MAP_CELL_GRID_SIZE - 1; x >= 0; x--) {
            cell = GetCell(x, y);
            screenX = x * VIEW_WORLD_CELL_PIXELS + VIEW_WORLD_ORIGIN;
            rowPixelY = y * VIEW_WORLD_CELL_PIXELS + VIEW_WORLD_ORIGIN;
            if ((drawAllObjects || (gGame->m_mapExtra[x][y] & gCurPlayerBit))
                && (cell->m_triggerType & MAP_TRIGGER_EVENT)) {
                switch (spellType) {
                    case SPELL_VIEW_ALL:
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_ARTIFACT))
                            flags->DrawToBuffer(
                                screenX,
                                rowPixelY,
                                VIEW_WORLD_FLAG_ARTIFACT,
                                ICON_DRAW_NORMAL,
                                ICON_DRAW_OFFSET_FULL
                            );
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_TOWN)) {
                            owner = gGame->m_townOwners[cell->m_objectMetadata];
                            if (owner >= 0) {
                                index =
                                    (gGame->m_players[owner].m_color);
                                flags->DrawToBuffer(
                                    screenX - VIEW_WORLD_TOWN_FLAG_LEFT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_FLIPPED,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                flags->DrawToBuffer(
                                    screenX + VIEW_WORLD_TOWN_FLAG_RIGHT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                            }
                        } else if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)
                                   && gGame->m_heroRecs[cell->m_objectMetadata].m_locationType
                                          == MAP_EVENT_TRIGGER(MAP_OBJECT_TOWN)) {
                            owner = gGame->m_townOwners[gGame->m_heroRecs[cell->m_objectMetadata]
                                                            .m_locationMetadata];
                            if (owner >= 0) {
                                index =
                                    (gGame->m_players[owner].m_color);
                                flags->DrawToBuffer(
                                    screenX - VIEW_WORLD_TOWN_FLAG_LEFT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_FLIPPED,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                flags->DrawToBuffer(
                                    screenX + VIEW_WORLD_TOWN_FLAG_RIGHT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                            }
                        }
                        switch (MAP_TRIGGER_OBJECT(cell->m_triggerType)) {
                            case MAP_OBJECT_ALCHEMIST_LAB:
                            case MAP_OBJECT_MINE:
                            case MAP_OBJECT_SAWMILL:
                                owner = gGame->m_mineOwners[cell->m_objectMetadata];
                                index = owner < 0
                                            ? PLAYER_COLOR_NEUTRAL
                                            : (gGame->m_players[owner].m_color);
                                spheres->DrawToBuffer(
                                    screenX,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                lettersIcons->DrawToBuffer(
                                    screenX,
                                    rowPixelY,
                                    (gGame->m_mines[cell->m_objectMetadata].type),
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                break;
                            case MAP_OBJECT_HERO:
                                switch (MAP_TRIGGER_OBJECT(
                                    gGame->m_heroRecs[cell->m_objectMetadata].m_locationType
                                )) {
                                    case MAP_OBJECT_ALCHEMIST_LAB:
                                    case MAP_OBJECT_MINE:
                                    case MAP_OBJECT_SAWMILL:
                                        owner = gGame->m_mineOwners
                                                    [gGame->m_heroRecs[cell->m_objectMetadata]
                                                         .m_locationMetadata];
                                        index =
                                            owner < 0
                                                ? PLAYER_COLOR_NEUTRAL
                                                : (gGame->m_players[owner].m_color);
                                        spheres->DrawToBuffer(
                                            screenX,
                                            rowPixelY,
                                            index,
                                            ICON_DRAW_NORMAL,
                                            ICON_DRAW_OFFSET_FULL
                                        );
                                        // A hero's cell holds the hero, not the mine
                                        // (Tournament Edition TE-FIX-8); the original
                                        // drew the letter of mine record number
                                        // hero id, up to frame 23 of 7.
                                        lettersIcons->DrawToBuffer(
                                            screenX,
                                            rowPixelY,
                                            (gGame
                                                ->m_mines[gGame->m_heroRecs[cell->m_objectMetadata]
                                                              .m_locationMetadata]
                                                .type),
                                            ICON_DRAW_NORMAL,
                                            ICON_DRAW_OFFSET_FULL
                                        );
                                        break;
                                    default:
                                        break;
                                }
                        }
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)) {
                            owner = gGame->m_heroOwners[cell->m_objectMetadata];
                            if (owner >= 0) {
                                index =
                                    (gGame->m_players[owner].m_color);
                                flags->DrawToBuffer(
                                    screenX,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                            }
                        }
                        break;
                    case SPELL_VIEW_MINES:
                        switch (MAP_TRIGGER_OBJECT(cell->m_triggerType)) {
                            case MAP_OBJECT_ALCHEMIST_LAB:
                            case MAP_OBJECT_MINE:
                            case MAP_OBJECT_SAWMILL:
                                owner = gGame->m_mineOwners[cell->m_objectMetadata];
                                index = owner < 0
                                            ? PLAYER_COLOR_NEUTRAL
                                            : (gGame->m_players[owner].m_color);
                                spheres->DrawToBuffer(
                                    screenX,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                lettersIcons->DrawToBuffer(
                                    screenX,
                                    rowPixelY,
                                    (gGame->m_mines[cell->m_objectMetadata].type),
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                break;
                            case MAP_OBJECT_HERO:
                                switch (MAP_TRIGGER_OBJECT(
                                    gGame->m_heroRecs[cell->m_objectMetadata].m_locationType
                                )) {
                                    case MAP_OBJECT_ALCHEMIST_LAB:
                                    case MAP_OBJECT_MINE:
                                    case MAP_OBJECT_SAWMILL:
                                        owner = gGame->m_mineOwners
                                                    [gGame->m_heroRecs[cell->m_objectMetadata]
                                                         .m_locationMetadata];
                                        index =
                                            owner < 0
                                                ? PLAYER_COLOR_NEUTRAL
                                                : (gGame->m_players[owner].m_color);
                                        spheres->DrawToBuffer(
                                            screenX,
                                            rowPixelY,
                                            index,
                                            ICON_DRAW_NORMAL,
                                            ICON_DRAW_OFFSET_FULL
                                        );
                                        // A hero's cell holds the hero, not the mine
                                        // (Tournament Edition TE-FIX-8); the original
                                        // drew the letter of mine record number
                                        // hero id, up to frame 23 of 7.
                                        lettersIcons->DrawToBuffer(
                                            screenX,
                                            rowPixelY,
                                            (gGame
                                                ->m_mines[gGame->m_heroRecs[cell->m_objectMetadata]
                                                              .m_locationMetadata]
                                                .type),
                                            ICON_DRAW_NORMAL,
                                            ICON_DRAW_OFFSET_FULL
                                        );
                                        break;
                                    default:
                                        break;
                                }
                                break;
                            default:
                                break;
                        }
                        break;
                    case SPELL_VIEW_RESOURCES:
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_RESOURCE)) {
                            spheres->DrawToBuffer(
                                screenX - VIEW_WORLD_RESOURCE_X_SHIFT,
                                rowPixelY,
                                PLAYER_COLOR_NEUTRAL,
                                ICON_DRAW_NORMAL,
                                ICON_DRAW_OFFSET_FULL
                            );
                            lettersIcons->DrawToBuffer(
                                screenX - VIEW_WORLD_RESOURCE_X_SHIFT,
                                rowPixelY,
                                cell->m_objectIndex - RESOURCE_PILE_OBJECT_BASE,
                                ICON_DRAW_NORMAL,
                                ICON_DRAW_OFFSET_FULL
                            );
                        }
                        break;
                    case SPELL_VIEW_ARTIFACTS:
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_ARTIFACT))
                            flags->DrawToBuffer(
                                screenX,
                                rowPixelY,
                                VIEW_WORLD_FLAG_ARTIFACT,
                                ICON_DRAW_NORMAL,
                                ICON_DRAW_OFFSET_FULL
                            );
                        break;
                    case SPELL_VIEW_TOWNS:
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_TOWN)) {
                            owner = gGame->m_townOwners[cell->m_objectMetadata];
                            if (owner >= 0) {
                                index =
                                    (gGame->m_players[owner].m_color);
                                flags->DrawToBuffer(
                                    screenX - VIEW_WORLD_TOWN_FLAG_LEFT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_FLIPPED,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                flags->DrawToBuffer(
                                    screenX + VIEW_WORLD_TOWN_FLAG_RIGHT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                            }
                        } else if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)
                                   && gGame->m_heroRecs[cell->m_objectMetadata].m_locationType
                                          == MAP_EVENT_TRIGGER(MAP_OBJECT_TOWN)) {
                            owner = gGame->m_townOwners[gGame->m_heroRecs[cell->m_objectMetadata]
                                                            .m_locationMetadata];
                            if (owner >= 0) {
                                index =
                                    (gGame->m_players[owner].m_color);
                                flags->DrawToBuffer(
                                    screenX - VIEW_WORLD_TOWN_FLAG_LEFT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_FLIPPED,
                                    ICON_DRAW_OFFSET_FULL
                                );
                                flags->DrawToBuffer(
                                    screenX + VIEW_WORLD_TOWN_FLAG_RIGHT,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                            }
                        }
                        break;
                    case SPELL_VIEW_HEROES:
                        if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)) {
                            owner = gGame->m_heroOwners[cell->m_objectMetadata];
                            if (owner >= 0) {
                                index =
                                    (gGame->m_players[owner].m_color);
                                flags->DrawToBuffer(
                                    screenX,
                                    rowPixelY,
                                    index,
                                    ICON_DRAW_NORMAL,
                                    ICON_DRAW_OFFSET_FULL
                                );
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
            if (currHero && currHero->m_x == x && currHero->m_y == y)
                flags->DrawToBuffer(
                    screenX,
                    rowPixelY,
                    VIEW_WORLD_FLAG_CURRENT_HERO,
                    ICON_DRAW_NORMAL,
                    ICON_DRAW_OFFSET_FULL
                );
        }
        for (x = MAP_CELL_GRID_SIZE - 1; x >= 0; x--) {
            cell = GetCell(x, y);
            if ((gGame->m_mapExtra[x][y] & gCurPlayerBit) || drawAllTerrains
                || (cell->m_triggerType == MAP_OBJECT_TRIGGER(MAP_OBJECT_TOWN)
                    && spellType == SPELL_VIEW_TOWNS)) {
                screenX = x * VIEW_WORLD_CELL_PIXELS + VIEW_WORLD_ORIGIN;
                rowPixelY = y * VIEW_WORLD_CELL_PIXELS + VIEW_WORLD_ORIGIN;
                if (cell->m_overlayIndex != MAP_CELL_NO_FRAME) {
                    tileset = (cell->m_overlayTileset & MAP_CELL_TILESET_MASK);
                    // The small trees have fewer frames than the map's
                    // trees (98 against 101); the original drew the last
                    // three from a frame entry past the table.
                    if (((1 << tileset) & mask)
                        && cell->m_objectIndex < mapTilesets[tileset]->m_frameCount)
                        mapTilesets[tileset]->DrawToBuffer(
                            screenX,
                            rowPixelY,
                            cell->m_overlayIndex,
                            ICON_DRAW_NORMAL,
                            ICON_DRAW_OFFSET_FULL
                        );
                }
            }
        }
    }

    bitmap worldImage(BITMAP_TYPE_MEMORY, kWorldSize, kWorldSize);
    worldImage.GrabBitmap(gWindowManager->m_screen, BORDER_EDGE_SIZE, BORDER_EDGE_SIZE);
    WorldMapView view{worldImage};
    const int centerX = (m_mapOriginX + ADVENTURE_VIEW_CENTER_X) * VIEW_WORLD_CELL_PIXELS
                        + VIEW_WORLD_ORIGIN - BORDER_EDGE_SIZE;
    const int centerY = (m_mapOriginY + ADVENTURE_VIEW_CENTER_Y) * VIEW_WORLD_CELL_PIXELS
                        + VIEW_WORLD_ORIGIN - BORDER_EDGE_SIZE;
    view.left = centerX - kWorldSize / (2 * gWorldZoom);
    view.top = centerY - kWorldSize / (2 * gWorldZoom);
    view.Draw();
    sprintf(
        gText,
        "view-%02d.bin",
        spellType - SPELL_VIEW_MINES
    );
    win = new heroWindow(RADAR_LEFT, RADAR_TOP, gText);
    if (!win)
        MemError();
    win->AddWidget(new goldButton(11, 120, 24, 22, kWorldZoomOut, "-"), WINDOW_Z_ORDER_BOTTOM);
    win->AddWidget(new goldButton(109, 120, 24, 22, kWorldZoomIn, "+"), WINDOW_Z_ORDER_BOTTOM);
    gWorldMapView = &view;
    gWindowManager->DoDialog(win, WorldMapHandler, false);
    gWorldMapView = nullptr;
    delete win;
    canvas.Restore();
    UpdateRadar(true, false);
    for (i = 0; i < VIEW_WORLD_TILESET_COUNT; i++) {
        if (mapTilesets[i])
            gResourceManager->Dispose(mapTilesets[i]);
    }
    gResourceManager->Dispose(ground);
    gResourceManager->Dispose(flags);
    gResourceManager->Dispose(spheres);
    gResourceManager->Dispose(lettersIcons);
    RedrawAdvScreen(true);
}

void advManager::GrabScreen(void) {
    gMouseManager->ReallyHidePointer();
    GrabScreenBitmap(gWindowManager->m_screen, 0, 0);
    gMouseManager->ReallyShowPointer();
}

i16 advManager::ControlPanel(void) {
    tag_message message;
    b32 heroWasMobilized;
    i8 oldSpeedState;
    i32 gameCommand;
    i32 n;

    TrimLoopingSounds(ADVMGR_ACTIVE_SOUND_COUNT);
    gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
    gameCommand = MAIN_MENU_NO_COMMAND;
    oldSpeedState = gConfig.walkSpeed;
    gFreshSave = false;
    heroWasMobilized = m_heroMobilized;
    gPrefsChanged = false;
    DemobilizeCurrHero();
    gAdventurePanel = new heroWindow(160, 10, "cpanel.bin");
    if (gAdventurePanel == NULL)
        MemError();
    SetWinText(gAdventurePanel, WINDOW_TEXT_CONTROL_PANEL);
    if (gRemoteOn) {
        message.type = MESSAGE_WIDGET;
        message.id = CONTROL_NEW_GAME;
        message.command = WIDGET_COMMAND_SET_FLAGS;
        message.value = WIDGET_FLAG_DIM_REQUEST;
        gAdventurePanel->BroadcastMessage(message);
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.value = WIDGET_FLAG_ENABLED;
        gAdventurePanel->BroadcastMessage(message);
        message.id = CONTROL_LOAD_GAME;
        message.command = WIDGET_COMMAND_SET_FLAGS;
        message.value = WIDGET_FLAG_DIM_REQUEST;
        gAdventurePanel->BroadcastMessage(message);
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.value = WIDGET_FLAG_ENABLED;
        gAdventurePanel->BroadcastMessage(message);
    }
    UpdateCPanel(true);
    gAdventurePanel->SetConfirmButton(PANEL_CLOSE_WIDGET, true);
    gWindowManager->DoDialog(gAdventurePanel, CPanelHandler, false);
    delete gAdventurePanel;
    switch (gWindowManager->m_dialogResult) {
        case CONTROL_NEW_GAME:
        case CONTROL_LOAD_GAME:
        case CONTROL_QUIT:
            gameCommand = gWindowManager->m_dialogResult;
            break;
        case CONTROL_SCENARIO_INFO:
            if (gGame->m_campaignType > 0)
                gGame->ShowCampaignInfo(gGame->m_campaignScenario, true, 0);
            else
                gGame->ShowScenInfo();
            break;
        case CONTROL_SAVE_GAME:
            SaveGame();
            break;
    }
    if (gConfig.walkSpeed != oldSpeedState) {
        for (n = TERRAIN_WATER; n < TERRAIN_COUNT; n++) {
            if (m_cursorSamples[n]) {
                gResourceManager->Dispose(m_cursorSamples[n]);
                m_cursorSamples[n] = NULL;
            }
        }
        GetCursorSampleSet(gConfig.walkSpeed);
    }
    if (gPrefsChanged)
        WritePrefs();
    if (heroWasMobilized)
        MobilizeCurrHero(false);
    if (gameCommand != MAIN_MENU_NO_COMMAND) {
        gGameCommand = gameCommand;
        return 1;
    }
    return 0;
}

void UpdateCPanel(b8 initialDraw) {
    tag_message message;

    SET_WIDGET_MESSAGE(message, WIDGET_COMMAND_SET_FRAME, CONTROL_MUSIC_VOLUME);
    message.value = gConfig.musicVolume ? CPANEL_FRAME_MUSIC_ON : CPANEL_FRAME_MUSIC_OFF;
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_SOUND_VOLUME;
    message.value = gConfig.soundVolume ? CPANEL_FRAME_SOUND_ON : CPANEL_FRAME_SOUND_OFF;
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_WALK_SPEED;
    message.value = gConfig.walkSpeed + CPANEL_FRAME_WALK_SPEED_FIRST;
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_MUSIC_SOURCE;
    message.value =
        (gConfig.musicSource ? CPANEL_MUSIC_LABEL_CD
                                                               : CPANEL_MUSIC_LABEL_LOCAL)
        + CPANEL_FRAME_MUSIC_SOURCE_FIRST;
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_SHOW_ROUTE;
    message.value = gConfig.showRoute + CPANEL_FRAME_SHOW_ROUTE_FIRST;
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_SHOW_ENEMY_MOVES;
    if (gRemoteOn)
        message.value = CPANEL_FRAME_ENEMY_MOVES_FIRST;
    else
        message.value = 1 - gConfig.blackoutComputer + CPANEL_FRAME_ENEMY_MOVES_FIRST;
    gAdventurePanel->BroadcastMessage(message);
    message.command = WIDGET_COMMAND_SET_TEXT;
    message.id = CONTROL_MUSIC_VOLUME_TEXT;
    message.text = gOnOffText[gConfig.musicVolume];
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_SOUND_VOLUME_TEXT;
    message.text = gOnOffText[gConfig.soundVolume];
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_WALK_SPEED_TEXT;
    message.text = gWalkSpeedText[gConfig.walkSpeed];
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_MUSIC_SOURCE_TEXT;
    message.text = gMusicQualityText
        [gConfig.musicSource ? CPANEL_MUSIC_LABEL_CD
                                                               : CPANEL_MUSIC_LABEL_LOCAL];
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_SHOW_ROUTE_TEXT;
    message.text = gOnOffText[gConfig.showRoute];
    gAdventurePanel->BroadcastMessage(message);
    message.id = CONTROL_SHOW_ENEMY_MOVES_TEXT;
    message.text = gOnOffText[1 - gConfig.blackoutComputer];
    gAdventurePanel->BroadcastMessage(message);
    if (!initialDraw)
        gAdventurePanel->MoveWindow(0, 0);
}

i8 SaveGame(void) {
    i16 res;
    fileRequester* newFileReq;
    char searchMask[16];
    i8 success;
    i32 humans;
    i32 player;
    char extensionBuf[8];

    success = 0;
    humans = 0;
    gAdvManager->DisableButtons();
    gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
    for (player = 0; player < GAME_PLAYER_COUNT; player++)
        if (!gGame->m_playerDead[player] && gHumanPlayer[player])
            humans++;
    if (gCampaignChoice > CAMPAIGN_NONE) {
        sprintf(extensionBuf, ".CGM");
        sprintf(searchMask, "*.CGM");
    } else {
        sprintf(extensionBuf, ".GM%d", humans);
        sprintf(searchMask, "*.GM*");
    }
    newFileReq =
        new fileRequester(0xa0, 0x28, FILE_REQUESTER_SAVE, searchMask, gGamePath, extensionBuf);
    if (!newFileReq)
        MemError();
    res = gExec->DoDialog(newFileReq);
    if (res == DIALOG_BUTTON_2) {
        success = 1;
        gFreshSave = true;
        success = gGame->SaveGame(gLastFilename, false);
        if (success)
            NormalDialog(localization::Tr("adventure.save.success"), NORMAL_DIALOG_TYPE_OK, 0xb1);
    }
    delete newFileReq;
    gAdvManager->EnableButtons();
    return success;
}

i16 CPanelHandler(struct tag_message& message) {
    b8 anyChanged = false;
    char question[120];
    b8 handled = false;
    if (message.type == MESSAGE_WIDGET) {
        if (message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
            if (IS_WIDGET_SELECTION_NOTIFICATION(message.command)) {
                i32 helpIndex = CPANEL_HELP_NONE;
                switch (message.id) {
                    case CONTROL_NEW_GAME:
                        helpIndex = CPANEL_HELP_NEW_GAME;
                        break;
                    case CONTROL_LOAD_GAME:
                        helpIndex = CPANEL_HELP_LOAD_GAME;
                        break;
                    case CONTROL_QUIT:
                        helpIndex = CPANEL_HELP_QUIT;
                        break;
                    case PANEL_CLOSE_WIDGET:
                        helpIndex = CPANEL_HELP_CLOSE;
                        break;
                    case CONTROL_SAVE_GAME:
                        helpIndex = CPANEL_HELP_SAVE_GAME;
                        break;
                    case CONTROL_MUSIC_VOLUME:
                        helpIndex = CPANEL_HELP_MUSIC_VOLUME;
                        break;
                    case CONTROL_SOUND_VOLUME:
                        helpIndex = CPANEL_HELP_SOUND_VOLUME;
                        break;
                    case CONTROL_WALK_SPEED:
                        helpIndex = CPANEL_HELP_WALK_SPEED;
                        break;
                    case CONTROL_MUSIC_SOURCE:
                        helpIndex = CPANEL_HELP_MUSIC_SOURCE;
                        break;
                    case CONTROL_SHOW_ROUTE:
                        helpIndex = CPANEL_HELP_SHOW_ROUTE;
                        break;
                    case CONTROL_SHOW_ENEMY_MOVES:
                        helpIndex = CPANEL_HELP_SHOW_ENEMY_MOVES;
                        break;
                    case CONTROL_SCENARIO_INFO:
                        helpIndex = CPANEL_HELP_SCENARIO_INFO;
                        break;
                }
                if (helpIndex >= CPANEL_HELP_FIRST)
                    NormalDialog(gCPanelHelp[helpIndex], NORMAL_DIALOG_TYPE_QUICK_VIEW, 0xb1);
            }
        } else {
            switch (message.command) {
                case WIDGET_NOTIFY_DESELECT:
                    switch (message.id) {
                        case CONTROL_NEW_GAME:
                            strcpy(question, localization::Tr("adventure.confirm_restart"));
                            goto confirm_reset;
                        case CONTROL_LOAD_GAME:
                            strcpy(question, localization::Tr("adventure.confirm_load"));
                            goto confirm_reset;
                        case CONTROL_QUIT:
                            strcpy(question, localization::Tr("adventure.confirm_quit"));
                        confirm_reset:
                            handled = true;
                            if (!gFreshSave) {
                                NormalDialog(question, NORMAL_DIALOG_TYPE_YES_NO, 0xb1, 0x50);
                                if (gWindowManager->m_dialogResult == NORMAL_DIALOG_CANCEL)
                                    handled = false;
                            }
                            break;
                        case CONTROL_SAVE_GAME:
                            handled = true;
                            break;
                        case CONTROL_SCENARIO_INFO:
                        case PANEL_CLOSE_WIDGET:
                            handled = true;
                            break;
                    }
                    break;
                case WIDGET_NOTIFY_SELECT:
                    switch (message.id) {
                        case CONTROL_MUSIC_VOLUME:
                            gConfig.musicVolume =
                                (gConfig.musicVolume + 1) % (SOUND_VOLUME_LAST + 1);
                            SetMusicVolume(gConfig.musicVolume);
                            anyChanged = true;
                            gPrefsChanged = true;
                            break;
                        case CONTROL_SOUND_VOLUME:
                            gConfig.soundVolume =
                                (gConfig.soundVolume + 1) % (SOUND_VOLUME_LAST + 1);
                            SetEffectsVolume(gConfig.soundVolume);
                            anyChanged = true;
                            gPrefsChanged = true;
                            break;
                        case CONTROL_WALK_SPEED:
                            ++gConfig.walkSpeed;
                            WALK_SPEED_WRAP(gConfig.walkSpeed);
                            anyChanged = true;
                            gPrefsChanged = true;
                            break;
                        case CONTROL_MUSIC_SOURCE:
                            if (gConfig.musicSource) {
                                gConfig.musicSource = SOUND_MUSIC_SOURCE_DIGITAL;
                            } else {
                                gConfig.musicSource = SOUND_MUSIC_SOURCE_CD;
                            }
                            SetMusicSource(gConfig.musicSource != SOUND_MUSIC_SOURCE_DIGITAL);
                            anyChanged = true;
                            gPrefsChanged = true;
                            break;
                        case CONTROL_SHOW_ROUTE:
                            gConfig.showRoute = 1 - gConfig.showRoute;
                            anyChanged = true;
                            gPrefsChanged = true;
                            break;
                        case CONTROL_SHOW_ENEMY_MOVES:
                            if (!gRemoteOn) {
                                gConfig.blackoutComputer = 1 - gConfig.blackoutComputer;
                                anyChanged = true;
                                gPrefsChanged = true;
                            }
                            break;
                    }
                    break;
            }
        }
    }
    if (anyChanged)
        UpdateCPanel(false);
    if (handled) {
        FINISH_DIALOG_MESSAGE(message);
        return MESSAGE_DISPATCH_FORWARD;
    }
    return MESSAGE_DISPATCH_CONSUME;
}

void advManager::CheckCastSpell(void) {
    if (gCurPlayerData->CurrentHero() != HERO_ID_NONE) {
        MobilizeCurrHero(false);
        CompleteDraw(false);
        UpdateScreen(false, false);
        GrabScreen();
        gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
        CastSpell(gGame->ViewSpells(
            gGame->GetHero(gCurPlayerData->m_currentHero),
            SPELL_TYPE_ADVENTURE,
            NullHandler,
            false
        ));
    }
}

void advManager::AdvPanel(void) {
    heroWindow* adventurePanel;
    {
        TrimLoopingSounds(ADVMGR_ACTIVE_SOUND_COUNT);
        gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
        b32 heroWasMobilized = m_heroMobilized;
        struct tag_message message;
        DemobilizeCurrHero();

        adventurePanel = new heroWindow(160, 40, "apanel.bin");
        if (adventurePanel == NULL)
            MemError();
        if (gCurPlayerData->CurrentHero() == HERO_ID_NONE) {
            message.type = MESSAGE_WIDGET;
            message.id = PANEL_SEARCH;
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_ENABLED;
            adventurePanel->BroadcastMessage(message);
            message.id = PANEL_CAST_SPELL;
            adventurePanel->BroadcastMessage(message);
            message.id = PANEL_SEARCH;
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DIM_REQUEST;
            adventurePanel->BroadcastMessage(message);
            message.id = PANEL_CAST_SPELL;
            adventurePanel->BroadcastMessage(message);
        }

        adventurePanel->SetConfirmButton(PANEL_CLOSE_WIDGET, true);
        gWindowManager->DoDialog(adventurePanel, APanelHandler, false);
        delete adventurePanel;
        switch (gWindowManager->m_dialogResult) {
            case PANEL_CAST_SPELL:
                CheckCastSpell();
                break;
            case PANEL_SEARCH:
                ProcessSearch(ADVMGR_SEARCH_VIEW_CENTER, ADVMGR_SEARCH_VIEW_CENTER);
                break;
            case PANEL_VIEW_WORLD:
                ViewWorld(SPELL_VIEW_ALL, false, false);
                break;
            case PANEL_VIEW_PUZZLE:
                ViewPuzzle();
                break;
        }

        if (heroWasMobilized)
            MobilizeCurrHero(false);
    }
}

i16 APanelHandler(struct tag_message& message) {
    b8 handled = false;
    if (message.type == MESSAGE_WIDGET) {
        if (message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
            if (IS_WIDGET_SELECTION_NOTIFICATION(message.command)) {
                i32 helpIndex = PANEL_NO_HELP;
                switch (message.id) {
                    case PANEL_VIEW_WORLD:
                        helpIndex = PANEL_VIEW_WORLD_HELP;
                        break;
                    case PANEL_VIEW_PUZZLE:
                        helpIndex = PANEL_VIEW_PUZZLE_HELP;
                        break;
                    case PANEL_CAST_SPELL:
                        helpIndex = PANEL_CAST_SPELL_HELP;
                        break;
                    case PANEL_SEARCH:
                        helpIndex = PANEL_SEARCH_HELP;
                        break;
                    case PANEL_CLOSE_WIDGET:
                        helpIndex = PANEL_CLOSE_HELP;
                        break;
                }
                if (helpIndex >= 0)
                    NormalDialog(gAPanelHelp[helpIndex], NORMAL_DIALOG_TYPE_QUICK_VIEW, 0xb1);
            }
        } else {
            switch (message.command) {
                case WIDGET_NOTIFY_DESELECT:
                    switch (message.id) {
                        case PANEL_VIEW_WORLD:
                        case PANEL_VIEW_PUZZLE:
                        case PANEL_CAST_SPELL:
                        case PANEL_SEARCH:
                        case PANEL_CLOSE_WIDGET:
                            handled = true;
                            break;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (handled) {
        FINISH_DIALOG_MESSAGE(message);
        return MESSAGE_DISPATCH_FORWARD;
    }
    return MESSAGE_DISPATCH_CONSUME;
}

i16 DimensionDoorHandler(struct tag_message& message) {
    b8 result;
    i16 mouseX;
    i16 mouseY;
    mapCell* cell;

    if (gTimers[ADVENTURE_FRAME_TIMER_SLOT] < KBTickCount()) {
        gAdvManager->CompleteDraw(gAdvManager->m_mapOriginX, gAdvManager->m_mapOriginY, false);
        gAdvManager->UpdateScreen(false, false);
    }
    result = false;
    switch (message.type) {
        case MESSAGE_WIDGET:
            switch (message.command) {
                case WIDGET_NOTIFY_SELECT:
                    switch (message.id) {
                        case DIMENSION_DOOR_FIRST_BUTTON:
                        case DIMENSION_DOOR_LAST_BUTTON:
                            if (message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON) {
                            } else if (gWindowManager->m_dialogResult == TRAVEL_DIALOG_ACCEPT) {
                                result = true;
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                case WIDGET_COMMAND_HOVER:
                    switch (message.id) {
                        case DIMENSION_DOOR_LAST_BUTTON:
                            gWindowManager->m_dialogResult = TRAVEL_DIALOG_REJECT;
                            gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                            break;
                        case ADVENTURE_CONTROL_MAP_VIEW:
                            gMouseManager->MouseCoords(mouseX, mouseY);
                            if (!gAdvManager->MapCellAtScreenPoint(mouseX, mouseY, mouseX, mouseY)) {
                                gWindowManager->m_dialogResult = TRAVEL_DIALOG_REJECT;
                                gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                                break;
                            }
                            if (gAdvManager->m_hoverCellX != mouseX
                                || gAdvManager->m_hoverCellY != mouseY) {
                                gAdvManager->m_hoverCellX = mouseX;
                                gAdvManager->m_hoverCellY = mouseY;
                                cell = gAdvManager->GetCell(
                                    gAdvManager->m_mapOriginX + mouseX,
                                    gAdvManager->m_mapOriginY + mouseY
                                );
                                if ((cell->m_triggerType & MAP_TRIGGER_EVENT)
                                    || (cell->m_secondaryTrigger & MAP_CELL_SECONDARY_BLOCKED)) {
                                    gWindowManager->m_dialogResult = TRAVEL_DIALOG_REJECT;
                                    gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
                                } else {
                                    gWindowManager->m_dialogResult = TRAVEL_DIALOG_ACCEPT;
                                    gMouseManager->SetPointer(ADVENTURE_POINTER_MOVE);
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                case WIDGET_NOTIFY_DESELECT:
                    switch (message.id) {
                        case PANEL_CLOSE_WIDGET:
                            gWindowManager->m_dialogResult = TRAVEL_DIALOG_REJECT;
                            result = true;
                            break;
                    }
                    break;
            }
            break;
    }
    if (result) {
        message.command = (message.id = WIDGET_COMMAND_DIALOG_SELECT);
        return MESSAGE_DISPATCH_FORWARD;
    }
    return MESSAGE_DISPATCH_CONSUME;
}

b8 advManager::ComboDraw(i16 originX, i16 originY, b8 animate) {
    static i32 gFrameCount = 0;
    i32 updateCount;
    i32 drawY;
    i32 drawX;
    mapCell* cell;

    PollSound();
    if (!gShowIt)
        return false;
    if (m_forceCompleteDraw) {
        CompleteDraw(originX, originY, false);
        return true;
    }
    if (animate) {
        gFrameCount += gFrameStep;
        if (gFrameCount < COMBO_FRAME_LIMIT) {
            Process1WindowsMessage();
            if (gTimers[ADVENTURE_FRAME_TIMER_SLOT] < KBTickCount())
                gTimers[ADVENTURE_FRAME_TIMER_SLOT] = KBTickCount() + TIMER_DELAY;
            PollSound();
            return false;
        } else {
            gFrameCount = 0;
        }
    }

    // Incremental redraws assume the screen pixels are aligned to map
    // tiles. Rebuild the scene before applying a fractional camera offset.
    if (HasCameraOffset()) {
        CompleteDraw(originX, originY, false);
        return true;
    }

    m_previousOriginX = m_mapOriginX;
    m_previousOriginY = m_mapOriginY;
    memset(gComboDraw, 0, sizeof(gComboDraw));
    m_comboHeroDrawn = false;

    for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
        for (drawY = 0; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
            if (originX + drawX >= 0 && originX + drawX < MAP_CELL_GRID_SIZE && originY + drawY >= 0
                && originY + drawY < MAP_CELL_GRID_SIZE) {
                cell = GetCell(originX + drawX, originY + drawY);
                if (cell->m_flags & (MAP_CELL_OBJECT_ANIMATED | MAP_CELL_OVERLAY_ANIMATED))
                    ++gComboDraw[drawX][drawY];
                if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_MONSTER)) {
                    ++gComboDraw[drawX][drawY];
                    if (GetCloudLookup(drawX + originX, drawY + originY)) {
                        gComboDraw[drawX + 1][drawY] += COMBO_CLOUD_MARK;
                        if (drawY >= 1) {
                            gComboDraw[drawX][drawY - 1] += COMBO_CLOUD_MARK;
                            gComboDraw[drawX + 1][drawY - 1] += COMBO_CLOUD_MARK;
                        }
                    } else {
                        ++gComboDraw[drawX + 1][drawY];
                        if (drawY >= 1) {
                            ++*(gComboDraw[drawX] + drawY - 1);
                            ++gComboDraw[drawX + 1][drawY - 1];
                        }
                    }
                }
                if (cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_HERO)
                    || cell->m_triggerType == MAP_EVENT_TRIGGER(MAP_OBJECT_SHIP)) {
                    ++gComboDraw[drawX][drawY];
                    if (GetCloudLookup(drawX + originX, drawY + originY)) {
                        gComboDraw[drawX + 1][drawY] += COMBO_CLOUD_MARK;
                        gComboDraw[drawX][drawY + 1] += COMBO_CLOUD_MARK;
                        if (drawY >= 1)
                            gComboDraw[drawX][drawY - 1] += COMBO_CLOUD_MARK;
                        if (drawX >= 1)
                            gComboDraw[drawX - 1][drawY] += COMBO_CLOUD_MARK;
                    } else {
                        ++gComboDraw[drawX + 1][drawY];
                        ++gComboDraw[drawX][drawY + 1];
                        if (drawY >= 1)
                            ++*(gComboDraw[drawX] + drawY - 1);
                        if (drawX >= 1)
                            ++gComboDraw[drawX - 1][drawY];
                    }
                }
            }
        }
    }

    for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
        for (drawY = 0; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
            if (gComboDraw[drawX][drawY]) {
                if (originX + drawX < 0 || originX + drawX >= MAP_CELL_GRID_SIZE
                    || originY + drawY < 0 || originY + drawY >= MAP_CELL_GRID_SIZE)
                    gComboDraw[drawX][drawY] = 0;
                else if (gComboDraw[drawX][drawY] < COMBO_CLOUD_MARK
                         && !GetCloudLookup(drawX + originX, drawY + originY))
                    gComboDraw[drawX][drawY] = 0;
            }
        }
    }

    if (gMouseManager->IsVis()) {
        drawX = std::clamp<int>(gMouseManager->m_savedLeft >> CELL_PIXEL_SHIFT, 0, 61);
        drawY = std::clamp<int>(gMouseManager->m_savedTop >> CELL_PIXEL_SHIFT, 0, 62);
        ++gComboDraw[drawX][drawY];
        ++gComboDraw[drawX + 1][drawY];
        ++gComboDraw[drawX][drawY + 1];
        ++gComboDraw[drawX + 1][drawY + 1];
        ++gComboDraw[drawX + COMBO_FAR_NEIGHBOR_OFFSET][drawY + 1];
    }
    if (m_heroMobilized) {
        for (drawY = ADVENTURE_VIEW_CENTER_Y - 1; drawY <= ADVENTURE_VIEW_CENTER_Y + 1; drawY++)
            for (drawX = ADVENTURE_VIEW_CENTER_X - 1; drawX <= ADVENTURE_VIEW_CENTER_X + 1; drawX++)
                ++gComboDraw[drawX][drawY];
    }
    if (m_cursorType == ADVMGR_HERO_ICON_BOAT) {
        ++gComboDraw[ADVENTURE_VIEW_CENTER_X - 1][ADVENTURE_VIEW_CENTER_Y - 2];
        ++gComboDraw[ADVENTURE_VIEW_CENTER_X][ADVENTURE_VIEW_CENTER_Y - 2];
        ++gComboDraw[ADVENTURE_VIEW_CENTER_X + 1][ADVENTURE_VIEW_CENTER_Y - 2];
    }

    for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
        if (gComboDraw[drawX][0])
            DrawCell(
                originX + drawX,
                originY,
                drawX,
                0,
                ADVMGR_DRAW_GROUND | ADVMGR_DRAW_OBJECT,
                false,
                false
            );
    }
    for (drawY = 1; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
            if (gComboDraw[drawX][drawY])
                DrawCell(
                    originX + drawX,
                    originY + drawY,
                    drawX,
                    drawY,
                    ADVMGR_DRAW_GROUND,
                    false,
                    false
                );
        }
    }
    for (drawY = 1; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
        PollSound();
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
            if (gComboDraw[drawX][drawY - 1])
                DrawCell(
                    originX + drawX,
                    originY + drawY - 1,
                    drawX,
                    drawY - 1,
                    ADVMGR_DRAW_OVERLAY | ADVMGR_DRAW_HERO,
                    false,
                    false
                );
        }
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
            if (gComboDraw[drawX][drawY])
                DrawCell(
                    originX + drawX,
                    originY + drawY,
                    drawX,
                    drawY,
                    ADVMGR_DRAW_OBJECT,
                    false,
                    false
                );
        }
    }
    for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
        if (gComboDraw[drawX][ADVENTURE_VIEWPORT_CELLS_Y - 1])
            DrawCell(
                originX + drawX,
                originY + ADVENTURE_VIEWPORT_CELLS_Y - 1,
                drawX,
                ADVENTURE_VIEWPORT_CELLS_Y - 1,
                ADVMGR_DRAW_OVERLAY | ADVMGR_DRAW_HERO,
                false,
                false
            );
    }
    for (drawY = 0; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
            if (gComboDraw[drawX][drawY])
                DrawCell(
                    originX + drawX,
                    originY + drawY,
                    drawX,
                    drawY,
                    ADVMGR_DRAW_CLOUD,
                    false,
                    false
                );
        }
    }

    PollSound();
    UpdBottomView(false, true, true);
    DrawAdventureBorder();
    gLimitUpdMinX = ADVENTURE_VIEWPORT_CELLS_X;
    gLimitUpdMinY = ADVENTURE_VIEWPORT_CELLS_Y;
    gLimitUpdMaxX = 0;
    gLimitUpdMaxY = 0;
    updateCount = 0;
    for (drawY = 0; drawY < ADVENTURE_VIEWPORT_CELLS_Y; drawY++) {
        for (drawX = 0; drawX < ADVENTURE_VIEWPORT_CELLS_X; drawX++) {
            if (gComboDraw[drawX][drawY]) {
                updateCount++;
                if (drawX < gLimitUpdMinX)
                    gLimitUpdMinX = drawX;
                if (drawX > gLimitUpdMaxX)
                    gLimitUpdMaxX = drawX;
                if (drawY < gLimitUpdMinY)
                    gLimitUpdMinY = drawY;
                if (drawY > gLimitUpdMaxY)
                    gLimitUpdMaxY = drawY;
            }
        }
    }
    gLimitUpdMinX <<= CELL_PIXEL_SHIFT;
    gLimitUpdMinY <<= CELL_PIXEL_SHIFT;
    gLimitUpdMaxX = ((gLimitUpdMaxX + 1) << CELL_PIXEL_SHIFT) - 1;
    gLimitUpdMaxY = ((gLimitUpdMaxY + 1) << CELL_PIXEL_SHIFT) - 1;
    if (gLimitUpdMinX < BORDER_EDGE_SIZE)
        gLimitUpdMinX = BORDER_EDGE_SIZE;
    if (gLimitUpdMaxX > BORDER_MIDDLE_END_X - 1)
        gLimitUpdMaxX = BORDER_MIDDLE_END_X - 1;
    if (gLimitUpdMinY < BORDER_EDGE_SIZE)
        gLimitUpdMinY = BORDER_EDGE_SIZE;
    if (gLimitUpdMaxY > BORDER_MIDDLE_END_Y - 1)
        gLimitUpdMaxY = BORDER_MIDDLE_END_Y - 1;
    if (gLimitUpdMinX > gLimitUpdMaxX || gLimitUpdMinY > gLimitUpdMaxY) {
        gLimitUpdMinX = gLimitUpdMaxX - 1;
        gLimitUpdMinY = gLimitUpdMaxY - 1;
        return false;
    }
    return true;
}

b8 advManager::ComboDraw(b32 animate) {
    return ComboDraw(m_mapOriginX, m_mapOriginY, animate);
}

void advManager::SetEnvironmentOrigin(i16 originX, i16 originY, i16 stopSounds) {
    i32 soundRadius;
    i32 edgeOffset;
    i32 maxCells = ADVMGR_ACTIVE_SOUND_COUNT / 2;
    i32 layer;

    for (edgeOffset = 0; edgeOffset < ADVMGR_ACTIVE_SOUND_COUNT; ++edgeOffset) {
        if (m_activeSounds[edgeOffset].soundId != MAP_SOUND_NONE) {
            if (stopSounds) {
                StopSample(m_loopingSamples[m_activeSounds[edgeOffset].soundId]);
                m_activeSounds[edgeOffset].soundId = MAP_SOUND_NONE;
                m_activeSounds[edgeOffset].distance = ENVIRONMENT_SOUND_FAR_DISTANCE;
            } else {
                m_activeSounds[edgeOffset].distance = ENVIRONMENT_SOUND_FAR_DISTANCE;
            }
        }
    }
    if (gConfig.soundVolume != SOUND_VOLUME_OFF) {
        m_activeSoundMask = 0;
        for (layer = ENVIRONMENT_SOUND_FIRST_LAYER; layer <= ENVIRONMENT_SOUND_LAYER_COUNT;
             ++layer) {
            InsertSound(originX, originY, 0, layer);
            for (soundRadius = 0; soundRadius < ENVIRONMENT_SOUND_RADIUS_COUNT; ++soundRadius) {
                for (edgeOffset = 0; edgeOffset < soundRadius * ENVIRONMENT_SOUND_EDGE_SPAN;
                     ++edgeOffset) {
                    InsertSound(
                        originX - soundRadius + edgeOffset,
                        originY - soundRadius,
                        soundRadius,
                        layer
                    );
                    InsertSound(
                        originX + soundRadius,
                        originY - soundRadius + edgeOffset,
                        soundRadius,
                        layer
                    );
                    InsertSound(
                        originX + soundRadius - edgeOffset,
                        originY + soundRadius,
                        soundRadius,
                        layer
                    );
                    InsertSound(
                        originX - soundRadius,
                        originY + soundRadius - edgeOffset,
                        soundRadius,
                        layer
                    );
                }
            }
        }
        for (edgeOffset = 0; edgeOffset < ADVMGR_ACTIVE_SOUND_COUNT; ++edgeOffset) {
            if (m_activeSounds[edgeOffset].soundId != MAP_SOUND_NONE
                && m_activeSounds[edgeOffset].distance > ENVIRONMENT_SOUND_MAX_DISTANCE) {
                StopSample(m_loopingSamples[m_activeSounds[edgeOffset].soundId]);
                m_activeSounds[edgeOffset].soundId = MAP_SOUND_NONE;
            }
            if (m_activeSounds[edgeOffset].soundId != MAP_SOUND_NONE
                && (m_activeSoundMask & (1 << m_activeSounds[edgeOffset].soundId)) != 0) {
                m_loopingSamples[m_activeSounds[edgeOffset].soundId]->m_playbackData.volume =
                    gEnvironmentVolume[m_activeSounds[edgeOffset].distance];
                UpdateSampleVolume(m_loopingSamples[m_activeSounds[edgeOffset].soundId]);
            }
        }
    }
}

void advManager::CheckLoadSample(i32 index) {
    if (m_loopingSamples[index] == NULL) {
        TrimLoopingSounds(ADVMGR_ACTIVE_SOUND_COUNT);
        sprintf(gText, "loop%04d.82M", index);
        m_loopingSamples[index] = gResourceManager->GetSample(gText);
    }
}

void advManager::InsertSound(i16 x, i16 y, i16 distance, i8 soundLayer) {
    i32 slot;
    i32 distanceLimit;
    i32 i;
    i32 soundId;

    if (x < 0 || y < 0 || x >= MAP_CELL_GRID_SIZE || y >= MAP_CELL_GRID_SIZE)
        return;
    soundId = gGame->m_mapSounds[x][y];
    if (soundId == MAP_SOUND_NONE)
        return;
    for (i = 0; i < ADVMGR_ACTIVE_SOUND_COUNT; ++i) {
        if (m_activeSounds[i].soundId == soundId) {
            if (m_activeSounds[i].distance > distance) {
                m_activeSounds[i].distance = distance;
                m_activeSoundMask |= 1 << m_activeSounds[i].soundId;
            }
            return;
        }
    }
    if (soundLayer == ENVIRONMENT_SOUND_FIRST_LAYER)
        return;
    distanceLimit = distance;
    slot = ENVIRONMENT_SOUND_NO_SLOT;
    for (i = 0; i < ADVMGR_ACTIVE_SOUND_COUNT; ++i) {
        if (m_activeSounds[i].distance > distanceLimit) {
            distanceLimit = m_activeSounds[i].distance;
            slot = i;
        }
    }
    if (slot != ENVIRONMENT_SOUND_NO_SLOT) {
        if (m_activeSounds[slot].soundId != MAP_SOUND_NONE)
            StopSample(m_loopingSamples[m_activeSounds[slot].soundId]);
        m_activeSounds[slot].soundId = soundId;
        m_activeSounds[slot].distance = distance;
        CheckLoadSample(soundId);
        m_loopingSamples[soundId]->m_playbackData.volume = gEnvironmentVolume[distance];
        m_loopingSamples[soundId]->m_playbackData.repeat = true;
        PlaySample(m_loopingSamples[soundId]);
        m_activeSoundMask ^= 1 << m_activeSounds[slot].soundId;
    }
}

i32 gThisMaxY;
i32 gThisMinY;
i32 gUnusedAdvTemp;
i32 gAdvSpareFlag2;
i32 gAdvSpareInt;

void advManager::TeleportTo(i32 x, i32 y, i32) {
    b32 savedShow;
    i32 curFizzle;
    mapCell* location;
    mapCell* savedOldCell;
    i32 hold;
    i8 newTerrain;
    hero* mapHero;
    town* occupiedTown;

    savedShow = gShowIt;
    mapHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    location = GetCell(x, y);
    savedOldCell = GetCell(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y);
    if (mapHero->m_locationType == MAP_EVENT_TRIGGER(MAP_OBJECT_TOWN)) {
        occupiedTown = gGame->GetTown(mapHero->m_locationMetadata);
        occupiedTown->m_occupyingHeroId = TOWN_OCCUPYING_HERO_NONE;
    }
    if (savedOldCell->m_flags & MAP_CELL_HERO_CURSOR)
        MAP_CELL_SUBTRACT_FLAG(savedOldCell->m_flags, MAP_CELL_HERO_CURSOR);
    CompleteDraw(false);
    if (!gHumanPlayer[gCurPlayer]) {
        if (!gConfig.blackoutComputer && !gRemoteOn
            && (gGame->m_mapExtra[mapHero->m_x][mapHero->m_y] & gCurWatchPlayerHighBit))
            gShowIt = true;
        else
            gShowIt = false;
    }
    if (savedShow)
        HideRoute(true, true, true);
    // The current hero's position is the view's centre (DemobilizeCurrHero
    // takes it from there), and MoveHero moves the view with the hero
    // whether or not the move is shown. The original moved the view here
    // only when the teleport was shown: a computer player's Dimension Door
    // out of the watcher's sight was undone when the hero was put down,
    // after the spell and the movement were spent, and the hero could be
    // put down onto a boat or another hero, leaving a hero cell behind.
    m_cameraOffsetX = m_cameraOffsetY = 0;
    m_mapOriginX = x - ADVENTURE_VIEW_CENTER_X;
    m_mapOriginY = y - ADVENTURE_VIEW_CENTER_Y;
    if (gShowIt)
        DelayMilli(90);
    mapHero->m_x = x;
    mapHero->m_y = y;
    gGame->SetVisibility(
        m_mapOriginX + ADVENTURE_VIEW_CENTER_X,
        m_mapOriginY + ADVENTURE_VIEW_CENTER_Y,
        gCurPlayer,
        gHeroScoutRadius[mapHero->m_heroClass]
    );
    if (gShowIt) {
        location->m_flags |= MAP_CELL_HERO_CURSOR;
        gMouseManager->ReallyHidePointer();
        gWindowManager->SaveFizzleSource(
            BORDER_EDGE_SIZE,
            BORDER_EDGE_SIZE,
            ADVENTURE_VIEWPORT_INNER_WIDTH,
            ADVENTURE_VIEWPORT_INNER_HEIGHT
        );
        CompleteDraw(false);
        PollSound();
        curFizzle = TELEPORT_FIZZLE_TIME;
        if (!gHumanPlayer[gCurPlayer])
            curFizzle -= TELEPORT_REMOTE_FIZZLE_ADJUSTMENT;
        gWindowManager->FizzleForward(
            BORDER_EDGE_SIZE,
            BORDER_EDGE_SIZE,
            ADVENTURE_VIEWPORT_INNER_WIDTH,
            ADVENTURE_VIEWPORT_INNER_HEIGHT,
            FIZZLE_USE_DEFAULT_DELAY
        );
        PollSound();
        gMouseManager->ReallyShowPointer();
    }
    SetEnvironmentOrigin(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y, 1);
    newTerrain = CELL_TERRAIN(location);
    if (newTerrain != m_currentTerrain) {
        m_currentTerrain = newTerrain;
        PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
    }
    Reseed(0, 0);
    UpdateRadar(true, false);
    CompleteDraw(false);
    ForceNewHover();
}

void advManager::DimensionDoor(void) {
    hero* targetHero;
    heroWindow* window;
    i16 newX;
    i16 newY;
    mapCell* targetCell;

    window = new heroWindow(0, 0, "dimdoor.bin");
    if (window == NULL)
        MemError();
    window->m_posX = window->m_posY = 0;
    window->m_winWidth = LOGICAL_SCREEN_WIDTH;
    window->m_winHeight = LOGICAL_SCREEN_HEIGHT;
    for (widget* item = window->m_widgetListHead; item; item = item->m_next) {
        if (item->m_id == ADVENTURE_CONTROL_MAP_VIEW) {
            item->m_width = ADVENTURE_VIEWPORT_WIDTH;
            item->m_height = ADVENTURE_VIEWPORT_HEIGHT;
        } else if (item->m_x >= 480) {
            item->m_x += LOGICAL_SCREEN_WIDTH - 640;
        }
    }
    SetWinText(window, WINDOW_TEXT_DIMENSION_DOOR);
    gWindowManager->DoDialog(window, DimensionDoorHandler, false);
    delete window;
    targetHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    if (gWindowManager->m_dialogResult == TRAVEL_DIALOG_ACCEPT) {
        newX = m_mapOriginX + m_hoverCellX;
        newY = m_mapOriginY + m_hoverCellY;
        targetCell = GetCell(newX, newY);
        // The view can show the border beyond the map's edge. The original
        // tested the clamped cell (0,0) for such a target and teleported the
        // hero off the map (Tournament Edition X28); it now fails.
        if (newX < 0 || newY < 0 || newX >= MAP_CELL_GRID_SIZE || newY >= MAP_CELL_GRID_SIZE
            || (targetHero->IsEmbarked() && targetCell->m_tileIndex >= MAP_CELL_TILES_PER_TERRAIN)
            || (!targetHero->IsEmbarked()
                && targetCell->m_tileIndex < MAP_CELL_TILES_PER_TERRAIN)) {
            NormalDialog(
                localization::Tr("adventure.dimension_door.failed"),
                NORMAL_DIALOG_TYPE_OK,
                0x61,
                0x91
            );
            UpdateRadar(true, false);
        } else {
            PlayMusic(MUSIC_TRACK_TELEPORT);
            TeleportTo(newX, newY, 0);
            PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
        }
        gGame->GetHero(gCurPlayerData->m_currentHero)->UseSpell(SPELL_DIMENSION_DOOR);
    } else {
        UpdateRadar(true, false);
    }
}

struct tag_message gUSMsg;
struct tag_message gCDMsg;

void advManager::TownGate(void) {
    hero* targetHero;
    i32 dist;
    i32 townSlot;
    i32 i;
    i32 nearestDistance;

    nearestDistance = TOWN_PORTAL_DISTANCE_LIMIT;
    townSlot = TOWN_GATE_NO_TOWN;
    targetHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    if (targetHero->IsEmbarked()) {
        NormalDialog(localization::Tr("adventure.town_gate.land_required"), NORMAL_DIALOG_TYPE_OK);
        return;
    }
    for (i = 0; i < gCurPlayerData->m_townCount; i++) {
        dist = MANHATTAN_LENGTH(
            gGame->m_castleRecs[gCurPlayerData->m_townIds[i]].m_x - targetHero->m_x,
            gGame->m_castleRecs[gCurPlayerData->m_townIds[i]].m_y - targetHero->m_y
        );
        if (dist < nearestDistance) {
            nearestDistance = dist;
            townSlot = i;
        }
    }
    // The original went on after this message and teleported the hero into
    // the town named by the byte before the town list.
    if (townSlot == TOWN_GATE_NO_TOWN) {
        NormalDialog(localization::Tr("adventure.town_gate.no_town"), NORMAL_DIALOG_TYPE_OK);
        return;
    }
    if (gGame->m_castleRecs[gCurPlayerData->m_townIds[townSlot]].m_occupyingHeroId
        != TOWN_OCCUPYING_HERO_NONE) {
        NormalDialog(
            localization::Tr("adventure.town_gate.occupied"),
            NORMAL_DIALOG_TYPE_OK,
            NORMAL_DIALOG_ADVENTURE_X
        );
        return;
    }
    PlayMusic(MUSIC_TRACK_TELEPORT);
    TeleportTo(
        gGame->m_castleRecs[gCurPlayerData->m_townIds[townSlot]].m_x,
        gGame->m_castleRecs[gCurPlayerData->m_townIds[townSlot]].m_y,
        0
    );
    targetHero->UseSpell(SPELL_TOWN_GATE);
    gGame->m_castleRecs[gCurPlayerData->m_townIds[townSlot]].m_occupyingHeroId = targetHero->m_id;
    gGame->m_castleRecs[gCurPlayerData->m_townIds[townSlot]].GiveSpells();
    targetHero->m_locationType = MAP_EVENT_TRIGGER(MAP_OBJECT_TOWN);
    targetHero->m_locationMetadata = gCurPlayerData->m_townIds[townSlot];
    PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
}

void advManager::SummonBoat(void) {
    hero* summonHero;
    mapCell* destinationCell;
    b8 foundWaterCell;
    i16 boatIndex;
    i16 direction;
    b8 foundBoat;
    i8 summonHeroId;
    boatRecord* boatRec;
    mapCell* fromCell;
    i16 clipWidth;
    i16 clipX;
    i16 clipY;
    i16 clipHeight;

    summonHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    foundWaterCell = false;
    foundBoat = false;
    destinationCell = GetCell(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y);
    if (destinationCell->m_tileIndex < MAP_CELL_TILES_PER_TERRAIN)
        goto summon_done;
    for (direction = MAP_DIRECTION_FIRST; direction < MAP_DIRECTION_COUNT; direction++) {
        destinationCell = GetCell(
            m_mapOriginX + gNormalDirTable[direction].x
                + ADVENTURE_VIEW_CENTER_X,
            m_mapOriginY + gNormalDirTable[direction].y
                + ADVENTURE_VIEW_CENTER_Y
        );
        if (destinationCell->m_objectIndex == MAP_CELL_NO_FRAME
            && destinationCell->m_tileIndex < MAP_CELL_TILES_PER_TERRAIN) {
            foundWaterCell = true;
            break;
        }
    }
    if (foundWaterCell) {
        summonHeroId = gCurPlayerData->CurrentHero();
        for (boatIndex = 0; boatIndex < GAME_BOAT_COUNT; boatIndex++) {
            if (gGame->m_boatSlots[boatIndex] != GAME_TABLE_FREE
                && gGame->m_boats[boatIndex].heroId == (summonHeroId | BOAT_VACATED_FLAG)) {
                foundBoat = true;
                break;
            }
        }
        if (!foundBoat) {
            for (boatIndex = 0; boatIndex < GAME_BOAT_COUNT; boatIndex++) {
                if (gGame->m_boatSlots[boatIndex] != GAME_TABLE_FREE
                    && (gGame->m_boats[boatIndex].heroId & BOAT_VACATED_FLAG)
                    && gGame->m_boats[boatIndex].owner == gCurPlayer) {
                    foundBoat = true;
                    break;
                }
            }
        }
        if (foundBoat) {
            boatRec = &gGame->m_boats[boatIndex];
            fromCell = GetCell(boatRec->x, boatRec->y);
            gGame->RestoreCell(
                boatRec->x,
                boatRec->y,
                boatRec->savedTriggerType,
                boatRec->savedEventData,
                NULL,
                SUMMON_RESTORE_MODE
            );
            if (boatRec->x >= m_mapOriginX && boatRec->x < m_mapOriginX + ADVENTURE_VIEWPORT_CELLS_X
                && boatRec->y >= m_mapOriginY
                && boatRec->y < m_mapOriginY + ADVENTURE_VIEWPORT_CELLS_Y) {
                clipX = (boatRec->x - m_mapOriginX) * CELL_PIXELS - SUMMON_FIZZLE_X_OFFSET;
                if (clipX < BORDER_EDGE_SIZE)
                    clipX = BORDER_EDGE_SIZE;
                clipY = (boatRec->y - m_mapOriginY) * CELL_PIXELS - SUMMON_FIZZLE_Y_OFFSET;
                if (clipY < BORDER_EDGE_SIZE)
                    clipY = BORDER_EDGE_SIZE;
                clipWidth = SUMMON_FIZZLE_WIDTH;
                clipHeight = SUMMON_FIZZLE_HEIGHT;
                if (clipX + clipWidth >= BORDER_MIDDLE_END_X)
                    clipWidth = BORDER_MIDDLE_END_X - clipX;
                if (clipY + clipHeight >= BORDER_MIDDLE_END_Y)
                    clipHeight = BORDER_MIDDLE_END_Y - clipY;
                gWindowManager->SaveFizzleSource(clipX, clipY, clipWidth, clipHeight);
                CompleteDraw(m_mapOriginX, m_mapOriginY, false);
                gWindowManager
                    ->FizzleForward(clipX, clipY, clipWidth, clipHeight, FIZZLE_USE_DEFAULT_DELAY);
            }
            boatRec->x = m_mapOriginX + gNormalDirTable[direction].x
                         + ADVENTURE_VIEW_CENTER_X;
            boatRec->y = m_mapOriginY + gNormalDirTable[direction].y
                         + ADVENTURE_VIEW_CENTER_Y;
            boatRec->savedTriggerType = destinationCell->m_triggerType;
            boatRec->savedEventData = destinationCell->m_objectMetadata;
            destinationCell->m_triggerType = MAP_EVENT_TRIGGER(MAP_OBJECT_SHIP);
            destinationCell->m_objectMetadata = boatIndex;
            gWindowManager->SaveFizzleSource(
                SUMMON_TARGET_X,
                SUMMON_TARGET_Y,
                SUMMON_TARGET_WIDTH,
                SUMMON_TARGET_HEIGHT
            );
            CompleteDraw(m_mapOriginX, m_mapOriginY, false);
            gWindowManager->FizzleForward(
                SUMMON_TARGET_X,
                SUMMON_TARGET_Y,
                SUMMON_TARGET_WIDTH,
                SUMMON_TARGET_HEIGHT,
                FIZZLE_USE_DEFAULT_DELAY
            );
        }
    }

summon_done:
    UpdateScreen(false, false);
    Reseed(0, 0);
    if (!foundBoat)
        NormalDialog(
            localization::Tr("adventure.summon_boat.failed"),
            NORMAL_DIALOG_TYPE_OK,
            0x61,
            0x91
        );
}

void advManager::ShowRoute(b32 redraw, i32, b32 updateButton) {
    class hero* currentHero;
    b32 reachable;
    i32 fromDir;
    i32 remain;
    i32 mapX;
    i32 index;
    i32 mapY;
    i32 dir;
    i32 terr;
    i16 flagCommand;

    reachable = false;
    if (!gThisNetHumanPlayer[gCurPlayer] && (!gDebugLevel || !gShowComputerRoute))
        return;
    if (gCurPlayerData->m_currentHero == HERO_ID_NONE) {
        HideRoute(redraw, false, true);
        return;
    }
    currentHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    if (currentHero->m_destinationX == HERO_DESTINATION_NONE) {
        HideRoute(redraw, true, true);
        return;
    }
    gSearchArray->BuildPath(
        currentHero->m_x,
        currentHero->m_y,
        currentHero->m_destinationX,
        currentHero->m_destinationY,
        SEARCH_UNLIMITED_COST
    );
    if (gSearchArray->m_pathLength > 0) {
        memset(m_routeMap, 0, MAP_CELL_GRID_SIZE * MAP_CELL_GRID_SIZE);
        m_routeShown = true;
        remain = currentHero->m_remainingMobility;
        mapX = currentHero->m_x;
        mapY = currentHero->m_y;
        for (index = gSearchArray->m_pathLength - 1; index >= 0; --index) {
            dir = gSearchArray->m_directions[index];
            terr = CELL_TERRAIN(GetCell(mapX, mapY));
            const int cost = CalcTerrainCost(terr, dir & MAP_DIRECTION_DIAGONAL_BIT, remain,
                                             currentHero->m_heroClass);
            const bool canReach = remain >= cost;
            mapX += gNormalDirTable[dir].x;
            mapY += gNormalDirTable[dir].y;
            remain = canReach ? MobilityAfterStep(remain, cost, CELL_TERRAIN(GetCell(mapX, mapY)),
                                                  currentHero->m_heroClass) : -1;
            if (index == 0) {
                m_routeMap[mapX + mapY * MAP_CELL_GRID_SIZE] = ROUTE_CELL_DESTINATION;
            } else {
                fromDir = gSearchArray->m_directions[index - 1];
                m_routeMap[mapX + mapY * MAP_CELL_GRID_SIZE] = gRouteFrame[fromDir][dir];
            }
            if (canReach) {
                m_routeMap[mapX + mapY * MAP_CELL_GRID_SIZE] += ROUTE_CELL_REACHABLE_OFFSET;
                reachable = true;
            }
        }
        if (updateButton) {
            flagCommand = reachable ? WIDGET_COMMAND_CLEAR_FLAGS : WIDGET_COMMAND_SET_FLAGS;
            gWindowManager->BroadcastMessage(
                MESSAGE_WIDGET,
                flagCommand,
                ADVENTURE_CONTROL_CONTINUE_ROUTE,
                WIDGET_FLAG_UPDATE | WIDGET_FLAG_DIMMED
            );
        }
    } else {
        HideRoute(redraw, true, true);
    }
    if (redraw) {
        CompleteDraw(false);
        gMouseManager->ReallyHidePointer();
        UpdateScreen(false, false);
        gMouseManager->ReallyShowPointer();
    }
}

void advManager::HideRoute(b32 redraw, b32 clearDestination, b32 updateButton) {
    hero* currentHero;

    if (!gThisNetHumanPlayer[gCurPlayer] && (!gDebugLevel || !gShowComputerRoute))
        return;

    if (updateButton)
        gWindowManager->BroadcastMessage(
            MESSAGE_WIDGET,
            WIDGET_COMMAND_SET_FLAGS,
            ADVENTURE_CONTROL_CONTINUE_ROUTE,
            WIDGET_FLAG_UPDATE | WIDGET_FLAG_DIMMED
        );

    if (clearDestination && gCurPlayerData->m_currentHero != HERO_ID_NONE) {
        currentHero = gGame->GetHero(gCurPlayerData->m_currentHero);
        currentHero->m_destinationX = HERO_DESTINATION_NONE;
        currentHero->m_destinationY = HERO_DESTINATION_NONE;
    }

    if (!m_routeShown)
        return;

    m_routeShown = false;
    if (redraw) {
        CompleteDraw(false);
        UpdateScreen(false, false);
    }
}

void advManager::CheckDimHero(void) {
    if (!gThisNetHumanPlayer[gCurPlayer] || gCurPlayerData->CurrentHero() == HERO_ID_NONE)
        return;
    if (!gGame->IsMobile(gCurPlayerData->CurrentHero())) {
        ShowRoute(true, 0, false);
        UpdateHeroLocators(true, true);
        gAdvManager->CheckDimNextHeroBut();
    }
}

void advManager::ToggleHeroWaiting(i8 heroId) {
    if (heroId == HERO_ID_NONE || gRemoteOn)
        return;
    gGame->GetHero(heroId)->m_eventFlags ^= HERO_EVENT_WAITING;
    UpdateHeroLocators(true, true);
    CheckDimNextHeroBut();
}

void advManager::CheckDimNextHeroBut(void) {
    i16 flagCommand;

    flagCommand = gThisNetHumanPlayer[gCurPlayer] && gCurPlayerData->HasMobileHero()
                      ? static_cast<i16>(WIDGET_COMMAND_CLEAR_FLAGS)
                      : static_cast<i16>(WIDGET_COMMAND_SET_FLAGS);
    gWindowManager->BroadcastMessage(
        MESSAGE_WIDGET,
        flagCommand,
        ADVENTURE_CONTROL_NEXT_HERO,
        WIDGET_FLAG_UPDATE | WIDGET_FLAG_DIMMED
    );
}

void advManager::SeedTo(i32 targetX, i32 targetY) {
    hero* currentHero;

    if (!gThisNetHumanPlayer[gCurPlayer])
        return;
    if (gCurPlayerData->m_currentHero == HERO_ID_NONE)
        return;

    currentHero = gGame->GetHero(gCurPlayerData->m_currentHero);
    if (!gSeedingValid)
        gSearchArray->SeedPosition(
            currentHero->m_x,
            currentHero->m_y,
            m_cursorDirection,
            SEARCH_UNLIMITED_COST,
            m_cursorType == ADVMGR_HERO_ICON_BOAT,
            false,
            currentHero->m_remainingMobility,
            currentHero->m_heroClass,
            targetX,
            targetY,
            false,
            true
        );
    else if (!gFullySeeded)
        gSearchArray->SeedPosition(
            currentHero->m_x,
            currentHero->m_y,
            m_cursorDirection,
            SEARCH_UNLIMITED_COST,
            m_cursorType == ADVMGR_HERO_ICON_BOAT,
            false,
            currentHero->m_remainingMobility,
            currentHero->m_heroClass,
            targetX,
            targetY,
            true,
            true
        );
}

void advManager::ForceNewHover(void) {
    struct tag_message msg{};

    if (!gThisNetHumanPlayer[gCurPlayer])
        return;
    m_hoverCellX = CURSOR_INVALID_POSITION;
    msg.id = ADVENTURE_CONTROL_MAP_VIEW;
    gMouseManager->MouseCoords(msg.mouseX, msg.mouseY);
    ProcessHover(&msg);
}

void advManager::ScreenScroll(i8 direction, b32 updatePointer) {
    i16 yOrigin;
    i16 xOrigin;

    xOrigin = m_mapOriginX;
    yOrigin = m_mapOriginY;
    gLastScrollTime = KBTickCount();

    switch (direction) {
        case MAP_DIRECTION_NORTH:
            --yOrigin;
            break;
        case MAP_DIRECTION_NORTH_EAST:
            ++xOrigin;
            --yOrigin;
            break;
        case MAP_DIRECTION_EAST:
            ++xOrigin;
            break;
        case MAP_DIRECTION_SOUTH_EAST:
            ++xOrigin;
            ++yOrigin;
            break;
        case MAP_DIRECTION_SOUTH:
            ++yOrigin;
            break;
        case MAP_DIRECTION_SOUTH_WEST:
            --xOrigin;
            ++yOrigin;
            break;
        case MAP_DIRECTION_WEST:
            --xOrigin;
            break;
        case MAP_DIRECTION_NORTH_WEST:
            --xOrigin;
            --yOrigin;
            break;
    }

    if (updatePointer)
        gMouseManager->SetPointer(
            direction + HOVER_SCROLL_FRAME_FIRST
        );

    PanMap(xOrigin, yOrigin);
}

void advManager::PanMap(i16 xOrigin, i16 yOrigin) {
    if (xOrigin < SCROLL_MIN_ORIGIN_X)
        xOrigin = SCROLL_MIN_ORIGIN_X;
    if (xOrigin > SCROLL_MAX_ORIGIN_X)
        xOrigin = SCROLL_MAX_ORIGIN_X;
    if (yOrigin < SCROLL_MIN_ORIGIN_Y)
        yOrigin = SCROLL_MIN_ORIGIN_Y;
    if (yOrigin > SCROLL_MAX_ORIGIN_Y)
        yOrigin = SCROLL_MAX_ORIGIN_Y;

    if (xOrigin != m_mapOriginX || yOrigin != m_mapOriginY || HasCameraOffset()) {
        platform::BeginDisplayUpdate();
        DemobilizeCurrHero(false);
        m_cameraOffsetX = m_cameraOffsetY = 0;
        m_mapOriginX = xOrigin;
        m_mapOriginY = yOrigin;
        UpdateRadar(true, false);
        CompleteDraw(false);
        UpdateScreen(false, false);
        platform::EndDisplayUpdate();
    }
}

bool advManager::BeginMapDrag(i16 x, i16 y) {
    if (gHeroMoving || !gShowIt || gAllBlack || !gThisNetHumanPlayer[gCurPlayer]
        || x < BORDER_EDGE_SIZE || x >= BORDER_MIDDLE_END_X
        || y < BORDER_EDGE_SIZE || y >= BORDER_MIDDLE_END_Y)
        return false;

    m_dragX = x;
    m_dragY = y;
    m_dragStartPixelX = m_mapOriginX * CELL_PIXELS + m_cameraOffsetX;
    m_dragStartPixelY = m_mapOriginY * CELL_PIXELS + m_cameraOffsetY;
    m_mapDragging = false;
    return true;
}

bool advManager::DragMap(i16 x, i16 y) {
    constexpr int threshold = 6;
    const bool starting = !m_mapDragging;
    if (starting && abs(x - m_dragX) < threshold && abs(y - m_dragY) < threshold)
        return false;

    const int minX = SCROLL_MIN_ORIGIN_X * CELL_PIXELS;
    const int minY = SCROLL_MIN_ORIGIN_Y * CELL_PIXELS;
    const int pixelX = std::clamp(m_dragStartPixelX + m_dragX - x,
                                  minX, SCROLL_MAX_ORIGIN_X * CELL_PIXELS);
    const int pixelY = std::clamp(m_dragStartPixelY + m_dragY - y,
                                  minY, SCROLL_MAX_ORIGIN_Y * CELL_PIXELS);
    // Round to the closest tile. The rest can be sampled from the 16 pixels
    // of map normally hidden under each side of the frame.
    const int originX = (pixelX - minX + BORDER_EDGE_SIZE) / CELL_PIXELS + SCROLL_MIN_ORIGIN_X;
    const int originY = (pixelY - minY + BORDER_EDGE_SIZE) / CELL_PIXELS + SCROLL_MIN_ORIGIN_Y;
    const int offsetX = pixelX - originX * CELL_PIXELS;
    const int offsetY = pixelY - originY * CELL_PIXELS;
    const bool rebuild = starting || originX != m_mapOriginX || originY != m_mapOriginY;
    if (!rebuild && offsetX == m_cameraOffsetX && offsetY == m_cameraOffsetY)
        return true;

    m_mapDragging = true;
    m_cameraOffsetX = offsetX;
    m_cameraOffsetY = offsetY;
    platform::BeginDisplayUpdate();
    if (rebuild) {
        if (starting)
            DemobilizeCurrHero(false);
        m_mapOriginX = originX;
        m_mapOriginY = originY;
        CompleteDraw(false);
        UpdateRadar(true, false);
    } else {
        DrawCameraFrame();
    }
    gWindowManager->UpdateScreenRegion(BORDER_EDGE_SIZE, BORDER_EDGE_SIZE,
                                       ADVENTURE_VIEWPORT_INNER_WIDTH, ADVENTURE_VIEWPORT_INNER_HEIGHT);
    platform::EndDisplayUpdate();
    return true;
}

void advManager::DrawCameraFrame() {
    m_cameraFrame->CopyTo(gWindowManager->m_screen, BORDER_EDGE_SIZE, BORDER_EDGE_SIZE,
                          BORDER_EDGE_SIZE + m_cameraOffsetX, BORDER_EDGE_SIZE + m_cameraOffsetY,
                          ADVENTURE_VIEWPORT_INNER_WIDTH, ADVENTURE_VIEWPORT_INNER_HEIGHT);
}

bool advManager::MapCellAtScreenPoint(i16 x, i16 y, i16& cellX, i16& cellY) const {
    if (x < BORDER_EDGE_SIZE || x >= BORDER_MIDDLE_END_X || y < BORDER_EDGE_SIZE || y >= BORDER_MIDDLE_END_Y)
        return false;
    cellX = (x + m_cameraOffsetX) / CELL_PIXELS;
    cellY = (y + m_cameraOffsetY) / CELL_PIXELS;
    return true;
}

void advManager::EndMapDrag() {
    if (m_mapDragging) {
        m_resumeEdgeScrollOnExit = MouseInScrollZone();
        m_hoverCellX = m_hoverCellY = CURSOR_INVALID_POSITION;
        gInputManager->m_forceMouseMove = true;
    }
    m_dragX = m_dragY = -1;
    m_mapDragging = false;
}

void advManager::CheckScreenScroll(void) {
    if (m_resumeEdgeScrollOnExit) {
        if (MouseInScrollZone())
            return;
        m_resumeEdgeScrollOnExit = false;
    }
    i16 mouseX;
    i16 mouseY;
    i32 oldMapX;
    i32 oldMapY;

    if (KBTickCount() - gLastScrollTime > SCROLL_TICK_INTERVAL) {
        gLastScrollTime = KBTickCount();
        oldMapX = m_mapOriginX;
        oldMapY = m_mapOriginY;
        gMouseManager->MouseCoords(mouseX, mouseY);

        if (mouseX >= 0 && mouseX < LOGICAL_SCREEN_WIDTH && mouseY >= 0
            && mouseY < LOGICAL_SCREEN_HEIGHT) {
            if (mouseX < SCROLL_BORDER) {
                if (mouseY < SCROLL_BORDER)
                    ScreenScroll(MAP_DIRECTION_NORTH_WEST, true);
                else if (mouseY > LOGICAL_SCREEN_HEIGHT - SCROLL_BORDER)
                    ScreenScroll(MAP_DIRECTION_SOUTH_WEST, true);
                else
                    ScreenScroll(MAP_DIRECTION_WEST, true);
            } else if (mouseX > LOGICAL_SCREEN_WIDTH - SCROLL_BORDER - 1) {
                if (mouseY < SCROLL_BORDER)
                    ScreenScroll(MAP_DIRECTION_NORTH_EAST, true);
                else if (mouseY > LOGICAL_SCREEN_HEIGHT - SCROLL_BORDER)
                    ScreenScroll(MAP_DIRECTION_SOUTH_EAST, true);
                else
                    ScreenScroll(MAP_DIRECTION_EAST, true);
            } else if (mouseY < SCROLL_BORDER) {
                ScreenScroll(MAP_DIRECTION_NORTH, true);
            } else if (mouseY > LOGICAL_SCREEN_HEIGHT - SCROLL_BORDER) {
                ScreenScroll(MAP_DIRECTION_SOUTH, true);
            }
        }

        if (gMouseManager->m_cursorFrame >= HOVER_SCROLL_FRAME_FIRST
            && gMouseManager->m_cursorFrame < HOVER_SCROLL_FRAME_END && oldMapX == m_mapOriginX
            && oldMapY == m_mapOriginY)
            gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
    }
}

b32 advManager::MouseInScrollZone(void) {
    i16 mouseX;
    i16 mouseY;

    gMouseManager->MouseCoords(mouseX, mouseY);
    if (mouseX >= 0 && mouseX < LOGICAL_SCREEN_WIDTH && mouseY >= 0
        && mouseY < LOGICAL_SCREEN_HEIGHT) {
        if (mouseX < SCROLL_BORDER || mouseX > LOGICAL_SCREEN_WIDTH - SCROLL_BORDER - 1
            || mouseY < SCROLL_BORDER || mouseY > LOGICAL_SCREEN_HEIGHT - SCROLL_BORDER) {
            return true;
        }
    }
    return false;
}

void advManager::SetInitialMapOrigin(void) {
    m_cameraOffsetX = m_cameraOffsetY = 0;
    i16 x;
    i16 y;
    game* gameStateItem;
    hero* heroPtr;
    town* townPointer;
    town* firstTownPointer;

    gWindowManager->BroadcastMessage(
        MESSAGE_WIDGET,
        WIDGET_COMMAND_SET_FLAGS,
        ADVENTURE_CONTROL_CONTINUE_ROUTE,
        WIDGET_FLAG_UPDATE | WIDGET_FLAG_DIMMED
    );
    m_hoverCellX = m_hoverCellY = 0;
    DemobilizeCurrHero();
    m_cursorActive = false;
    gHeroMoving = false;
    if (gCurPlayerData->CurrentTown() != GAME_TOWN_NONE) {
        townPointer = gGame->GetTown(gCurPlayerData->m_currentTown);
        m_mapOriginX = townPointer->m_x - ADVENTURE_VIEW_CENTER_X;
        m_mapOriginY = townPointer->m_y - ADVENTURE_VIEW_CENTER_Y;
    } else if (gCurPlayerData->CurrentHero() != HERO_ID_NONE) {
        MobilizeCurrHero(false);
    } else if (gCurPlayerData->m_heroCount > 0) {
        heroPtr = &gGame->m_heroRecs[gCurPlayerData->m_heroIds[0]];
        m_mapOriginX = heroPtr->m_x - ADVENTURE_VIEW_CENTER_X;
        m_mapOriginY = heroPtr->m_y - ADVENTURE_VIEW_CENTER_Y;
    } else if (gCurPlayerData->m_townCount > 0) {
        firstTownPointer = &gGame->m_castleRecs[gCurPlayerData->m_townIds[0]];
        m_mapOriginX = firstTownPointer->m_x - ADVENTURE_VIEW_CENTER_X;
        m_mapOriginY = firstTownPointer->m_y - ADVENTURE_VIEW_CENTER_Y;
    } else {
        m_mapOriginX = 0;
        m_mapOriginY = 0;
    }
    m_currentTerrain = gGroundToTerrain
        [GetCell(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y)
             ->m_tileIndex];
    PlayMusic(TERRAIN_MUSIC_TRACK(m_currentTerrain));
    SetEnvironmentOrigin(m_mapOriginX + ADVENTURE_VIEW_CENTER_X, m_mapOriginY + ADVENTURE_VIEW_CENTER_Y, 1);
    gMouseManager->MouseCoords(x, y);
    gMouseManager->WarpPointer(x - 20, y - 20);
    Reseed(0, 0);
    CheckDimNextHeroBut();
}

void advManager::LoadRemote(void) {
    gMouseManager->ReallyHidePointer();
    if (gThisNetHumanPlayer[gCurPlayer])
        gMouseManager->SetPointer("advmice.mse", ADVENTURE_POINTER_DEFAULT);
    gGame->LoadGame("REMOTE.GAM", false, true);
    if (gThisNetHumanPlayer[gCurPlayer])
        gGame->CancelComputerScreen();
    gGame->DoNewTurn();
    UpdateHeroLocators(true, true);
    UpdateTownLocators(true, true);
    UpdateRadar(true, false);
    gMouseManager->ReallyShowPointer();
    UpdBottomView(true, true, true);
    if ((gGame->m_day != 1 || (gGame->m_week == 1 && gGame->m_month == 1)) && gRemoteOn
        && gThisNetHumanPlayer[gCurPlayer] && gForceSwitchMusic == FORCED_MUSIC_IDLE) {
        PlayMusic(MUSIC_TRACK_NETWORK_TURN);
        gForceSwitchMusic = KBTickCount();
    }
    gAdvManager->ForceNewHover();
}

RemoteMessage* advManager::CheckHandleNet(void) {
    RemoteMessage* receivedPacket;
    b32 exitedFlag;
    RemoteSaveHeader saveHeader;
    RemotePlayerExit playerExit;

    receivedPacket = GetRemoteData(true);
    if (receivedPacket && receivedPacket->type == REMOTE_MESSAGE_RELIABLE) {
        RecordReader payload = RemotePayloadReader(*receivedPacket);
        switch (receivedPacket->command) {
            case BOX_REMOTE_SAVE:
                ReadRemoteSaveHeader(payload, saveHeader);
                exitedFlag = saveHeader.playerExited;
                if (!gGame->ReceiveSaveGame(saveHeader.saveSize, receivedPacket->sender))
                    ShutDown(NULL);
                if (exitedFlag)
                    ReceiveRemotePlayerExit(receivedPacket->sender, 0, true, false);
                LoadRemote();
                break;
            case REMOTE_COMMAND_CHAT:
                PopNetBox(receivedPacket->payload.data);
                break;
            case REMOTE_COMMAND_HERO_TOWN_DATA:
                if (gInCombat)
                    return receivedPacket;
                else
                    DoNetCombat(receivedPacket);
                break;
            case REMOTE_COMMAND_PLAYER_EXIT:
                ReadRemotePlayerExit(payload, playerExit);
                ReceiveRemotePlayerExit(playerExit.position, playerExit.hadControl, false, false);
                break;
            default:
                return receivedPacket;
        }
    }
    return NULL;
}

i16 advManager::CheckHandleNetPlayerWait(struct tag_message& message, b8 doMain) {
    if (message.type == MESSAGE_MOUSE_MOVE)
        gMouseManager->Main(message);

    CheckDoMain(1, doMain);
    if (message.type == MESSAGE_KEY_DOWN) {
        switch (message.keyCode) {
            case INPUT_SCAN_F2:
                PopNetBox(NULL);
                break;

            case INPUT_SCAN_Q:
                if (message.modifiers & MESSAGE_MODIFIER_CONTROL_KEYS) {
                    message.type = MESSAGE_EXECUTIVE;
                    message.executiveCommand = EXECUTIVE_COMMAND_TERMINATE_LOOP;
                    return MESSAGE_DISPATCH_FORWARD;
                }
            default:
                break;
        }
    }

    UpdBottomView(false, true, true);
    return MESSAGE_DISPATCH_CONTINUE;
}

void advManager::TrimLoopingSounds(i32 maxSamples) {
    if (gHighMemBuffer > 0)
        maxSamples += gHighMemBuffer / HIGH_MEMORY_BUFFER_DIVISOR;

    if (maxSamples >= ADVMGR_ENVIRONMENT_SOUND_COUNT)
        return;

    i8 keep[ADVMGR_ENVIRONMENT_SOUND_COUNT];
    i32 loaded = 0;
    memset(keep, 0, sizeof(keep));

    i32 i;
    for (i = 0; i < ADVMGR_ACTIVE_SOUND_COUNT; ++i) {
        if (m_activeSounds[i].soundId >= 0
            && m_activeSounds[i].soundId < ADVMGR_ENVIRONMENT_SOUND_COUNT)
            ++keep[m_activeSounds[i].soundId];
    }

    for (i = 0; i < ADVMGR_ENVIRONMENT_SOUND_COUNT; ++i) {
        if (keep[i] != 0)
            ++loaded;
    }

    if (loaded < maxSamples) {
        for (i = 0; i < ADVMGR_ENVIRONMENT_SOUND_COUNT; ++i) {
            if (keep[i] == 0 && m_loopingSamples[i] != NULL) {
                ++keep[i];
                ++loaded;
                if (loaded >= maxSamples)
                    goto disposeSamples;
            }
        }
    }

disposeSamples:
    for (i = 0; i < ADVMGR_ENVIRONMENT_SOUND_COUNT; ++i) {
        if (m_loopingSamples[i] != NULL && keep[i] == 0) {
            gResourceManager->Dispose(m_loopingSamples[i]);
            m_loopingSamples[i] = NULL;
        }
    }
}

void advManager::DisableButtons(void) {
    if (gAdvManager->m_active != 1)
        return;
    struct tag_message message;
    SET_ADVENTURE_BUTTON_FLAGS(message, m_adventureWindow, WIDGET_COMMAND_CLEAR_FLAGS);
}

void advManager::EnableButtons(void) {
    if (gAdvManager->m_active != 1)
        return;
    struct tag_message message;
    SET_ADVENTURE_BUTTON_FLAGS(message, m_adventureWindow, WIDGET_COMMAND_SET_FLAGS);
}

void advManager::SaveAdventureBorder(void) {
    if (m_adventureBorder != NULL)
        return;

    m_adventureBorder = static_cast<u8*>(malloc(BORDER_BUFFER_SIZE));
    u8* savedPixels = m_adventureBorder;
    u8* screen = gWindowManager->m_screen->m_pixels;
    i32 row;
    for (row = 0; row < BORDER_EDGE_SIZE; ++row) {
        memcpy(savedPixels, screen, ADVENTURE_VIEWPORT_WIDTH);
        screen += LOGICAL_SCREEN_WIDTH;
        savedPixels += ADVENTURE_VIEWPORT_WIDTH;
    }
    for (row = BORDER_EDGE_SIZE; row < BORDER_MIDDLE_END_Y; ++row) {
        memcpy(savedPixels, screen, BORDER_SIDE_BYTES);
        memcpy(savedPixels + BORDER_SIDE_BYTES, screen + BORDER_MIDDLE_END_X, BORDER_SIDE_BYTES);
        screen += LOGICAL_SCREEN_WIDTH;
        savedPixels += BORDER_SAVED_SIDE_BYTES;
    }
    for (row = BORDER_MIDDLE_END_Y; row < LOGICAL_SCREEN_HEIGHT; ++row) {
        memcpy(savedPixels, screen, ADVENTURE_VIEWPORT_WIDTH);
        screen += LOGICAL_SCREEN_WIDTH;
        savedPixels += ADVENTURE_VIEWPORT_WIDTH;
    }
}

void advManager::DrawAdventureBorder(void) {
    u8* savedPixels;
    u8* screen;
    i32 row;

    if (m_adventureBorder == NULL)
        return;
    if (gNoBorder)
        return;

    screen = gWindowManager->m_screen->m_pixels;
    savedPixels = m_adventureBorder;
    for (row = 0; row < BORDER_EDGE_SIZE; ++row) {
        memcpy(screen, savedPixels, ADVENTURE_VIEWPORT_WIDTH);
        screen += LOGICAL_SCREEN_WIDTH;
        savedPixels += ADVENTURE_VIEWPORT_WIDTH;
    }
    for (row = BORDER_EDGE_SIZE; row < BORDER_MIDDLE_END_Y; ++row) {
        memcpy(screen, savedPixels, BORDER_SIDE_BYTES);
        memcpy(screen + BORDER_MIDDLE_END_X, savedPixels + BORDER_SIDE_BYTES, BORDER_SIDE_BYTES);
        screen += LOGICAL_SCREEN_WIDTH;
        savedPixels += BORDER_SAVED_SIDE_BYTES;
    }
    for (row = BORDER_MIDDLE_END_Y; row < LOGICAL_SCREEN_HEIGHT; ++row) {
        memcpy(screen, savedPixels, ADVENTURE_VIEWPORT_WIDTH);
        screen += LOGICAL_SCREEN_WIDTH;
        savedPixels += ADVENTURE_VIEWPORT_WIDTH;
    }
}

i32 gLimitUpdMinX = UPDATE_NONE;
i32 gCurBottomViewEnemy = BOTTOM_VIEW_NO_ENEMY;
i32 gLastHourGlassPhase = 1;
i32 gUnusedAdventureValue = 28;
class heroWindow* gAdventurePanel;
i32 gFrameStep;
char gArmySizeName[12];
i32 gLimitUpdMaxX;
i32 gLimitUpdMaxY;
b8 gPrefsChanged;
i32 gLimitUpdMinY;
i8 gComboDraw[64][64];
b8 gFreshSave;
i32 gLastAnimFrame;
const i32 gEnvironmentVolume[8] = {127, 96, 63, 31, 21};
