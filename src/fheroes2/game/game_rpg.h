#pragma once

#include <cstdint>
#include <string>

enum class PlayerColor : uint8_t;

namespace fheroes2::RPG
{
    enum class ExperienceKind : uint8_t
    {
        HERO,
        OFFLINE,
        BATTLE,
        ADVENTURE
    };

    // The local player's kingdom owns the persistent profile. Enemy profiles only live for this map.
    std::string dataDirectory();
    void beginMap( PlayerColor playerColor );
    void endMap();
    uint64_t addExperience( PlayerColor color, uint64_t amount, ExperienceKind kind = ExperienceKind::HERO );
    void awardAdventureAction( PlayerColor color, int objectType, int32_t tileIndex );
    void awardBattle( PlayerColor color, PlayerColor opponent, uint32_t battleExperience, bool won, bool defending, bool siege );

    // Persistent RPG combat affixes used by battle stacks.
    uint32_t creatureAttackBonus( PlayerColor color );
    uint32_t creatureDefenseBonus( PlayerColor color );
    int moraleBonus( PlayerColor color );
    int luckBonus( PlayerColor color );
    double lifeStealPercent( PlayerColor color );
    double killHealPercent( PlayerColor color );
    double regenerationPercent( PlayerColor color );
    uint32_t criticalChance( PlayerColor color );
    double criticalDamageBonusPercent( PlayerColor color );
    uint32_t evasionChance( PlayerColor color );
    double rangedMeleePenaltyRecoveryPercent( PlayerColor color );

    double damageMultiplier( PlayerColor attacker, PlayerColor defender, bool ranged, bool attackerOutnumbered, bool defenderOutnumbered,
                             bool attackerFullHealth, bool defenderFullHealth, bool attackerBelowHalf, bool defenderBelowHalf );
    double spellMultiplier( PlayerColor attacker, PlayerColor defender, int spellId );
    std::string formatExperience( uint64_t value );
    void showMenu();
}
