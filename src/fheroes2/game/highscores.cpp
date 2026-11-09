/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2022 - 2024                                             *
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

#include "highscores.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <ctime>
#include <iterator>

#include "campaign_scenariodata.h"
#include "game_language.h"
#include "serialize.h"
#include "translations.h"
#include "ui_language.h"
#include "zzlib.h"

namespace
{
    const uint32_t highscoreFileMagicValueV1 = 0xBADC0DE;
    const uint32_t highscoreFileMagicValueV2 = 0xBADC1DE;

    const size_t highscoreMaximumEntries = 10;

    const std::array<Monster::MonsterType, 33> monstersInRanking = { Monster::PEASANT,       Monster::GOBLIN,
                                                                     Monster::SPRITE,        Monster::CENTAUR,
                                                                     Monster::ROGUE,         Monster::ORC,
                                                                     Monster::ARCHER,        Monster::DWARF,
                                                                     Monster::ELF,           Monster::GARGOYLE,
                                                                     Monster::PIKEMAN,       Monster::NOMAD,
                                                                     Monster::WOLF,          Monster::OGRE,
                                                                     Monster::GRIFFIN,       Monster::SWORDSMAN,
                                                                     Monster::DRUID,         Monster::AIR_ELEMENT,
                                                                     Monster::FIRE_ELEMENT,  Monster::GHOST,
                                                                     Monster::WATER_ELEMENT, Monster::EARTH_ELEMENT,
                                                                     Monster::MINOTAUR,      Monster::CAVALRY,
                                                                     Monster::TROLL,         Monster::MEDUSA,
                                                                     Monster::GENIE,         Monster::UNICORN,
                                                                     Monster::HYDRA,         Monster::PALADIN,
                                                                     Monster::CYCLOPS,       Monster::PHOENIX,
                                                                     Monster::GREEN_DRAGON };

    int32_t saveHighscoreEntry( fheroes2::HighscoreData && data, std::vector<fheroes2::HighscoreData> & entries, const bool isCampaign )
    {
        auto iter = std::find( entries.begin(), entries.end(), data );
        if ( iter != entries.end() ) {
            // This is the same game completion. Just replace the entry.
            *iter = std::move( data );
            return static_cast<int32_t>( iter - entries.begin() );
        }

        entries.emplace_back( data );
        std::sort( entries.begin(), entries.end(), [isCampaign]( const fheroes2::HighscoreData & first, const fheroes2::HighscoreData & second ) {
            if ( isCampaign ) {
                return first.rating < second.rating;
            }
            return first.rating > second.rating;
        } );

        if ( entries.size() > highscoreMaximumEntries ) {
            entries.resize( highscoreMaximumEntries );
        }

        iter = std::find( entries.begin(), entries.end(), data );
        if ( iter != entries.end() ) {
            return static_cast<int32_t>( iter - entries.begin() );
        }

        return -1;
    }
}

namespace fheroes2
{
    uint32_t HighscoreData::generateCompletionTime()
    {
        return static_cast<uint32_t>( std::time( nullptr ) );
    }

    void HighscoreData::loadV1( IStreamBase & stream )
    {
        languageAbbreviation = fheroes2::getLanguageAbbreviation( fheroes2::SupportedLanguage::English );

        stream >> playerName >> scenarioName >> completionTime >> dayCount >> rating >> mapSeed;
    }

    OStreamBase & operator<<( OStreamBase & stream, const HighscoreData & data )
    {
        return stream << data.languageAbbreviation << data.playerName << data.scenarioName << data.completionTime << data.dayCount << data.rating << data.mapSeed;
    }

    IStreamBase & operator>>( IStreamBase & stream, HighscoreData & data )
    {
        return stream >> data.languageAbbreviation >> data.playerName >> data.scenarioName >> data.completionTime >> data.dayCount >> data.rating >> data.mapSeed;
    }

    bool HighScoreDataContainer::load( const std::string & fileName )
    {
        StreamFile fileStream;
        fileStream.setBigendian( true );
        if ( !fileStream.open( fileName, "rb" ) ) {
            return false;
        }

        RWStreamBuf hdata;
        hdata.setBigendian( true );

        if ( !Compression::unzipStream( fileStream, hdata ) ) {
            return false;
        }

        uint32_t magicValue = 0;

        hdata >> magicValue;

        if ( magicValue != highscoreFileMagicValueV1 && magicValue != highscoreFileMagicValueV2 ) {
            // It is not a highscore file.
            return false;
        }

        if ( magicValue == highscoreFileMagicValueV1 ) {
            uint32_t size = hdata.get32();
            _highScoresStandard.resize( size );

            for ( HighscoreData & data : _highScoresStandard ) {
                data.loadV1( hdata );
            }

            size = hdata.get32();
            _highScoresCampaign.resize( size );

            for ( HighscoreData & data : _highScoresCampaign ) {
                data.loadV1( hdata );
            }
        }
        else {
            hdata >> _highScoresStandard >> _highScoresCampaign;
        }

        if ( hdata.fail() ) {
            return false;
        }

        // Since the introduction of campaign difficulty we need to calculate rating of a campaign completion.
        // Before the change rating for campaigns was always 0. We need to set it to the number of days.
        for ( fheroes2::HighscoreData & data : _highScoresCampaign ) {
            if ( data.rating == 0 ) {
                data.rating = data.dayCount;
            }
        }

        if ( _highScoresStandard.size() < highscoreMaximumEntries ) {
            populateStandardDefaultHighScores();
        }
        else if ( _highScoresStandard.size() > highscoreMaximumEntries ) {
            _highScoresStandard.resize( highscoreMaximumEntries );
        }

        if ( _highScoresCampaign.size() < highscoreMaximumEntries ) {
            populateCampaignDefaultHighScores();
        }
        else if ( _highScoresCampaign.size() > highscoreMaximumEntries ) {
            _highScoresCampaign.resize( highscoreMaximumEntries );
        }

        return true;
    }

    bool HighScoreDataContainer::save( const std::string & fileName ) const
    {
        StreamFile fileStream;
        fileStream.setBigendian( true );

        if ( !fileStream.open( fileName, "wb" ) ) {
            return false;
        }

        RWStreamBuf hdata;
        hdata.setBigendian( true );
        hdata << highscoreFileMagicValueV2 << _highScoresStandard << _highScoresCampaign;

        return !hdata.fail() && Compression::zipStreamBuf( hdata, fileStream );
    }

    int32_t HighScoreDataContainer::registerScoreStandard( HighscoreData && data )
    {
        return saveHighscoreEntry( std::move( data ), _highScoresStandard, false );
    }

    int32_t HighScoreDataContainer::registerScoreCampaign( HighscoreData && data )
    {
        return saveHighscoreEntry( std::move( data ), _highScoresCampaign, true );
    }

    Monster HighScoreDataContainer::getMonsterByRating( const size_t rating )
    {
        static const std::vector<std::pair<size_t, Monster::MonsterType>> ratingForMonster = []() {
            std::vector<std::pair<size_t, Monster::MonsterType>> result;

            uint32_t threshold = 0;
            uint32_t step = 0;

            for ( const Monster::MonsterType monster : monstersInRanking ) {
                // 0 to 3
                if ( monster == Monster::PEASANT ) {
                    step = 3;
                }
                // 4 to 131
                else if ( monster == Monster::GOBLIN ) {
                    step = 4;
                }
                // 132 to 227
                else if ( monster == Monster::DRUID ) {
                    step = 3;
                }
                // >= 228
                else if ( monster == Monster::GREEN_DRAGON ) {
                    step = 1;
                }

                threshold += step;

                result.emplace_back( threshold, monster );
            }

            return result;
        }();

        {
            assert( !ratingForMonster.empty() );

            const auto & [threshold, monster] = ratingForMonster.back();
            if ( rating >= threshold ) {
                return { monster };
            }
        }

        for ( const auto & [threshold, monster] : ratingForMonster ) {
            if ( rating <= threshold ) {
                return { monster };
            }
        }

        assert( 0 );

        return { Monster::PEASANT };
    }

    Monster HighScoreDataContainer::getMonsterByDay( const size_t numOfDays )
    {
        static const std::vector<std::pair<size_t, Monster::MonsterType>> numOfDaysForMonster = []() {
            std::vector<std::pair<size_t, Monster::MonsterType>> result;

            uint32_t threshold = 0;
            uint32_t step = 0;

            for ( auto iter = monstersInRanking.crbegin(); iter != monstersInRanking.crend(); ++iter ) {
                const Monster::MonsterType monster = *iter;

                // 0 to 300
                if ( monster == Monster::GREEN_DRAGON ) {
                    step = 300;
                }
                // 301 to 1000
                else if ( monster == Monster::PHOENIX ) {
                    step = 20;
                }
                // 1001 to 2000
                else if ( monster == Monster::DRUID ) {
                    step = 100;
                }
                // 2001 to 5800
                else if ( monster == Monster::DWARF ) {
                    step = 200;
                }
                // >= 5801
                else if ( monster == Monster::PEASANT ) {
                    step = 1;
                }

                threshold += step;

                result.emplace_back( threshold, monster );
            }

            return result;
        }();

        {
            assert( !numOfDaysForMonster.empty() );

            const auto & [threshold, monster] = numOfDaysForMonster.back();
            if ( numOfDays >= threshold ) {
                return { monster };
            }
        }

        for ( const auto & [threshold, monster] : numOfDaysForMonster ) {
            if ( numOfDays <= threshold ) {
                return { monster };
            }
        }

        assert( 0 );

        return { Monster::PEASANT };
    }

    bool HighScoreDataContainer::loadHoMM1( const std::string & standardPath, const std::string & campaignPath )
    {
        // HoMM1 .HS format: 10 entries × 87 bytes.
        // Each entry: [17-byte name][15-byte scenario][4-byte uint32 LE value][51-byte padding]
        // Name/scenario: space-padded to max length, null terminator at byte 16/14.
        constexpr size_t entrySize = 87;
        constexpr int entryCount = 10;
        constexpr size_t nameSize = 17;
        constexpr size_t scenarioSize = 15;
        constexpr size_t valueOffset = nameSize + scenarioSize;

        const auto readHS = [&]( const std::string & path, std::vector<HighscoreData> & entries, const bool isCampaign ) {
            StreamFile fs;
            if ( !fs.open( path, "rb" ) || fs.size() != entrySize * entryCount ) {
                return;
            }

            const std::vector<uint8_t> data = fs.getRaw( entrySize * entryCount );
            entries.clear();

            const std::string lang = fheroes2::getLanguageAbbreviation( fheroes2::SupportedLanguage::English );
            for ( int i = 0; i < entryCount; ++i ) {
                const uint8_t * entry = data.data() + i * entrySize;

                // Player name: copy up to the null terminator, strip trailing spaces.
                std::string name( reinterpret_cast<const char *>( entry ), nameSize - 1 );
                name.resize( std::min( name.size(), name.find( '\0' ) ) );
                while ( !name.empty() && name.back() == ' ' )
                    name.pop_back();

                if ( name.empty() )
                    continue;

                // Scenario name.
                std::string scenario( reinterpret_cast<const char *>( entry + nameSize ), scenarioSize - 1 );
                scenario.resize( std::min( scenario.size(), scenario.find( '\0' ) ) );
                while ( !scenario.empty() && scenario.back() == ' ' )
                    scenario.pop_back();

                // 4-byte little-endian value (days for campaign, rating for standard).
                const uint32_t value = static_cast<uint32_t>( entry[valueOffset] )
                                       | ( static_cast<uint32_t>( entry[valueOffset + 1] ) << 8 )
                                       | ( static_cast<uint32_t>( entry[valueOffset + 2] ) << 16 )
                                       | ( static_cast<uint32_t>( entry[valueOffset + 3] ) << 24 );

                const uint32_t days = isCampaign ? value : 0;
                const uint32_t rating = value; // both modes store rating here
                entries.push_back( { lang, name, scenario, 0, days, rating, 0 } );
            }
        };

        readHS( standardPath, _highScoresStandard, false );
        readHS( campaignPath, _highScoresCampaign, true );

        if ( _highScoresStandard.size() < highscoreMaximumEntries )
            populateStandardDefaultHighScores();
        if ( _highScoresCampaign.size() < highscoreMaximumEntries )
            populateCampaignDefaultHighScores();

        return true;
    }

    bool HighScoreDataContainer::saveHoMM1( const std::string & standardPath, const std::string & campaignPath ) const
    {
        constexpr size_t entrySize = 87;
        constexpr int entryCount = 10;
        constexpr size_t nameSize = 17;
        constexpr size_t scenarioSize = 15;
        constexpr size_t valueOffset = nameSize + scenarioSize;

        const auto writeHS = [&]( const std::string & path, const std::vector<HighscoreData> & entries ) -> bool {
            std::vector<uint8_t> data( entrySize * entryCount, 0 );

            for ( int i = 0; i < entryCount && i < static_cast<int>( entries.size() ); ++i ) {
                uint8_t * entry = data.data() + i * entrySize;
                const HighscoreData & hs = entries[i];

                // Name: space-fill 16 bytes, write name chars, set null at index 16.
                std::memset( entry, ' ', 16 );
                entry[16] = '\0';
                const size_t nameLen = std::min( hs.playerName.size(), static_cast<size_t>( 16 ) );
                std::memcpy( entry, hs.playerName.data(), nameLen );

                // Scenario: space-fill 14 bytes, write scenario chars, set null at index 14.
                uint8_t * scenPtr = entry + nameSize;
                std::memset( scenPtr, ' ', 14 );
                scenPtr[14] = '\0';
                const size_t scenLen = std::min( hs.scenarioName.size(), static_cast<size_t>( 14 ) );
                std::memcpy( scenPtr, hs.scenarioName.data(), scenLen );

                // Value: write rating (standard) or rating (campaign uses rating==dayCount).
                const uint32_t value = hs.rating;
                uint8_t * valuePtr = entry + valueOffset;
                valuePtr[0] = value & 0xFF;
                valuePtr[1] = ( value >> 8 ) & 0xFF;
                valuePtr[2] = ( value >> 16 ) & 0xFF;
                valuePtr[3] = ( value >> 24 ) & 0xFF;
            }

            StreamFile fs;
            if ( !fs.open( path, "wb" ) )
                return false;

            fs.putRaw( reinterpret_cast<const char *>( data.data() ), data.size() );
            return !fs.fail();
        };

        return writeHS( standardPath, _highScoresStandard ) && writeHS( campaignPath, _highScoresCampaign );
    }

    void HighScoreDataContainer::populateStandardDefaultHighScores()
    {
        const uint32_t currentTime = HighscoreData::generateCompletionTime();

        const std::string lang = fheroes2::getLanguageAbbreviation( fheroes2::getCurrentLanguage() );

        registerScoreStandard( { lang, _( "Lord Kilburn" ), "Beltway", currentTime, 70, 150, 0 } );
        registerScoreStandard( { lang, _( "Tsabu" ), "Deathgate", currentTime, 80, 140, 0 } );
        registerScoreStandard( { lang, _( "Sir Galant" ), "Enroth", currentTime, 90, 130, 0 } );
        registerScoreStandard( { lang, _( "Thundax" ), "Lost Continent", currentTime, 100, 120, 0 } );
        registerScoreStandard( { lang, _( "Lord Haart" ), "Mountain King", currentTime, 120, 110, 0 } );
        registerScoreStandard( { lang, _( "Ariel" ), "Pandemonium", currentTime, 140, 100, 0 } );
        registerScoreStandard( { lang, _( "Rebecca" ), "Terra Firma", currentTime, 160, 90, 0 } );
        registerScoreStandard( { lang, _( "Sandro" ), "The Clearing", currentTime, 180, 80, 0 } );
        registerScoreStandard( { lang, _( "Crodo" ), "Vikings!", currentTime, 200, 70, 0 } );
        registerScoreStandard( { lang, _( "Barock" ), "Wastelands", currentTime, 240, 60, 0 } );
    }

    void HighScoreDataContainer::populateCampaignDefaultHighScores()
    {
        const uint32_t currentTime = HighscoreData::generateCompletionTime();

        const std::string lang = fheroes2::getLanguageAbbreviation( fheroes2::getCurrentLanguage() );

        registerScoreCampaign( { lang, _( "Antoine" ), Campaign::getCampaignName( Campaign::IRONFIST_CAMPAIGN ), currentTime, 600, 600, 0 } );
        registerScoreCampaign( { lang, _( "Astra" ), Campaign::getCampaignName( Campaign::SLAYER_CAMPAIGN ), currentTime, 650, 650, 0 } );
        registerScoreCampaign( { lang, _( "Agar" ), Campaign::getCampaignName( Campaign::IRONFIST_CAMPAIGN ), currentTime, 700, 700, 0 } );
        registerScoreCampaign( { lang, _( "Vatawna" ), Campaign::getCampaignName( Campaign::SLAYER_CAMPAIGN ), currentTime, 750, 750, 0 } );
        registerScoreCampaign( { lang, _( "Vesper" ), Campaign::getCampaignName( Campaign::IRONFIST_CAMPAIGN ), currentTime, 800, 800, 0 } );
        registerScoreCampaign( { lang, _( "Ambrose" ), Campaign::getCampaignName( Campaign::SLAYER_CAMPAIGN ), currentTime, 850, 850, 0 } );
        registerScoreCampaign( { lang, _( "Troyan" ), Campaign::getCampaignName( Campaign::IRONFIST_CAMPAIGN ), currentTime, 900, 900, 0 } );
        registerScoreCampaign( { lang, _( "Jojosh" ), Campaign::getCampaignName( Campaign::SLAYER_CAMPAIGN ), currentTime, 1000, 1000, 0 } );
        registerScoreCampaign( { lang, _( "Wrathmont" ), Campaign::getCampaignName( Campaign::IRONFIST_CAMPAIGN ), currentTime, 2000, 2000, 0 } );
        registerScoreCampaign( { lang, _( "Maximus" ), Campaign::getCampaignName( Campaign::SLAYER_CAMPAIGN ), currentTime, 3000, 3000, 0 } );
    }
}
