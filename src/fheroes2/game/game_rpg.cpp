#include "game_rpg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
    constexpr int profileVersion = 6;
    using namespace fheroes2::RPG;

    uint64_t xpToNextLevel( uint64_t level );

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
        "Adds +1 Attack per rank to every creature stack controlled by this RPG profile, up to +8 Attack.",
        "Adds +1 Defense per rank to every creature stack controlled by this RPG profile, up to +8 Defense.",
        "Adds +1 Attack and +1 Defense per rank to every creature stack, up to +4 of each. Its ranks cost more because both stats increase together.",
        "Whenever one of your creature stacks deals attack damage, it heals for up to 20% of the actual damage dealt. Healing repairs the surviving stack but does not resurrect killed creatures.",
        "Whenever one of your attacks kills creatures, the attacking stack heals for up to 30% of the slain creatures' hit points. It cannot resurrect creatures already lost from that stack.",

        "Increases all physical damage dealt by your creature stacks.",
        "Increases physical damage dealt by ranged attacks.",
        "Increases physical damage dealt by melee attacks.",
        "Increases physical damage against any stack that has already lost hit points or creatures.",
        "Increases physical damage against a stack while it is currently at its full starting battle hit points.",

        "Increases physical damage when the attacking stack has fewer creatures than its target.",
        "Increases physical damage when the attacking stack has more creatures than its target.",
        "Increases physical damage while the attacking stack is below half of its starting battle hit points.",
        "Increases physical damage while the attacking stack is still at its full starting battle hit points.",
        "Ignores up to 60% of the defender's RPG physical damage reduction after all applicable defensive upgrades are combined.",

        "Reduces all physical damage received by your creature stacks. Combined RPG physical reduction is capped at 70% before Armor Piercing.",
        "Adds extra damage reduction against ranged creature attacks.",
        "Adds extra damage reduction against melee creature attacks.",
        "Adds extra physical damage reduction while the defending stack is below half of its starting battle hit points.",
        "Adds extra physical damage reduction while the defending stack has fewer creatures than its attacker.",

        "Increases damage from every damaging spell cast by a hero using this RPG profile.",
        "Adds damage to Fireball and Fireblast.",
        "Adds damage to Cold Ray and Cold Ring.",
        "Adds damage to Lightning Bolt and Chain Lightning.",
        "Adds damage to Elemental Storm, Meteor Shower, and Armageddon.",

        "Reduces damage received from every damaging spell. Combined RPG spell reduction is capped at 70% before Arcane Piercing.",
        "Adds RPG resistance against Fireball and Fireblast.",
        "Adds RPG resistance against Cold Ray and Cold Ring.",
        "Adds RPG resistance against Lightning Bolt and Chain Lightning.",
        "Adds RPG resistance against Elemental Storm, Meteor Shower, and Armageddon.",

        "Adds Morale to your creature stacks during combat, up to +3 from this upgrade.",
        "Adds Luck to your creature stacks during combat, up to +3 from this upgrade.",
        "At the beginning of a stack's turn, restores up to 15% of one creature's maximum hit points to the surviving stack. This repairs the wounded top creature but never resurrects dead creatures.",
        "Gives each creature attack up to a 20% chance to become a critical hit. A critical hit deals 50% extra damage before Brutal Criticals is added.",
        "Increases the bonus damage of critical hits beyond their normal +50%. Requires at least one rank of Critical Training.",

        "Gives your creature stacks up to a 15% chance to reduce an incoming creature attack's final damage by 50%.",
        "Ignores up to 60% of the target's combined RPG spell resistance when your hero casts a damaging spell.",
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

    Profile playerProfile;
    std::map<PlayerColor, Profile> enemyProfiles;
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

    uint64_t linearRankInvestment( const uint64_t rank, const uint64_t baseCost, const uint64_t rankCost )
    {
        return saturatedAdd( saturatedMultiply( rank, baseCost ), saturatedMultiply( saturatedTriangular( rank ), rankCost ) );
    }

    uint64_t steppedRankInvestment( const uint64_t rank, const uint64_t baseCost, const uint64_t groupSize )
    {
        assert( groupSize > 0 );
        const uint64_t completeGroups = rank / groupSize;
        const uint64_t remainder = rank % groupSize;
        const uint64_t completedGroupSteps = saturatedMultiply( groupSize, saturatedTriangular( completeGroups ) );
        const uint64_t remainderSteps = saturatedMultiply( completeGroups, remainder );
        return saturatedAdd( saturatedMultiply( rank, baseCost ), saturatedAdd( completedGroupSteps, remainderSteps ) );
    }

    uint64_t rankInvestment( const size_t id, const uint64_t rank )
    {
        switch ( id ) {
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
            return linearRankInvestment( rank, 2, 1 );
        case VETERAN_CORE:
            return linearRankInvestment( rank, 3, 2 );
        case LEADERSHIP:
        case FORTUNE:
            return linearRankInvestment( rank, 2, 2 );
        case BLOOD_DRINKER:
        case REAPER:
        case REGENERATION:
            return steppedRankInvestment( rank, 1, 4 );
        case CRITICAL_TRAINING:
        case BRUTAL_CRITICALS:
        case EVASION:
            return steppedRankInvestment( rank, 2, 3 );
        default:
            return steppedRankInvestment( rank, 1, 5 );
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

    long double percentEffect( const uint64_t rank )
    {
        return 4.0L * std::log1p( static_cast<long double>( rank ) );
    }

    long double effect( const size_t id, const uint64_t rank )
    {
        if ( rank == 0 ) {
            return 0;
        }

        switch ( id ) {
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
            return static_cast<long double>( std::min<uint64_t>( rank, 8 ) );
        case VETERAN_CORE:
            return static_cast<long double>( std::min<uint64_t>( rank, 4 ) );
        case LEADERSHIP:
        case FORTUNE:
            return static_cast<long double>( std::min<uint64_t>( rank, 3 ) );
        case BLOOD_DRINKER:
            return std::min<long double>( 20.0L, 3.0L * std::log1p( static_cast<long double>( rank ) ) );
        case REAPER:
            return std::min<long double>( 30.0L, 4.0L * std::log1p( static_cast<long double>( rank ) ) );
        case REGENERATION:
            return std::min<long double>( 15.0L, 2.5L * std::log1p( static_cast<long double>( rank ) ) );
        case CRITICAL_TRAINING:
            return std::min<long double>( 20.0L, 4.0L * std::log1p( static_cast<long double>( rank ) ) );
        case BRUTAL_CRITICALS:
            return std::min<long double>( 100.0L, 8.0L * std::log1p( static_cast<long double>( rank ) ) );
        case EVASION:
            return std::min<long double>( 15.0L, 2.5L * std::log1p( static_cast<long double>( rank ) ) );
        case ARMOR_PIERCING:
        case ARCANE_PIERCING:
            return std::min<long double>( 60.0L, 6.0L * std::log1p( static_cast<long double>( rank ) ) );
        case CLOSE_QUARTERS:
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
            return std::min<long double>( 50.0L, 3.5L * std::log1p( static_cast<long double>( rank ) ) );
        default:
            return percentEffect( rank );
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

    bool isCurrentProfileStateValid( const Profile & profile )
    {
        // Current-format profiles have a closed guild-point ledger: every point is either
        // still available or was spent through the current doctrine price curve.
        uint64_t investedPoints = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            const uint64_t rank = profile.ranks[id];

            // A stored rank must have produced a real mechanical increase when it was bought.
            // This rejects parseable corruption that pushes bounded doctrines beyond their
            // effective cap, which could otherwise mint saturated refunds during a respec.
            if ( rank > 0 && effect( id, rank ) <= effect( id, rank - 1 ) ) {
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
        return profile.progress <= profile.experience && profile.progress < xpToNextLevel( profile.level );
    }

    uint64_t cost( const size_t id, const uint64_t rank )
    {
        switch ( id ) {
        case ARMS_TRAINING:
        case ARMOR_TRAINING:
            return saturatedAdd( 2, rank );
        case VETERAN_CORE:
            return saturatedAdd( 3, saturatedMultiply( 2, rank ) );
        case LEADERSHIP:
        case FORTUNE:
            return saturatedAdd( 2, saturatedMultiply( 2, rank ) );
        case BLOOD_DRINKER:
        case REAPER:
        case REGENERATION:
            return saturatedAdd( 1, rank / 4 );
        case CRITICAL_TRAINING:
        case BRUTAL_CRITICALS:
        case EVASION:
            return saturatedAdd( 2, rank / 3 );
        default:
            return saturatedAdd( 1, rank / 5 );
        }
    }

    bool buy( Profile & profile, const size_t id )
    {
        if ( id >= upgradeCount || profile.ranks[id] == std::numeric_limits<uint64_t>::max()
             || effect( id, profile.ranks[id] + 1 ) <= effect( id, profile.ranks[id] )
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
            return delta * 7.0L;
        case VETERAN_CORE:
            return delta * 12.0L;
        case BLOOD_DRINKER:
            return delta * 0.75L;
        case REAPER:
            return delta * 0.45L;
        case FEROCITY:
        case IRON_SKIN:
        case SORCERY:
        case SPELL_WARD:
            return delta;
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
            return delta * 0.70L;
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
            return delta * 0.55L;
        case ARMOR_PIERCING:
        case ARCANE_PIERCING:
            return delta * 0.45L;
        case LEADERSHIP:
        case FORTUNE:
            return delta * 4.0L;
        case REGENERATION:
            return delta * 0.80L;
        case CRITICAL_TRAINING:
            return delta * ( 50.0L + effect( BRUTAL_CRITICALS, profile.ranks[BRUTAL_CRITICALS] ) ) / 100.0L;
        case BRUTAL_CRITICALS:
            return delta * effect( CRITICAL_TRAINING, profile.ranks[CRITICAL_TRAINING] ) / 100.0L;
        case EVASION:
            return delta * 0.50L;
        case CLOSE_QUARTERS:
            return delta * 0.35L;
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
            return 1.0L + std::min( 0.35L, familiarity );
        }
        default:
            return 1.0L;
        }
    }

    long double autoBuyMarginalReturn( const Profile & profile, const size_t id )
    {
        const uint64_t rank = profile.ranks[id];
        if ( rank == std::numeric_limits<uint64_t>::max() || effect( id, rank + 1 ) <= effect( id, rank )
             || ( id == BRUTAL_CRITICALS && profile.ranks[CRITICAL_TRAINING] == 0 ) ) {
            return 0.0L;
        }

        return autoBuyUtility( profile, id ) * autoBuyActivityFactor( profile, id ) / static_cast<long double>( cost( id, rank ) );
    }

    void autoBuy( Profile & profile )
    {
        // The marginal value of almost every doctrine only changes when that doctrine itself
        // gains a rank. Cache these values so a large stored point balance does not recalculate
        // logarithmic effects for all 40 doctrines on every single purchase.
        std::array<long double, upgradeCount> marginalReturns{};
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            marginalReturns[id] = autoBuyMarginalReturn( profile, id );
        }

        for ( size_t purchases = 0; purchases < 100000; ++purchases ) {
            size_t bestId = upgradeCount;
            long double bestReturn = 0;

            for ( size_t id = 0; id < upgradeCount; ++id ) {
                if ( profile.points < cost( id, profile.ranks[id] ) ) {
                    continue;
                }

                if ( marginalReturns[id] <= 0.0L ) {
                    continue;
                }

                if ( bestId == upgradeCount || marginalReturns[id] > bestReturn
                     || ( marginalReturns[id] == bestReturn && profile.ranks[id] < profile.ranks[bestId] ) ) {
                    bestReturn = marginalReturns[id];
                    bestId = id;
                }
            }

            if ( bestId == upgradeCount || !buy( profile, bestId ) ) {
                break;
            }

            marginalReturns[bestId] = autoBuyMarginalReturn( profile, bestId );

            // These two doctrines are the only cross-dependent pair: Brutal Criticals is locked
            // behind Critical Training, and each doctrine's utility depends on the other's effect.
            if ( bestId == CRITICAL_TRAINING ) {
                marginalReturns[BRUTAL_CRITICALS] = autoBuyMarginalReturn( profile, BRUTAL_CRITICALS );
            }
            else if ( bestId == BRUTAL_CRITICALS ) {
                marginalReturns[CRITICAL_TRAINING] = autoBuyMarginalReturn( profile, CRITICAL_TRAINING );
            }
        }
    }

    std::string profilePath()
    {
        return System::concatPath( fheroes2::RPG::dataDirectory(), "rpg_profile.dat" );
    }

    bool readProfile( const std::string & path, Profile & profile, std::set<uint64_t> & visitedTiles )
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
            // so classify it as field/hero Renown before rewriting the profile as version 6.
            candidate.fieldExperience = candidate.experience;
            candidate.heroExperience = candidate.experience;
        }
        else if ( version == 2 ) {
            candidate.heroExperience = candidate.fieldExperience;
        }

        // Version 6 replaces the previous economy-oriented tree with combat affixes.
        // Refund the old investment and reset the slots so legacy ranks cannot silently
        // turn into oversized Attack, Defense, critical or sustain bonuses.
        if ( version < profileVersion ) {
            // The migration is a complete respec. Reconstruct the closed guild-point ledger
            // from level instead of trusting legacy rank/cost data that is about to be discarded.
            candidate.points = saturatedMultiply( candidate.level - 1, pointsPerLevel );
            candidate.ranks.fill( 0 );
            candidate.useCounts.fill( 0 );
        }

        candidate.autoBuy = version < profileVersion ? false : autoBuyValue != 0;
        if ( version == profileVersion && !isCurrentProfileStateValid( candidate ) ) {
            return false;
        }

        // Profile snapshots are single-record files. Extra tokens indicate a partial append,
        // manual damage or a future format that this build must not silently reinterpret.
        input >> std::ws;
        if ( !input.eof() ) {
            return false;
        }

        profile = std::move( candidate );
        visitedTiles = std::move( candidateVisited );
        return true;
    }

    void saveProfile( const bool discardInvalidPrimary = false )
    {
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
            if ( discardInvalidPrimary ) {
                // Recovery loaded a valid fallback while the primary file itself is corrupt.
                // Never rotate that corrupt primary over the known-good persistent backup.
                if ( !System::Unlink( path ) ) {
                    ERROR_LOG( "Unable to remove invalid RPG profile." )
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

    uint64_t xpToNextLevel( const uint64_t level )
    {
        return level > ( std::numeric_limits<uint64_t>::max() - 150000 ) / 100000 ? std::numeric_limits<uint64_t>::max() : 150000 + level * 100000;
    }

    bool canGainLevels( const Profile & profile, const uint64_t count )
    {
        if ( count == 0 ) {
            return true;
        }
        if ( count > std::numeric_limits<uint64_t>::max() - profile.level
             || profile.level > ( std::numeric_limits<uint64_t>::max() - 150000 ) / 100000 ) {
            return false;
        }

        const uint64_t firstCost = xpToNextLevel( profile.level );
        if ( count > profile.progress / firstCost ) {
            return false;
        }
        const uint64_t remaining = profile.progress - count * firstCost;
        return count == 1 || count <= remaining / 50000 / ( count - 1 );
    }

    uint64_t xpForLevels( const Profile & profile, const uint64_t count )
    {
        return count * xpToNextLevel( profile.level ) + 50000 * count * ( count - 1 );
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

    std::string formatEffect( const long double value )
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
        return result + '%';
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
        case ARMS_TRAINING: return "+" + formatNumber( static_cast<uint64_t>( value ) ) + " Attack";
        case ARMOR_TRAINING: return "+" + formatNumber( static_cast<uint64_t>( value ) ) + " Defense";
        case VETERAN_CORE: return "+" + formatNumber( static_cast<uint64_t>( value ) ) + " Attack & Defense";
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
        const bool canAdvance = rank < std::numeric_limits<uint64_t>::max() && effect( id, rank + 1 ) > currentEffect;
        std::string message = upgradeDetails[id];
        message += "\n\nCurrent rank: " + formatNumber( rank );
        message += "\nCurrent: " + shortEffectSummary( id, rank );
        const bool prerequisiteMet = id != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;
        if ( canAdvance ) {
            message += "\nNext rank: " + shortEffectSummary( id, rank + 1 );
            const uint64_t price = cost( id, rank );
            message += "\nNext rank costs: " + formatNumber( price ) + " points";
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
            message += "\nPurchase status: MAX - maximum effective rank reached.";
        }
        const uint64_t invested = totalSpentPointsOnUpgrade( playerProfile, id );
        if ( invested > 0 ) {
            message += "\nPoints invested in doctrine: " + formatNumber( invested );
        }
        if ( playerProfile.useCounts[id] > 0 ) {
            message += "\nBattle triggers: " + formatNumber( playerProfile.useCounts[id] );
        }
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
            const bool canAdvance = rank < std::numeric_limits<uint64_t>::max() && effect( id, rank + 1 ) > effect( id, rank );
            const bool prerequisiteMet = id != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;

            message += "\n  " + std::string( upgrades[id].name ) + " - Rank " + formatNumber( rank ) + ": " + shortEffectSummary( id, rank );
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
        uint64_t totalTriggers = 0;
        for ( const uint64_t uses : playerProfile.useCounts ) {
            totalTriggers = saturatedAdd( totalTriggers, uses );
        }
        if ( totalTriggers > 0 ) {
            message += "\nTotal Battle Triggers: " + formatNumber( totalTriggers );
        }
        message += "\nSteward Auto-Buyer: ";
        message += playerProfile.autoBuy ? "Active (ON)" : "Paused (OFF)";
        message += "\n\nRENOWN (RPG EXPERIENCE)";
        message += "\nTotal Renown: " + formatNumber( playerProfile.experience );
        message += "\n  From Hero Experience: " + formatNumber( playerProfile.heroExperience );
        message += "\n  From Battles: " + formatNumber( playerProfile.battleExperience );
        message += "\n  From Adventure Sites: " + formatNumber( playerProfile.adventureExperience );
        message += "\n  From Offline Progress: " + formatNumber( playerProfile.offlineExperience );
        message += "\n\nRenown to Next Level: " + formatNumber( remainingXP );
        message += "\nUnique Map Sites Visited: " + formatNumber( visitedActionTiles.size() );

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
    for ( const std::string & candidate : profileCandidates ) {
        Profile candidateProfile;
        std::set<uint64_t> candidateVisited;
        if ( readProfile( candidate, candidateProfile, candidateVisited ) ) {
            playerProfile = std::move( candidateProfile );
            visitedActionTiles = std::move( candidateVisited );
            loadedProfilePath = candidate;
            break;
        }
    }

    // If recovery had to fall back from a corrupt primary, do not let the recovery save
    // rotate that corrupt file over a known-good backup. Validation uses temporary outputs
    // so probing the primary cannot disturb the recovered profile or its visited-site set.
    bool discardInvalidPrimary = false;
    if ( !loadedProfilePath.empty() && loadedProfilePath != path && System::IsFile( path ) ) {
        Profile primaryProfile;
        std::set<uint64_t> primaryVisited;
        discardInvalidPrimary = !readProfile( path, primaryProfile, primaryVisited );
    }

    // Persist migrations and recovered snapshots immediately. In particular, this prevents
    // a version-5 profile from being refunded repeatedly if the game exits before the first XP award.
    saveProfile( discardInvalidPrimary );

    // Temporary enemy RPG builds are deterministic for the same map/profile level and
    // scale only upgrades the player has actually purchased. This avoids fresh profiles
    // facing invisible free enemy perks and keeps opponent power tied to real RPG choices.
    uint64_t seed = static_cast<uint64_t>( world.GetMapSeed() ) << 32;
    // Unsigned multiplication intentionally wraps here: this is a hash mix, not arithmetic progression.
    seed ^= playerProfile.level * 0x9E3779B185EBCA87ULL;
    seed ^= static_cast<uint64_t>( playerColor ) * 0xC2B2AE3D27D4EB4FULL;
    std::mt19937_64 rng( seed );

    const auto makeTemporaryProfile = [&rng]( const int minimumPower, const int maximumPower, const bool roundUpSmallRanks ) {
        std::uniform_int_distribution<int> variation( minimumPower, maximumPower );
        Profile temporary;
        temporary.level = std::max<uint64_t>( 1, scaledValue( playerProfile.level, variation( rng ) ) );

        for ( size_t id = 0; id < upgradeCount; ++id ) {
            if ( playerProfile.ranks[id] == 0 ) {
                continue;
            }

            const int percent = variation( rng );
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

    for ( const Player * player : Settings::Get().GetPlayers().getVector() ) {
        if ( player == nullptr || !player->isPlay() || player->GetColor() == playerColor
             || Players::isFriends( playerColor, static_cast<PlayerColorsSet>( player->GetColor() ) ) ) {
            continue;
        }

        enemyProfiles.emplace( player->GetColor(), makeTemporaryProfile( 85, 115, true ) );
    }
    enemyProfiles.emplace( PlayerColor::NONE, makeTemporaryProfile( 60, 90, false ) );
}

void fheroes2::RPG::endMap()
{
    if ( activePlayerColor != PlayerColor::NONE ) {
        saveProfile();
    }
    activePlayerColor = PlayerColor::NONE;
    enemyProfiles.clear();
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

    uint64_t low = 0;
    uint64_t high = std::min( playerProfile.progress / 250000 + 1, std::numeric_limits<uint64_t>::max() - playerProfile.level );
    while ( low < high ) {
        const uint64_t middle = low + ( high - low + 1 ) / 2;
        if ( canGainLevels( playerProfile, middle ) ) {
            low = middle;
        }
        else {
            high = middle - 1;
        }
    }
    if ( low > 0 ) {
        playerProfile.progress -= xpForLevels( playerProfile, low );
        playerProfile.level += low;
        playerProfile.points = saturatedAdd( playerProfile.points, low > std::numeric_limits<uint64_t>::max() / pointsPerLevel
                                                                  ? std::numeric_limits<uint64_t>::max()
                                                                  : low * pointsPerLevel );
        if ( kind != ExperienceKind::OFFLINE ) {
            AudioManager::PlaySound( M82::NWHEROLV );
        }
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
    const long double earned = base * challenge;
    static_cast<void>( addExperience( color, static_cast<uint64_t>( std::min( earned, static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) ),
                                      ExperienceKind::BATTLE ) );
}

void fheroes2::RPG::awardAdventureAction( const PlayerColor color, const int objectType, const int32_t tileIndex )
{
    if ( color != activePlayerColor || tileIndex < 0 ) {
        return;
    }

    uint64_t base = 70;
    switch ( objectType ) {
    case MP2::OBJ_MONSTER:
    case MP2::OBJ_HERO:
    case MP2::OBJ_BOAT:
        return;
    case MP2::OBJ_RESOURCE:
    case MP2::OBJ_BARREL:
    case MP2::OBJ_BOTTLE:
    case MP2::OBJ_CAMPFIRE:
    case MP2::OBJ_FLOTSAM:
    case MP2::OBJ_WINDMILL:
    case MP2::OBJ_WATER_WHEEL:
    case MP2::OBJ_MAGIC_GARDEN:
    case MP2::OBJ_LEAN_TO:
        base = 80;
        break;
    case MP2::OBJ_TREASURE_CHEST:
    case MP2::OBJ_SEA_CHEST:
    case MP2::OBJ_WAGON:
        base = 180;
        break;
    case MP2::OBJ_ARTIFACT:
    case MP2::OBJ_SHIPWRECK_SURVIVOR:
    case MP2::OBJ_SKELETON:
        base = 200;
        break;
    case MP2::OBJ_MINE:
    case MP2::OBJ_ALCHEMIST_LAB:
    case MP2::OBJ_SAWMILL:
    case MP2::OBJ_LIGHTHOUSE:
    case MP2::OBJ_ABANDONED_MINE:
        base = 180;
        break;
    case MP2::OBJ_CASTLE:
        base = 140;
        break;
    case MP2::OBJ_SHRINE_FIRST_CIRCLE:
    case MP2::OBJ_SHRINE_SECOND_CIRCLE:
    case MP2::OBJ_SHRINE_THIRD_CIRCLE:
    case MP2::OBJ_TEMPLE:
        base = 120;
        break;
    case MP2::OBJ_FORT:
    case MP2::OBJ_MERCENARY_CAMP:
    case MP2::OBJ_WITCH_DOCTORS_HUT:
    case MP2::OBJ_STANDING_STONES:
    case MP2::OBJ_ARENA:
    case MP2::OBJ_GAZEBO:
    case MP2::OBJ_WITCHS_HUT:
    case MP2::OBJ_TREE_OF_KNOWLEDGE:
        base = 150;
        break;
    case MP2::OBJ_FOUNTAIN:
    case MP2::OBJ_FAERIE_RING:
    case MP2::OBJ_IDOL:
    case MP2::OBJ_MERMAID:
    case MP2::OBJ_OASIS:
    case MP2::OBJ_WATERING_HOLE:
    case MP2::OBJ_BUOY:
        base = 90;
        break;
    case MP2::OBJ_EVENT:
    case MP2::OBJ_SIGN:
    case MP2::OBJ_SPHINX:
    case MP2::OBJ_ORACLE:
        base = 100;
        break;
    case MP2::OBJ_STONE_LITHS:
    case MP2::OBJ_WHIRLPOOL:
        base = 90;
        break;
    case MP2::OBJ_SHIPWRECK:
    case MP2::OBJ_DERELICT_SHIP:
    case MP2::OBJ_SIRENS:
        base = 130;
        break;
    case MP2::OBJ_OBSERVATION_TOWER:
    case MP2::OBJ_MAGELLANS_MAPS:
    case MP2::OBJ_OBELISK:
    case MP2::OBJ_HUT_OF_MAGI:
    case MP2::OBJ_EYE_OF_MAGI:
        base = 150;
        break;
    default:
        if ( !MP2::isInGameActionObject( static_cast<MP2::MapObjectType>( objectType ), false ) ) {
            return;
        }
        break;
    }

    const uint64_t key = ( static_cast<uint64_t>( world.GetMapSeed() ) << 32 ) | static_cast<uint32_t>( tileIndex );
    if ( !visitedActionTiles.insert( key ).second ) {
        return;
    }

    const long double levelScale = 1.0L + std::log1p( static_cast<long double>( playerProfile.level - 1 ) ) / 5.0L;
    static_cast<void>( addExperience( color, static_cast<uint64_t>( static_cast<long double>( base ) * levelScale ), ExperienceKind::ADVENTURE ) );
}

uint32_t fheroes2::RPG::creatureAttackBonus( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    if ( profile == nullptr ) {
        return 0;
    }

    const long double total = effect( ARMS_TRAINING, profile->ranks[ARMS_TRAINING] ) + effect( VETERAN_CORE, profile->ranks[VETERAN_CORE] );
    return static_cast<uint32_t>( std::min<long double>( total, std::numeric_limits<uint32_t>::max() ) );
}

uint32_t fheroes2::RPG::creatureDefenseBonus( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    if ( profile == nullptr ) {
        return 0;
    }

    const long double total = effect( ARMOR_TRAINING, profile->ranks[ARMOR_TRAINING] ) + effect( VETERAN_CORE, profile->ranks[VETERAN_CORE] );
    return static_cast<uint32_t>( std::min<long double>( total, std::numeric_limits<uint32_t>::max() ) );
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
    return profile == nullptr ? 0.0 : static_cast<double>( effect( BLOOD_DRINKER, profile->ranks[BLOOD_DRINKER] ) );
}

double fheroes2::RPG::killHealPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( REAPER, profile->ranks[REAPER] ) );
}

double fheroes2::RPG::regenerationPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( REGENERATION, profile->ranks[REGENERATION] ) );
}

double fheroes2::RPG::criticalChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( CRITICAL_TRAINING, profile->ranks[CRITICAL_TRAINING] ) );
}

double fheroes2::RPG::criticalDamageBonusPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 50.0 : 50.0 + static_cast<double>( effect( BRUTAL_CRITICALS, profile->ranks[BRUTAL_CRITICALS] ) );
}

double fheroes2::RPG::evasionChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0.0 : static_cast<double>( effect( EVASION, profile->ranks[EVASION] ) );
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
        attackBonus += effect( FEROCITY, attackProfile->ranks[FEROCITY] );
        attackBonus += effect( ranged ? MARKSMAN : BRAWLER, attackProfile->ranks[ranged ? MARKSMAN : BRAWLER] );

        if ( !defenderFullHealth ) {
            attackBonus += effect( EXECUTIONER, attackProfile->ranks[EXECUTIONER] );
        }
        if ( defenderFullHealth ) {
            attackBonus += effect( OPENING_BLOW, attackProfile->ranks[OPENING_BLOW] );
        }
        if ( attackerOutnumbered ) {
            attackBonus += effect( GIANT_SLAYER, attackProfile->ranks[GIANT_SLAYER] );
        }
        if ( defenderOutnumbered ) {
            attackBonus += effect( OVERWHELM, attackProfile->ranks[OVERWHELM] );
        }
        if ( attackerBelowHalf ) {
            attackBonus += effect( FRENZY, attackProfile->ranks[FRENZY] );
        }
        if ( attackerFullHealth ) {
            attackBonus += effect( DISCIPLINE, attackProfile->ranks[DISCIPLINE] );
        }
        if ( defenderBelowHalf ) {
            attackBonus += effect( RUTHLESS, attackProfile->ranks[RUTHLESS] );
        }
    }

    long double defenseReduction = 0;
    if ( defenseProfile != nullptr ) {
        defenseReduction += effect( IRON_SKIN, defenseProfile->ranks[IRON_SKIN] );
        defenseReduction += effect( ranged ? ARROW_WARD : MELEE_GUARD, defenseProfile->ranks[ranged ? ARROW_WARD : MELEE_GUARD] );

        if ( defenderBelowHalf ) {
            defenseReduction += effect( LAST_STAND, defenseProfile->ranks[LAST_STAND] );
        }
        if ( defenderOutnumbered ) {
            defenseReduction += effect( BULWARK, defenseProfile->ranks[BULWARK] );
        }
        if ( defenderFullHealth ) {
            defenseReduction += effect( UNYIELDING, defenseProfile->ranks[UNYIELDING] );
        }
    }

    attackBonus = std::min<long double>( attackBonus, 100.0L );
    defenseReduction = std::min<long double>( defenseReduction, 70.0L );
    if ( attackProfile != nullptr && defenseReduction > 0 ) {
        const long double piercing = effect( ARMOR_PIERCING, attackProfile->ranks[ARMOR_PIERCING] );
        defenseReduction *= 1.0L - piercing / 100.0L;
    }

    return static_cast<double>( ( 1.0L + attackBonus / 100.0L ) * ( 1.0L - defenseReduction / 100.0L ) );
}

double fheroes2::RPG::spellMultiplier( const PlayerColor attacker, const PlayerColor defender, const int spellId )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );
    const size_t specialization = spellSpecializationId( spellId );

    long double spellBonus = attackProfile == nullptr ? 0 : effect( SORCERY, attackProfile->ranks[SORCERY] );
    long double defenseReduction = defenseProfile == nullptr ? 0 : effect( SPELL_WARD, defenseProfile->ranks[SPELL_WARD] );

    if ( specialization != upgradeCount ) {
        if ( attackProfile != nullptr ) {
            spellBonus += effect( specialization, attackProfile->ranks[specialization] );
        }
        if ( defenseProfile != nullptr ) {
            const size_t ward = spellWardId( spellId );
            defenseReduction += effect( ward, defenseProfile->ranks[ward] );
        }
    }

    spellBonus = std::min<long double>( spellBonus, 100.0L );
    defenseReduction = std::min<long double>( defenseReduction, 70.0L );
    if ( attackProfile != nullptr && defenseReduction > 0 ) {
        const long double piercing = effect( ARCANE_PIERCING, attackProfile->ranks[ARCANE_PIERCING] );
        defenseReduction *= 1.0L - piercing / 100.0L;
    }

    return static_cast<double>( ( 1.0L + spellBonus / 100.0L ) * ( 1.0L - defenseReduction / 100.0L ) );
}

std::string fheroes2::RPG::formatExperience( const uint64_t value )
{
    return formatNumber( value );
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
            drawText( "Renown " + formatExperience( playerProfile.experience ) + "    " + formatExperience( remainingXP ) + " to next level",
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
            drawSingleLine( "1-8 Tabs   O Overview   S/A Steward   R Respec   Esc Close", area.x + 12, area.y + 344, area.width - 24,
                            fheroes2::FontType::smallWhite() );
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
                "Automatically buys the available next rank with the largest immediate mechanical gain per point. Battle-trigger history gives a modest preference to specialties your kingdom actually uses. Capped upgrades are skipped once another rank would add no effect.",
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

