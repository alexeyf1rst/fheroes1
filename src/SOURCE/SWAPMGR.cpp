#include <H1/Ints.h>

#include <BASE/baseManager.h>
#include <BASE/heroWindow.h>
#include <BASE/heroWindowManager.h>
#include <BASE/icon.h>
#include <BASE/inputManager.h>
#include <BASE/message.h>
#include <BASE/mouseManager.h>
#include <BASE/palette.h>
#include <BASE/resourceManager.h>
#include <BASE/widget.h>
#include <BASE/goldButton.h>
#include <algorithm>
#include <array>
#include <SOURCE/advManager.h>
#include <SOURCE/armyGroup.h>
#include <SOURCE/EVENTS.h>
#include <SOURCE/game.h>
#include <SOURCE/hero.h>
#include <SOURCE/KB.h>
#include <SOURCE/kbwin.h>
#include <SOURCE/swapManager.h>
#include <SOURCE/townManager.h>

#include <stdio.h>
#include <string.h>

namespace {
enum MeetingControl {
    TROOPS_LEFT = 1000, TROOPS_RIGHT, TROOPS_SWAP,
    ARTIFACTS_LEFT, ARTIFACTS_RIGHT, ARTIFACTS_SWAP
};

bool CanTradeArtifact(i8 artifact) {
    return artifact != ARTIFACT_MAGIC_BOOK && artifact != ARTIFACT_FIZBIN_OF_MISFORTUNE;
}
}

swapManager::swapManager(void) {
    m_window = NULL;
    m_selectorIcon = NULL;
    m_selectedSide = SWAP_SIDE_NONE;
    m_targetSide = SWAP_SIDE_NONE;
    m_itemType = SWAP_ITEM_NONE;
    m_selectedSlot = SWAP_SLOT_NONE;
    m_targetSlot = SWAP_SLOT_NONE;
    m_heroes[SWAP_SIDE_LEFT] = NULL;
    m_heroes[SWAP_SIDE_RIGHT] = NULL;
}

swapManager::swapManager(class hero* leftHero, class hero* rightHero) {
    m_heroes[SWAP_SIDE_LEFT] = leftHero;
    m_heroes[SWAP_SIDE_RIGHT] = rightHero;
}

void swapManager::Reset(void) {
    m_selectedSide = m_targetSide = ((m_itemType = (m_selectedSlot = m_targetSlot = SWAP_SLOT_NONE)));
}

i16 swapManager::Open(i16 id) {
    tag_message message;
    i32 i;

    Reset();
    m_window = new heroWindow(16, 16, "swapwin.bin");
    if (!m_window)
        MemError();
    SetWinText(m_window, WINDOW_TEXT_SWAP);
    const goldButton::Mark arrows[] = {goldButton::Left, goldButton::Right, goldButton::Exchange};
    for (i16 index = 0; index < 6; ++index) {
        const i16 y = index < 3 ? 192 : 335;
        auto* button = new goldButton(176 + (index % 3) * 36, y, 32, 22,
                                      TROOPS_LEFT + index, "", arrows[index % 3]);
        m_window->AddWidget(button, WINDOW_Z_ORDER_APPEND);
    }
    message.type = MESSAGE_WIDGET;
    message.command = WIDGET_COMMAND_SET_ICON;
    sprintf(gText, "port%04d.icn", m_heroes[SWAP_SIDE_LEFT]->m_portrait);
    message.id = CONTROL_LEFT_HERO;
    message.text = gText;
    m_window->BroadcastMessage(message);
    sprintf(gText, "port%04d.icn", m_heroes[SWAP_SIDE_RIGHT]->m_portrait);
    message.id = CONTROL_RIGHT_HERO;
    m_window->BroadcastMessage(message);
    message.command = WIDGET_COMMAND_SET_TEXT;
    sprintf(
        gText,
        localization::Tr("hero.meeting.title"),
        m_heroes[SWAP_SIDE_LEFT]->m_name,
        m_heroes[SWAP_SIDE_RIGHT]->m_name
    );
    message.text = gText;
    message.id = CONTROL_TITLE;
    m_window->BroadcastMessage(message);
    SET_ADVENTURE_BUTTON_FLAGS(message, gAdvManager->m_adventureWindow, WIDGET_COMMAND_CLEAR_FLAGS);
    Update();
    gWindowManager->AddWindow(m_window, WINDOW_Z_ORDER_APPEND, true);
    KBChangeMenu(gAdventureMenu);
    gMonoIconSkip = 2;
    m_selectorIcon = gResourceManager->GetIcon("swapbtn.icn");
    gMonoIconSkip = -1;
    gMouseManager->SetPointer(ADVENTURE_POINTER_DEFAULT);
    m_messageFilter = MESSAGE_KEY_DOWN | MESSAGE_KEY_UP | MESSAGE_MOUSE_MOVE
                      | MESSAGE_LEFT_BUTTON_DOWN | MESSAGE_RIGHT_BUTTON_DOWN | 0x100
                      | MESSAGE_WIDGET;
    m_messageMask = BASE_MANAGER_ACCEPT_SWAP;
    m_priority = id;
    m_active = 1;
    strcpy(m_name, "swapManager");
    return BASE_MANAGER_SUCCESS;
}

void swapManager::Close(void) {
    tag_message message;

    gResourceManager->Dispose(m_selectorIcon);
    gWindowManager->RemoveWindow(m_window);
    delete m_window;
    m_active = 0;
    gAdvManager->Activate();
    SET_ADVENTURE_BUTTON_FLAGS(message, gAdvManager->m_adventureWindow, WIDGET_COMMAND_SET_FLAGS);
}

void swapManager::DrawSelector(void) {
    const i16 frameColor = PALETTE_SELECTION_COLOR;
    const i16 leftArmyLeft = 24;
    const i16 rightArmyLeft = 252;
    const i16 armyRow = 148;
    const i16 armyStride = 35;
    const i16 leftArtifactCol = 76;
    const i16 rightArtifactCol = 305;
    const i16 artifactY0 = 194;
    const i16 artifactsSpacing = 35;
    i16 selectorX0 = 0;
    i16 selectorY0 = 0;

    if (m_selectedSide != SWAP_SIDE_NONE && m_selectedSlot != SWAP_SLOT_NONE) {
        switch (m_selectedSide) {
            case SWAP_SIDE_LEFT:
                switch (m_itemType) {
                    case SWAP_ITEM_ARMY:
                        selectorX0 = m_selectedSlot * armyStride + leftArmyLeft - 1;
                        selectorY0 = armyRow - 1;
                        break;
                    case SWAP_ITEM_ARTIFACT:
                        selectorX0 =
                            leftArtifactCol
                            + (m_selectedSlot > SWAP_ARTIFACTS_PER_COLUMN - 1 ? artifactsSpacing
                                                                              : 0)
                            - 1;
                        selectorY0 = m_selectedSlot % SWAP_ARTIFACTS_PER_COLUMN * artifactsSpacing
                                     + artifactY0 - 1;
                        break;
                }
                break;
            case SWAP_SIDE_RIGHT:
                switch (m_itemType) {
                    case SWAP_ITEM_ARMY:
                        selectorX0 = m_selectedSlot * armyStride + rightArmyLeft - 1;
                        selectorY0 = armyRow - 1;
                        break;
                    case SWAP_ITEM_ARTIFACT:
                        selectorX0 =
                            rightArtifactCol
                            + (m_selectedSlot > SWAP_ARTIFACTS_PER_COLUMN - 1 ? artifactsSpacing
                                                                              : 0)
                            - 1;
                        selectorY0 = m_selectedSlot % SWAP_ARTIFACTS_PER_COLUMN * artifactsSpacing
                                     + artifactY0 - 1;
                        break;
                }
                break;
        }
        m_selectorIcon->FillToBuffer(
            selectorX0 + 16,
            selectorY0 + 16,
            2,
            frameColor,
            ICON_DRAW_NORMAL,
            ICON_DRAW_OFFSET_FULL
        );
        gWindowManager->UpdateScreenRegion(selectorX0 + 16, selectorY0 + 16, 36, 36);
    }
}

void swapManager::TransferMeetingItems(i16 action) {
    auto& left = m_heroes[SWAP_SIDE_LEFT]->m_army;
    auto& right = m_heroes[SWAP_SIDE_RIGHT]->m_army;
    if (action == TROOPS_LEFT) {
        right.MoveTroopsTo(left, true, CREATURE_NONE);
    } else if (action == TROOPS_RIGHT) {
        left.MoveTroopsTo(right, true, CREATURE_NONE);
    } else if (action == TROOPS_SWAP) {
        if (left.GetNumArmies() && right.GetNumArmies())
            std::swap(left, right);
    } else {
        m_selectedSide = action == ARTIFACTS_LEFT ? SWAP_SIDE_RIGHT : SWAP_SIDE_LEFT;
        m_targetSide = m_selectedSide == SWAP_SIDE_LEFT ? SWAP_SIDE_RIGHT : SWAP_SIDE_LEFT;
        auto& source = m_heroes[m_selectedSide]->m_artifacts;
        auto& target = m_heroes[m_targetSide]->m_artifacts;
        std::array<i16, HERO_ARTIFACT_SLOT_COUNT> sourceSlots, targetSlots;
        i16 sourceCount = 0, targetCount = 0;
        i16 sourceItems = 0, targetItems = 0;
        for (i16 slot = 0; slot < HERO_ARTIFACT_SLOT_COUNT; ++slot) {
            if (CanTradeArtifact(source[slot])) {
                sourceSlots[sourceCount++] = slot;
                sourceItems += source[slot] != ARTIFACT_NONE;
            }
            if (CanTradeArtifact(target[slot])) {
                targetSlots[targetCount++] = slot;
                targetItems += target[slot] != ARTIFACT_NONE;
            }
        }
        if (action == ARTIFACTS_SWAP) {
            if (sourceItems <= targetCount && targetItems <= sourceCount) {
                // Pair occupied slots first so a full backpack can still be swapped.
                std::stable_partition(sourceSlots.begin(), sourceSlots.begin() + sourceCount,
                                      [&](i16 slot) { return source[slot] != ARTIFACT_NONE; });
                std::stable_partition(targetSlots.begin(), targetSlots.begin() + targetCount,
                                      [&](i16 slot) { return target[slot] != ARTIFACT_NONE; });
                for (i16 index = 0; index < std::max(sourceItems, targetItems); ++index) {
                    m_selectedSlot = sourceSlots[index];
                    m_targetSlot = targetSlots[index];
                    SwapArtifacts();
                }
            }
        } else {
            for (i16 index = 0, dest = 0; index < sourceCount; ++index) {
                m_selectedSlot = sourceSlots[index];
                if (source[m_selectedSlot] == ARTIFACT_NONE)
                    continue;
                while (dest < targetCount && target[targetSlots[dest]] != ARTIFACT_NONE)
                    ++dest;
                if (dest == targetCount)
                    break;
                m_targetSlot = targetSlots[dest++];
                SwapArtifacts();
            }
        }
    }
    Reset();
    Update();
    m_window->DrawWindow();
}

i16 swapManager::Main(struct tag_message& message) {
    b8 nowCloseRequested = false;
    b8 quickView;
    i32 artIndex;

    if (message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON)
        quickView = true;
    else
        quickView = false;
    if (!(message.type & m_messageFilter)) {
        if (message.type) {
            message.type = MESSAGE_NONE;
            return MESSAGE_DISPATCH_FORWARD;
        }
        return MESSAGE_DISPATCH_CONTINUE;
    }
    switch (message.type) {
        case MESSAGE_KEY_UP: {
            const i16 tap = ArmyShortcutTap(message);
            if (!tap || quickView)
                break;
            const i16 left = m_window->HoveredWidget(CONTROL_LEFT_ARMY_FIRST, CONTROL_LEFT_ARMY_LAST);
            const i16 right = m_window->HoveredWidget(CONTROL_RIGHT_ARMY_FIRST, CONTROL_RIGHT_ARMY_LAST);
            if (left != WIDGET_ID_NONE
                && m_heroes[SWAP_SIDE_LEFT]->m_army.QuickArrange(left - CONTROL_LEFT_ARMY_FIRST, tap, true)) {
                Reset();
                Update();
                m_window->DrawWindow();
            } else if (right != WIDGET_ID_NONE
                       && m_heroes[SWAP_SIDE_RIGHT]->m_army.QuickArrange(right - CONTROL_RIGHT_ARMY_FIRST, tap, true)) {
                Reset();
                Update();
                m_window->DrawWindow();
            }
            break;
        }
        case MESSAGE_KEY_DOWN: {
            armyGroup& left = m_heroes[SWAP_SIDE_LEFT]->m_army;
            armyGroup& right = m_heroes[SWAP_SIDE_RIGHT]->m_army;
            const i8 preferred = m_itemType == SWAP_ITEM_ARMY && m_selectedSide != SWAP_SIDE_NONE
                                     ? m_heroes[m_selectedSide]->m_army.m_creatureTypes[m_selectedSlot]
                                     : static_cast<i8>(CREATURE_NONE);
            if (message.keyCode == INPUT_SCAN_NUMPAD_4) {
                right.MoveTroopsTo(left, true, preferred);
            } else if (message.keyCode == INPUT_SCAN_NUMPAD_6) {
                left.MoveTroopsTo(right, true, preferred);
            } else if (TranslateShortcut(message.keyCode, SHORTCUT_ARMY) == INPUT_SCAN_X) {
                const armyGroup previous = left;
                left = right;
                right = previous;
            } else if (message.keyCode == INPUT_SCAN_ESCAPE) {
                nowCloseRequested = true;
                break;
            } else {
                break;
            }
            Reset();
            Update();
            m_window->DrawWindow();
            break;
        }
        case MESSAGE_RIGHT_BUTTON_DOWN:
            if (quickView)
                break;
            Reset();
            Update();
            m_window->DrawWindow();
            break;
        case MESSAGE_WIDGET:
            switch (message.command) {
                case WIDGET_NOTIFY_DESELECT:
                    if (quickView)
                        break;
                    if (message.id == DIALOG_BUTTON_0)
                        nowCloseRequested = true;
                    else if (message.id >= TROOPS_LEFT && message.id <= ARTIFACTS_SWAP)
                        TransferMeetingItems(message.id);
                    break;
                case WIDGET_NOTIFY_SELECT:
                    switch (message.id) {
                        case CONTROL_LEFT_HERO:
                            if (quickView)
                                break;
                            m_heroes[SWAP_SIDE_LEFT]->HeroView(true);
                            gAdvManager->RedrawAdvScreen(true);
                            Update();
                            m_window->DrawWindow();
                            Reset();
                            gWindowManager->FadeScreen(WINDOW_FADE_IN, WINDOW_FADE_SHORT, NULL);
                            break;
                        case CONTROL_RIGHT_HERO:
                            if (quickView)
                                break;
                            m_heroes[SWAP_SIDE_RIGHT]->HeroView(true);
                            gAdvManager->RedrawAdvScreen(true);
                            Update();
                            m_window->DrawWindow();
                            Reset();
                            gWindowManager->FadeScreen(WINDOW_FADE_IN, WINDOW_FADE_SHORT, NULL);
                            break;
                        case CONTROL_LEFT_ARTIFACT_FIRST:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 1:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 2:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 3:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 4:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 5:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 6:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 7:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 8:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 9:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 10:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 11:
                        case CONTROL_LEFT_ARTIFACT_FIRST + 12:
                        case CONTROL_LEFT_ARTIFACT_LAST:
                            artIndex = message.id - CONTROL_LEFT_ARTIFACT_FIRST;
                            if (!quickView
                                && (m_heroes[SWAP_SIDE_LEFT]->m_artifacts[artIndex]
                                        == ARTIFACT_MAGIC_BOOK
                                    || m_heroes[SWAP_SIDE_LEFT]->m_artifacts[artIndex]
                                           == ARTIFACT_FIZBIN_OF_MISFORTUNE)) {
                                NormalDialog(
                                    localization::Tr("artifact.trade.forbidden"),
                                    NORMAL_DIALOG_TYPE_OK
                                );
                                break;
                            }
                            if (quickView) {
                                if (m_heroes[SWAP_SIDE_LEFT]->m_artifacts[artIndex]
                                    == ARTIFACT_NONE)
                                    break;
                                m_heroes[SWAP_SIDE_LEFT]->ViewArtifact(
                                    m_heroes[SWAP_SIDE_LEFT]->m_artifacts[artIndex],
                                    true
                                );
                                break;
                            }
                            if (m_itemType != SWAP_ITEM_ARTIFACT) {
                                if (m_heroes[SWAP_SIDE_LEFT]->m_artifacts[artIndex]
                                    != ARTIFACT_NONE) {
                                    m_selectedSide = SWAP_SIDE_LEFT;
                                    m_targetSide = SWAP_SIDE_NONE;
                                    m_itemType = SWAP_ITEM_ARTIFACT;
                                    m_selectedSlot = artIndex;
                                    m_targetSlot = SWAP_SLOT_NONE;
                                } else {
                                    Reset();
                                }
                            } else {
                                m_targetSide = SWAP_SIDE_LEFT;
                                m_targetSlot = artIndex;
                                if (m_selectedSide == SWAP_SIDE_LEFT
                                    && m_selectedSlot == m_targetSlot) {
                                    m_heroes[SWAP_SIDE_LEFT]->ViewArtifact(
                                        m_heroes[SWAP_SIDE_LEFT]->m_artifacts[artIndex],
                                        false
                                    );
                                    Reset();
                                }
                                SwapArtifacts();
                                Reset();
                            }
                            break;
                        case CONTROL_RIGHT_ARTIFACT_FIRST:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 1:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 2:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 3:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 4:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 5:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 6:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 7:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 8:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 9:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 10:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 11:
                        case CONTROL_RIGHT_ARTIFACT_FIRST + 12:
                        case CONTROL_RIGHT_ARTIFACT_LAST:
                            artIndex = message.id - CONTROL_RIGHT_ARTIFACT_FIRST;
                            if (!quickView
                                && (m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[artIndex]
                                        == ARTIFACT_MAGIC_BOOK
                                    || m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[artIndex]
                                           == ARTIFACT_FIZBIN_OF_MISFORTUNE)) {
                                NormalDialog(
                                    localization::Tr("artifact.trade.forbidden"),
                                    NORMAL_DIALOG_TYPE_OK
                                );
                                break;
                            }
                            if (quickView) {
                                if (m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[artIndex]
                                    == ARTIFACT_NONE)
                                    break;
                                m_heroes[SWAP_SIDE_RIGHT]->ViewArtifact(
                                    m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[artIndex],
                                    true
                                );
                                break;
                            }
                            if (m_itemType != SWAP_ITEM_ARTIFACT) {
                                if (m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[artIndex]
                                    != ARTIFACT_NONE) {
                                    m_selectedSide = SWAP_SIDE_RIGHT;
                                    m_targetSide = SWAP_SIDE_NONE;
                                    m_itemType = SWAP_ITEM_ARTIFACT;
                                    m_selectedSlot = artIndex;
                                    m_targetSlot = SWAP_SLOT_NONE;
                                } else {
                                    Reset();
                                }
                            } else {
                                m_targetSide = SWAP_SIDE_RIGHT;
                                m_targetSlot = artIndex;
                                if (m_selectedSide == SWAP_SIDE_RIGHT
                                    && m_selectedSlot == m_targetSlot) {
                                    m_heroes[SWAP_SIDE_RIGHT]->ViewArtifact(
                                        m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[artIndex],
                                        false
                                    );
                                    Reset();
                                }
                                SwapArtifacts();
                                Reset();
                            }
                            break;
                        case CONTROL_LEFT_ARMY_FIRST:
                        case CONTROL_LEFT_ARMY_FIRST + 1:
                        case CONTROL_LEFT_ARMY_FIRST + 2:
                        case CONTROL_LEFT_ARMY_FIRST + 3:
                        case CONTROL_LEFT_ARMY_LAST:
                            if (!quickView && m_heroes[SWAP_SIDE_LEFT]->m_army.QuickArrange(
                                    message.id - CONTROL_LEFT_ARMY_FIRST, message.modifiers,
                                    m_itemType != SWAP_ITEM_ARMY)) {
                                Reset();
                                break;
                            }
                            if (quickView) {
                                if (m_heroes[SWAP_SIDE_LEFT]
                                        ->m_army
                                        .m_creatureTypes[message.id - CONTROL_LEFT_ARMY_FIRST]
                                    != CREATURE_NONE)
                                    gGame->ViewArmy(
                                        TOWN_ARMY_VIEW_X,
                                        TOWN_ARMY_VIEW_Y,
                                        m_heroes[SWAP_SIDE_LEFT]
                                            ->m_army
                                            .m_creatureTypes[message.id - CONTROL_LEFT_ARMY_FIRST],
                                        m_heroes[SWAP_SIDE_LEFT]
                                            ->m_army
                                            .m_creatureCounts[message.id - CONTROL_LEFT_ARMY_FIRST],
                                        NULL,
                                        false,
                                        ARMY_FACING_RIGHT,
                                        true,
                                        m_heroes[SWAP_SIDE_LEFT],
                                        NULL,
                                        &m_heroes[SWAP_SIDE_LEFT]->m_army
                                    );
                                break;
                            }
                            if (m_itemType) {
                                if (m_heroes[SWAP_SIDE_LEFT]
                                        ->m_army
                                        .m_creatureTypes[message.id - CONTROL_LEFT_ARMY_FIRST]
                                    != CREATURE_NONE) {
                                    m_selectedSide = SWAP_SIDE_LEFT;
                                    m_targetSide = SWAP_SIDE_NONE;
                                    m_itemType = SWAP_ITEM_ARMY;
                                    m_selectedSlot = message.id - CONTROL_LEFT_ARMY_FIRST;
                                    m_targetSlot = SWAP_SLOT_NONE;
                                } else {
                                    Reset();
                                }
                            } else {
                                m_targetSide = SWAP_SIDE_LEFT;
                                m_targetSlot = message.id - CONTROL_LEFT_ARMY_FIRST;
                                if (m_selectedSide == SWAP_SIDE_LEFT
                                    && m_selectedSlot == m_targetSlot) {
                                    ViewMon();
                                    Reset();
                                }
                                if ((message.modifiers & MESSAGE_MODIFIER_SHIFT_KEYS)
                                    && m_selectedSide != m_targetSide
                                    && (m_heroes[m_targetSide]->m_army.m_creatureTypes[m_targetSlot]
                                            == CREATURE_NONE
                                        || m_heroes[m_targetSide]
                                                   ->m_army.m_creatureTypes[m_targetSlot]
                                               == m_heroes[m_selectedSide]
                                                      ->m_army.m_creatureTypes[m_selectedSlot])) {
                                    SplitMons();
                                    Reset();
                                } else {
                                    SwapMons();
                                    Reset();
                                }
                            }
                            break;
                        case CONTROL_RIGHT_ARMY_FIRST:
                        case CONTROL_RIGHT_ARMY_FIRST + 1:
                        case CONTROL_RIGHT_ARMY_FIRST + 2:
                        case CONTROL_RIGHT_ARMY_FIRST + 3:
                        case CONTROL_RIGHT_ARMY_LAST:
                            if (!quickView && m_heroes[SWAP_SIDE_RIGHT]->m_army.QuickArrange(
                                    message.id - CONTROL_RIGHT_ARMY_FIRST, message.modifiers,
                                    m_itemType != SWAP_ITEM_ARMY)) {
                                Reset();
                                break;
                            }
                            if (quickView) {
                                if (m_heroes[SWAP_SIDE_RIGHT]
                                        ->m_army
                                        .m_creatureTypes[message.id - CONTROL_RIGHT_ARMY_FIRST]
                                    != CREATURE_NONE)
                                    gGame->ViewArmy(
                                        TOWN_ARMY_VIEW_X,
                                        TOWN_ARMY_VIEW_Y,
                                        m_heroes[SWAP_SIDE_RIGHT]
                                            ->m_army
                                            .m_creatureTypes[message.id - CONTROL_RIGHT_ARMY_FIRST],
                                        m_heroes[SWAP_SIDE_RIGHT]->m_army.m_creatureCounts
                                            [message.id - CONTROL_RIGHT_ARMY_FIRST],
                                        NULL,
                                        false,
                                        ARMY_FACING_RIGHT,
                                        true,
                                        m_heroes[SWAP_SIDE_RIGHT],
                                        NULL,
                                        &m_heroes[SWAP_SIDE_RIGHT]->m_army
                                    );
                                break;
                            }
                            if (m_itemType) {
                                if (m_heroes[SWAP_SIDE_RIGHT]
                                        ->m_army
                                        .m_creatureTypes[message.id - CONTROL_RIGHT_ARMY_FIRST]
                                    != CREATURE_NONE) {
                                    m_selectedSide = SWAP_SIDE_RIGHT;
                                    m_targetSide = SWAP_SIDE_NONE;
                                    m_itemType = SWAP_ITEM_ARMY;
                                    m_selectedSlot = message.id - CONTROL_RIGHT_ARMY_FIRST;
                                    m_targetSlot = SWAP_SLOT_NONE;
                                } else {
                                    Reset();
                                }
                            } else {
                                m_targetSide = SWAP_SIDE_RIGHT;
                                m_targetSlot = message.id - CONTROL_RIGHT_ARMY_FIRST;
                                if (m_selectedSide == SWAP_SIDE_RIGHT
                                    && m_selectedSlot == m_targetSlot) {
                                    ViewMon();
                                    Reset();
                                }
                                if ((message.modifiers & MESSAGE_MODIFIER_SHIFT_KEYS)
                                    && m_selectedSide != m_targetSide
                                    && (m_heroes[m_targetSide]->m_army.m_creatureTypes[m_targetSlot]
                                            == CREATURE_NONE
                                        || m_heroes[m_targetSide]
                                                   ->m_army.m_creatureTypes[m_targetSlot]
                                               == m_heroes[m_selectedSide]
                                                      ->m_army.m_creatureTypes[m_selectedSlot])) {
                                    SplitMons();
                                    Reset();
                                } else {
                                    SwapMons();
                                    Reset();
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    if (!quickView) {
                        Update();
                        m_window->DrawWindow();
                        DrawSelector();
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    if (nowCloseRequested == true) {
        message.type = MESSAGE_EXECUTIVE;
        message.executiveCommand = EXECUTIVE_COMMAND_RETURN_RESULT;
        return MESSAGE_DISPATCH_FORWARD;
    }
    return MESSAGE_DISPATCH_CONSUME;
}

void swapManager::ViewMon(void) {
    gGame->ViewArmy(
        TOWN_ARMY_VIEW_X,
        TOWN_ARMY_VIEW_Y,
        m_heroes[m_selectedSide]->m_army.m_creatureTypes[m_targetSlot],
        m_heroes[m_selectedSide]->m_army.m_creatureCounts[m_targetSlot],
        NULL,
        m_heroes[m_selectedSide]->m_army.GetNumArmies() == 1,
        ARMY_FACING_RIGHT,
        false,
        m_heroes[m_selectedSide],
        NULL,
        &m_heroes[m_selectedSide]->m_army
    );
}

void swapManager::SwapArtifacts(void) {
    i8 targetArtifact;
    i8 selectedArtifact;

    if (m_selectedSide == SWAP_SIDE_NONE || m_targetSide == SWAP_SIDE_NONE)
        return;

    selectedArtifact = m_heroes[m_selectedSide]->m_artifacts[m_selectedSlot];
    targetArtifact = m_heroes[m_targetSide]->m_artifacts[m_targetSlot];
    if (selectedArtifact == targetArtifact || !CanTradeArtifact(selectedArtifact) || !CanTradeArtifact(targetArtifact))
        return;
    gAdvManager
        ->GiveTakeArtifactStat(m_heroes[m_selectedSide], selectedArtifact, EVENT_ARTIFACT_TAKE);
    gAdvManager->GiveTakeArtifactStat(m_heroes[m_targetSide], targetArtifact, EVENT_ARTIFACT_TAKE);
    m_heroes[m_selectedSide]->m_artifacts[m_selectedSlot] = targetArtifact;
    m_heroes[m_targetSide]->m_artifacts[m_targetSlot] = selectedArtifact;
    gAdvManager
        ->GiveTakeArtifactStat(m_heroes[m_selectedSide], targetArtifact, EVENT_ARTIFACT_GIVE);
    gAdvManager
        ->GiveTakeArtifactStat(m_heroes[m_targetSide], selectedArtifact, EVENT_ARTIFACT_GIVE);
}

void swapManager::SwapMons(void) {
    armyGroup* targetTroops;
    armyGroup* selectedArmy;
    i16 i;
    i16 j;

    selectedArmy = &m_heroes[m_selectedSide]->m_army;
    targetTroops = &m_heroes[m_targetSide]->m_army;
    if (selectedArmy != targetTroops) {
        if (selectedArmy->GetNumArmies() == 1
            && (targetTroops->m_creatureTypes[m_targetSlot] == CREATURE_NONE
                || targetTroops->IsMember(selectedArmy->m_creatureTypes[m_selectedSlot])))
            return;
        if (targetTroops->IsMember(selectedArmy->m_creatureTypes[m_selectedSlot])) {
            for (i = 0; i < ARMY_GROUP_SLOT_COUNT; i++) {
                if (targetTroops->m_creatureTypes[i]
                    == selectedArmy->m_creatureTypes[m_selectedSlot])
                    break;
            }
            targetTroops->m_creatureCounts[i] += selectedArmy->m_creatureCounts[m_selectedSlot];
            selectedArmy->m_creatureTypes[m_selectedSlot] = CREATURE_NONE;
            selectedArmy->m_creatureCounts[m_selectedSlot] = 0;
            return;
        } else if (selectedArmy->IsMember(targetTroops->m_creatureTypes[m_targetSlot])) {
            for (j = 0; j < ARMY_GROUP_SLOT_COUNT; j++) {
                if (selectedArmy->m_creatureTypes[j] == targetTroops->m_creatureTypes[m_targetSlot])
                    break;
            }
            selectedArmy->m_creatureCounts[j] += targetTroops->m_creatureCounts[m_targetSlot];
            targetTroops->m_creatureTypes[m_targetSlot] = CREATURE_NONE;
            targetTroops->m_creatureCounts[m_targetSlot] = 0;
            if (j != m_selectedSlot) {
                targetTroops->m_creatureTypes[m_targetSlot] =
                    selectedArmy->m_creatureTypes[m_selectedSlot];
                targetTroops->m_creatureCounts[m_targetSlot] =
                    selectedArmy->m_creatureCounts[m_selectedSlot];
                selectedArmy->m_creatureTypes[m_selectedSlot] = CREATURE_NONE;
                selectedArmy->m_creatureCounts[m_selectedSlot] = 0;
            }
            return;
        }
    }
    selectedArmy->Swap(m_selectedSlot, targetTroops, m_targetSlot);
}

void swapManager::Update(void) {
    tag_message message;
    i16 i;

    message.type = MESSAGE_WIDGET;
    message.command = WIDGET_COMMAND_SET_TEXT;
    message.text = gText;
    for (i = 0; i < HERO_PRIMARY_STAT_COUNT; i++) {
        message.id = i + CONTROL_LEFT_PRIMARY_SKILL_FIRST;
        sprintf(
            gText,
            "%d",
            m_heroes[SWAP_SIDE_LEFT]->m_primaryStats[i]
        );
        m_window->BroadcastMessage(message);
        message.id = i + CONTROL_RIGHT_PRIMARY_SKILL_FIRST;
        sprintf(
            gText,
            "%d",
            m_heroes[SWAP_SIDE_RIGHT]->m_primaryStats[i]
        );
        m_window->BroadcastMessage(message);
    }
    for (i = 0; i < ARMY_GROUP_SLOT_COUNT; i++) {
        message.id = i + CONTROL_LEFT_ARMY_FIRST;
        if (m_heroes[SWAP_SIDE_LEFT]->m_army.m_creatureTypes[i] == CREATURE_NONE) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_FRAME;
            message.value =
                m_heroes[SWAP_SIDE_LEFT]->m_army.m_creatureTypes[i];
        }
        m_window->BroadcastMessage(message);
    }
    for (i = 0; i < ARMY_GROUP_SLOT_COUNT; i++) {
        message.id = i + CONTROL_LEFT_ARMY_COUNT_FIRST;
        if (m_heroes[SWAP_SIDE_LEFT]->m_army.m_creatureTypes[i] == CREATURE_NONE) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_TEXT;
            sprintf(gText, "%d", m_heroes[SWAP_SIDE_LEFT]->m_army.m_creatureCounts[i]);
            message.text = gText;
        }
        m_window->BroadcastMessage(message);
    }
    for (i = 0; i < ARMY_GROUP_SLOT_COUNT; i++) {
        message.id = i + CONTROL_RIGHT_ARMY_FIRST;
        if (m_heroes[SWAP_SIDE_RIGHT]->m_army.m_creatureTypes[i] == CREATURE_NONE) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_FRAME;
            message.value =
                m_heroes[SWAP_SIDE_RIGHT]->m_army.m_creatureTypes[i];
        }
        m_window->BroadcastMessage(message);
    }
    for (i = 0; i < ARMY_GROUP_SLOT_COUNT; i++) {
        message.id = i + CONTROL_RIGHT_ARMY_COUNT_FIRST;
        if (m_heroes[SWAP_SIDE_RIGHT]->m_army.m_creatureTypes[i] == CREATURE_NONE) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_TEXT;
            sprintf(gText, "%d", m_heroes[SWAP_SIDE_RIGHT]->m_army.m_creatureCounts[i]);
            message.text = gText;
        }
        m_window->BroadcastMessage(message);
    }
    for (i = 0; i < HERO_ARTIFACT_SLOT_COUNT; i++) {
        message.id = i + CONTROL_LEFT_ARTIFACT_FIRST;
        if (m_heroes[SWAP_SIDE_LEFT]->m_artifacts[i] == ARTIFACT_NONE) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_FRAME;
            message.value = m_heroes[SWAP_SIDE_LEFT]->m_artifacts[i];
        }
        m_window->BroadcastMessage(message);
    }
    for (i = 0; i < HERO_ARTIFACT_SLOT_COUNT; i++) {
        message.id = i + CONTROL_RIGHT_ARTIFACT_FIRST;
        if (m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[i] == ARTIFACT_NONE) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_FRAME;
            message.value = m_heroes[SWAP_SIDE_RIGHT]->m_artifacts[i];
        }
        m_window->BroadcastMessage(message);
    }
}

void swapManager::SplitMons(void) {
    i16 textControl;
    armyGroup* targetTroops;
    armyGroup* selectedArmy;
    i16 amountWidgetId;
    tag_message message;
    i16 found;
    i16 placeSlot;

    amountWidgetId = TOWN_SPLIT_AMOUNT_CONTROL;
    found = 0;
    selectedArmy = &m_heroes[m_selectedSide]->m_army;
    targetTroops = &m_heroes[m_targetSide]->m_army;
    found = 0;
    textControl = TOWN_SPLIT_PROMPT_CONTROL;
    gTownManager->m_childWindow =
        new heroWindow(TOWN_SPLIT_WINDOW_X, TOWN_SPLIT_WINDOW_Y, "splitwin.bin");
    if (!gTownManager->m_childWindow)
        MemError();
    gTownManager->m_splitAmount = 0;
    gTownManager->m_splitMaximum = selectedArmy->m_creatureCounts[m_selectedSlot];
    message.type = MESSAGE_WIDGET;
    sprintf(
        gText,
        localization::Tr("army.transfer.prompt"),
        gArmyNames[selectedArmy->m_creatureTypes[m_selectedSlot]],
        m_heroes[m_selectedSide]->m_name,
        m_heroes[m_targetSide]->m_name
    );
    message.command = WIDGET_COMMAND_SET_TEXT;
    message.id = TOWN_SPLIT_PROMPT_CONTROL;
    message.text = gText;
    gTownManager->m_childWindow->BroadcastMessage(message);
    sprintf(gText, "%d", gTownManager->m_splitAmount);
    message.id = TOWN_SPLIT_AMOUNT_CONTROL;
    message.text = gText;
    gTownManager->m_childWindow->BroadcastMessage(message);
    gTownManager->m_childWindow->SetConfirmButton(DIALOG_BUTTON_2);
    gWindowManager->DoDialog(gTownManager->m_childWindow, SplitArmyHandler, false);
    delete gTownManager->m_childWindow;
    if (gWindowManager->m_dialogResult == DIALOG_BUTTON_2) {
        for (placeSlot = 0; placeSlot < ARMY_GROUP_SLOT_COUNT; placeSlot++) {
            if (targetTroops->m_creatureTypes[placeSlot]
                == selectedArmy->m_creatureTypes[m_selectedSlot]) {
                found = 1;
                break;
            }
        }
        if (found) {
            targetTroops->m_creatureCounts[placeSlot] += gTownManager->m_splitAmount;
        } else {
            if (targetTroops->m_creatureTypes[m_targetSlot] != CREATURE_NONE) {
                for (placeSlot = 0; placeSlot < ARMY_GROUP_SLOT_COUNT; placeSlot++) {
                    if (targetTroops->m_creatureTypes[placeSlot] == CREATURE_NONE)
                        break;
                }
                if (placeSlot < ARMY_GROUP_SLOT_COUNT)
                    m_targetSlot = placeSlot;
            }
            targetTroops->m_creatureTypes[m_targetSlot] =
                selectedArmy->m_creatureTypes[m_selectedSlot];
            targetTroops->m_creatureCounts[m_targetSlot] = gTownManager->m_splitAmount;
        }
        selectedArmy->m_creatureCounts[m_selectedSlot] -= gTownManager->m_splitAmount;
        if (selectedArmy->m_creatureCounts[m_selectedSlot] == 0)
            selectedArmy->m_creatureTypes[m_selectedSlot] = CREATURE_NONE;
    }
}
