#include <H1/Ints.h>

#include <PLATFORM/File.h>
#include <PLATFORM/Records.h>
#include <SOURCE/saveRecords.h>

#include <vector>

#include <BASE/baseManager.h>
#include <BASE/bitmap.h>
#include <BASE/font.h>
#include <BASE/icon.h>
#include <BASE/MAKEFILEID.h>
#include <BASE/Misc.h>
#include <BASE/miscwin.h>
#include <BASE/palette.h>
#include <BASE/resource.h>
#include <BASE/resourceManager.h>
#include <BASE/sample.h>
#include <BASE/tileset.h>
#include <SOURCE/KB.h>
#include <SOURCE/kbwin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HOMM1_EDITOR
#define RESMGR_CPP_PATH "U:\\HMM\\VSS\\HMM1\\Source\\Base\\RESMGR.CPP"
#else
#define RESMGR_CPP_PATH "E:\\Users\\igorl\\VSS\\HMM\\HMM1\\Source\\Base\\RESMGR.CPP"
#endif

resourceManager::resourceManager(void) : baseManager() {
    m_active = 0;
    m_resourceListHead = NULL;
    m_aggregateDir = NULL;
    m_aggregateFd = FILE_DESCRIPTOR_INVALID;
    m_aggregateEntryCount = 0;
    m_expunging = false;
    strcpy(m_lastFileName, "");
    m_lastFileId = 0;
}

void resourceManager::GetBackdrop(char* name, class bitmap* backdrop) {
    PointToFile(MakeId(name));
    ReadWord();
    ReadWord();
    ReadWord();
    ReadBlock(backdrop->m_pixels, backdrop->m_width * backdrop->m_height);
    PostprocessBitmap(backdrop->m_pixels, backdrop->m_width, backdrop->m_height);
}

void resourceManager::GetBackdropAtLoc(
    char* filename,
    class bitmap* destination,
    i32 destinationX,
    i32 destinationY
) {
    i32 curRow;
    i16 imageHeight;
    i16 width;
    i16 fileId = MakeId(filename);
    PointToFile(fileId);
    ReadWord();
    width = ReadWord();
    imageHeight = ReadWord();
    // The image is read row by row into the destination at the given place.
    if (width < 0 || imageHeight < 0 || destinationX < 0 || destinationY < 0
        || destinationX + width > destination->m_width
        || destinationY + imageHeight > destination->m_height)
        InvalidResource(fileId);
    for (curRow = destinationY; curRow < destinationY + imageHeight; curRow++) {
        ReadBlock(destination->m_pixels + curRow * LOGICAL_SCREEN_WIDTH + destinationX, width);
    }
}

palette* resourceManager::GetPalette(char* name) {
    i16 id = MakeId(name);
    resource* resourceEntry = Query(id);
    if (resourceEntry != NULL) {
        resourceEntry->m_refCount++;
        return static_cast<palette*>(resourceEntry);
    } else {
        resourceEntry = new palette(id);
        AddResource(resourceEntry);
        return static_cast<palette*>(resourceEntry);
    }
}

bitmap* resourceManager::GetBitmap(char* name) {
    i16 id = MakeId(name);
    resource* resourceEntry = Query(id);
    if (resourceEntry != NULL) {
        resourceEntry->m_refCount++;
        return static_cast<bitmap*>(resourceEntry);
    } else {
        resourceEntry = new bitmap(id);
        AddResource(resourceEntry);
        return static_cast<bitmap*>(resourceEntry);
    }
}

icon* resourceManager::GetIcon(char* name) {
    i16 fileId = MakeId(name);
    return GetIcon(fileId);
}

icon* resourceManager::GetIcon(i16 fileId) {
    icon* iconEntry = static_cast<icon*>(Query(fileId));
    if (iconEntry != NULL) {
        iconEntry->m_refCount++;
        return iconEntry;
    } else {
        iconEntry = new icon(fileId);
        AddResource(iconEntry);
        return iconEntry;
    }
}

tileset* resourceManager::GetTileset(char* name) {
    i16 id = MakeId(name);
    resource* resourceEntry = Query(id);
    if (resourceEntry != NULL) {
        resourceEntry->m_refCount++;
        return static_cast<tileset*>(resourceEntry);
    } else {
        resourceEntry = new tileset(id);
        AddResource(resourceEntry);
        return static_cast<tileset*>(resourceEntry);
    }
}

mouse* resourceManager::GetMouse(char* name) {
    return NULL;
}

font* resourceManager::GetFont(char* name) {
    i16 resourceId = MakeId(name);
    resource* fontEntry = Query(resourceId);
    if (fontEntry != NULL) {
        fontEntry->m_refCount++;
        return static_cast<font*>(fontEntry);
    } else {
        fontEntry = new font(resourceId);
        AddResource(fontEntry);
        return static_cast<font*>(fontEntry);
    }
}

class sample* resourceManager::GetSample(char* name) {
    i16 fileId = MakeId(name);
    resource* resourceEntry = Query(fileId);
    if (resourceEntry != NULL) {
        resourceEntry->m_refCount++;
        return static_cast<sample*>(resourceEntry);
    } else {
        resourceEntry = new sample(name);
        AddResource(resourceEntry);
        return static_cast<sample*>(resourceEntry);
    }
}

void resourceManager::Dispose(class resource* resourceToDispose) {
    if (m_expunging)
        return;
    if (resourceToDispose != NULL) {
        resourceToDispose->m_refCount--;
        if (resourceToDispose->m_refCount > RESOURCE_REFERENCE_EMPTY) {
            return;
        } else {
            RemoveResource(resourceToDispose);
            delete resourceToDispose;
        }
    }
}

void resourceManager::AddResource(class resource* newResource) {
    if (m_resourceListHead == NULL) {
        m_resourceListHead = newResource;
        m_resourceListHead->m_next = NULL;
    } else {
        newResource->m_next = m_resourceListHead;
        m_resourceListHead = newResource;
    }
}

void resourceManager::Expunge(void) {
    m_expunging = true;
    resource* current = m_resourceListHead;
    resource* next = NULL;
    while (current != NULL) {
        next = current->m_next;
        RemoveResource(current);
        delete current;
        current = next;
    }
    m_expunging = false;
}

class resource* resourceManager::Query(i16 resourceId) {
    resource* cursorResource = m_resourceListHead;
    while (cursorResource != NULL && cursorResource->m_id != resourceId)
        cursorResource = cursorResource->m_next;
    return cursorResource;
}

i16 resourceManager::Main(tag_message& message) {
    return MESSAGE_DISPATCH_CONTINUE;
}

i16 resourceManager::Open(i16 priority) {
    if (LoadAggregateHeader(gDefaultAggregateName) != RESOURCE_MANAGER_LOAD_SUCCESS)
        return BASE_MANAGER_ERROR;
    m_messageMask = BASE_MANAGER_ACCEPT_RESOURCE;
    m_priority = priority;
    m_active = 1;
    strcpy(m_name, "resourceManager");
    m_resourceListHead = NULL;
    return BASE_MANAGER_SUCCESS;
}

void resourceManager::RemoveResource(class resource* resourceToRemove) {
    if (m_resourceListHead == resourceToRemove) {
        m_resourceListHead = resourceToRemove->m_next;
        return;
    }
    resource* previousResource = m_resourceListHead;
    while (previousResource != NULL && previousResource->m_next != resourceToRemove)
        previousResource = previousResource->m_next;
    if (previousResource == NULL) {
        return;
    } else {
        previousResource->m_next = resourceToRemove->m_next;
    }
}

void resourceManager::Close(void) {
    if (m_active != 1)
        return;
    Expunge();
    m_resourceListHead = NULL;
    if (m_aggregateDir != NULL)
        free(m_aggregateDir);
    if (m_aggregateFd != FILE_DESCRIPTOR_INVALID) {
        FileClose(m_aggregateFd);
        m_aggregateFd = FILE_DESCRIPTOR_INVALID;
    }
    m_active = 0;
}

i16 resourceManager::LoadAggregateHeader(char* aggregateName) {
    i32 aggregateFd = FileOpen(aggregateName, FILE_OPEN_READ);
    if (aggregateFd == FILE_DESCRIPTOR_INVALID) {
        sprintf(gText, localization::Tr("file.aggregate.open_failed"), aggregateName);
        ShutDown(gText);
        return RESOURCE_MANAGER_LOAD_ERROR;
    }
    if (m_aggregateFd != FILE_DESCRIPTOR_INVALID)
        FileClose(m_aggregateFd);
    if (m_aggregateDir != NULL)
        free(m_aggregateDir);
    m_aggregateFd = aggregateFd;
    m_aggregateEntryCount = ReadWord();
    const i32 fileLength = FileLength(m_aggregateFd);
    const i32 directoryStart = sizeof(i16);
    u8 firstRecord[AGG_ENTRY_RECORD_SIZE];
    if (m_aggregateEntryCount <= 0 || !FileReadExact(m_aggregateFd, firstRecord, sizeof(firstRecord))) {
        sprintf(gText, localization::Tr("file.aggregate.open_failed"), aggregateName);
        ShutDown(gText);
        return RESOURCE_MANAGER_LOAD_ERROR;
    }

    // The GOG archives put a second size field after each Buka-format record.
    // In these archives the first resource starts immediately after the table.
    RecordReader first(firstRecord, sizeof(firstRecord));
    aggEntry firstEntry;
    ReadAggEntry(first, firstEntry);
    const i32 gogRecordSize = AGG_ENTRY_RECORD_SIZE + sizeof(u32);
    const bool gog = firstEntry.offset == directoryStart + m_aggregateEntryCount * gogRecordSize;
    const i32 recordSize = gog ? gogRecordSize : AGG_ENTRY_RECORD_SIZE;
    const i32 directoryLength = m_aggregateEntryCount * recordSize;
    std::vector<u8> directory(directoryLength);
    if (directoryLength + directoryStart > fileLength
        || FileSeek(m_aggregateFd, directoryStart, FILE_SEEK_SET) != directoryStart
        || !FileReadExact(m_aggregateFd, &directory[0], directoryLength)) {
        sprintf(gText, localization::Tr("file.aggregate.open_failed"), aggregateName);
        ShutDown(gText);
        return RESOURCE_MANAGER_LOAD_ERROR;
    }
    m_aggregateDir = static_cast<aggEntry*>(malloc(m_aggregateEntryCount * sizeof(aggEntry)));
    RecordReader entries(&directory[0], directoryLength);
    for (i32 entry = 0; entry < m_aggregateEntryCount; entry++) {
        ReadAggEntry(entries, m_aggregateDir[entry]);
        if (gog)
            entries.Skip(sizeof(u32));
        if (m_aggregateDir[entry].offset < 0 || m_aggregateDir[entry].offset > fileLength
            || m_aggregateDir[entry].size > static_cast<u32>(fileLength - m_aggregateDir[entry].offset)) {
            sprintf(gText, localization::Tr("file.aggregate.open_failed"), aggregateName);
            ShutDown(gText);
            return RESOURCE_MANAGER_LOAD_ERROR;
        }
    }
    return RESOURCE_MANAGER_LOAD_SUCCESS;
}

// A resource whose contents do not fit its archive entry, or the structure
// the game reads it through, is reported as a damaged archive, as a
// directory that does not fit the file is.
void resourceManager::InvalidResource(i16 fileId) {
    sprintf(gText, localization::Tr("file.aggregate.open_failed"), gDefaultAggregateName);
    ShutDown(gText);
}

void resourceManager::PointToFile(i16 fileId) {
    i16 entry;
    if (m_aggregateDir == NULL)
        ShutDown("File Error: .AGG File not valid");
    entry = 0;
    while (entry < m_aggregateEntryCount && m_aggregateDir[entry].id != fileId)
        entry++;
    if (entry >= m_aggregateEntryCount) {
        sprintf(
            gText,
            "ResMgr::PointToFile failure!  ThisFileId:%d  LastFileId:%d  LastFileName:%s",
            fileId,
            m_lastFileId,
            m_lastFileName
        );
        ShutDown(gText);
    }
    FileSeek(m_aggregateFd, m_aggregateDir[entry].offset, FILE_SEEK_SET);
}

u32 resourceManager::GetFileSize(i16 fileId) {
    if (m_aggregateDir == NULL)
        return 0;
    i16 entry = 0;
    while (entry < m_aggregateEntryCount && m_aggregateDir[entry].id != fileId)
        entry++;
    if (entry >= m_aggregateEntryCount) {
        sprintf(
            gText,
            "ResMgr::PointToFile(GetFileSize) failure!  ThisFileId:%d  LastFileId:%d  "
            "LastFileName:%s",
            fileId,
            m_lastFileId,
            m_lastFileName
        );
        ShutDown(gText);
        return RESOURCE_MANAGER_LOAD_ERROR;
    }
    return m_aggregateDir[entry].size;
}

void resourceManager::SavePosition(void) {
    m_savedPosition = FileTell(m_aggregateFd);
}

void resourceManager::RestorePosition(void) {
    FileSeek(m_aggregateFd, m_savedPosition, FILE_SEEK_SET);
}

i8 resourceManager::ReadByte(void) {
    H1_ASSERT(m_aggregateFd != FILE_DESCRIPTOR_INVALID);
    u8 bytes[1] = {0};
    FileRead(m_aggregateFd, bytes, sizeof(bytes));
    i8 value;
    RecordReader reader(bytes, sizeof(bytes));
    reader.Get(value);
    return value;
}

i16 resourceManager::ReadWord(void) {
    H1_ASSERT(m_aggregateFd != FILE_DESCRIPTOR_INVALID);
    u8 bytes[2] = {0, 0};
    FileRead(m_aggregateFd, bytes, sizeof(bytes));
    i16 value;
    RecordReader reader(bytes, sizeof(bytes));
    reader.Get(value);
    return value;
}

i32 resourceManager::ReadLong(void) {
    H1_ASSERT(m_aggregateFd != FILE_DESCRIPTOR_INVALID);
    u8 bytes[4] = {0, 0, 0, 0};
    FileRead(m_aggregateFd, bytes, sizeof(bytes));
    i32 value;
    RecordReader reader(bytes, sizeof(bytes));
    reader.Get(value);
    return value;
}

i16 resourceManager::MakeId(char* name) {
    u32 result = MAKEFILEID(name);
    strcpy(m_lastFileName, name);
    m_lastFileId = result;
    return result;
}

void resourceManager::Read13(char* destination) {
    ReadBlock(destination, RESOURCE_NAME_CAPACITY);
}

void resourceManager::ReadBlock(void* destination, u32 size) {
    H1_ASSERT(m_aggregateFd != FILE_DESCRIPTOR_INVALID);
    PollSound();
    i32 bytesRead = FileRead(m_aggregateFd, destination, size);
    PollSound();
}
