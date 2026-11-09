#include <H1/Ints.h>

#include <SOURCE/army.h>
#include <SOURCE/armyGroup.h>
#include <SOURCE/combatManager.h>
#include <SOURCE/combatTypes.h>
#include <SOURCE/hero.h>
#include <SOURCE/KB.h>
#include <SOURCE/PATH.h>
#include <SOURCE/philAI.h>

#include <stdlib.h>

static const float SPELL_AI_SLOW_MODIFIER = -0.11f;
static const float SPELL_AI_BLIND_MODIFIER = -0.6f;
static const float SPELL_AI_CURSE_MODIFIER = -0.18f;
static const float SPELL_AI_BERSERK_MODIFIER = -0.6f;
static const float SPELL_AI_PARALYZE_MODIFIER = -0.7f;
static const float SPELL_AI_HASTE_MODIFIER = 0.33f;
static const float SPELL_AI_BLESS_MODIFIER = 0.18f;
static const float SPELL_AI_PROTECTION_MODIFIER = 0.24f;
static const float SPELL_AI_ANTI_MAGIC_MODIFIER = 0.15f;

i32 gSpellAIEffectShift;

b32 combatManager::DoSpellAI(i8 side) {
    i32 chosenSpell;
    i32 bestValue;
    i32 effect;
    i32 slot;
    i32 bestHex;
    i32 hex;

    bestValue = 0;
    chosenSpell = SPELL_NONE;
    bestHex = -1;
    if (m_heroes[side] == NULL)
        return false;
    if (m_heroes[side]->m_primaryStats[HERO_PRIMARY_SPELL_POWER] == HERO_SPELL_POWER_ONE)
        gSpellAIEffectShift = 2;
    else if (m_heroes[side]->m_primaryStats[HERO_PRIMARY_SPELL_POWER]
             == HERO_SPELL_POWER_TWO)
        gSpellAIEffectShift = 1;
    else
        gSpellAIEffectShift = 0;
    for (slot = 0; slot < HERO_SPELL_SLOT_COUNT; slot++) {
        const i8 spell = m_heroes[side]->m_spells[slot];
        if (spell >= SPELL_FIRST && spell < SPELL_COUNT
            && (gSpellAIFlags[spell] & SPELL_AI_FLAG_COMBAT)
            && m_heroes[side]->m_spellCharges[slot] > 0) {
            DetermineEffectOfSpell(spell, &effect, &hex);
            if (effect > bestValue) {
                bestValue = effect;
                chosenSpell = spell;
                bestHex = hex;
            }
        }
    }
    if (bestValue > 0) {
        gNextAction = ACTION_CAST_SPELL;
        gNextActionExtra = chosenSpell;
        gNextActionGridIndex = bestHex;
        return true;
    }
    return false;
}

void combatManager::DetermineEffectOfSpell(
    i32 spell,
    i32* bestEffect,
    i32* bestHex
) {
    i32 spellMode;
    *bestEffect = 0;
    *bestHex = ARMY_HEX_INVALID;
    switch (spell) {
        case SPELL_CURE:
        case SPELL_DISPEL_MAGIC:
        case SPELL_ARMAGEDDON:
        case SPELL_STORM:
            spellMode = SPELL_AI_GLOBAL;
            break;
        case SPELL_FIREBALL:
        case SPELL_METEOR_SHOWER:
            spellMode = SPELL_AI_AREA;
            break;
        case SPELL_RESURRECT:
        case SPELL_HASTE:
        case SPELL_BLESS:
        case SPELL_PROTECTION:
        case SPELL_ANTI_MAGIC:
            spellMode = SPELL_AI_FRIENDLY;
            break;
        case SPELL_LIGHTNING_BOLT:
        case SPELL_SLOW:
        case SPELL_BLIND:
        case SPELL_CURSE:
        case SPELL_TURN_UNDEAD:
        case SPELL_BERZERKER:
        case SPELL_PARALYZE:
            spellMode = SPELL_AI_ENEMY;
            break;
        default:
            return;
    }
    const i32 candidateCount = spellMode == SPELL_AI_GLOBAL ? 1 : COMBAT_HEX_COUNT;
    for (i32 hex = 0; hex < candidateCount; ++hex) {
        army* targetCreature = nullptr;
        if (spellMode == SPELL_AI_FRIENDLY || spellMode == SPELL_AI_ENEMY) {
            const i32 side = spellMode == SPELL_AI_FRIENDLY ? m_currentSide
                                                          : COMBAT_OPPOSING_SIDE(m_currentSide);
            const hexcell& cell = m_hexCells[hex];
            if (cell.m_occupantSide != side || cell.m_occupantIndex < 0
                || cell.m_occupantIndex >= m_numArmies[side])
                continue;
            targetCreature = &m_armies[side][cell.m_occupantIndex];
            if (targetCreature->m_quantity <= 0)
                continue;
        }
        if (spellMode != SPELL_AI_GLOBAL && !ValidSpellTarget(spell, hex))
            continue;

        i32 effect = 0;
        switch (spell) {
            case SPELL_CURE:
                EffectSpellCure(&effect, m_currentSide, true);
                break;
            case SPELL_DISPEL_MAGIC:
                EffectSpellCure(&effect, COMBAT_SIDE_ANY, false);
                break;
            case SPELL_RESURRECT:
                EffectSpellResurrect(&effect, hex);
                break;
            case SPELL_ARMAGEDDON:
            case SPELL_STORM:
            case SPELL_FIREBALL:
            case SPELL_METEOR_SHOWER:
            case SPELL_LIGHTNING_BOLT:
                EffectSpellDamage(&effect, spell, hex);
                break;
            case SPELL_HASTE:
            case SPELL_BLESS:
            case SPELL_PROTECTION:
            case SPELL_ANTI_MAGIC:
                if (spell == SPELL_ANTI_MAGIC
                    && m_heroes[COMBAT_OPPOSING_SIDE(m_currentSide)] == NULL)
                    effect = 0;
                else
                    effect = RawEffectSpellInfluence(targetCreature, spell) >> gSpellAIEffectShift;
                if (targetCreature->m_spellEffect >= SPELL_FIRST)
                    effect -=
                        RawEffectSpellInfluence(targetCreature, targetCreature->m_spellEffect);
                break;
            case SPELL_SLOW:
            case SPELL_BLIND:
            case SPELL_CURSE:
            case SPELL_BERZERKER:
            case SPELL_PARALYZE:
                effect = -(RawEffectSpellInfluence(targetCreature, spell) >> gSpellAIEffectShift);
                if (targetCreature->m_spellEffect >= SPELL_FIRST)
                    effect +=
                        RawEffectSpellInfluence(targetCreature, targetCreature->m_spellEffect);
                break;
            case SPELL_TURN_UNDEAD:
                if (targetCreature->m_creatureType == CREATURE_GHOST)
                    effect = targetCreature->m_quantity
                             * gMonsterDatabase[targetCreature->m_creatureType].fightValue;
                else
                    effect = 0;
                break;
            default:
                *bestEffect = 0;
                return;
        }
        if (effect > *bestEffect) {
            *bestEffect = effect;
            *bestHex = hex;
        }
    }
}

i32 combatManager::RawEffectSpellInfluence(army* target, i32 spell) {
    i32 worth;
    i32 effect;

    effect = 0;
    worth = target->m_quantity * gMonsterDatabase[target->m_creatureType].fightValue;
    switch (spell) {
        case SPELL_SLOW:
            if (target->m_stats.attributes & MONSTER_FLAGS_SHOOTER)
                effect = 0;
            else if (target->m_stats.attributes & MONSTER_FLAGS_FLYING)
                effect = worth * SPELL_AI_SLOW_MODIFIER * 3.0f;
            else
                effect = worth * SPELL_AI_SLOW_MODIFIER * (target->m_stats.speed - 1);
            break;
        case SPELL_BLIND:
            effect = worth * SPELL_AI_BLIND_MODIFIER;
            break;
        case SPELL_CURSE:
            effect = worth * SPELL_AI_CURSE_MODIFIER;
            break;
        case SPELL_BERZERKER:
            effect = worth * SPELL_AI_BERSERK_MODIFIER;
            break;
        case SPELL_PARALYZE:
            effect = worth * SPELL_AI_PARALYZE_MODIFIER;
            break;
        case SPELL_HASTE:
            if (target->m_stats.attributes & MONSTER_FLAGS_FLYING)
                effect = 0;
            else if (target->m_stats.attributes & MONSTER_FLAGS_SHOOTER)
                effect = 0;
            else if (target->m_stats.speed < CREATURE_SPEED_SLOW_END)
                effect = worth * SPELL_AI_HASTE_MODIFIER;
            else if (target->m_stats.speed
                     < CREATURE_SPEED_MEDIUM_END)
                effect = worth * SPELL_AI_HASTE_MODIFIER / 2.0f;
            else
                effect = 0;
            break;
        case SPELL_BLESS:
            effect = worth * SPELL_AI_BLESS_MODIFIER;
            break;
        case SPELL_PROTECTION:
            effect = worth * SPELL_AI_PROTECTION_MODIFIER;
            break;
        case SPELL_ANTI_MAGIC:
            effect = worth * SPELL_AI_ANTI_MAGIC_MODIFIER;
            break;
        default:
            effect = 0;
            break;
    }
    if (ARMY_IGNORES_SPELLS(target))
        effect = 0;
    else if (target->m_creatureType == CREATURE_DWARF && effect < 0)
        effect = effect * 0.75;
    return effect;
}

void combatManager::ClearEffects(void) {
    i32 side;
    i32 armyIndex;
    for (side = COMBAT_SIDE_FIRST; side < COMBAT_SIDE_COUNT; ++side) {
        for (armyIndex = 0; armyIndex < ARMY_GROUP_SLOT_COUNT; ++armyIndex)
            gArmyEffected[side][armyIndex] = false;
    }
}

void combatManager::EffectSpellCure(
    i32* effect,
    i32 targetSide,
    b8 cureOnly
) {
    i32 sideIndex;
    i32 negTotal;
    i32 stackNum;
    army* creature;
    i32 posTotal;
    b32 done;
    i32 armyWorth;

    *effect = 0;
    done = false;
    if (targetSide == COMBAT_SIDE_ANY)
        sideIndex = m_currentSide;
    else
        sideIndex = targetSide;
    while (!done) {
        negTotal = 0;
        posTotal = 0;
        for (stackNum = 0; stackNum < ARMY_GROUP_SLOT_COUNT; stackNum++) {
            if (m_armies[sideIndex][stackNum].IsAlive()) {
                creature = &m_armies[sideIndex][stackNum];
                armyWorth =
                    creature->m_quantity * gMonsterDatabase[creature->m_creatureType].fightValue;
                switch (creature->m_spellEffect) {
                    case SPELL_SLOW:
                    case SPELL_BLIND:
                    case SPELL_CURSE:
                    case SPELL_BERZERKER:
                    case SPELL_PARALYZE:
                        negTotal += -RawEffectSpellInfluence(creature, creature->m_spellEffect);
                        break;
                    case SPELL_HASTE:
                    case SPELL_BLESS:
                    case SPELL_PROTECTION:
                    case SPELL_ANTI_MAGIC:
                        posTotal += RawEffectSpellInfluence(creature, creature->m_spellEffect);
                        break;
                }
            }
        }
        if (cureOnly == true)
            posTotal = 0;
        if (targetSide == COMBAT_SIDE_ANY) {
            if (sideIndex == m_currentSide)
                *effect += negTotal - posTotal;
            else
                *effect += posTotal - negTotal;
        } else
            *effect += negTotal;
        if (targetSide == COMBAT_SIDE_ANY && sideIndex == m_currentSide)
            sideIndex = COMBAT_OPPOSING_SIDE(m_currentSide);
        else
            done = true;
    }
}

void combatManager::EffectSpellResurrect(i32* effect, i32 hex) {
    army* target;
    i32 resurrectPower;
    i32 count;

    target = &m_armies[m_hexCells[hex].m_occupantSide][m_hexCells[hex].m_occupantIndex];
    if (ARMY_IGNORES_SPELLS(target)) {
        *effect = 0;
        return;
    }
    count = m_heroes[m_currentSide]->m_primaryStats[HERO_PRIMARY_SPELL_POWER] * 50
            / target->m_stats.hitPoints;
    if (count + target->m_quantity > target->m_initialQuantity)
        count = target->m_initialQuantity - target->m_quantity;
    *effect = count * gMonsterDatabase[target->m_creatureType].fightValue;
}

void combatManager::EffectSpellDamage(
    i32* effect,
    i32 spell,
    i32 targetHex
) {
    const i32 damage = SpellDamage(spell);
    const bool global = spell == SPELL_ARMAGEDDON || spell == SPELL_STORM;
    const bool area = spell == SPELL_FIREBALL || spell == SPELL_METEOR_SHOWER;
    bool affected[COMBAT_SIDE_COUNT][COMBAT_SIDE_ARMY_COUNT] = {};
    if (!global) {
        const auto markTarget = [&](i32 hex) {
            if (!ValidHex(hex))
                return;
            const hexcell& cell = m_hexCells[hex];
            if (cell.m_occupantSide >= COMBAT_SIDE_FIRST && cell.m_occupantIndex >= 0)
                affected[cell.m_occupantSide][cell.m_occupantIndex] = true;
        };
        markTarget(targetHex);
        if (area) {
            for (i16 direction = 0; direction < COMBAT_DIRECTION_ADJACENT_COUNT; ++direction)
                markTarget(GetAdjacentCellIndexNoArmy(targetHex, direction));
        }
    }

    i32 living[COMBAT_SIDE_COUNT] = {};
    i32 possibleKills[COMBAT_SIDE_COUNT] = {};
    i32 guaranteedKills[COMBAT_SIDE_COUNT] = {};
    i32 damageValue[COMBAT_SIDE_COUNT] = {};
    for (i32 side = COMBAT_SIDE_FIRST; side < COMBAT_SIDE_COUNT; ++side) {
        for (i32 index = 0; index < m_numArmies[side]; ++index) {
            const army& stack = m_armies[side][index];
            if (stack.m_quantity <= 0 || (stack.m_stats.attributes & MONSTER_FLAGS_DEAD))
                continue;
            ++living[side];
            if ((!global && !affected[side][index]) || ARMY_IGNORES_SPELLS(&stack)
                || (area && stack.m_powFrames != ARMY_POW_NONE) || stack.m_stats.hitPoints <= 0)
                continue;

            const bool dwarf = stack.m_creatureType == CREATURE_DWARF;
            const i32 killed = stack.CasualtiesFromDamage(damage);
            const bool wipesStack = killed == stack.m_quantity;
            if (wipesStack) {
                ++possibleKills[side];
                if (!dwarf)
                    ++guaranteedKills[side];
            }

            const i32 hitPoints = stack.m_stats.hitPoints;
            const i32 newDamage = wipesStack ? 0 : damage - killed * hitPoints;
            const i32 value = gMonsterDatabase[stack.m_creatureType].fightValue;
            i32 score = killed * value + newDamage * 3 * value / (4 * hitPoints);
            if (dwarf)
                score = spell == SPELL_LIGHTNING_BOLT ? score * 4 / 5 : score * 3 / 4;
            damageValue[side] += score;
        }
    }

    const i32 own = m_currentSide;
    const i32 enemy = COMBAT_OPPOSING_SIDE(own);
    if (living[own] > 0 && possibleKills[own] == living[own])
        *effect = 0;
    else if (living[enemy] > 0 && guaranteedKills[enemy] == living[enemy])
        *effect = 100000000 - damageValue[own] - gSpellAIValue[spell];
    else
        *effect = damageValue[enemy] - damageValue[own];
}
