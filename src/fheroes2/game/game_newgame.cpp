/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2025                                             *
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

#include "game.h" // IWYU pragma: associated

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "agg.h"
#include "agg_image.h"
#include "audio.h"
#include "audio_manager.h"
#include "campaign_savedata.h"
#include "campaign_scenariodata.h"
#include "cursor.h"
#include "dialog.h"
#include "game_delays.h"
#include "game_hotkeys.h"
#include "game_io.h"
#include "game_mainmenu_ui.h"
#include "game_mode.h"
#include "game_video.h"
#include "game_video_type.h"
#include "icn.h"
#include "image.h"
#include "localevent.h"
#include "logging.h"
#include "maps_fileinfo.h"
#include "math_base.h"
#include "serialize.h"
#include "mus.h"
#include "screen.h"
#include "settings.h"
#include "smk_decoder.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "ui_tool.h"
#include "ui_window.h"
#include "world.h"

namespace
{
    const std::array<fheroes2::GameMode, 5> playerCountModes
        = { fheroes2::GameMode::SELECT_SCENARIO_TWO_HUMAN_PLAYERS, fheroes2::GameMode::SELECT_SCENARIO_THREE_HUMAN_PLAYERS,
            fheroes2::GameMode::SELECT_SCENARIO_FOUR_HUMAN_PLAYERS, fheroes2::GameMode::SELECT_SCENARIO_FIVE_HUMAN_PLAYERS,
            fheroes2::GameMode::SELECT_SCENARIO_SIX_HUMAN_PLAYERS };

    std::unique_ptr<SMKVideoSequence> getVideo( const std::string & fileName )
    {
        std::string videoPath;
        if ( !Video::getVideoFilePath( fileName, videoPath ) ) {
            return nullptr;
        }

        std::unique_ptr<SMKVideoSequence> video = std::make_unique<SMKVideoSequence>( videoPath );
        if ( video->frameCount() < 1 ) {
            return nullptr;
        }

        return video;
    }

    void outputNewMenuInTextSupportMode()
    {
        START_TEXT_SUPPORT_MODE

        COUT( "New Game\n" )

        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_STANDARD ) << " to choose Standard Game." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_CAMPAIGN ) << " to choose Campaign Game." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_MULTI ) << " to choose Multiplayer Game." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_BATTLEONLY ) << " to choose Battle Only Game." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_SETTINGS ) << " to open Game Settings." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::DEFAULT_CANCEL ) << " to come back to Main Menu." )
    }

    void outputNewSuccessionWarsCampaignInTextSupportMode()
    {
        START_TEXT_SUPPORT_MODE

        COUT( "Choose your Lord:\n" )

        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::CAMPAIGN_ROLAND ) << " to choose Roland Campaign." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::CAMPAIGN_ARCHIBALD ) << " to choose Archibald Campaign." )
    }


    void showMissingVideoFilesWindow()
    {
        fheroes2::showStandardTextMessage( _( "Warning" ),
                                           _( "The required video files for the campaign selection window are missing. "
                                              "Please make sure that all necessary files are present in the system." ),
                                           Dialog::OK );
    }
}

fheroes2::GameMode Game::NewStandard()
{
    Settings & conf = Settings::Get();
    if ( conf.isCampaignGameType() ) {
        conf.setCurrentMapInfo( {} );
    }
    conf.SetGameType( Game::TYPE_STANDARD );
    return fheroes2::GameMode::SELECT_SCENARIO_ONE_HUMAN_PLAYER;
}

fheroes2::GameMode Game::NewBattleOnly()
{
    Settings & conf = Settings::Get();
    conf.SetGameType( Game::TYPE_BATTLEONLY );
    // Redraw the empty main menu screen to show it after the battle using screen restorer.
    fheroes2::drawMainMenuScreen();
    return fheroes2::GameMode::START_BATTLE_ONLY_MODE;
}

fheroes2::GameMode Game::NewHotSeat( const size_t playerCountOptionIndex )
{
    assert( playerCountOptionIndex < playerCountModes.size() );
    Settings & conf = Settings::Get();
    if ( conf.isCampaignGameType() ) {
        conf.setCurrentMapInfo( {} );
    }
    conf.SetGameType( Game::TYPE_HOTSEAT );
    return playerCountModes[playerCountOptionIndex];
}

fheroes2::GameMode Game::NewSuccessionWarsCampaign()
{
    Settings::Get().SetGameType( Game::TYPE_CAMPAIGN );

    // Reset all sound and music before playing videos
    AudioManager::ResetAudio();

    fheroes2::Display & display = fheroes2::Display::instance();
    const fheroes2::Point roiOffset( ( display.width() - display.DEFAULT_WIDTH ) / 2, ( display.height() - display.DEFAULT_HEIGHT ) / 2 );

    // Fade-out screen before playing video.
    fheroes2::fadeOutDisplay();

    const fheroes2::Text loadingScreen( _( "Loading video. Please wait..." ), fheroes2::FontType::normalWhite() );
    loadingScreen.draw( display.width() / 2 - loadingScreen.width() / 2, display.height() / 2 - loadingScreen.height() / 2 + 2, display );
    display.render();

    Video::ShowVideo( { { "INTRO.SMK", Video::VideoControl::PLAY_CUTSCENE } } );

    Campaign::CampaignSaveData & campaignSaveData = Campaign::CampaignSaveData::Get();
    campaignSaveData.reset();

    std::unique_ptr<SMKVideoSequence> video = getVideo( "CHOOSE.SMK" );
    if ( !video ) {
        showMissingVideoFilesWindow();
        campaignSaveData.setCurrentScenarioInfo( { Campaign::IRONFIST_CAMPAIGN, 0 } );
        return fheroes2::GameMode::SELECT_CAMPAIGN_SCENARIO;
    }

    const std::array<fheroes2::Rect, 2> campaignRoi{ fheroes2::Rect( 382 + roiOffset.x, 58 + roiOffset.y, 222, 298 ),
                                                     fheroes2::Rect( 30 + roiOffset.x, 59 + roiOffset.y, 224, 297 ) };

    const uint64_t customDelay = static_cast<uint64_t>( std::lround( video->microsecondsPerFrame() / 1000 ) );

    outputNewSuccessionWarsCampaignInTextSupportMode();

    AudioManager::ResetAudio();
    std::unique_ptr<SMKVideoSequence> audio = getVideo( "CHOOSEW.SMK" );
    if ( audio ) {
        for ( auto channel : audio->getAudioChannels() ) {
            Mixer::Play( channel.data(), static_cast<uint32_t>( channel.size() ), false );
        }
    }

    const fheroes2::ScreenPaletteRestorer screenRestorer;

    std::vector<uint8_t> palette = video->getCurrentPalette();
    screenRestorer.changePalette( palette.data() );

    const CursorRestorer cursorRestorer( true, Cursor::POINTER );
    Cursor::Get().setVideoPlaybackCursor();

    // Immediately indicate that the delay has passed to render first frame immediately.
    Game::passCustomAnimationDelay( customDelay );
    // Make sure that the first run is passed immediately.
    assert( !Game::isCustomDelayNeeded( customDelay ) );

    LocalEvent & le = LocalEvent::Get();
    while ( le.HandleEvents( Game::isCustomDelayNeeded( customDelay ) ) ) {
        if ( le.MouseClickLeft( campaignRoi[0] ) || HotKeyPressEvent( HotKeyEvent::CAMPAIGN_ROLAND ) ) {
            campaignSaveData.setCurrentScenarioInfo( { Campaign::IRONFIST_CAMPAIGN, 0 } );
            break;
        }
        if ( le.MouseClickLeft( campaignRoi[1] ) || HotKeyPressEvent( HotKeyEvent::CAMPAIGN_ARCHIBALD ) ) {
            campaignSaveData.setCurrentScenarioInfo( { Campaign::SLAYER_CAMPAIGN, 0 } );
            break;
        }

        size_t highlightCampaignId = campaignRoi.size();

        for ( size_t i = 0; i < campaignRoi.size(); ++i ) {
            if ( le.isMouseCursorPosInArea( campaignRoi[i] ) ) {
                highlightCampaignId = i;
                break;
            }
        }

        if ( Game::validateCustomAnimationDelay( customDelay ) ) {
            fheroes2::Rect frameRoi( roiOffset.x, roiOffset.y, 0, 0 );
            video->getNextFrame( display, frameRoi.x, frameRoi.y, frameRoi.width, frameRoi.height, palette );

            if ( highlightCampaignId < campaignRoi.size() ) {
                const fheroes2::Rect & roi = campaignRoi[highlightCampaignId];
                fheroes2::DrawRect( display, roi, 51 );
                fheroes2::DrawRect( display, { roi.x - 1, roi.y - 1, roi.width + 2, roi.height + 2 }, 51 );
            }

            display.render( frameRoi );

            if ( video->frameCount() <= video->getCurrentFrameId() ) {
                video->resetFrame();
            }
        }
    }

    screenRestorer.changePalette( nullptr );

    // Update the frame but do not render it.
    display.fill( 0 );
    display.updateNextRenderRoi( { 0, 0, display.width(), display.height() } );

    // Set the fade-in for the Campaign scenario info.
    setDisplayFadeIn();

    return fheroes2::GameMode::SELECT_CAMPAIGN_SCENARIO;
}

fheroes2::GameMode Game::NewHoMM1Campaign()
{
    // Stop all sounds, but not the music
    AudioManager::stopSounds();

    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::drawMainMenuScreen();

    // Campaign hero buttons: same positions as the main menu buttons (BTNCMPGN.ICN, 127×63 each).
    // Frames: 0/1=Ironfist, 2/3=Slayer, 4/5=Lamanda, 6/7=Alamar, 8/9=Cancel
    constexpr int32_t btnX = 459;
    fheroes2::Button buttonIronfist( btnX, 80,  ICN::H1BTNCAMP, 0, 1 );
    fheroes2::Button buttonSlayer(   btnX, 146, ICN::H1BTNCAMP, 2, 3 );
    fheroes2::Button buttonLamanda(  btnX, 212, ICN::H1BTNCAMP, 4, 5 );
    fheroes2::Button buttonAlamar(   btnX, 278, ICN::H1BTNCAMP, 6, 7 );
    fheroes2::Button buttonCancel(   btnX, 344, ICN::H1BTNCAMP, 8, 9 );

    buttonIronfist.draw();
    buttonSlayer.draw();
    buttonLamanda.draw();
    buttonAlamar.draw();
    buttonCancel.draw();

    fheroes2::validateFadeInAndRender();

    LocalEvent & le = LocalEvent::Get();

    while ( le.HandleEvents() ) {
        buttonIronfist.drawOnState( le.isMouseLeftButtonPressedInArea( buttonIronfist.area() ) );
        buttonSlayer.drawOnState( le.isMouseLeftButtonPressedInArea( buttonSlayer.area() ) );
        buttonLamanda.drawOnState( le.isMouseLeftButtonPressedInArea( buttonLamanda.area() ) );
        buttonAlamar.drawOnState( le.isMouseLeftButtonPressedInArea( buttonAlamar.area() ) );
        buttonCancel.drawOnState( le.isMouseLeftButtonPressedInArea( buttonCancel.area() ) );

        const auto startHoMM1Campaign = [&]( int campaignId ) -> fheroes2::GameMode {
            Campaign::CampaignSaveData::Get().reset();
            Campaign::CampaignSaveData::Get().setCurrentScenarioInfo( { campaignId, 0 } );

            // Load CAMP1.CMP from the MAPS/ directory.
            std::string campFilePath;
            if ( !Maps::tryGetMatchingFile( "camp1.cmp", campFilePath ) ) {
                fheroes2::showStandardTextMessage( _( "Error" ), _( "Campaign map CAMP1.CMP not found in MAPS/." ), Dialog::OK );
                return fheroes2::GameMode::NEW_GAME;
            }

            StreamFile fs;
            if ( !fs.open( campFilePath, "rb" ) ) {
                fheroes2::showStandardTextMessage( _( "Error" ), _( "Could not open CAMP1.CMP." ), Dialog::OK );
                return fheroes2::GameMode::NEW_GAME;
            }
            std::vector<uint8_t> mapData = fs.getRaw( fs.size() );

            Maps::FileInfo mapInfo;
            if ( !mapInfo.readHoMM1MapFromBytes( mapData, campFilePath ) ) {
                fheroes2::showStandardTextMessage( _( "Error" ), _( "Campaign map not found." ), Dialog::OK );
                return fheroes2::GameMode::NEW_GAME;
            }

            Settings & conf = Settings::Get();
            conf.setCurrentMapInfo( mapInfo );
            conf.SetGameType( Game::TYPE_CAMPAIGN );
            conf.GetPlayers().SetStartGame();

            if ( world.loadHoMM1Map( mapData ) ) {
                fheroes2::fadeOutDisplay();
                Game::setDisplayFadeIn();
                return fheroes2::GameMode::START_GAME;
            }

            fheroes2::showStandardTextMessage( _( "Error" ), _( "Could not load campaign map." ), Dialog::OK );
            return fheroes2::GameMode::NEW_GAME;
        };

        if ( le.MouseClickLeft( buttonIronfist.area() ) ) {
            return startHoMM1Campaign( Campaign::IRONFIST_CAMPAIGN );
        }
        if ( le.MouseClickLeft( buttonSlayer.area() ) ) {
            return startHoMM1Campaign( Campaign::SLAYER_CAMPAIGN );
        }
        if ( le.MouseClickLeft( buttonLamanda.area() ) ) {
            return startHoMM1Campaign( Campaign::LAMANDA_CAMPAIGN );
        }
        if ( le.MouseClickLeft( buttonAlamar.area() ) ) {
            return startHoMM1Campaign( Campaign::ALAMAR_CAMPAIGN );
        }
        if ( HotKeyPressEvent( HotKeyEvent::DEFAULT_CANCEL ) || le.MouseClickLeft( buttonCancel.area() ) ) {
            return fheroes2::GameMode::NEW_GAME;
        }
    }

    return fheroes2::GameMode::QUIT_GAME;
}

fheroes2::GameMode Game::NewGame( const bool /*isProbablyDemoVersion*/ )
{
    outputNewMenuInTextSupportMode();

    // Stop all sounds, but not the music
    AudioManager::stopSounds();

    AudioManager::PlayMusicAsync( MUS::MAINMENU, Music::PlaybackMode::RESUME_AND_PLAY_INFINITE );

    // Reset last save name
    Game::SetLastSaveName( "" );

    // Setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::drawMainMenuScreen();

    // HoMM1 New Game submenu: same panel layout as main menu, buttons from BTNNEWGM.ICN.
    // Frames: 0/1=Standard Game, 2/3=Campaign Game, 4/5=Multi-Player Game, 6/7=Cancel.
    constexpr int32_t btnX = 459;
    fheroes2::Button buttonStandard   ( btnX, 80,  ICN::H1BTNNEWGM, 0, 1 );
    fheroes2::Button buttonCampaign   ( btnX, 146, ICN::H1BTNNEWGM, 2, 3 );
    fheroes2::Button buttonMultiPlayer( btnX, 212, ICN::H1BTNNEWGM, 4, 5 );
    fheroes2::Button buttonCancel     ( btnX, 344, ICN::H1BTNNEWGM, 6, 7 );

    buttonStandard.draw();
    buttonCampaign.draw();
    buttonMultiPlayer.draw();
    buttonCancel.draw();

    fheroes2::validateFadeInAndRender();

    LocalEvent & le = LocalEvent::Get();

    while ( le.HandleEvents() ) {
        buttonStandard.drawOnState( le.isMouseLeftButtonPressedInArea( buttonStandard.area() ) );
        buttonCampaign.drawOnState( le.isMouseLeftButtonPressedInArea( buttonCampaign.area() ) );
        buttonMultiPlayer.drawOnState( le.isMouseLeftButtonPressedInArea( buttonMultiPlayer.area() ) );
        buttonCancel.drawOnState( le.isMouseLeftButtonPressedInArea( buttonCancel.area() ) );

        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_STANDARD ) || le.MouseClickLeft( buttonStandard.area() ) ) {
            return fheroes2::GameMode::NEW_STANDARD;
        }
        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_CAMPAIGN ) || le.MouseClickLeft( buttonCampaign.area() ) ) {
            return fheroes2::GameMode::NEW_HOMM1_CAMPAIGN;
        }
        if ( HotKeyPressEvent( HotKeyEvent::MAIN_MENU_MULTI ) || le.MouseClickLeft( buttonMultiPlayer.area() ) ) {
            return NewHotSeat( 0 );
        }
        if ( HotKeyPressEvent( HotKeyEvent::DEFAULT_CANCEL ) || le.MouseClickLeft( buttonCancel.area() ) ) {
            return fheroes2::GameMode::MAIN_MENU;
        }
    }

    return fheroes2::GameMode::QUIT_GAME;
}
