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

#include "monster.h"

#include <algorithm>
#include <cassert>
#include <vector>

#include "castle.h"
#include "icn.h"
#include "luck.h"
#include "morale.h"
#include "race.h"
#include "rand.h"
#include "resource.h"
#include "spell.h"
#include "translations.h"

uint32_t Monster::GetMissileICN( uint32_t monsterID )
{
    switch ( monsterID ) {
    case Monster::ARCHER:
        return ICN::ARCH_MSL;
    case Monster::ORC:
        return ICN::ORC__MSL;
    case Monster::TROLL:
        return ICN::TROLLMSL;
    case Monster::ELF:
        return ICN::ELF__MSL;
    case Monster::DRUID:
        return ICN::DRUIDMSL;
    case Monster::CENTAUR:
        // Doesn't have own missile file, game falls back to ELF__MSL
        return ICN::ELF__MSL;

    default:
        break;
    }

    return ICN::UNKNOWN;
}

Monster::Monster( const Spell & sp )
    : id( UNKNOWN )
{
    switch ( sp.GetID() ) {
    case Spell::SETEGUARDIAN:
    case Spell::SUMMONEELEMENT:
        id = EARTH_ELEMENT;
        break;

    case Spell::SETAGUARDIAN:
    case Spell::SUMMONAELEMENT:
        id = AIR_ELEMENT;
        break;

    case Spell::SETFGUARDIAN:
    case Spell::SUMMONFELEMENT:
        id = FIRE_ELEMENT;
        break;

    case Spell::SETWGUARDIAN:
    case Spell::SUMMONWELEMENT:
        id = WATER_ELEMENT;
        break;

    case Spell::HAUNT:
        id = GHOST;
        break;

    default:
        break;
    }
}

Monster::Monster( const int race, const uint32_t dw )
    : id( UNKNOWN )
{
    id = FromDwelling( race, dw ).id;
}

uint32_t Monster::GetAttack() const
{
    return fheroes2::getMonsterData( id ).battleStats.attack;
}

uint32_t Monster::GetDefense() const
{
    return fheroes2::getMonsterData( id ).battleStats.defense;
}

int Monster::GetMorale() const
{
    return Morale::NORMAL;
}

int Monster::GetLuck() const
{
    return Luck::NORMAL;
}

int Monster::GetRace() const
{
    return fheroes2::getMonsterData( id ).generalStats.race;
}

uint32_t Monster::GetShots() const
{
    return fheroes2::getMonsterData( id ).battleStats.shots;
}

double Monster::GetMonsterStrength( int attack /* = -1 */, int defense /* = -1 */ ) const
{
    // TODO: do not use virtual functions when calculating strength for troops without hero's skills.

    if ( attack < 0 ) {
        // This is a virtual function that can be overridden in subclasses and take into account various bonuses (for example, hero bonuses)
        attack = GetAttack();
    }

    if ( defense < 0 ) {
        // This is a virtual function that can be overridden in subclasses and take into account various bonuses (for example, hero bonuses)
        defense = GetDefense();
    }

    const double attackDefense = 1.0 + attack * 0.1 + defense * 0.05;

    return attackDefense * fheroes2::getMonsterData( id ).battleStats.monsterBaseStrength;
}

uint32_t Monster::GetRNDSize() const
{
    if ( !isValid() )
        return 0;

    const uint32_t defaultArmySizePerLevel[7] = { 0, 50, 30, 25, 25, 12, 8 };
    uint32_t result = 0;

    // Check for outliers
    switch ( id ) {
    case PEASANT:
        result = 80;
        break;
    case ROGUE:
        result = 40;
        break;
    case PIKEMAN:
    case WOLF:
    case ELF:
        result = 30;
        break;
    case GARGOYLE:
        result = 25;
        break;
    case GHOST:
    case MEDUSA:
        result = 20;
        break;
    case MINOTAUR:
    case UNICORN:
        result = 16;
        break;
    case CAVALRY:
        result = 18;
        break;
    case PALADIN:
    case CYCLOPS:
    case PHOENIX:
        result = 12;
        break;
    default:
        // for most units default range is okay
        result = defaultArmySizePerLevel[GetMonsterLevel()];
        break;
    }

    return ( result > 1 ) ? Rand::Get( result / 2, result ) : 1;
}

bool Monster::isAbilityPresent( const fheroes2::MonsterAbilityType abilityType ) const
{
    return fheroes2::isAbilityPresent( fheroes2::getMonsterData( id ).battleStats.abilities, abilityType );
}

bool Monster::isWeaknessPresent( const fheroes2::MonsterWeaknessType weaknessType ) const
{
    return fheroes2::isWeaknessPresent( fheroes2::getMonsterData( id ).battleStats.weaknesses, weaknessType );
}

Monster Monster::GetDowngrade() const
{
    // HoMM1 has no unit upgrades
    return Monster( id );
}

Monster Monster::GetUpgrade() const
{
    // HoMM1 has no unit upgrades
    return Monster( id );
}

Monster Monster::FromDwelling( int race, uint32_t dwelling )
{
    switch ( dwelling ) {
    case DWELLING_MONSTER1:
        switch ( race ) {
        case Race::KNGT:
            return Monster( PEASANT );
        case Race::BARB:
            return Monster( GOBLIN );
        case Race::SORC:
            return Monster( SPRITE );
        case Race::WRLK:
            return Monster( CENTAUR );
        default:
            break;
        }
        break;

    case DWELLING_MONSTER2:
        switch ( race ) {
        case Race::KNGT:
            return Monster( ARCHER );
        case Race::BARB:
            return Monster( ORC );
        case Race::SORC:
            return Monster( DWARF );
        case Race::WRLK:
            return Monster( GARGOYLE );
        default:
            break;
        }
        break;

    case DWELLING_MONSTER3:
        switch ( race ) {
        case Race::KNGT:
            return Monster( PIKEMAN );
        case Race::BARB:
            return Monster( WOLF );
        case Race::SORC:
            return Monster( ELF );
        case Race::WRLK:
            return Monster( GRIFFIN );
        default:
            break;
        }
        break;

    case DWELLING_MONSTER4:
        switch ( race ) {
        case Race::KNGT:
            return Monster( SWORDSMAN );
        case Race::BARB:
            return Monster( OGRE );
        case Race::SORC:
            return Monster( DRUID );
        case Race::WRLK:
            return Monster( MINOTAUR );
        default:
            break;
        }
        break;

    case DWELLING_MONSTER5:
        switch ( race ) {
        case Race::KNGT:
            return Monster( CAVALRY );
        case Race::BARB:
            return Monster( TROLL );
        case Race::SORC:
            return Monster( UNICORN );
        case Race::WRLK:
            return Monster( HYDRA );
        default:
            break;
        }
        break;

    case DWELLING_MONSTER6:
        switch ( race ) {
        case Race::KNGT:
            return Monster( PALADIN );
        case Race::BARB:
            return Monster( CYCLOPS );
        case Race::SORC:
            return Monster( PHOENIX );
        case Race::WRLK:
            return Monster( GREEN_DRAGON );
        default:
            break;
        }
        break;

    default:
        break;
    }

    return Monster( UNKNOWN );
}

Monster Monster::Rand( const LevelType type )
{
    if ( type == LevelType::LEVEL_ANY )
        return Monster( Rand::Get( PEASANT, WATER_ELEMENT ) );
    static std::vector<Monster> monstersVec[static_cast<int>( LevelType::LEVEL_4 )];
    if ( monstersVec[0].empty() ) {
        for ( uint32_t i = PEASANT; i <= WATER_ELEMENT; ++i ) {
            const Monster monster( i );
            if ( monster.GetRandomUnitLevel() > LevelType::LEVEL_ANY )
                monstersVec[static_cast<int>( monster.GetRandomUnitLevel() ) - 1].push_back( monster );
        }
    }
    return Rand::Get( monstersVec[static_cast<int>( type ) - 1] );
}

Monster::LevelType Monster::GetRandomUnitLevel() const
{
    switch ( id ) {
    case PEASANT:
    case ARCHER:
    case GOBLIN:
    case ORC:
    case SPRITE:
    case CENTAUR:
    case ROGUE:
    case RANDOM_MONSTER_LEVEL_1:
        return LevelType::LEVEL_1;

    case PIKEMAN:
    case WOLF:
    case DWARF:
    case ELF:
    case GARGOYLE:
    case NOMAD:
    case RANDOM_MONSTER_LEVEL_2:
        return LevelType::LEVEL_2;

    case SWORDSMAN:
    case CAVALRY:
    case OGRE:
    case TROLL:
    case DRUID:
    case GRIFFIN:
    case MINOTAUR:
    case GHOST:
    case MEDUSA:
    case EARTH_ELEMENT:
    case AIR_ELEMENT:
    case FIRE_ELEMENT:
    case WATER_ELEMENT:
    case RANDOM_MONSTER_LEVEL_3:
        return LevelType::LEVEL_3;

    case PALADIN:
    case CYCLOPS:
    case UNICORN:
    case PHOENIX:
    case HYDRA:
    case GREEN_DRAGON:
    case GENIE:
    case RANDOM_MONSTER_LEVEL_4:
        return LevelType::LEVEL_4;

    case RANDOM_MONSTER:
        switch ( Rand::Get( 0, 3 ) ) {
        default:
            return LevelType::LEVEL_1;
        case 1:
            return LevelType::LEVEL_2;
        case 2:
            return LevelType::LEVEL_3;
        case 3:
            return LevelType::LEVEL_4;
        }

    default:
        break;
    }

    return LevelType::LEVEL_ANY;
}

uint32_t Monster::GetDwelling() const
{
    switch ( id ) {
    case PEASANT:
    case GOBLIN:
    case SPRITE:
    case CENTAUR:
        return DWELLING_MONSTER1;

    case ARCHER:
    case ORC:
    case DWARF:
    case GARGOYLE:
        return DWELLING_MONSTER2;

    case PIKEMAN:
    case WOLF:
    case ELF:
    case GRIFFIN:
        return DWELLING_MONSTER3;

    case SWORDSMAN:
    case OGRE:
    case DRUID:
    case MINOTAUR:
        return DWELLING_MONSTER4;

    case CAVALRY:
    case TROLL:
    case UNICORN:
    case HYDRA:
        return DWELLING_MONSTER5;

    case PALADIN:
    case CYCLOPS:
    case PHOENIX:
    case GREEN_DRAGON:
        return DWELLING_MONSTER6;

    default:
        break;
    }

    return 0;
}

const char * Monster::GetName() const
{
    return _( fheroes2::getMonsterData( id ).generalStats.untranslatedName );
}

const char * Monster::GetMultiName() const
{
    return _( fheroes2::getMonsterData( id ).generalStats.untranslatedPluralName );
}

const char * Monster::GetPluralName( uint32_t count ) const
{
    const fheroes2::MonsterGeneralStats & generalStats = fheroes2::getMonsterData( id ).generalStats;
    return count == 1 ? _( generalStats.untranslatedName ) : _( generalStats.untranslatedPluralName );
}

const char * Monster::getRandomRaceMonstersName( const uint32_t building )
{
    switch ( building ) {
    case DWELLING_MONSTER1:
        return _( "randomRace|level 1 creatures" );
    case DWELLING_MONSTER2:
        return _( "randomRace|level 2 creatures" );
    case DWELLING_MONSTER3:
        return _( "randomRace|level 3 creatures" );
    case DWELLING_MONSTER4:
        return _( "randomRace|level 4 creatures" );
    case DWELLING_MONSTER5:
        return _( "randomRace|level 5 creatures" );
    case DWELLING_MONSTER6:
        return _( "randomRace|level 6 creatures" );
    default:
        assert( 0 );
        return _( "Unknown Monsters" );
    }
}

int32_t Monster::ICNMonh() const
{
    return id >= PEASANT && id <= WATER_ELEMENT ? ICN::MONH0000 + id - PEASANT : ICN::UNKNOWN;
}

Funds Monster::GetUpgradeCost() const
{
    const Monster upgr = GetUpgrade();
    if ( id == upgr.id ) {
        return {};
    }

    return ( upgr.GetCost() - GetCost() ) * 2;
}

uint32_t Monster::GetCountFromHitPoints( const Monster & mons, const uint32_t hp )
{
    if ( hp == 0 ) {
        return 0;
    }

    const uint32_t singleMonsterHP = mons.GetHitPoints();
    const uint32_t quotient = hp / singleMonsterHP;
    const uint32_t remainder = hp % singleMonsterHP;

    return ( remainder > 0 ? quotient + 1 : quotient );
}
