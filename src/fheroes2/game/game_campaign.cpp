/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2020 - 2025                                             *
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

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

#include "audio.h"
#include "audio_manager.h"
#include "campaign_data.h"
#include "campaign_savedata.h"
#include "campaign_scenariodata.h"
#include "cursor.h"
#include "dialog.h"
#include "game.h"
#include "game_io.h"
#include "game_mode.h"
#include "highscores.h"
#include "maps_fileinfo.h"
#include "monster.h"
#include "mus.h"
#include "players.h"
#include "serialize.h"
#include "settings.h"
#include "tools.h"
#include "translations.h"
#include "ui_dialog.h"
#include "ui_tool.h"
#include "world.h"

namespace
{
    std::string getCampaignDifficultyText( const int32_t difficulty )
    {
        switch ( difficulty ) {
        case Campaign::CampaignDifficulty::Easy:
            return { _( "Easy" ) };
        case Campaign::CampaignDifficulty::Normal:
            // Original campaign difficulty.
            return { _( "Normal" ) };
        case Campaign::CampaignDifficulty::Hard:
            return { _( "Hard" ) };
        default:
            // Did you add a new campaign difficulty? Add the logic above!
            assert( 0 );
            return {};
        }
    }
}

bool Game::isSuccessionWarsCampaignPresent()
{
    return Campaign::CampaignData::getCampaignData( Campaign::IRONFIST_CAMPAIGN ).isAllCampaignMapsPresent()
           && Campaign::CampaignData::getCampaignData( Campaign::SLAYER_CAMPAIGN ).isAllCampaignMapsPresent();
}

bool Game::isPriceOfLoyaltyCampaignPresent()
{
    return false; // PoL campaigns removed in HoMM1 conversion
}

fheroes2::GameMode Game::CompleteCampaignScenario( const bool isLoadingSaveFile )
{
    Campaign::CampaignSaveData & saveData = Campaign::CampaignSaveData::Get();
    const Campaign::CampaignData & campaignData = Campaign::CampaignData::getCampaignData( saveData.getCampaignID() );

    if ( !isLoadingSaveFile ) {
        saveData.addCurrentMapToFinished();
        saveData.addDaysPassed( world.CountDay() );

        if ( !campaignData.isLastScenario( saveData.getLastCompletedScenarioInfoID() ) ) {
            Game::SaveCompletedCampaignScenario();
        }
    }

    const Campaign::ScenarioInfoId & lastCompletedScenarioInfo = saveData.getLastCompletedScenarioInfoID();

    if ( campaignData.isLastScenario( lastCompletedScenarioInfo ) ) {
        // Get data for ratings text.
        const Campaign::CampaignSaveData & campaignSaveData = Campaign::CampaignSaveData::Get();
        const int32_t daysPassed = static_cast<int32_t>( campaignSaveData.getDaysPassed() );
        // Rating is calculated based on difficulty of campaign.
        const int32_t score = daysPassed * static_cast<int32_t>( campaignSaveData.getCampaignDifficultyPercent() ) / 100;

        std::string results = _( "Days: %{days}\nDifficulty: %{difficulty}\nRating: %{rating}" );
        StringReplace( results, "%{days}", daysPassed );
        StringReplace( results, "%{difficulty}", getCampaignDifficultyText( campaignSaveData.getMinDifficulty() ) );
        StringReplace( results, "%{rating}", fheroes2::HighScoreDataContainer::getMonsterByDay( score ).GetName() );
        fheroes2::showStandardTextMessage( _( "Congratulations!" ), results, Dialog::OK );

        // fheroes2::PlayMusic is run here in order to start playing before displaying the high score.
        AudioManager::PlayMusicAsync( MUS::VICTORY, Music::PlaybackMode::REWIND_AND_PLAY_INFINITE );
        return fheroes2::GameMode::HIGHSCORES_CAMPAIGN;
    }

    const Campaign::ScenarioInfoId firstNextMap = Campaign::CampaignData::getScenariosAfter( lastCompletedScenarioInfo ).front();
    saveData.setCurrentScenarioInfo( firstNextMap );
    return fheroes2::GameMode::SELECT_CAMPAIGN_SCENARIO;
}

fheroes2::GameMode Game::SelectCampaignScenario( const fheroes2::GameMode prevMode, const bool allowToRestart )
{
    Campaign::CampaignSaveData & save = Campaign::CampaignSaveData::Get();
    const Campaign::ScenarioInfoId & current = save.getCurrentScenarioInfoId();
    const auto & campaign = Campaign::CampaignData::getCampaignData( current.campaignId );
    const auto & scenario = campaign.getAllScenarios().at( current.scenarioId );

    const CursorRestorer cursorRestorer( true, Cursor::POINTER );
    if ( allowToRestart ) {
        const std::string objective = std::string( scenario.getDescription() ) + "\n\n" + _( "Restart this scenario?" );
        if ( fheroes2::showStandardTextMessage( scenario.getScenarioName(), objective, Dialog::YES | Dialog::NO ) != Dialog::YES ) {
            return prevMode;
        }
    }
    else if ( fheroes2::showStandardTextMessage( scenario.getScenarioName(), scenario.getDescription(), Dialog::OK | Dialog::CANCEL ) != Dialog::OK ) {
        return prevMode;
    }

    Maps::FileInfo mapInfo = scenario.loadMap();
    StreamFile file;
    if ( mapInfo.filename.empty() || !file.open( mapInfo.filename, "rb" ) ) {
        fheroes2::showStandardTextMessage( _( "Campaign Scenario loading failure" ), _( "Please make sure that campaign files are correct and present." ),
                                          Dialog::OK );
        return prevMode;
    }

    Campaign::CampaignData::updateScenarioGameplayConditions( current, mapInfo );
    Settings & conf = Settings::Get();
    conf.SetGameType( Game::TYPE_CAMPAIGN );
    conf.setCurrentMapInfo( std::move( mapInfo ) );
    conf.GetPlayers().SetStartGame();
    if ( !world.loadHoMM1Map( file.getRaw( file.size() ) ) ) {
        fheroes2::showStandardTextMessage( _( "Campaign Scenario loading failure" ), _( "Please make sure that campaign files are correct and present." ),
                                          Dialog::OK );
        return fheroes2::GameMode::MAIN_MENU;
    }

    fheroes2::fadeOutDisplay();
    Game::setDisplayFadeIn();
    return fheroes2::GameMode::START_GAME;
}
