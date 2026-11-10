/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2021 - 2025                                             *
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

#include "campaign_data.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

#include "artifact.h"
#include "color.h"
#include "game_video_type.h"
#include "heroes.h"
#include "maps_fileinfo.h"
#include "monster.h"
#include "race.h"
#include "resource.h"
#include "spell.h"
#include "system.h"
#include "tools.h"
#include "translations.h"

namespace
{
    const Campaign::VideoSequence emptyPlayback;

    constexpr std::array<PlayerColor, 4> lordColors = { PlayerColor::RED, PlayerColor::GREEN, PlayerColor::YELLOW, PlayerColor::BLUE };

    struct HoMM1Chapter
    {
        const char * name;
        const char * objective;
        fheroes2::Point target;
    };

    constexpr std::array<HoMM1Chapter, 9> chapters = { {
        { gettext_noop( "Gateway" ), gettext_noop( "Capture the town of Gateway in the center of the island before another lord takes it." ), { 36, 35 } },
        { gettext_noop( "The Archipelago" ), gettext_noop( "Sail between the islands and defeat the three rival lords." ), {} },
        { gettext_noop( "The Wounded Land" ), gettext_noop( "Visit the obelisks to piece together the puzzle and dig up the Eye of Goros before your rivals." ), {} },
        { gettext_noop( "Free-for-All" ), gettext_noop( "Defeat all rival lords to claim the central continent." ), {} },
        { gettext_noop( "Castle Ironfist" ), gettext_noop( "Cross the Floodwater River and take Lord Ironfist's castle in the northwest." ), { 13, 10 } },
        { gettext_noop( "Castle Slayer" ), gettext_noop( "Cross the desert and take Lord Slayer's castle northeast of the mountain pass." ), { 62, 20 } },
        { gettext_noop( "Castle Lamanda" ), gettext_noop( "Find the teleport gate and a sea route to the forest, then take Queen Lamanda's castle in the northwest." ), { 8, 8 } },
        { gettext_noop( "Castle Alamar" ), gettext_noop( "Find your way through the Minotaur Maze and take Lord Alamar's castle in the southeast." ), { 66, 69 } },
        { gettext_noop( "King-of-the-Hill" ), gettext_noop( "Defeat the guardians of the Dragon Citadel on the central island before the other lords." ), { 41, 37 } }
    } };

    // HoMM1: each campaign plays 8 of the 9 CMP maps, each skipping a different one.
    // Ironfist: 1 2 3 4 6 7 8 9  (skips CAMP5)
    // Slayer:   1 2 3 4 5 7 8 9  (skips CAMP6)
    // Lamanda:  1 2 3 4 5 6 8 9  (skips CAMP7)
    // Alamar:   1 2 3 4 5 6 7 9  (skips CAMP8)
    Campaign::CampaignData buildHoMM1Campaign( const int campaignID, const std::array<int, 8> & mapNumbers )
    {
        constexpr int scenarioCount = 8;
        std::vector<Campaign::ScenarioData> scenarioDatas;
        scenarioDatas.reserve( scenarioCount );
        for ( int i = 0; i < scenarioCount; ++i ) {
            std::vector<Campaign::ScenarioInfoId> nextScenarios;
            if ( i + 1 < scenarioCount ) {
                nextScenarios.emplace_back( campaignID, i + 1 );
            }
            const int number = mapNumbers[i];
            const HoMM1Chapter & chapter = chapters[number - 1];
            const std::string mapFile = "CAMP" + std::to_string( number ) + ".CMP";
            const auto victory = number == 9 ? Campaign::ScenarioVictoryCondition::CAPTURE_DRAGON_CITY : Campaign::ScenarioVictoryCondition::STANDARD;
            scenarioDatas.emplace_back( Campaign::ScenarioInfoId{ campaignID, i }, std::move( nextScenarios ), mapFile, chapter.name, chapter.objective,
                                        emptyPlayback, emptyPlayback, victory );
        }

        Campaign::CampaignData campaignData;
        campaignData.setCampaignID( campaignID );
        campaignData.setCampaignScenarios( std::move( scenarioDatas ) );
        return campaignData;
    }

    Campaign::CampaignData getRolandCampaignData()
    {
        return buildHoMM1Campaign( Campaign::IRONFIST_CAMPAIGN, { 1, 2, 3, 4, 6, 7, 8, 9 } );
    }

    Campaign::CampaignData getArchibaldCampaignData()
    {
        return buildHoMM1Campaign( Campaign::SLAYER_CAMPAIGN, { 1, 2, 3, 4, 5, 7, 8, 9 } );
    }

    Campaign::CampaignData getLamandaCampaignData()
    {
        return buildHoMM1Campaign( Campaign::LAMANDA_CAMPAIGN, { 1, 2, 3, 4, 5, 6, 8, 9 } );
    }

    Campaign::CampaignData getAlamarCampaignData()
    {
        return buildHoMM1Campaign( Campaign::ALAMAR_CAMPAIGN, { 1, 2, 3, 4, 5, 6, 7, 9 } );
    }

}

namespace Campaign
{
    // this is used to get awards that are not directly obtainable via scenario clear, such as assembling artifacts
    std::vector<Campaign::CampaignAwardData> CampaignAwardData::getExtraCampaignAwardData( const int campaignID )
    {
        assert( campaignID >= 0 );

        switch ( campaignID ) {
        case IRONFIST_CAMPAIGN:
        case SLAYER_CAMPAIGN:
        case LAMANDA_CAMPAIGN:
        case ALAMAR_CAMPAIGN:
            break;
        default:
            // Did you add a new campaign? Add the case handling code for it!
            assert( 0 );
            break;
        }

        return {};
    }

    const std::vector<ScenarioInfoId> & CampaignData::getScenariosAfter( const ScenarioInfoId & scenarioInfo )
    {
        const CampaignData & campaignData = getCampaignData( scenarioInfo.campaignId );
        const std::vector<ScenarioData> & scenarios = campaignData.getAllScenarios();
        assert( scenarioInfo.scenarioId >= 0 && static_cast<size_t>( scenarioInfo.scenarioId ) < scenarios.size() );

        return scenarios[scenarioInfo.scenarioId].getNextScenarios();
    }

    std::vector<ScenarioInfoId> CampaignData::getStartingScenarios() const
    {
        std::vector<ScenarioInfoId> startingScenarios;

        for ( size_t i = 0; i < _scenarios.size(); ++i ) {
            const int scenarioID = _scenarios[i].getScenarioID();
            if ( isStartingScenario( { _campaignID, scenarioID } ) )
                startingScenarios.emplace_back( _campaignID, scenarioID );
        }

        return startingScenarios;
    }

    bool CampaignData::isStartingScenario( const ScenarioInfoId & scenarioInfo ) const
    {
        // starting scenario = a scenario that is never included as a nextMap
        for ( size_t i = 0; i < _scenarios.size(); ++i ) {
            const std::vector<ScenarioInfoId> & nextMaps = _scenarios[i].getNextScenarios();

            if ( std::find( nextMaps.begin(), nextMaps.end(), scenarioInfo ) != nextMaps.end() )
                return false;
        }

        return true;
    }

    bool CampaignData::isAllCampaignMapsPresent() const
    {
        for ( size_t i = 0; i < _scenarios.size(); ++i ) {
            if ( !_scenarios[i].isMapFilePresent() )
                return false;
        }

        return true;
    }

    bool CampaignData::isLastScenario( const Campaign::ScenarioInfoId & scenarioInfoId ) const
    {
        assert( !_scenarios.empty() );
        for ( const ScenarioData & scenario : _scenarios ) {
            if ( ( scenario.getScenarioInfoId() == scenarioInfoId ) && scenario.getNextScenarios().empty() ) {
                return true;
            }
        }

        return false;
    }

    void CampaignData::setCampaignScenarios( std::vector<ScenarioData> && scenarios )
    {
        _scenarios = std::move( scenarios );
    }

    const CampaignData & CampaignData::getCampaignData( const int campaignID )
    {
        switch ( campaignID ) {
        case IRONFIST_CAMPAIGN: {
            static const Campaign::CampaignData campaign( getRolandCampaignData() );
            return campaign;
        }
        case SLAYER_CAMPAIGN: {
            static const Campaign::CampaignData campaign( getArchibaldCampaignData() );
            return campaign;
        }
        case LAMANDA_CAMPAIGN: {
            static const Campaign::CampaignData campaign( getLamandaCampaignData() );
            return campaign;
        }
        case ALAMAR_CAMPAIGN: {
            static const Campaign::CampaignData campaign( getAlamarCampaignData() );
            return campaign;
        }
        default: {
            assert( 0 );
            static const Campaign::CampaignData noCampaign;
            return noCampaign;
        }
        }
    }

    void CampaignData::updateScenarioGameplayConditions( const Campaign::ScenarioInfoId & scenarioInfoId, Maps::FileInfo & mapInfo )
    {
        const std::string fileName = StringLower( System::GetFileName( mapInfo.filename ) );
        if ( fileName.size() != 9 || fileName.compare( 0, 4, "camp" ) != 0 || fileName.compare( 5, 4, ".cmp" ) != 0 || fileName[4] < '1'
             || fileName[4] > '9' ) {
            return;
        }

        const int number = fileName[4] - '0';
        const HoMM1Chapter & chapter = chapters[number - 1];
        mapInfo.name = _( chapter.name );
        mapInfo.description = _( chapter.objective );

        const PlayerColor human = lordColors[scenarioInfoId.campaignId];
        mapInfo.races = { Race::WRLK, Race::BARB, Race::KNGT, Race::SORC };
        mapInfo.colorsAvailableForHumans = static_cast<PlayerColorsSet>( human );
        if ( number >= 5 && number <= 8 ) {
            mapInfo.kingdomColors = human | lordColors[number - 5];
        }
        mapInfo.colorsAvailableForComp = mapInfo.kingdomColors & ~mapInfo.colorsAvailableForHumans;
        if ( number == 9 ) {
            for ( const PlayerColor color : PlayerColorsVector( mapInfo.colorsAvailableForComp ) ) {
                mapInfo.unions[Color::GetIndex( color )] = mapInfo.colorsAvailableForComp;
            }
        }

        mapInfo.victoryConditionParams = { static_cast<uint16_t>( chapter.target.x ), static_cast<uint16_t>( chapter.target.y ) };
        if ( number == 1 || ( number >= 5 && number <= 8 ) ) {
            mapInfo.victoryConditionType = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
            mapInfo.allowNormalVictory = false;
            mapInfo.compAlsoWins = number == 1;
        }
        else if ( number == 3 ) {
            mapInfo.victoryConditionType = Maps::FileInfo::VICTORY_OBTAIN_ARTIFACT;
            mapInfo.allowNormalVictory = false;
        }
    }

    // default amount to 1 for initialized campaign award data
    CampaignAwardData::CampaignAwardData( const int32_t id, const int32_t type, const int32_t subType )
        : CampaignAwardData( id, type, subType, 1, 0 )
    {
        // Do nothing.
    }

    CampaignAwardData::CampaignAwardData( const int32_t id, const int32_t type, const int32_t subType, const int32_t amount )
        : CampaignAwardData( id, type, subType, amount, 0 )
    {
        // Do nothing.
    }

    CampaignAwardData::CampaignAwardData( const int32_t id, const int32_t type, const int32_t subType, std::string customName )
        : CampaignAwardData( id, type, subType, 1, 0, std::move( customName ) )
    {
        // Do nothing.
    }

    CampaignAwardData::CampaignAwardData( const int32_t id, const int32_t type, const int32_t subType, const int32_t amount, const int32_t startScenarioID,
                                          std::string customName )
        : _id( id )
        , _type( type )
        , _subType( subType )
        , _amount( amount )
        , _startScenarioID( startScenarioID )
        , _customName( std::move( customName ) )
    {
        // Do nothing.
    }

    std::string CampaignAwardData::getName() const
    {
        if ( !_customName.empty() )
            return _( _customName );

        switch ( _type ) {
        case CampaignAwardData::TYPE_CREATURE_CURSE:
            return Monster( _subType ).GetName() + std::string( _( " bane" ) );
        case CampaignAwardData::TYPE_CREATURE_ALLIANCE:
            return Monster( _subType ).GetName() + std::string( _( " alliance" ) );
        case CampaignAwardData::TYPE_GET_ARTIFACT:
            return Artifact( _subType ).GetName();
        case CampaignAwardData::TYPE_CARRY_OVER_FORCES:
            return _( "Carry-over forces" );
        case CampaignAwardData::TYPE_RESOURCE_BONUS:
            return Resource::String( _subType ) + std::string( _( " bonus" ) );
        case CampaignAwardData::TYPE_GET_SPELL:
            return Spell( _subType ).GetName();
        case CampaignAwardData::TYPE_HIREABLE_HERO:
            return Heroes( _subType, 0 ).GetName();
        case CampaignAwardData::TYPE_DEFEAT_ENEMY_HERO:
            return Heroes( _subType, 0 ).GetName() + std::string( _( " defeated" ) );
        default:
            // Did you add a new award? Add the logic above!
            assert( 0 );
            break;
        }

        return {};
    }

    std::string CampaignAwardData::getDescription() const
    {
        switch ( _type ) {
        case CampaignAwardData::TYPE_CREATURE_CURSE: {
            std::vector<Monster> monsters;
            monsters.emplace_back( _subType );
            std::string description( monsters.back().GetMultiName() );

            while ( monsters.back() != monsters.back().GetUpgrade() ) {
                monsters.emplace_back( monsters.back().GetUpgrade() );
                description += ", ";
                description += monsters.back().GetMultiName();
            }

            description += _( " will always run away from your army." );
            return description;
        }
        case CampaignAwardData::TYPE_CREATURE_ALLIANCE: {
            std::vector<Monster> monsters;
            monsters.emplace_back( _subType );
            std::string description( monsters.back().GetMultiName() );

            while ( monsters.back() != monsters.back().GetUpgrade() ) {
                monsters.emplace_back( monsters.back().GetUpgrade() );
                description += ", ";
                description += monsters.back().GetMultiName();
            }

            description += _( " will be willing to join your army." );
            return description;
        }
        case CampaignAwardData::TYPE_GET_ARTIFACT: {
            std::string description( _( "\"%{artifact}\" artifact will be carried over in the campaign." ) );
            StringReplace( description, "%{artifact}", Artifact( _subType ).GetName() );
            return description;
        }
        case CampaignAwardData::TYPE_CARRY_OVER_FORCES: {
            return _( "The army will be carried over in the campaign." );
        }
        case CampaignAwardData::TYPE_RESOURCE_BONUS: {
            std::string description( _( "The kingdom will have +%{count} %{resource} each day." ) );
            StringReplace( description, "%{count}", std::to_string( _amount ) );
            StringReplace( description, "%{resource}", Resource::String( _subType ) );
            return description;
        }
        case CampaignAwardData::TYPE_GET_SPELL: {
            std::string description( _( "The \"%{spell}\" spell will be carried over in the campaign." ) );
            StringReplace( description, "%{spell}", Spell( _subType ).GetName() );
            return description;
        }
        case CampaignAwardData::TYPE_HIREABLE_HERO: {
            std::string description( _( "%{hero} can be hired during scenarios." ) );
            StringReplace( description, "%{hero}", Heroes( _subType, 0 ).GetName() );
            return description;
        }
        case CampaignAwardData::TYPE_DEFEAT_ENEMY_HERO: {
            std::string description( _( "%{hero} has been defeated and will not appear in subsequent scenarios." ) );
            StringReplace( description, "%{hero}", Heroes( _subType, 0 ).GetName() );
            return description;
        }
        default:
            // Did you add a new award? Add the logic above!
            assert( 0 );
            break;
        }

        return {};
    }

    std::vector<Campaign::CampaignAwardData> CampaignAwardData::getCampaignAwardData( const ScenarioInfoId & )
    {
        return {};
    }

    const char * CampaignAwardData::getAllianceJoiningMessage( const int monsterId )
    {
        switch ( monsterId ) {
        case Monster::DWARF:
            return _( "The dwarves recognize their allies and gladly join your forces." );
        case Monster::OGRE:
            return _( "The ogres recognize you as the Dwarfbane and lumber over to join you." );
        case Monster::GREEN_DRAGON:
            return _( "The dragons, snarling and growling, agree to join forces with you, their 'Ally'." );
        case Monster::ELF:
            return _(
                "As you approach the group of elves, their leader calls them all to attention. He shouts to them, \"Who of you is brave enough to join this fearless ally of ours?\" The group explodes with cheers as they run to join your ranks." );
        default:
            break;
        }

        assert( 0 ); // Did you forget to add a new alliance type?
        return nullptr;
    }

    const char * CampaignAwardData::getAllianceFleeingMessage( const int monsterId )
    {
        switch ( monsterId ) {
        case Monster::DWARF:
            return _( "The dwarves hail you, \"Any friend of Roland is a friend of ours. You may pass.\"" );
        case Monster::OGRE:
            return _( "The ogres give you a grunt of recognition, \"Archibald's allies may pass.\"" );
        case Monster::GREEN_DRAGON:
            return _(
                "The dragons see you and call out. \"Our alliance with Archibald compels us to join you. Unfortunately you have no room. A pity!\" They quickly scatter." );
        case Monster::ELF:
            return _(
                "The elves stand at attention as you approach. Their leader calls to you and says, \"Let us not impede your progress, ally! Move on, and may victory be yours.\"" );
        default:
            break;
        }

        assert( 0 ); // Did you forget to add a new alliance type?
        return nullptr;
    }

    const char * CampaignAwardData::getBaneFleeingMessage( const int monsterId )
    {
        switch ( monsterId ) {
        case Monster::DWARF:
            return _( "\"The Dwarfbane!!!!, run for your lives.\"" );
        default:
            break;
        }

        assert( 0 ); // Did you forget to add a new bane type?
        return nullptr;
    }
}
