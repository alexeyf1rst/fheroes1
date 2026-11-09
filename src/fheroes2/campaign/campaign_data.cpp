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
#include "resource.h"
#include "spell.h"
#include "system.h"
#include "tools.h"
#include "translations.h"

namespace
{
    const Campaign::VideoSequence emptyPlayback;

    std::vector<Campaign::CampaignAwardData> getRolandCampaignAwardData( const int scenarioID )
    {
        std::vector<Campaign::CampaignAwardData> obtainableAwards;

        switch ( scenarioID ) {
        case 2:
            obtainableAwards.emplace_back( 0, Campaign::CampaignAwardData::TYPE_CREATURE_ALLIANCE, Monster::DWARF, gettext_noop( "Dwarven Alliance" ) );
            break;
        case 5:
            obtainableAwards.emplace_back( 1, Campaign::CampaignAwardData::TYPE_HIREABLE_HERO, Heroes::ELIZA, 0, 0, gettext_noop( "Sorceress Guild" ) );
            break;
        case 6:
            obtainableAwards.emplace_back( 2, Campaign::CampaignAwardData::TYPE_CARRY_OVER_FORCES, 0, 0, 9 );
            break;
        case 7:
            obtainableAwards.emplace_back( 3, Campaign::CampaignAwardData::TYPE_GET_ARTIFACT, Artifact::ULTIMATE_CROWN, 1, 9 );
            break;
        case 8:
            obtainableAwards.emplace_back( 4, Campaign::CampaignAwardData::TYPE_DEFEAT_ENEMY_HERO, Heroes::CORLAGON, 0, 9 );
            break;
        default:
            break;
        }

        return obtainableAwards;
    }

    std::vector<Campaign::CampaignAwardData> getArchibaldCampaignAwardData( const int scenarioID )
    {
        std::vector<Campaign::CampaignAwardData> obtainableAwards;

        switch ( scenarioID ) {
        case 2:
            obtainableAwards.emplace_back( 1, Campaign::CampaignAwardData::TYPE_HIREABLE_HERO, Heroes::BRAX, 0, 0, gettext_noop( "Necromancer Guild" ) );
            break;
        case 3:
            obtainableAwards.emplace_back( 2, Campaign::CampaignAwardData::TYPE_CREATURE_ALLIANCE, Monster::OGRE, gettext_noop( "Ogre Alliance" ) );
            obtainableAwards.emplace_back( 3, Campaign::CampaignAwardData::TYPE_CREATURE_CURSE, Monster::DWARF, gettext_noop( "Dwarfbane" ) );
            break;
        case 6:
            obtainableAwards.emplace_back( 4, Campaign::CampaignAwardData::TYPE_CREATURE_ALLIANCE, Monster::GREEN_DRAGON, gettext_noop( "Dragon Alliance" ) );
            break;
        case 8:
            obtainableAwards.emplace_back( 5, Campaign::CampaignAwardData::TYPE_GET_ARTIFACT, Artifact::ULTIMATE_CROWN );
            break;
        case 9:
            obtainableAwards.emplace_back( 6, Campaign::CampaignAwardData::TYPE_CARRY_OVER_FORCES, 0 );
            break;
        default:
            break;
        }

        return obtainableAwards;
    }


    // HoMM1: each campaign plays 8 of the 9 CMP maps, each skipping a different one.
    // Ironfist: 1 2 3 4 6 7 8 9  (skips CAMP5)
    // Slayer:   1 2 3 4 5 7 8 9  (skips CAMP6)
    // Lamanda:  1 2 3 4 5 6 8 9  (skips CAMP7)
    // Alamar:   1 2 3 4 5 6 7 9  (skips CAMP8)
    Campaign::CampaignData buildHoMM1Campaign( const int campaignID, const std::array<int, 8> & mapNumbers )
    {
        constexpr int scenarioCount = 8;
        const std::array<std::string, scenarioCount> scenarioName
            = { gettext_noop( "Campaign Scenario 1" ), gettext_noop( "Campaign Scenario 2" ), gettext_noop( "Campaign Scenario 3" ),
                gettext_noop( "Campaign Scenario 4" ), gettext_noop( "Campaign Scenario 5" ), gettext_noop( "Campaign Scenario 6" ),
                gettext_noop( "Campaign Scenario 7" ), gettext_noop( "Campaign Scenario 8" ) };

        std::vector<Campaign::ScenarioData> scenarioDatas;
        scenarioDatas.reserve( scenarioCount );
        for ( int i = 0; i < scenarioCount; ++i ) {
            std::vector<Campaign::ScenarioInfoId> nextScenarios;
            if ( i + 1 < scenarioCount ) {
                nextScenarios.emplace_back( campaignID, i + 1 );
            }
            const std::string mapFile = "CAMP" + std::to_string( mapNumbers[i] ) + ".CMP";
            scenarioDatas.emplace_back( Campaign::ScenarioInfoId{ campaignID, i }, std::move( nextScenarios ), mapFile, scenarioName[i], "",
                                        emptyPlayback, emptyPlayback );
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
        bool allAIPlayersInAlliance = false;

        switch ( scenarioInfoId.campaignId ) {
        case IRONFIST_CAMPAIGN:
        case SLAYER_CAMPAIGN:
        case LAMANDA_CAMPAIGN:
        case ALAMAR_CAMPAIGN: {
            // Set per-CAMP victory conditions. Parse the CAMP number from the map filename.
            const std::string baseName = StringLower( System::GetFileName( mapInfo.filename ) );
            int campNum = 0;
            // Expected basename format: "campN.cmp" (N = 1-9)
            if ( baseName.size() == 9 && baseName.substr( 0, 4 ) == "camp" && baseName.substr( 5 ) == ".cmp" ) {
                const char c = baseName[4];
                if ( c >= '1' && c <= '9' ) {
                    campNum = c - '0';
                }
            }
            switch ( campNum ) {
            case 1:
                // CAMP1: Capture the Central Town at (41, 43)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
                mapInfo.victoryConditionParams[0]  = 41;
                mapInfo.victoryConditionParams[1]  = 43;
                mapInfo.allowNormalVictory         = true;
                break;
            case 3:
                // CAMP3: Find the Eye of Goros — ultimate artifact (params[0] = 0 means ultimate)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_OBTAIN_ARTIFACT;
                mapInfo.victoryConditionParams[0]  = 0;
                mapInfo.victoryConditionParams[1]  = 0;
                mapInfo.allowNormalVictory         = true;
                break;
            case 5:
                // CAMP5: Capture Castle Ironfist at (39, 35)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
                mapInfo.victoryConditionParams[0]  = 39;
                mapInfo.victoryConditionParams[1]  = 35;
                mapInfo.allowNormalVictory         = true;
                break;
            case 6:
                // CAMP6: Capture Castle Slayer at (66, 51)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
                mapInfo.victoryConditionParams[0]  = 66;
                mapInfo.victoryConditionParams[1]  = 51;
                mapInfo.allowNormalVictory         = true;
                break;
            case 7:
                // CAMP7: Capture Castle Lamanda at (61, 70)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
                mapInfo.victoryConditionParams[0]  = 61;
                mapInfo.victoryConditionParams[1]  = 70;
                mapInfo.allowNormalVictory         = true;
                break;
            case 8:
                // CAMP8: Capture Castle Alamar at (14, 38)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
                mapInfo.victoryConditionParams[0]  = 14;
                mapInfo.victoryConditionParams[1]  = 38;
                mapInfo.allowNormalVictory         = true;
                break;
            case 9:
                // CAMP9: Capture the Dragon Citadel at (61, 38)
                mapInfo.victoryConditionType       = Maps::FileInfo::VICTORY_CAPTURE_TOWN;
                mapInfo.victoryConditionParams[0]  = 61;
                mapInfo.victoryConditionParams[1]  = 38;
                mapInfo.allowNormalVictory         = true;
                break;
            default:
                // CAMP2, CAMP4: Conquer the Map — keep the default VICTORY_DEFEAT_EVERYONE.
                break;
            }
            break;
        }
        default:
            assert( 0 );
            return;
        }

        if ( allAIPlayersInAlliance ) {
            const PlayerColorsVector humanColors( mapInfo.colorsAvailableForHumans );
            // Make sure that this is only one human player on the map.
            if ( humanColors.size() != 1 ) {
                // Looks like somebody is modifying the original map.
                assert( 0 );
                return;
            }

            const PlayerColorsSet aiColors = ( mapInfo.kingdomColors & ( ~mapInfo.colorsAvailableForHumans ) );
            if ( aiColors == 0 ) {
                // This is definitely not the map to modify.
                assert( 0 );
                return;
            }

            const PlayerColor humanColor = humanColors.front();

            for ( PlayerColorsSet & allianceColor : mapInfo.unions ) {
                if ( allianceColor != static_cast<PlayerColorsSet>( humanColor ) && ( allianceColor & aiColors ) ) {
                    allianceColor = aiColors;
                }
            }
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

    std::vector<Campaign::CampaignAwardData> CampaignAwardData::getCampaignAwardData( const ScenarioInfoId & scenarioInfo )
    {
        assert( scenarioInfo.campaignId >= 0 && scenarioInfo.scenarioId >= 0 );

        switch ( scenarioInfo.campaignId ) {
        case IRONFIST_CAMPAIGN:
            return getRolandCampaignAwardData( scenarioInfo.scenarioId );
        case SLAYER_CAMPAIGN:
            return getArchibaldCampaignAwardData( scenarioInfo.scenarioId );
        default:
            // Did you add a new campaign? Add the corresponding case above.
            assert( 0 );
            break;
        }

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
