#include "game_rpg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "audio_manager.h"
#include "color.h"
#include "m82.h"
#include "cursor.h"
#include "dialog.h"
#include "game_assets.h"
#include "game_hotkeys.h"
#include "icn.h"
#include "image.h"
#include "kingdom.h"
#include "resource.h"
#include "localevent.h"
#include "logging.h"
#include "mp2.h"
#include "players.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "spell.h"
#include "translations.h"
#include "ui_dialog.h"
#include "ui_button.h"
#include "ui_text.h"
#include "ui_window.h"
#include "world.h"

namespace
{
    constexpr size_t upgradeCount = static_cast<size_t>( fheroes2::RPG::UPGRADE_COUNT );
    constexpr size_t upgradesPerTab = 5;
    constexpr uint64_t pointsPerLevel = 5;
    constexpr uint64_t baseLevelExperience = 50000;
    constexpr uint64_t experiencePerLevel = 25000;
    constexpr uint64_t legacyV7BaseLevelExperience = 100000;
    constexpr uint64_t legacyV7ExperiencePerLevel = 50000;
    constexpr uint64_t legacyV6BaseLevelExperience = 150000;
    constexpr uint64_t legacyV6ExperiencePerLevel = 100000;
    constexpr int profileVersion = 8;
    using namespace fheroes2::RPG;

    uint64_t xpToNextLevel( uint64_t level );
    uint64_t legacyV7XpToNextLevel( uint64_t level );
    uint64_t legacyV6XpToNextLevel( uint64_t level );

    struct UpgradeInfo
    {
        const char * name;
        const char * description;
    };
    constexpr std::array<UpgradeInfo, upgradeCount> upgrades{
        UpgradeInfo{ "Arms Training", "Increase creature Attack" }, { "Armor Training", "Increase creature Defense" },
        { "Veteran Core", "Increase Attack and Defense" }, { "Blood Drinker", "Attacks steal life" }, { "Reaper", "Kills restore life" },

        { "Ferocity", "Increase all creature damage" }, { "Marksman", "Increase ranged damage" }, { "Brawler", "Increase melee damage" },
        { "Executioner", "More damage to wounded stacks" }, { "Opening Blow", "More damage at full starting HP" },

        { "Giant Slayer", "More damage while outnumbered" }, { "Overwhelm", "More damage while outnumbering" },
        { "Frenzy", "More damage below half health" }, { "Discipline", "More damage at full health" },
        { "Armor Piercing", "Ignore RPG physical resistance" },

        { "Iron Skin", "Reduce all creature damage" }, { "Arrow Ward", "Reduce ranged damage" }, { "Melee Guard", "Reduce melee damage" },
        { "Last Stand", "Reduce damage below half health" }, { "Bulwark", "Reduce damage while outnumbered" },

        { "Sorcery", "Increase all damaging spells" }, { "Pyromancy", "Increase fire spell damage" }, { "Cryomancy", "Increase cold spell damage" },
        { "Stormcraft", "Increase lightning damage" }, { "Cataclysm", "Increase wide-area spell damage" },

        { "Spell Ward", "Reduce all spell damage" }, { "Fire Ward", "Reduce fire spell damage" }, { "Cold Ward", "Reduce cold spell damage" },
        { "Storm Ward", "Reduce lightning spell damage" }, { "Chaos Ward", "Reduce wide-area spell damage" },

        { "Leadership", "Increase creature Morale" }, { "Fortune", "Increase creature Luck" }, { "Regeneration", "Heal wounded creatures each turn" },
        { "Critical Training", "Chance for a critical attack" }, { "Brutal Criticals", "Critical attacks deal more damage" },

        { "Evasion", "Chance to halve creature damage" }, { "Arcane Piercing", "Ignore RPG spell resistance" },
        { "Close Quarters", "Reduce ranged melee penalty" }, { "Unyielding", "Reduce damage at full starting HP" },
        { "Ruthless", "More damage to stacks below half health" }
    };
    constexpr std::array<const char *, upgradeCount> upgradeSigils{
        "AT", "DF", "VC", "BD", "RP",
        "FC", "MK", "BR", "EX", "OB",
        "GS", "OW", "FZ", "DC", "AP",
        "IS", "AW", "MG", "LS", "BW",
        "SO", "PY", "CY", "ST", "CT",
        "SW", "FW", "CW", "LW", "XW",
        "LD", "FT", "RG", "CR", "BC",
        "EV", "MP", "CQ", "UY", "RT"
    };
    constexpr std::array<const char *, upgradeCount> upgradeDetails{
        "Adds +1 Attack per rank to every creature stack controlled by this RPG profile with no doctrine cap.",
        "Adds +1 Defense per rank to every creature stack controlled by this RPG profile with no doctrine cap.",
        "Adds +1 Attack and +1 Defense per rank to every creature stack with no doctrine cap. Its ranks cost more because both stats increase together.",
        "Whenever one of your creature stacks deals attack damage, it heals for a logarithmically scaling percentage of the actual damage dealt with no doctrine cap. Healing is still limited by the stack's missing hit points and cannot resurrect killed creatures.",
        "Whenever one of your attacks kills creatures, the attacking stack heals for a logarithmically scaling percentage of the slain creatures' hit points with no doctrine cap. Healing is still limited by missing hit points and cannot resurrect creatures already lost from that stack.",

        "Increases all physical damage dealt by your creature stacks, scaling evenly across melee and missile attacks.",
        "Increases physical damage dealt by ranged creature attacks, rewarding archer superiority.",
        "Increases physical damage dealt by melee creature attacks, rewarding infantry charges and close-quarters clashes.",
        "Increases physical damage against any stack that has already lost hit points or creatures, accelerating frontline attrition.",
        "Increases physical damage against an enemy stack while it is currently at its full starting battle hit points to break initial lines.",

        "Increases physical damage when the attacking stack has fewer creatures than its target, rewarding courageous underdog strikes.",
        "Increases physical damage when the attacking stack has more creatures than its target, punishing fractured enemy formations.",
        "Increases physical damage while the attacking stack is below half of its starting battle hit points, fueling desperate comebacks.",
        "Increases physical damage while the attacking stack is still at its full starting battle hit points, rewarding disciplined alpha strikes.",
        "Ignores an increasing share of the defender's RPG physical damage reduction after all applicable defensive upgrades are combined, with a hard safety ceiling of 100% resistance bypass.",

        "Reduces all physical damage received by your creature stacks. Combined RPG physical reduction is capped at 70% before Armor Piercing.",
        "Adds extra physical damage reduction against ranged creature attacks, blunting opposing missile barrages.",
        "Adds extra physical damage reduction against melee creature attacks, steadying frontliners against direct assaults.",
        "Adds extra physical damage reduction while the defending stack is below half of its starting battle hit points to endure final assaults.",
        "Adds extra physical damage reduction while the defending stack has fewer creatures than its attacker, mitigating outnumbered focus fire.",

        "Increases damage from every damaging spell cast by a hero using this RPG profile, boosting direct and area arcana.",
        "Adds damage to Fireball and Fireblast, amplifying blazing devastation.",
        "Adds damage to Cold Ray and Cold Ring, empowering biting frost arcana.",
        "Adds damage to Lightning Bolt and Chain Lightning, channeling concentrated celestial storms.",
        "Adds damage to Elemental Storm, Meteor Shower, and Armageddon, magnifying world-shaking battlefield destruction.",

        "Reduces damage received from every damaging spell. Combined RPG spell reduction is capped at 70% before Arcane Piercing.",
        "Adds RPG resistance against Fireball and Fireblast, shielding your ranks against incinerating firestorms.",
        "Adds RPG resistance against Cold Ray and Cold Ring, insulating your troops against chilling frost spells.",
        "Adds RPG resistance against Lightning Bolt and Chain Lightning, grounding deadly lightning strikes.",
        "Adds RPG resistance against Elemental Storm, Meteor Shower, and Armageddon, surviving apocalyptic planar cataclysms.",

        "Adds Morale to your creature stacks during combat, up to +3 from this upgrade.",
        "Adds Luck to your creature stacks during combat, up to +3 from this upgrade.",
        "At the beginning of a stack's turn, restores a logarithmically scaling percentage of one creature's maximum hit points to the surviving stack with no doctrine cap. Actual healing cannot exceed the stack's missing hit points and never resurrects dead creatures.",
        "Gives each creature attack an increasing chance to become a critical hit, hard-capped at 100% because probability cannot exceed certainty. A critical hit deals 50% extra damage before Brutal Criticals is added.",
        "Increases the bonus damage of critical hits beyond their normal +50%. Requires at least one rank of Critical Training.",

        "Gives your creature stacks an increasing chance to reduce an incoming creature attack's final damage by 50%, hard-capped at 100% because probability cannot exceed certainty.",
        "Ignores an increasing share of the target's combined RPG spell resistance when your hero casts a damaging spell, with a hard safety ceiling of 100% resistance bypass.",
        "Recovers part of the normal 50% melee penalty suffered by ranged creatures forced into hand-to-hand combat. At 100% recovery, the RPG penalty modifier removes that penalty.",
        "Reduces physical damage while the defending stack is currently at its full starting battle hit points.",
        "Adds another damage bonus against enemy stacks below half of their starting battle hit points, rewarding aggressive finishing attacks."
    };
    constexpr std::array<const char *, 8> tabNames{ "ARMY", "OFFENSE", "TACTICS", "DEFENSE", "MAGIC", "WARDS", "COMMAND", "MASTERY" };
    constexpr std::array<const char *, 8> tabPanelNames{
        "TRAINING YARD", "WAR COUNCIL", "TACTICS HALL", "GUARD HOUSE", "MAGE GUILD", "WARD HALL", "THRONE ROOM", "MASTER'S HALL"
    };
    static_assert( tabNames.size() * upgradesPerTab == upgradeCount );
    static_assert( tabPanelNames.size() == tabNames.size() );

    struct Profile
    {
        uint64_t level{ 1 };
        uint64_t experience{ 0 };
        uint64_t progress{ 0 };
        uint64_t points{ 0 };
        uint64_t fieldExperience{ 0 };
        uint64_t offlineExperience{ 0 };
        uint64_t battleExperience{ 0 };
        uint64_t adventureExperience{ 0 };
        uint64_t heroExperience{ 0 };
        std::array<uint64_t, upgradeCount> ranks{};
        std::array<uint64_t, upgradeCount> useCounts{};
        bool autoBuy{ false };
    };

    enum class RivalArchetype
    {
        NONE,
        WARLORD,
        PREDATOR,
        ARCANIST,
        SENTINEL,
        TRICKSTER
    };

    constexpr std::array<const char *, 5> rivalArchetypeNames{ "Warlord", "Predator", "Arcanist", "Sentinel", "Trickster" };

    uint64_t consumeAffordableLevelsWithCarry( Profile & profile );

    Profile playerProfile;
    std::map<PlayerColor, Profile> enemyProfiles;
    std::set<PlayerColor> eliteEnemyColors;
    std::map<PlayerColor, RivalArchetype> eliteRivalArchetypes;
    std::set<uint64_t> visitedActionTiles;
    PlayerColor activePlayerColor = PlayerColor::NONE;

    uint64_t saturatedAdd( const uint64_t a, const uint64_t b )
    {
        return a > std::numeric_limits<uint64_t>::max() - b ? std::numeric_limits<uint64_t>::max() : a + b;
    }

    uint64_t saturatedMultiply( const uint64_t a, const uint64_t b )
    {
        if ( a == 0 || b == 0 ) {
            return 0;
        }
        return a > std::numeric_limits<uint64_t>::max() / b ? std::numeric_limits<uint64_t>::max() : a * b;
    }

    uint64_t saturatedTriangular( const uint64_t value )
    {
        if ( value < 2 ) {
            return 0;
        }

        uint64_t first = value;
        uint64_t second = value - 1;
        if ( first % 2 == 0 ) {
            first /= 2;
        }
        else {
            second /= 2;
        }
        return saturatedMultiply( first, second );
    }

    uint64_t prestigeRankForLevel( const uint64_t level )
    {
        return level / 10;
    }

    uint64_t nextPrestigeLevel( const uint64_t level )
    {
        return saturatedMultiply( saturatedAdd( prestigeRankForLevel( level ), 1 ), 10 );
    }

    long double prestigeBattleRenownMultiplier( const uint64_t level )
    {
        const long double bonus = std::min<long double>( 0.25L, static_cast<long double>( prestigeRankForLevel( level ) ) * 0.02L );
        return 1.0L + bonus;
    }

    int eliteRivalChanceForLevel( const uint64_t level )
    {
        if ( level < 5 ) {
            return 0;
        }

        const uint64_t prestigeBonus = std::min<uint64_t>( 10, prestigeRankForLevel( level ) );
        return static_cast<int>( std::min<uint64_t>( 40, 10 + level / 4 + prestigeBonus ) );
    }

    size_t stewardPlanningHorizon( const uint64_t level )
    {
        return 3 + static_cast<size_t>( std::min<uint64_t>( 3, prestigeRankForLevel( level ) ) );
    }

    size_t eliteRivalFocusHallLimit( const uint64_t level )
    {
        return 4 + static_cast<size_t>( std::min<uint64_t>( 2, prestigeRankForLevel( level ) / 2 ) );
    }

    uint64_t rivalArchetypeHallBias( const RivalArchetype archetype, const size_t tab )
    {
        switch ( archetype ) {
        case RivalArchetype::WARLORD:
            return tab == 0 || tab == 6 ? 144 : tab == 3 ? 72 : 0;
        case RivalArchetype::PREDATOR:
            return tab == 1 || tab == 2 ? 144 : tab == 7 ? 96 : 0;
        case RivalArchetype::ARCANIST:
            return tab == 4 || tab == 5 ? 144 : tab == 7 ? 72 : 0;
        case RivalArchetype::SENTINEL:
            return tab == 3 || tab == 5 ? 144 : tab == 0 ? 72 : 0;
        case RivalArchetype::TRICKSTER:
            return tab == 2 || tab == 7 ? 144 : tab == 6 ? 72 : 0;
        default:
            return 0;
        }
    }

    int rivalArchetypeDoctrineBias( const RivalArchetype archetype, const size_t id )
    {
        switch ( archetype ) {
        case RivalArchetype::WARLORD:
            switch ( id ) {
            case ARMS_TRAINING:
            case ARMOR_TRAINING:
            case VETERAN_CORE:
            case LEADERSHIP:
            case REGENERATION:
                return 9;
            case CRITICAL_TRAINING:
            case BRUTAL_CRITICALS:
                return 6;
            default:
                return 0;
            }
        case RivalArchetype::PREDATOR:
            switch ( id ) {
            case EXECUTIONER:
            case RUTHLESS:
            case GIANT_SLAYER:
            case OVERWHELM:
            case FRENZY:
            case ARMOR_PIERCING:
                return 10;
            default:
                return 0;
            }
        case RivalArchetype::ARCANIST:
            switch ( id ) {
            case SORCERY:
            case PYROMANCY:
            case CRYOMANCY:
            case STORMCRAFT:
            case CATACLYSM:
            case ARCANE_PIERCING:
                return 10;
            default:
                return 0;
            }
        case RivalArchetype::SENTINEL:
            switch ( id ) {
            case IRON_SKIN:
            case ARROW_WARD:
            case MELEE_GUARD:
            case LAST_STAND:
            case BULWARK:
            case UNYIELDING:
            case SPELL_WARD:
            case FIRE_WARD:
            case COLD_WARD:
            case STORM_WARD:
            case CATACLYSM_WARD:
                return 9;
            default:
                return 0;
            }
        case RivalArchetype::TRICKSTER:
            switch ( id ) {
            case EVASION:
            case CLOSE_QUARTERS:
            case OPENING_BLOW:
            case DISCIPLINE:
            case FORTUNE:
            case CRITICAL_TRAINING:
                return 10;
            default:
                return 0;
            }
        default:
            return 0;
        }
    }

    uint64_t baseCost( const size_t id )
    {
        switch ( id ) {
        case VETERAN_CORE:
            return 3;
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
        case LEADERSHIP:
        case FORTUNE:
        case CRITICAL_TRAINING:
        case BRUTAL_CRITICALS:
        case EVASION:
            return 2;
        default:
            return 1;
        }
    }

    uint64_t rankInvestment( const size_t id, const uint64_t rank )
    {
        return saturatedMultiply( rank, baseCost( id ) );
    }

    uint64_t legacyLinearRankInvestment( const uint64_t rank, const uint64_t base, const uint64_t step )
    {
        return saturatedAdd( saturatedMultiply( rank, base ), saturatedMultiply( saturatedTriangular( rank ), step ) );
    }

    uint64_t legacySteppedRankInvestment( const uint64_t rank, const uint64_t base, const uint64_t groupSize )
    {
        assert( groupSize > 0 );
        const uint64_t completeGroups = rank / groupSize;
        const uint64_t remainder = rank % groupSize;
        const uint64_t completedGroupSteps = saturatedMultiply( groupSize, saturatedTriangular( completeGroups ) );
        const uint64_t remainderSteps = saturatedMultiply( completeGroups, remainder );
        return saturatedAdd( saturatedMultiply( rank, base ), saturatedAdd( completedGroupSteps, remainderSteps ) );
    }

    uint64_t legacyRankInvestment( const size_t id, const uint64_t rank )
    {
        switch ( id ) {
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
            return legacyLinearRankInvestment( rank, 2, 1 );
        case VETERAN_CORE:
            return legacyLinearRankInvestment( rank, 3, 2 );
        case LEADERSHIP:
        case FORTUNE:
            return legacyLinearRankInvestment( rank, 2, 2 );
        case BLOOD_DRINKER:
        case REAPER:
        case REGENERATION:
            return legacySteppedRankInvestment( rank, 1, 4 );
        case CRITICAL_TRAINING:
        case BRUTAL_CRITICALS:
        case EVASION:
            return legacySteppedRankInvestment( rank, 2, 3 );
        default:
            return legacySteppedRankInvestment( rank, 1, 5 );
        }
    }

    uint64_t scaledValue( const uint64_t value, const int percent )
    {
        const long double result = static_cast<long double>( value ) * percent / 100;
        return result >= static_cast<long double>( std::numeric_limits<uint64_t>::max() )
                   ? std::numeric_limits<uint64_t>::max()
                   : static_cast<uint64_t>( result );
    }

    // Suffix labels are generated, rather than indexed from a finite list.
    std::string numberSuffix( const size_t group )
    {
        constexpr std::array<const char *, 7> familiar{ "", "K", "M", "B", "T", "Qa", "Qi" };
        if ( group < familiar.size() ) {
            return familiar[group];
        }

        size_t ordinal = group - familiar.size() + 1;
        std::string suffix;
        do {
            --ordinal;
            suffix.insert( suffix.begin(), static_cast<char>( 'A' + ordinal % 26 ) );
            ordinal /= 26;
        } while ( ordinal > 0 );
        return suffix;
    }

    std::string formatDecimalNumber( const std::string & digits )
    {
        // Work on decimal digits so the display algorithm has no fixed suffix table or
        // digit-count limit. Current RPG counters are stored as uint64_t.
        if ( digits.empty() ) return "0";
        const size_t group = ( digits.size() - 1 ) / 3;
        if ( group == 0 ) return digits;

        const size_t leadingDigits = digits.size() - group * 3;
        std::string formatted = digits.substr( 0, leadingDigits );
        const std::string fraction = digits.substr( leadingDigits, 2 );
        if ( fraction != "00" ) {
            formatted += '.';
            formatted += fraction;
            if ( formatted.back() == '0' ) formatted.pop_back();
        }
        return formatted + numberSuffix( group );
    }

    std::string formatNumber( const uint64_t value )
    {
        return formatDecimalNumber( std::to_string( value ) );
    }

    std::string formatCompactDoctrineNumber( long double value )
    {
        if ( !std::isfinite( static_cast<double>( value ) ) ) {
            return "0";
        }

        const bool negative = value < 0.0L;
        value = std::abs( value );

        size_t group = 0;
        while ( value >= 1000.0L ) {
            value /= 1000.0L;
            ++group;
        }

        std::ostringstream text;
        const int precision = value >= 100.0L ? 0 : value >= 10.0L ? 1 : 2;
        text << std::fixed << std::setprecision( precision ) << static_cast<double>( value );
        std::string result = text.str();
        while ( result.size() > 1 && result.back() == '0' && result.find( '.' ) != std::string::npos ) {
            result.pop_back();
        }
        if ( !result.empty() && result.back() == '.' ) {
            result.pop_back();
        }

        if ( negative ) {
            result.insert( result.begin(), '-' );
        }
        return result + numberSuffix( group );
    }

    long double percentEffect( const uint64_t rank )
    {
        return 4.0L * std::log1p( static_cast<long double>( rank ) );
    }

    long double diminishingFlatStatEffect( const uint64_t rank )
    {
        if ( rank == 0 ) {
            return 0.0L;
        }

        // Flat combat stats used to be the only fully linear doctrines. Keep rank 1 worth one
        // full point, then gradually reduce each later rank's marginal value without ever imposing
        // a hard cap. The curve is intentionally gentler than the percentage-doctrine log curves.
        constexpr long double softness = 25.0L;
        return std::log1p( static_cast<long double>( rank ) / softness ) / std::log1p( 1.0L / softness );
    }

    uint64_t diminishingFlatStatBonus( const uint64_t rank )
    {
        const long double value = diminishingFlatStatEffect( rank );
        if ( value <= 0.0L ) {
            return 0;
        }
        return static_cast<uint64_t>( std::min<long double>(
            static_cast<long double>( std::numeric_limits<uint64_t>::max() ), std::ceil( value - 1.0e-12L ) ) );
    }

    long double effect( const size_t id, const uint64_t rank )
    {
        if ( rank == 0 ) {
            return 0;
        }

        switch ( id ) {
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
        case VETERAN_CORE:
            // Flat stats remain open-ended, but later ranks contribute progressively less.
            return diminishingFlatStatEffect( rank );
        case LEADERSHIP:
        case FORTUNE:
            // Heroes II only has three positive Morale/Luck steps. Additional ranks would have no
            // mechanical meaning, so these remain hard-capped engine mechanics.
            return static_cast<long double>( std::min<uint64_t>( rank, 3 ) );
        case BLOOD_DRINKER:
            return 3.0L * std::log1p( static_cast<long double>( rank ) );
        case REAPER:
            return 4.0L * std::log1p( static_cast<long double>( rank ) );
        case REGENERATION:
            return 2.5L * std::log1p( static_cast<long double>( rank ) );
        case CRITICAL_TRAINING:
            // Probability mechanics cannot exceed certainty.
            return std::min<long double>( 100.0L, 4.0L * std::log1p( static_cast<long double>( rank ) ) );
        case BRUTAL_CRITICALS:
            return 8.0L * std::log1p( static_cast<long double>( rank ) );
        case EVASION:
            return std::min<long double>( 100.0L, 2.5L * std::log1p( static_cast<long double>( rank ) ) );
        case ARMOR_PIERCING:
        case ARCANE_PIERCING:
            // More than 100% resistance bypass has no useful interpretation.
            return std::min<long double>( 100.0L, 6.0L * std::log1p( static_cast<long double>( rank ) ) );
        case CLOSE_QUARTERS:
            // 100% means the normal ranged-in-melee penalty is completely recovered.
            return std::min<long double>( 100.0L, 10.0L * std::log1p( static_cast<long double>( rank ) ) );
        case IRON_SKIN:
        case ARROW_WARD:
        case MELEE_GUARD:
        case LAST_STAND:
        case BULWARK:
        case UNYIELDING:
        case SPELL_WARD:
        case FIRE_WARD:
        case COLD_WARD:
        case STORM_WARD:
        case CATACLYSM_WARD:
            // Individual defensive doctrines keep scaling; the combined reduction is clamped
            // later to 70% so no stack can become invulnerable.
            return 3.5L * std::log1p( static_cast<long double>( rank ) );
        default:
            return percentEffect( rank );
        }
    }

    enum class AscensionChannel
    {
        ATTACK,
        DEFENSE,
        PHYSICAL_DAMAGE,
        PHYSICAL_REDUCTION,
        SPELL_DAMAGE,
        SPELL_REDUCTION,
        CRITICAL_CHANCE,
        CRITICAL_DAMAGE,
        EVASION,
        LIFE_STEAL,
        REGENERATION,
        ARMOR_PIERCING,
        ARCANE_PIERCING
    };

    constexpr std::array<uint64_t, 4> ascensionMilestones{ 10, 25, 50, 100 };

    constexpr std::array<AscensionChannel, upgradeCount> ascensionChannels{
        AscensionChannel::PHYSICAL_DAMAGE, AscensionChannel::PHYSICAL_REDUCTION, AscensionChannel::CRITICAL_DAMAGE,
        AscensionChannel::CRITICAL_CHANCE, AscensionChannel::REGENERATION,

        AscensionChannel::ATTACK, AscensionChannel::ARMOR_PIERCING, AscensionChannel::PHYSICAL_REDUCTION,
        AscensionChannel::CRITICAL_DAMAGE, AscensionChannel::ARMOR_PIERCING,

        AscensionChannel::ATTACK, AscensionChannel::CRITICAL_CHANCE, AscensionChannel::LIFE_STEAL,
        AscensionChannel::DEFENSE, AscensionChannel::PHYSICAL_DAMAGE,

        AscensionChannel::DEFENSE, AscensionChannel::EVASION, AscensionChannel::ATTACK,
        AscensionChannel::REGENERATION, AscensionChannel::PHYSICAL_DAMAGE,

        AscensionChannel::ARCANE_PIERCING, AscensionChannel::SPELL_REDUCTION, AscensionChannel::DEFENSE,
        AscensionChannel::CRITICAL_CHANCE, AscensionChannel::ARCANE_PIERCING,

        AscensionChannel::SPELL_DAMAGE, AscensionChannel::SPELL_DAMAGE, AscensionChannel::DEFENSE,
        AscensionChannel::EVASION, AscensionChannel::SPELL_DAMAGE,

        AscensionChannel::ATTACK, AscensionChannel::CRITICAL_DAMAGE, AscensionChannel::PHYSICAL_REDUCTION,
        AscensionChannel::CRITICAL_DAMAGE, AscensionChannel::CRITICAL_CHANCE,

        AscensionChannel::PHYSICAL_REDUCTION, AscensionChannel::SPELL_DAMAGE, AscensionChannel::PHYSICAL_DAMAGE,
        AscensionChannel::ATTACK, AscensionChannel::LIFE_STEAL
    };

    constexpr std::array<const char *, upgradeCount> ascensionNames{
        "Battle Rhythm", "Fortified Formation", "Veteran Instinct", "Blood Rush", "Harvest Cycle",
        "War Temper", "Piercing Volley", "Guarded Brawl", "Finality", "First Breach",
        "David's Aim", "Momentum", "Blood Heat", "Battle Order", "Exposed Weakness",
        "Iron Formation", "Deflection", "Counterstance", "Second Wind", "Hold and Strike",
        "Arcane Pressure", "Flame Tempering", "Frozen Guard", "Static Charge", "Worldbreaker",
        "Reflected Insight", "Fire Attunement", "Frost Shell", "Stormstep", "Chaos Insight",
        "Commander's Edge", "Lucky Break", "Renewed Guard", "Killing Form", "Blood Sense",
        "Flowing Guard", "Arcane Feedback", "Point-Blank Mastery", "Resolute Counter", "Predatory Finish"
    };

    uint8_t ascensionTierForRank( const uint64_t rank )
    {
        uint8_t tier = 0;
        for ( const uint64_t milestone : ascensionMilestones ) {
            if ( rank < milestone ) {
                break;
            }
            ++tier;
        }
        return tier;
    }

    uint64_t nextAscensionRank( const uint64_t rank )
    {
        for ( const uint64_t milestone : ascensionMilestones ) {
            if ( rank < milestone ) {
                return milestone;
            }
        }
        return 0;
    }

    const char * ascensionTierLabel( const uint8_t tier )
    {
        switch ( tier ) {
        case 1: return "Ascension I";
        case 2: return "Ascension II";
        case 3: return "Ascension III";
        case 4: return "Ascension IV";
        default: return "Unascended";
        }
    }

    const char * ascensionChannelDescription( const AscensionChannel channel )
    {
        switch ( channel ) {
        case AscensionChannel::ATTACK: return "+1 creature Attack at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::DEFENSE: return "+1 creature Defense at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::PHYSICAL_DAMAGE: return "+1.25% physical damage at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::PHYSICAL_REDUCTION: return "+0.75% physical reduction at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::SPELL_DAMAGE: return "+1.25% spell power at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::SPELL_REDUCTION: return "+0.75% spell reduction at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::CRITICAL_CHANCE: return "+0.5% critical chance at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::CRITICAL_DAMAGE: return "+2.5% critical damage at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::EVASION: return "+0.5% Evasion at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::LIFE_STEAL: return "+1% life steal at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::REGENERATION: return "+0.75% Regeneration at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::ARMOR_PIERCING: return "+1.5% Armor Piercing at Ascension I; later stages diminish to 75% / 55% / 40%";
        case AscensionChannel::ARCANE_PIERCING: return "+1.5% Arcane Piercing at Ascension I; later stages diminish to 75% / 55% / 40%";
        default: return "No secondary effect";
        }
    }

    long double ascensionPerTier( const AscensionChannel channel )
    {
        switch ( channel ) {
        case AscensionChannel::ATTACK:
        case AscensionChannel::DEFENSE:
            return 1.0L;
        case AscensionChannel::PHYSICAL_DAMAGE:
        case AscensionChannel::SPELL_DAMAGE:
            return 1.25L;
        case AscensionChannel::PHYSICAL_REDUCTION:
        case AscensionChannel::SPELL_REDUCTION:
        case AscensionChannel::REGENERATION:
            return 0.75L;
        case AscensionChannel::CRITICAL_CHANCE:
        case AscensionChannel::EVASION:
            return 0.50L;
        case AscensionChannel::CRITICAL_DAMAGE:
            return 2.50L;
        case AscensionChannel::LIFE_STEAL:
            return 1.0L;
        case AscensionChannel::ARMOR_PIERCING:
        case AscensionChannel::ARCANE_PIERCING:
            return 1.50L;
        default:
            return 0.0L;
        }
    }

    long double ascensionStageWeight( const uint8_t stage )
    {
        switch ( stage ) {
        case 1: return 1.00L;
        case 2: return 0.75L;
        case 3: return 0.55L;
        case 4: return 0.40L;
        default: return 0.0L;
        }
    }

    long double ascensionTierScale( const uint8_t tier )
    {
        long double scale = 0.0L;
        for ( uint8_t stage = 1; stage <= tier; ++stage ) {
            scale += ascensionStageWeight( stage );
        }
        return scale;
    }

    long double ascensionChannelBonus( const Profile & profile, const AscensionChannel channel )
    {
        long double total = 0.0L;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            if ( ascensionChannels[id] == channel ) {
                total += ascensionTierScale( ascensionTierForRank( profile.ranks[id] ) ) * ascensionPerTier( channel );
            }
        }
        return total;
    }

    bool doctrineCanAdvance( const size_t id, const uint64_t rank )
    {
        if ( id >= upgradeCount || rank == std::numeric_limits<uint64_t>::max() ) {
            return false;
        }

        if ( effect( id, rank + 1 ) > effect( id, rank ) ) {
            return true;
        }

        // A hard-capped primary mechanic may still progress toward a real Ascension milestone.
        return nextAscensionRank( rank ) != 0;
    }

    bool storedDoctrineRankIsReachable( const size_t id, const uint64_t rank )
    {
        if ( rank == 0 ) {
            return true;
        }
        if ( effect( id, rank ) > effect( id, rank - 1 ) ) {
            return true;
        }

        // Ranks at or below the final Ascension milestone can be mechanically meaningful even
        // after the doctrine's primary effect has reached a hard engine cap.
        return rank <= ascensionMilestones.back();
    }

    long double ascensionMilestoneUtility( const size_t id, const uint64_t currentRank )
    {
        if ( id >= upgradeCount || currentRank == std::numeric_limits<uint64_t>::max() ) {
            return 0.0L;
        }

        const uint8_t currentTier = ascensionTierForRank( currentRank );
        const uint8_t nextTier = ascensionTierForRank( currentRank + 1 );
        if ( nextTier > currentTier ) {
            const AscensionChannel channel = ascensionChannels[id];
            const long double raw = ascensionPerTier( channel ) * ascensionStageWeight( nextTier );
            switch ( channel ) {
            case AscensionChannel::ATTACK:
            case AscensionChannel::DEFENSE:
                return raw * 7.5L;
            case AscensionChannel::CRITICAL_DAMAGE:
                return raw * 0.60L;
            case AscensionChannel::CRITICAL_CHANCE:
            case AscensionChannel::EVASION:
                return raw * 0.85L;
            case AscensionChannel::LIFE_STEAL:
            case AscensionChannel::REGENERATION:
                return raw * 0.80L;
            default:
                return raw;
            }
        }

        // Hard-capped primaries still need a small planning value for the ranks between milestones,
        // otherwise the Steward could never deliberately reach their next evolution.
        if ( effect( id, currentRank + 1 ) <= effect( id, currentRank ) ) {
            const uint64_t milestone = nextAscensionRank( currentRank );
            if ( milestone > currentRank ) {
                return 0.35L / static_cast<long double>( milestone - currentRank );
            }
        }

        return 0.0L;
    }

    long double doctrinePairResonance( const Profile & profile, const size_t first, const size_t second, const long double scale,
                                        const long double cap )
    {
        if ( profile.ranks[first] == 0 || profile.ranks[second] == 0 ) {
            return 0.0L;
        }

        return std::min( cap, std::min( effect( first, profile.ranks[first] ), effect( second, profile.ranks[second] ) ) * scale );
    }

    long double doctrineTripleResonance( const Profile & profile, const size_t first, const size_t second, const size_t third,
                                          const long double scale, const long double cap )
    {
        if ( profile.ranks[first] == 0 || profile.ranks[second] == 0 || profile.ranks[third] == 0 ) {
            return 0.0L;
        }

        return std::min(
            cap, std::min( { effect( first, profile.ranks[first] ), effect( second, profile.ranks[second] ), effect( third, profile.ranks[third] ) } ) * scale );
    }

    const char * doctrineResonanceHint( const size_t id )
    {
        switch ( id ) {
        case EXECUTIONER:
        case RUTHLESS:
            return "Finisher - both doctrines add a bonus against targets below half health.";
        case GIANT_SLAYER:
        case BULWARK:
            return "Underdog - the pair reinforces offense and defense while outnumbered.";
        case FRENZY:
        case LAST_STAND:
            return "Last Fury - the pair reinforces offense and defense below half health.";
        case OPENING_BLOW:
        case DISCIPLINE:
        case UNYIELDING:
            return "Vanguard - the three-doctrine package rewards starting an exchange at full health.";
        case SORCERY:
        case PYROMANCY:
        case CRYOMANCY:
        case STORMCRAFT:
        case CATACLYSM:
            return "Spell Convergence - Sorcery plus an elemental discipline adds extra matching spell power.";
        case SPELL_WARD:
        case FIRE_WARD:
        case COLD_WARD:
        case STORM_WARD:
        case CATACLYSM_WARD:
            return "Ward Matrix - Spell Ward plus an elemental ward adds extra matching spell resistance.";
        default:
            return nullptr;
        }
    }

    size_t spellSpecializationId( const int spellId )
    {
        switch ( spellId ) {
        case Spell::FIREBALL:
        case Spell::FIREBLAST:
            return PYROMANCY;
        case Spell::COLDRAY:
        case Spell::COLDRING:
            return CRYOMANCY;
        case Spell::LIGHTNINGBOLT:
        case Spell::CHAINLIGHTNING:
            return STORMCRAFT;
        case Spell::ELEMENTALSTORM:
        case Spell::METEORSHOWER:
        case Spell::ARMAGEDDON:
            return CATACLYSM;
        default:
            return upgradeCount;
        }
    }

    size_t spellWardId( const int spellId )
    {
        const size_t specialization = spellSpecializationId( spellId );
        return specialization == upgradeCount ? upgradeCount : specialization + ( FIRE_WARD - PYROMANCY );
    }

    bool isProfileStateValid( const Profile & profile, const uint64_t nextLevelCost )
    {
        // Current-format profiles have a closed guild-point ledger: every point is either
        // still available or was spent through the current doctrine price curve.
        uint64_t investedPoints = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            const uint64_t rank = profile.ranks[id];

            // A stored rank must either increase the primary mechanic or remain within the
            // four-stage Ascension progression window. This rejects corrupted ranks beyond the
            // last meaningful milestone on a hard-capped doctrine.
            if ( !storedDoctrineRankIsReachable( id, rank ) ) {
                return false;
            }

            investedPoints = saturatedAdd( investedPoints, rankInvestment( id, rank ) );
        }

        if ( profile.ranks[BRUTAL_CRITICALS] > 0 && profile.ranks[CRITICAL_TRAINING] == 0 ) {
            return false;
        }

        const uint64_t earnedPoints = saturatedMultiply( profile.level - 1, pointsPerLevel );
        if ( investedPoints > earnedPoints || profile.points != earnedPoints - investedPoints ) {
            return false;
        }

        // Every credited Renown point belongs to exactly one top-level source. Field Renown
        // is further divided into hero, battle and adventure sources.
        if ( profile.experience != saturatedAdd( profile.fieldExperience, profile.offlineExperience )
             || profile.fieldExperience
                    != saturatedAdd( saturatedAdd( profile.heroExperience, profile.battleExperience ), profile.adventureExperience ) ) {
            return false;
        }

        // Progress is always a remainder of total earned Renown. It must be smaller than
        // the cost of the next level because addExperience() immediately consumes every
        // affordable level. Accepting a larger remainder would let a damaged snapshot mint
        // levels and guild points on the next XP award.
        return profile.progress <= profile.experience && profile.progress < nextLevelCost;
    }

    bool isCurrentProfileStateValid( const Profile & profile )
    {
        return isProfileStateValid( profile, xpToNextLevel( profile.level ) );
    }

    bool isLegacyV7ProfileStateValid( const Profile & profile, const uint64_t nextLevelCost )
    {
        uint64_t investedPoints = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            const uint64_t rank = profile.ranks[id];
            if ( rank > 0 && effect( id, rank ) <= effect( id, rank - 1 ) ) {
                return false;
            }

            investedPoints = saturatedAdd( investedPoints, legacyRankInvestment( id, rank ) );
        }

        if ( profile.ranks[BRUTAL_CRITICALS] > 0 && profile.ranks[CRITICAL_TRAINING] == 0 ) {
            return false;
        }

        const uint64_t earnedPoints = saturatedMultiply( profile.level - 1, pointsPerLevel );
        if ( investedPoints > earnedPoints || profile.points != earnedPoints - investedPoints ) {
            return false;
        }

        if ( profile.experience != saturatedAdd( profile.fieldExperience, profile.offlineExperience )
             || profile.fieldExperience
                    != saturatedAdd( saturatedAdd( profile.heroExperience, profile.battleExperience ), profile.adventureExperience ) ) {
            return false;
        }

        return profile.progress <= profile.experience && profile.progress < nextLevelCost;
    }

    uint64_t cost( const size_t id, const uint64_t /* rank */ )
    {
        return baseCost( id );
    }

    bool buy( Profile & profile, const size_t id )
    {
        if ( id >= upgradeCount || !doctrineCanAdvance( id, profile.ranks[id] )
             || ( id == BRUTAL_CRITICALS && profile.ranks[CRITICAL_TRAINING] == 0 ) ) {
            return false;
        }

        const uint64_t price = cost( id, profile.ranks[id] );
        if ( profile.points < price ) {
            return false;
        }

        profile.points -= price;
        ++profile.ranks[id];
        return true;
    }

    long double autoBuyUtility( const Profile & profile, const size_t id )
    {
        const uint64_t rank = profile.ranks[id];
        const long double delta = effect( id, rank + 1 ) - effect( id, rank );

        switch ( id ) {
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
            // A single primary-stat point often changes physical damage by roughly 5-10%.
            return delta * 7.5L;
        case VETERAN_CORE:
            return delta * 12.5L;
        case BLOOD_DRINKER:
            return delta * 0.80L;
        case REAPER:
            return delta * 0.50L;
        case FEROCITY:
        case IRON_SKIN:
        case SORCERY:
        case SPELL_WARD:
            return delta * 1.05L;
        case MARKSMAN:
        case BRAWLER:
        case PYROMANCY:
        case CRYOMANCY:
        case STORMCRAFT:
        case CATACLYSM:
        case ARROW_WARD:
        case MELEE_GUARD:
        case FIRE_WARD:
        case COLD_WARD:
        case STORM_WARD:
        case CATACLYSM_WARD:
            return delta * 0.75L;
        case EXECUTIONER:
        case OPENING_BLOW:
        case GIANT_SLAYER:
        case OVERWHELM:
        case FRENZY:
        case DISCIPLINE:
        case LAST_STAND:
        case BULWARK:
        case UNYIELDING:
        case RUTHLESS:
            return delta * 0.60L;
        case ARMOR_PIERCING:
        case ARCANE_PIERCING:
            return delta * 0.50L;
        case LEADERSHIP:
        case FORTUNE:
            return delta * 4.2L;
        case REGENERATION:
            return delta * 0.85L;
        case CRITICAL_TRAINING:
            return delta * ( 50.0L + effect( BRUTAL_CRITICALS, profile.ranks[BRUTAL_CRITICALS] ) ) / 100.0L;
        case BRUTAL_CRITICALS:
            return delta * effect( CRITICAL_TRAINING, profile.ranks[CRITICAL_TRAINING] ) / 100.0L;
        case EVASION:
            return delta * 0.55L;
        case CLOSE_QUARTERS:
            return delta * 0.40L;
        default:
            return delta;
        }
    }

    long double autoBuyActivityFactor( const Profile & profile, const size_t id )
    {
        switch ( id ) {
        case BLOOD_DRINKER:
        case REAPER:
        case MARKSMAN:
        case BRAWLER:
        case EXECUTIONER:
        case OPENING_BLOW:
        case GIANT_SLAYER:
        case OVERWHELM:
        case FRENZY:
        case DISCIPLINE:
        case ARMOR_PIERCING:
        case ARROW_WARD:
        case MELEE_GUARD:
        case LAST_STAND:
        case BULWARK:
        case PYROMANCY:
        case CRYOMANCY:
        case STORMCRAFT:
        case CATACLYSM:
        case FIRE_WARD:
        case COLD_WARD:
        case STORM_WARD:
        case CATACLYSM_WARD:
        case LEADERSHIP:
        case FORTUNE:
        case REGENERATION:
        case CRITICAL_TRAINING:
        case BRUTAL_CRITICALS:
        case EVASION:
        case ARCANE_PIERCING:
        case CLOSE_QUARTERS:
        case UNYIELDING:
        case RUTHLESS: {
            // Battle-trigger history is intentionally only a modest tiebreaker. It helps the
            // Steward deepen doctrines that are actually paying off for this profile without
            // allowing a heavily used niche perk to overwhelm raw mechanical value per point.
            const long double familiarity = std::log1p( static_cast<long double>( profile.useCounts[id] ) ) / 12.0L;
            const long double familiarityFactor = 1.0L + std::min( 0.35L, familiarity );
            const long double breadthBonus = profile.ranks[id] == 0 ? 0.12L : 0.06L / ( 1.0L + std::log1p( static_cast<long double>( profile.ranks[id] ) ) );
            return familiarityFactor * ( 1.0L + breadthBonus );
        }
        default:
            return 1.0L;
        }
    }

    long double autoBuySynergyFactor( const Profile & profile, const size_t id )
    {
        switch ( id ) {
        case BLOOD_DRINKER:
            return 1.0L
                   + std::min<long double>( 0.16L, effect( REGENERATION, profile.ranks[REGENERATION] ) / 150.0L
                                                      + effect( REAPER, profile.ranks[REAPER] ) / 300.0L );
        case REAPER:
            return 1.0L
                   + std::min<long double>( 0.16L, effect( REGENERATION, profile.ranks[REGENERATION] ) / 150.0L
                                                      + effect( BLOOD_DRINKER, profile.ranks[BLOOD_DRINKER] ) / 250.0L );
        case REGENERATION:
            return 1.0L
                   + std::min<long double>( 0.18L, ( effect( BLOOD_DRINKER, profile.ranks[BLOOD_DRINKER] )
                                                     + effect( REAPER, profile.ranks[REAPER] ) )
                                                       / 180.0L );

        case MARKSMAN:
            return 1.0L + std::min<long double>( 0.18L, effect( CLOSE_QUARTERS, profile.ranks[CLOSE_QUARTERS] ) / 500.0L );
        case CLOSE_QUARTERS:
            return 1.0L + std::min<long double>( 0.18L, effect( MARKSMAN, profile.ranks[MARKSMAN] ) / 100.0L );

        case EXECUTIONER:
            return 1.0L + std::min<long double>( 0.15L, effect( RUTHLESS, profile.ranks[RUTHLESS] ) / 150.0L );
        case RUTHLESS:
            return 1.0L + std::min<long double>( 0.15L, effect( EXECUTIONER, profile.ranks[EXECUTIONER] ) / 150.0L );

        case FRENZY:
            return 1.0L + std::min<long double>( 0.15L, effect( LAST_STAND, profile.ranks[LAST_STAND] ) / 150.0L );
        case LAST_STAND:
            return 1.0L + std::min<long double>( 0.15L, effect( FRENZY, profile.ranks[FRENZY] ) / 150.0L );

        case GIANT_SLAYER:
            return 1.0L + std::min<long double>( 0.15L, effect( BULWARK, profile.ranks[BULWARK] ) / 150.0L );
        case BULWARK:
            return 1.0L + std::min<long double>( 0.15L, effect( GIANT_SLAYER, profile.ranks[GIANT_SLAYER] ) / 150.0L );

        case OPENING_BLOW:
        case DISCIPLINE:
            return 1.0L + std::min<long double>( 0.15L, effect( UNYIELDING, profile.ranks[UNYIELDING] ) / 220.0L );
        case UNYIELDING:
            return 1.0L
                   + std::min<long double>( 0.15L, ( effect( OPENING_BLOW, profile.ranks[OPENING_BLOW] )
                                                     + effect( DISCIPLINE, profile.ranks[DISCIPLINE] ) )
                                                       / 260.0L );

        case SORCERY: {
            const long double strongestElement = std::max( { effect( PYROMANCY, profile.ranks[PYROMANCY] ),
                                                             effect( CRYOMANCY, profile.ranks[CRYOMANCY] ),
                                                             effect( STORMCRAFT, profile.ranks[STORMCRAFT] ),
                                                             effect( CATACLYSM, profile.ranks[CATACLYSM] ) } );
            return 1.0L + std::min<long double>( 0.20L, strongestElement / 100.0L );
        }
        case PYROMANCY:
        case CRYOMANCY:
        case STORMCRAFT:
        case CATACLYSM:
            return 1.0L + std::min<long double>( 0.20L, effect( SORCERY, profile.ranks[SORCERY] ) / 100.0L
                                                          + effect( ARCANE_PIERCING, profile.ranks[ARCANE_PIERCING] ) / 300.0L );
        case ARCANE_PIERCING:
            return 1.0L + std::min<long double>( 0.15L, effect( SORCERY, profile.ranks[SORCERY] ) / 150.0L );

        case IRON_SKIN:
            return 1.0L + std::min<long double>( 0.15L,
                                                ( effect( ARROW_WARD, profile.ranks[ARROW_WARD] )
                                                  + effect( MELEE_GUARD, profile.ranks[MELEE_GUARD] ) )
                                                    / 250.0L );
        case ARROW_WARD:
        case MELEE_GUARD:
            return 1.0L + std::min<long double>( 0.15L, effect( IRON_SKIN, profile.ranks[IRON_SKIN] ) / 150.0L );

        case SPELL_WARD: {
            const long double strongestElementWard = std::max( { effect( FIRE_WARD, profile.ranks[FIRE_WARD] ),
                                                                 effect( COLD_WARD, profile.ranks[COLD_WARD] ),
                                                                 effect( STORM_WARD, profile.ranks[STORM_WARD] ),
                                                                 effect( CATACLYSM_WARD, profile.ranks[CATACLYSM_WARD] ) } );
            return 1.0L + std::min<long double>( 0.15L, strongestElementWard / 120.0L );
        }
        case FIRE_WARD:
        case COLD_WARD:
        case STORM_WARD:
        case CATACLYSM_WARD:
            return 1.0L + std::min<long double>( 0.15L, effect( SPELL_WARD, profile.ranks[SPELL_WARD] ) / 150.0L );

        case CRITICAL_TRAINING:
            return 1.0L + std::min<long double>( 0.22L, effect( BRUTAL_CRITICALS, profile.ranks[BRUTAL_CRITICALS] ) / 250.0L );
        case BRUTAL_CRITICALS:
            return 1.0L + std::min<long double>( 0.22L, effect( CRITICAL_TRAINING, profile.ranks[CRITICAL_TRAINING] ) / 50.0L );
        default:
            return 1.0L;
        }
    }

    long double autoBuyPlaystyleFactor( const Profile & profile, const size_t id )
    {
        const size_t targetTab = id / upgradesPerTab;
        uint64_t targetTabUses = 0;
        uint64_t targetTabInvestment = 0;
        uint64_t maximumTabUses = 0;

        for ( size_t tab = 0; tab < tabNames.size(); ++tab ) {
            uint64_t tabUses = 0;
            const size_t firstUpgrade = tab * upgradesPerTab;
            for ( size_t offset = 0; offset < upgradesPerTab; ++offset ) {
                const size_t upgradeId = firstUpgrade + offset;
                tabUses = saturatedAdd( tabUses, profile.useCounts[upgradeId] );
                if ( tab == targetTab ) {
                    targetTabInvestment = saturatedAdd( targetTabInvestment, rankInvestment( upgradeId, profile.ranks[upgradeId] ) );
                }
            }

            maximumTabUses = std::max( maximumTabUses, tabUses );
            if ( tab == targetTab ) {
                targetTabUses = tabUses;
            }
        }

        // The Steward learns a broad combat style rather than only chasing one frequently-triggered
        // doctrine. Battle telemetry supplies the stronger signal, while existing investment gives a
        // smaller commitment bonus so a manually-started build continues to develop coherently.
        const long double activityBonus
            = std::min( 0.14L, std::log1p( static_cast<long double>( targetTabUses ) ) / 32.0L );
        const long double commitmentBonus
            = std::min( 0.08L, std::log1p( static_cast<long double>( targetTabInvestment ) ) / 38.0L );
        const long double signatureBonus = targetTabUses > 0 && targetTabUses == maximumTabUses ? 0.08L : 0.0L;

        return 1.0L + activityBonus + commitmentBonus + signatureBonus;
    }

    long double autoBuyMarginalReturn( const Profile & profile, const size_t id )
    {
        const uint64_t rank = profile.ranks[id];
        if ( !doctrineCanAdvance( id, rank ) || ( id == BRUTAL_CRITICALS && profile.ranks[CRITICAL_TRAINING] == 0 ) ) {
            return 0.0L;
        }

        const long double utility = autoBuyUtility( profile, id ) + ascensionMilestoneUtility( id, rank );
        return utility * autoBuyActivityFactor( profile, id ) * autoBuySynergyFactor( profile, id )
               * autoBuyPlaystyleFactor( profile, id ) / static_cast<long double>( cost( id, rank ) );
    }

    size_t stewardPackagePartner( const Profile & profile, const size_t id )
    {
        std::array<size_t, 6> candidates{};
        candidates.fill( upgradeCount );
        size_t candidateCount = 0;

        const auto addCandidate = [&candidates, &candidateCount, id]( const size_t candidate ) {
            if ( candidate != id && candidateCount < candidates.size() ) {
                candidates[candidateCount++] = candidate;
            }
        };

        switch ( id ) {
        case BLOOD_DRINKER:
        case REAPER:
        case REGENERATION:
            addCandidate( BLOOD_DRINKER );
            addCandidate( REAPER );
            addCandidate( REGENERATION );
            break;
        case MARKSMAN:
        case CLOSE_QUARTERS:
            addCandidate( MARKSMAN );
            addCandidate( CLOSE_QUARTERS );
            break;
        case EXECUTIONER:
        case RUTHLESS:
            addCandidate( EXECUTIONER );
            addCandidate( RUTHLESS );
            break;
        case GIANT_SLAYER:
        case BULWARK:
            addCandidate( GIANT_SLAYER );
            addCandidate( BULWARK );
            break;
        case FRENZY:
        case LAST_STAND:
            addCandidate( FRENZY );
            addCandidate( LAST_STAND );
            break;
        case OPENING_BLOW:
        case DISCIPLINE:
        case UNYIELDING:
            addCandidate( OPENING_BLOW );
            addCandidate( DISCIPLINE );
            addCandidate( UNYIELDING );
            break;
        case SORCERY:
        case PYROMANCY:
        case CRYOMANCY:
        case STORMCRAFT:
        case CATACLYSM:
        case ARCANE_PIERCING:
            addCandidate( SORCERY );
            addCandidate( PYROMANCY );
            addCandidate( CRYOMANCY );
            addCandidate( STORMCRAFT );
            addCandidate( CATACLYSM );
            addCandidate( ARCANE_PIERCING );
            break;
        case IRON_SKIN:
        case ARROW_WARD:
        case MELEE_GUARD:
            addCandidate( IRON_SKIN );
            addCandidate( ARROW_WARD );
            addCandidate( MELEE_GUARD );
            break;
        case SPELL_WARD:
        case FIRE_WARD:
        case COLD_WARD:
        case STORM_WARD:
        case CATACLYSM_WARD:
            addCandidate( SPELL_WARD );
            addCandidate( FIRE_WARD );
            addCandidate( COLD_WARD );
            addCandidate( STORM_WARD );
            addCandidate( CATACLYSM_WARD );
            break;
        case LEADERSHIP:
        case FORTUNE:
            addCandidate( LEADERSHIP );
            addCandidate( FORTUNE );
            break;
        case CRITICAL_TRAINING:
        case BRUTAL_CRITICALS:
            addCandidate( CRITICAL_TRAINING );
            addCandidate( BRUTAL_CRITICALS );
            break;
        default:
            break;
        }

        size_t best = upgradeCount;
        long double bestScore = 0.0L;
        for ( size_t index = 0; index < candidateCount; ++index ) {
            const size_t candidate = candidates[index];
            const long double marginal = autoBuyMarginalReturn( profile, candidate );
            if ( marginal <= 0.0L ) {
                continue;
            }

            const long double activityBonus
                = 1.0L + std::min<long double>( 0.10L, std::log1p( static_cast<long double>( profile.useCounts[candidate] ) ) / 40.0L );
            const long double score = marginal * activityBonus;
            if ( best == upgradeCount || score > bestScore
                 || ( score == bestScore && profile.ranks[candidate] < profile.ranks[best] ) ) {
                best = candidate;
                bestScore = score;
            }
        }

        return best;
    }

    struct StewardGoal
    {
        size_t id{ upgradeCount };
        uint64_t targetRank{ 0 };
        long double score{ 0.0L };
    };

    StewardGoal findStewardGoal( const Profile & profile )
    {
        const size_t goalHorizon = stewardPlanningHorizon( profile.level );
        StewardGoal bestGoal;

        for ( size_t id = 0; id < upgradeCount; ++id ) {
            Profile simulated = profile;
            long double totalUtility = 0.0L;
            uint64_t totalCost = 0;
            size_t plannedRanks = 0;

            for ( size_t step = 0; step < goalHorizon; ++step ) {
                const long double marginal = autoBuyMarginalReturn( simulated, id );
                if ( marginal <= 0.0L ) {
                    break;
                }

                const uint64_t rankCost = cost( id, simulated.ranks[id] );
                totalUtility += marginal * static_cast<long double>( rankCost );
                totalCost = saturatedAdd( totalCost, rankCost );
                ++simulated.ranks[id];
                ++plannedRanks;
            }

            if ( plannedRanks == 0 || totalCost == 0 ) {
                continue;
            }

            // Reward doctrines that stay efficient across several ranks instead of selecting a
            // flashy one-rank spike. The Steward also values a goal more highly when it has a
            // useful package partner available, so long-term plans build coherent combinations
            // rather than repeatedly tunneling one isolated doctrine.
            const long double durabilityBonus = 1.0L + static_cast<long double>( plannedRanks - 1 ) * 0.035L;
            const size_t packagePartner = stewardPackagePartner( simulated, id );
            const long double packageBonus = packagePartner < upgradeCount ? ( profile.ranks[packagePartner] > 0 ? 1.12L : 1.08L ) : 1.0L;
            const long double score = totalUtility / static_cast<long double>( totalCost ) * durabilityBonus * packageBonus;
            if ( bestGoal.id == upgradeCount || score > bestGoal.score
                 || ( score == bestGoal.score && simulated.ranks[id] < bestGoal.targetRank ) ) {
                bestGoal.id = id;
                bestGoal.targetRank = simulated.ranks[id];
                bestGoal.score = score;
            }
        }

        return bestGoal;
    }

    void autoBuy( Profile & profile )
    {
        // Cache doctrine scores so a large stored point balance does not recalculate every hall on
        // every purchase. Synergy partners and the purchased doctrine's whole hall are refreshed
        // below because those are the only rank-dependent scores that can change immediately.
        std::array<long double, upgradeCount> marginalReturns{};
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            marginalReturns[id] = autoBuyMarginalReturn( profile, id );
        }

        StewardGoal goal = findStewardGoal( profile );

        for ( size_t purchases = 0; purchases < 100000; ++purchases ) {
            size_t bestId = upgradeCount;
            long double bestReturn = 0;
            const size_t packagePartner = goal.id < upgradeCount ? stewardPackagePartner( profile, goal.id ) : upgradeCount;

            for ( size_t id = 0; id < upgradeCount; ++id ) {
                if ( profile.points < cost( id, profile.ranks[id] ) ) {
                    continue;
                }

                if ( marginalReturns[id] <= 0.0L ) {
                    continue;
                }

                const bool pursuingGoal = id == goal.id && profile.ranks[id] < goal.targetRank;
                const bool supportingPackage = id == packagePartner;
                const long double planPreference = pursuingGoal ? 1.18L : supportingPackage ? 1.12L : 1.0L;
                const long double candidateReturn = marginalReturns[id] * planPreference;

                if ( bestId == upgradeCount || candidateReturn > bestReturn
                     || ( candidateReturn == bestReturn && profile.ranks[id] < profile.ranks[bestId] ) ) {
                    bestReturn = candidateReturn;
                    bestId = id;
                }
            }

            if ( bestId == upgradeCount || !buy( profile, bestId ) ) {
                break;
            }

            marginalReturns[bestId] = autoBuyMarginalReturn( profile, bestId );

            const auto refresh = [&profile, &marginalReturns]( const size_t id ) {
                marginalReturns[id] = autoBuyMarginalReturn( profile, id );
            };

            // Hall-level playstyle commitment changes whenever any rank in that hall changes.
            const size_t purchasedTabStart = ( bestId / upgradesPerTab ) * upgradesPerTab;
            for ( size_t offset = 0; offset < upgradesPerTab; ++offset ) {
                refresh( purchasedTabStart + offset );
            }

            if ( bestId == BLOOD_DRINKER || bestId == REAPER || bestId == REGENERATION ) {
                refresh( BLOOD_DRINKER );
                refresh( REAPER );
                refresh( REGENERATION );
            }
            if ( bestId == CRITICAL_TRAINING || bestId == BRUTAL_CRITICALS ) {
                refresh( CRITICAL_TRAINING );
                refresh( BRUTAL_CRITICALS );
            }
            if ( bestId == MARKSMAN || bestId == CLOSE_QUARTERS ) {
                refresh( MARKSMAN );
                refresh( CLOSE_QUARTERS );
            }
            if ( bestId == EXECUTIONER || bestId == RUTHLESS ) {
                refresh( EXECUTIONER );
                refresh( RUTHLESS );
            }
            if ( bestId == FRENZY || bestId == LAST_STAND ) {
                refresh( FRENZY );
                refresh( LAST_STAND );
            }
            if ( bestId == GIANT_SLAYER || bestId == BULWARK ) {
                refresh( GIANT_SLAYER );
                refresh( BULWARK );
            }
            if ( bestId == OPENING_BLOW || bestId == DISCIPLINE || bestId == UNYIELDING ) {
                refresh( OPENING_BLOW );
                refresh( DISCIPLINE );
                refresh( UNYIELDING );
            }
            if ( bestId == SORCERY || bestId == PYROMANCY || bestId == CRYOMANCY || bestId == STORMCRAFT || bestId == CATACLYSM
                 || bestId == ARCANE_PIERCING ) {
                refresh( SORCERY );
                refresh( PYROMANCY );
                refresh( CRYOMANCY );
                refresh( STORMCRAFT );
                refresh( CATACLYSM );
                refresh( ARCANE_PIERCING );
            }
            if ( bestId == IRON_SKIN || bestId == ARROW_WARD || bestId == MELEE_GUARD ) {
                refresh( IRON_SKIN );
                refresh( ARROW_WARD );
                refresh( MELEE_GUARD );
            }
            if ( bestId == SPELL_WARD || bestId == FIRE_WARD || bestId == COLD_WARD || bestId == STORM_WARD || bestId == CATACLYSM_WARD ) {
                refresh( SPELL_WARD );
                refresh( FIRE_WARD );
                refresh( COLD_WARD );
                refresh( STORM_WARD );
                refresh( CATACLYSM_WARD );
            }

            if ( goal.id == upgradeCount || profile.ranks[goal.id] >= goal.targetRank || marginalReturns[goal.id] <= 0.0L ) {
                goal = findStewardGoal( profile );
            }
        }
    }

    std::string profilePath()
    {
        return System::concatPath( fheroes2::RPG::dataDirectory(), "rpg_profile.dat" );
    }

    bool readProfile( const std::string & path, Profile & profile, std::set<uint64_t> & visitedTiles, bool * needsMigration = nullptr )
    {
        std::ifstream input( path );
        int version = 0;
        int autoBuyValue = 0;
        Profile candidate;
        if ( !( input >> version >> candidate.level >> candidate.experience >> candidate.progress >> candidate.points >> autoBuyValue )
             || ( version < 1 || version > profileVersion )
             || candidate.level == 0 || ( autoBuyValue != 0 && autoBuyValue != 1 ) ) {
            return false;
        }

        if ( version < 3 ) {
            std::array<uint64_t, 5> oldRanks{};
            for ( uint64_t & rank : oldRanks ) {
                if ( !( input >> rank ) ) {
                    return false;
                }
            }
            candidate.ranks[ARMS_TRAINING] = oldRanks[0];
            candidate.ranks[ARMOR_TRAINING] = oldRanks[1];
            candidate.ranks[SORCERY] = oldRanks[2];
            candidate.ranks[VETERAN_CORE] = oldRanks[3];
            candidate.ranks[SPELL_WARD] = oldRanks[4];
        }
        else {
            for ( uint64_t & rank : candidate.ranks ) {
                if ( !( input >> rank ) ) {
                    return false;
                }
            }
        }
        if ( version >= 2 && !( input >> candidate.fieldExperience >> candidate.offlineExperience ) ) {
            return false;
        }
        if ( version >= 3 && !( input >> candidate.battleExperience >> candidate.adventureExperience >> candidate.heroExperience ) ) {
            return false;
        }
        std::set<uint64_t> candidateVisited;
        if ( version >= 4 ) {
            size_t count = 0;
            if ( !( input >> count ) || count > 1000000 ) {
                return false;
            }
            for ( size_t i = 0; i < count; ++i ) {
                uint64_t key = 0;
                if ( !( input >> key ) || !candidateVisited.insert( key ).second ) {
                    return false;
                }
            }
        }
        if ( version >= 5 ) {
            for ( uint64_t & uses : candidate.useCounts ) {
                if ( !( input >> uses ) ) {
                    return false;
                }
            }
        }
        if ( version < 2 ) {
            // Version 1 predates source ledgers. Its Renown came from normal in-game progression,
            // so classify it as field/hero Renown before rewriting it to the current profile format.
            candidate.fieldExperience = candidate.experience;
            candidate.heroExperience = candidate.experience;
        }
        else if ( version == 2 ) {
            candidate.heroExperience = candidate.fieldExperience;
        }

        // Version 6 replaced the previous economy-oriented tree with combat affixes.
        // Only pre-v6 profiles need that one-time full respec.
        if ( version < 6 ) {
            candidate.points = saturatedMultiply( candidate.level - 1, pointsPerLevel );
            candidate.ranks.fill( 0 );
            candidate.useCounts.fill( 0 );
        }

        candidate.autoBuy = version < 6 ? false : autoBuyValue != 0;

        if ( version == 6 ) {
            // Version 6 used the harder 250k + 100k/level progression curve. Validate every
            // invariant against that historical progress threshold before granting levels made
            // affordable by the new curve.
            if ( !isLegacyV7ProfileStateValid( candidate, legacyV6XpToNextLevel( candidate.level ) ) ) {
                return false;
            }
        }
        else if ( version == 7 ) {
            // Version 7 used the 150k + 50k/level progression curve and escalating doctrine costs.
            if ( !isLegacyV7ProfileStateValid( candidate, legacyV7XpToNextLevel( candidate.level ) ) ) {
                return false;
            }
        }
        else if ( version == profileVersion && !isCurrentProfileStateValid( candidate ) ) {
            return false;
        }

        if ( version < profileVersion ) {
            uint64_t oldInvested = 0;
            uint64_t newInvested = 0;
            for ( size_t id = 0; id < upgradeCount; ++id ) {
                oldInvested = saturatedAdd( oldInvested, legacyRankInvestment( id, candidate.ranks[id] ) );
                newInvested = saturatedAdd( newInvested, rankInvestment( id, candidate.ranks[id] ) );
            }
            if ( oldInvested > newInvested ) {
                candidate.points = saturatedAdd( candidate.points, oldInvested - newInvested );
            }

            Profile simulated;
            simulated.level = 1;
            simulated.progress = candidate.experience;
            consumeAffordableLevelsWithCarry( simulated );

            const uint64_t newLevel = std::max( candidate.level, simulated.level );
            const uint64_t gainedLevels = newLevel > candidate.level ? newLevel - candidate.level : 0;
            candidate.level = newLevel;
            candidate.progress = simulated.level >= candidate.level
                                     ? simulated.progress
                                     : std::min( candidate.progress, xpToNextLevel( candidate.level ) == 0 ? 0 : xpToNextLevel( candidate.level ) - 1 );
            candidate.points = saturatedAdd( candidate.points, saturatedMultiply( gainedLevels, pointsPerLevel ) );

            if ( ( gainedLevels > 0 || oldInvested > newInvested ) && candidate.autoBuy ) {
                autoBuy( candidate );
            }

            if ( !isCurrentProfileStateValid( candidate ) ) {
                return false;
            }
        }

        // Profile snapshots are single-record files. Extra tokens indicate a partial append,
        // manual damage or a future format that this build must not silently reinterpret.
        input >> std::ws;
        if ( !input.eof() ) {
            return false;
        }

        profile = std::move( candidate );
        visitedTiles = std::move( candidateVisited );
        if ( needsMigration != nullptr ) {
            *needsMigration = version < profileVersion;
        }
        return true;
    }

    void saveProfile( const bool preserveRecoveryBackup = false )
    {
        // Never replace a recoverable on-disk profile with an impossible runtime state. If a
        // future gameplay bug breaks one of the closed RPG ledgers, keep the last good snapshot.
        if ( !isCurrentProfileStateValid( playerProfile ) ) {
            ERROR_LOG( "Refusing to save invalid RPG profile state." )
            return;
        }

        const std::string path = profilePath();
        const std::string tempPath = path + ".tmp";
        const std::string backupPath = path + ".bak";

        std::ofstream output( tempPath, std::ios::trunc );
        if ( !output ) {
            ERROR_LOG( "Unable to write RPG profile." )
            return;
        }

        output << profileVersion << ' ' << playerProfile.level << ' ' << playerProfile.experience << ' ' << playerProfile.progress << ' ' << playerProfile.points << ' '
               << static_cast<int>( playerProfile.autoBuy );
        for ( const uint64_t rank : playerProfile.ranks ) {
            output << ' ' << rank;
        }
        output << ' ' << playerProfile.fieldExperience << ' ' << playerProfile.offlineExperience << ' ' << playerProfile.battleExperience << ' '
               << playerProfile.adventureExperience << ' ' << playerProfile.heroExperience << ' ' << visitedActionTiles.size();
        for ( const uint64_t key : visitedActionTiles ) {
            output << ' ' << key;
        }
        for ( const uint64_t uses : playerProfile.useCounts ) {
            output << ' ' << uses;
        }
        output << '\n';
        output.close();
        if ( !output ) {
            System::Unlink( tempPath );
            ERROR_LOG( "Unable to finish RPG profile." )
            return;
        }

        const bool hadOriginal = System::IsFile( path );
        bool movedOriginalToBackup = false;
        if ( hadOriginal ) {
            if ( preserveRecoveryBackup ) {
                // Recovery loaded a valid fallback. Never rotate an invalid or older primary
                // over the known-good persistent backup.
                if ( !System::Unlink( path ) ) {
                    ERROR_LOG( "Unable to remove superseded RPG profile." )
                    return;
                }
            }
            else {
                System::Unlink( backupPath );
                if ( std::rename( path.c_str(), backupPath.c_str() ) != 0 ) {
                    ERROR_LOG( "Unable to rotate RPG profile backup." )
                    return;
                }
                movedOriginalToBackup = true;
            }
        }

        if ( std::rename( tempPath.c_str(), path.c_str() ) != 0 ) {
            if ( movedOriginalToBackup ) {
                std::rename( backupPath.c_str(), path.c_str() );
            }
            // tempPath is already a complete, validated write candidate. Keep it so the
            // next launch can recover the newest profile instead of discarding good data.
            ERROR_LOG( "Unable to promote temporary RPG profile." )
            return;
        }
        // Keep the previous valid profile at backupPath as a resilient fallback in case of corruption or crash.
    }

    const Profile * getProfile( const PlayerColor color )
    {
        if ( color == activePlayerColor && color != PlayerColor::NONE ) {
            return &playerProfile;
        }

        const auto enemy = enemyProfiles.find( color );
        return enemy == enemyProfiles.end() ? nullptr : &enemy->second;
    }

    uint64_t xpToNextLevelForCurve( const uint64_t level, const uint64_t baseExperience, const uint64_t perLevelExperience )
    {
        return level > ( std::numeric_limits<uint64_t>::max() - baseExperience ) / perLevelExperience
                   ? std::numeric_limits<uint64_t>::max()
                   : baseExperience + level * perLevelExperience;
    }

    uint64_t xpToNextLevel( const uint64_t level )
    {
        return xpToNextLevelForCurve( level, baseLevelExperience, experiencePerLevel );
    }

    uint64_t legacyV7XpToNextLevel( const uint64_t level )
    {
        return xpToNextLevelForCurve( level, legacyV7BaseLevelExperience, legacyV7ExperiencePerLevel );
    }

    uint64_t legacyV6XpToNextLevel( const uint64_t level )
    {
        return xpToNextLevelForCurve( level, legacyV6BaseLevelExperience, legacyV6ExperiencePerLevel );
    }

    bool canGainLevels( const Profile & profile, const uint64_t count )
    {
        if ( count == 0 ) {
            return true;
        }
        if ( count > std::numeric_limits<uint64_t>::max() - profile.level
             || profile.level > ( std::numeric_limits<uint64_t>::max() - baseLevelExperience ) / experiencePerLevel ) {
            return false;
        }

        const uint64_t firstCost = xpToNextLevel( profile.level );
        if ( firstCost == 0 || count > profile.progress / firstCost ) {
            return false;
        }

        const uint64_t remaining = profile.progress - count * firstCost;
        constexpr uint64_t triangularStep = experiencePerLevel / 2;
        static_assert( experiencePerLevel % 2 == 0 );
        return count == 1 || count <= remaining / triangularStep / ( count - 1 );
    }

    uint64_t xpForLevels( const Profile & profile, const uint64_t count )
    {
        return saturatedAdd( saturatedMultiply( count, xpToNextLevel( profile.level ) ),
                             saturatedMultiply( experiencePerLevel, saturatedTriangular( count ) ) );
    }

    uint64_t consumeAffordableLevelsWithCarry( Profile & profile )
    {
        const uint64_t firstCost = xpToNextLevel( profile.level );
        if ( firstCost == 0 || profile.progress < firstCost ) {
            return 0;
        }

        uint64_t low = 0;
        uint64_t high = std::min( saturatedAdd( profile.progress / firstCost, 1 ),
                                  std::numeric_limits<uint64_t>::max() - profile.level );
        while ( low < high ) {
            const uint64_t middle = low + ( high - low + 1 ) / 2;
            if ( canGainLevels( profile, middle ) ) {
                low = middle;
            }
            else {
                high = middle - 1;
            }
        }

        if ( low == 0 ) {
            return 0;
        }

        profile.progress -= xpForLevels( profile, low );
        profile.level += low;
        profile.points = saturatedAdd( profile.points, saturatedMultiply( low, pointsPerLevel ) );
        return low;
    }

    void drawText( const std::string & value, const int32_t x, const int32_t y, const int32_t width, const fheroes2::FontType & font )
    {
        fheroes2::Text( value, font ).draw( x, y, width, fheroes2::Display::instance() );
    }

    void drawSingleLine( const std::string & value, const int32_t x, const int32_t y, const int32_t width, const fheroes2::FontType & font )
    {
        fheroes2::Text line( value, font );
        line.fitToOneRow( width );
        line.draw( x, y, fheroes2::Display::instance() );
    }

    std::string formatDecimal( const long double value )
    {
        std::ostringstream text;
        text << std::fixed << std::setprecision( 2 ) << static_cast<double>( value );
        std::string result = text.str();
        while ( result.size() > 1 && result.back() == '0' ) {
            result.pop_back();
        }
        if ( !result.empty() && result.back() == '.' ) {
            result.pop_back();
        }
        return result;
    }

    std::string formatEffect( const long double value )
    {
        return formatDecimal( value ) + '%';
    }

    void drawBeveledPanel( const fheroes2::Rect & roi, const bool inset )
    {
        fheroes2::Display & display = fheroes2::Display::instance();
        const uint8_t face = fheroes2::GetColorId( 93, 67, 42 );
        const uint8_t highlight = fheroes2::GetColorId( 222, 184, 92 );
        const uint8_t shadow = fheroes2::GetColorId( 49, 35, 24 );
        const uint8_t topLeft = inset ? shadow : highlight;
        const uint8_t bottomRight = inset ? highlight : shadow;

        fheroes2::Fill( display, roi.x, roi.y, roi.width, roi.height, face );
        fheroes2::Fill( display, roi.x, roi.y, roi.width, 2, topLeft );
        fheroes2::Fill( display, roi.x, roi.y, 2, roi.height, topLeft );
        fheroes2::Fill( display, roi.x, roi.y + roi.height - 2, roi.width, 2, bottomRight );
        fheroes2::Fill( display, roi.x + roi.width - 2, roi.y, 2, roi.height, bottomRight );
    }

    std::string shortEffectSummary( const size_t id, const uint64_t rank )
    {
        const long double value = effect( id, rank );
        switch ( id ) {
        case ARMS_TRAINING: return "+" + formatNumber( diminishingFlatStatBonus( rank ) ) + " Attack";
        case ARMOR_TRAINING: return "+" + formatNumber( diminishingFlatStatBonus( rank ) ) + " Defense";
        case VETERAN_CORE: return "+" + formatNumber( diminishingFlatStatBonus( rank ) ) + " Attack & Defense";
        case BLOOD_DRINKER: return formatEffect( value ) + " life steal";
        case REAPER: return formatEffect( value ) + " slain-HP healing";
        case FEROCITY: return "+" + formatEffect( value ) + " creature damage";
        case MARKSMAN: return "+" + formatEffect( value ) + " ranged damage";
        case BRAWLER: return "+" + formatEffect( value ) + " melee damage";
        case EXECUTIONER: return "+" + formatEffect( value ) + " vs wounded";
        case OPENING_BLOW: return "+" + formatEffect( value ) + " at full HP";
        case GIANT_SLAYER: return "+" + formatEffect( value ) + " while outnumbered";
        case OVERWHELM: return "+" + formatEffect( value ) + " while outnumbering";
        case FRENZY: return "+" + formatEffect( value ) + " below half HP";
        case DISCIPLINE: return "+" + formatEffect( value ) + " at full HP";
        case ARMOR_PIERCING: return formatEffect( value ) + " physical resistance ignored";
        case IRON_SKIN: return formatEffect( value ) + " physical reduction";
        case ARROW_WARD: return formatEffect( value ) + " ranged reduction";
        case MELEE_GUARD: return formatEffect( value ) + " melee reduction";
        case LAST_STAND: return formatEffect( value ) + " reduction below half HP";
        case BULWARK: return formatEffect( value ) + " reduction while outnumbered";
        case SORCERY: return "+" + formatEffect( value ) + " spell damage";
        case PYROMANCY: return "+" + formatEffect( value ) + " fire damage";
        case CRYOMANCY: return "+" + formatEffect( value ) + " cold damage";
        case STORMCRAFT: return "+" + formatEffect( value ) + " lightning damage";
        case CATACLYSM: return "+" + formatEffect( value ) + " area spell damage";
        case SPELL_WARD: return formatEffect( value ) + " spell reduction";
        case FIRE_WARD: return formatEffect( value ) + " fire reduction";
        case COLD_WARD: return formatEffect( value ) + " cold reduction";
        case STORM_WARD: return formatEffect( value ) + " lightning reduction";
        case CATACLYSM_WARD: return formatEffect( value ) + " area spell reduction";
        case LEADERSHIP: return "+" + formatNumber( static_cast<uint64_t>( value ) ) + " Morale";
        case FORTUNE: return "+" + formatNumber( static_cast<uint64_t>( value ) ) + " Luck";
        case REGENERATION: return formatEffect( value ) + " turn regeneration";
        case CRITICAL_TRAINING: return formatEffect( value ) + " crit chance";
        case BRUTAL_CRITICALS: return "+" + formatEffect( 50.0L + value ) + " crit damage";
        case EVASION: return formatEffect( value ) + " evade chance";
        case ARCANE_PIERCING: return formatEffect( value ) + " spell resistance ignored";
        case CLOSE_QUARTERS: return formatEffect( value ) + " melee penalty recovered";
        case UNYIELDING: return formatEffect( value ) + " reduction at full HP";
        case RUTHLESS: return "+" + formatEffect( value ) + " vs targets below half HP";
        default: return formatEffect( value );
        }
    }

    uint64_t totalSpentPoints( const Profile & profile )
    {
        uint64_t total = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            total = saturatedAdd( total, rankInvestment( id, profile.ranks[id] ) );
        }
        return total;
    }

    uint64_t totalSpentPointsOnUpgrade( const Profile & profile, const size_t id )
    {
        if ( id >= upgradeCount ) {
            return 0;
        }
        return rankInvestment( id, profile.ranks[id] );
    }

    void respecProfile( Profile & profile )
    {
        const uint64_t refund = totalSpentPoints( profile );
        profile.points = saturatedAdd( profile.points, refund );
        profile.ranks.fill( 0 );
        profile.useCounts.fill( 0 );
        profile.autoBuy = false;
        saveProfile();
    }

    void showUpgradeDetails( const size_t id )
    {
        if ( id >= upgradeCount ) {
            return;
        }

        const uint64_t rank = playerProfile.ranks[id];
        const long double currentEffect = effect( id, rank );
        const bool canAdvance = doctrineCanAdvance( id, rank );
        const uint8_t ascensionTier = ascensionTierForRank( rank );
        std::string message = upgradeDetails[id];
        message += "\n\nCurrent rank: " + formatNumber( rank );
        message += "\nCurrent: " + shortEffectSummary( id, rank );
        message += "\nAscension: " + std::string( ascensionTierLabel( ascensionTier ) );
        if ( ascensionTier > 0 ) {
            message += " - " + std::string( ascensionNames[id] );
        }
        message += "\nEvolution: " + std::string( ascensionChannelDescription( ascensionChannels[id] ) );
        const uint64_t nextMilestone = nextAscensionRank( rank );
        if ( nextMilestone > 0 ) {
            message += "\nNext Ascension: Rank " + formatNumber( nextMilestone );
        }
        else {
            message += "\nAscension IV mastered.";
        }
        const bool prerequisiteMet = id != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;
        if ( canAdvance ) {
            const long double nextEffect = effect( id, rank + 1 );
            message += "\nNext rank: " + shortEffectSummary( id, rank + 1 );
            const uint8_t nextAscensionTier = ascensionTierForRank( rank + 1 );
            if ( nextAscensionTier > ascensionTier ) {
                message += "\nUNLOCKS: " + std::string( ascensionTierLabel( nextAscensionTier ) ) + " - " + ascensionNames[id];
            }

            const uint64_t flatGain = nextEffect > currentEffect ? static_cast<uint64_t>( nextEffect - currentEffect ) : 0;
            const uint64_t currentFlatStat = diminishingFlatStatBonus( rank );
            const uint64_t nextFlatStat = diminishingFlatStatBonus( rank + 1 );
            const uint64_t actualFlatGain = nextFlatStat >= currentFlatStat ? nextFlatStat - currentFlatStat : 0;
            if ( nextEffect <= currentEffect ) {
                message += "\nPrimary effect: capped; this rank advances Ascension progress.";
            }
            else if ( id == ARMS_TRAINING ) {
                message += "\nNext-rank gain: +" + formatNumber( actualFlatGain ) + " Attack";
            }
            else if ( id == ARMOR_TRAINING ) {
                message += "\nNext-rank gain: +" + formatNumber( actualFlatGain ) + " Defense";
            }
            else if ( id == VETERAN_CORE ) {
                message += "\nNext-rank gain: +" + formatNumber( actualFlatGain ) + " Attack & Defense";
            }
            else if ( id == LEADERSHIP ) {
                message += "\nNext-rank gain: +" + formatNumber( flatGain ) + " Morale";
            }
            else if ( id == FORTUNE ) {
                message += "\nNext-rank gain: +" + formatNumber( flatGain ) + " Luck";
            }
            else {
                message += "\nNext-rank gain: +" + formatEffect( nextEffect - currentEffect );
            }

            const uint64_t price = cost( id, rank );
            message += "\nNext rank costs: " + formatNumber( price ) + " points";
            if ( price > 0 && nextEffect > currentEffect ) {
                const long double gainPerPoint = ( nextEffect - currentEffect ) / static_cast<long double>( price );
                if ( id == ARMS_TRAINING ) {
                    message += "\nDiminishing stat gain: +" + formatNumber( actualFlatGain ) + " actual Attack this rank";
                }
                else if ( id == ARMOR_TRAINING ) {
                    message += "\nDiminishing stat gain: +" + formatNumber( actualFlatGain ) + " actual Defense this rank";
                }
                else if ( id == VETERAN_CORE ) {
                    message += "\nDiminishing stat gain: +" + formatNumber( actualFlatGain ) + " actual Attack & Defense this rank";
                }
                else if ( id == LEADERSHIP ) {
                    message += "\nGain per point: " + formatDecimal( gainPerPoint ) + " Morale";
                }
                else if ( id == FORTUNE ) {
                    message += "\nGain per point: " + formatDecimal( gainPerPoint ) + " Luck";
                }
                else {
                    message += "\nGain per point: " + formatEffect( gainPerPoint );
                }
            }
            if ( !prerequisiteMet ) {
                message += "\nPurchase status: LOCKED - requires Critical Training rank 1.";
            }
            else if ( playerProfile.points < price ) {
                message += "\nPurchase status: NEED " + formatNumber( price - playerProfile.points ) + " more guild points.";
            }
            else {
                message += "\nPurchase status: READY.";
            }
        }
        else {
            message += "\nPurchase status: MAX - no primary growth or Ascension milestone remains.";
        }
        const uint64_t invested = totalSpentPointsOnUpgrade( playerProfile, id );
        if ( invested > 0 ) {
            message += "\nPoints invested in doctrine: " + formatNumber( invested );
        }
        if ( playerProfile.useCounts[id] > 0 ) {
            message += "\nBattle triggers: " + formatNumber( playerProfile.useCounts[id] );
        }
        if ( const char * resonance = doctrineResonanceHint( id ); resonance != nullptr ) {
            message += "\nResonance: " + std::string( resonance );
        }
        message += "\nScaling: Diminishing returns - each later primary rank adds less than the previous one.";
        message += "\nAvailable points: " + formatNumber( playerProfile.points );
        fheroes2::showStandardTextMessage( upgrades[id].name, std::move( message ), Dialog::ZERO );
    }

    void showTabDetails( const size_t tabIndex )
    {
        if ( tabIndex >= tabNames.size() ) {
            return;
        }

        constexpr std::array<const char *, 8> tabDescriptions{
            "Fundamental martial training for your forces. Direct creature Attack and Defense bonuses, and sustaining stacks through lifesteal and battle triage.",
            "Core physical doctrines for dealing damage. Increases all creature strikes, specialized melee or ranged weapons, and target opportunism against wounded or untouched foes.",
            "Battlefield tactics and positional doctrines. Capitalizes on numbers, morale discipline, desperate frenzy below half health, and armor penetration.",
            "Defensive guard doctrines. Fortifies stacks against physical strikes, ranged volleys, and establishes resilient last stands and outnumbered bulwarks.",
            "Arcane spellcasting doctrines for spellcasting heroes. Empowers damage from all spells and specialized elemental disciplines: Fire, Cold, Lightning, and Cataclysm.",
            "Arcane warding doctrines. Shields friendly creature stacks against hero spell damage, specialized elemental damage, and devastating area storms.",
            "Command and morale doctrines. Blesses your army with high Morale, Luck, start-of-turn regeneration, and deadly Critical strikes.",
            "Advanced martial and arcane mastery. Grants agility to Evade creature attacks, Arcane Piercing through spell wards, Close Quarters melee recovery for archers, and Ruthless strikes."
        };

        std::string message = tabPanelNames[tabIndex];
        message += "\n\n";
        message += tabDescriptions[tabIndex];
        message += "\n\nDOCTRINES:";
        for ( size_t offset = 0; offset < upgradesPerTab; ++offset ) {
            const size_t id = tabIndex * upgradesPerTab + offset;
            const uint64_t rank = playerProfile.ranks[id];
            const bool canAdvance = doctrineCanAdvance( id, rank );
            const bool prerequisiteMet = id != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;

            message += "\n  " + std::string( upgrades[id].name ) + " - Rank " + formatNumber( rank ) + ": " + shortEffectSummary( id, rank );
            const uint8_t ascensionTier = ascensionTierForRank( rank );
            if ( ascensionTier > 0 ) {
                message += " [" + std::string( ascensionTierLabel( ascensionTier ) ) + ": " + ascensionNames[id] + "]";
            }
            if ( !canAdvance ) {
                message += " [MAX]";
            }
            else if ( !prerequisiteMet ) {
                message += " [LOCKED]";
            }
            else {
                const uint64_t price = cost( id, rank );
                message += playerProfile.points >= price ? " [BUY " + formatNumber( price ) + "]"
                                                         : " [NEED " + formatNumber( price - playerProfile.points ) + "]";
            }
        }
        fheroes2::showStandardTextMessage( tabNames[tabIndex], std::move( message ), Dialog::ZERO );
    }

    void showRoyalGuildHelp()
    {
        const std::string path = profilePath();
        const bool hasBackup = System::IsFile( path + ".bak" );
        const bool hasRecoverySnapshot = System::IsFile( path + ".tmp" );

        std::string message = "KEYBOARD\n";
        message += "\n1-8: Jump directly to a doctrine hall";
        message += "\nLeft/Right or Tab: Cycle doctrine halls";
        message += "\nUp/Down: Select a doctrine";
        message += "\nPageUp/PageDown/Home/End: Jump to first or last doctrine";
        message += "\nB, Enter, or Space: Buy the selected doctrine";
        message += "\nI: Inspect the selected doctrine";
        message += "\nO: Open the Royal Guild Overview";
        message += "\nK: Open Build Analytics";
        message += "\nV: Open Elite Rival Intel";
        message += "\nS or A: Toggle Steward auto-buy";
        message += "\nR: Respec doctrines";
        message += "\nH: Open this help";
        message += "\nF9 or Esc: Close the Royal Guild";

        message += "\n\nMOUSE & TOUCH";
        message += "\nClick/tap a doctrine row to inspect it and use BUY to purchase.";
        message += "\nRight-click/hold a doctrine hall to inspect all five doctrines.";
        message += "\nClick/tap the Renown panel to open the Overview.";
        message += "\nUse the wheel, scroll arrows, or scrollbar track to move through doctrine rows.";

        message += "\n\nPROFILE SAFETY";
        message += "\nData folder: " + fheroes2::RPG::dataDirectory();
        message += "\nPersistent backup: ";
        message += hasBackup ? "Ready" : "Not created yet";
        message += "\nPending recovery snapshot: ";
        message += hasRecoverySnapshot ? "Available" : "None";
        message += "\nPurchases, Steward changes, respecs, and RPG progression save automatically.";
        message += "\nValid .bak and .tmp snapshots can recover a damaged primary profile on startup.";

        fheroes2::showStandardTextMessage( "Royal Guild Help", std::move( message ), Dialog::ZERO );
    }

    void showBuildAnalytics()
    {
        const std::string path = profilePath();
        const uint64_t totalInvested = totalSpentPoints( playerProfile );
        const uint64_t earnedPoints = saturatedMultiply( playerProfile.level - 1, pointsPerLevel );

        std::string message = "BUILD ANALYTICS\n";
        message += "\nProfile Ledger: ";
        message += isCurrentProfileStateValid( playerProfile ) ? "VALID" : "INVALID - autosave protection active";
        message += "\nEarned Guild Points: " + formatNumber( earnedPoints );
        message += "\nInvested Guild Points: " + formatNumber( totalInvested );
        message += "\nAvailable Guild Points: " + formatNumber( playerProfile.points );

        std::array<uint64_t, tabNames.size()> investedByTab{};
        std::array<uint64_t, tabNames.size()> triggersByTab{};
        uint64_t totalTriggers = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            investedByTab[id / upgradesPerTab] = saturatedAdd( investedByTab[id / upgradesPerTab], rankInvestment( id, playerProfile.ranks[id] ) );
            triggersByTab[id / upgradesPerTab] = saturatedAdd( triggersByTab[id / upgradesPerTab], playerProfile.useCounts[id] );
            totalTriggers = saturatedAdd( totalTriggers, playerProfile.useCounts[id] );
        }

        const auto investmentFocus = std::max_element( investedByTab.begin(), investedByTab.end() );
        if ( investmentFocus != investedByTab.end() && *investmentFocus > 0 ) {
            message += "\nBuild Focus: "
                       + std::string( tabNames[static_cast<size_t>( std::distance( investedByTab.begin(), investmentFocus ) )] );
        }
        const auto activityFocus = std::max_element( triggersByTab.begin(), triggersByTab.end() );
        if ( activityFocus != triggersByTab.end() && *activityFocus > 0 ) {
            message += "\nCombat Focus: "
                       + std::string( tabNames[static_cast<size_t>( std::distance( triggersByTab.begin(), activityFocus ) )] )
                       + " (" + formatNumber( *activityFocus ) + " triggers)";
        }
        message += "\nTotal Doctrine Triggers: " + formatNumber( totalTriggers );

        size_t ascendedDoctrines = 0;
        uint64_t totalAscensionStages = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            const uint8_t tier = ascensionTierForRank( playerProfile.ranks[id] );
            if ( tier > 0 ) {
                ++ascendedDoctrines;
                totalAscensionStages = saturatedAdd( totalAscensionStages, tier );
            }
        }
        message += "\nAscended Doctrines: " + formatNumber( ascendedDoctrines ) + " / " + formatNumber( upgradeCount );
        message += "\nTotal Ascension Stages: " + formatNumber( totalAscensionStages );

        const StewardGoal stewardGoal = findStewardGoal( playerProfile );
        if ( stewardGoal.id < upgradeCount ) {
            message += "\n\nSTEWARD ROADMAP";
            message += "\nPrimary: " + std::string( upgrades[stewardGoal.id].name ) + " -> Rank " + formatNumber( stewardGoal.targetRank );
            const size_t packagePartner = stewardPackagePartner( playerProfile, stewardGoal.id );
            if ( packagePartner < upgradeCount ) {
                message += "\nPackage Partner: " + std::string( upgrades[packagePartner].name );
            }
            message += "\nPlanning Horizon: " + formatNumber( stewardPlanningHorizon( playerProfile.level ) ) + " ranks";
        }

        message += "\n\nACTIVE COMBAT RESONANCES";
        bool anyResonance = false;
        const auto addResonance = [&message, &anyResonance]( const char * name, const long double bonus, const char * condition ) {
            if ( bonus <= 0.0L ) {
                return;
            }
            anyResonance = true;
            message += "\n" + std::string( name ) + ": +" + formatEffect( bonus ) + " - " + condition;
        };

        addResonance( "Finisher", doctrinePairResonance( playerProfile, EXECUTIONER, RUTHLESS, 0.25L, 6.0L ), "damage vs targets below half HP" );
        addResonance( "Underdog", doctrinePairResonance( playerProfile, GIANT_SLAYER, BULWARK, 0.20L, 5.0L ), "offense/defense while outnumbered" );
        addResonance( "Last Fury", doctrinePairResonance( playerProfile, FRENZY, LAST_STAND, 0.20L, 5.0L ), "offense/defense below half HP" );
        addResonance( "Vanguard", doctrineTripleResonance( playerProfile, OPENING_BLOW, DISCIPLINE, UNYIELDING, 0.18L, 5.0L ),
                      "opening pressure while at full HP" );

        long double strongestSpellResonance = 0.0L;
        for ( size_t id = PYROMANCY; id <= CATACLYSM; ++id ) {
            strongestSpellResonance = std::max( strongestSpellResonance, doctrinePairResonance( playerProfile, SORCERY, id, 0.20L, 6.0L ) );
        }
        addResonance( "Spell Convergence", strongestSpellResonance, "matching elemental spell damage" );

        long double strongestWardResonance = 0.0L;
        for ( size_t id = FIRE_WARD; id <= CATACLYSM_WARD; ++id ) {
            strongestWardResonance = std::max( strongestWardResonance, doctrinePairResonance( playerProfile, SPELL_WARD, id, 0.20L, 6.0L ) );
        }
        addResonance( "Ward Matrix", strongestWardResonance, "matching elemental spell resistance" );

        if ( !anyResonance ) {
            message += "\nNone active yet. Pair complementary doctrines to unlock resonance bonuses.";
        }

        message += "\n\nPROFILE SNAPSHOTS";
        message += "\nPrimary: ";
        message += System::IsFile( path ) ? "Ready" : "Missing";
        message += "\nBackup (.bak): ";
        message += System::IsFile( path + ".bak" ) ? "Ready" : "Not created yet";
        message += "\nRecovery (.tmp): ";
        message += System::IsFile( path + ".tmp" ) ? "Available" : "None";

        fheroes2::showStandardTextMessage( "Build Analytics", std::move( message ), Dialog::ZERO );
    }

    void showRivalIntel()
    {
        std::array<size_t, rivalArchetypeNames.size()> archetypeCounts{};
        for ( const auto & [color, archetype] : eliteRivalArchetypes ) {
            static_cast<void>( color );
            const size_t index = static_cast<size_t>( archetype );
            if ( index > 0 && index <= archetypeCounts.size() ) {
                ++archetypeCounts[index - 1];
            }
        }

        std::string message = "ELITE RIVAL INTEL\n";
        message += "\nEncounter Chance / Hostile Kingdom: " + std::to_string( eliteRivalChanceForLevel( playerProfile.level ) ) + "%";
        message += "\nMaximum Coordinated Halls: " + formatNumber( eliteRivalFocusHallLimit( playerProfile.level ) );
        message += "\nElite Rivals This Map: " + formatNumber( eliteEnemyColors.size() );

        std::array<uint64_t, tabNames.size()> activityByTab{};
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            activityByTab[id / upgradesPerTab] = saturatedAdd( activityByTab[id / upgradesPerTab], playerProfile.useCounts[id] );
        }
        const auto observedFocus = std::max_element( activityByTab.begin(), activityByTab.end() );
        if ( observedFocus != activityByTab.end() && *observedFocus > 0 ) {
            message += "\nObserved Player Combat Focus: "
                       + std::string( tabNames[static_cast<size_t>( std::distance( activityByTab.begin(), observedFocus ) )] );
        }

        bool anyArchetype = false;
        for ( size_t index = 0; index < archetypeCounts.size(); ++index ) {
            if ( archetypeCounts[index] == 0 ) {
                continue;
            }

            if ( !anyArchetype ) {
                message += "\n\nARCHETYPES";
                anyArchetype = true;
            }
            message += "\n" + std::string( rivalArchetypeNames[index] ) + ": " + formatNumber( archetypeCounts[index] );
        }
        if ( !anyArchetype ) {
            message += "\n\nNo Elite Rival archetypes are active on this map.";
        }

        message += "\n\nWarlord: ARMY / COMMAND pressure";
        message += "\nPredator: OFFENSE / TACTICS pressure";
        message += "\nArcanist: MAGIC / WARDS pressure";
        message += "\nSentinel: DEFENSE / WARDS pressure";
        message += "\nTrickster: TACTICS / MASTERY pressure";
        message += "\n\nElite focus also reads your recorded doctrine-trigger history, so frequently used halls are more likely to be recognized.";
        message += "\nRivals still use only doctrines you have purchased and every generated rank remains inside the existing 115% envelope.";

        fheroes2::showStandardTextMessage( "Elite Rival Intel", std::move( message ), Dialog::ZERO );
    }

    void showKingdomOverview()
    {
        const uint64_t remainingXP = xpToNextLevel( playerProfile.level ) > playerProfile.progress
                                         ? xpToNextLevel( playerProfile.level ) - playerProfile.progress
                                         : 0;
        const uint64_t totalInvested = totalSpentPoints( playerProfile );
        const uint64_t nextLevelCost = xpToNextLevel( playerProfile.level );
        const long double levelProgressPercent
            = nextLevelCost == 0 ? 0.0L : std::min( 100.0L, static_cast<long double>( playerProfile.progress ) * 100.0L / nextLevelCost );

        std::string message = "KINGDOM RPG SUMMARY\n";
        message += "\nKingdom Level: " + formatNumber( playerProfile.level );
        message += "\nAvailable Guild Points: " + formatNumber( playerProfile.points );
        message += "\nTotal Points Invested: " + formatNumber( totalInvested );
        message += "\nCurrent Level Progress: " + formatNumber( playerProfile.progress ) + " / " + formatNumber( nextLevelCost )
                   + " (" + formatEffect( levelProgressPercent ) + ")";

        size_t activeDoctrines = 0;
        for ( const uint64_t rank : playerProfile.ranks ) {
            if ( rank > 0 ) {
                ++activeDoctrines;
            }
        }
        message += "\nActive Doctrines Unlocked: " + std::to_string( activeDoctrines ) + " / " + std::to_string( upgradeCount );
        const uint64_t prestigeRank = prestigeRankForLevel( playerProfile.level );
        message += "\n\nGUILD PRESTIGE";
        message += "\nPrestige Rank: " + formatNumber( prestigeRank );
        message += "\nNext Prestige Milestone: Guild Level " + formatNumber( nextPrestigeLevel( playerProfile.level ) );
        message += "\nBattle Renown Bonus: +" + formatEffect( ( prestigeBattleRenownMultiplier( playerProfile.level ) - 1.0L ) * 100.0L );
        message += "\nSteward Planning Horizon: " + formatNumber( stewardPlanningHorizon( playerProfile.level ) ) + " ranks";
        message += "\nElite Rival Chance / Hostile Kingdom: " + std::to_string( eliteRivalChanceForLevel( playerProfile.level ) ) + "%";
        message += "\nElite Rival Focus Limit: " + formatNumber( eliteRivalFocusHallLimit( playerProfile.level ) ) + " halls";
        uint64_t totalTriggers = 0;
        for ( const uint64_t uses : playerProfile.useCounts ) {
            totalTriggers = saturatedAdd( totalTriggers, uses );
        }
        if ( totalTriggers > 0 ) {
            message += "\nTotal Battle Triggers: " + formatNumber( totalTriggers );

            const auto mostUsed = std::max_element( playerProfile.useCounts.begin(), playerProfile.useCounts.end() );
            if ( mostUsed != playerProfile.useCounts.end() && *mostUsed > 0 ) {
                const size_t mostUsedId = static_cast<size_t>( std::distance( playerProfile.useCounts.begin(), mostUsed ) );
                message += "\nSignature Doctrine: " + std::string( upgrades[mostUsedId].name ) + " (" + formatNumber( *mostUsed ) + " triggers)";
            }
        }
        std::array<uint64_t, tabNames.size()> investedByTab{};
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            investedByTab[id / upgradesPerTab] = saturatedAdd( investedByTab[id / upgradesPerTab], rankInvestment( id, playerProfile.ranks[id] ) );
        }
        const auto focusIt = std::max_element( investedByTab.begin(), investedByTab.end() );
        if ( focusIt != investedByTab.end() && *focusIt > 0 ) {
            const size_t focusTab = static_cast<size_t>( std::distance( investedByTab.begin(), focusIt ) );
            message += "\nBuild Focus: " + std::string( tabNames[focusTab] ) + " (" + formatNumber( *focusIt ) + " points)";
        }

        size_t stewardFocusTab = tabNames.size();
        long double stewardFocusFactor = 1.0L;
        for ( size_t tab = 0; tab < tabNames.size(); ++tab ) {
            const long double factor = autoBuyPlaystyleFactor( playerProfile, tab * upgradesPerTab );
            if ( factor > stewardFocusFactor ) {
                stewardFocusFactor = factor;
                stewardFocusTab = tab;
            }
        }
        if ( stewardFocusTab < tabNames.size() ) {
            message += "\nSteward Emerging Focus: " + std::string( tabNames[stewardFocusTab] ) + " (+"
                       + formatEffect( ( stewardFocusFactor - 1.0L ) * 100.0L ) + " preference)";
        }

        const StewardGoal stewardGoal = findStewardGoal( playerProfile );
        if ( stewardGoal.id < upgradeCount && stewardGoal.targetRank > playerProfile.ranks[stewardGoal.id] ) {
            message += "\nSteward Long-Term Goal: " + std::string( upgrades[stewardGoal.id].name ) + " -> Rank "
                       + formatNumber( stewardGoal.targetRank );
            const size_t packagePartner = stewardPackagePartner( playerProfile, stewardGoal.id );
            if ( packagePartner < upgradeCount ) {
                message += "\nSteward Package Partner: " + std::string( upgrades[packagePartner].name );
            }
        }
        if ( !eliteEnemyColors.empty() ) {
            message += "\nElite Rival Kingdoms This Map: " + formatNumber( eliteEnemyColors.size() );
            std::array<size_t, rivalArchetypeNames.size()> archetypeCounts{};
            for ( const auto & [color, archetype] : eliteRivalArchetypes ) {
                static_cast<void>( color );
                const size_t index = static_cast<size_t>( archetype );
                if ( index > 0 && index <= archetypeCounts.size() ) {
                    ++archetypeCounts[index - 1];
                }
            }
            for ( size_t index = 0; index < archetypeCounts.size(); ++index ) {
                if ( archetypeCounts[index] > 0 ) {
                    message += "\n  " + std::string( rivalArchetypeNames[index] ) + ": " + formatNumber( archetypeCounts[index] );
                }
            }
        }

        size_t stewardPick = upgradeCount;
        long double stewardPickValue = 0.0L;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            const long double marginal = autoBuyMarginalReturn( playerProfile, id );
            const bool pursuingGoal = id == stewardGoal.id && playerProfile.ranks[id] < stewardGoal.targetRank;
            const long double candidate = marginal * ( pursuingGoal ? 1.18L : 1.0L );
            if ( candidate > stewardPickValue ) {
                stewardPickValue = candidate;
                stewardPick = id;
            }
        }
        if ( stewardPick < upgradeCount ) {
            const uint64_t stewardPrice = cost( stewardPick, playerProfile.ranks[stewardPick] );
            message += "\nSteward Recommendation: " + std::string( upgrades[stewardPick].name ) + " (" + formatNumber( stewardPrice ) + " pts)";
        }

        message += "\nNext Level Reward: +" + formatNumber( pointsPerLevel ) + " guild points";
        message += "\nSteward Auto-Buyer: ";
        message += playerProfile.autoBuy ? "Active (ON)" : "Paused (OFF)";
        message += "\n\nRENOWN (RPG EXPERIENCE)";
        message += "\nCurrent Level XP: " + formatNumber( playerProfile.progress ) + " / " + formatNumber( nextLevelCost );
        message += "\nLifetime Renown: " + formatNumber( playerProfile.experience );
        message += "\n  From Hero Experience: " + formatNumber( playerProfile.heroExperience );
        message += "\n  From Battles: " + formatNumber( playerProfile.battleExperience );
        message += "\n  From Adventure Sites: " + formatNumber( playerProfile.adventureExperience );
        message += "\n  From Offline Progress: " + formatNumber( playerProfile.offlineExperience );
        message += "\n\nRenown to Next Level: " + formatNumber( remainingXP );
        message += "\nUnique Map Sites Visited: " + formatNumber( visitedActionTiles.size() );

        const std::string path = profilePath();
        message += "\n\nPROFILE & RECOVERY";
        message += "\nData Folder: " + fheroes2::RPG::dataDirectory();
        message += "\nPersistent Backup: ";
        message += System::IsFile( path + ".bak" ) ? "Ready" : "Not created yet";
        message += "\nPending Recovery Snapshot: ";
        message += System::IsFile( path + ".tmp" ) ? "Available" : "None";
        message += "\nAutosave: purchases, Steward changes, respecs, and RPG progression";

        fheroes2::showStandardTextMessage( "Royal Guild Overview", std::move( message ), Dialog::ZERO );
    }
}

std::string fheroes2::RPG::dataDirectory()
{
    const std::string directory = []() {
#if defined( _WIN32 )
        if ( const char * userHome = std::getenv( "USERPROFILE" ); userHome != nullptr ) {
            return System::concatPath( System::concatPath( userHome, "Documents" ), "Homm2RPG" );
        }
#endif
        return System::concatPath( System::GetDataDirectory( "fheroes2" ), "Homm2RPG" );
    }();

    static bool migrated = false;
    if ( !migrated ) {
        migrated = true;
        System::MakeDirectory( directory );

        const std::string oldConfig = System::GetConfigDirectory( "fheroes2" );
        for ( const char * fileName : { "rpg_profile.dat", "rpg_profile.dat.tmp", "rpg_profile.dat.bak",
                                       "offline_progress.dat", "offline_progress.dat.tmp", "offline_progress.dat.bak" } ) {
            const std::filesystem::path source( System::concatPath( oldConfig, fileName ) );
            const std::filesystem::path target( System::concatPath( directory, fileName ) );
            std::error_code error;
            if ( std::filesystem::exists( source, error ) ) {
                std::filesystem::copy_file( source, target, std::filesystem::copy_options::skip_existing, error );
            }
        }

        const std::filesystem::path oldSaveDirectory( System::concatPath( System::concatPath( System::GetDataDirectory( "fheroes2" ), "files" ), "save" ) );
        std::error_code error;
        for ( std::filesystem::directory_iterator file( oldSaveDirectory, error ); !error && file != std::filesystem::directory_iterator(); file.increment( error ) ) {
            if ( file->is_regular_file( error ) ) {
                std::filesystem::copy_file( file->path(), std::filesystem::path( directory ) / file->path().filename(),
                                            std::filesystem::copy_options::skip_existing, error );
            }
        }
    }

    return directory;
}

void fheroes2::RPG::beginMap( const PlayerColor playerColor )
{
    activePlayerColor = playerColor;
    enemyProfiles.clear();
    eliteEnemyColors.clear();
    eliteRivalArchetypes.clear();
    visitedActionTiles.clear();
    playerProfile = {};
    if ( playerColor == PlayerColor::NONE ) {
        return;
    }

    const std::string path = profilePath();
    // stable_sort preserves this priority when filesystem timestamps tie. A complete temporary
    // snapshot is produced after the primary and therefore represents the newer save attempt.
    std::array<std::string, 3> profileCandidates{ path + ".tmp", path, path + ".bak" };
    std::stable_sort( profileCandidates.begin(), profileCandidates.end(), []( const std::string & first, const std::string & second ) {
        std::error_code firstError;
        std::error_code secondError;
        const auto firstTime = std::filesystem::last_write_time( first, firstError );
        const auto secondTime = std::filesystem::last_write_time( second, secondError );
        if ( firstError ) {
            return false;
        }
        if ( secondError ) {
            return true;
        }
        return firstTime > secondTime;
    } );
    std::string loadedProfilePath;
    bool loadedProfileNeedsMigration = false;
    for ( const std::string & candidate : profileCandidates ) {
        Profile candidateProfile;
        std::set<uint64_t> candidateVisited;
        if ( readProfile( candidate, candidateProfile, candidateVisited, &loadedProfileNeedsMigration ) ) {
            playerProfile = std::move( candidateProfile );
            visitedActionTiles = std::move( candidateVisited );
            loadedProfilePath = candidate;
            break;
        }
    }

    // A selected backup must survive recovery even if the primary is an older valid file.
    // An invalid primary must also never replace the backup. When needed, validation uses
    // temporary outputs so probing the primary cannot disturb the recovered profile or visited-site set.
    bool preserveRecoveryBackup = loadedProfilePath == path + ".bak";
    if ( !loadedProfilePath.empty() && loadedProfilePath != path && System::IsFile( path ) ) {
        Profile primaryProfile;
        std::set<uint64_t> primaryVisited;
        preserveRecoveryBackup = preserveRecoveryBackup || !readProfile( path, primaryProfile, primaryVisited );
    }

    // Persist new profiles, migrations and recovered snapshots immediately. An unchanged
    // current primary already has a durable copy; rewriting it on every map entry would
    // needlessly replace the older recovery backup with an identical snapshot.
    if ( loadedProfilePath != path || loadedProfileNeedsMigration ) {
        saveProfile( preserveRecoveryBackup );
    }

    // Temporary enemy RPG builds are deterministic for the same map/profile level and
    // scale only upgrades the player has actually purchased. This avoids fresh profiles
    // facing invisible free enemy perks and keeps opponent power tied to real RPG choices.
    uint64_t seed = static_cast<uint64_t>( world.GetMapSeed() ) << 32;
    // Unsigned multiplication intentionally wraps here: this is a hash mix, not arithmetic progression.
    seed ^= playerProfile.level * 0x9E3779B185EBCA87ULL;
    seed ^= static_cast<uint64_t>( playerColor ) * 0xC2B2AE3D27D4EB4FULL;
    std::mt19937_64 rng( seed );

    const auto makeTemporaryProfile
        = [&rng]( const int minimumPower, const int maximumPower, const bool roundUpSmallRanks, const bool sophisticatedKingdom,
                  const bool eliteKingdom, const RivalArchetype archetype ) {
              std::uniform_int_distribution<int> variation( minimumPower, maximumPower );
              const int levelPercent = variation( rng );

              Profile temporary;
              temporary.level = std::max<uint64_t>( 1, scaledValue( playerProfile.level, levelPercent ) );

              std::array<size_t, tabNames.size()> investedTabs{};
              std::array<uint64_t, tabNames.size()> tabInvestments{};
              std::array<uint64_t, tabNames.size()> tabActivity{};
              size_t investedTabCount = 0;
              for ( size_t tab = 0; tab < tabNames.size(); ++tab ) {
                  const size_t firstUpgrade = tab * upgradesPerTab;
                  for ( size_t offset = 0; offset < upgradesPerTab; ++offset ) {
                      const size_t id = firstUpgrade + offset;
                      tabInvestments[tab] = saturatedAdd( tabInvestments[tab], rankInvestment( id, playerProfile.ranks[id] ) );
                      tabActivity[tab] = saturatedAdd( tabActivity[tab], playerProfile.useCounts[id] );
                  }

                  if ( tabInvestments[tab] > 0 ) {
                      investedTabs[investedTabCount++] = tab;
                  }
              }

              size_t focusTabCount = 0;
              int sophisticationTier = 0;
              if ( investedTabCount > 0 ) {
                  if ( sophisticatedKingdom ) {
                      // Ordinary rivals coordinate one to three invested halls according to their
                      // strength roll. Elite rivals begin at four focused halls and earn up to two
                      // additional coordinated halls through the player's Guild Prestige.
                      sophisticationTier = eliteKingdom ? static_cast<int>( eliteRivalFocusHallLimit( playerProfile.level ) )
                                                       : levelPercent >= 105 ? 3 : levelPercent >= 95 ? 2 : 1;
                      focusTabCount = std::min( static_cast<size_t>( sophisticationTier ), investedTabCount );

                      std::array<uint64_t, tabNames.size()> priorities{};
                      std::uniform_int_distribution<uint64_t> tieBreak( 0, 15 );
                      for ( size_t i = 0; i < investedTabCount; ++i ) {
                          const size_t tab = investedTabs[i];
                          const uint64_t activityWeight = static_cast<uint64_t>(
                              std::min<long double>( 255.0L, std::log1p( static_cast<long double>( tabActivity[tab] ) ) * 24.0L ) );
                          const uint64_t adaptiveWeight = eliteKingdom ? activityWeight : activityWeight / 2;
                          const uint64_t archetypeWeight = eliteKingdom ? rivalArchetypeHallBias( archetype, tab ) : 0;
                          priorities[tab] = saturatedAdd(
                              saturatedAdd( saturatedAdd( saturatedMultiply( tabInvestments[tab], 16 ), adaptiveWeight ), archetypeWeight ),
                              tieBreak( rng ) );
                      }
                      std::sort( investedTabs.begin(), investedTabs.begin() + investedTabCount,
                                 [&priorities]( const size_t first, const size_t second ) { return priorities[first] > priorities[second]; } );
                  }
                  else {
                      sophisticationTier = 1;
                      focusTabCount = 1;
                      std::uniform_int_distribution<size_t> tabPick( 0, investedTabCount - 1 );
                      std::swap( investedTabs[0], investedTabs[tabPick( rng )] );
                  }
              }

              const auto isFocusedTab = [&investedTabs, focusTabCount]( const size_t tab ) {
                  for ( size_t focus = 0; focus < focusTabCount; ++focus ) {
                      if ( investedTabs[focus] == tab ) {
                          return focus;
                      }
                  }
                  return focusTabCount;
              };

              const auto hasPurchased = []( const size_t id ) { return playerProfile.ranks[id] > 0; };
              const auto packageBonus = [sophisticationTier, eliteKingdom, &hasPurchased]( const size_t id ) {
                  if ( sophisticationTier < 2 ) {
                      return 0;
                  }

                  const int bonus = eliteKingdom ? 12 : sophisticationTier >= 3 ? 8 : 4;
                  switch ( id ) {
                  case BLOOD_DRINKER:
                  case REAPER:
                  case REGENERATION:
                      return ( static_cast<int>( hasPurchased( BLOOD_DRINKER ) ) + static_cast<int>( hasPurchased( REAPER ) )
                               + static_cast<int>( hasPurchased( REGENERATION ) ) )
                                 >= 2
                             ? bonus
                             : 0;
                  case MARKSMAN:
                  case CLOSE_QUARTERS:
                      return hasPurchased( MARKSMAN ) && hasPurchased( CLOSE_QUARTERS ) ? bonus : 0;
                  case EXECUTIONER:
                  case RUTHLESS:
                      return hasPurchased( EXECUTIONER ) && hasPurchased( RUTHLESS ) ? bonus : 0;
                  case FRENZY:
                  case LAST_STAND:
                      return hasPurchased( FRENZY ) && hasPurchased( LAST_STAND ) ? bonus : 0;
                  case OPENING_BLOW:
                  case DISCIPLINE:
                  case UNYIELDING:
                      return ( static_cast<int>( hasPurchased( OPENING_BLOW ) ) + static_cast<int>( hasPurchased( DISCIPLINE ) )
                               + static_cast<int>( hasPurchased( UNYIELDING ) ) )
                                 >= 2
                             ? bonus
                             : 0;
                  case SORCERY:
                  case PYROMANCY:
                  case CRYOMANCY:
                  case STORMCRAFT:
                  case CATACLYSM:
                  case ARCANE_PIERCING:
                      return hasPurchased( SORCERY )
                                 && ( hasPurchased( PYROMANCY ) || hasPurchased( CRYOMANCY ) || hasPurchased( STORMCRAFT )
                                      || hasPurchased( CATACLYSM ) )
                             ? bonus
                             : 0;
                  case SPELL_WARD:
                  case FIRE_WARD:
                  case COLD_WARD:
                  case STORM_WARD:
                  case CATACLYSM_WARD:
                      return hasPurchased( SPELL_WARD )
                                 && ( hasPurchased( FIRE_WARD ) || hasPurchased( COLD_WARD ) || hasPurchased( STORM_WARD )
                                      || hasPurchased( CATACLYSM_WARD ) )
                             ? bonus
                             : 0;
                  case CRITICAL_TRAINING:
                  case BRUTAL_CRITICALS:
                      return hasPurchased( CRITICAL_TRAINING ) && hasPurchased( BRUTAL_CRITICALS ) ? bonus : 0;
                  default:
                      return 0;
                  }
              };

              for ( size_t id = 0; id < upgradeCount; ++id ) {
                  if ( playerProfile.ranks[id] == 0 ) {
                      continue;
                  }

                  int percent = variation( rng );
                  const size_t focus = isFocusedTab( id / upgradesPerTab );
                  if ( focus < focusTabCount ) {
                      percent += focus == 0 ? ( eliteKingdom ? 12 : 8 )
                                           : focus == 1 ? ( eliteKingdom ? 8 : 5 )
                                                        : focus == 2 ? ( eliteKingdom ? 5 : 3 ) : 3;
                  }
                  else {
                      percent -= 2;
                  }
                  percent += packageBonus( id );
                  if ( eliteKingdom ) {
                      percent += rivalArchetypeDoctrineBias( archetype, id );
                  }
                  percent = std::clamp( percent, minimumPower, maximumPower );

                  const long double scaledRank = static_cast<long double>( playerProfile.ranks[id] ) * percent / 100.0L;
                  const long double roundedRank = roundUpSmallRanks ? std::floor( scaledRank + 0.5L ) : std::floor( scaledRank );
                  temporary.ranks[id] = static_cast<uint64_t>( std::min<long double>(
                      static_cast<long double>( std::numeric_limits<uint64_t>::max() ), std::max<long double>( 0.0L, roundedRank ) ) );
              }

              if ( temporary.ranks[CRITICAL_TRAINING] == 0 ) {
                  temporary.ranks[BRUTAL_CRITICALS] = 0;
              }

              return temporary;
          };

    // Elite rivals are deterministic for this map because they use the same seeded generator.
    // They only begin appearing after a few RPG levels. Guild Prestige raises the late-game
    // encounter rate gradually, while the hard cap keeps ordinary rival kingdoms common.
    const int eliteChance = eliteRivalChanceForLevel( playerProfile.level );
    std::uniform_int_distribution<int> eliteRoll( 0, 99 );
    std::uniform_int_distribution<size_t> archetypeRoll( 0, rivalArchetypeNames.size() - 1 );

    for ( const Player * player : Settings::Get().GetPlayers().getVector() ) {
        if ( player == nullptr || !player->isPlay() || player->GetColor() == playerColor
             || Players::isFriends( playerColor, static_cast<PlayerColorsSet>( player->GetColor() ) ) ) {
            continue;
        }

        const bool elite = eliteChance > 0 && eliteRoll( rng ) < eliteChance;
        if ( elite ) {
            const RivalArchetype archetype = static_cast<RivalArchetype>( archetypeRoll( rng ) + 1 );
            eliteEnemyColors.insert( player->GetColor() );
            eliteRivalArchetypes[player->GetColor()] = archetype;
            enemyProfiles.emplace( player->GetColor(), makeTemporaryProfile( 100, 115, true, true, true, archetype ) );
        }
        else {
            enemyProfiles.emplace( player->GetColor(), makeTemporaryProfile( 85, 115, true, true, false, RivalArchetype::NONE ) );
        }
    }
    enemyProfiles.emplace( PlayerColor::NONE, makeTemporaryProfile( 60, 90, false, false, false, RivalArchetype::NONE ) );
}

void fheroes2::RPG::endMap()
{
    if ( activePlayerColor != PlayerColor::NONE ) {
        saveProfile();
    }
    activePlayerColor = PlayerColor::NONE;
    enemyProfiles.clear();
    eliteEnemyColors.clear();
    eliteRivalArchetypes.clear();
    visitedActionTiles.clear();
}

uint64_t fheroes2::RPG::addExperience( const PlayerColor color, const uint64_t amount, const ExperienceKind kind )
{
    if ( color != activePlayerColor || color == PlayerColor::NONE || amount == 0 ) {
        return 0;
    }

    const uint64_t gained = amount;
    const uint64_t credited = std::min( gained, std::numeric_limits<uint64_t>::max() - playerProfile.experience );
    playerProfile.experience += credited;
    playerProfile.progress = saturatedAdd( playerProfile.progress, credited );
    uint64_t & sourceTotal = kind == ExperienceKind::OFFLINE ? playerProfile.offlineExperience : playerProfile.fieldExperience;
    sourceTotal = saturatedAdd( sourceTotal, credited );
    uint64_t * detailedSource = kind == ExperienceKind::BATTLE ? &playerProfile.battleExperience
                                : kind == ExperienceKind::ADVENTURE ? &playerProfile.adventureExperience
                                : kind == ExperienceKind::HERO ? &playerProfile.heroExperience : nullptr;
    if ( detailedSource != nullptr ) {
        *detailedSource = saturatedAdd( *detailedSource, credited );
    }

    // Spend each completed level threshold in order and carry the exact remainder into the
    // resulting level. Very large awards can legitimately cross multiple thresholds at once.
    const uint64_t gainedLevels = consumeAffordableLevelsWithCarry( playerProfile );
    if ( gainedLevels > 0 && kind != ExperienceKind::OFFLINE ) {
        AudioManager::PlaySound( M82::NWHEROLV );
    }

    if ( playerProfile.autoBuy ) {
        autoBuy( playerProfile );
    }
    saveProfile();
    return credited;
}

void fheroes2::RPG::awardBattle( const PlayerColor color, const PlayerColor opponent, const uint32_t battleExperience, const bool won,
                                  const bool defending, const bool siege )
{
    if ( color != activePlayerColor ) {
        return;
    }

    const Profile * opponentProfile = getProfile( opponent );
    const long double challenge = opponentProfile == nullptr
                                      ? 1.0L
                                      : std::clamp( static_cast<long double>( opponentProfile->level )
                                                        / std::max<long double>( 1.0L, static_cast<long double>( playerProfile.level ) ),
                                                    0.5L, 2.0L );
    long double base = ( won ? 200.0L : 75.0L ) + static_cast<long double>( battleExperience ) * ( won ? 0.2L : 0.08L );
    if ( won && siege ) {
        base *= 1.25L;
    }
    if ( won && defending ) {
        base *= 1.10L;
    }
    if ( won && challenge > 1.0L ) {
        // Beating a stronger RPG profile deserves a modest heroic bonus on top of the normal
        // challenge multiplier. The bonus reaches +25% at the existing 2x challenge cap.
        base *= 1.0L + ( challenge - 1.0L ) * 0.25L;
    }

    // Guild Prestige is an endgame progression layer derived entirely from kingdom level.
    // It does not alter combat stats or the saved point ledger: every ten levels simply makes
    // future battles modestly more rewarding, up to a +25% Renown bonus.
    base *= prestigeBattleRenownMultiplier( playerProfile.level );

    if ( won && opponent != PlayerColor::NONE && eliteEnemyColors.count( opponent ) > 0 ) {
        // Elite rivals become tactically denser as Prestige grows, so their victory premium grows
        // from +35% toward +50% without granting them unpurchased doctrines or exceeding rank caps.
        const long double prestigeEliteBonus
            = std::min<long double>( 0.15L, static_cast<long double>( prestigeRankForLevel( playerProfile.level ) ) * 0.01L );
        base *= 1.35L + prestigeEliteBonus;
    }
    const long double earned = base * challenge;
    static_cast<void>( addExperience( color, static_cast<uint64_t>( std::min( earned, static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) ),
                                      ExperienceKind::BATTLE ) );
}

uint64_t fheroes2::RPG::previewAdventureActionExperience( const PlayerColor color, const int objectType, const int32_t tileIndex )
{
    if ( color != activePlayerColor || tileIndex < 0 ) {
        return 0;
    }

    uint64_t base = 70;
    switch ( objectType ) {
    case MP2::OBJ_MONSTER:
    case MP2::OBJ_HERO:
    case MP2::OBJ_BOAT:
        return 0;

    // Tiny lore/interactables are intentionally low-value so they cannot outshine real exploration.
    case MP2::OBJ_SIGN:
    case MP2::OBJ_BOTTLE:
        base = 40;
        break;

    case MP2::OBJ_RESOURCE:
    case MP2::OBJ_BARREL:
    case MP2::OBJ_CAMPFIRE:
    case MP2::OBJ_FLOTSAM:
    case MP2::OBJ_WINDMILL:
    case MP2::OBJ_WATER_WHEEL:
    case MP2::OBJ_MAGIC_GARDEN:
    case MP2::OBJ_LEAN_TO:
        base = 105;
        break;

    case MP2::OBJ_TREASURE_CHEST:
    case MP2::OBJ_SEA_CHEST:
    case MP2::OBJ_WAGON:
        base = 210;
        break;

    case MP2::OBJ_ARTIFACT:
    case MP2::OBJ_SHIPWRECK_SURVIVOR:
    case MP2::OBJ_SKELETON:
        base = 235;
        break;

    case MP2::OBJ_MINE:
    case MP2::OBJ_ALCHEMIST_LAB:
    case MP2::OBJ_SAWMILL:
    case MP2::OBJ_LIGHTHOUSE:
    case MP2::OBJ_ABANDONED_MINE:
        base = 215;
        break;

    case MP2::OBJ_CASTLE:
        base = 175;
        break;

    case MP2::OBJ_SHRINE_FIRST_CIRCLE:
    case MP2::OBJ_SHRINE_SECOND_CIRCLE:
    case MP2::OBJ_SHRINE_THIRD_CIRCLE:
    case MP2::OBJ_TEMPLE:
        base = 165;
        break;

    // Character-growth sites are satisfying RPG destinations and deserve to stand above loose loot.
    case MP2::OBJ_FORT:
    case MP2::OBJ_MERCENARY_CAMP:
    case MP2::OBJ_WITCH_DOCTORS_HUT:
    case MP2::OBJ_STANDING_STONES:
    case MP2::OBJ_ARENA:
    case MP2::OBJ_GAZEBO:
    case MP2::OBJ_WITCHS_HUT:
    case MP2::OBJ_TREE_OF_KNOWLEDGE:
        base = 225;
        break;

    case MP2::OBJ_FOUNTAIN:
    case MP2::OBJ_FAERIE_RING:
    case MP2::OBJ_IDOL:
    case MP2::OBJ_MERMAID:
    case MP2::OBJ_OASIS:
    case MP2::OBJ_WATERING_HOLE:
    case MP2::OBJ_BUOY:
        base = 120;
        break;

    case MP2::OBJ_EVENT:
        base = 135;
        break;
    case MP2::OBJ_ORACLE:
        base = 205;
        break;
    case MP2::OBJ_SPHINX:
        base = 270;
        break;

    case MP2::OBJ_STONE_LITHS:
    case MP2::OBJ_WHIRLPOOL:
        base = 115;
        break;

    // Dangerous adventure sites should feel like RPG accomplishments rather than ordinary clicks.
    case MP2::OBJ_SHIPWRECK:
    case MP2::OBJ_DERELICT_SHIP:
    case MP2::OBJ_SIRENS:
    case MP2::OBJ_GRAVEYARD:
    case MP2::OBJ_DAEMON_CAVE:
        base = 260;
        break;
    case MP2::OBJ_PYRAMID:
        base = 325;
        break;

    case MP2::OBJ_OBSERVATION_TOWER:
    case MP2::OBJ_MAGELLANS_MAPS:
    case MP2::OBJ_OBELISK:
    case MP2::OBJ_HUT_OF_MAGI:
    case MP2::OBJ_EYE_OF_MAGI:
        base = 190;
        break;

    default:
        if ( !MP2::isInGameActionObject( static_cast<MP2::MapObjectType>( objectType ), false ) ) {
            return 0;
        }
        break;
    }

    const uint64_t key = ( static_cast<uint64_t>( world.GetMapSeed() ) << 32 ) | static_cast<uint32_t>( tileIndex );
    if ( visitedActionTiles.count( key ) != 0 ) {
        return 0;
    }

    const long double levelScale = 1.0L + std::log1p( static_cast<long double>( playerProfile.level - 1 ) ) / 5.0L;
    return static_cast<uint64_t>( std::min<long double>( static_cast<long double>( std::numeric_limits<uint64_t>::max() ),
                                                         static_cast<long double>( base ) * levelScale ) );
}

void fheroes2::RPG::awardAdventureAction( const PlayerColor color, const int objectType, const int32_t tileIndex )
{
    const uint64_t reward = previewAdventureActionExperience( color, objectType, tileIndex );
    if ( reward == 0 ) {
        return;
    }

    const uint64_t key = ( static_cast<uint64_t>( world.GetMapSeed() ) << 32 ) | static_cast<uint32_t>( tileIndex );
    if ( !visitedActionTiles.insert( key ).second ) {
        return;
    }

    static_cast<void>( addExperience( color, reward, ExperienceKind::ADVENTURE ) );
}

uint64_t fheroes2::RPG::creatureAttackDoctrineModifier( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    if ( profile == nullptr ) {
        return 0;
    }

    const uint64_t base
        = saturatedAdd( diminishingFlatStatBonus( profile->ranks[ARMS_TRAINING] ), diminishingFlatStatBonus( profile->ranks[VETERAN_CORE] ) );
    const uint64_t ascension = static_cast<uint64_t>( ascensionChannelBonus( *profile, AscensionChannel::ATTACK ) );
    return saturatedAdd( base, ascension );
}

uint64_t fheroes2::RPG::creatureDefenseDoctrineModifier( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    if ( profile == nullptr ) {
        return 0;
    }

    const uint64_t base
        = saturatedAdd( diminishingFlatStatBonus( profile->ranks[ARMOR_TRAINING] ), diminishingFlatStatBonus( profile->ranks[VETERAN_CORE] ) );
    const uint64_t ascension = static_cast<uint64_t>( ascensionChannelBonus( *profile, AscensionChannel::DEFENSE ) );
    return saturatedAdd( base, ascension );
}

uint32_t fheroes2::RPG::creatureAttackBonus( const PlayerColor color )
{
    return static_cast<uint32_t>(
        std::min<uint64_t>( creatureAttackDoctrineModifier( color ), std::numeric_limits<uint32_t>::max() ) );
}

uint32_t fheroes2::RPG::creatureDefenseBonus( const PlayerColor color )
{
    return static_cast<uint32_t>(
        std::min<uint64_t>( creatureDefenseDoctrineModifier( color ), std::numeric_limits<uint32_t>::max() ) );
}

int fheroes2::RPG::moraleBonus( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0 : static_cast<int>( effect( LEADERSHIP, profile->ranks[LEADERSHIP] ) );
}

int fheroes2::RPG::luckBonus( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0 : static_cast<int>( effect( FORTUNE, profile->ranks[FORTUNE] ) );
}

double fheroes2::RPG::lifeStealPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 0.0
               : static_cast<double>( effect( BLOOD_DRINKER, profile->ranks[BLOOD_DRINKER] )
                                      + ascensionChannelBonus( *profile, AscensionChannel::LIFE_STEAL ) );
}

double fheroes2::RPG::killHealPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( REAPER, profile->ranks[REAPER] ) );
}

double fheroes2::RPG::regenerationPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 0.0
               : static_cast<double>( effect( REGENERATION, profile->ranks[REGENERATION] )
                                      + ascensionChannelBonus( *profile, AscensionChannel::REGENERATION ) );
}

double fheroes2::RPG::criticalChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 0.0
               : static_cast<double>( std::min<long double>(
                     100.0L, effect( CRITICAL_TRAINING, profile->ranks[CRITICAL_TRAINING] )
                                 + ascensionChannelBonus( *profile, AscensionChannel::CRITICAL_CHANCE ) ) );
}

double fheroes2::RPG::criticalDamageBonusPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 50.0
               : 50.0 + static_cast<double>( effect( BRUTAL_CRITICALS, profile->ranks[BRUTAL_CRITICALS] )
                                             + ascensionChannelBonus( *profile, AscensionChannel::CRITICAL_DAMAGE ) );
}

double fheroes2::RPG::expectedCriticalDamageMultiplier( const PlayerColor color )
{
    const double chance = std::clamp( criticalChance( color ), 0.0, 100.0 ) / 100.0;
    const double bonus = std::max( 0.0, criticalDamageBonusPercent( color ) ) / 100.0;
    return 1.0 + chance * bonus;
}

double fheroes2::RPG::sustainValuePercent( const PlayerColor color )
{
    // Kill-heal depends on actually finishing creatures, so count half of its headline value in
    // deterministic AI/strategic estimates. This matches the existing strategic weighting while
    // keeping all consumers on one shared definition.
    return lifeStealPercent( color ) + killHealPercent( color ) * 0.5 + regenerationPercent( color );
}

double fheroes2::RPG::evasionChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 0.0
               : static_cast<double>( std::min<long double>(
                     100.0L, effect( EVASION, profile->ranks[EVASION] )
                                 + ascensionChannelBonus( *profile, AscensionChannel::EVASION ) ) );
}

double fheroes2::RPG::rangedMeleePenaltyRecoveryPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( CLOSE_QUARTERS, profile->ranks[CLOSE_QUARTERS] ) );
}

double fheroes2::RPG::damageMultiplier( const PlayerColor attacker, const PlayerColor defender, const bool ranged, const bool attackerOutnumbered,
                                        const bool defenderOutnumbered, const bool attackerFullHealth, const bool defenderFullHealth,
                                        const bool attackerBelowHalf, const bool defenderBelowHalf )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );

    long double attackBonus = 0;
    if ( attackProfile != nullptr ) {
        attackBonus += ascensionChannelBonus( *attackProfile, AscensionChannel::PHYSICAL_DAMAGE );
        attackBonus += effect( FEROCITY, attackProfile->ranks[FEROCITY] );
        attackBonus += effect( ranged ? MARKSMAN : BRAWLER, attackProfile->ranks[ranged ? MARKSMAN : BRAWLER] );

        if ( !defenderFullHealth ) {
            attackBonus += effect( EXECUTIONER, attackProfile->ranks[EXECUTIONER] );
        }
        if ( defenderBelowHalf ) {
            attackBonus += doctrinePairResonance( *attackProfile, EXECUTIONER, RUTHLESS, 0.28L, 7.5L );
        }
        if ( defenderFullHealth ) {
            attackBonus += effect( OPENING_BLOW, attackProfile->ranks[OPENING_BLOW] );
        }
        if ( attackerOutnumbered ) {
            attackBonus += effect( GIANT_SLAYER, attackProfile->ranks[GIANT_SLAYER] );
            attackBonus += doctrinePairResonance( *attackProfile, GIANT_SLAYER, BULWARK, 0.24L, 6.0L );
        }
        if ( defenderOutnumbered ) {
            attackBonus += effect( OVERWHELM, attackProfile->ranks[OVERWHELM] );
        }
        if ( attackerBelowHalf ) {
            attackBonus += effect( FRENZY, attackProfile->ranks[FRENZY] );
            attackBonus += doctrinePairResonance( *attackProfile, FRENZY, LAST_STAND, 0.24L, 6.0L );
        }
        if ( attackerFullHealth ) {
            attackBonus += effect( DISCIPLINE, attackProfile->ranks[DISCIPLINE] );
            if ( defenderFullHealth ) {
                attackBonus += doctrineTripleResonance( *attackProfile, OPENING_BLOW, DISCIPLINE, UNYIELDING, 0.22L, 6.5L );
            }
        }
        if ( defenderBelowHalf ) {
            attackBonus += effect( RUTHLESS, attackProfile->ranks[RUTHLESS] );
        }
    }

    long double defenseReduction = 0;
    if ( defenseProfile != nullptr ) {
        defenseReduction += ascensionChannelBonus( *defenseProfile, AscensionChannel::PHYSICAL_REDUCTION );
        defenseReduction += effect( IRON_SKIN, defenseProfile->ranks[IRON_SKIN] );
        defenseReduction += effect( ranged ? ARROW_WARD : MELEE_GUARD, defenseProfile->ranks[ranged ? ARROW_WARD : MELEE_GUARD] );

        if ( defenderBelowHalf ) {
            defenseReduction += effect( LAST_STAND, defenseProfile->ranks[LAST_STAND] );
            defenseReduction += doctrinePairResonance( *defenseProfile, FRENZY, LAST_STAND, 0.20L, 5.0L );
        }
        if ( defenderOutnumbered ) {
            defenseReduction += effect( BULWARK, defenseProfile->ranks[BULWARK] );
            defenseReduction += doctrinePairResonance( *defenseProfile, GIANT_SLAYER, BULWARK, 0.20L, 5.0L );
        }
        if ( defenderFullHealth ) {
            defenseReduction += effect( UNYIELDING, defenseProfile->ranks[UNYIELDING] );
            defenseReduction += doctrinePairResonance( *defenseProfile, DISCIPLINE, UNYIELDING, 0.20L, 5.0L );
        }
    }

    // Offensive doctrine power is open-ended. Defensive reduction remains capped so no stack
    // can become invulnerable through rank stacking.
    defenseReduction = std::min<long double>( defenseReduction, 70.0L );
    if ( attackProfile != nullptr && defenseReduction > 0 ) {
        const long double piercing = std::min<long double>(
            100.0L, effect( ARMOR_PIERCING, attackProfile->ranks[ARMOR_PIERCING] )
                       + ascensionChannelBonus( *attackProfile, AscensionChannel::ARMOR_PIERCING ) );
        defenseReduction *= 1.0L - piercing / 100.0L;
    }

    return static_cast<double>( ( 1.0L + attackBonus / 100.0L ) * ( 1.0L - defenseReduction / 100.0L ) );
}

double fheroes2::RPG::spellMultiplier( const PlayerColor attacker, const PlayerColor defender, const int spellId )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );
    const size_t specialization = spellSpecializationId( spellId );

    long double spellBonus
        = attackProfile == nullptr
              ? 0
              : effect( SORCERY, attackProfile->ranks[SORCERY] )
                    + ascensionChannelBonus( *attackProfile, AscensionChannel::SPELL_DAMAGE );
    long double defenseReduction
        = defenseProfile == nullptr
              ? 0
              : effect( SPELL_WARD, defenseProfile->ranks[SPELL_WARD] )
                    + ascensionChannelBonus( *defenseProfile, AscensionChannel::SPELL_REDUCTION );

    if ( specialization != upgradeCount ) {
        if ( attackProfile != nullptr ) {
            spellBonus += effect( specialization, attackProfile->ranks[specialization] );
            spellBonus += doctrinePairResonance( *attackProfile, SORCERY, specialization, 0.25L, 7.5L );
        }
        if ( defenseProfile != nullptr ) {
            const size_t ward = spellWardId( spellId );
            defenseReduction += effect( ward, defenseProfile->ranks[ward] );
            defenseReduction += doctrinePairResonance( *defenseProfile, SPELL_WARD, ward, 0.25L, 7.5L );
        }
    }

    // Damaging spell doctrine power is open-ended. Spell reduction keeps the same 70% global
    // safety ceiling before Arcane Piercing.
    defenseReduction = std::min<long double>( defenseReduction, 70.0L );
    if ( attackProfile != nullptr && defenseReduction > 0 ) {
        const long double piercing = std::min<long double>(
            100.0L, effect( ARCANE_PIERCING, attackProfile->ranks[ARCANE_PIERCING] )
                       + ascensionChannelBonus( *attackProfile, AscensionChannel::ARCANE_PIERCING ) );
        defenseReduction *= 1.0L - piercing / 100.0L;
    }

    return static_cast<double>( ( 1.0L + spellBonus / 100.0L ) * ( 1.0L - defenseReduction / 100.0L ) );
}

std::string fheroes2::RPG::formatExperience( const uint64_t value )
{
    return formatNumber( value );
}

std::string fheroes2::RPG::formatDoctrineModifier( const double value, const bool percentage )
{
    std::string output = formatCompactDoctrineNumber( static_cast<long double>( value ) );
    if ( percentage ) {
        output += '%';
    }
    return output;
}

void fheroes2::RPG::recordDoctrineUse( const PlayerColor color, const size_t upgradeId, const uint64_t count )
{
    if ( color != activePlayerColor || color == PlayerColor::NONE || upgradeId >= upgradeCount || count == 0 ) {
        return;
    }

    if ( playerProfile.ranks[upgradeId] == 0 ) {
        return;
    }

    playerProfile.useCounts[upgradeId] = saturatedAdd( playerProfile.useCounts[upgradeId], count );
}

void fheroes2::RPG::recordSpellDoctrineUse( const PlayerColor attacker, const PlayerColor defender, const int spellId )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );

    if ( attacker == activePlayerColor && attackProfile != nullptr ) {
        if ( attackProfile->ranks[SORCERY] > 0 ) {
            recordDoctrineUse( attacker, SORCERY );
        }
        const size_t specialization = spellSpecializationId( spellId );
        if ( specialization != upgradeCount && attackProfile->ranks[specialization] > 0 ) {
            recordDoctrineUse( attacker, specialization );
        }
        if ( defenseProfile != nullptr ) {
            const size_t ward = spellWardId( spellId );
            if ( defenseProfile->ranks[SPELL_WARD] > 0 || ( ward != upgradeCount && defenseProfile->ranks[ward] > 0 ) ) {
                recordDoctrineUse( attacker, ARCANE_PIERCING );
            }
        }
    }

    if ( defender == activePlayerColor && defenseProfile != nullptr ) {
        if ( defenseProfile->ranks[SPELL_WARD] > 0 ) {
            recordDoctrineUse( defender, SPELL_WARD );
        }
        const size_t ward = spellWardId( spellId );
        if ( ward != upgradeCount && defenseProfile->ranks[ward] > 0 ) {
            recordDoctrineUse( defender, ward );
        }
    }
}

void fheroes2::RPG::recordPhysicalDoctrineUse( const PlayerColor attacker, const PlayerColor defender, const bool ranged,
                                               const bool attackerOutnumbered, const bool defenderOutnumbered,
                                               const bool attackerFullHealth, const bool defenderFullHealth,
                                               const bool attackerBelowHalf, const bool defenderBelowHalf,
                                               const bool inMeleePenalty )
{
    if ( attacker == activePlayerColor ) {
        recordDoctrineUse( attacker, FEROCITY );
        recordDoctrineUse( attacker, ARMS_TRAINING );
        recordDoctrineUse( attacker, VETERAN_CORE );
        if ( ranged ) {
            recordDoctrineUse( attacker, MARKSMAN );
        }
        else {
            recordDoctrineUse( attacker, BRAWLER );
        }

        if ( inMeleePenalty ) {
            recordDoctrineUse( attacker, CLOSE_QUARTERS );
        }

        if ( !defenderFullHealth ) {
            recordDoctrineUse( attacker, EXECUTIONER );
        }
        else {
            recordDoctrineUse( attacker, OPENING_BLOW );
        }

        if ( attackerOutnumbered ) {
            recordDoctrineUse( attacker, GIANT_SLAYER );
        }
        if ( defenderOutnumbered ) {
            recordDoctrineUse( attacker, OVERWHELM );
        }
        if ( attackerBelowHalf ) {
            recordDoctrineUse( attacker, FRENZY );
        }
        if ( attackerFullHealth ) {
            recordDoctrineUse( attacker, DISCIPLINE );
        }
        if ( defenderBelowHalf ) {
            recordDoctrineUse( attacker, RUTHLESS );
        }

        const Profile * defProfile = getProfile( defender );
        if ( defProfile != nullptr && ( defProfile->ranks[IRON_SKIN] > 0 || defProfile->ranks[ranged ? ARROW_WARD : MELEE_GUARD] > 0
                                        || ( defenderBelowHalf && defProfile->ranks[LAST_STAND] > 0 )
                                        || ( defenderOutnumbered && defProfile->ranks[BULWARK] > 0 )
                                        || ( defenderFullHealth && defProfile->ranks[UNYIELDING] > 0 ) ) ) {
            recordDoctrineUse( attacker, ARMOR_PIERCING );
        }
    }

    if ( defender == activePlayerColor ) {
        recordDoctrineUse( defender, IRON_SKIN );
        recordDoctrineUse( defender, ARMOR_TRAINING );
        recordDoctrineUse( defender, VETERAN_CORE );
        if ( ranged ) {
            recordDoctrineUse( defender, ARROW_WARD );
        }
        else {
            recordDoctrineUse( defender, MELEE_GUARD );
        }

        if ( defenderBelowHalf ) {
            recordDoctrineUse( defender, LAST_STAND );
        }
        if ( defenderOutnumbered ) {
            recordDoctrineUse( defender, BULWARK );
        }
        if ( defenderFullHealth ) {
            recordDoctrineUse( defender, UNYIELDING );
        }
    }
}

uint64_t fheroes2::RPG::availablePoints()
{
    return playerProfile.points;
}

uint64_t fheroes2::RPG::kingdomLevel()
{
    return playerProfile.level;
}

uint64_t fheroes2::RPG::renownToNextLevel( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    if ( profile == nullptr ) {
        return 0;
    }

    const uint64_t required = xpToNextLevel( profile->level );
    return required > profile->progress ? required - profile->progress : 0;
}

double fheroes2::RPG::spellcastingInvestmentPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    if ( profile == nullptr ) {
        return 0.0;
    }

    const long double strongestElement
        = std::max( { effect( PYROMANCY, profile->ranks[PYROMANCY] ), effect( CRYOMANCY, profile->ranks[CRYOMANCY] ),
                      effect( STORMCRAFT, profile->ranks[STORMCRAFT] ), effect( CATACLYSM, profile->ranks[CATACLYSM] ) } );
    return static_cast<double>( effect( SORCERY, profile->ranks[SORCERY] ) + strongestElement
                                + effect( ARCANE_PIERCING, profile->ranks[ARCANE_PIERCING] ) * 0.25L );
}

bool fheroes2::RPG::isStewardActive()
{
    return playerProfile.autoBuy;
}

uint64_t fheroes2::RPG::doctrineRank( const PlayerColor color, const size_t upgradeId )
{
    if ( upgradeId >= upgradeCount ) {
        return 0;
    }

    const Profile * profile = getProfile( color );
    return profile != nullptr ? profile->ranks[upgradeId] : 0;
}

double fheroes2::RPG::doctrineEffect( const PlayerColor color, const size_t upgradeId )
{
    if ( upgradeId >= upgradeCount ) {
        return 0.0;
    }

    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( upgradeId, profile->ranks[upgradeId] ) );
}

void fheroes2::RPG::showMenu()
{
    if ( activePlayerColor == PlayerColor::NONE ) {
        return;
    }

    const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
    fheroes2::Display & display = fheroes2::Display::instance();
    fheroes2::StandardWindow window( 432, 396, true, display );
    const fheroes2::Rect area = window.activeArea();
    const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();
    const int scrollIcn = isEvilInterface ? ICN::SCROLLE : ICN::SCROLL;
    // upgradesPerTab defined at namespace scope
    constexpr size_t visibleRows = 3;
    std::array<fheroes2::Rect, tabNames.size()> tabAreas{};
    std::array<fheroes2::Rect, visibleRows> visibleUpgradeAreas{};
    std::array<fheroes2::Rect, visibleRows> buyAreas{};
    std::array<size_t, tabNames.size()> scrollOffsets{};
    std::array<size_t, tabNames.size()> selectedOffsets{};
    const fheroes2::Rect statsArea( area.x + 12, area.y + 38, area.width - 24, 42 );
    const fheroes2::Rect listArea( area.x + 12, area.y + 151, area.width - 24, 181 );
    const int32_t scrollbarX = listArea.x + listArea.width - 19;
    fheroes2::Button scrollUp( scrollbarX + 1, listArea.y + 1, scrollIcn, 0, 1 );
    fheroes2::Button scrollDown( scrollbarX + 1, listArea.y + listArea.height - 15, scrollIcn, 2, 3 );
    fheroes2::ButtonSprite autoButton;
    fheroes2::ButtonSprite respecButton;
    fheroes2::ButtonSprite closeButton;
    size_t tab = 0;
    bool redraw = true;
    LocalEvent & event = LocalEvent::Get();

    const auto keepSelectedDoctrineVisible = [&scrollOffsets, &selectedOffsets, visibleRows]( const size_t tabIndex ) {
        size_t & selectedOffset = selectedOffsets[tabIndex];
        size_t & scrollOffset = scrollOffsets[tabIndex];

        selectedOffset = std::min( selectedOffset, upgradesPerTab - 1 );
        scrollOffset = std::min( scrollOffset, upgradesPerTab - visibleRows );

        if ( selectedOffset < scrollOffset ) {
            scrollOffset = selectedOffset;
        }
        else if ( selectedOffset >= scrollOffset + visibleRows ) {
            scrollOffset = selectedOffset - visibleRows + 1;
        }
    };

    while ( event.HandleEvents() ) {
        if ( redraw ) {
            window.render();
            window.applyGemDecoratedCorners();

            drawText( "KINGDOM RPG", area.x + 12, area.y + 4, area.width - 24, fheroes2::FontType::normalYellow() );
            drawText( "ROYAL GUILD - RANKS & DOCTRINES", area.x + 12, area.y + 22, area.width - 24, fheroes2::FontType::smallWhite() );
            fheroes2::Fill( display, area.x + 22, area.y + 34, area.width - 44, 1, fheroes2::GetColorId( 219, 175, 66 ) );

            window.applyTextBackgroundShading( statsArea );
            drawBeveledPanel( statsArea, true );
            fheroes2::Fill( display, statsArea.x + 4, statsArea.y + 4, statsArea.width - 8, 1, fheroes2::GetColorId( 219, 175, 66 ) );
            drawSingleLine( "LEVEL  " + formatNumber( playerProfile.level ), statsArea.x + 12, statsArea.y + 6, statsArea.width / 2 - 18,
                            fheroes2::FontType::smallYellow() );
            drawSingleLine( "GUILD POINTS  " + formatNumber( playerProfile.points ), statsArea.x + statsArea.width / 2 + 5, statsArea.y + 6,
                            statsArea.width / 2 - 17, fheroes2::FontType::smallYellow() );

            const uint64_t remainingXP = xpToNextLevel( playerProfile.level ) > playerProfile.progress
                                             ? xpToNextLevel( playerProfile.level ) - playerProfile.progress
                                             : 0;
            drawText( "XP " + formatExperience( playerProfile.progress ) + " / " + formatExperience( xpToNextLevel( playerProfile.level ) )
                          + "    " + formatExperience( remainingXP ) + " to next",
                      statsArea.x + 8, statsArea.y + 20, statsArea.width - 16, fheroes2::FontType::smallWhite() );

            const int32_t barWidth = statsArea.width - 24;
            const uint64_t nextLevelCost = xpToNextLevel( playerProfile.level );
            const long double progressRatio
                = nextLevelCost == 0 ? 0.0L : std::min( 1.0L, static_cast<long double>( playerProfile.progress ) / nextLevelCost );
            fheroes2::Fill( display, statsArea.x + 12, statsArea.y + 36, barWidth, 3, fheroes2::GetColorId( 53, 42, 32 ) );
            fheroes2::Fill( display, statsArea.x + 12, statsArea.y + 36, static_cast<int32_t>( barWidth * progressRatio ), 3,
                            fheroes2::GetColorId( 219, 175, 66 ) );

            for ( size_t i = 0; i < tabNames.size(); ++i ) {
                tabAreas[i] = { area.x + 12 + static_cast<int32_t>( i % 4 ) * 102, area.y + 87 + static_cast<int32_t>( i / 4 ) * 25, 99, 23 };
                window.applyTextBackgroundShading( tabAreas[i] );
                drawBeveledPanel( tabAreas[i], i == tab );
                if ( i == tab ) {
                    fheroes2::Fill( display, tabAreas[i].x + 5, tabAreas[i].y + 4, tabAreas[i].width - 10, 1, fheroes2::GetColorId( 219, 175, 66 ) );
                }
                drawText( tabNames[i], tabAreas[i].x + 4, tabAreas[i].y + 5, tabAreas[i].width - 8,
                          i == tab ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
            }

            drawText( tabPanelNames[tab], area.x + 18, area.y + 137, area.width - 36, fheroes2::FontType::smallYellow() );

            window.applyTextBackgroundShading( listArea );
            drawBeveledPanel( listArea, true );
            fheroes2::Fill( display, listArea.x + 4, listArea.y + 4, listArea.width - 8, 1, fheroes2::GetColorId( 219, 175, 66 ) );
            window.renderScrollbarBackground( { scrollbarX, listArea.y, 16, listArea.height }, isEvilInterface );
            scrollUp.draw();
            scrollDown.draw();

            const fheroes2::Sprite & scrollThumb = Assets::getImage( scrollIcn, 4 );
            const int32_t thumbTravel = std::max( 0, listArea.height - 38 - scrollThumb.height() );
            const int32_t thumbY = listArea.y + 19 + static_cast<int32_t>( scrollOffsets[tab] * thumbTravel / ( upgradesPerTab - visibleRows ) );
            fheroes2::Blit( scrollThumb, display, scrollbarX + 2, thumbY );

            const size_t first = tab * upgradesPerTab + scrollOffsets[tab];
            for ( size_t row = 0; row < visibleRows; ++row ) {
                const size_t i = first + row;
                fheroes2::Rect & rowArea = visibleUpgradeAreas[row];
                rowArea = { listArea.x + 5, listArea.y + 5 + static_cast<int32_t>( row * 57 ), listArea.width - 31, 52 };
                window.applyTextBackgroundShading( rowArea );
                drawBeveledPanel( rowArea, false );

                const bool prerequisiteMet = i != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;
                const bool canBuy = playerProfile.ranks[i] < std::numeric_limits<uint64_t>::max()
                                    && effect( i, playerProfile.ranks[i] + 1 ) > effect( i, playerProfile.ranks[i] ) && prerequisiteMet
                                    && playerProfile.points >= cost( i, playerProfile.ranks[i] );
                if ( canBuy ) {
                    fheroes2::Fill( display, rowArea.x + 3, rowArea.y + 5, 2, rowArea.height - 10, fheroes2::GetColorId( 219, 175, 66 ) );
                }

                const fheroes2::Rect badgeArea{ rowArea.x + 7, rowArea.y + 8, 34, 34 };
                drawBeveledPanel( badgeArea, true );
                drawText( upgradeSigils[i], badgeArea.x + 2, badgeArea.y + 8, badgeArea.width - 4,
                          fheroes2::FontType::normalYellow() );

                const int32_t textX = rowArea.x + 48;
                const int32_t buyWidth = 63;
                buyAreas[row] = { rowArea.x + rowArea.width - buyWidth - 7, rowArea.y + 27, buyWidth, 20 };
                drawBeveledPanel( buyAreas[row], !canBuy );
                const bool isSelected = selectedOffsets[tab] == scrollOffsets[tab] + row;
                drawSingleLine( ( isSelected ? "> " : "" ) + std::string( upgrades[i].name ), textX, rowArea.y + 4, rowArea.width - 150,
                                fheroes2::FontType::normalYellow() );
                drawSingleLine( "Rank " + formatNumber( playerProfile.ranks[i] ), rowArea.x + rowArea.width - 94, rowArea.y + 7, 84,
                                fheroes2::FontType::smallWhite() );
                drawSingleLine( upgrades[i].description, textX, rowArea.y + 21, rowArea.width - 126, fheroes2::FontType::smallWhite() );

                const uint64_t rank = playerProfile.ranks[i];
                const std::string current = shortEffectSummary( i, rank );
                const bool rankCanAdvance
                    = rank < std::numeric_limits<uint64_t>::max() && effect( i, rank + 1 ) > effect( i, rank );
                const std::string next = rankCanAdvance ? shortEffectSummary( i, rank + 1 ) : "MAX EFFECT";
                drawSingleLine( current + " -> " + next, textX, rowArea.y + 36, rowArea.width - 130,
                                canBuy ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
                const uint64_t price = rankCanAdvance ? cost( i, playerProfile.ranks[i] ) : 0;
                const std::string buyLabel = !rankCanAdvance ? "MAX"
                                             : !prerequisiteMet ? "LOCKED"
                                             : playerProfile.points < price ? "NEED " + formatNumber( price - playerProfile.points )
                                                                            : "BUY " + formatNumber( price );
                drawText( buyLabel, buyAreas[row].x + 3, buyAreas[row].y + 5, buyAreas[row].width - 6,
                          canBuy ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
            }

            const size_t firstVisibleRow = scrollOffsets[tab] + 1;
            const size_t lastVisibleRow = std::min( upgradesPerTab, scrollOffsets[tab] + visibleRows );
            drawSingleLine( "Rows " + std::to_string( firstVisibleRow ) + "-" + std::to_string( lastVisibleRow ) + "/"
                                + std::to_string( upgradesPerTab ) + "   Up/Down Select   B/Enter/Space Buy   I Details",
                            area.x + 12, area.y + 333, area.width - 24, fheroes2::FontType::smallWhite() );
            drawSingleLine( "1-8 Tabs   O Overview   K Analytics   V Rivals   H Help   S/A Steward   R Respec", area.x + 12, area.y + 344,
                            area.width - 24, fheroes2::FontType::smallWhite() );
            window.renderTextAdaptedButtonSprite( autoButton, playerProfile.autoBuy ? "Steward ON" : "Steward OFF", { 18, 6 },
                                                  fheroes2::StandardWindow::Padding::BOTTOM_LEFT );
            window.renderTextAdaptedButtonSprite( respecButton, "Respec", { 0, 6 }, fheroes2::StandardWindow::Padding::BOTTOM_CENTER );
            window.renderTextAdaptedButtonSprite( closeButton, "Close", { 18, 6 }, fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );
            display.render( window.totalArea() );
            redraw = false;
        }

        autoButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( autoButton.area() ) );
        respecButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( respecButton.area() ) );
        closeButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( closeButton.area() ) );
        scrollUp.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( scrollUp.area() ) );
        scrollDown.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( scrollDown.area() ) );

        if ( Game::HotKeyCloseWindow() || event.isKeyPressed( fheroes2::Key::KEY_F9 ) || event.MouseClickLeft( closeButton.area() ) ) {
            break;
        }

        bool tabChanged = false;
        for ( size_t i = 0; i < tabNames.size(); ++i ) {
            const auto numKey = static_cast<fheroes2::Key>( static_cast<int32_t>( fheroes2::Key::KEY_1 ) + i );
            const auto kpKey = static_cast<fheroes2::Key>( static_cast<int32_t>( fheroes2::Key::KEY_KP_1 ) + i );
            if ( event.isKeyPressed( numKey ) || event.isKeyPressed( kpKey ) ) {
                tab = i;
                redraw = true;
                tabChanged = true;
                break;
            }
            if ( event.isMouseRightButtonPressedInArea( tabAreas[i] ) || event.MouseLongPressLeft( tabAreas[i] ) ) {
                showTabDetails( i );
                redraw = true;
                tabChanged = true;
                break;
            }
            if ( event.MouseClickLeft( tabAreas[i] ) ) {
                tab = i;
                redraw = true;
                tabChanged = true;
                break;
            }
        }
        if ( tabChanged ) {
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_LEFT ) ) {
            tab = ( tab + tabNames.size() - 1 ) % tabNames.size();
            redraw = true;
            continue;
        }
        if ( event.isKeyPressed( fheroes2::Key::KEY_RIGHT ) || event.isKeyPressed( fheroes2::Key::KEY_TAB ) ) {
            tab = ( tab + 1 ) % tabNames.size();
            redraw = true;
            continue;
        }

        size_t & scrollOffset = scrollOffsets[tab];
        if ( event.isKeyPressed( fheroes2::Key::KEY_PAGE_UP ) || event.isKeyPressed( fheroes2::Key::KEY_HOME ) ) {
            selectedOffsets[tab] = 0;
            keepSelectedDoctrineVisible( tab );
            redraw = true;
            continue;
        }
        if ( event.isKeyPressed( fheroes2::Key::KEY_PAGE_DOWN ) || event.isKeyPressed( fheroes2::Key::KEY_END ) ) {
            selectedOffsets[tab] = upgradesPerTab - 1;
            keepSelectedDoctrineVisible( tab );
            redraw = true;
            continue;
        }
        if ( event.isKeyPressed( fheroes2::Key::KEY_UP ) ) {
            if ( selectedOffsets[tab] > 0 ) {
                --selectedOffsets[tab];
                keepSelectedDoctrineVisible( tab );
                redraw = true;
            }
            continue;
        }
        if ( event.isKeyPressed( fheroes2::Key::KEY_DOWN ) ) {
            if ( selectedOffsets[tab] + 1 < upgradesPerTab ) {
                ++selectedOffsets[tab];
                keepSelectedDoctrineVisible( tab );
                redraw = true;
            }
            continue;
        }
        if ( ( event.isMouseWheelUpInArea( listArea ) || event.MouseClickLeft( scrollUp.area() ) ) && scrollOffset > 0 ) {
            --scrollOffset;
            selectedOffsets[tab] = std::clamp( selectedOffsets[tab], scrollOffset, scrollOffset + visibleRows - 1 );
            redraw = true;
            continue;
        }
        if ( ( event.isMouseWheelDownInArea( listArea ) || event.MouseClickLeft( scrollDown.area() ) )
             && scrollOffset + visibleRows < upgradesPerTab ) {
            ++scrollOffset;
            selectedOffsets[tab] = std::clamp( selectedOffsets[tab], scrollOffset, scrollOffset + visibleRows - 1 );
            redraw = true;
            continue;
        }

        const fheroes2::Sprite & scrollThumb = Assets::getImage( scrollIcn, 4 );
        const int32_t thumbTravel = std::max( 0, listArea.height - 38 - scrollThumb.height() );
        const int32_t thumbY = listArea.y + 19 + static_cast<int32_t>( scrollOffsets[tab] * thumbTravel / ( upgradesPerTab - visibleRows ) );
        const fheroes2::Rect scrollTrack( scrollbarX, listArea.y + 16, 16, listArea.height - 32 );
        if ( event.MouseClickLeft( scrollTrack ) ) {
            const Point & cursor = event.getMouseCursorPos();
            if ( cursor.y < thumbY && scrollOffset > 0 ) {
                --scrollOffset;
                selectedOffsets[tab] = std::clamp( selectedOffsets[tab], scrollOffset, scrollOffset + visibleRows - 1 );
                redraw = true;
                continue;
            }
            if ( cursor.y > thumbY + scrollThumb.height() && scrollOffset + visibleRows < upgradesPerTab ) {
                ++scrollOffset;
                selectedOffsets[tab] = std::clamp( selectedOffsets[tab], scrollOffset, scrollOffset + visibleRows - 1 );
                redraw = true;
                continue;
            }
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_I ) ) {
            showUpgradeDetails( tab * upgradesPerTab + selectedOffsets[tab] );
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_H ) ) {
            showRoyalGuildHelp();
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_V ) ) {
            showRivalIntel();
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_K ) ) {
            showBuildAnalytics();
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_O ) || event.isMouseRightButtonPressedInArea( statsArea ) || event.MouseLongPressLeft( statsArea )
             || event.MouseClickLeft( statsArea ) ) {
            showKingdomOverview();
            redraw = true;
            continue;
        }

        for ( size_t row = 0; row < visibleRows; ++row ) {
            const size_t i = tab * upgradesPerTab + scrollOffset + row;
            if ( event.isMouseRightButtonPressedInArea( visibleUpgradeAreas[row] ) || event.MouseLongPressLeft( visibleUpgradeAreas[row] ) ) {
                selectedOffsets[tab] = scrollOffset + row;
                showUpgradeDetails( i );
                redraw = true;
                break;
            }
            if ( event.MouseClickLeft( buyAreas[row] ) ) {
                selectedOffsets[tab] = scrollOffset + row;
                if ( buy( playerProfile, i ) ) {
                    saveProfile();
                }
                else {
                    showUpgradeDetails( i );
                }
                redraw = true;
                break;
            }
            if ( event.MouseClickLeft( visibleUpgradeAreas[row] ) ) {
                selectedOffsets[tab] = scrollOffset + row;
                showUpgradeDetails( i );
                redraw = true;
                break;
            }
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_B ) || event.isKeyPressed( fheroes2::Key::KEY_ENTER ) || event.isKeyPressed( fheroes2::Key::KEY_SPACE ) ) {
            const size_t i = tab * upgradesPerTab + selectedOffsets[tab];
            if ( buy( playerProfile, i ) ) {
                saveProfile();
            }
            else {
                showUpgradeDetails( i );
            }
            redraw = true;
            continue;
        }

        if ( event.isMouseRightButtonPressedInArea( autoButton.area() ) || event.MouseLongPressLeft( autoButton.area() ) ) {
            fheroes2::showStandardTextMessage(
                "Steward",
                "Automatically buys the strongest available next rank while balancing immediate value, battle-trigger history, long-term goals, and complementary package partners. Capped or locked upgrades are skipped.",
                Dialog::ZERO );
            redraw = true;
            continue;
        }
        if ( event.MouseClickLeft( autoButton.area() ) || event.isKeyPressed( fheroes2::Key::KEY_S ) || event.isKeyPressed( fheroes2::Key::KEY_A ) ) {
            playerProfile.autoBuy = !playerProfile.autoBuy;
            if ( playerProfile.autoBuy ) {
                autoBuy( playerProfile );
            }
            saveProfile();
            redraw = true;
        }

        if ( event.isMouseRightButtonPressedInArea( respecButton.area() ) || event.MouseLongPressLeft( respecButton.area() ) ) {
            fheroes2::showStandardTextMessage(
                "Respec Doctrines",
                "Refund all guild points spent on doctrines and reset ranks to zero, allowing you to freely reallocate your build.",
                Dialog::ZERO );
            redraw = true;
            continue;
        }
        if ( event.MouseClickLeft( respecButton.area() ) || event.isKeyPressed( fheroes2::Key::KEY_R ) ) {
            const uint64_t refund = totalSpentPoints( playerProfile );
            if ( refund == 0 ) {
                fheroes2::showStandardTextMessage( "Respec Doctrines", "No guild points have been spent yet.", Dialog::OK );
                redraw = true;
            }
            else {
                const uint64_t pointsAfterRefund = saturatedAdd( playerProfile.points, refund );
                const std::string prompt = "Reset all upgrade ranks and refund " + formatNumber( refund )
                                           + " guild points?\n\nYou will have " + formatNumber( pointsAfterRefund )
                                           + " available points. Steward auto-buy will be paused and battle-trigger familiarity will be cleared.";
                if ( fheroes2::showStandardTextMessage( "Respec Doctrines", prompt, Dialog::YES | Dialog::NO ) == Dialog::YES ) {
                    respecProfile( playerProfile );
                    redraw = true;
                }
            }
        }
    }
}

