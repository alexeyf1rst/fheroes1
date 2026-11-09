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

#include "monster_info.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>

#include "icn.h"
#include "m82.h"
#include "monster.h"
#include "race.h"
#include "speed.h"
#include "spell.h"
#include "tools.h"
#include "translations.h"

namespace
{
    std::vector<fheroes2::MonsterData> monsterData;

    double getMonsterBaseStrength( const fheroes2::MonsterData & data )
    {
        const fheroes2::MonsterBattleStats & battleStats = data.battleStats;
        const std::vector<fheroes2::MonsterAbility> & abilities = battleStats.abilities;

        const double effectiveHP = battleStats.hp * ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION ) ? 1.4 : 1 );
        const bool isArchers = ( battleStats.shots > 0 );

        double damagePotential = ( battleStats.damageMin + battleStats.damageMax ) / 2.0;

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::DOUBLE_SHOOTING ) ) {
            // How can it be that this ability is assigned not to a shooter?
            assert( isArchers );

            damagePotential *= 2;
        }
        else if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::DOUBLE_MELEE_ATTACK ) ) {
            // Melee attacker will lose potential on second attack after retaliation
            damagePotential *= fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION ) ? 2 : 1.75;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::DOUBLE_DAMAGE_TO_UNDEAD ) ) {
            damagePotential *= 1.15; // 15% of all Monsters are Undead, deals double damage.
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::TWO_CELL_MELEE_ATTACK ) ) {
            damagePotential *= 1.2;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::UNLIMITED_RETALIATION ) ) {
            damagePotential *= 1.25;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::ALL_ADJACENT_CELL_MELEE_ATTACK )
             || fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::AREA_SHOT ) ) {
            damagePotential *= 1.3;
        }

        double monsterSpecial = 1.0;

        if ( isArchers ) {
            monsterSpecial += fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::NO_MELEE_PENALTY ) ? 0.5 : 0.4;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::FLYING ) ) {
            monsterSpecial += 0.3;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::ENEMY_HALVING ) ) {
            monsterSpecial += 1;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::SOUL_EATER ) ) {
            monsterSpecial += 2;
        }

        if ( fheroes2::isAbilityPresent( abilities, fheroes2::MonsterAbilityType::HP_DRAIN ) ) {
            monsterSpecial += 0.3;
        }

        if ( const auto abilityIter = std::find( abilities.begin(), abilities.end(), fheroes2::MonsterAbilityType::SPELL_CASTER ); abilityIter != abilities.end() ) {
            // This is a tricky evaluation. Spell casting ability depends on a type of spell and chance to inflict the spell.
            switch ( abilityIter->value ) {
            case Spell::PARALYZE:
            case Spell::BLIND:
            case Spell::PETRIFY:
                monsterSpecial += abilityIter->percentage / 100.0;
                break;
            case Spell::DISPEL:
            case Spell::CURSE:
                // These spell are very weak and do not impact much during battle.
                monsterSpecial += abilityIter->percentage / 100.0 / 10.0;
                break;
            default:
                // Did you add a new spell casting ability? Add the logic above!
                assert( 0 );
                break;
            }
        }

        // Higher speed gives initiative advantage/first attack. Remap speed value to -0.2...+0.15, AVERAGE is 0
        // Punish slow speeds more as unit won't participate in first rounds and slows down strategic army
        const int speedDiff = battleStats.speed - Speed::AVERAGE;
        monsterSpecial += ( speedDiff < 0 ) ? speedDiff * 0.1 : speedDiff * 0.05;

        // Additional HP and Damage effectiveness diminishes with every combat round; strictly x4 HP == x2 unit count
        return sqrt( damagePotential * effectiveHP ) * monsterSpecial;
    }

    void populateMonsterData()
    {
        const int monsterIcnIds[Monster::MONSTER_COUNT]
            = { ICN::UNKNOWN,  ICN::PEASANT,  ICN::ARCHER,   ICN::PIKEMAN,  ICN::SWORDSMN, ICN::CAVALRYR, ICN::PALADIN,
                ICN::GOBLIN,   ICN::ORC,      ICN::WOLF,     ICN::OGRE,     ICN::TROLL,    ICN::CYCLOPS,
                ICN::SPRITE,   ICN::DWARF,    ICN::ELF,      ICN::DRUID,    ICN::UNICORN,  ICN::PHOENIX,
                ICN::CENTAUR,  ICN::GARGOYLE, ICN::GRIFFIN,  ICN::MINOTAUR, ICN::HYDRA,    ICN::DRAGGREE,
                ICN::ROGUE,    ICN::NOMAD,    ICN::GHOST,    ICN::GENIE,    ICN::MEDUSA,
                ICN::EELEM,    ICN::AELEM,    ICN::FELEM,    ICN::WELEM,    ICN::UNKNOWN,  ICN::UNKNOWN,  ICN::UNKNOWN,  ICN::UNKNOWN,  ICN::UNKNOWN };

        const char * binFileName[Monster::MONSTER_COUNT]
            = { "UNKNOWN",      "PEAS_FRM.BIN", "ARCHRFRM.BIN", "PIKMNFRM.BIN", "SWRDSFRM.BIN", "CVLRYFRM.BIN", "PALADFRM.BIN",
                "GOBLNFRM.BIN", "ORC__FRM.BIN", "WOLF_FRM.BIN", "OGRE_FRM.BIN", "TROLLFRM.BIN", "CYCLOFRM.BIN",
                "SPRITFRM.BIN", "DWARFFRM.BIN", "ELF__FRM.BIN", "DRUIDFRM.BIN", "UNICOFRM.BIN", "PHOENFRM.BIN",
                "CENTRFRM.BIN", "GARGLFRM.BIN", "GRIFFFRM.BIN", "MINOTFRM.BIN", "HYDRAFRM.BIN", "DRAGGFRM.BIN",
                "ROGUEFRM.BIN", "NOMADFRM.BIN", "GHOSTFRM.BIN", "GENIEFRM.BIN", "MEDUSFRM.BIN",
                "FELEMFRM.BIN", "FELEMFRM.BIN", "FELEMFRM.BIN", "FELEMFRM.BIN", "UNKNOWN",      "UNKNOWN",      "UNKNOWN",      "UNKNOWN",      "UNKNOWN" };

        const fheroes2::MonsterSound monsterSounds[Monster::MONSTER_COUNT] = {
            // melee attack | death | movement | wince | ranged attack | takeoff | landing | explosion
            { M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Unknown Monster
            { M82::PSNTATTK, M82::PSNTKILL, M82::PSNTMOVE, M82::PSNTWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Peasant
            { M82::ARCHATTK, M82::ARCHKILL, M82::ARCHMOVE, M82::ARCHWNCE, M82::ARCHSHOT, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Archer
            { M82::PIKEATTK, M82::PIKEKILL, M82::PIKEMOVE, M82::PIKEWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Pikeman
            { M82::SWDMATTK, M82::SWDMKILL, M82::SWDMMOVE, M82::SWDMWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Swordsman
            { M82::CAVLATTK, M82::CAVLKILL, M82::CAVLMOVE, M82::CAVLWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Cavalry
            { M82::PLDNATTK, M82::PLDNKILL, M82::PLDNMOVE, M82::PLDNWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Paladin
            { M82::GBLNATTK, M82::GBLNKILL, M82::GBLNMOVE, M82::GBLNWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Goblin
            { M82::ORC_ATTK, M82::ORC_KILL, M82::ORC_MOVE, M82::ORC_WNCE, M82::ORC_SHOT, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Orc
            { M82::WOLFATTK, M82::WOLFKILL, M82::WOLFMOVE, M82::WOLFWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Wolf
            { M82::OGREATTK, M82::OGREKILL, M82::OGREMOVE, M82::OGREWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Ogre
            { M82::TRLLATTK, M82::TRLLKILL, M82::TRLLMOVE, M82::TRLLWNCE, M82::TRLLSHOT, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Troll
            { M82::CYCLATTK, M82::CYCLKILL, M82::CYCLMOVE, M82::CYCLWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Cyclops
            { M82::SPRTATTK, M82::SPRTKILL, M82::SPRTMOVE, M82::SPRTWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Sprite
            { M82::DWRFATTK, M82::DWRFKILL, M82::DWRFMOVE, M82::DWRFWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Dwarf
            { M82::ELF_ATTK, M82::ELF_KILL, M82::ELF_MOVE, M82::ELF_WNCE, M82::ELF_SHOT, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Elf
            { M82::DRUIATTK, M82::DRUIKILL, M82::DRUIMOVE, M82::DRUIWNCE, M82::DRUISHOT, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Druid
            { M82::UNICATTK, M82::UNICKILL, M82::UNICMOVE, M82::UNICWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Unicorn
            { M82::PHOEATTK, M82::PHOEKILL, M82::PHOEMOVE, M82::PHOEWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Phoenix
            { M82::CNTRATTK, M82::CNTRKILL, M82::CNTRMOVE, M82::CNTRWNCE, M82::CNTRSHOT, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Centaur
            { M82::GARGATTK, M82::GARGKILL, M82::GARGMOVE, M82::GARGWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Gargoyle
            { M82::GRIFATTK, M82::GRIFKILL, M82::GRIFMOVE, M82::GRIFWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Griffin
            { M82::MINOATTK, M82::MINOKILL, M82::MINOMOVE, M82::MINOWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Minotaur
            { M82::HYDRATTK, M82::HYDRKILL, M82::HYDRMOVE, M82::HYDRWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Hydra
            { M82::DRGNATTK, M82::DRGNKILL, M82::DRGNMOVE, M82::DRGNWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Green Dragon
            { M82::ROGUATTK, M82::ROGUKILL, M82::ROGUMOVE, M82::ROGUWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Rogue
            { M82::NMADATTK, M82::NMADKILL, M82::NMADMOVE, M82::NMADWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Nomad
            { M82::GHSTATTK, M82::GHSTKILL, M82::GHSTMOVE, M82::GHSTWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Ghost
            { M82::GENIATTK, M82::GENIKILL, M82::GENIMOVE, M82::GENIWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Genie
            { M82::MEDSATTK, M82::MEDSKILL, M82::MEDSMOVE, M82::MEDSWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Medusa
            { M82::EELMATTK, M82::EELMKILL, M82::EELMMOVE, M82::EELMWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Earth Elemental
            { M82::AELMATTK, M82::AELMKILL, M82::AELMMOVE, M82::AELMWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Air Elemental
            { M82::FELMATTK, M82::FELMKILL, M82::FELMMOVE, M82::FELMWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Fire Elemental
            { M82::WELMATTK, M82::WELMKILL, M82::WELMMOVE, M82::WELMWNCE, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Water Elemental
            { M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Random Monster
            { M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Random Monster 1
            { M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Random Monster 2
            { M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Random Monster 3
            { M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN, M82::UNKNOWN }, // Random Monster 4
        };

        // Monster abilities and weaknesses will be added later. Base strength is calculated based on abilities.
        const fheroes2::MonsterBattleStats monsterBattleStats[Monster::MONSTER_COUNT] = {
            // attack | defence | damageMin | damageMax | hp | speed | shots | baseStrength | abilities | weaknesses
            { 0, 0, 0, 0, 0, Speed::VERYSLOW, 0, 0, {}, {} }, // Unknown Monster
            { 1, 1, 1, 1, 1, Speed::VERYSLOW, 0, 0, {}, {} }, // Peasant
            { 5, 3, 2, 3, 10, Speed::VERYSLOW, 12, 0, {}, {} }, // Archer
            { 5, 9, 3, 4, 15, Speed::AVERAGE, 0, 0, {}, {} }, // Pikeman
            { 7, 9, 4, 6, 25, Speed::AVERAGE, 0, 0, {}, {} }, // Swordsman
            { 10, 9, 5, 10, 30, Speed::VERYFAST, 0, 0, {}, {} }, // Cavalry
            { 11, 12, 10, 20, 50, Speed::FAST, 0, 0, {}, {} }, // Paladin
            { 3, 1, 1, 2, 3, Speed::AVERAGE, 0, 0, {}, {} }, // Goblin
            { 3, 4, 2, 3, 10, Speed::VERYSLOW, 8, 0, {}, {} }, // Orc
            { 6, 2, 3, 5, 20, Speed::VERYFAST, 0, 0, {}, {} }, // Wolf
            { 9, 5, 4, 6, 40, Speed::VERYSLOW, 0, 0, {}, {} }, // Ogre
            { 10, 5, 5, 7, 40, Speed::AVERAGE, 8, 0, {}, {} }, // Troll
            { 12, 9, 12, 24, 80, Speed::FAST, 0, 0, {}, {} }, // Cyclops
            { 4, 2, 1, 2, 2, Speed::AVERAGE, 0, 0, {}, {} }, // Sprite
            { 6, 5, 2, 4, 20, Speed::VERYSLOW, 0, 0, {}, {} }, // Dwarf
            { 4, 3, 2, 3, 15, Speed::AVERAGE, 24, 0, {}, {} }, // Elf
            { 7, 5, 5, 8, 25, Speed::FAST, 8, 0, {}, {} }, // Druid
            { 10, 9, 7, 14, 40, Speed::FAST, 0, 0, {}, {} }, // Unicorn
            { 12, 10, 20, 40, 100, Speed::ULTRAFAST, 0, 0, {}, {} }, // Phoenix
            { 3, 1, 1, 2, 5, Speed::AVERAGE, 8, 0, {}, {} }, // Centaur
            { 4, 7, 2, 3, 15, Speed::VERYFAST, 0, 0, {}, {} }, // Gargoyle
            { 6, 6, 3, 5, 25, Speed::AVERAGE, 0, 0, {}, {} }, // Griffin
            { 9, 8, 5, 10, 35, Speed::AVERAGE, 0, 0, {}, {} }, // Minotaur
            { 8, 9, 6, 12, 75, Speed::VERYSLOW, 0, 0, {}, {} }, // Hydra
            { 12, 12, 25, 50, 200, Speed::AVERAGE, 0, 0, {}, {} }, // Green Dragon
            { 6, 1, 1, 2, 4, Speed::FAST, 0, 0, {}, {} }, // Rogue
            { 7, 6, 2, 5, 20, Speed::VERYFAST, 0, 0, {}, {} }, // Nomad
            { 8, 7, 4, 6, 20, Speed::FAST, 0, 0, {}, {} }, // Ghost
            { 10, 9, 20, 30, 50, Speed::VERYFAST, 0, 0, {}, {} }, // Genie
            { 8, 9, 6, 10, 35, Speed::AVERAGE, 0, 0, {}, {} }, // Medusa
            { 8, 8, 4, 5, 50, Speed::SLOW, 0, 0, {}, {} }, // Earth Elemental
            { 7, 7, 2, 8, 35, Speed::VERYFAST, 0, 0, {}, {} }, // Air Elemental
            { 8, 6, 4, 6, 40, Speed::FAST, 0, 0, {}, {} }, // Fire Elemental
            { 6, 8, 3, 7, 45, Speed::AVERAGE, 0, 0, {}, {} }, // Water Elemental
            { 0, 0, 0, 0, 0, Speed::VERYSLOW, 0, 0, {}, {} }, // Random Monster
            { 0, 0, 0, 0, 0, Speed::VERYSLOW, 0, 0, {}, {} }, // Random Monster 1
            { 0, 0, 0, 0, 0, Speed::VERYSLOW, 0, 0, {}, {} }, // Random Monster 2
            { 0, 0, 0, 0, 0, Speed::VERYSLOW, 0, 0, {}, {} }, // Random Monster 3
            { 0, 0, 0, 0, 0, Speed::VERYSLOW, 0, 0, {}, {} }, // Random Monster 4
        };

        const fheroes2::MonsterGeneralStats monsterGeneralStats[Monster::MONSTER_COUNT]
            = { // name | plural name | growth | race | level | cost
                { gettext_noop( "Unknown Monster" ), gettext_noop( "Unknown Monsters" ), 0, Race::NONE, 0, { 0, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Peasant" ), gettext_noop( "Peasants" ), 12, Race::KNGT, 1, { 20, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Archer" ), gettext_noop( "Archers" ), 8, Race::KNGT, 2, { 150, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Pikeman" ), gettext_noop( "Pikemen" ), 5, Race::KNGT, 3, { 200, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Swordsman" ), gettext_noop( "Swordsmen" ), 4, Race::KNGT, 4, { 250, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Cavalry" ), gettext_noop( "Cavalries" ), 3, Race::KNGT, 5, { 300, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Paladin" ), gettext_noop( "Paladins" ), 2, Race::KNGT, 6, { 600, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Goblin" ), gettext_noop( "Goblins" ), 10, Race::BARB, 1, { 40, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Orc" ), gettext_noop( "Orcs" ), 8, Race::BARB, 2, { 140, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Wolf" ), gettext_noop( "Wolves" ), 5, Race::BARB, 3, { 200, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Ogre" ), gettext_noop( "Ogres" ), 4, Race::BARB, 4, { 300, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Troll" ), gettext_noop( "Trolls" ), 3, Race::BARB, 5, { 600, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Cyclops" ), gettext_noop( "Cyclopes" ), 2, Race::BARB, 6, { 750, 0, 0, 0, 0, 1, 0 } },
                { gettext_noop( "Sprite" ), gettext_noop( "Sprites" ), 8, Race::SORC, 1, { 50, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Dwarf" ), gettext_noop( "Dwarves" ), 6, Race::SORC, 2, { 200, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Elf" ), gettext_noop( "Elves" ), 4, Race::SORC, 3, { 250, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Druid" ), gettext_noop( "Druids" ), 3, Race::SORC, 4, { 350, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Unicorn" ), gettext_noop( "Unicorns" ), 2, Race::SORC, 5, { 500, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Phoenix" ), gettext_noop( "Phoenixes" ), 1, Race::SORC, 6, { 1500, 0, 1, 0, 0, 0, 0 } },
                { gettext_noop( "Centaur" ), gettext_noop( "Centaurs" ), 8, Race::WRLK, 1, { 60, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Gargoyle" ), gettext_noop( "Gargoyles" ), 6, Race::WRLK, 2, { 200, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Griffin" ), gettext_noop( "Griffins" ), 4, Race::WRLK, 3, { 300, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Minotaur" ), gettext_noop( "Minotaurs" ), 3, Race::WRLK, 4, { 400, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Hydra" ), gettext_noop( "Hydras" ), 2, Race::WRLK, 5, { 800, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Green Dragon" ), gettext_noop( "Green Dragons" ), 1, Race::WRLK, 6, { 3000, 0, 0, 0, 1, 0, 0 } },
                { gettext_noop( "Rogue" ), gettext_noop( "Rogues" ), 8, Race::NONE, 1, { 50, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Nomad" ), gettext_noop( "Nomads" ), 4, Race::NONE, 2, { 200, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Ghost" ), gettext_noop( "Ghosts" ), 3, Race::NONE, 3, { 1000, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Genie" ), gettext_noop( "Genies" ), 2, Race::NONE, 6, { 650, 0, 0, 0, 0, 0, 1 } },
                { gettext_noop( "Medusa" ), gettext_noop( "Medusas" ), 5, Race::NONE, 4, { 500, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Earth Elemental" ), gettext_noop( "Earth Elementals" ), 4, Race::NONE, 4, { 500, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Air Elemental" ), gettext_noop( "Air Elementals" ), 4, Race::NONE, 4, { 500, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Fire Elemental" ), gettext_noop( "Fire Elementals" ), 4, Race::NONE, 4, { 500, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Water Elemental" ), gettext_noop( "Water Elementals" ), 4, Race::NONE, 4, { 500, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Random Monster" ), gettext_noop( "Random Monsters" ), 0, Race::NONE, 0, { 0, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Random Monster 1" ), gettext_noop( "Random Monsters 1" ), 0, Race::NONE, 1, { 0, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Random Monster 2" ), gettext_noop( "Random Monsters 2" ), 0, Race::NONE, 2, { 0, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Random Monster 3" ), gettext_noop( "Random Monsters 3" ), 0, Race::NONE, 3, { 0, 0, 0, 0, 0, 0, 0 } },
                { gettext_noop( "Random Monster 4" ), gettext_noop( "Random Monsters 4" ), 0, Race::NONE, 4, { 0, 0, 0, 0, 0, 0, 0 } } };

        monsterData.reserve( Monster::MONSTER_COUNT );

        for ( int i = 0; i < Monster::MONSTER_COUNT; ++i ) {
            monsterData.emplace_back( monsterIcnIds[i], binFileName[i], monsterSounds[i], monsterBattleStats[i], monsterGeneralStats[i] );
        }

        // Add monster abilities and weaknesses.
        monsterData[Monster::CAVALRY].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );

        monsterData[Monster::PALADIN].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_MELEE_ATTACK );

        monsterData[Monster::WOLF].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::WOLF].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_MELEE_ATTACK );

        monsterData[Monster::TROLL].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::HP_REGENERATION );

        monsterData[Monster::CYCLOPS].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::TWO_CELL_MELEE_ATTACK );
        monsterData[Monster::CYCLOPS].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::SPELL_CASTER, 20, Spell::PARALYZE );

        monsterData[Monster::SPRITE].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION );
        monsterData[Monster::SPRITE].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );

        monsterData[Monster::DWARF].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::MAGIC_RESISTANCE, 25, 0 );

        monsterData[Monster::ELF].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_SHOOTING );

        monsterData[Monster::UNICORN].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::UNICORN].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::SPELL_CASTER, 20, Spell::BLIND );

        monsterData[Monster::PHOENIX].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::PHOENIX].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::TWO_CELL_MELEE_ATTACK );
        monsterData[Monster::PHOENIX].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );
        monsterData[Monster::PHOENIX].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ELEMENTAL_SPELL_IMMUNITY );

        monsterData[Monster::CENTAUR].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );

        monsterData[Monster::GARGOYLE].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );

        monsterData[Monster::GRIFFIN].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::GRIFFIN].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );
        monsterData[Monster::GRIFFIN].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::UNLIMITED_RETALIATION );

        monsterData[Monster::HYDRA].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::HYDRA].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ALL_ADJACENT_CELL_MELEE_ATTACK );
        monsterData[Monster::HYDRA].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION );

        monsterData[Monster::GREEN_DRAGON].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DRAGON );
        monsterData[Monster::GREEN_DRAGON].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::GREEN_DRAGON].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );
        monsterData[Monster::GREEN_DRAGON].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::MAGIC_RESISTANCE, 100, 0 );
        monsterData[Monster::GREEN_DRAGON].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::TWO_CELL_MELEE_ATTACK );

        monsterData[Monster::ROGUE].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::NO_ENEMY_RETALIATION );

        monsterData[Monster::GHOST].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::SOUL_EATER );
        monsterData[Monster::GHOST].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );
        monsterData[Monster::GHOST].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::UNDEAD );

        monsterData[Monster::GENIE].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ENEMY_HALVING, 10, 0 );
        monsterData[Monster::GENIE].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FLYING );

        monsterData[Monster::MEDUSA].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );
        monsterData[Monster::MEDUSA].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::SPELL_CASTER, 20, Spell::PETRIFY );

        monsterData[Monster::NOMAD].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::DOUBLE_HEX_SIZE );

        monsterData[Monster::AIR_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ELEMENTAL );
        monsterData[Monster::AIR_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::AIR_CREATURE );
        monsterData[Monster::AIR_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL, 100, Spell::METEORSHOWER );
        monsterData[Monster::AIR_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::EXTRA_DAMAGE_FROM_CERTAIN_SPELL, 100,
                                                                               Spell::CHAINLIGHTNING );
        monsterData[Monster::AIR_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::EXTRA_DAMAGE_FROM_CERTAIN_SPELL, 100,
                                                                               Spell::ELEMENTALSTORM );
        monsterData[Monster::AIR_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::EXTRA_DAMAGE_FROM_CERTAIN_SPELL, 100,
                                                                               Spell::LIGHTNINGBOLT );
        monsterData[Monster::AIR_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_EARTH_CREATURES );

        monsterData[Monster::EARTH_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ELEMENTAL );
        monsterData[Monster::EARTH_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::EARTH_CREATURE );
        monsterData[Monster::EARTH_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL, 100, Spell::CHAINLIGHTNING );
        monsterData[Monster::EARTH_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL, 100, Spell::ELEMENTALSTORM );
        monsterData[Monster::EARTH_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL, 100, Spell::LIGHTNINGBOLT );
        monsterData[Monster::EARTH_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::EXTRA_DAMAGE_FROM_CERTAIN_SPELL, 100,
                                                                                 Spell::METEORSHOWER );
        monsterData[Monster::EARTH_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_AIR_CREATURES );

        monsterData[Monster::FIRE_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ELEMENTAL );
        monsterData[Monster::FIRE_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FIRE_CREATURE );
        monsterData[Monster::FIRE_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::FIRE_SPELL_IMMUNITY );
        monsterData[Monster::FIRE_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_COLD_SPELLS );
        monsterData[Monster::FIRE_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_WATER_CREATURES );

        monsterData[Monster::WATER_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::ELEMENTAL );
        monsterData[Monster::WATER_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::WATER_CREATURE );
        monsterData[Monster::WATER_ELEMENT].battleStats.abilities.emplace_back( fheroes2::MonsterAbilityType::COLD_SPELL_IMMUNITY );
        monsterData[Monster::WATER_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_FIRE_SPELLS );
        monsterData[Monster::WATER_ELEMENT].battleStats.weaknesses.emplace_back( fheroes2::MonsterWeaknessType::DOUBLE_DAMAGE_FROM_FIRE_CREATURES );

        // Calculate base value of monster strength.
        for ( fheroes2::MonsterData & data : monsterData ) {
            data.battleStats.monsterBaseStrength = getMonsterBaseStrength( data );
        }

        // TODO: verify that no duplicates of abilities and weaknesses exist.
    }

    void removeDuplicateSpell( std::set<int> & sortedSpellIds, const int massSpellId, const int spellId )
    {
        if ( sortedSpellIds.count( massSpellId ) > 0 && sortedSpellIds.count( spellId ) > 0 ) {
            sortedSpellIds.erase( massSpellId );
        }
    }

    std::vector<int> replaceMassSpells( const std::vector<int> & spellIds )
    {
        std::set<int> sortedSpellIds( spellIds.begin(), spellIds.end() );

        removeDuplicateSpell( sortedSpellIds, Spell::CHAINLIGHTNING, Spell::LIGHTNINGBOLT );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSCURE, Spell::CURE );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSCURSE, Spell::CURSE );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSBLESS, Spell::BLESS );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSHASTE, Spell::HASTE );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSSHIELD, Spell::SHIELD );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSDISPEL, Spell::DISPEL );
        removeDuplicateSpell( sortedSpellIds, Spell::MASSSLOW, Spell::SLOW );

        return std::vector<int>( sortedSpellIds.begin(), sortedSpellIds.end() );
    }
}

namespace fheroes2
{
    const MonsterData & getMonsterData( const int monsterId )
    {
        if ( monsterData.empty() ) {
            populateMonsterData();
        }

        assert( monsterId >= 0 && static_cast<size_t>( monsterId ) < monsterData.size() );
        if ( monsterId < 0 || static_cast<size_t>( monsterId ) >= monsterData.size() ) {
            return monsterData.front();
        }

        return monsterData[monsterId];
    }

    std::string getMonsterAbilityDescription( const MonsterAbility & ability, const bool ignoreBasicAbilities )
    {
        switch ( ability.type ) {
        case MonsterAbilityType::NONE:
            return ignoreBasicAbilities ? "" : _( "None" );
        case MonsterAbilityType::DOUBLE_SHOOTING:
            return _( "Double shot" );
        case MonsterAbilityType::DOUBLE_HEX_SIZE:
            return ignoreBasicAbilities ? "" : _( "2-hex monster" );
        case MonsterAbilityType::DOUBLE_MELEE_ATTACK:
            return _( "Double strike" );
        case MonsterAbilityType::DOUBLE_DAMAGE_TO_UNDEAD:
            return _( "Double damage to Undead" );
        case MonsterAbilityType::MAGIC_RESISTANCE:
            return std::to_string( ability.percentage ) + _( "% magic resistance" );
        case MonsterAbilityType::MIND_SPELL_IMMUNITY:
            return _( "Immune to Mind spells" );
        case MonsterAbilityType::ELEMENTAL_SPELL_IMMUNITY:
            return _( "Immune to Elemental spells" );
        case MonsterAbilityType::FIRE_SPELL_IMMUNITY:
            return _( "Immune to Fire spells" );
        case MonsterAbilityType::COLD_SPELL_IMMUNITY:
            return _( "Immune to Cold spells" );
        case MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL:
            if ( ability.percentage == 100 ) {
                return std::string( _( "Immune to " ) ) + Spell( ability.value ).GetName();
            }
            else {
                std::string str = _( "% immunity to %{spell} spell" );
                StringReplace( str, "%{spell}", Spell( ability.value ).GetName() );
                return std::to_string( ability.percentage ) + str;
            }
        case MonsterAbilityType::ELEMENTAL_SPELL_DAMAGE_REDUCTION:
            return std::to_string( ability.percentage ) + _( "% damage from Elemental spells" );
        case MonsterAbilityType::CERTAIN_SPELL_DAMAGE_REDUCTION: {
            std::string str = _( "% damage from %{spell} spell" );
            StringReplace( str, "%{spell}", Spell( ability.value ).GetName() );
            return std::to_string( ability.percentage ) + str;
        }
        case MonsterAbilityType::SPELL_CASTER:
            if ( ability.value == Spell::DISPEL ) {
                return std::to_string( ability.percentage ) + _( "% chance to Dispel beneficial spells" );
            }
            else if ( ability.value == Spell::PARALYZE ) {
                return std::to_string( ability.percentage ) + _( "% chance to Paralyze" );
            }
            else if ( ability.value == Spell::PETRIFY ) {
                return std::to_string( ability.percentage ) + _( "% chance to Petrify" );
            }
            else if ( ability.value == Spell::BLIND ) {
                return std::to_string( ability.percentage ) + _( "% chance to Blind" );
            }
            else if ( ability.value == Spell::CURSE ) {
                return std::to_string( ability.percentage ) + _( "% chance to Curse" );
            }
            else {
                std::string str = _( "% chance to cast %{spell} spell" );
                StringReplace( str, "%{spell}", Spell( ability.value ).GetName() );
                return std::to_string( ability.percentage ) + str;
            }
        case MonsterAbilityType::HP_REGENERATION:
            return _( "HP regeneration" );
        case MonsterAbilityType::TWO_CELL_MELEE_ATTACK:
            return _( "Two hexes attack" );
        case MonsterAbilityType::FLYING:
            return ignoreBasicAbilities ? "" : _( "Flyer" );
        case MonsterAbilityType::UNLIMITED_RETALIATION:
            return _( "Unlimited retaliation" );
        case MonsterAbilityType::ALL_ADJACENT_CELL_MELEE_ATTACK:
            return _( "Attacks all adjacent enemies" );
        case MonsterAbilityType::NO_MELEE_PENALTY:
            return _( "No melee penalty" );
        case MonsterAbilityType::DRAGON:
            return ignoreBasicAbilities ? "" : _( "Dragon" );
        case MonsterAbilityType::UNDEAD:
            return _( "Undead" );
        case MonsterAbilityType::NO_ENEMY_RETALIATION:
            return _( "No enemy retaliation" );
        case MonsterAbilityType::HP_DRAIN:
            return _( "HP drain" );
        case MonsterAbilityType::AREA_SHOT:
            return _( "Cloud attack" );
        case MonsterAbilityType::MORAL_DECREMENT:
            return _( "Decreases enemy's morale by " ) + std::to_string( ability.value );
        case MonsterAbilityType::ENEMY_HALVING:
            return std::to_string( ability.percentage ) + _( "% chance to halve enemy" );
        case MonsterAbilityType::SOUL_EATER:
            return _( "Soul Eater" );
        case MonsterAbilityType::ELEMENTAL:
            return ignoreBasicAbilities ? _( "No Morale" ) : _( "Elemental" );
        case MonsterAbilityType::EARTH_CREATURE:
            return ignoreBasicAbilities ? "" : _( "Earth creature" );
        case MonsterAbilityType::AIR_CREATURE:
            return ignoreBasicAbilities ? "" : _( "Air creature" );
        case MonsterAbilityType::FIRE_CREATURE:
            return ignoreBasicAbilities ? "" : _( "Fire creature" );
        case MonsterAbilityType::WATER_CREATURE:
            return ignoreBasicAbilities ? "" : _( "Water creature" );
        default:
            break;
        }

        assert( 0 ); // Did you add a new ability? Please add the implementation!
        return "";
    }

    std::string getMonsterWeaknessDescription( const MonsterWeakness & weakness, const bool ignoreBasicWeaknesses )
    {
        switch ( weakness.type ) {
        case MonsterWeaknessType::NONE:
            return ignoreBasicWeaknesses ? "" : _( "None" );
        case MonsterWeaknessType::DOUBLE_DAMAGE_FROM_FIRE_SPELLS:
            return _( "Double damage from Fire spells" );
        case MonsterWeaknessType::DOUBLE_DAMAGE_FROM_COLD_SPELLS:
            return _( "Double damage from Cold spells" );
        case MonsterWeaknessType::EXTRA_DAMAGE_FROM_CERTAIN_SPELL: {
            std::string str = _( "% damage from %{spell} spell" );
            StringReplace( str, "%{spell}", Spell( weakness.value ).GetName() );
            return std::to_string( weakness.percentage + 100 ) + str;
        }
        case MonsterWeaknessType::DOUBLE_DAMAGE_FROM_EARTH_CREATURES:
            return ignoreBasicWeaknesses ? "" : _( "Double damage from Earth creatures" );
        case MonsterWeaknessType::DOUBLE_DAMAGE_FROM_AIR_CREATURES:
            return ignoreBasicWeaknesses ? "" : _( "Double damage from Air creatures" );
        case MonsterWeaknessType::DOUBLE_DAMAGE_FROM_FIRE_CREATURES:
            return ignoreBasicWeaknesses ? "" : _( "Double damage from Fire creatures" );
        case MonsterWeaknessType::DOUBLE_DAMAGE_FROM_WATER_CREATURES:
            return ignoreBasicWeaknesses ? "" : _( "Double damage from Water creatures" );
        default:
            break;
        }

        assert( 0 ); // Did you add a new weakness? Please add the implementation!
        return "";
    }

    std::string getMonsterDescription( const int monsterId )
    {
        if ( monsterData.empty() ) {
            populateMonsterData();
        }

        assert( monsterId >= 0 && static_cast<size_t>( monsterId ) < monsterData.size() );
        if ( monsterId < 0 || static_cast<size_t>( monsterId ) >= monsterData.size() ) {
            return "";
        }

        const MonsterData & monster = monsterData[monsterId];

        std::ostringstream os;
        os << "----------" << std::endl;
        os << "Name: " << monster.generalStats.untranslatedName << std::endl;
        os << "Plural name: " << monster.generalStats.untranslatedPluralName << std::endl;
        os << "Base growth: " << monster.generalStats.baseGrowth << std::endl;
        os << "Race: " << Race::String( monster.generalStats.race ) << std::endl;
        os << "Level: " << monster.generalStats.level << std::endl;
        os << "Cost: " << Funds( monster.generalStats.cost ).String() << std::endl;
        os << std::endl;
        os << "Attack: " << monster.battleStats.attack << std::endl;
        os << "Defense: " << monster.battleStats.defense << std::endl;
        os << "Min damage: " << monster.battleStats.damageMin << std::endl;
        os << "Max damage: " << monster.battleStats.damageMax << std::endl;
        os << "Hit Points: " << monster.battleStats.hp << std::endl;
        os << "Speed: " << Speed::String( monster.battleStats.speed ) << std::endl;
        os << "Number of shots: " << monster.battleStats.shots << std::endl;
        if ( !monster.battleStats.abilities.empty() ) {
            os << std::endl;
            os << "Abilities:" << std::endl;
            for ( const MonsterAbility & ability : monster.battleStats.abilities ) {
                os << "   " << getMonsterAbilityDescription( ability, false ) << std::endl;
            }
        }

        if ( !monster.battleStats.weaknesses.empty() ) {
            os << std::endl;
            os << "Weaknesses:" << std::endl;
            for ( const MonsterWeakness & weakness : monster.battleStats.weaknesses ) {
                os << "   " << getMonsterWeaknessDescription( weakness, false ) << std::endl;
            }
        }

        return os.str();
    }

    std::vector<std::string> getMonsterPropertiesDescription( const int monsterId )
    {
        std::vector<std::string> output;

        const auto listSpells = []( const std::vector<int> & sortedSpells ) {
            std::string result;

            for ( size_t i = 0; i < sortedSpells.size(); ++i ) {
                if ( i > 0 ) {
                    result += ", ";
                }

                if ( sortedSpells[i] == Spell::LIGHTNINGBOLT ) {
                    result += _( "Lightning" );
                }
                else {
                    result += Spell( sortedSpells[i] ).GetName();
                }
            }

            result += '.';

            return result;
        };

        const MonsterBattleStats & battleStats = getMonsterData( monsterId ).battleStats;

        {
            std::map<uint32_t, std::vector<int>> immuneToSpells;
            std::map<uint32_t, std::vector<int>> reducedDamageFromSpells;

            for ( const MonsterAbility & ability : battleStats.abilities ) {
                if ( ability.type == MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL ) {
                    immuneToSpells[ability.percentage].emplace_back( ability.value );
                    continue;
                }

                if ( ability.type == MonsterAbilityType::CERTAIN_SPELL_DAMAGE_REDUCTION ) {
                    reducedDamageFromSpells[ability.percentage].emplace_back( ability.value );
                    continue;
                }

                if ( const std::string description = getMonsterAbilityDescription( ability, true ); !description.empty() ) {
                    output.emplace_back( description + '.' );
                }
            }

            for ( const auto & [percentage, spells] : immuneToSpells ) {
                assert( !spells.empty() );

                output.emplace_back( ( percentage == 100 ? _( "Immune to " ) : std::to_string( percentage ) + _( "% immunity to " ) )
                                     + listSpells( replaceMassSpells( spells ) ) );
            }

            for ( const auto & [percentage, spells] : reducedDamageFromSpells ) {
                assert( !spells.empty() );

                output.emplace_back( std::to_string( percentage ) + _( "% damage from " ) + listSpells( replaceMassSpells( spells ) ) );
            }
        }

        {
            std::map<uint32_t, std::vector<int>> extraDamageFromSpells;

            for ( const MonsterWeakness & weakness : battleStats.weaknesses ) {
                if ( weakness.type == MonsterWeaknessType::EXTRA_DAMAGE_FROM_CERTAIN_SPELL ) {
                    extraDamageFromSpells[weakness.percentage].emplace_back( weakness.value );
                    continue;
                }

                if ( const std::string description = getMonsterWeaknessDescription( weakness, true ); !description.empty() ) {
                    output.emplace_back( description + '.' );
                }
            }

            for ( const auto & [percentage, spells] : extraDamageFromSpells ) {
                assert( !spells.empty() );

                output.emplace_back( std::to_string( percentage + 100 ) + _( "% damage from " ) + listSpells( replaceMassSpells( spells ) ) );
            }
        }

        return output;
    }

    uint32_t getSpellResistance( const int monsterId, const int spellId )
    {
        const std::vector<MonsterAbility> & abilities = getMonsterData( monsterId ).battleStats.abilities;

        Spell spell( spellId );

        if ( spell.isMindInfluence() ) {
            if ( std::find( abilities.begin(), abilities.end(), MonsterAbilityType::MIND_SPELL_IMMUNITY ) != abilities.end() ) {
                return 100;
            }

            if ( std::find( abilities.begin(), abilities.end(), MonsterAbilityType::UNDEAD ) != abilities.end() ) {
                return 100;
            }

            if ( std::find( abilities.begin(), abilities.end(), MonsterAbilityType::ELEMENTAL ) != abilities.end() ) {
                return 100;
            }
        }

        if ( spell.isAliveOnly() && std::find( abilities.begin(), abilities.end(), MonsterAbilityType::UNDEAD ) != abilities.end() ) {
            return 100;
        }

        if ( spell.isUndeadOnly() && std::find( abilities.begin(), abilities.end(), MonsterAbilityType::UNDEAD ) == abilities.end() ) {
            return 100;
        }

        if ( spell.isCold() && std::find( abilities.begin(), abilities.end(), MonsterAbilityType::COLD_SPELL_IMMUNITY ) != abilities.end() ) {
            return 100;
        }

        if ( spell.isFire() && std::find( abilities.begin(), abilities.end(), MonsterAbilityType::FIRE_SPELL_IMMUNITY ) != abilities.end() ) {
            return 100;
        }

        if ( spell.isElementalSpell() && std::find( abilities.begin(), abilities.end(), MonsterAbilityType::ELEMENTAL_SPELL_IMMUNITY ) != abilities.end() ) {
            return 100;
        }

        uint32_t spellResistance = 0;

        // Find magic immunity for every spell.
        if ( const auto abilityIter = std::find( abilities.begin(), abilities.end(), MonsterAbilityType::MAGIC_RESISTANCE ); abilityIter != abilities.end() ) {
            if ( abilityIter->percentage == 100 ) {
                // Immune to everything.
                return 100;
            }
            if ( spell.isDamage() || spell.isApplyToEnemies() ) {
                spellResistance = abilityIter->percentage;
            }
        }

        for ( const MonsterAbility & ability : abilities ) {
            if ( ability.type == MonsterAbilityType::IMMUNE_TO_CERTAIN_SPELL && static_cast<int>( ability.value ) == spellId ) {
                spellResistance = std::max( spellResistance, ability.percentage );
            }
        }

        return spellResistance;
    }

    bool isAbilityPresent( const std::vector<MonsterAbility> & abilities, const MonsterAbilityType abilityType )
    {
        return std::find( abilities.begin(), abilities.end(), abilityType ) != abilities.end();
    }

    bool isWeaknessPresent( const std::vector<MonsterWeakness> & weaknesses, const MonsterWeaknessType weaknessType )
    {
        return std::find( weaknesses.begin(), weaknesses.end(), weaknessType ) != weaknesses.end();
    }
}
