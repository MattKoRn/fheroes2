#pragma once

#include <cstdint>
#include <string>

enum class PlayerColor : uint8_t;

namespace fheroes2::RPG
{
    // Keep the numeric values stable. ExperienceKind crosses several RPG award paths and explicit
    // values prevent a future enum insertion/reordering from silently changing an existing kind.
    enum class ExperienceKind : uint8_t
    {
        HERO = 0,
        OFFLINE = 1,
        BATTLE = 2,
        ADVENTURE = 3
    };

    // The local player's kingdom owns the persistent profile. Enemy profiles only live for this map.
    [[nodiscard]] std::string dataDirectory();
    void beginMap( PlayerColor playerColor );
    void endMap();
    [[nodiscard]] uint64_t addExperience( PlayerColor color, uint64_t amount, ExperienceKind kind = ExperienceKind::HERO );
    void awardAdventureAction( PlayerColor color, int objectType, int32_t tileIndex );
    void awardBattle( PlayerColor color, PlayerColor opponent, uint32_t battleExperience, bool won, bool defending, bool siege );

    // Persistent RPG combat affixes used by battle stacks. These accessors are intentionally
    // nodiscard: silently dropping a modifier is almost always a gameplay integration bug.
    [[nodiscard]] uint32_t creatureAttackBonus( PlayerColor color );
    [[nodiscard]] uint32_t creatureDefenseBonus( PlayerColor color );
    [[nodiscard]] int moraleBonus( PlayerColor color );
    [[nodiscard]] int luckBonus( PlayerColor color );
    [[nodiscard]] double lifeStealPercent( PlayerColor color );
    [[nodiscard]] double killHealPercent( PlayerColor color );
    [[nodiscard]] double regenerationPercent( PlayerColor color );
    [[nodiscard]] double criticalChance( PlayerColor color );
    [[nodiscard]] double criticalDamageBonusPercent( PlayerColor color );
    [[nodiscard]] double evasionChance( PlayerColor color );
    [[nodiscard]] double rangedMeleePenaltyRecoveryPercent( PlayerColor color );

    [[nodiscard]] double damageMultiplier( PlayerColor attacker, PlayerColor defender, bool ranged, bool attackerOutnumbered, bool defenderOutnumbered,
                                           bool attackerFullHealth, bool defenderFullHealth, bool attackerBelowHalf, bool defenderBelowHalf );
    [[nodiscard]] double spellMultiplier( PlayerColor attacker, PlayerColor defender, int spellId );
    [[nodiscard]] std::string formatExperience( uint64_t value );
    void showMenu();
}
