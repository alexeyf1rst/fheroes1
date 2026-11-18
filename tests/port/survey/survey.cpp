// The long-running sanitizer survey (opt-in: -DHOMM1_SURVEY=ON; driven by
// tools/port/survey.py). It links the game's units with its own entry point,
// starts the game headless (SDL's dummy drivers) and plays it from the inside:
//
//   survey ai MAP DAYS SEED PLAYERS   a new game on MAPS\MAP with every player
//                                     a computer player, for DAYS days; each
//                                     week the game is saved, loaded and saved
//                                     again, and the two files must match
//   survey campaign SIDE SCENARIO DAYS SEED
//                                     a campaign scenario's opening, all
//                                     players computer-controlled
//   survey load SAVE DAYS SEED        a saved game (GAMES\SAVE), continued by
//                                     the computer
//   survey combat MAP COUNT SEED      COUNT battles fought on the combat
//                                     screen by two computer players with
//                                     random heroes, armies, spells and
//                                     artifacts, in the open, against
//                                     monsters and at towns
//
// Message boxes are answered by the input replay ("key return" every 200 ms)
// the runner passes in HOMM1_INPUT_REPLAY. Progress goes to stdout, so the
// runner can tell where a hang or a sanitizer report happened.

#include <H1/Ints.h>

#include <BASE/Misc.h>
#include <BASE/bitmap.h>
#include <BASE/executive.h>
#include <BASE/heroWindowManager.h>
#include <BASE/heroWindow.h>
#include <BASE/inputManager.h>
#include <BASE/miscwin.h>
#include <BASE/mouseManager.h>
#include <BASE/palette.h>
#include <BASE/resourceManager.h>
#include <PLATFORM/File.h>
#include <PLATFORM/Platform.h>
#include <SOURCE/KB.h>
#include <SOURCE/advManager.h>
#include <SOURCE/army.h>
#include <SOURCE/armyGroup.h>
#include <SOURCE/artifactTypes.h>
#include <SOURCE/combatManager.h>
#include <SOURCE/creatureTypes.h>
#include <SOURCE/game.h>
#include <SOURCE/hero.h>
#include <SOURCE/kbwin.h>
#include <SOURCE/mapCell.h>
#include <SOURCE/mapObjectTypes.h>
#include <SOURCE/philAI.h>
#include <SOURCE/playerData.h>
#include <SOURCE/spellTypes.h>
#include <SOURCE/town.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <pthread.h>
#endif

bool KBStartHost(const char* dataRoot, const char* gameArguments, i32 fullScreen);

namespace {

std::mt19937 gRandom;

// The watchdog: with HOMM1_SURVEY_WATCHDOG=SECONDS, a run that makes no
// progress for that long is stopped with SIGABRT on the main thread, so the
// sanitizer runtime (ASAN_OPTIONS=handle_abort=1) prints where it hangs.
std::atomic<long long> gLastProgress{0};

long long Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void StartWatchdog() {
    const char* setting = std::getenv("HOMM1_SURVEY_WATCHDOG");
    int seconds = setting != nullptr ? std::atoi(setting) : 0;
    if (seconds <= 0)
        return;
    gLastProgress = Now();
#ifndef _WIN32
    pthread_t mainThread = pthread_self();
    std::thread([mainThread, seconds] {
#else
    std::thread([seconds] {
#endif
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (Now() - gLastProgress > seconds) {
                std::fprintf(stderr, "FINDING hang: no progress for %d s\n", seconds);
                std::fflush(stderr);
#ifdef _WIN32
                std::abort();
#else
                pthread_kill(mainThread, SIGABRT);
#endif
                return;
            }
        }
    }).detach();
}

int Pick(int low, int high) {
    return std::uniform_int_distribution<int>(low, high)(gRandom);
}

void Progress(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Progress(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::vprintf(format, arguments);
    va_end(arguments);
    std::putchar('\n');
    std::fflush(stdout);
    gLastProgress = Now();
}

[[noreturn]] void Finish(int code) {
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}

// oldmain's start-up up to the main menu.
void StartGame() {
    if (gExec->InitSystem())
        Finish(2);
    KBChangeMenu(gDefaultMenu);
    gPalette = gResourceManager->GetPalette("kb.pal");
    PostprocessPalette(gPalette->m_data);
    SetPalette(gPalette->m_data, 1);
    gWindowManager->m_colorCycling = 1;
    gPhilAI->m_debugFont = gResourceManager->GetFont("smalfont.fnt");
    LoadSystemwideIcons();
    memset(gThisNetHumanPlayer, 0, GAME_PLAYER_COUNT);
    gMouseManager->SetPointer("advmice.mse", 0);
}

void AllComputerPlayers() {
    for (int i = 0; i < GAME_PLAYER_COUNT; i++) {
        gHumanPlayer[i] = 0;
        gThisNetHumanPlayer[i] = 0;
    }
}

// What oldmain does between the new game window and the adventure loop.
void EnterAdventure() {
    gGameInitialized = 1;
    gInCheckEndGame = 1;
    gMapX = 0;
    gMapY = 0;
    if (gExec->AddManager(gAdvManager, BASE_MANAGER_PRIORITY_UNASSIGNED))
        Finish(2);
}

void LeaveAdventure() {
    gExec->RemoveManager(gAdvManager);
}

void NewComputerGame(const char* map, int players) {
    gNumHumanPlayers = 1;
    gHumanPlayer[0] = 1;
    gGame->LoadGame("origdata.bin", 1, 0);
    AllComputerPlayers();
    gNumHumanPlayers = 0;
    gGame->m_difficulty = static_cast<i8>(Pick(0, DIFFICULTY_COUNT - 1));
    gGame->m_playerCount = static_cast<i8>(players);
    for (int i = 0; i < GAME_PLAYER_COUNT; i++)
        gGame->m_players[i].m_difficulty =
            static_cast<i8>(i < players ? Pick(PLAYER_TYPE_DUMB, PLAYER_TYPE_GENIUS) : PLAYER_TYPE_NONE);
    gGame->m_players[0].m_color = static_cast<i8>(Pick(0, GAME_PLAYER_COUNT - 1));
    gIAmGreatest = Pick(0, 1);
    std::snprintf(gGame->m_mapName, sizeof(gGame->m_mapName), "%s", map);
    gGame->NewMap(const_cast<char*>(map));
    Progress("new game %s: %d players, difficulty %d", map, gGame->m_playerCount,
             gGame->m_difficulty);
}

std::vector<u8> ReadGameFile(const char* gamePath) {
    char resolved[FILE_PATH_CAPACITY];
    std::vector<u8> bytes;
    if (!FileResolve(gamePath, FILE_OPEN_READ, resolved, sizeof(resolved)))
        return bytes;
    std::FILE* file = std::fopen(resolved, "rb");
    if (file == nullptr)
        return bytes;
    int c;
    while ((c = std::fgetc(file)) != EOF)
        bytes.push_back(static_cast<u8>(c));
    std::fclose(file);
    return bytes;
}

// Saves the game, loads it and saves it again: the files must be identical.
// The save marks player 0 human (the loader needs one human to watch), and
// the computer players take over again afterwards.
bool SaveLoadRoundTrip(int day) {
    i32 savedPlayer = gCurPlayer;
    gHumanPlayer[0] = 1;
    gGame->SaveGame(const_cast<char*>("SURVEY_A"), 1);
    std::vector<u8> first = ReadGameFile("GAMES\\SURVEY_A.GM1");
    gNumHumanPlayers = 1;
    gGame->LoadGame(const_cast<char*>("SURVEY_A.GM1"), 0, 0);
    gNumHumanPlayers = 0;
    gGame->SaveGame(const_cast<char*>("SURVEY_A"), 1);
    std::vector<u8> second = ReadGameFile("GAMES\\SURVEY_A.GM1");
    AllComputerPlayers();
    gCurPlayer = savedPlayer;
    gCurPlayerData = &gGame->m_players[gCurPlayer];
    gCurPlayerBit = 1 << gCurPlayer;
    gCurPlayerHighBit = 1 << (gCurPlayer + GAME_PLAYER_HIGH_BIT_SHIFT);
    if (first.empty() || first != second) {
        size_t at = 0;
        while (at < first.size() && at < second.size() && first[at] == second[at])
            at++;
        Progress("FINDING save-load: day %d: the saved game changes when loaded and saved "
                 "again (%zu vs %zu bytes, first difference at %zu)",
                 day, first.size(), second.size(), at);
        return false;
    }
    Progress("day %d: save, load, save identical (%zu bytes)", day, first.size());
    return true;
}

// CheckEndGame ends a game without a living human player. The survey
// keeps it from running on its own and runs it after each turn with the
// first living player standing in as the human watching the game, so that
// players are still eliminated and a single survivor still wins.
void CheckEndOfGame() {
    int watcher = -1;
    for (int i = 0; i < gGame->m_playerCount && watcher < 0; i++)
        if (!gGame->m_playerDead[i])
            watcher = i;
    if (watcher < 0)
        return;
    gHumanPlayer[watcher] = 1;
    gThisNetHumanPlayer[watcher] = 1;
    gInCheckEndGame = 0;
    CheckEndGame(0);
    gInCheckEndGame = 1;
    gHumanPlayer[watcher] = 0;
    gThisNetHumanPlayer[watcher] = 0;
}

// Every hero cell names a living hero standing there: a cell left behind
// by a hero that moved, died or was dismissed is drawn and visited as a
// hero without an owner.
void CheckMapConsistency(int day, const char* when) {
    for (int x = 0; x < MAP_CELL_GRID_SIZE; x++) {
        for (int y = 0; y < MAP_CELL_GRID_SIZE; y++) {
            const mapCell& cell = gGame->m_map[x][y];
            if (cell.m_triggerType != (MAP_TRIGGER_EVENT | MAP_OBJECT_HERO))
                continue;
            int id = cell.m_objectMetadata;
            if (id >= GAME_HERO_COUNT) {
                Progress("FINDING map-hero: day %d %s: cell %d,%d names hero %d", day, when, x, y, id);
                continue;
            }
            const hero& theHero = gGame->m_heroRecs[id];
            if (theHero.m_owner < 0 || theHero.m_x != x || theHero.m_y != y)
                Progress("FINDING map-hero: day %d %s: cell %d,%d names hero %d (owner %d at %d,%d)",
                         day, when, x, y, id, theHero.m_owner, theHero.m_x, theHero.m_y);
        }
    }
}

// The adventure loop for computer players (advManager::Main's branch for a
// player that is not human), for the given number of days.
void PlayDays(int days, bool roundTrips) {
    int played = 0;
    int lastDay = GAME_DAY_NUMBER(*gGame);
    while (played < days && !gGameOver) {
        Progress("day %d player %d: %d heroes, %d towns", GAME_DAY_NUMBER(*gGame), gCurPlayer,
                 gCurPlayerData->m_heroCount, gCurPlayerData->m_townCount);
        int player = gCurPlayer;
        gPhilAI->DoAI(gCurPlayer);
        char when[32];
        std::snprintf(when, sizeof(when), "after player %d", player);
        CheckMapConsistency(GAME_DAY_NUMBER(*gGame), when);
        if (gGameOver)
            break;
        CheckEndOfGame();
        if (gGameOver)
            break;
        gGame->NextPlayer();
        CheckEndOfGame();
        int day = GAME_DAY_NUMBER(*gGame);
        if (day != lastDay) {
            played++;
            lastDay = day;
            if (roundTrips && day % 7 == 1)
                SaveLoadRoundTrip(day);
        }
    }
    Progress("played %d days%s", played, gGameOver ? " (game over)" : "");
    int winner = -1;
    int living = 0;
    for (int player = 0; player < gGame->m_playerCount; ++player) {
        if (!gGame->m_playerDead[player]) {
            winner = player;
            ++living;
        }
    }
    Progress("PLAYTEST day=%d winner=%d", GAME_DAY_NUMBER(*gGame), living == 1 && gGameOver ? winner : -1);
}

// ---------------------------------------------------------------- combat

void RandomArmy(armyGroup& group, int strength) {
    for (int slot = 0; slot < ARMY_GROUP_SLOT_COUNT; slot++) {
        if (slot > 0 && Pick(0, 3) == 0) {
            group.m_creatureTypes[slot] = CREATURE_NONE;
            group.m_creatureCounts[slot] = 0;
            continue;
        }
        group.m_creatureTypes[slot] = static_cast<i8>(Pick(0, CREATURE_COUNT - 1));
        group.m_creatureCounts[slot] = static_cast<i16>(Pick(1, strength));
    }
}

void RandomHero(hero& theHero, int owner) {
    theHero.m_owner = static_cast<i8>(owner);
    for (int stat = 0; stat < HERO_PRIMARY_STAT_COUNT; stat++)
        theHero.m_primaryStats[stat] = static_cast<i8>(Pick(0, 12));
    theHero.m_morale = static_cast<i8>(Pick(-3, 3));
    theHero.m_luck = static_cast<i8>(Pick(-3, 3));
    RandomArmy(theHero.m_army, Pick(1, 3) == 1 ? 200 : 25);
    memset(theHero.m_spells, SPELL_NONE, sizeof(theHero.m_spells));
    memset(theHero.m_spellCharges, 0, sizeof(theHero.m_spellCharges));
    memset(theHero.m_artifacts, ARTIFACT_NONE, sizeof(theHero.m_artifacts));
    int artifacts = 0;
    if (Pick(0, 4) != 0)
        theHero.m_artifacts[artifacts++] = ARTIFACT_MAGIC_BOOK;
    int extra = Pick(0, 5);
    for (int i = 0; i < extra && artifacts < HERO_ARTIFACT_SLOT_COUNT; i++)
        theHero.m_artifacts[artifacts++] = static_cast<i8>(Pick(0, ARTIFACT_REGULAR_END - 1));
    int spells = Pick(0, 8);
    for (int i = 0; i < spells; i++)
        theHero.AddSpell(static_cast<i8>(Pick(0, HERO_COMBAT_SPELL_SLOT_COUNT - 1)),
                         static_cast<i8>(Pick(1, 5)), 0);
}

// A land cell without an object, for the battlefield's terrain.
bool OpenCell(int& x, int& y) {
    for (int tries = 0; tries < 10000; tries++) {
        x = Pick(1, MAP_CELL_GRID_SIZE - 2);
        y = Pick(1, MAP_CELL_GRID_SIZE - 2);
        mapCell* cell = gAdvManager->GetCell(static_cast<i16>(x), static_cast<i16>(y));
        if (cell->m_tileIndex >= MAP_CELL_TILES_PER_TERRAIN && cell->m_triggerType == 0)
            return true;
    }
    return false;
}

void Battles(int count) {
    for (int battle = 0; battle < count && !gGameOver; battle++) {
        int x;
        int y;
        if (!OpenCell(x, y))
            break;
        hero* attacker = &gGame->m_heroRecs[Pick(0, GAME_HERO_COUNT - 1)];
        hero* defender = &gGame->m_heroRecs[Pick(0, GAME_HERO_COUNT - 1)];
        while (defender == attacker)
            defender = &gGame->m_heroRecs[Pick(0, GAME_HERO_COUNT - 1)];
        int attackerOwner = Pick(0, gGame->m_playerCount - 1);
        int defenderOwner = (attackerOwner + Pick(1, gGame->m_playerCount - 1)) % gGame->m_playerCount;
        RandomHero(*attacker, attackerOwner);
        RandomHero(*defender, defenderOwner);
        int kind = Pick(0, 2);
        town* combatTown = nullptr;
        armyGroup monsters;
        armyGroup* secondArmy = &defender->m_army;
        hero* secondHero = defender;
        if (kind == 1) {
            // Wandering monsters: one creature type.
            secondHero = nullptr;
            secondArmy = &monsters;
            RandomArmy(monsters, 60);
            for (int slot = 1; slot < ARMY_GROUP_SLOT_COUNT; slot++)
                if (monsters.m_creatureTypes[slot] != CREATURE_NONE)
                    monsters.m_creatureTypes[slot] = monsters.m_creatureTypes[0];
        } else if (kind == 2) {
            // A town, with or without a visiting hero and a castle.
            int townId = Pick(0, GAME_TOWN_COUNT - 1);
            combatTown = &gGame->m_castleRecs[townId];
            if (combatTown->m_x <= 0 && combatTown->m_y <= 0) {
                combatTown = nullptr;
            } else {
                combatTown->m_owner = static_cast<i8>(defenderOwner);
                RandomArmy(combatTown->m_army, 40);
                if (Pick(0, 1))
                    combatTown->m_buildings |= 1 << BUILDING_SLOT_CASTLE;
                if (Pick(0, 1)) {
                    combatTown->m_occupyingHeroId = defender->m_id;
                } else {
                    combatTown->m_occupyingHeroId = TOWN_OCCUPYING_HERO_NONE;
                    secondHero = nullptr;
                }
                secondArmy = &combatTown->m_army;
                x = combatTown->m_x;
                y = combatTown->m_y;
            }
        }
        Progress("battle %d: %s at %d,%d; attacker hero %d (player %d), defender %s %d", battle,
                 kind == 0 ? "heroes" : kind == 1 ? "monsters" : "town", x, y, attacker->m_id,
                 attackerOwner, secondHero ? "hero" : "none", secondHero ? secondHero->m_id : -1);
        gCurPlayer = attackerOwner;
        gCurPlayerData = &gGame->m_players[gCurPlayer];
        // DoCombat runs from inside advManager::Main, and CallManager swaps
        // the running manager out for the combat manager.
        gExec->m_activeManager = gAdvManager;
        i32 result = gAdvManager->DoCombat(x, y, attacker, &attacker->m_army, combatTown, secondHero,
                                           secondArmy, x, y, Pick(1, 1000), 0);
        Progress("battle %d: result %d", battle, result);
    }
}

// Use the real adventure window with a full roster without modifying a save.
void CheckLabLocators() {
    auto& player = *gCurPlayerData;
    player.m_heroCount = player.m_townCount = 8;
    player.m_heroLocatorPage = player.m_townLocatorPage = 0;
    for (int i = 0; i < 8; ++i) {
        player.m_heroIds[i] = player.m_townIds[i] = static_cast<i8>(i);
        auto* hero = gGame->GetHero(i);
        hero->m_owner = gCurPlayer;
        hero->m_x = 10 + 3 * i;
        hero->m_y = 20;
        gGame->m_mapExtra[hero->m_x][hero->m_y] |= gCurPlayerBit;
        auto* town = gGame->GetTown(i);
        town->m_owner = gCurPlayer;
        town->m_x = 10 + 3 * i;
        town->m_y = 30;
        gGame->m_mapExtra[town->m_x][town->m_y] |= gCurPlayerBit;
    }
    gAllBlack = false;
    gShowIt = true;
    gAdvManager->RedrawAdvScreen(true);

    const auto expect = [](bool passed, const char* label) {
        Progress("%s %s", passed ? "ok" : "FAIL", label);
        if (!passed)
            Finish(1);
    };
    const auto wheel = [](int x, int delta, u32 time) {
        tag_message message{};
        message.x = x;
        message.y = 380;
        message.value = delta;
        message.mouseTimestamp = time;
        gAdvManager->ScrollLocators(message);
    };
    wheel(824, -15, 100);
    expect(player.m_heroLocatorPage == 1 && player.m_townLocatorPage == 0,
           "fractional input reaches eighth hero independently");
    wheel(896, -15, 110);
    expect(player.m_heroLocatorPage == 1 && player.m_townLocatorPage == 1,
           "fractional input reaches eighth town independently");

    const auto shiftLastRow = [&](int x, i16& id) {
        tag_message click{};
        click.type = MESSAGE_LEFT_BUTTON_DOWN;
        click.x = click.mouseX = x;
        click.y = click.mouseY = 382;
        click.modifiers = MESSAGE_MODIFIER_SHIFT_KEYS;
        gAdvManager->m_adventureWindow->BroadcastMessage(click);
        id = click.id;
        if (click.type != MESSAGE_WIDGET || click.command != WIDGET_NOTIFY_SELECT)
            return false;
        mapCell* cell = nullptr;
        gAdvManager->ProcessSelect(&click, &cell);
        return true;
    };
    i16 heroButton = -1, townButton = -1;
    expect(shiftLastRow(824, heroButton) && player.m_heroIds[0] == 7 && player.m_heroLocatorPage == 0,
           "clicking row seven reorders the eighth hero");
    expect(shiftLastRow(896, townButton) && player.m_townIds[0] == 7 && player.m_townLocatorPage == 0
               && townButton != heroButton,
           "clicking row seven reorders the eighth town");
    wheel(824, -120, 500);
    wheel(896, -120, 510);
    wheel(824, 120, 550);
    wheel(896, 120, 560);
    expect(player.m_heroLocatorPage == 0 && player.m_townLocatorPage == 0,
           "reversing wheel direction restores both first pages");

    const int markerX = RADAR_LEFT + 31 * RADAR_CELL_PIXELS;
    expect(gAdvManager->HeroAtRadarPoint(markerX, RADAR_TOP + 20 * RADAR_CELL_PIXELS) == 7,
           "mini-map finds the eighth hero");
    expect(gAdvManager->TownAtRadarPoint(markerX, RADAR_TOP + 30 * RADAR_CELL_PIXELS) == 7,
           "mini-map finds the eighth town");
    expect(gAdvManager->HeroAtRadarPoint(RADAR_LEFT - 1, RADAR_TOP) == HERO_ID_NONE,
           "clicks outside the mini-map cannot target a hero");
    int box[4];
    expect(platform::CompareWithReference(box) == 0, "sidebar uploads match the game buffer");

    gAdvManager->PanMap(10, 10);
    auto* screen = gWindowManager->m_screen;
    const auto borderPixel = screen->m_pixels[0];
    expect(gAdvManager->BeginMapDrag(300, 280)
               && !gAdvManager->DragMap(297, 280)
               && gAdvManager->DragMap(291, 275),
           "small clicks stay clicks and an intentional drag starts");
    const auto* frame = gAdvManager->m_cameraFrame;
    const auto sample = [&](int dx, int dy) {
        return std::memcmp(screen->m_pixels + (BORDER_EDGE_SIZE + 40) * screen->m_width + BORDER_EDGE_SIZE,
                           frame->m_pixels + (BORDER_EDGE_SIZE + 40 + dy) * frame->m_width
                               + BORDER_EDGE_SIZE + dx,
                           64) == 0;
    };
    expect(gAdvManager->m_mapOriginX == 10 && gAdvManager->m_cameraOffsetX == 9
               && gAdvManager->m_cameraOffsetY == 5 && sample(9, 5),
           "drag moves by pixels within the same cell");
    expect(gAdvManager->DragMap(282, 275) && gAdvManager->m_mapOriginX == 11
               && gAdvManager->m_cameraOffsetX == -14 && sample(-14, 5),
           "drag crosses the cell boundary without losing the remaining movement");
    expect(screen->m_pixels[0] == borderPixel && platform::CompareWithReference(box) == 0,
           "drag keeps the frame fixed and uploads the correct map");
    gAdvManager->EndMapDrag();
    expect(!gAdvManager->m_mapDragging && sample(-14, 5) && platform::CompareWithReference(box) == 0,
           "release keeps the last displayed camera position");
    i16 cellX, cellY;
    expect(gAdvManager->MapCellAtScreenPoint(37, 27, cellX, cellY) && cellX == 0 && cellY == 1
               && !gAdvManager->MapCellAtScreenPoint(1, 27, cellX, cellY),
           "clicks use the displayed tiles and reject the decorative border");
    tag_message hover{};
    hover.id = ADVENTURE_CONTROL_MAP_VIEW;
    hover.mouseX = 37;
    hover.mouseY = 27;
    gAdvManager->ProcessHover(&hover);
    expect(gAdvManager->m_commandTargetX == 11 && gAdvManager->m_commandTargetY == 11,
           "map input targets the visible cell at the released camera position");
    expect(gAdvManager->HeroAtRadarPoint(markerX, RADAR_TOP + 20 * RADAR_CELL_PIXELS) == 7
               && gAdvManager->TownAtRadarPoint(markerX, RADAR_TOP + 30 * RADAR_CELL_PIXELS) == 7,
           "hero and town markers still resolve with the camera between cells");
    gAdvManager->CompleteDraw(false);
    gAdvManager->UpdateScreen(false, false);
    expect(sample(-14, 5) && platform::CompareWithReference(box) == 0,
           "a full map redraw preserves the fractional camera");
    expect(gAdvManager->BeginMapDrag(300, 280) && gAdvManager->DragMap(291, 280)
               && gAdvManager->m_mapOriginX == 11 && gAdvManager->m_cameraOffsetX == -5
               && sample(-5, 5),
           "a second drag starts exactly where the previous drag ended");
    gAdvManager->EndMapDrag();
    gAdvManager->PanMap(11, 10);
    expect(!gAdvManager->HasCameraOffset() && platform::CompareWithReference(box) == 0,
           "an explicit camera jump recenters the map");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: survey ai MAP DAYS SEED PLAYERS | campaign SIDE SCENARIO DAYS SEED | "
                     "load SAVE DAYS SEED | combat MAP COUNT SEED | lab MAP\n");
        return 2;
    }
    std::string mode = argv[1];
    auto argument = [&](int index, int fallback) {
        return index < argc ? std::atoi(argv[index]) : fallback;
    };
    const auto setEnvironment = [](const char* key, const char* value, bool overwrite) {
        if (overwrite || std::getenv(key) == nullptr) {
#ifdef _WIN32
            _putenv_s(key, value);
#else
            setenv(key, value, 1);
#endif
        }
    };
    setEnvironment("SDL_VIDEODRIVER", "dummy", false);
    // SIGTERM ends the program at once (timeout, ctest); SDL would turn it into
    // a quit event that a hung loop never reads.
    setEnvironment("SDL_NO_SIGNAL_HANDLERS", "1", false);
    setEnvironment("SDL_AUDIO_DRIVER", "dummy", false);
    setEnvironment("HOMM1_NO_DIALOGS", "1", true);
    if (mode == "lab")
        setEnvironment("HOMM1_NATIVE_SIZE", "960x600", true);
    if (!KBStartHost(nullptr, "/I0", 0))
        return 2;
    StartWatchdog();
    StartGame();

    int seed = 0;
    if (mode == "ai") {
        seed = argument(4, 1);
        gRandom.seed(static_cast<u32>(seed));
        srand(static_cast<u32>(seed));
        NewComputerGame(argv[2], argument(5, GAME_PLAYER_COUNT));
        EnterAdventure();
        PlayDays(argument(3, 30), true);
        LeaveAdventure();
    } else if (mode == "campaign") {
        seed = argument(5, 1);
        gRandom.seed(static_cast<u32>(seed));
        srand(static_cast<u32>(seed));
        int side = argument(2, 1);
        int scenario = argument(3, 0);
        // LoadGame waits for a human player to watch the game.
        gNumHumanPlayers = 1;
        gHumanPlayer[0] = 1;
        gGame->InitEntireCampaign(side);
        if (scenario != 0) {
            gGame->m_campaignScenario = scenario;
            gGame->InitCampaignMap(scenario, 0);
        }
        AllComputerPlayers();
        gNumHumanPlayers = 0;
        Progress("campaign side %d scenario %d", side, scenario);
        EnterAdventure();
        PlayDays(argument(4, 7), true);
        LeaveAdventure();
    } else if (mode == "load") {
        seed = argument(4, 1);
        gRandom.seed(static_cast<u32>(seed));
        srand(static_cast<u32>(seed));
        gNumHumanPlayers = 1;
        gGame->LoadGame(argv[2], 0, 0);
        AllComputerPlayers();
        gNumHumanPlayers = 0;
        Progress("loaded %s: day %d", argv[2], GAME_DAY_NUMBER(*gGame));
        EnterAdventure();
        PlayDays(argument(3, 14), true);
        LeaveAdventure();
    } else if (mode == "lab" && argc >= 3) {
        NewComputerGame(argv[2], GAME_PLAYER_COUNT);
        gThisNetHumanPlayer[gCurPlayer] = true;
        EnterAdventure();
        CheckLabLocators();
        LeaveAdventure();
    } else if (mode == "combat") {
        seed = argument(4, 1);
        gRandom.seed(static_cast<u32>(seed));
        srand(static_cast<u32>(seed));
        NewComputerGame(argv[2], GAME_PLAYER_COUNT);
        EnterAdventure();
        Battles(argument(3, 10));
        LeaveAdventure();
    } else {
        std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
        return 2;
    }
    Progress("survey done");
    Finish(0);
}
