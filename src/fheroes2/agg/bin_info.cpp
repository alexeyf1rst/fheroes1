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

#include "bin_info.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <ostream>
#include <string>
#include <utility>

#include "agg.h"
#include "battle_cell.h"
#include "logging.h"
#include "monster.h"
#include "monster_info.h"
#include "serialize.h"

namespace
{
    template <typename T>
    T getValue( const uint8_t * data, const size_t base, const size_t offset = 0 )
    {
        return fheroes2::getLEValue<T>( reinterpret_cast<const char *>( data ), base, offset );
    }

    const char * GetFilename( const int monsterId )
    {
        return fheroes2::getMonsterData( monsterId ).binFileName;
    }

    class MonsterAnimCache
    {
    public:
        Bin_Info::MonsterAnimInfo getAnimInfo( const int monsterID )
        {
            auto mapIterator = _animMap.find( monsterID );
            if ( mapIterator != _animMap.end() ) {
                return mapIterator->second;
            }

            Bin_Info::MonsterAnimInfo info( monsterID, AGG::getDataFromAggFile( GetFilename( monsterID ), false ) );
            if ( info.isValid() ) {
                _animMap[monsterID] = info;
                return info;
            }

            DEBUG_LOG( DBG_GAME, DBG_WARN, "Missing BIN file data: " << GetFilename( monsterID ) << ", monster ID: " << monsterID )
            return {};
        }

    private:
        std::map<int, Bin_Info::MonsterAnimInfo> _animMap;
    };

    MonsterAnimCache _infoCache;
}

namespace Bin_Info
{
    const size_t CORRECT_FRM_LENGTH = 821;

    // When base unit and its upgrade use the same FRM file (e.g. Archer and Ranger)
    // We modify animation speed value to make them go faster
    const double MOVE_SPEED_UPGRADE = 0.12;
    const double SHOOT_SPEED_UPGRADE = 0.08;
    const double RANGER_SHOOT_SPEED = 0.78;

    MonsterAnimInfo::MonsterAnimInfo( const int monsterID /* = 0 */, const std::vector<uint8_t> & bytes /* = std::vector<uint8_t>() */ )
    {
        if ( bytes.size() != Bin_Info::CORRECT_FRM_LENGTH ) {
            return;
        }

        const uint8_t * data = bytes.data();

        eyePosition = { getValue<int16_t>( data, 1 ), getValue<int16_t>( data, 3 ) };

        for ( size_t moveID = 0; moveID < 7; ++moveID ) {
            std::vector<int> moveOffset;

            moveOffset.reserve( 16 );

            for ( int frame = 0; frame < 16; ++frame ) {
                moveOffset.push_back( static_cast<int>( getValue<int8_t>( data, 5 + moveID * 16, frame ) ) );
            }

            frameXOffset.emplace_back( std::move( moveOffset ) );
        }

        // Idle animations data
        idleAnimationCount = data[117];
        if ( idleAnimationCount > 5U ) {
            idleAnimationCount = 5U; // here we need to reset our object
        }

        for ( uint32_t i = 0; i < idleAnimationCount; ++i ) {
            idlePriority.push_back( getValue<float>( data, 118, i ) );
        }

        for ( uint32_t i = 0; i < idleAnimationCount; ++i ) {
            unusedIdleDelays.push_back( getValue<uint32_t>( data, 138, i ) );
        }

        idleAnimationDelay = getValue<uint32_t>( data, 158 );

        // Monster speed data
        moveSpeed = getValue<uint32_t>( data, 162 );
        shootSpeed = getValue<uint32_t>( data, 166 );
        flightSpeed = getValue<uint32_t>( data, 170 );

        // Projectile data
        for ( size_t i = 0; i < 3; ++i ) {
            projectileOffset.emplace_back( getValue<int16_t>( data, 174 + ( i * 4 ) ), getValue<int16_t>( data, 176 + ( i * 4 ) ) );
        }

        // We change all projectile start positions to match the last projectile position in shooting animation,
        // this position will be used to calculate the projectile path, but will not be used in rendering.
        if ( monsterID == Monster::ARCHER ) {
            projectileOffset[0].x -= 30;
            projectileOffset[0].y += 5;
            projectileOffset[1].x -= 46;
            projectileOffset[1].y -= 5;
            projectileOffset[2].x -= 30;
            projectileOffset[2].y -= 40;
        }

        if ( monsterID == Monster::ORC ) {
            for ( fheroes2::Point & offset : projectileOffset ) {
                offset.x -= 20;
                offset.y = -36;
            }
        }

        if ( monsterID == Monster::TROLL ) {
            projectileOffset[0].x -= 30;
            projectileOffset[0].y -= 5;
            projectileOffset[1].x -= 14;
            --projectileOffset[1].y;
            projectileOffset[2].x -= 15;
            projectileOffset[2].y -= 19;
        }

        if ( monsterID == Monster::ELF ) {
            projectileOffset[0].x -= 19;
            projectileOffset[0].y += 22;
            projectileOffset[1].x -= 40;
            --projectileOffset[1].y;
            projectileOffset[2].x -= 19;
            // IMPORTANT: In BIN file Elves and Grand Elves have incorrect start Y position for lower shooting attack ( -1 ).
            projectileOffset[2].y = -54;
        }

        if ( monsterID == Monster::DRUID ) {
            projectileOffset[0].x -= 12;
            projectileOffset[0].y += 23;
            projectileOffset[1].x -= 20;
            projectileOffset[2].x -= 7;
            projectileOffset[2].y -= 21;
        }

        if ( monsterID == Monster::CENTAUR ) {
            projectileOffset[0].x -= 24;
            projectileOffset[0].y += 31;
            projectileOffset[1].x -= 32;
            projectileOffset[1].y += 2;
            projectileOffset[2].x -= 24;
            projectileOffset[2].y -= 27;
        }

        uint8_t projectileCount = data[186];
        if ( projectileCount > 12U ) {
            projectileCount = 12U; // here we need to reset our object
        }

        for ( uint8_t i = 0; i < projectileCount; ++i ) {
            projectileAngles.push_back( getValue<float>( data, 187, i ) );
        }

        // Positional offsets for sprites & drawing
        troopCountOffsetLeft = getValue<int32_t>( data, 235 );
        troopCountOffsetRight = getValue<int32_t>( data, 239 );

        // Load animation sequences themselves
        for ( int idx = MOVE_START; idx <= SHOOT3_END; ++idx ) {
            std::vector<int> anim;
            uint8_t count = data[243 + idx];
            if ( count > 16 ) {
                count = 16; // here we need to reset our object
            }

            for ( uint8_t frame = 0; frame < count; ++frame ) {
                anim.push_back( static_cast<int>( data[277 + idx * 16 + frame] ) );
            }

            animationFrames.emplace_back( std::move( anim ) );
        }

        if ( monsterID == Monster::WOLF ) { // Wolves have incorrect frame for lower attack animation
            if ( animationFrames[ATTACK3].size() == 3 && animationFrames[ATTACK3][0] == 16 ) {
                animationFrames[ATTACK3][0] = 2;
            }
            if ( animationFrames[ATTACK3_END].size() == 3 && animationFrames[ATTACK3_END][2] == 16 ) {
                animationFrames[ATTACK3_END][2] = 2;
            }
        }

        if ( monsterID == Monster::DWARF && animationFrames[DEATH].size() == 8 ) {
            // Dwarves and Battle Dwarves have incorrect death animation.
            animationFrames[DEATH].clear();

            for ( int frameId = 49; frameId <= 55; ++frameId ) {
                animationFrames[DEATH].push_back( frameId );
            }
        }

        // Modify AnimInfo for elemental monsters (no own FRM file - use Fire Element as base)
        int speedDiff = 0;
        switch ( monsterID ) {
        case Monster::EARTH_ELEMENT:
        case Monster::AIR_ELEMENT:
        case Monster::WATER_ELEMENT:
            speedDiff = static_cast<int>( Monster( monsterID ).GetSpeed() ) - Monster( Monster::FIRE_ELEMENT ).GetSpeed();
            break;
        default:
            break;
        }

        if ( std::abs( speedDiff ) > 0 ) {
            moveSpeed = static_cast<uint32_t>( ( 1 - MOVE_SPEED_UPGRADE * speedDiff ) * moveSpeed );
            shootSpeed = static_cast<uint32_t>( ( 1 - SHOOT_SPEED_UPGRADE * speedDiff ) * shootSpeed );
        }

        if ( frameXOffset[MOVE_STOP][0] == 0 && frameXOffset[MOVE_TILE_END][0] != 0 ) {
            frameXOffset[MOVE_STOP][0] = frameXOffset[MOVE_TILE_END][0];
        }

        for ( int idx = MOVE_START; idx <= MOVE_ONE; ++idx ) {
            frameXOffset[idx].resize( animationFrames[idx].size(), 0 );
        }

        if ( frameXOffset[MOVE_STOP].size() == 1 && frameXOffset[MOVE_STOP][0] == 0 ) {
            if ( frameXOffset[MOVE_TILE_END].size() == 1 && frameXOffset[MOVE_TILE_END][0] != 0 ) {
                frameXOffset[MOVE_STOP][0] = frameXOffset[MOVE_TILE_END][0];
            }
            else if ( frameXOffset[MOVE_TILE_START].size() == 1 && frameXOffset[MOVE_TILE_START][0] != 0 ) {
                frameXOffset[MOVE_STOP][0] = 44 + frameXOffset[MOVE_TILE_START][0];
            }
            else {
                frameXOffset[MOVE_STOP][0] = frameXOffset[MOVE_MAIN].back();
            }
        }

        // The Ogre has a duplicate frame of the 'STATIC' animation at the start of the 'MOVE_MAIN' animation, making him to move non-smoothly.
        if ( monsterID == Monster::OGRE && frameXOffset[MOVE_MAIN].size() == 9 && animationFrames[MOVE_MAIN].size() == 9 ) {
            animationFrames[MOVE_MAIN].erase( animationFrames[MOVE_MAIN].begin() );
            frameXOffset[MOVE_MAIN].erase( frameXOffset[MOVE_MAIN].begin() );
        }

        // Some creatures needs their 'x' offset in moving animations to be bigger by 3px to avoid sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::ORC || monsterID == Monster::OGRE || monsterID == Monster::DWARF
             || monsterID == Monster::UNICORN || monsterID == Monster::CENTAUR || monsterID == Monster::ROGUE ) {
            for ( const int animType : { MOVE_START, MOVE_TILE_START, MOVE_MAIN, MOVE_TILE_END, MOVE_STOP, MOVE_ONE } ) {
                for ( int & xOffset : frameXOffset[animType] ) {
                    xOffset += 3;
                }
            }
        }

        // Archers needs their 'x' offset in moving animations to be bigger by 1px to avoid sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::ARCHER ) {
            for ( const int animType : { MOVE_MAIN, MOVE_ONE } ) {
                for ( int & xOffset : frameXOffset[animType] ) {
                    ++xOffset;
                }
            }
        }

        // Paladin needs their 'x' offset in moving animations to be lower by 1px to avoid sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::PALADIN && frameXOffset[MOVE_MAIN].size() == 8 && animationFrames[MOVE_MAIN].size() == 8 ) {
            for ( const int animType : { MOVE_MAIN, MOVE_ONE } ) {
                for ( int & xOffset : frameXOffset[animType] ) {
                    --xOffset;
                }

                // The 5th and 8th frames needs extra shift by 2 and 1 pixel.
                frameXOffset[animType][4] -= 2;
                --frameXOffset[animType][7];
            }
        }

        // Goblins needs their 'x' offset in moving animations to be bigger by 6px to avoid sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::GOBLIN ) {
            for ( const int animType : { MOVE_TILE_START, MOVE_MAIN, MOVE_STOP, MOVE_ONE } ) {
                for ( int & xOffset : frameXOffset[animType] ) {
                    xOffset += 6;
                }
            }
        }

        // Trolls needs their 'x' offset in moving animations to be bigger by 2px to avoid sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::TROLL && frameXOffset[MOVE_MAIN].size() == 14 && frameXOffset[MOVE_ONE].size() == 14 ) {
            for ( const int animType : { MOVE_MAIN, MOVE_ONE } ) {
                for ( int & xOffset : frameXOffset[animType] ) {
                    xOffset += 2;
                }

                // The 7th and 14th frames needs extra shift by 1 px.
                ++frameXOffset[animType][6];
                ++frameXOffset[animType][13];
            }
        }

        // Nomad needs their 'x' offset in moving animations to be corrected to avoid one sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::NOMAD && frameXOffset[MOVE_MAIN].size() == 8 && frameXOffset[MOVE_ONE].size() == 8 ) {
            for ( const int animType : { MOVE_MAIN, MOVE_ONE } ) {
                frameXOffset[animType][2] -= 2;
            }
        }

        // Nomad needs their 'x' offset in moving animations to be corrected to avoid one sprite shift in Well and during diagonal movement.
        if ( monsterID == Monster::MEDUSA && frameXOffset[MOVE_MAIN].size() == 15 && frameXOffset[MOVE_ONE].size() == 15 ) {
            for ( const int animType : { MOVE_MAIN, MOVE_ONE } ) {
                frameXOffset[animType][3] += 2;
                frameXOffset[animType][4] += 2;
                ++frameXOffset[animType][5];
                ++frameXOffset[animType][9];
                frameXOffset[animType][10] += 2;
                ++frameXOffset[animType][11];
                ++frameXOffset[animType][14];
            }
        }

        // Elementals needs their 'x' offset in moving animations to be corrected to avoid sprite shift in Well and during diagonal movement.
        if ( ( monsterID == Monster::EARTH_ELEMENT || monsterID == Monster::AIR_ELEMENT || monsterID == Monster::FIRE_ELEMENT || monsterID == Monster::WATER_ELEMENT )
             && frameXOffset[MOVE_MAIN].size() == 8 && frameXOffset[MOVE_ONE].size() == 8 ) {
            for ( const int animType : { MOVE_MAIN, MOVE_ONE } ) {
                frameXOffset[animType][0] += 3;
                frameXOffset[animType][1] += 3;
                frameXOffset[animType][2] += 5;
                frameXOffset[animType][3] += 3;
                --frameXOffset[animType][5];
                ++frameXOffset[animType][6];
                --frameXOffset[animType][7];
            }
        }

        if ( monsterID == Monster::SWORDSMAN ) {
            // X offset fix for Swordsman.
            if ( frameXOffset[MOVE_START].size() == 2 && frameXOffset[MOVE_STOP].size() == 1 ) {
                frameXOffset[MOVE_START][0] = 0;
                frameXOffset[MOVE_START][1] = Battle::Cell::widthPx * 1 / 8;
                for ( int & xOffset : frameXOffset[MOVE_MAIN] ) {
                    xOffset += Battle::Cell::widthPx / 4 + 3;
                }

                frameXOffset[MOVE_STOP][0] = Battle::Cell::widthPx;
            }

            // Add extra movement frame to make the animation more smooth.
            if ( animationFrames[MOVE_MAIN].size() == 7 && frameXOffset[MOVE_MAIN].size() == 7 ) {
                animationFrames[MOVE_MAIN].insert( animationFrames[MOVE_MAIN].begin(), 45 );
                frameXOffset[MOVE_MAIN].insert( frameXOffset[MOVE_MAIN].begin(), 8 );
            }
            // Also update the once-cell move animation.
            if ( animationFrames[MOVE_ONE].size() == 10 && frameXOffset[MOVE_ONE].size() == 10 ) {
                animationFrames[MOVE_ONE].insert( animationFrames[MOVE_ONE].begin() + 2, 45 );
                frameXOffset[MOVE_ONE].insert( frameXOffset[MOVE_ONE].begin() + 2, 8 );
            }
        }
    }

    bool MonsterAnimInfo::isValid() const
    {
        if ( animationFrames.size() != SHOOT3_END + 1 ) {
            return false;
        }

        // Check absolute minimal set of animations.
        for ( const size_t i : { MOVE_MAIN, STATIC, DEATH, WINCE_UP, ATTACK1, ATTACK2, ATTACK3 } ) {
            if ( animationFrames[i].empty() ) {
                return false;
            }
        }

        return idlePriority.size() == static_cast<size_t>( idleAnimationCount );
    }

    size_t MonsterAnimInfo::getProjectileID( const double angle ) const
    {
        const std::vector<float> & angles = projectileAngles;
        if ( angles.empty() ) {
            return 0;
        }

        for ( size_t id = 0u; id < angles.size() - 1; ++id ) {
            if ( angle >= static_cast<double>( angles[id] + angles[id + 1] ) / 2.0 ) {
                return id;
            }
        }

        return angles.size() - 1;
    }

    MonsterAnimInfo GetMonsterInfo( const uint32_t monsterID )
    {
        return _infoCache.getAnimInfo( monsterID );
    }
}
