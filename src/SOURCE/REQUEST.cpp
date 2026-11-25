#include <H1/Ints.h>

#include <BASE/baseManager.h>
#include <BASE/font.h>
#include <BASE/goldButton.h>
#include <BASE/heroWindow.h>
#include <BASE/heroWindowManager.h>
#include <BASE/icon.h>
#include <BASE/iconWidget.h>
#include <BASE/inputManager.h>
#include <BASE/message.h>
#include <BASE/Misc.h>
#include <BASE/palette.h>
#include <BASE/textWidget.h>
#include <BASE/widget.h>
#include <SOURCE/fileRequester.h>
#include <SOURCE/game.h>
#include <SOURCE/KB.h>
#include <BASE/mouseManager.h>
#include <BASE/resourceManager.h>
#include <BASE/soundmgr.h>
#include <SOURCE/kbwin.h>
#include <SOURCE/saveRecords.h>

#include <PLATFORM/File.h>
#include <PLATFORM/Records.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <ctime>

b32 ShowThisMap(char* fileName) {
    // Names are copied into fields sized for the original's 8.3 names; a
    // longer name, which a host with long file names lists, would overflow
    // them and is not shown.
    char* extension = FindLastToken(fileName, '.');
    if (strlen(fileName) > FILE_REQUESTER_LISTED_NAME_LIMIT
        || (extension && strlen(extension) >= FILE_REQUESTER_EXTENSION_SIZE))
        return false;
#ifdef HOMM1_EDITOR
    if (strnicmp(fileName, "AES3", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "BEM2", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "CAMP", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "CNM5", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "DNL3", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "ENS1", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "FEL6", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "GHM4", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "HNM1", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "INM6", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "JEM7", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "KNS2", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "LNS4", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "MIS7", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "NHL5", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "ONL7", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "PNM3", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "QNL1", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "RNL4", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "SEL2", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "THS5", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH)
        && strnicmp(fileName, "UHS6", FILE_REQUESTER_SHIPPED_MAP_PREFIX_LENGTH))
#endif
        return true;
    return false;
}

namespace {
constexpr int kSavePreviewHeight = 50;

class SavePreviewPanel : public textWidget {
public:
    SavePreviewPanel(const fileRequester& requester, i16 y)
        : textWidget(8, y, requester.m_window->m_winWidth - 16, kSavePreviewHeight,
                     strdup(""), "smalfont.fnt", TEXT_WIDGET_PLAIN_COLOR, WIDGET_ID_NONE, WIDGET_KIND_TEXT),
          m_requester(requester) {
        m_flags &= ~WIDGET_FLAG_ENABLED;
    }
    void Draw() override {
        const int x = m_owner->m_posX + m_x, y = m_owner->m_posY + m_y;
        auto* screen = gWindowManager->m_screen;
        FillBitmapArea(screen, x, y, m_width, m_height, goldButton::MatchGold(39, 31, 18, true));
        FillBitmapArea(screen, x + 2, y + 2, m_width - 4, m_height - 4, goldButton::MatchGold(18, 17, 15, true));
        m_font->DrawBoundedString(const_cast<char*>(m_requester.m_previewTitle), x + 8, y + 5, m_width - 16,
                                  m_font->m_height, 1, FONT_ALIGN_CENTER);
        m_font->DrawBoundedString(const_cast<char*>(m_requester.m_previewDetails), x + 8, y + 19, m_width - 16,
                                  m_font->m_height, 1, FONT_ALIGN_CENTER);
        m_font->DrawBoundedString(const_cast<char*>(m_requester.m_previewTime), x + 8, y + 33, m_width - 16,
                                  m_font->m_height, 1, FONT_ALIGN_CENTER);
    }
private:
    const fileRequester& m_requester;
};

void FormatSaveTime(u64 stamp, char* output, size_t capacity) {
    if (stamp == 0)
        return;
#if defined(_WIN32)
    constexpr u64 epochDifference = 11644473600ULL;
    const u64 seconds = stamp / 10000000ULL;
    if (seconds < epochDifference)
        return;
    const std::time_t localSeconds = static_cast<std::time_t>(seconds - epochDifference);
#else
    const std::time_t localSeconds = static_cast<std::time_t>(stamp / 1000000000ULL);
#endif
    if (const std::tm* local = std::localtime(&localSeconds))
        std::strftime(output, capacity, "%Y-%m-%d %H:%M", local);
}
}

fileRequester::fileRequester(
    i16 x,
    i16 y,
    i16 mode,
    const char* pattern,
    const char* directory,
    const char* defaultExtension
) : m_directory(directory) {
    char fullFileName[FILE_PATH_CAPACITY];
    SMapHeader header;
    struct Entry {
        std::string name;
        std::string extension;
        u64 modified;
    };

    m_selectedIndex = FILE_REQUESTER_SELECTION_NONE;
    m_fileCount = 0;
    m_topIndex = 0;
    m_fileNames = NULL;
    m_extensions = NULL;
    m_mapNames = NULL;
    m_mapInfo = NULL;
    m_filename[0] = '\0';
    m_x = x;
    m_y = y;
    strcpy(m_defaultExtension, defaultExtension);
    if (m_defaultExtension[1] == 'G')
        gRequestingGames = true;
    else
        gRequestingGames = false;
    m_mode = mode;

    const bool sortByDate = mode == FILE_REQUESTER_LOAD
                            && (!strnicmp(defaultExtension, ".GM", 3) || !stricmp(defaultExtension, ".CGM"));
    m_showSavePreview = sortByDate;
    std::vector<Entry> entries;
    const std::string wildcard = std::string(directory) + pattern;
    FileFindData data;
    const i32 find = FileFindFirst(wildcard.c_str(), &data);
    if (find != FILE_INVALID) {
        do {
            if (!ShowThisMap(data.name))
                continue;
            const char* extension = FindLastToken(data.name, '.');
            const std::string name(data.name, extension ? extension - data.name : strlen(data.name));
            if (name.size() > FILE_REQUESTER_LISTED_NAME_LIMIT
                || (extension && strlen(extension) >= FILE_REQUESTER_EXTENSION_SIZE))
                continue;
            const u64 modified = sortByDate ? FileModificationTime((std::string(directory) + data.name).c_str()) : 0;
            entries.push_back({name, extension ? extension : "", modified});
        } while (FileFindNext(find, &data));
        FileFindClose(find);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.modified != right.modified)
            return left.modified > right.modified;
        return strcmpi(left.name.c_str(), right.name.c_str()) < 0;
    });

    m_fileCount = static_cast<i16>(std::min<size_t>(entries.size(), INT16_MAX));
    m_fileNames = new FileRequesterName[m_fileCount + 1];
    m_extensions = new FileRequesterExtension[m_fileCount + 1];
    for (i32 i = 0; i < m_fileCount; ++i) {
        strcpy(m_fileNames[i].text, entries[i].name.c_str());
        strcpy(m_extensions[i].text, entries[i].extension.c_str());
    }
    if (gShowMapInfo) {
        m_mapNames = new FileRequesterName[m_fileCount + 1];
        m_mapInfo = new FileRequesterMapInfo[m_fileCount + 1];
    }

    if (gShowMapInfo) {
        for (i32 entryIndex = 0; entryIndex < m_fileCount; entryIndex++) {
            u8 bytes[MAP_HEADER_RECORD_SIZE];
            snprintf(
                fullFileName, sizeof(fullFileName),
                "%s%s%s",
                directory,
                m_fileNames[entryIndex].text,
                m_extensions[entryIndex].text
            );
            const i32 file = FileOpen(fullFileName, FILE_OPEN_READ);
            const bool read = file != FILE_INVALID && FileReadExact(file, bytes, sizeof(bytes));
            if (file != FILE_INVALID)
                FileClose(file);
            if (read) {
                RecordReader mapHeader(bytes, sizeof(bytes));
                ReadMapHeader(mapHeader, header);
            }
            // The size and difficulty index the tables of their names.
            if (read && header.id == MAP_HEADER_ID && header.size >= MAP_SIZE_SMALL
                && header.size <= MAP_SIZE_LARGE && header.difficulty >= MAP_DIFFICULTY_EASY
                && header.difficulty <= MAP_DIFFICULTY_FORGET_IT) {
                CopyTextField(
                    m_mapNames[entryIndex].text,
                    sizeof(m_mapNames[entryIndex].text),
                    header.name[0],
                    sizeof(header.name[0])
                );
                CopyTextField(
                    m_mapInfo[entryIndex].description,
                    sizeof(m_mapInfo[entryIndex].description),
                    header.description[0],
                    sizeof(header.description[0])
                );
                m_mapInfo[entryIndex].difficulty = header.difficulty;
                m_mapInfo[entryIndex].size = header.size;
            } else {
                strcpy(m_mapNames[entryIndex].text, m_fileNames[entryIndex].text);
                strcpy(m_mapInfo[entryIndex].description, "");
                m_mapInfo[entryIndex].difficulty = MAP_DIFFICULTY_EASY;
                m_mapInfo[entryIndex].size = MAP_SIZE_SMALL;
            }
        }
    }

    KBChangeMenu(gDefaultMenu);
    m_acceptMask = FILE_REQUESTER_DISPATCH_MASK | MESSAGE_MOUSE_WHEEL;
    m_result = FILE_REQUESTER_MAP_INFO_NONE;
}

fileRequester::~fileRequester() {}

void fileRequester::Close(void) {
    if (!m_active)
        return;
    strcpy(gLastMapName, GetMapName());
    strcpy(gLastFilename, GetFilename());
    if (m_fileNames)
        delete[] m_fileNames;
    if (m_extensions)
        delete[] m_extensions;
    if (m_mapNames)
        delete[] m_mapNames;
    if (m_mapInfo)
        delete[] m_mapInfo;
    gWindowManager->RemoveWindow(m_window);
    delete m_window;
    m_active = 0;
}

i16 fileRequester::Open(i16 priority) {
    const i16 scrollKnobId = FILE_REQUESTER_SCROLL_KNOB;
    tag_message message;
    i32 i;
    const i16 nameLabelId = FILE_REQUESTER_FILENAME_LABEL;
    char* extensionStart;
    b8 enableOk;

    strcpy(gLastMapName, "");
    strcpy(gLastFilename, "");
    m_window = new heroWindow(m_x, m_y, "request.bin");
    if (!m_window)
        MemError();
    if (m_showSavePreview) {
        const i16 previewY = m_window->m_winHeight + 4;
        m_window->m_winHeight = previewY + kSavePreviewHeight;
        m_window->AddWidget(new SavePreviewPanel(*this, previewY), WINDOW_Z_ORDER_APPEND);
    }
    m_window->SetConfirmButton(DIALOG_BUTTON_2);
    m_scrollKnob = new iconWidget(
        283,
        56,
        8,
        17,
        "scroll.icn",
        4,
        ICON_DRAW_NORMAL,
        scrollKnobId,
        ICON_WIDGET_DRAW,
        1
    );
    if (!m_scrollKnob)
        MemError();
    m_window->AddWidget(m_scrollKnob, WINDOW_Z_ORDER_APPEND);

    message.type = MESSAGE_WIDGET;
    message.command = WIDGET_COMMAND_SET_TEXT;
#ifdef HOMM1_EDITOR
    enableOk = false;
    message.id = nameLabelId;
    sprintf(gText, localization::Tr("file.load.label"));
    message.text = gText;
    m_window->BroadcastMessage(message);
#else
    if (m_mode == FILE_REQUESTER_SAVE) {
        enableOk = true;
        const i16 textEntryId = FILE_REQUESTER_FILENAME_ENTRY;
        strcpy(m_filename, gGame->m_saveName);
        extensionStart = FindLastToken(m_filename, '.');
        if (extensionStart)
            *extensionStart = '\0';
        message.id = textEntryId;
        message.text = m_filename;
        m_window->BroadcastMessage(message);
        message.id = nameLabelId;
        sprintf(gText, localization::Tr("file.save.label"));
        message.text = gText;
        m_window->BroadcastMessage(message);
        for (i = 0; i < m_fileCount; i++) {
            if (!strcmpi(m_fileNames[i].text, m_filename)
                && m_extensions[i].text[FILE_REQUESTER_EXTENSION_PLAYER_DIGIT] - '0'
                       == gNumHumanPlayers)
                m_selectedIndex = i;
        }
    } else {
        enableOk = false;
        if (m_mode == FILE_REQUESTER_LOAD && m_defaultExtension[1] == 'M') {
            for (i = 0; i < m_fileCount; i++) {
                if (!strnicmp(m_fileNames[i].text, gMapName, SAVE_FILE_BASE_NAME_LENGTH)) {
                    m_selectedIndex = i;
                    enableOk = true;
                }
            }
        }
        message.id = nameLabelId;
        sprintf(gText, localization::Tr("file.load.label"));
        message.text = gText;
        m_window->BroadcastMessage(message);
    }
#endif
    const i16 entryId = FILE_REQUESTER_FILENAME_ENTRY;
    SET_WIDGET_MESSAGE(message, WIDGET_COMMAND_SET_MAX_LENGTH, entryId);
    message.value = FILE_REQUESTER_FILENAME_MAX_LENGTH;
    m_window->BroadcastMessage(message);
    Update(false);
#ifndef HOMM1_EDITOR
    if (gShowMapInfo)
        gWindowManager->AddWindow(gReqExtraWindow, WINDOW_Z_ORDER_APPEND, true);
#endif
    gWindowManager->AddWindow(m_window, WINDOW_Z_ORDER_APPEND, true);
    SetOK(enableOk);
    UpdateMapInfo();
    m_messageMask = BASE_MANAGER_ACCEPT_EXECUTIVE;
    m_priority = priority;
    m_active = 1;
    strcpy(m_name, "fileRequester");
    return BASE_MANAGER_SUCCESS;
}

void fileRequester::SetOK(b8 enabled) {
    tag_message message;

    SET_WIDGET_MESSAGE(
        message,
        enabled ? WIDGET_COMMAND_CLEAR_FLAGS : WIDGET_COMMAND_SET_FLAGS,
        DIALOG_BUTTON_2
    );
    message.value = WIDGET_FLAG_DIMMED;
    m_window->BroadcastMessage(message);
    message.command = enabled ? WIDGET_COMMAND_SET_FLAGS : WIDGET_COMMAND_CLEAR_FLAGS;
    message.value = WIDGET_FLAG_ENABLED;
    m_window->BroadcastMessage(message);
}

i16 fileRequester::Main(tag_message& message) {
    i32 firstShown;
    i32 stepSize;
    i32 pageCount;
    const i16 arrowUpId = FILE_REQUESTER_SCROLL_UP;
    tag_message reply;
    i16 length;
    i16 number;
    b32 handled;
    i16 mouseY;
    i16 mouseX;
    char nameBuffer[FILE_REQUESTER_LOCAL_NAME_SIZE];
    const i16 downId = FILE_REQUESTER_SCROLL_DOWN;
    const i16 scrollBarId = FILE_REQUESTER_SCROLL_GUTTER;
    const i16 firstItemId = FILE_REQUESTER_LIST_FIRST;
    const i16 scrollerId = FILE_REQUESTER_SCROLL_KNOB;
    const i16 fileNameId = FILE_REQUESTER_FILENAME_ENTRY;

    handled = false;
    if (!(message.type & m_acceptMask)) {
        if (message.type) {
            message.type = MESSAGE_NONE;
            return MESSAGE_DISPATCH_FORWARD;
        }
        return MESSAGE_DISPATCH_CONTINUE;
    }
    if (message.modifiers & MESSAGE_MODIFIER_RIGHT_BUTTON)
        return MESSAGE_DISPATCH_CONSUME;

    switch (message.type) {
        case MESSAGE_MOUSE_WHEEL: {
            if (message.x < m_x || message.x >= m_x + m_window->m_winWidth || message.y < m_y
                || message.y >= m_y + m_window->m_winHeight)
                break;
            m_wheelRemainder += message.value;
            const i32 steps = m_wheelRemainder / INPUT_WHEEL_STEP;
            m_wheelRemainder %= INPUT_WHEEL_STEP;
            const int next = std::clamp<int>(m_topIndex - steps, 0, std::max(0, m_fileCount - FILE_REQUESTER_VISIBLE_ROWS));
            if (next != m_topIndex) {
                m_topIndex = next;
                m_lastListClick = FILE_REQUESTER_SELECTION_NONE;
                Update(true);
            }
            break;
        }
        case MESSAGE_KEY_DOWN: {
            if (!m_fileCount)
                break;
            i32 selected = m_selectedIndex;
            switch (message.keyCode) {
                case INPUT_SCAN_NUMPAD_8:
                    selected = std::max<i32>(0, selected - 1);
                    break;
                case INPUT_SCAN_NUMPAD_2:
                    selected = std::min<i32>(m_fileCount - 1, selected + 1);
                    break;
                case INPUT_SCAN_NUMPAD_7:
                    selected = 0;
                    break;
                case INPUT_SCAN_NUMPAD_1:
                    selected = m_fileCount - 1;
                    break;
                case INPUT_SCAN_NUMPAD_9:
                    selected = std::max<i32>(0, selected - FILE_REQUESTER_VISIBLE_ROWS);
                    break;
                case INPUT_SCAN_NUMPAD_3:
                    selected = std::min<i32>(m_fileCount - 1, selected + FILE_REQUESTER_VISIBLE_ROWS);
                    break;
            }
            if (selected != m_selectedIndex) {
                m_lastListClick = FILE_REQUESTER_SELECTION_NONE;
                m_selectedIndex = selected;
                if (selected < m_topIndex)
                    m_topIndex = selected;
                else if (selected >= m_topIndex + FILE_REQUESTER_VISIBLE_ROWS)
                    m_topIndex = selected - FILE_REQUESTER_VISIBLE_ROWS + 1;
                SetOK(true);
                Update(true);
            }
            break;
        }
        case MESSAGE_WIDGET:
            switch (message.command) {
                case WIDGET_NOTIFY_DESELECT:
                    switch (message.id) {
                        case DIALOG_BUTTON_2:
                            if (m_selectedIndex == FILE_REQUESTER_SELECTION_NONE
                                && !m_filename[0]) {
                                NormalDialog(
                                    localization::Tr("file.selection.required"),
                                    NORMAL_DIALOG_TYPE_OK
                                );
                                break;
                            } else {
                                message.value = message.id;
                                handled = true;
                            }
                            break;
                        case DIALOG_BUTTON_1:
                            message.value = message.id;
                            handled = true;
                            break;
                    }
                    break;
                case WIDGET_NOTIFY_SELECT:
                    switch (message.id) {
                        case fileNameId:
                            SET_WIDGET_MESSAGE(reply, WIDGET_COMMAND_GET_TEXT, fileNameId);
                            // The load requester's name field is not a text
                            // entry and does not answer; the original copied
                            // from whatever the reply's text pointer held.
                            reply.text = NULL;
                            m_window->BroadcastMessage(reply);
                            if (reply.text == NULL)
                                break;
                            memset(nameBuffer, 0, 9);
                            strcpy(nameBuffer, reply.text);
                            length = strlen(nameBuffer);
                            for (number = 0; number < length; number++) {
                                if ((static_cast<u8>(nameBuffer[number]) < 'A'
                                     || static_cast<u8>(nameBuffer[number]) > 'Z')
                                    && (static_cast<u8>(nameBuffer[number]) < 'a'
                                        || static_cast<u8>(nameBuffer[number]) > 'z')
                                    && (static_cast<u8>(nameBuffer[number]) < '0'
                                        || static_cast<u8>(nameBuffer[number]) > '9')
                                    && (static_cast<u8>(nameBuffer[number]) < CYRILLIC_CAPITAL_A
                                        || static_cast<u8>(nameBuffer[number])
                                               > CYRILLIC_CAPITAL_YA)
                                    && (static_cast<u8>(nameBuffer[number]) < CYRILLIC_SMALL_A
                                        || static_cast<u8>(nameBuffer[number]) > CYRILLIC_SMALL_YA)
                                    && static_cast<u8>(nameBuffer[number]) != CYRILLIC_CAPITAL_YO
                                    && static_cast<u8>(nameBuffer[number]) != CYRILLIC_SMALL_YO
                                    && static_cast<u8>(nameBuffer[number]) != '_'
                                    && static_cast<u8>(nameBuffer[number]) != ' '
                                    && !FindToken("$%'-_@~`!(){}^#&+,;=[].", nameBuffer[number]))
                                    nameBuffer[number] = '\0';
                            }
                            for (number = strlen(nameBuffer) - 1; number >= 0; number--) {
                                if (static_cast<u8>(nameBuffer[number]) == ' ')
                                    nameBuffer[number] = '\0';
                                else
                                    number = -1;
                            }
                            if (strlen(nameBuffer) > 0 && static_cast<u8>(nameBuffer[0]) > ' ') {
                                m_selectedIndex = FILE_REQUESTER_SELECTION_NONE;
                                strcpy(m_filename, nameBuffer);
                                SetOK(true);
                            }
                            reply.command = WIDGET_COMMAND_SET_TEXT;
                            reply.id = fileNameId;
                            reply.text = m_filename;
                            m_window->BroadcastMessage(reply);
                            Update(true);
                            break;
                        case arrowUpId:
                            if (m_topIndex > 0) {
                                m_topIndex--;
                                Update(true);
                            }
                            break;
                        case downId:
                            if (m_topIndex + FILE_REQUESTER_VISIBLE_ROWS < m_fileCount) {
                                m_topIndex++;
                                if (m_topIndex + FILE_REQUESTER_LAST_ROW_OFFSET >= m_fileCount)
                                    m_topIndex = m_fileCount - FILE_REQUESTER_VISIBLE_ROWS;
                                Update(true);
                            }
                            break;
                        case scrollBarId:
                            pageCount = std::max(0, m_fileCount - FILE_REQUESTER_VISIBLE_ROWS);
                            stepSize = FILE_REQUESTER_GUTTER_BOTTOM - FILE_REQUESTER_GUTTER_TOP;
                            mouseY = message.mouseY - m_y - FILE_REQUESTER_GUTTER_TOP
                                - FILE_REQUESTER_SCROLL_KNOB_HALF_HEIGHT;
                            firstShown = std::clamp<int>(mouseY, 0, stepSize);
                            m_topIndex = (firstShown * pageCount + stepSize / 2) / stepSize;
                            Update(true);
                            break;
                        case scrollerId:
                            DoKnob();
                            break;
                        case firstItemId:
                        case firstItemId + 1:
                        case firstItemId + 2:
                        case firstItemId + 3:
                        case firstItemId + 4:
                        case firstItemId + 5:
                        case firstItemId + 6:
                        case firstItemId + 7:
                        case firstItemId + 8:
                        case firstItemId + 9: {
                            const i16 clicked = message.id - firstItemId + m_topIndex;
                            const bool doubleClick = clicked < m_fileCount && clicked == m_selectedIndex
                                && clicked == m_lastListClick && message.mouseTimestamp - m_lastListClickTime <= 650;
                            m_lastListClick = clicked;
                            m_lastListClickTime = message.mouseTimestamp;
                            if (doubleClick) {
                                message.value = DIALOG_BUTTON_2;
                                message.id = DIALOG_BUTTON_2;
                                handled = true;
                                break;
                            }
                            m_selectedIndex = message.id - firstItemId + m_topIndex;
                            if (m_selectedIndex >= m_fileCount) {
                                m_selectedIndex = FILE_REQUESTER_SELECTION_NONE;
                                SetOK(false);
                            } else {
                                SetOK(true);
                            }
                            Update(true);
                            break;
                        }
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    if (handled == true) {
#ifndef HOMM1_EDITOR
        if (m_showSavePreview && message.value == DIALOG_BUTTON_2) {
            UpdateSavePreview(true);
            if (!m_saveCanLoad) {
                NormalDialog(localization::Tr("file.preview.invalid"), NORMAL_DIALOG_TYPE_OK);
                handled = false;
            }
        }
        if (handled && gCampaignChoice <= CAMPAIGN_NONE && m_mode == FILE_REQUESTER_LOAD
            && m_selectedIndex >= 0 && gRequestingGames && message.value != FILE_REQUESTER_CANCEL) {
            number =
                m_extensions[m_selectedIndex].text[FILE_REQUESTER_EXTENSION_PLAYER_DIGIT] - '0';
            if (number < gNumHumanPlayers
                && gDebugLevel < FILE_REQUESTER_DEBUG_ALLOW_PLAYER_MISMATCH_MIN) {
                sprintf(gText, localization::Tr("file.humans.minimum"), number, gNumHumanPlayers);
                NormalDialog(gText, NORMAL_DIALOG_TYPE_OK);
                handled = false;
            }
            if (number > gNumHumanPlayers) {
                sprintf(
                    gText,
                    localization::Tr("file.humans.computer"),
                    number,
                    number - gNumHumanPlayers
                );
                NormalDialog(gText, NORMAL_DIALOG_TYPE_YES_NO);
                if (gWindowManager->m_dialogResult != NORMAL_DIALOG_CONFIRM)
                    handled = false;
            }
        }
#endif
        if (handled) {
            message.type = MESSAGE_EXECUTIVE;
            message.executiveCommand = EXECUTIVE_COMMAND_RETURN_RESULT;
            return MESSAGE_DISPATCH_FORWARD;
        }
    }
    UpdateMapInfo();
    return MESSAGE_DISPATCH_CONSUME;
}

void fileRequester::UpdateMapInfo(void) {
    if (m_selectedIndex != m_result && gShowMapInfo) {
        if (m_selectedIndex >= 0)
            strcpy(gCurMapName, m_fileNames[m_selectedIndex].text);
        else
            strcpy(gCurMapName, "");
        ShowMapInfo();
        m_result = m_selectedIndex;
    }
}

char* gFRDummy = "";

void fileRequester::DoKnob(void) {
    const int lastTop = std::max(0, m_fileCount - FILE_REQUESTER_VISIBLE_ROWS);
    const int span = FILE_REQUESTER_GUTTER_BOTTOM - FILE_REQUESTER_GUTTER_TOP;
    if (lastTop == 0) {
        m_scrollKnob->m_flags &= ~WIDGET_FLAG_SELECTED;
        return;
    }
    i16 x;
    i16 grabPointerY;

    gMouseManager->SetCursorShape(4);
    gMouseManager->MouseCoords(x, grabPointerY);
    const int grabOffset = grabPointerY - m_y - m_scrollKnob->m_y;
    while (gForegroundApp) {
        Process1WindowsMessage();
        const tag_message event = gInputManager->GetEvent();
        if (IS_BUTTON_RELEASE_MESSAGE(event.type))
            break;
        if (event.type == MESSAGE_MOUSE_MOVE) {
            const int position = std::clamp(event.y - m_y - grabOffset - FILE_REQUESTER_GUTTER_TOP, 0, span);
            const int next = (position * lastTop + span / 2) / span;
            if (next != m_topIndex) {
                m_topIndex = next;
                m_lastListClick = FILE_REQUESTER_SELECTION_NONE;
                Update(true);
            }
        } else if (event.type != MESSAGE_NONE) {
            gInputManager->PutBackEvent(event);
            break;
        }
    }
    gMouseManager->SetCursorShape(6);
    m_scrollKnob->m_flags &= ~WIDGET_FLAG_SELECTED;
    Update(true);
}

void fileRequester::Update(b8 drawWindow) {
    double gutterFactor;
    i32 textLimit;
    const i16 firstRowId = FILE_REQUESTER_LIST_FIRST;
    const i16 filenameEntryId = FILE_REQUESTER_FILENAME_ENTRY;
    i32 length;
    const i16 selectedColor = PALETTE_SELECTION_COLOR;
    i32 savedPlayerCount;
    b32 hasPlayerSuffix;
    const i16 plainColor = TEXT_WIDGET_PLAIN_COLOR;
    tag_message message;
    char playersString[FILE_REQUESTER_UPDATE_STORAGE_SIZE];
    i32 theSuffixWidth;
    font* bigFont;
    i16 row;

    message.type = MESSAGE_WIDGET;
    bigFont = gResourceManager->GetFont("bigfont.fnt");
    for (row = 0; row < FILE_REQUESTER_VISIBLE_ROWS; row++) {
        message.id = row + firstRowId;
        if (m_topIndex + row >= m_fileCount) {
            message.command = WIDGET_COMMAND_CLEAR_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
        } else {
            message.command = WIDGET_COMMAND_SET_FLAGS;
            message.value = WIDGET_FLAG_DRAW;
            m_window->BroadcastMessage(message);
            message.command = WIDGET_COMMAND_SET_TEXT;
            if (gShowMapInfo)
                sprintf(gText, "%s", m_mapNames[m_topIndex + row].text);
            else
                sprintf(gText, "%s", m_fileNames[m_topIndex + row].text);
            savedPlayerCount =
                m_extensions[m_topIndex + row].text[FILE_REQUESTER_EXTENSION_PLAYER_DIGIT] - '0';
            hasPlayerSuffix = false;
#ifndef HOMM1_EDITOR
            if (savedPlayerCount != 1 && gCampaignChoice <= CAMPAIGN_NONE && gRequestingGames) {
                hasPlayerSuffix = true;
                sprintf(
                    playersString,
                    " (%d %s)",
                    savedPlayerCount,
                    localization::Tr("file.players.label")
                );
                theSuffixWidth = bigFont->LineWidth(playersString);
            }
#endif
            textLimit = FILE_REQUESTER_ROW_TEXT_WIDTH;
            if (hasPlayerSuffix)
                textLimit -= theSuffixWidth + FILE_REQUESTER_PLAYER_SUFFIX_GAP;
            length = strlen(gText);
            while (length > 0 && bigFont->LineWidth(gText) > textLimit) {
                length--;
                gText[length] = 0;
            }
            if (hasPlayerSuffix)
                strcat(gText, playersString);
            message.text = gText;
        }
        m_window->BroadcastMessage(message);
        message.command = WIDGET_COMMAND_SET_COLOR;
        if (m_selectedIndex == m_topIndex + row)
            message.value = selectedColor;
        else
            message.value = plainColor;
        m_window->BroadcastMessage(message);
    }

    message.id = filenameEntryId;
    message.command = WIDGET_COMMAND_SET_FLAGS;
    message.value = WIDGET_FLAG_ENABLED;
    m_window->BroadcastMessage(message);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE) {
        message.command = WIDGET_COMMAND_SET_TEXT;
        if (gShowMapInfo)
            sprintf(gText, "%s", m_mapNames[m_selectedIndex].text);
        else
            sprintf(gText, "%s", m_fileNames[m_selectedIndex].text);
        message.text = gText;
        m_window->BroadcastMessage(message);
    }
    if (m_mode == FILE_REQUESTER_LOAD) {
        message.command = WIDGET_COMMAND_CLEAR_FLAGS;
        message.value = WIDGET_FLAG_ENABLED;
        m_window->BroadcastMessage(message);
    }
    if (m_fileCount <= FILE_REQUESTER_VISIBLE_ROWS) {
        m_scrollKnob->m_y = (FILE_REQUESTER_GUTTER_TOP + FILE_REQUESTER_GUTTER_BOTTOM) / 2;
    } else {
        gutterFactor = 156.0 / (m_fileCount - FILE_REQUESTER_VISIBLE_ROWS);
        m_scrollKnob->m_y = m_topIndex * gutterFactor + 56.0;
    }
    UpdateSavePreview();
    if (drawWindow)
        m_window->DrawWindow();
    gResourceManager->Dispose(bigFont);
}

void fileRequester::UpdateSavePreview(bool force) {
#ifndef HOMM1_EDITOR
    if (!m_showSavePreview || (!force && m_previewIndex == m_selectedIndex))
        return;
    m_previewIndex = m_selectedIndex;
    m_saveCanLoad = false;
    m_previewTitle[0] = m_previewDetails[0] = m_previewTime[0] = '\0';
    if (m_selectedIndex < 0 || m_selectedIndex >= m_fileCount) {
        snprintf(m_previewTitle, sizeof(m_previewTitle), "%s", m_fileCount
            ? localization::Tr("file.preview.select") : localization::Tr("file.preview.empty"));
    } else {
        const std::string path = m_directory + m_fileNames[m_selectedIndex].text + m_extensions[m_selectedIndex].text;
        SaveFilePreview preview{};
        const bool campaignPattern = !stricmp(m_defaultExtension, ".CGM");
        m_saveCanLoad = gGame->ReadSavePreview(path.c_str(), preview)
            && campaignPattern == (preview.campaign > CAMPAIGN_NONE);
        if (m_saveCanLoad) {
            snprintf(m_previewTitle, sizeof(m_previewTitle), "%s",
                     preview.mapName[0] ? preview.mapName : m_fileNames[m_selectedIndex].text);
            if (campaignPattern)
                snprintf(m_previewDetails, sizeof(m_previewDetails), localization::Tr("file.preview.campaign"),
                         preview.scenario + 1, preview.day);
            else
                snprintf(m_previewDetails, sizeof(m_previewDetails), localization::Tr("file.preview.scenario"),
                         preview.day, preview.players);
            FormatSaveTime(FileModificationTime(path.c_str()), m_previewTime, sizeof(m_previewTime));
        } else {
            snprintf(m_previewTitle, sizeof(m_previewTitle), "%s", m_fileNames[m_selectedIndex].text);
            snprintf(m_previewDetails, sizeof(m_previewDetails), "%s", localization::Tr("file.preview.invalid"));
        }
    }
    SetOK(m_saveCanLoad);
#endif
}

char* fileRequester::GetMapName(void) {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_fileCount && m_mapNames)
        return m_mapNames[m_selectedIndex].text;
    else
        return gFRDummy;
}

char* fileRequester::GetFilename(void) {
    if (m_mode != FILE_REQUESTER_SAVE && (m_selectedIndex < 0 || m_selectedIndex >= m_fileCount))
        return gFRDummy;

    if (m_selectedIndex == FILE_REQUESTER_SELECTION_NONE)
        sprintf(gText, "%s%s", m_filename, m_defaultExtension);
    else if (m_mode == FILE_REQUESTER_LOAD)
        sprintf(
            gText,
            "%s%s",
            m_fileNames[m_selectedIndex].text,
            m_extensions[m_selectedIndex].text
        );
    else
        sprintf(gText, "%s%s", m_fileNames[m_selectedIndex].text, m_defaultExtension);
    strcpy(m_filename, gText);
    return m_filename;
}

void fileRequester::ShowMapInfo(void) {
#ifndef HOMM1_EDITOR
    const i32 sizeIdPos = FILE_REQUESTER_MAP_SIZE;
    const i32 levelId = FILE_REQUESTER_MAP_LEVEL;
    const i32 descriptionId = FILE_REQUESTER_MAP_DESCRIPTION;
    tag_message msg;

    sprintf(gText, "");
    msg.text = gText;
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        gMapSize = m_mapInfo[m_selectedIndex].size;
    SET_WIDGET_MESSAGE(msg, WIDGET_COMMAND_SET_TEXT, sizeIdPos);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        msg.text = gMapSizeNames[m_mapInfo[m_selectedIndex].size];
    gReqExtraWindow->BroadcastMessage(msg);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        gMapDifficulty = m_mapInfo[m_selectedIndex].difficulty;
    sprintf(gText, "%s", gCurMapName);
    SET_WIDGET_MESSAGE(msg, WIDGET_COMMAND_SET_TEXT, levelId);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        msg.text = gMapDifficultyNames[m_mapInfo[m_selectedIndex].difficulty];
    gReqExtraWindow->BroadcastMessage(msg);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        strcpy(gFullMapName, m_mapNames[m_selectedIndex].text);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        strcpy(gMapDescription, m_mapInfo[m_selectedIndex].description);
    SET_WIDGET_MESSAGE(msg, WIDGET_COMMAND_SET_TEXT, descriptionId);
    if (m_selectedIndex != FILE_REQUESTER_SELECTION_NONE)
        msg.text = m_mapInfo[m_selectedIndex].description;
    gReqExtraWindow->BroadcastMessage(msg);
    gReqExtraWindow->DrawWindow();
#endif
}

b8 gRequestingGames;
