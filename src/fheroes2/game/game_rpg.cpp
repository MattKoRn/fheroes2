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

#include "color.h"
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
    constexpr size_t upgradeCount = 40;
    constexpr uint64_t pointsPerLevel = 5;
    enum UpgradeId : size_t
    {
        ARMS_TRAINING, ARMOR_TRAINING, VETERAN_CORE, BLOOD_DRINKER, REAPER,
        FEROCITY, MARKSMAN, BRAWLER, EXECUTIONER, OPENING_BLOW,
        GIANT_SLAYER, OVERWHELM, FRENZY, DISCIPLINE, ARMOR_PIERCING,
        IRON_SKIN, ARROW_WARD, MELEE_GUARD, LAST_STAND, BULWARK,
        SORCERY, PYROMANCY, CRYOMANCY, STORMCRAFT, CATACLYSM,
        SPELL_WARD, FIRE_WARD, COLD_WARD, STORM_WARD, CATACLYSM_WARD,
        LEADERSHIP, FORTUNE, REGENERATION, CRITICAL_TRAINING, BRUTAL_CRITICALS,
        EVASION, ARCANE_PIERCING, CLOSE_QUARTERS, UNYIELDING, RUTHLESS
    };

    struct UpgradeInfo
    {
        const char * name;
        const char * description;
    };
    constexpr std::array<UpgradeInfo, upgradeCount> upgrades{
        UpgradeInfo{ "Arms Training", "Increase creature Attack" }, { "Armor Training", "Increase creature Defense" },
        { "Veteran Core", "Increase Attack and Defense" }, { "Blood Drinker", "Attacks steal life" }, { "Reaper", "Kills restore life" },

        { "Ferocity", "Increase all creature damage" }, { "Marksman", "Increase ranged damage" }, { "Brawler", "Increase melee damage" },
        { "Executioner", "More damage to wounded stacks" }, { "Opening Blow", "More damage to untouched stacks" },

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
        { "Close Quarters", "Reduce ranged melee penalty" }, { "Unyielding", "Reduce damage while untouched" },
        { "Ruthless", "More damage to stacks below half health" }
    };
    constexpr std::array<const char *, upgradeCount> upgradeDetails{
        "Adds a flat Attack bonus to every creature stack controlled by this RPG profile. Each rank adds +1 Attack.",
        "Adds a flat Defense bonus to every creature stack controlled by this RPG profile. Each rank adds +1 Defense.",
        "Adds both Attack and Defense to every creature stack. Each rank adds +1 to both stats.",
        "Whenever one of your creature stacks deals attack damage, it heals for a percentage of the actual damage dealt. Healing repairs the surviving stack but does not resurrect killed creatures.",
        "Whenever one of your attacks kills creatures, the attacking stack heals for a percentage of the slain creatures' hit points. It cannot resurrect creatures already lost from that stack.",

        "Increases all physical damage dealt by your creature stacks.",
        "Increases physical damage dealt by ranged attacks.",
        "Increases physical damage dealt by melee attacks.",
        "Increases physical damage against any stack that has already lost hit points or creatures.",
        "Increases physical damage against a stack that has taken no damage and lost no creatures yet.",

        "Increases physical damage when the attacking stack has fewer creatures than its target.",
        "Increases physical damage when the attacking stack has more creatures than its target.",
        "Increases physical damage while the attacking stack is below half of its starting battle hit points.",
        "Increases physical damage while the attacking stack is still at its full starting battle hit points.",
        "Ignores a percentage of the defender's RPG physical damage reduction after all applicable defensive upgrades are combined.",

        "Reduces all physical damage received by your creature stacks.",
        "Adds extra damage reduction against ranged creature attacks.",
        "Adds extra damage reduction against melee creature attacks.",
        "Adds extra physical damage reduction while the defending stack is below half of its starting battle hit points.",
        "Adds extra physical damage reduction while the defending stack has fewer creatures than its attacker.",

        "Increases damage from every damaging spell cast by a hero using this RPG profile.",
        "Adds damage to Fireball and Fireblast.",
        "Adds damage to Cold Ray and Cold Ring.",
        "Adds damage to Lightning Bolt and Chain Lightning.",
        "Adds damage to Elemental Storm and Armageddon.",

        "Reduces damage received from every damaging spell. Combined RPG spell resistance is capped so it cannot make a stack immune.",
        "Adds RPG resistance against Fireball and Fireblast.",
        "Adds RPG resistance against Cold Ray and Cold Ring.",
        "Adds RPG resistance against Lightning Bolt and Chain Lightning.",
        "Adds RPG resistance against Elemental Storm and Armageddon.",

        "Adds Morale to your creature stacks during combat, up to +3 from this upgrade.",
        "Adds Luck to your creature stacks during combat, up to +3 from this upgrade.",
        "At the beginning of a stack's turn, restores a percentage of one creature's maximum hit points to the surviving stack. This repairs the wounded top creature but never resurrects dead creatures.",
        "Gives each creature attack a chance to become a critical hit. A critical hit deals 50% extra damage before Brutal Criticals is added.",
        "Increases the bonus damage of critical hits beyond their normal +50% damage.",

        "Gives your creature stacks a chance to evade part of an incoming creature attack, reducing that attack's final damage by 50%.",
        "Ignores a percentage of the target's combined RPG spell resistance when your hero casts a damaging spell.",
        "Recovers part of the normal 50% melee penalty suffered by ranged creatures forced into hand-to-hand combat. At 100% recovery, the RPG penalty modifier removes that penalty.",
        "Reduces physical damage while the defending stack is still at its full starting battle hit points.",
        "Adds another damage bonus against enemy stacks below half of their starting battle hit points, rewarding aggressive finishing attacks."
    };
    constexpr std::array<const char *, 8> tabNames{ "ARMY", "OFFENSE", "TACTICS", "DEFENSE", "MAGIC", "WARDS", "COMMAND", "MASTERY" };
    constexpr std::array<const char *, 8> tabPanelNames{
        "TRAINING YARD", "WAR COUNCIL", "TACTICS HALL", "GUARD HOUSE", "MAGE GUILD", "WARD HALL", "THRONE ROOM", "MASTER'S HALL"
    };

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
        return 5.0L * std::log1p( static_cast<long double>( rank ) );
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
            return static_cast<long double>( rank );
        case LEADERSHIP:
        case FORTUNE:
            return static_cast<long double>( std::min<uint64_t>( rank, 3 ) );
        case BLOOD_DRINKER:
            return std::min<long double>( 35.0L, 3.5L * std::log1p( static_cast<long double>( rank ) ) );
        case REAPER:
            return std::min<long double>( 50.0L, 5.0L * std::log1p( static_cast<long double>( rank ) ) );
        case REGENERATION:
            return std::min<long double>( 25.0L, 3.0L * std::log1p( static_cast<long double>( rank ) ) );
        case CRITICAL_TRAINING:
            return std::min<long double>( 30.0L, 5.0L * std::log1p( static_cast<long double>( rank ) ) );
        case BRUTAL_CRITICALS:
            return std::min<long double>( 150.0L, 10.0L * std::log1p( static_cast<long double>( rank ) ) );
        case EVASION:
            return std::min<long double>( 25.0L, 3.0L * std::log1p( static_cast<long double>( rank ) ) );
        case ARMOR_PIERCING:
        case ARCANE_PIERCING:
            return std::min<long double>( 75.0L, 8.0L * std::log1p( static_cast<long double>( rank ) ) );
        case CLOSE_QUARTERS:
            return std::min<long double>( 100.0L, 12.0L * std::log1p( static_cast<long double>( rank ) ) );
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
            return std::min<long double>( 60.0L, 4.0L * std::log1p( static_cast<long double>( rank ) ) );
        default:
            return percentEffect( rank );
        }
    }

    uint64_t cost( const uint64_t rank )
    {
        return 1 + rank / 10;
    }

    bool buy( Profile & profile, const size_t id )
    {
        if ( id >= upgradeCount || profile.ranks[id] == std::numeric_limits<uint64_t>::max()
             || effect( id, profile.ranks[id] + 1 ) <= effect( id, profile.ranks[id] ) ) {
            return false;
        }

        const uint64_t price = cost( profile.ranks[id] );
        if ( profile.points < price ) {
            return false;
        }

        profile.points -= price;
        ++profile.ranks[id];
        return true;
    }

    void autoBuy( Profile & profile )
    {
        for ( size_t purchases = 0; purchases < 100000; ++purchases ) {
            size_t bestId = upgradeCount;
            long double bestReturn = 0;

            for ( size_t id = 0; id < upgradeCount; ++id ) {
                const uint64_t rank = profile.ranks[id];
                if ( rank == std::numeric_limits<uint64_t>::max() || profile.points < cost( rank ) ) {
                    continue;
                }

                const long double marginalReturn
                    = ( effect( id, rank + 1 ) - effect( id, rank ) ) / static_cast<long double>( cost( rank ) );
                if ( marginalReturn > bestReturn ) {
                    bestReturn = marginalReturn;
                    bestId = id;
                }
            }

            if ( bestId == upgradeCount || !buy( profile, bestId ) ) {
                break;
            }
        }
    }

    std::string profilePath()
    {
        return System::concatPath( fheroes2::RPG::dataDirectory(), "rpg_profile.dat" );
    }

    bool readProfile( const std::string & path, Profile & profile )
    {
        std::ifstream input( path );
        int version = 0;
        int autoBuyValue = 0;
        Profile candidate;
        if ( !( input >> version >> candidate.level >> candidate.experience >> candidate.progress >> candidate.points >> autoBuyValue )
             || ( version < 1 || version > 5 )
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
                if ( !( input >> key ) ) {
                    return false;
                }
                candidateVisited.insert( key );
            }
        }
        if ( version >= 5 ) {
            for ( uint64_t & uses : candidate.useCounts ) {
                if ( !( input >> uses ) ) {
                    return false;
                }
            }
        }
        if ( version == 2 ) {
            candidate.heroExperience = candidate.fieldExperience;
        }

        candidate.autoBuy = autoBuyValue != 0;
        profile = candidate;
        visitedActionTiles = std::move( candidateVisited );
        return true;
    }

    void saveProfile()
    {
        const std::string path = profilePath();
        const std::string tempPath = path + ".tmp";
        const std::string backupPath = path + ".bak";

        std::ofstream output( tempPath, std::ios::trunc );
        if ( !output ) {
            ERROR_LOG( "Unable to write RPG profile." )
            return;
        }

        output << "5 " << playerProfile.level << ' ' << playerProfile.experience << ' ' << playerProfile.progress << ' ' << playerProfile.points << ' '
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
        if ( hadOriginal ) {
            System::Unlink( backupPath );
            if ( std::rename( path.c_str(), backupPath.c_str() ) != 0 ) {
                System::Unlink( tempPath );
                return;
            }
        }

        if ( std::rename( tempPath.c_str(), path.c_str() ) != 0 ) {
            if ( hadOriginal ) {
                std::rename( backupPath.c_str(), path.c_str() );
            }
            System::Unlink( tempPath );
            return;
        }
        System::Unlink( backupPath );
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
        text << std::fixed << std::setprecision( 3 ) << static_cast<double>( value ) << '%';
        return text.str();
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
        case OPENING_BLOW: return "+" + formatEffect( value ) + " vs untouched";
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
        case BRUTAL_CRITICALS: return "+50% + " + formatEffect( value ) + " crit damage";
        case EVASION: return formatEffect( value ) + " evade chance";
        case ARCANE_PIERCING: return formatEffect( value ) + " spell resistance ignored";
        case CLOSE_QUARTERS: return formatEffect( value ) + " melee penalty recovered";
        case UNYIELDING: return formatEffect( value ) + " reduction while untouched";
        case RUTHLESS: return "+" + formatEffect( value ) + " vs targets below half HP";
        default: return formatEffect( value );
        }
    }

    void showUpgradeDetails( const size_t id )
    {
        const uint64_t rank = playerProfile.ranks[id];
        const long double currentEffect = effect( id, rank );
        const bool canAdvance = rank < std::numeric_limits<uint64_t>::max() && effect( id, rank + 1 ) > currentEffect;
        std::string message = upgradeDetails[id];
        message += "\n\nCurrent rank: " + formatNumber( rank );
        message += "\nCurrent: " + shortEffectSummary( id, rank );
        if ( canAdvance ) {
            const long double nextEffect = effect( id, rank + 1 );
            message += "\nNext rank: " + shortEffectSummary( id, rank + 1 );
            message += "\nScaling gained: +" + formatEffect( nextEffect - currentEffect );
            message += "\nNext rank costs: " + formatNumber( cost( rank ) ) + " points";
        }
        message += "\nAvailable points: " + formatNumber( playerProfile.points );
        fheroes2::showStandardTextMessage( upgrades[id].name, std::move( message ), Dialog::ZERO );
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
    if ( !readProfile( path, playerProfile ) && !readProfile( path + ".tmp", playerProfile ) ) {
        readProfile( path + ".bak", playerProfile );
    }

    std::mt19937_64 rng( std::random_device{}() );
    std::uniform_int_distribution<int> startingRank( 0, 2 );
    const auto makeTemporaryProfile = [&rng, &startingRank]( const int minimumPower, const int maximumPower ) {
        std::uniform_int_distribution<int> variation( minimumPower, maximumPower );
        Profile temporary;
        temporary.level = std::max<uint64_t>( 1, scaledValue( playerProfile.level, variation( rng ) ) );
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            temporary.ranks[id] = saturatedAdd( scaledValue( playerProfile.ranks[id], variation( rng ) ),
                                                static_cast<uint64_t>( startingRank( rng ) ) );
        }
        return temporary;
    };

    for ( const Player * player : Settings::Get().GetPlayers().getVector() ) {
        if ( player == nullptr || !player->isPlay() || player->GetColor() == playerColor
             || Players::isFriends( playerColor, static_cast<PlayerColorsSet>( player->GetColor() ) ) ) {
            continue;
        }

        enemyProfiles.emplace( player->GetColor(), makeTemporaryProfile( 75, 125 ) );
    }
    enemyProfiles.emplace( PlayerColor::NONE, makeTemporaryProfile( 65, 115 ) );
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
    }

    if ( playerProfile.autoBuy ) {
        autoBuy( playerProfile );
    }
    saveProfile();
    return credited;
}

void fheroes2::RPG::awardBattle( const PlayerColor color, const PlayerColor opponent, const uint32_t battleExperience, const bool won,
                                  const bool /* defending */, const bool /* siege */ )
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
    const long double base = ( won ? 250.0L : 100.0L ) + static_cast<long double>( battleExperience ) * ( won ? 0.5L : 0.2L );
    const long double earned = base * challenge;
    addExperience( color, static_cast<uint64_t>( std::min( earned, static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) ),
                   ExperienceKind::BATTLE );
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

    const long double levelScale = 1.0L + std::log1p( static_cast<long double>( playerProfile.level ) ) / 5.0L;
    addExperience( color, static_cast<uint64_t>( static_cast<long double>( base ) * levelScale ), ExperienceKind::ADVENTURE );
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

uint32_t fheroes2::RPG::criticalChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0 : static_cast<uint32_t>( effect( CRITICAL_TRAINING, profile->ranks[CRITICAL_TRAINING] ) );
}

double fheroes2::RPG::criticalDamageBonusPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 50.0 : 50.0 + static_cast<double>( effect( BRUTAL_CRITICALS, profile->ranks[BRUTAL_CRITICALS] ) );
}

uint32_t fheroes2::RPG::evasionChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0 : static_cast<uint32_t>( effect( EVASION, profile->ranks[EVASION] ) );
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

    defenseReduction = std::min<long double>( defenseReduction, 80.0L );
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
    const size_t specialization = [spellId]() -> size_t {
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
        case Spell::ARMAGEDDON:
            return CATACLYSM;
        default:
            return upgradeCount;
        }
    }();

    long double spellBonus = attackProfile == nullptr ? 0 : effect( SORCERY, attackProfile->ranks[SORCERY] );
    long double defenseReduction = defenseProfile == nullptr ? 0 : effect( SPELL_WARD, defenseProfile->ranks[SPELL_WARD] );

    if ( specialization != upgradeCount ) {
        if ( attackProfile != nullptr ) {
            spellBonus += effect( specialization, attackProfile->ranks[specialization] );
        }
        if ( defenseProfile != nullptr ) {
            const size_t ward = specialization + ( FIRE_WARD - PYROMANCY );
            defenseReduction += effect( ward, defenseProfile->ranks[ward] );
        }
    }

    defenseReduction = std::min<long double>( defenseReduction, 80.0L );
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
    constexpr size_t upgradesPerTab = 5;
    constexpr size_t visibleRows = 3;
    std::array<fheroes2::Rect, tabNames.size()> tabAreas{};
    std::array<fheroes2::Rect, visibleRows> visibleUpgradeAreas{};
    std::array<fheroes2::Rect, visibleRows> buyAreas{};
    std::array<size_t, tabNames.size()> scrollOffsets{};
    const fheroes2::Rect statsArea( area.x + 12, area.y + 38, area.width - 24, 42 );
    const fheroes2::Rect listArea( area.x + 12, area.y + 151, area.width - 24, 181 );
    const int32_t scrollbarX = listArea.x + listArea.width - 19;
    fheroes2::Button scrollUp( scrollbarX + 1, listArea.y + 1, scrollIcn, 0, 1 );
    fheroes2::Button scrollDown( scrollbarX + 1, listArea.y + listArea.height - 15, scrollIcn, 2, 3 );
    fheroes2::ButtonSprite autoButton;
    fheroes2::ButtonSprite closeButton;
    size_t tab = 0;
    bool redraw = true;
    LocalEvent & event = LocalEvent::Get();

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

                const bool canBuy = playerProfile.ranks[i] < std::numeric_limits<uint64_t>::max()
                                    && effect( i, playerProfile.ranks[i] + 1 ) > effect( i, playerProfile.ranks[i] )
                                    && playerProfile.points >= cost( playerProfile.ranks[i] );
                if ( canBuy ) {
                    fheroes2::Fill( display, rowArea.x + 3, rowArea.y + 5, 2, rowArea.height - 10, fheroes2::GetColorId( 219, 175, 66 ) );
                }

                const fheroes2::Rect badgeArea{ rowArea.x + 7, rowArea.y + 8, 34, 34 };
                drawBeveledPanel( badgeArea, true );
                drawText( std::string( 1, upgrades[i].name[0] ), badgeArea.x + 2, badgeArea.y + 8, badgeArea.width - 4,
                          fheroes2::FontType::normalYellow() );

                const int32_t textX = rowArea.x + 48;
                const int32_t buyWidth = 63;
                buyAreas[row] = { rowArea.x + rowArea.width - buyWidth - 7, rowArea.y + 27, buyWidth, 20 };
                drawBeveledPanel( buyAreas[row], !canBuy );
                drawSingleLine( upgrades[i].name, textX, rowArea.y + 4, rowArea.width - 150, fheroes2::FontType::normalYellow() );
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
                drawText( "BUY " + formatNumber( cost( playerProfile.ranks[i] ) ), buyAreas[row].x + 3, buyAreas[row].y + 5, buyAreas[row].width - 6,
                          canBuy ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
            }

            drawText( "Page " + std::to_string( scrollOffsets[tab] + 1 ) + "/3   Wheel: scroll   Right-click: inspect decree",
                      area.x + 12, area.y + 337, area.width - 24, fheroes2::FontType::smallWhite() );
            window.renderTextAdaptedButtonSprite( autoButton, playerProfile.autoBuy ? "Steward ON" : "Steward OFF", { 18, 6 },
                                                  fheroes2::StandardWindow::Padding::BOTTOM_LEFT );
            window.renderTextAdaptedButtonSprite( closeButton, "Close", { 18, 6 }, fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );
            display.render( window.totalArea() );
            redraw = false;
        }

        autoButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( autoButton.area() ) );
        closeButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( closeButton.area() ) );
        scrollUp.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( scrollUp.area() ) );
        scrollDown.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( scrollDown.area() ) );

        if ( Game::HotKeyCloseWindow() || event.MouseClickLeft( closeButton.area() ) ) {
            break;
        }

        bool tabChanged = false;
        for ( size_t i = 0; i < tabNames.size(); ++i ) {
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

        size_t & scrollOffset = scrollOffsets[tab];
        if ( ( event.isMouseWheelUpInArea( listArea ) || event.MouseClickLeft( scrollUp.area() ) ) && scrollOffset > 0 ) {
            --scrollOffset;
            redraw = true;
            continue;
        }
        if ( ( event.isMouseWheelDownInArea( listArea ) || event.MouseClickLeft( scrollDown.area() ) ) && scrollOffset + visibleRows < upgradesPerTab ) {
            ++scrollOffset;
            redraw = true;
            continue;
        }

        for ( size_t row = 0; row < visibleRows; ++row ) {
            const size_t i = tab * upgradesPerTab + scrollOffset + row;
            if ( event.isMouseRightButtonPressedInArea( visibleUpgradeAreas[row] ) ) {
                showUpgradeDetails( i );
                redraw = true;
                break;
            }
            if ( event.MouseClickLeft( buyAreas[row] ) && buy( playerProfile, i ) ) {
                saveProfile();
                redraw = true;
                break;
            }
        }

        if ( event.isMouseRightButtonPressedInArea( autoButton.area() ) ) {
            fheroes2::showStandardTextMessage(
                "Steward", "Automatically buys the available next rank with the largest immediate mechanical gain per point. Capped upgrades are skipped once another rank would add no effect.",
                Dialog::ZERO );
            redraw = true;
            continue;
        }
        if ( event.MouseClickLeft( autoButton.area() ) ) {
            playerProfile.autoBuy = !playerProfile.autoBuy;
            if ( playerProfile.autoBuy ) {
                autoBuy( playerProfile );
            }
            saveProfile();
            redraw = true;
        }
    }
}

