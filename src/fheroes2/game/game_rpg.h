#pragma once

#include <cstddef>
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

    // These IDs are part of the RPG progression contract. Keep this guard next to the enum so a
    // future refactor cannot accidentally renumber an award source while still compiling cleanly.
    static_assert( static_cast<uint8_t>( ExperienceKind::HERO ) == 0 );
    static_assert( static_cast<uint8_t>( ExperienceKind::OFFLINE ) == 1 );
    static_assert( static_cast<uint8_t>( ExperienceKind::BATTLE ) == 2 );
    static_assert( static_cast<uint8_t>( ExperienceKind::ADVENTURE ) == 3 );

    // The local player's kingdom owns the persistent profile. Enemy profiles only live for this map.
    [[nodiscard]] std::string dataDirectory();
    void beginMap( PlayerColor playerColor );
    void endMap();
    [[nodiscard]] uint64_t addExperience( PlayerColor color, uint64_t amount, ExperienceKind kind = ExperienceKind::HERO );
    void awardAdventureAction( PlayerColor color, int objectType, int32_t tileIndex );
    [[nodiscard]] uint64_t previewAdventureActionExperience( PlayerColor color, int objectType, int32_t tileIndex );
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
    [[nodiscard]] double expectedCriticalDamageMultiplier( PlayerColor color );
    [[nodiscard]] double sustainValuePercent( PlayerColor color );
    [[nodiscard]] double evasionChance( PlayerColor color );
    [[nodiscard]] double rangedMeleePenaltyRecoveryPercent( PlayerColor color );

    // Upgrade IDs index persistent rank/use-count arrays and are also shared by combat and AI code.
    // Keep their layout stable: accidental insertion or reordering would silently reinterpret saved
    // profiles and doctrine telemetry even though the project would still compile.
    enum UpgradeId : size_t
    {
        ARMS_TRAINING = 0, ARMOR_TRAINING, VETERAN_CORE, BLOOD_DRINKER, REAPER,
        FEROCITY, MARKSMAN, BRAWLER, EXECUTIONER, OPENING_BLOW,
        GIANT_SLAYER, OVERWHELM, FRENZY, DISCIPLINE, ARMOR_PIERCING,
        IRON_SKIN, ARROW_WARD, MELEE_GUARD, LAST_STAND, BULWARK,
        SORCERY, PYROMANCY, CRYOMANCY, STORMCRAFT, CATACLYSM,
        SPELL_WARD, FIRE_WARD, COLD_WARD, STORM_WARD, CATACLYSM_WARD,
        LEADERSHIP, FORTUNE, REGENERATION, CRITICAL_TRAINING, BRUTAL_CRITICALS,
        EVASION, ARCANE_PIERCING, CLOSE_QUARTERS, UNYIELDING, RUTHLESS,
        UPGRADE_COUNT
    };

    static_assert( ARMS_TRAINING == 0 );
    static_assert( REAPER == 4 );
    static_assert( FEROCITY == 5 );
    static_assert( ARMOR_PIERCING == 14 );
    static_assert( IRON_SKIN == 15 );
    static_assert( SORCERY == 20 );
    static_assert( SPELL_WARD == 25 );
    static_assert( LEADERSHIP == 30 );
    static_assert( EVASION == 35 );
    static_assert( RUTHLESS == 39 );
    static_assert( UPGRADE_COUNT == 40 );

    // Spell specialties and their matching wards intentionally share the same relative layout.
    // Combat, telemetry and UI helpers rely on this mapping, so guard it at compile time.
    static_assert( FIRE_WARD - PYROMANCY == 5 );
    static_assert( COLD_WARD - CRYOMANCY == 5 );
    static_assert( STORM_WARD - STORMCRAFT == 5 );
    static_assert( CATACLYSM_WARD - CATACLYSM == 5 );

    void recordDoctrineUse( PlayerColor color, size_t upgradeId, uint64_t count = 1 );
    void recordSpellDoctrineUse( PlayerColor attacker, PlayerColor defender, int spellId );
    void recordPhysicalDoctrineUse( PlayerColor attacker, PlayerColor defender, bool ranged, bool attackerOutnumbered, bool defenderOutnumbered,
                                    bool attackerFullHealth, bool defenderFullHealth, bool attackerBelowHalf, bool defenderBelowHalf,
                                    bool inMeleePenalty = false );
    [[nodiscard]] uint64_t availablePoints();
    [[nodiscard]] uint64_t kingdomLevel();
    [[nodiscard]] bool isStewardActive();
    [[nodiscard]] uint64_t doctrineRank( PlayerColor color, size_t upgradeId );
    // Returns the actual current mechanical value of a doctrine (flat stat/point or percentage,
    // depending on the doctrine). Prefer this over raw rank when valuing diminishing-return upgrades.
    [[nodiscard]] double doctrineEffect( PlayerColor color, size_t upgradeId );

    [[nodiscard]] double damageMultiplier( PlayerColor attacker, PlayerColor defender, bool ranged, bool attackerOutnumbered, bool defenderOutnumbered,
                                           bool attackerFullHealth, bool defenderFullHealth, bool attackerBelowHalf, bool defenderBelowHalf );
    [[nodiscard]] double spellMultiplier( PlayerColor attacker, PlayerColor defender, int spellId );
    [[nodiscard]] std::string formatExperience( uint64_t value );
    void showMenu();
}
