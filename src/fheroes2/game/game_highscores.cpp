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

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "agg_image.h"
#include "campaign_savedata.h"
#include "campaign_scenariodata.h"
#include "cursor.h"
#include "dialog.h"
#include "game.h" // IWYU pragma: associated
#include "game_delays.h"
#include "game_hotkeys.h"
#include "game_io.h"
#include "game_mode.h"
#include "game_over.h"
#include "highscores.h"
#include "icn.h"
#include "image.h"
#include "localevent.h"
#include "maps_fileinfo.h"
#include "math_base.h"
#include "monster.h"
#include "monster_anim.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_language.h"
#include "ui_text.h"
#include "ui_tool.h"
#include "ui_window.h"
#include "world.h"

#ifdef WITH_DEBUG
#include "logging.h"
#endif

namespace fheroes2
{
    enum class SupportedLanguage : uint8_t;
}

namespace
{
    const std::string highScoreFileName = "fheroes2.hgs";
    fheroes2::HighScoreDataContainer highScoreDataContainer;
    const int32_t initialHighScoreEntryOffsetY = 72;
    const int32_t highScoreEntryStepY = 40;

    // Returns true when both HoMM1 high-score files exist on disk.
    bool hoMM1ScoreFilesExist( std::string & outStandard, std::string & outCampaign )
    {
        return Settings::findFile( "data", "standard.hs", outStandard )
               && Settings::findFile( "data", "campaign.hs", outCampaign );
    }

    // HoMM1 high scores layout (positions within the 640×480 HISCORE.BMP screen).
    // Row numbers 1-10 are already drawn as gold lettering in the BMP — do NOT draw them in font.
    // The "STANDARD" / "CAMPAIGN" labels are provided by HISCORE.ICN — do NOT draw them in font.
    constexpr int32_t h1EntryStartY = 68;  // y of the first entry row (aligned to gold lettering in BMP)
    constexpr int32_t h1EntryStepY  = 40;  // vertical spacing between rows
    constexpr int32_t h1ColPlayer   = 88;  // player name column
    constexpr int32_t h1ColLand     = 248; // scenario/land column
    constexpr int32_t h1ColScore    = 388; // score/rating column
    constexpr int32_t h1ColTitle    = 450; // creature title column

    // HISCORE.ICN button layout (28×134 vertical side buttons):
    //   frames 0/1  — Standard  tab (released / pressed)
    //   frames 2/3  — Campaign  tab (released / pressed)
    //   frames 4/5  — Exit      button (released / pressed)
    constexpr int32_t h1BtnStandardX = 2;    // left edge, Standard tab
    constexpr int32_t h1BtnStandardY = 172;  // top of Standard tab
    constexpr int32_t h1BtnCampaignX = 2;    // left edge, Campaign tab (below Standard)
    constexpr int32_t h1BtnCampaignY = 310;  // top of Campaign tab
    constexpr int32_t h1BtnExitX     = 610;  // right edge, Exit button
    constexpr int32_t h1BtnExitY     = 172;  // top of Exit button

    void redrawHoMM1HighScoreScreen( const int32_t selectedScoreIndex, const std::vector<fheroes2::HighscoreData> & highScores, const bool isCampaign )
    {
        fheroes2::Display & display = fheroes2::Display::instance();

        // Draw the full-screen HoMM1 high scores background (includes gold row numbers and column headers).
        const fheroes2::Sprite & bg = fheroes2::AGG::GetICN( ICN::H1HISCORE_BMP, 0 );
        if ( !bg.empty() ) {
            fheroes2::Copy( bg, 0, 0, display, 0, 0, bg.width(), bg.height() );
        }
        else {
            display.fill( 0 );
        }

        // Draw score entries — white text aligned to the gold row numbers in the BMP.
        // Row numbers and section titles are NOT drawn here (they are in the BMP/ICN art).
        fheroes2::Text text( "", fheroes2::FontType::normalWhite() );

        int32_t scoreIndex = 0;
        for ( const fheroes2::HighscoreData & data : highScores ) {
            const fheroes2::FontType font
                = ( scoreIndex == selectedScoreIndex ) ? fheroes2::FontType::normalYellow() : fheroes2::FontType::normalWhite();
            const int32_t rowY = h1EntryStartY + scoreIndex * h1EntryStepY;

            // Player name.
            text.set( data.playerName, font );
            text.fitToOneRow( h1ColLand - h1ColPlayer - 4 );
            text.draw( h1ColPlayer, rowY, display );

            // Scenario/land name.
            text.set( data.scenarioName, font );
            text.fitToOneRow( h1ColScore - h1ColLand - 4 );
            text.draw( h1ColLand, rowY, display );

            // Score (rating for standard, days for campaign).
            const uint32_t scoreValue = isCampaign ? data.dayCount : data.rating;
            text.set( std::to_string( scoreValue ), font );
            text.draw( h1ColScore, rowY, display );

            // Creature title name.
            const Monster monster = isCampaign ? fheroes2::HighScoreDataContainer::getMonsterByDay( data.rating )
                                               : fheroes2::HighScoreDataContainer::getMonsterByRating( data.rating );
            text.set( monster.GetName(), font );
            text.fitToOneRow( 640 - h1ColTitle - 4 );
            text.draw( h1ColTitle, rowY, display );

            ++scoreIndex;
        }
    }

    fheroes2::GameMode openHighScores( const bool isCampaign, const bool isInternalUpdate, const fheroes2::StandardWindow & /*window*/ )
    {
        GameOver::Result & gameResult = GameOver::Result::Get();

#ifdef WITH_DEBUG
        if ( IS_DEVEL() && ( gameResult.GetResult() & GameOver::WINS ) ) {
            std::string msg = "Developer mode is active, the result will not be saved! \n\n Your result: ";
            if ( isCampaign ) {
                msg += std::to_string( Campaign::CampaignSaveData::Get().getDaysPassed() );
            }
            else {
                msg += std::to_string( Game::GetRating() * Game::getGameOverScoreFactor() / 100 );
            }

            fheroes2::showStandardTextMessage( _( "High Scores" ), std::move( msg ), Dialog::OK );

            gameResult.ResetResult();

            return fheroes2::GameMode::MAIN_MENU;
        }
#endif

        // Try to load High Scores only once. If a player switches between modes we shouldn't reload the game file again and again.
        if ( !isInternalUpdate ) {
            std::string standardPath, campaignPath;
            if ( hoMM1ScoreFilesExist( standardPath, campaignPath ) ) {
                highScoreDataContainer.loadHoMM1( standardPath, campaignPath );
            }
            else {
                const std::string highScoreDataPath = System::concatPath( Game::GetSaveDir(), highScoreFileName );
                if ( !highScoreDataContainer.load( highScoreDataPath ) ) {
                    // Unable to load the file. Let's populate with the default values.
                    highScoreDataContainer.clear();
                    highScoreDataContainer.populateStandardDefaultHighScores();
                    highScoreDataContainer.populateCampaignDefaultHighScores();
                }
            }
        }

        int32_t selectedEntryIndex = -1;
        const bool isAfterGameCompletion = ( ( gameResult.GetResult() & GameOver::WINS ) != 0 );

        fheroes2::Display & display = fheroes2::Display::instance();
        const bool isDefaultScreenSize = display.isDefaultSize();

        const auto & conf = Settings::Get();

        if ( isAfterGameCompletion ) {
            assert( !isInternalUpdate );

            const auto inputPlayerName = []( std::string & playerName ) {
                Dialog::inputString( fheroes2::Text{}, fheroes2::Text{ _( "Your Name" ), fheroes2::FontType::normalWhite() }, playerName, 15, false, {} );
                if ( playerName.empty() ) {
                    playerName = _( "Unknown Hero" );
                }
            };

            const uint32_t completionTime = fheroes2::HighscoreData::generateCompletionTime();
            std::string lang = fheroes2::getLanguageAbbreviation( fheroes2::getCurrentLanguage() );

            // Check whether the game result is good enough to be put on high score board. If not then just skip showing the player name dialog.
            if ( isCampaign ) {
                const Campaign::CampaignSaveData & campaignSaveData = Campaign::CampaignSaveData::Get();
                const uint32_t daysPassed = campaignSaveData.getDaysPassed();
                // Rating is calculated based on difficulty of campaign.
                const uint32_t rating = daysPassed * campaignSaveData.getCampaignDifficultyPercent() / 100;

                const auto & campaignHighscoreData = highScoreDataContainer.getHighScoresCampaign();
                assert( !campaignHighscoreData.empty() );

                if ( campaignHighscoreData.back().rating < rating ) {
                    gameResult.ResetResult();
                    return fheroes2::GameMode::MAIN_MENU;
                }

                std::string playerName;
                inputPlayerName( playerName );

                selectedEntryIndex
                    = highScoreDataContainer.registerScoreCampaign( { std::move( lang ), playerName, Campaign::getCampaignName( campaignSaveData.getCampaignID() ),
                                                                      completionTime, daysPassed, rating, world.GetMapSeed() } );
            }
            else {
                const uint32_t rating = Game::GetRating() * Game::getGameOverScoreFactor() / 100;

                const auto & standardHighscoreData = highScoreDataContainer.getHighScoresStandard();
                assert( !standardHighscoreData.empty() );

                if ( standardHighscoreData.back().rating > rating ) {
                    gameResult.ResetResult();
                    return fheroes2::GameMode::MAIN_MENU;
                }

                const uint32_t daysPassed = world.CountDay();
                std::string playerName;
                inputPlayerName( playerName );

                selectedEntryIndex = highScoreDataContainer.registerScoreStandard(
                    { std::move( lang ), playerName, conf.getCurrentMapInfo().name, completionTime, daysPassed, rating, world.GetMapSeed() } );
            }

            std::string standardPath, campaignPath;
            if ( hoMM1ScoreFilesExist( standardPath, campaignPath ) ) {
                highScoreDataContainer.saveHoMM1( standardPath, campaignPath );
            }
            else {
                const std::string highScoreDataPath = System::concatPath( Game::GetSaveDir(), highScoreFileName );
                highScoreDataContainer.save( highScoreDataPath );
            }

            gameResult.ResetResult();

            // Fade-out game screen.
            fheroes2::fadeOutDisplay();
        }
        else if ( isDefaultScreenSize && !isInternalUpdate ) {
            fheroes2::fadeOutDisplay();
        }

        const CursorRestorer cursorRestorer( true, Cursor::POINTER );

        const std::vector<fheroes2::HighscoreData> & scores
            = isCampaign ? highScoreDataContainer.getHighScoresCampaign() : highScoreDataContainer.getHighScoresStandard();

        // HISCORE.ICN buttons: Standard (0/1), Campaign (2/3), Exit (4/5).
        fheroes2::Button btnStandard( h1BtnStandardX, h1BtnStandardY, ICN::H1HISCORE_ICN, 0, 1 );
        fheroes2::Button btnCampaign( h1BtnCampaignX, h1BtnCampaignY, ICN::H1HISCORE_ICN, 2, 3 );
        fheroes2::Button btnExit    ( h1BtnExitX,     h1BtnExitY,     ICN::H1HISCORE_ICN, 4, 5 );

        redrawHoMM1HighScoreScreen( selectedEntryIndex, scores, isCampaign );
        btnStandard.draw();
        btnCampaign.draw();
        btnExit.draw();

        if ( Game::validateDisplayFadeIn() ) {
            fheroes2::fadeInDisplay();
        }
        else {
            display.render();
        }

        LocalEvent & le = LocalEvent::Get();
        while ( le.HandleEvents() ) {
            btnStandard.drawOnState( le.isMouseLeftButtonPressedInArea( btnStandard.area() ) );
            btnCampaign.drawOnState( le.isMouseLeftButtonPressedInArea( btnCampaign.area() ) );
            btnExit.drawOnState( le.isMouseLeftButtonPressedInArea( btnExit.area() ) );

            if ( le.MouseClickLeft( btnExit.area() ) || Game::HotKeyCloseWindow() ) {
                fheroes2::fadeOutDisplay();
                Game::setDisplayFadeIn();
                return fheroes2::GameMode::MAIN_MENU;
            }

            if ( le.MouseClickLeft( btnStandard.area() ) ) {
                return fheroes2::GameMode::HIGHSCORES_STANDARD;
            }

            if ( le.MouseClickLeft( btnCampaign.area() ) ) {
                return fheroes2::GameMode::HIGHSCORES_CAMPAIGN;
            }
        }

        return fheroes2::GameMode::QUIT_GAME;
    }
}

fheroes2::GameMode Game::DisplayHighScores( bool isCampaign )
{
    fheroes2::GameMode returnValue{ fheroes2::GameMode::HIGHSCORES_STANDARD };
    bool isInternalUpdate{ false };

    const fheroes2::StandardWindow window( fheroes2::Display::DEFAULT_WIDTH, fheroes2::Display::DEFAULT_HEIGHT, false );

    while ( returnValue == fheroes2::GameMode::HIGHSCORES_STANDARD || returnValue == fheroes2::GameMode::HIGHSCORES_CAMPAIGN ) {
        returnValue = openHighScores( isCampaign, isInternalUpdate, window );
        isInternalUpdate = true;
        isCampaign = ( returnValue == fheroes2::GameMode::HIGHSCORES_CAMPAIGN );
    }

    return returnValue;
}
