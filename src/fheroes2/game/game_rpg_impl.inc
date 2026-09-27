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
#include <vector>

#include "audio_manager.h"
#include "color.h"
#include "m82.h"
#include "cursor.h"
#include "dialog.h"
#include "game_assets.h"
#include "game_hotkeys.h"
#include "heroes.h"
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
    constexpr uint64_t heroCaptureRenown = 500;
    constexpr int heroRenownFileVersion = 1;
    constexpr int heroChronicleFileVersion = 1;
    constexpr int profileVersion = 8;

    struct HeroLegacyTier
    {
        const char * title;
        uint64_t threshold;
    };

    constexpr std::array<HeroLegacyTier, 5> heroLegacyTiers{ {
        { "Unknown", 0 },
        { "Proven", 1000 },
        { "Famous", 5000 },
        { "Legendary", 25000 },
        { "Mythic", 100000 },
    } };

    static_assert( heroCaptureRenown > 0 );
    static_assert( heroRenownFileVersion == 1 );
    static_assert( heroChronicleFileVersion == 1 );
    static_assert( heroLegacyTiers[0].threshold == 0 );
    static_assert( heroLegacyTiers[0].threshold < heroLegacyTiers[1].threshold );
    static_assert( heroLegacyTiers[1].threshold < heroLegacyTiers[2].threshold );
    static_assert( heroLegacyTiers[2].threshold < heroLegacyTiers[3].threshold );
    static_assert( heroLegacyTiers[3].threshold < heroLegacyTiers[4].threshold );
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
        "Adds open-ended creature Attack with diminishing returns. Rank 1 grants a full +1 Attack; later ranks still improve the underlying curve but each rank contributes less than the previous one.",
        "Adds open-ended creature Defense with diminishing returns. Rank 1 grants a full +1 Defense; later ranks still improve the underlying curve but each rank contributes less than the previous one.",
        "Adds open-ended Attack and Defense with diminishing returns. Rank 1 grants a full +1 to both stats; later ranks add progressively less. Its ranks cost more because both stats grow together.",
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

    enum class EliteMutation
    {
        VAMPIRIC,
        BERSERKER,
        ARMORED,
        ARCANE_SHIELDED,
        REGENERATING,
        DEADLY,
        ELUSIVE,
        PIERCING,
        UNSTABLE_MAGIC,
        WARFORGED
    };

    constexpr std::array<const char *, 5> rivalArchetypeNames{ "Warlord", "Predator", "Arcanist", "Sentinel", "Trickster" };
    constexpr std::array<EliteMutation, 10> eliteMutationPool{
        EliteMutation::VAMPIRIC, EliteMutation::BERSERKER, EliteMutation::ARMORED, EliteMutation::ARCANE_SHIELDED,
        EliteMutation::REGENERATING, EliteMutation::DEADLY, EliteMutation::ELUSIVE, EliteMutation::PIERCING,
        EliteMutation::UNSTABLE_MAGIC, EliteMutation::WARFORGED
    };

    const char * eliteMutationName( const EliteMutation mutation )
    {
        switch ( mutation ) {
        case EliteMutation::VAMPIRIC: return "Vampiric";
        case EliteMutation::BERSERKER: return "Berserker";
        case EliteMutation::ARMORED: return "Armored";
        case EliteMutation::ARCANE_SHIELDED: return "Arcane-Shielded";
        case EliteMutation::REGENERATING: return "Regenerating";
        case EliteMutation::DEADLY: return "Deadly";
        case EliteMutation::ELUSIVE: return "Elusive";
        case EliteMutation::PIERCING: return "Piercing";
        case EliteMutation::UNSTABLE_MAGIC: return "Unstable Magic";
        case EliteMutation::WARFORGED: return "Warforged";
        default: return "Unknown";
        }
    }

    const char * eliteMutationDescription( const EliteMutation mutation )
    {
        switch ( mutation ) {
        case EliteMutation::VAMPIRIC: return "+8% life steal";
        case EliteMutation::BERSERKER: return "+10% physical damage";
        case EliteMutation::ARMORED: return "+8% physical reduction";
        case EliteMutation::ARCANE_SHIELDED: return "+8% spell reduction";
        case EliteMutation::REGENERATING: return "+5% turn regeneration";
        case EliteMutation::DEADLY: return "+4% critical chance and +10% critical bonus damage";
        case EliteMutation::ELUSIVE: return "+6% Evasion chance";
        case EliteMutation::PIERCING: return "+10% Armor and Arcane Piercing";
        case EliteMutation::UNSTABLE_MAGIC: return "+12% damaging-spell power";
        case EliteMutation::WARFORGED: return "+3 Attack and +3 Defense";
        default: return "No effect";
        }
    }

    struct HeroChronicle
    {
        uint64_t battleVictories{ 0 };
        uint64_t castleCaptures{ 0 };
        uint64_t eliteVictories{ 0 };
    };

    uint64_t consumeAffordableLevelsWithCarry( Profile & profile );

    Profile playerProfile;
    std::map<PlayerColor, Profile> enemyProfiles;
    std::set<PlayerColor> eliteEnemyColors;
    std::map<PlayerColor, RivalArchetype> eliteRivalArchetypes;
    std::map<PlayerColor, std::vector<EliteMutation>> eliteRivalMutations;
    std::map<int32_t, uint64_t> heroRenownLedger;
    std::map<int32_t, HeroChronicle> heroChronicleLedger;
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

    [[maybe_unused]] uint64_t nextPrestigeLevel( const uint64_t level )
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

    size_t eliteRivalMutationCount( const uint64_t level )
    {
        // Elite kingdoms always have one mutation. Deep Prestige adds a second and third mutation
        // without letting the modifier stack grow without bound.
        return 1 + static_cast<size_t>( std::min<uint64_t>( 2, prestigeRankForLevel( level ) / 3 ) );
    }

    bool hasEliteMutation( const PlayerColor color, const EliteMutation mutation )
    {
        const auto rival = eliteRivalMutations.find( color );
        if ( rival == eliteRivalMutations.end() ) {
            return false;
        }
        return std::find( rival->second.begin(), rival->second.end(), mutation ) != rival->second.end();
    }

    size_t activeEliteMutationCount( const PlayerColor color )
    {
        const auto rival = eliteRivalMutations.find( color );
        return rival == eliteRivalMutations.end() ? 0 : rival->second.size();
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

    size_t heroLegacyTierIndex( const uint64_t renown )
    {
        for ( size_t i = heroLegacyTiers.size(); i > 0; --i ) {
            if ( renown >= heroLegacyTiers[i - 1].threshold ) {
                return i - 1;
            }
        }

        return 0;
    }

    std::string heroLegacyProgressText( const uint64_t renown )
    {
        const size_t tierIndex = heroLegacyTierIndex( renown );
        std::string text = heroLegacyTiers[tierIndex].title;
        text += " - " + formatNumber( renown );

        if ( tierIndex + 1 < heroLegacyTiers.size() ) {
            const HeroLegacyTier & nextTier = heroLegacyTiers[tierIndex + 1];
            const uint64_t remaining = nextTier.threshold > renown ? nextTier.threshold - renown : 0;
            text += " / " + formatNumber( nextTier.threshold );
            text += " (" + formatNumber( remaining ) + " to " + std::string( nextTier.title ) + ")";
        }
        else {
            text += " (maximum legacy tier)";
        }

        return text;
    }

    [[maybe_unused]] const char * heroChronicleEpithet( const HeroChronicle & chronicle )
    {
        if ( chronicle.battleVictories == 0 && chronicle.castleCaptures == 0 && chronicle.eliteVictories == 0 ) {
            return "Unwritten";
        }

        // These display-only weights identify the hero's defining deed profile.
        const long double battleScore = static_cast<long double>( chronicle.battleVictories );
        const long double castleScore = static_cast<long double>( chronicle.castleCaptures ) * 4.0L;
        const long double eliteScore = static_cast<long double>( chronicle.eliteVictories ) * 12.0L;

        // Deliberate deterministic tie priority: Elite > castle > ordinary battle deeds.
        if ( chronicle.eliteVictories > 0 && eliteScore >= castleScore && eliteScore >= battleScore ) {
            return "Rival-Bane";
        }
        if ( chronicle.castleCaptures > 0 && castleScore >= battleScore ) {
            return "Castlebreaker";
        }
        return "Veteran";
    }

    [[maybe_unused]] std::string heroChronicleSummary( const HeroChronicle & chronicle )
    {
        return std::string( heroChronicleEpithet( chronicle ) ) + " | " + formatNumber( chronicle.battleVictories ) + " battle wins, "
               + formatNumber( chronicle.castleCaptures ) + " castles, " + formatNumber( chronicle.eliteVictories ) + " Elite wins";
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

    [[maybe_unused]] const char * ascensionChannelDescription( const AscensionChannel channel )
    {
        switch ( channel ) {
        case AscensionChannel::ATTACK: return "diminishing +4 / +3 / +2 / +1 creature Attack across Ascension I-IV";
        case AscensionChannel::DEFENSE: return "diminishing +4 / +3 / +2 / +1 creature Defense across Ascension I-IV";
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

    long double ascensionStageIncrement( const AscensionChannel channel, const uint8_t stage )
    {
        if ( channel == AscensionChannel::ATTACK || channel == AscensionChannel::DEFENSE ) {
            // Heroes II stores these stats as integers, so each Ascension must cross a real
            // mechanical boundary. The increments still diminish at every milestone.
            switch ( stage ) {
            case 1: return 4.0L;
            case 2: return 3.0L;
            case 3: return 2.0L;
            case 4: return 1.0L;
            default: return 0.0L;
            }
        }

        return ascensionPerTier( channel ) * ascensionStageWeight( stage );
    }

    long double ascensionChannelBonus( const Profile & profile, const AscensionChannel channel )
    {
        long double total = 0.0L;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            if ( ascensionChannels[id] != channel ) {
                continue;
            }

            const uint8_t tier = ascensionTierForRank( profile.ranks[id] );
            for ( uint8_t stage = 1; stage <= tier; ++stage ) {
                total += ascensionStageIncrement( channel, stage );
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
            const long double raw = ascensionStageIncrement( channel, nextTier );
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

    [[maybe_unused]] const char * doctrineResonanceHint( const size_t id )
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

    std::string heroRenownPath()
    {
        return System::concatPath( fheroes2::RPG::dataDirectory(), "hero_renown.dat" );
    }

    bool readHeroRenown( const std::string & path, std::map<int32_t, uint64_t> & ledger )
    {
        std::ifstream input( path );
        int version = 0;
        size_t count = 0;
        if ( !( input >> version >> count ) || version != heroRenownFileVersion || count > 4096 ) {
            return false;
        }

        std::map<int32_t, uint64_t> candidate;
        for ( size_t i = 0; i < count; ++i ) {
            int32_t heroId = -1;
            uint64_t renown = 0;
            if ( !( input >> heroId >> renown ) || heroId < 0 || !candidate.emplace( heroId, renown ).second ) {
                return false;
            }
        }

        input >> std::ws;
        if ( !input.eof() ) {
            return false;
        }

        ledger = std::move( candidate );
        return true;
    }

    void loadHeroRenown()
    {
        heroRenownLedger.clear();

        const std::string path = heroRenownPath();
        for ( const std::string & candidatePath : { path, path + ".bak" } ) {
            std::map<int32_t, uint64_t> loaded;
            if ( readHeroRenown( candidatePath, loaded ) ) {
                heroRenownLedger = std::move( loaded );
                return;
            }
        }
    }

    void saveHeroRenown()
    {
        const std::string path = heroRenownPath();
        const std::string tempPath = path + ".tmp";
        const std::string backupPath = path + ".bak";

        std::ofstream output( tempPath, std::ios::trunc );
        if ( !output ) {
            ERROR_LOG( "Unable to write hero Renown ledger." )
            return;
        }

        output << heroRenownFileVersion << ' ' << heroRenownLedger.size();
        for ( const auto & [heroId, renown] : heroRenownLedger ) {
            output << ' ' << heroId << ' ' << renown;
        }
        output << '\n';
        output.close();
        if ( !output ) {
            System::Unlink( tempPath );
            ERROR_LOG( "Unable to finish hero Renown ledger." )
            return;
        }

        const bool hadOriginal = System::IsFile( path );
        if ( hadOriginal ) {
            System::Unlink( backupPath );
            if ( std::rename( path.c_str(), backupPath.c_str() ) != 0 ) {
                System::Unlink( tempPath );
                ERROR_LOG( "Unable to rotate hero Renown backup." )
                return;
            }
        }

        if ( std::rename( tempPath.c_str(), path.c_str() ) != 0 ) {
            if ( hadOriginal ) {
                static_cast<void>( std::rename( backupPath.c_str(), path.c_str() ) );
            }
            System::Unlink( tempPath );
            ERROR_LOG( "Unable to install hero Renown ledger." )
        }
    }

    std::string heroChroniclePath()
    {
        return System::concatPath( fheroes2::RPG::dataDirectory(), "hero_chronicle.dat" );
    }

    bool readHeroChronicle( const std::string & path, std::map<int32_t, HeroChronicle> & ledger )
    {
        std::ifstream input( path );
        int version = 0;
        size_t count = 0;
        if ( !( input >> version >> count ) || version != heroChronicleFileVersion || count > 4096 ) {
            return false;
        }

        std::map<int32_t, HeroChronicle> candidate;
        for ( size_t i = 0; i < count; ++i ) {
            int32_t heroId = -1;
            HeroChronicle chronicle;
            if ( !( input >> heroId >> chronicle.battleVictories >> chronicle.castleCaptures >> chronicle.eliteVictories )
                 || heroId < 0 || chronicle.eliteVictories > chronicle.battleVictories
                 || !candidate.emplace( heroId, chronicle ).second ) {
                return false;
            }
        }

        input >> std::ws;
        if ( !input.eof() ) {
            return false;
        }

        ledger = std::move( candidate );
        return true;
    }

    void loadHeroChronicle()
    {
        heroChronicleLedger.clear();

        const std::string path = heroChroniclePath();
        for ( const std::string & candidatePath : { path, path + ".bak" } ) {
            std::map<int32_t, HeroChronicle> loaded;
            if ( readHeroChronicle( candidatePath, loaded ) ) {
                heroChronicleLedger = std::move( loaded );
                return;
            }
        }
    }

    void saveHeroChronicle()
    {
        const std::string path = heroChroniclePath();
        const std::string tempPath = path + ".tmp";
        const std::string backupPath = path + ".bak";

        std::ofstream output( tempPath, std::ios::trunc );
        if ( !output ) {
            ERROR_LOG( "Unable to write Hero Chronicle ledger." )
            return;
        }

        output << heroChronicleFileVersion << ' ' << heroChronicleLedger.size();
        for ( const auto & [heroId, chronicle] : heroChronicleLedger ) {
            output << ' ' << heroId << ' ' << chronicle.battleVictories << ' ' << chronicle.castleCaptures << ' ' << chronicle.eliteVictories;
        }
        output << '\n';
        output.close();
        if ( !output ) {
            System::Unlink( tempPath );
            ERROR_LOG( "Unable to finish Hero Chronicle ledger." )
            return;
        }

        const bool hadOriginal = System::IsFile( path );
        if ( hadOriginal ) {
            System::Unlink( backupPath );
            if ( std::rename( path.c_str(), backupPath.c_str() ) != 0 ) {
                System::Unlink( tempPath );
                ERROR_LOG( "Unable to rotate Hero Chronicle backup." )
                return;
            }
        }

        if ( std::rename( tempPath.c_str(), path.c_str() ) != 0 ) {
            if ( hadOriginal ) {
                static_cast<void>( std::rename( backupPath.c_str(), path.c_str() ) );
            }
            System::Unlink( tempPath );
            ERROR_LOG( "Unable to install Hero Chronicle ledger." )
        }
    }

    void recordHeroBattleVictory( const PlayerColor color, const int32_t heroId, const bool eliteVictory )
    {
        if ( color != activePlayerColor || color == PlayerColor::NONE || heroId < 0 ) {
            return;
        }

        HeroChronicle & chronicle = heroChronicleLedger[heroId];
        chronicle.battleVictories = saturatedAdd( chronicle.battleVictories, 1 );
        if ( eliteVictory ) {
            chronicle.eliteVictories = saturatedAdd( chronicle.eliteVictories, 1 );
        }
        saveHeroChronicle();
    }

    void recordHeroCastleCapture( const PlayerColor color, const int32_t heroId )
    {
        if ( color != activePlayerColor || color == PlayerColor::NONE || heroId < 0 ) {
            return;
        }

        HeroChronicle & chronicle = heroChronicleLedger[heroId];
        chronicle.castleCaptures = saturatedAdd( chronicle.castleCaptures, 1 );
        saveHeroChronicle();
    }

    uint64_t addHeroRenown( const PlayerColor color, const int32_t heroId, const uint64_t amount )
    {
        if ( color != activePlayerColor || color == PlayerColor::NONE || heroId < 0 || amount == 0 ) {
            return 0;
        }

        uint64_t & total = heroRenownLedger[heroId];
        const size_t previousTier = heroLegacyTierIndex( total );
        const uint64_t credited = std::min( amount, std::numeric_limits<uint64_t>::max() - total );
        total += credited;
        saveHeroRenown();

        const size_t currentTier = heroLegacyTierIndex( total );
        if ( credited > 0 && currentTier > previousTier ) {
            const Heroes * hero = world.GetHeroes( heroId );
            const std::string heroName = hero != nullptr ? hero->GetName() : "Hero #" + std::to_string( heroId );

            std::string message = heroName + " has earned the " + std::string( heroLegacyTiers[currentTier].title ) + " legacy title.";
            message += "\n\nHero Renown: " + formatNumber( total );
            if ( currentTier + 1 < heroLegacyTiers.size() ) {
                const HeroLegacyTier & nextTier = heroLegacyTiers[currentTier + 1];
                message += "\nNext title: " + std::string( nextTier.title ) + " at " + formatNumber( nextTier.threshold ) + " Renown";
            }
            else {
                message += "\nThis is the highest Hero Legacy tier.";
            }

            fheroes2::showStandardTextMessage( "Hero Legacy", std::move( message ), Dialog::OK );
        }

        return credited;
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

    [[maybe_unused]] uint64_t totalSpentPointsOnUpgrade( const Profile & profile, const size_t id )
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

    void showUpgradeDetails( const size_t id, const int buttons = Dialog::ZERO )
    {
        if ( id >= upgradeCount ) {
            return;
        }

        const uint64_t rank = playerProfile.ranks[id];
        const bool canAdvance = doctrineCanAdvance( id, rank );
        const uint8_t ascensionTier = ascensionTierForRank( rank );
        const bool prerequisiteMet = id != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;

        std::string message = upgradeDetails[id];
        message += "\n\nCurrent: Rank " + formatNumber( rank ) + " (" + shortEffectSummary( id, rank ) + ")";
        if ( ascensionTier > 0 ) {
            message += "\nAscension: " + std::string( ascensionTierLabel( ascensionTier ) ) + " - " + ascensionNames[id];
        }
        const uint64_t nextMilestone = nextAscensionRank( rank );
        if ( nextMilestone > 0 ) {
            message += "\nNext Ascension: Rank " + formatNumber( nextMilestone );
        }
        else {
            message += "\nAscension: Mastered";
        }

        if ( !canAdvance ) {
            message += "\nNext: MAX rank reached";
        }
        else if ( !prerequisiteMet ) {
            message += "\nNext: LOCKED (requires Critical Training)";
        }
        else {
            const uint64_t price = cost( id, rank );
            message += "\nNext: Rank " + formatNumber( rank + 1 ) + " (" + shortEffectSummary( id, rank + 1 ) + ")";
            const uint8_t nextAscensionTier = ascensionTierForRank( rank + 1 );
            if ( nextAscensionTier > ascensionTier ) {
                message += "\nUnlocks: " + std::string( ascensionTierLabel( nextAscensionTier ) ) + " - " + ascensionNames[id];
            }
            message += "\nCost: " + formatNumber( price ) + " points ("
                       + ( playerProfile.points >= price ? "Ready" : "Need " + formatNumber( price - playerProfile.points ) ) + ")";
        }
        message += "\nAvailable points: " + formatNumber( playerProfile.points );

        fheroes2::showStandardTextMessage( upgrades[id].name, std::move( message ), buttons );
    }

    void showTabDetails( const size_t tabIndex, const int buttons = Dialog::ZERO )
    {
        if ( tabIndex >= tabNames.size() ) {
            return;
        }

        constexpr std::array<const char *, 8> tabDescriptions{
            "Martial training: Attack, Defense, and life sustain.",
            "Physical offense: Creature strikes and damage opportunism.",
            "Battle tactics: Positional advantages and armor penetration.",
            "Defensive guard: Damage reduction, wards, and bulwarks.",
            "Arcane spellcasting: Hero spell damage and elemental magic.",
            "Arcane warding: Hero and elemental spell resistance.",
            "Command: Morale, Luck, regeneration, and critical strikes.",
            "Combat mastery: Evasion, spell piercing, and tactical edge."
        };

        std::string message = tabDescriptions[tabIndex];
        message += "\n\nDoctrines:";
        for ( size_t offset = 0; offset < upgradesPerTab; ++offset ) {
            const size_t id = tabIndex * upgradesPerTab + offset;
            const uint64_t rank = playerProfile.ranks[id];
            message += "\n- " + std::string( upgrades[id].name ) + " (Rank " + formatNumber( rank ) + "): " + shortEffectSummary( id, rank );
        }
        fheroes2::showStandardTextMessage( tabNames[tabIndex], std::move( message ), buttons );
    }

    void showRoyalGuildHelp( const int buttons = Dialog::ZERO )
    {
        std::string message = "Keyboard Controls:\n";
        message += "1-8: Doctrine halls\n";
        message += "Up/Down: Select doctrine\n";
        message += "B / Enter / Space: Buy doctrine\n";
        message += "I: Inspect doctrine\n";
        message += "O: Guild Overview\n";
        message += "K: Build Analytics\n";
        message += "V: Elite Rival Intel\n";
        message += "S / A: Steward Auto-Buy\n";
        message += "R: Respec doctrines\n";
        message += "Esc / F9: Close menu\n\n";
        message += "Mouse:\n";
        message += "Click BUY to purchase.\n";
        message += "Right-click any item for quick info.";

        fheroes2::showStandardTextMessage( "Royal Guild Help", std::move( message ), buttons );
    }

    void showBuildAnalytics( const int buttons = Dialog::ZERO )
    {
        const uint64_t totalInvested = totalSpentPoints( playerProfile );
        const uint64_t earnedPoints = saturatedMultiply( playerProfile.level - 1, pointsPerLevel );

        std::string message = "Earned Points: " + formatNumber( earnedPoints );
        message += "\nInvested Points: " + formatNumber( totalInvested );
        message += "\nAvailable Points: " + formatNumber( playerProfile.points );

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
                       + std::string( tabNames[static_cast<size_t>( std::distance( triggersByTab.begin(), activityFocus ) )] );
        }
        message += "\nTotal Battle Triggers: " + formatNumber( totalTriggers );

        size_t ascendedDoctrines = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            if ( ascensionTierForRank( playerProfile.ranks[id] ) > 0 ) {
                ++ascendedDoctrines;
            }
        }
        message += "\nAscended Doctrines: " + formatNumber( ascendedDoctrines ) + " / " + formatNumber( upgradeCount );

        const StewardGoal stewardGoal = findStewardGoal( playerProfile );
        if ( stewardGoal.id < upgradeCount ) {
            message += "\nSteward Goal: " + std::string( upgrades[stewardGoal.id].name ) + " -> R" + formatNumber( stewardGoal.targetRank );
        }

        message += "\nProfile Status: ";
        message += isCurrentProfileStateValid( playerProfile ) ? "Valid" : "Protected";

        fheroes2::showStandardTextMessage( "Build Analytics", std::move( message ), buttons );
    }

    void showRivalIntel( const int buttons = Dialog::ZERO )
    {
        std::array<size_t, rivalArchetypeNames.size()> archetypeCounts{};
        for ( const auto & [color, archetype] : eliteRivalArchetypes ) {
            static_cast<void>( color );
            const size_t index = static_cast<size_t>( archetype );
            if ( index > 0 && index <= archetypeCounts.size() ) {
                ++archetypeCounts[index - 1];
            }
        }

        std::string message = "Encounter Chance: " + std::to_string( eliteRivalChanceForLevel( playerProfile.level ) ) + "%";
        message += "\nElite Rivals This Map: " + formatNumber( eliteEnemyColors.size() );
        message += "\nMax Focus Halls: " + formatNumber( eliteRivalFocusHallLimit( playerProfile.level ) );
        message += "\nMutations per Elite: " + formatNumber( eliteRivalMutationCount( playerProfile.level ) );

        std::string archetypes;
        for ( size_t index = 0; index < archetypeCounts.size(); ++index ) {
            if ( archetypeCounts[index] > 0 ) {
                if ( !archetypes.empty() ) {
                    archetypes += ", ";
                }
                archetypes += std::string( rivalArchetypeNames[index] ) + " (" + formatNumber( archetypeCounts[index] ) + ")";
            }
        }
        message += "\nArchetypes: " + ( archetypes.empty() ? "None active" : archetypes );

        if ( !eliteRivalMutations.empty() ) {
            message += "\nMutations: Active";
        }

        fheroes2::showStandardTextMessage( "Elite Rival Intel", std::move( message ), buttons );
    }

    void showKingdomOverview( const int buttons = Dialog::ZERO )
    {
        const uint64_t remainingXP = xpToNextLevel( playerProfile.level ) > playerProfile.progress
                                         ? xpToNextLevel( playerProfile.level ) - playerProfile.progress
                                         : 0;
        const uint64_t totalInvested = totalSpentPoints( playerProfile );
        const uint64_t nextLevelCost = xpToNextLevel( playerProfile.level );

        std::string message = "Kingdom Level: " + formatNumber( playerProfile.level );
        const uint64_t prestigeRank = prestigeRankForLevel( playerProfile.level );
        if ( prestigeRank > 0 ) {
            message += " (Prestige " + formatNumber( prestigeRank ) + ")";
        }
        message += "\nAvailable Points: " + formatNumber( playerProfile.points );
        message += "\nInvested Points: " + formatNumber( totalInvested );
        message += "\nRenown XP: " + formatNumber( playerProfile.progress ) + " / " + formatNumber( nextLevelCost );
        message += " (" + formatNumber( remainingXP ) + " to next)";
        message += "\nLifetime Renown: " + formatNumber( playerProfile.experience );

        size_t activeDoctrines = 0;
        for ( const uint64_t rank : playerProfile.ranks ) {
            if ( rank > 0 ) {
                ++activeDoctrines;
            }
        }
        message += "\nActive Doctrines: " + formatNumber( activeDoctrines ) + " / " + formatNumber( upgradeCount );

        std::array<uint64_t, tabNames.size()> investedByTab{};
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            investedByTab[id / upgradesPerTab] = saturatedAdd( investedByTab[id / upgradesPerTab], rankInvestment( id, playerProfile.ranks[id] ) );
        }
        const auto focusIt = std::max_element( investedByTab.begin(), investedByTab.end() );
        if ( focusIt != investedByTab.end() && *focusIt > 0 ) {
            const size_t focusTab = static_cast<size_t>( std::distance( investedByTab.begin(), focusIt ) );
            message += "\nBuild Focus: " + std::string( tabNames[focusTab] );
        }

        if ( !heroRenownLedger.empty() ) {
            const auto topHero = std::max_element( heroRenownLedger.begin(), heroRenownLedger.end(), []( const auto & left, const auto & right ) {
                return left.second < right.second;
            } );
            const Heroes * hero = world.GetHeroes( topHero->first );
            const std::string heroName = hero != nullptr ? hero->GetName() : "Hero #" + std::to_string( topHero->first );
            message += "\nTop Hero: " + heroName + " (" + heroLegacyProgressText( topHero->second ) + ")";
        }

        message += "\nSteward Auto-Buyer: ";
        message += playerProfile.autoBuy ? "Active (ON)" : "Paused (OFF)";

        fheroes2::showStandardTextMessage( "Royal Guild Overview", std::move( message ), buttons );
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
                                       "offline_progress.dat", "offline_progress.dat.tmp", "offline_progress.dat.bak",
                                       "hero_renown.dat", "hero_renown.dat.tmp", "hero_renown.dat.bak",
                                       "hero_chronicle.dat", "hero_chronicle.dat.tmp", "hero_chronicle.dat.bak" } ) {
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
    eliteRivalMutations.clear();
    heroRenownLedger.clear();
    heroChronicleLedger.clear();
    visitedActionTiles.clear();
    playerProfile = {};
    if ( playerColor == PlayerColor::NONE ) {
        return;
    }

    loadHeroRenown();
    loadHeroChronicle();

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

    // Temporary enemy RPG builds scale with RNG from the player's profile and points budget,
    // generating distinct randomized builds across doctrine archetypes rather than mimicking
    // the player's profile.
    uint64_t seed = static_cast<uint64_t>( world.GetMapSeed() ) << 32;
    // Unsigned multiplication intentionally wraps here: this is a hash mix, not arithmetic progression.
    seed ^= playerProfile.level * 0x9E3779B185EBCA87ULL;
    seed ^= static_cast<uint64_t>( playerColor ) * 0xC2B2AE3D27D4EB4FULL;
    std::mt19937_64 rng( seed );

    const uint64_t playerSpentPoints = totalSpentPoints( playerProfile );
    const uint64_t playerTotalPoints = saturatedAdd( playerSpentPoints, playerProfile.points );
    const uint64_t playerLevelPoints = saturatedMultiply( playerProfile.level > 0 ? playerProfile.level - 1 : 0, pointsPerLevel );
    const uint64_t playerBudget = std::max( playerTotalPoints, playerLevelPoints );

    // Elite rivals are deterministic for this map because they use the same seeded generator.
    // They only begin appearing after a few RPG levels. Guild Prestige raises the late-game
    // encounter rate gradually, while the hard cap keeps ordinary rival kingdoms common.
    const int eliteChance = eliteRivalChanceForLevel( playerProfile.level );
    std::uniform_int_distribution<int> eliteRoll( 0, 99 );
    std::uniform_int_distribution<size_t> archetypeRoll( 0, rivalArchetypeNames.size() - 1 );

    const auto makeTemporaryProfile
        = [&rng, playerBudget, &archetypeRoll]( const int minimumPower, const int maximumPower, const bool roundUpSmallRanks,
                                                const bool sophisticatedKingdom, const bool eliteKingdom, const RivalArchetype archetype ) {
              std::uniform_int_distribution<int> variation( minimumPower, maximumPower );
              const int powerPercent = variation( rng );

              Profile temporary;
              temporary.level = std::max<uint64_t>( 1, scaledValue( playerProfile.level, powerPercent ) );

              // Total doctrine point budget scales with RNG from the player's own profile.
              uint64_t enemyBudget = scaledValue( playerBudget, powerPercent );
              if ( enemyBudget == 0 && playerBudget > 0 && powerPercent > 0 && roundUpSmallRanks ) {
                  enemyBudget = 1;
              }
              temporary.points = enemyBudget;

              if ( enemyBudget == 0 ) {
                  return temporary;
              }

              // Determine archetype for this enemy build
              const RivalArchetype effectiveArchetype
                  = archetype != RivalArchetype::NONE ? archetype : static_cast<RivalArchetype>( archetypeRoll( rng ) + 1 );

              // Determine focus halls
              size_t focusTabCount = 1;
              if ( sophisticatedKingdom ) {
                  focusTabCount = eliteKingdom ? std::min<size_t>( tabNames.size(), eliteRivalFocusHallLimit( playerProfile.level ) )
                                               : powerPercent >= 105 ? 3 : powerPercent >= 95 ? 2 : 1;
              }

              std::array<size_t, tabNames.size()> sortedTabs{};
              for ( size_t i = 0; i < tabNames.size(); ++i ) {
                  sortedTabs[i] = i;
              }

              std::array<uint64_t, tabNames.size()> tabScores{};
              std::uniform_int_distribution<uint64_t> hallJitter( 0, 30 );
              for ( size_t tab = 0; tab < tabNames.size(); ++tab ) {
                  tabScores[tab] = saturatedAdd( rivalArchetypeHallBias( effectiveArchetype, tab ), hallJitter( rng ) );
              }
              std::sort( sortedTabs.begin(), sortedTabs.end(), [&tabScores]( const size_t first, const size_t second ) {
                  return tabScores[first] > tabScores[second];
              } );

              std::array<size_t, tabNames.size()> focusTabs{};
              for ( size_t i = 0; i < focusTabCount; ++i ) {
                  focusTabs[i] = sortedTabs[i];
              }

              // Doctrine weights based on archetype, focus halls, and RNG jitter
              std::array<int, upgradeCount> doctrineWeights{};
              std::uniform_int_distribution<int> docJitter( 8, 24 );
              for ( size_t id = 0; id < upgradeCount; ++id ) {
                  const size_t tab = id / upgradesPerTab;
                  int weight = docJitter( rng );

                  const uint64_t hallBias = rivalArchetypeHallBias( effectiveArchetype, tab );
                  weight += static_cast<int>( hallBias / ( eliteKingdom ? 2 : 3 ) );

                  const int docBias = rivalArchetypeDoctrineBias( effectiveArchetype, id );
                  weight += docBias * ( eliteKingdom ? 6 : 4 );

                  for ( size_t f = 0; f < focusTabCount; ++f ) {
                      if ( focusTabs[f] == tab ) {
                          weight += static_cast<int>( ( focusTabCount - f ) * 10 );
                          break;
                      }
                  }

                  doctrineWeights[id] = std::max( 1, weight );
              }

              // Allocate doctrine ranks using weighted random selection until budget is spent
              while ( temporary.points > 0 ) {
                  std::vector<size_t> candidates;
                  std::vector<int> candidateWeights;
                  candidates.reserve( upgradeCount );
                  candidateWeights.reserve( upgradeCount );

                  for ( size_t id = 0; id < upgradeCount; ++id ) {
                      if ( !doctrineCanAdvance( id, temporary.ranks[id] ) ) {
                          continue;
                      }
                      if ( cost( id, temporary.ranks[id] ) > temporary.points ) {
                          continue;
                      }
                      if ( id == BRUTAL_CRITICALS && temporary.ranks[CRITICAL_TRAINING] == 0 ) {
                          continue;
                      }

                      int w = doctrineWeights[id];

                      // Synergy bonuses when related doctrines are acquired
                      if ( id == BRUTAL_CRITICALS && temporary.ranks[CRITICAL_TRAINING] > 0 ) {
                          w += 35;
                      }
                      if ( ( id == PYROMANCY || id == CRYOMANCY || id == STORMCRAFT || id == CATACLYSM || id == ARCANE_PIERCING )
                           && temporary.ranks[SORCERY] > 0 ) {
                          w += 30;
                      }
                      if ( ( id == FIRE_WARD || id == COLD_WARD || id == STORM_WARD || id == CATACLYSM_WARD )
                           && temporary.ranks[SPELL_WARD] > 0 ) {
                          w += 25;
                      }
                      if ( id == REAPER && temporary.ranks[BLOOD_DRINKER] > 0 ) {
                          w += 20;
                      }
                      if ( id == RUTHLESS && temporary.ranks[EXECUTIONER] > 0 ) {
                          w += 20;
                      }
                      if ( id == CLOSE_QUARTERS && temporary.ranks[MARKSMAN] > 0 ) {
                          w += 20;
                      }
                      if ( id == LAST_STAND && temporary.ranks[FRENZY] > 0 ) {
                          w += 20;
                      }

                      // Taper weight for already-high ranks to encourage cohesive builds rather than dumping into a single skill
                      const int rankPenalty = static_cast<int>( temporary.ranks[id] * 3 );
                      w = std::max( 1, w - rankPenalty );

                      candidates.push_back( id );
                      candidateWeights.push_back( w );
                  }

                  if ( candidates.empty() ) {
                      break;
                  }

                  std::discrete_distribution<size_t> dist( candidateWeights.begin(), candidateWeights.end() );
                  const size_t pick = candidates[dist( rng )];
                  if ( !buy( temporary, pick ) ) {
                      break;
                  }
              }

              if ( temporary.ranks[CRITICAL_TRAINING] == 0 ) {
                  temporary.ranks[BRUTAL_CRITICALS] = 0;
              }

              return temporary;
          };

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

            std::array<EliteMutation, eliteMutationPool.size()> mutationPool = eliteMutationPool;
            std::shuffle( mutationPool.begin(), mutationPool.end(), rng );
            const size_t mutationCount = std::min( eliteRivalMutationCount( playerProfile.level ), mutationPool.size() );
            auto & mutations = eliteRivalMutations[player->GetColor()];
            mutations.assign( mutationPool.begin(), mutationPool.begin() + mutationCount );

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
        saveHeroRenown();
        saveHeroChronicle();
    }
    activePlayerColor = PlayerColor::NONE;
    enemyProfiles.clear();
    eliteEnemyColors.clear();
    eliteRivalArchetypes.clear();
    eliteRivalMutations.clear();
    heroRenownLedger.clear();
    heroChronicleLedger.clear();
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
                                  const bool defending, const bool siege, const int32_t heroId )
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
        // Each active mutation increases the reward for defeating the more dangerous Elite.
        base *= 1.0L + static_cast<long double>( activeEliteMutationCount( opponent ) ) * 0.05L;
    }
    const long double earned = base * challenge;
    const uint64_t reward = static_cast<uint64_t>( std::min( earned, static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) );
    static_cast<void>( addExperience( color, reward, ExperienceKind::BATTLE ) );

    // Individual Hero Renown is a separate progression record. It deliberately does not alter
    // combat stats, doctrine ranks, kingdom level or the kingdom-level Renown economy.
    if ( won ) {
        static_cast<void>( addHeroRenown( color, heroId, reward ) );
        const bool eliteVictory = opponent != PlayerColor::NONE && eliteEnemyColors.count( opponent ) > 0;
        recordHeroBattleVictory( color, heroId, eliteVictory );
    }
}

void fheroes2::RPG::awardTownCapture( const PlayerColor color, const int32_t heroId )
{
    static_cast<void>( addHeroRenown( color, heroId, heroCaptureRenown ) );
    recordHeroCastleCapture( color, heroId );
}

uint64_t fheroes2::RPG::heroRenown( const int32_t heroId )
{
    const auto found = heroRenownLedger.find( heroId );
    return found == heroRenownLedger.end() ? 0 : found->second;
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
    const uint64_t mutation = hasEliteMutation( color, EliteMutation::WARFORGED ) ? 3 : 0;
    return saturatedAdd( saturatedAdd( base, ascension ), mutation );
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
    const uint64_t mutation = hasEliteMutation( color, EliteMutation::WARFORGED ) ? 3 : 0;
    return saturatedAdd( saturatedAdd( base, ascension ), mutation );
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
                                      + ascensionChannelBonus( *profile, AscensionChannel::LIFE_STEAL )
                                      + ( hasEliteMutation( color, EliteMutation::VAMPIRIC ) ? 8.0L : 0.0L ) );
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
                                      + ascensionChannelBonus( *profile, AscensionChannel::REGENERATION )
                                      + ( hasEliteMutation( color, EliteMutation::REGENERATING ) ? 5.0L : 0.0L ) );
}

double fheroes2::RPG::criticalChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 0.0
               : static_cast<double>( std::min<long double>(
                     100.0L, effect( CRITICAL_TRAINING, profile->ranks[CRITICAL_TRAINING] )
                                 + ascensionChannelBonus( *profile, AscensionChannel::CRITICAL_CHANCE )
                                 + ( hasEliteMutation( color, EliteMutation::DEADLY ) ? 4.0L : 0.0L ) ) );
}

double fheroes2::RPG::criticalDamageBonusPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 50.0
               : 50.0 + static_cast<double>( effect( BRUTAL_CRITICALS, profile->ranks[BRUTAL_CRITICALS] )
                                             + ascensionChannelBonus( *profile, AscensionChannel::CRITICAL_DAMAGE )
                                             + ( hasEliteMutation( color, EliteMutation::DEADLY ) ? 10.0L : 0.0L ) );
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
                                 + ascensionChannelBonus( *profile, AscensionChannel::EVASION )
                                 + ( hasEliteMutation( color, EliteMutation::ELUSIVE ) ? 6.0L : 0.0L ) ) );
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
        if ( hasEliteMutation( attacker, EliteMutation::BERSERKER ) ) {
            attackBonus += 10.0L;
        }
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
        if ( hasEliteMutation( defender, EliteMutation::ARMORED ) ) {
            defenseReduction += 8.0L;
        }
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
                       + ascensionChannelBonus( *attackProfile, AscensionChannel::ARMOR_PIERCING )
                       + ( hasEliteMutation( attacker, EliteMutation::PIERCING ) ? 10.0L : 0.0L ) );
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
                    + ascensionChannelBonus( *attackProfile, AscensionChannel::SPELL_DAMAGE )
                    + ( hasEliteMutation( attacker, EliteMutation::UNSTABLE_MAGIC ) ? 12.0L : 0.0L );
    long double defenseReduction
        = defenseProfile == nullptr
              ? 0
              : effect( SPELL_WARD, defenseProfile->ranks[SPELL_WARD] )
                    + ascensionChannelBonus( *defenseProfile, AscensionChannel::SPELL_REDUCTION )
                    + ( hasEliteMutation( defender, EliteMutation::ARCANE_SHIELDED ) ? 8.0L : 0.0L );

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
                       + ascensionChannelBonus( *attackProfile, AscensionChannel::ARCANE_PIERCING )
                       + ( hasEliteMutation( attacker, EliteMutation::PIERCING ) ? 10.0L : 0.0L ) );
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

        if ( event.isMouseRightButtonPressedInArea( closeButton.area() ) || event.MouseLongPressLeft( closeButton.area() ) ) {
            fheroes2::showStandardTextMessage( "Close", "Exit the Royal Guild menu.", Dialog::ZERO );
            redraw = true;
            continue;
        }

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
        if ( event.isMouseRightButtonPressedInArea( scrollUp.area() ) || event.MouseLongPressLeft( scrollUp.area() ) ) {
            fheroes2::showStandardTextMessage( "Scroll Up", "Scroll up one doctrine row.", Dialog::ZERO );
            redraw = true;
            continue;
        }
        if ( ( event.isMouseWheelUpInArea( listArea ) || event.MouseClickLeft( scrollUp.area() ) ) && scrollOffset > 0 ) {
            --scrollOffset;
            selectedOffsets[tab] = std::clamp( selectedOffsets[tab], scrollOffset, scrollOffset + visibleRows - 1 );
            redraw = true;
            continue;
        }
        if ( event.isMouseRightButtonPressedInArea( scrollDown.area() ) || event.MouseLongPressLeft( scrollDown.area() ) ) {
            fheroes2::showStandardTextMessage( "Scroll Down", "Scroll down one doctrine row.", Dialog::ZERO );
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
        if ( event.isMouseRightButtonPressedInArea( scrollTrack ) || event.MouseLongPressLeft( scrollTrack ) ) {
            fheroes2::showStandardTextMessage( "Scrollbar", "Click to scroll through doctrines.", Dialog::ZERO );
            redraw = true;
            continue;
        }
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
            showUpgradeDetails( tab * upgradesPerTab + selectedOffsets[tab], Dialog::OK );
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_H ) ) {
            showRoyalGuildHelp( Dialog::OK );
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_V ) ) {
            showRivalIntel( Dialog::OK );
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_K ) ) {
            showBuildAnalytics( Dialog::OK );
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_O ) || event.MouseClickLeft( statsArea ) ) {
            showKingdomOverview( Dialog::OK );
            redraw = true;
            continue;
        }
        if ( event.isMouseRightButtonPressedInArea( statsArea ) || event.MouseLongPressLeft( statsArea ) ) {
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
                    showUpgradeDetails( i, Dialog::OK );
                }
                redraw = true;
                break;
            }
            if ( event.MouseClickLeft( visibleUpgradeAreas[row] ) ) {
                selectedOffsets[tab] = scrollOffset + row;
                showUpgradeDetails( i, Dialog::OK );
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
                showUpgradeDetails( i, Dialog::OK );
            }
            redraw = true;
            continue;
        }

        if ( event.isMouseRightButtonPressedInArea( autoButton.area() ) || event.MouseLongPressLeft( autoButton.area() ) ) {
            fheroes2::showStandardTextMessage(
                "Steward",
                "Automatically buys recommended doctrines based on combat focus and available points. Toggle with S, A, or left-click.",
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
                "Refund all spent guild points and reset doctrine ranks to zero to freely reallocate your build. Left-click or press R to respec.",
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

