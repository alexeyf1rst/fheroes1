#include <H1/Ints.h>

#include <BASE/border.h>
#include <BASE/font.h>
#include <BASE/goldButton.h>
#include <BASE/heroWindow.h>
#include <BASE/heroWindowManager.h>
#include <BASE/icon.h>
#include <BASE/message.h>
#include <BASE/resourceManager.h>
#include <BASE/widget.h>
#include <SOURCE/army.h>
#include <SOURCE/armyGroup.h>
#include <SOURCE/bankBox.h>
#include <SOURCE/KB.h>
#include <SOURCE/playerData.h>
#include <SOURCE/strip.h>
#include <SOURCE/townManager.h>

#include <stdio.h>

namespace {

void DrawTownInset(bitmap* screen, int x, int y, int width, int height, int background) {
    FillBitmapArea(screen, x, y, width, height, goldButton::MatchGold(31, 23, 13, true));
    FillBitmapArea(screen, x + 1, y + 1, width - 2, height - 2, background);
}

class TownTreasury : public textWidget {
public:
    TownTreasury(const playerData& player)
        : textWidget(4, 5, 88, 165, strdup(""), "smalfont.fnt", TEXT_WIDGET_PLAIN_COLOR, WIDGET_ID_NONE, WIDGET_KIND_TEXT),
          m_player(player), m_resources(gResourceManager->GetIcon("resource.icn")) {
        m_flags &= ~WIDGET_FLAG_ENABLED;
    }

    ~TownTreasury() override { gResourceManager->Dispose(m_resources); }

    void Draw() override {
        const int x = m_owner->m_posX + m_x;
        const int y = m_owner->m_posY + m_y;
        auto* screen = gWindowManager->m_screen;
        const int stone = goldButton::MatchGold(13, 12, 10, true);
        const int rowColor = goldButton::MatchGold(19, 17, 13, true);
        const int goldColor = goldButton::MatchGold(26, 19, 8, true);
        DrawTownInset(screen, x, y, m_width, m_height, stone);
        char title[32];
        snprintf(title, sizeof(title), "%s", localization::Tr("town.treasury.title"));
        m_font->DrawBoundedString(title, x + 3, y + 5, m_width - 6, m_font->m_height, 1, FONT_ALIGN_CENTER);
        constexpr int order[] = {RESOURCE_WOOD, RESOURCE_ORE, RESOURCE_MERCURY, RESOURCE_SULFUR,
                                 RESOURCE_CRYSTAL, RESOURCE_GEMS, RESOURCE_GOLD};
        bitmap sprite(BITMAP_TYPE_MEMORY, 80, 40);
        for (int row = 0; row < RESOURCE_COUNT; ++row) {
            const int resource = order[row];
            const bool gold = resource == RESOURCE_GOLD;
            const int rowY = y + 20 + row * 19 + (gold ? 4 : 0);
            const int rowHeight = gold ? 23 : 18;
            FillBitmapArea(screen, x + 3, rowY, m_width - 6, rowHeight, gold ? goldColor : rowColor);
            DrawResource(sprite, screen, resource, x + 5, rowY, 24, rowHeight, gold ? goldColor : rowColor);
            char amount[16];
            snprintf(amount, sizeof(amount), "%d", m_player.m_resources[resource]);
            m_font->DrawBoundedString(amount, x + 31, rowY + (rowHeight - m_font->m_height) / 2, m_width - 37,
                                      m_font->m_height, 1, FONT_ALIGN_RIGHT);
        }
    }

private:
    void DrawResource(bitmap& sprite, bitmap* screen, int resource, int x, int y, int width, int height,
                      int background) const {
        if (resource >= m_resources->m_frameCount)
            return;
        const auto& frame = m_resources->m_frames[resource];
        if (!frame.w || !frame.h || frame.w > sprite.m_width || frame.h > sprite.m_height)
            return;
        FillBitmapArea(&sprite, 0, 0, frame.w, frame.h, background);
        IconToBitmap(m_resources, &sprite, -frame.x, -frame.y, resource, ICON_DRAW_OFFSET_FULL);
        const double scale = std::min({1.0, static_cast<double>(width) / frame.w,
                                       static_cast<double>(height - 4) / frame.h});
        const int drawWidth = std::max(1, static_cast<int>(frame.w * scale));
        const int drawHeight = std::max(1, static_cast<int>(frame.h * scale));
        x += (width - drawWidth) / 2;
        y += (height - drawHeight) / 2;
        for (int row = 0; row < drawHeight; ++row) {
            const u8* source = sprite.m_pixels + (row * frame.h / drawHeight) * sprite.m_width;
            u8* destination = screen->m_pixels + (y + row) * screen->m_width + x;
            for (int column = 0; column < drawWidth; ++column)
                destination[column] = source[column * frame.w / drawWidth];
        }
    }

    const playerData& m_player;
    icon* m_resources;
};

}

strip::strip(
    i16 x,
    i16 y,
    i8 stripType,
    i16 portraitIconId,
    i8 portraitFrame,
    class armyGroup* troops,
    i16 firstBorderId,
    b32 drawWindow
) {
    m_selectedSlot = STRIP_SLOT_NONE;
    m_x = x;
    m_y = y;
    m_stripType = stripType;
    m_portraitIcon = gResourceManager->GetIcon(portraitIconId);
    m_portraitFrame = portraitFrame;
    m_army = troops;
    m_stripIcon = gResourceManager->GetIcon("strip.icn");
    m_monsterIcon = gResourceManager->GetIcon("monsters.icn");
    m_font = gResourceManager->GetFont("smalfont.fnt");
    m_window = new heroWindow(
        m_x, m_y, STRIP_WINDOW_WIDTH, STRIP_WINDOW_HEIGHT,
        WINDOW_FLAG_STRIP_WINDOW | WINDOW_FLAG_OWNS_WIDGETS
    );
    if (!m_window)
        MemError();
    if (m_army) {
        auto* portrait = new border(
            STRIP_PORTRAIT_X,
            STRIP_CONTENT_Y,
            STRIP_PORTRAIT_BORDER_WIDTH,
            STRIP_BORDER_HEIGHT,
            firstBorderId,
            WIDGET_KIND_TRANSPARENT,
            0,
            NULL
        );
        if (!portrait)
            MemError();
        m_window->AddWidget(portrait, WINDOW_Z_ORDER_APPEND);
        for (i16 i = 0; i < ARMY_GROUP_SLOT_COUNT; i++) {
            auto* slot = new border(
                i * STRIP_ARMY_X_STEP + STRIP_ARMY_FIRST_X,
                STRIP_CONTENT_Y,
                STRIP_ARMY_BORDER_WIDTH,
                STRIP_BORDER_HEIGHT,
                firstBorderId + i + 1,
                WIDGET_KIND_TRANSPARENT,
                0,
                NULL
            );
            if (!slot)
                MemError();
            m_window->AddWidget(slot, WINDOW_Z_ORDER_APPEND);
        }
    }
    DrawIcons(drawWindow);
    gWindowManager->AddWindow(m_window, WINDOW_Z_ORDER_APPEND, drawWindow);
}

strip::~strip() {
    gWindowManager->RemoveWindow(m_window);
    delete m_window;
    gResourceManager->Dispose(m_font);
    gResourceManager->Dispose(m_stripIcon);
    gResourceManager->Dispose(m_monsterIcon);
    gResourceManager->Dispose(m_portraitIcon);
}

void strip::Draw(void) {
    DrawIcons(true);
    gWindowManager->UpdateScreenRegion(m_x, m_y, STRIP_WINDOW_WIDTH, STRIP_WINDOW_HEIGHT);
}

void strip::DrawIcons(b8 drawWindow) {
    m_portraitIcon->DrawToBuffer(
        m_x + STRIP_PORTRAIT_X,
        m_y + STRIP_CONTENT_Y,
        m_portraitFrame,
        ICON_DRAW_NORMAL,
        ICON_DRAW_OFFSET_FULL
    );
    auto* screen = gWindowManager->m_screen;
    const int inset = goldButton::MatchGold(12, 10, 7, true);
    const char* caption = nullptr;
    if (m_stripType == TOWN_CREST_FRAME_WITHOUT_HERO)
        caption = localization::Tr("town.army.garrison");
    else if (!m_army)
        caption = localization::Tr("town.army.no_visitor");
    if (caption) {
        const int y = m_y + 80;
        DrawTownInset(screen, m_x + 7, y, 98, 19, inset);
        char label[32];
        snprintf(label, sizeof(label), "%s", caption);
        m_font->DrawBoundedString(label, m_x + 10, y + (19 - m_font->m_height) / 2, 92,
                                  m_font->m_height, 1, FONT_ALIGN_CENTER);
    }
    for (int i = 0; i < ARMY_GROUP_SLOT_COUNT; ++i) {
        const int creatureType = m_army ? m_army->m_creatureTypes[i] : static_cast<int>(CREATURE_NONE);
        const int slotX = m_x + i * STRIP_ARMY_X_STEP + STRIP_ARMY_FIRST_X;
        const int slotY = m_y + STRIP_CONTENT_Y;
        if (creatureType != CREATURE_NONE) {
            m_stripIcon->DrawToBuffer(
                slotX, slotY,
                CREATURE_FACTION(creatureType) + STRIP_FACTION_FRAME_OFFSET,
                ICON_DRAW_NORMAL,
                ICON_DRAW_OFFSET_FULL
            );
            m_monsterIcon->DrawToBuffer(
                m_x + i * STRIP_ARMY_X_STEP + STRIP_MONSTER_X,
                m_y + STRIP_MONSTER_Y,
                creatureType,
                ICON_DRAW_NORMAL,
                ICON_DRAW_OFFSET_FULL
            );
            char amount[12];
            snprintf(amount, sizeof(amount), "%d", m_army->m_creatureCounts[i]);
            const int amountWidth = m_font->LineWidth(amount) + 12;
            const int amountX = slotX + STRIP_QUANTITY_WIDTH - amountWidth;
            const int amountY = m_y + STRIP_QUANTITY_Y - 2;
            DrawTownInset(screen, amountX, amountY, amountWidth, m_font->m_height + 4, inset);
            m_font->DrawBoundedString(
                amount,
                amountX + 3,
                m_y + STRIP_QUANTITY_Y,
                amountWidth - 6,
                m_font->m_height,
                1,
                FONT_ALIGN_CENTER
            );
        } else {
            m_stripIcon->DrawToBuffer(
                slotX, slotY,
                STRIP_EMPTY_FRAME,
                ICON_DRAW_NORMAL,
                ICON_DRAW_OFFSET_FULL
            );
            DrawTownInset(screen, slotX + 3, slotY + 3, STRIP_ARMY_BORDER_WIDTH - 5, STRIP_BORDER_HEIGHT - 5, inset);
        }
    }
    m_window->DrawWindow(drawWindow);
    if (m_selectedSlot != STRIP_SLOT_NONE)
        m_stripIcon->DrawToBuffer(
            m_x + m_selectedSlot * STRIP_ARMY_X_STEP + STRIP_ARMY_FIRST_X,
            m_y + STRIP_CONTENT_Y,
            STRIP_SELECTED_FRAME,
            ICON_DRAW_NORMAL,
            ICON_DRAW_OFFSET_FULL
        );
}

void strip::DrawFrame(void) {
    m_stripIcon
        ->DrawToBuffer(m_x, m_y, STRIP_BACKGROUND_FRAME, ICON_DRAW_NORMAL, ICON_DRAW_OFFSET_FULL);
}

bankBox::bankBox(i16 x, i16 y, class playerData* player) {
    m_window = new heroWindow(x, y, "bankbox.bin");
    if (!m_window)
        MemError();
    m_window->AddWidget(new TownTreasury(*player), WINDOW_Z_ORDER_APPEND);
    gWindowManager->AddWindow(m_window, WINDOW_Z_ORDER_APPEND, true);
    Update();
}

bankBox::~bankBox() {
    gWindowManager->RemoveWindow(m_window);
    delete m_window;
}

void bankBox::Update(void) {
    m_window->DrawWindow();
}
