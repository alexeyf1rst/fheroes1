/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2026                                             *
 *                                                                         *
 *   Free Heroes2 Engine: http://sourceforge.net/projects/fheroes2         *
 *   Copyright (C) 2009 by Andrey Afletdinov <fheroes2@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "agg_image.h"
#include "audio.h"
#include "audio_manager.h"
#include "cursor.h"
#include "dialog.h"
#include "dialog_game_settings.h"
#include "dialog_language_selection.h"
#include "dialog_resolution.h"
#include "editor_mainmenu.h"
#include "game.h" // IWYU pragma: associated
#include "game_delays.h"
#include "game_exit.h"
#include "game_hotkeys.h"
#include "game_mainmenu_ui.h"
#include "game_mode.h"
#include "icn.h"
#include "image.h"
#include "localevent.h"
#include "logging.h"
#include "math_base.h"
#include "mus.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_language.h"
#include "ui_tool.h"

namespace
{
    // HoMM1 BTNMAIN.ICN frame indices: even = released, odd = pressed.
    enum
    {
        H1_LOADGAME_RELEASED = 0,
        H1_LOADGAME_PRESSED = 1,
        H1_NEWGAME_RELEASED = 2,
        H1_NEWGAME_PRESSED = 3,
        H1_HIGHSCORES_RELEASED = 4,
        H1_HIGHSCORES_PRESSED = 5,
        H1_CREDITS_RELEASED = 6,
        H1_CREDITS_PRESSED = 7,
        H1_QUIT_RELEASED = 8,
        H1_QUIT_PRESSED = 9
    };

    // HoMM1 button absolute screen positions (within 640×480 layout).
    constexpr int32_t H1_BTN_X = 459;
    constexpr int32_t H1_BTN_NEWGAME_Y = 80;
    constexpr int32_t H1_BTN_LOADGAME_Y = 146;
    constexpr int32_t H1_BTN_HIGHSCORES_Y = 212;
    constexpr int32_t H1_BTN_CREDITS_Y = 278;
    constexpr int32_t H1_BTN_QUIT_Y = 344;

    void outputMainMenuInTextSupportMode()
    {
        START_TEXT_SUPPORT_MODE
        COUT( "Main Menu\n" )

        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_NEW_GAME ) << " to choose New Game." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_LOAD_GAME ) << " to choose Load previously saved game." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_HIGHSCORES ) << " to show High Scores." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_CREDITS ) << " to show Credits." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_SETTINGS ) << " to open Game Settings." )

        if ( Settings::Get().isPriceOfLoyaltySupported() ) {
            COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::EDITOR_MAIN_MENU ) << " to open Editor." )
        }

        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::GLOBAL_APP_QUIT ) << " or "
                       << Game::getHotKeyNameByEventId( Game::HotKeyEvent::DEFAULT_CANCEL ) << " to Quit the game." )
    }
}

void Game::mainGameLoop( bool isFirstGameRun, bool isProbablyDemoVersion )
{
    fheroes2::GameMode result = fheroes2::GameMode::MAIN_MENU;

    bool exit = false;

    while ( !exit ) {
        switch ( result ) {
        case fheroes2::GameMode::QUIT_GAME:
            exit = true;
            break;
        case fheroes2::GameMode::MAIN_MENU:
            result = Game::MainMenu( isFirstGameRun );
            isFirstGameRun = false;
            break;
        case fheroes2::GameMode::NEW_GAME:
            result = Game::NewGame( isProbablyDemoVersion );
            isProbablyDemoVersion = false;
            break;
        case fheroes2::GameMode::LOAD_GAME:
            result = Game::LoadGame();
            break;
        case fheroes2::GameMode::HIGHSCORES_STANDARD:
            result = Game::DisplayHighScores( false );
            break;
        case fheroes2::GameMode::HIGHSCORES_CAMPAIGN:
            result = Game::DisplayHighScores( true );
            break;
        case fheroes2::GameMode::CREDITS:
            result = Game::Credits();
            break;
        case fheroes2::GameMode::NEW_STANDARD:
            result = Game::NewStandard();
            break;
        case fheroes2::GameMode::NEW_SUCCESSION_WARS_CAMPAIGN:
            result = Game::NewSuccessionWarsCampaign();
            break;
        case fheroes2::GameMode::NEW_HOMM1_CAMPAIGN:
            result = Game::NewHoMM1Campaign();
            break;
        case fheroes2::GameMode::NEW_BATTLE_ONLY:
            result = Game::NewBattleOnly();
            break;
        case fheroes2::GameMode::LOAD_STANDARD:
            result = Game::LoadStandard();
            break;
        case fheroes2::GameMode::LOAD_CAMPAIGN:
            result = Game::LoadCampaign();
            break;
        case fheroes2::GameMode::LOAD_HOT_SEAT:
            result = Game::LoadHotseat();
            break;
        case fheroes2::GameMode::SELECT_SCENARIO_ONE_HUMAN_PLAYER:
        case fheroes2::GameMode::SELECT_SCENARIO_TWO_HUMAN_PLAYERS:
        case fheroes2::GameMode::SELECT_SCENARIO_THREE_HUMAN_PLAYERS:
        case fheroes2::GameMode::SELECT_SCENARIO_FOUR_HUMAN_PLAYERS:
        case fheroes2::GameMode::SELECT_SCENARIO_FIVE_HUMAN_PLAYERS:
        case fheroes2::GameMode::SELECT_SCENARIO_SIX_HUMAN_PLAYERS: {
            const uint8_t humanPlayerCount
                = static_cast<uint8_t>( static_cast<int>( result ) - static_cast<int>( fheroes2::GameMode::SELECT_SCENARIO_ONE_HUMAN_PLAYER ) );
            // Add +1 since we don't have zero human player option.
            result = Game::SelectScenario( humanPlayerCount + 1 );
            break;
        }
        case fheroes2::GameMode::START_GAME:
            result = Game::StartGame();
            break;
        case fheroes2::GameMode::SELECT_CAMPAIGN_SCENARIO:
            result = Game::SelectCampaignScenario( fheroes2::GameMode::MAIN_MENU, false );
            break;
        case fheroes2::GameMode::COMPLETE_CAMPAIGN_SCENARIO:
            result = Game::CompleteCampaignScenario( false );
            break;
        case fheroes2::GameMode::COMPLETE_CAMPAIGN_SCENARIO_FROM_LOAD_FILE:
            result = Game::CompleteCampaignScenario( true );
            while ( result == fheroes2::GameMode::SELECT_CAMPAIGN_SCENARIO ) {
                result = Game::SelectCampaignScenario( fheroes2::GameMode::LOAD_CAMPAIGN, false );
            }
            break;
        case fheroes2::GameMode::START_BATTLE_ONLY_MODE:
            result = Game::StartBattleOnly();
            break;
        case fheroes2::GameMode::EDITOR_MAIN_MENU:
            result = Editor::menuMain( false );
            break;
        case fheroes2::GameMode::EDITOR_NEW_MAP:
            result = Editor::menuMain( true );
            break;
        case fheroes2::GameMode::EDITOR_LOAD_MAP:
            result = Editor::menuLoadMap();
            break;

        default:
            // If this assertion blows up then you are entering an infinite loop!
            // Add the logic for the newly added entry.
            assert( 0 );
            exit = true;
            break;
        }
    }

    // We are quitting the game, so fade-out the screen.
    fheroes2::fadeOutDisplay();
}

fheroes2::GameMode Game::MainMenu( const bool isFirstGameRun )
{
    // Stop all sounds, but not the music
    AudioManager::stopSounds();

    AudioManager::PlayMusicAsync( MUS::MAINMENU, Music::PlaybackMode::RESUME_AND_PLAY_INFINITE );

    Settings & conf = Settings::Get();

    conf.SetGameType( TYPE_MENU );

    // setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::drawMainMenuScreen();

    if ( isFirstGameRun ) {
        // Fade in Main Menu image before showing messages. This also resets the "need fade" state to have no fade-in after these messages.
        fheroes2::validateFadeInAndRender();

        fheroes2::selectLanguage( fheroes2::getSupportedLanguages(), fheroes2::getLanguageFromAbbreviation( conf.getGameLanguage() ), true );

        {
            std::string body( _( "Welcome to Heroes of Might and Magic I powered by fheroes2 engine!" ) );

            if ( System::isTouchInputAvailable() ) {
                body += _(
                    "\n\nTo simulate a right-click with a touch to get info on various items, you need to first touch and keep touching on the item of interest and then touch anywhere else on the screen. While holding the second finger, you can remove the first one from the screen and keep viewing the information on the item." );
            }

            // Handheld devices should use the minimal game's resolution. Users on handheld devices aren't asked to choose resolution.
            if ( !System::isHandheldDevice() ) {
                body += _( "\n\nBefore starting the game, please select a game resolution." );
            }

            fheroes2::showStandardTextMessage( _( "Greetings!" ), std::move( body ), Dialog::OK );

            if ( !System::isHandheldDevice() && Dialog::SelectResolution() ) {
                fheroes2::drawMainMenuScreen();
            }
        }

        conf.resetFirstGameRun();
        conf.Save( Settings::configFileName );
    }

    outputMainMenuInTextSupportMode();

    LocalEvent & le = LocalEvent::Get();

    // HoMM1 buttons at their fixed screen positions within the 640×480 layout.
    fheroes2::Button buttonNewGame( H1_BTN_X, H1_BTN_NEWGAME_Y, ICN::H1BTNS, H1_NEWGAME_RELEASED, H1_NEWGAME_PRESSED );
    fheroes2::Button buttonLoadGame( H1_BTN_X, H1_BTN_LOADGAME_Y, ICN::H1BTNS, H1_LOADGAME_RELEASED, H1_LOADGAME_PRESSED );
    fheroes2::Button buttonHighScores( H1_BTN_X, H1_BTN_HIGHSCORES_Y, ICN::H1BTNS, H1_HIGHSCORES_RELEASED, H1_HIGHSCORES_PRESSED );
    fheroes2::Button buttonCredits( H1_BTN_X, H1_BTN_CREDITS_Y, ICN::H1BTNS, H1_CREDITS_RELEASED, H1_CREDITS_PRESSED );
    fheroes2::Button buttonQuit( H1_BTN_X, H1_BTN_QUIT_Y, ICN::H1BTNS, H1_QUIT_RELEASED, H1_QUIT_PRESSED );

    buttonNewGame.draw();
    buttonLoadGame.draw();
    buttonHighScores.draw();
    buttonCredits.draw();
    buttonQuit.draw();

    fheroes2::validateFadeInAndRender();

    while ( true ) {
        if ( !le.HandleEvents( true, true ) ) {
            if ( Game::processExitEvent() == fheroes2::GameMode::QUIT_GAME ) {
                break;
            }
            else {
                continue;
            }
        }

        buttonNewGame.drawOnState( le.isMouseLeftButtonPressedInArea( buttonNewGame.area() ) );
        buttonLoadGame.drawOnState( le.isMouseLeftButtonPressedInArea( buttonLoadGame.area() ) );
        buttonHighScores.drawOnState( le.isMouseLeftButtonPressedInArea( buttonHighScores.area() ) );
        buttonCredits.drawOnState( le.isMouseLeftButtonPressedInArea( buttonCredits.area() ) );
        buttonQuit.drawOnState( le.isMouseLeftButtonPressedInArea( buttonQuit.area() ) );

        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_NEW_GAME ) || le.MouseClickLeft( buttonNewGame.area() ) ) {
            return fheroes2::GameMode::NEW_GAME;
        }

        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_LOAD_GAME ) || le.MouseClickLeft( buttonLoadGame.area() ) ) {
            return fheroes2::GameMode::LOAD_GAME;
        }

        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_HIGHSCORES ) || le.MouseClickLeft( buttonHighScores.area() ) ) {
            return fheroes2::GameMode::HIGHSCORES_STANDARD;
        }

        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_CREDITS ) || le.MouseClickLeft( buttonCredits.area() ) ) {
            return fheroes2::GameMode::CREDITS;
        }

        if ( HotKeyPressEvent( HotKeyEvent::GLOBAL_APP_QUIT ) || HotKeyPressEvent( HotKeyEvent::DEFAULT_CANCEL ) || le.MouseClickLeft( buttonQuit.area() ) ) {
            if ( Game::processExitEvent() == fheroes2::GameMode::QUIT_GAME ) {
                return fheroes2::GameMode::QUIT_GAME;
            }
        }
        else if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_SETTINGS ) ) {
            fheroes2::openGameSettings();
            return fheroes2::GameMode::MAIN_MENU;
        }

        // right-click info
        if ( le.isMouseRightButtonPressedInArea( buttonQuit.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Quit" ), _( "Quit Heroes of Might and Magic and return to the operating system." ), Dialog::ZERO );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonLoadGame.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Load Game" ), _( "Load a previously saved game." ), Dialog::ZERO );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonCredits.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Credits" ), _( "View the credits screen." ), Dialog::ZERO );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonHighScores.area() ) ) {
            fheroes2::showStandardTextMessage( _( "High Scores" ), _( "View the high scores screen." ), Dialog::ZERO );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonNewGame.area() ) ) {
            fheroes2::showStandardTextMessage( _( "New Game" ), _( "Start a single or multi-player game." ), Dialog::ZERO );
        }
    }

    return fheroes2::GameMode::QUIT_GAME;
}
