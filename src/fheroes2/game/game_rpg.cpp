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
        MIGHT, GUARD, MARKSMAN, DUELIST, UNDERDOG,
        SORCERY, PYROMANCY, CRYOMANCY, STORMCRAFT, CATACLYSM,
        SPELL_WARD, FIRE_WARD, COLD_WARD, STORM_WARD, CATACLYSM_WARD,
        WISDOM, MEDITATION, VETERAN, EXPLORER, MENTOR,
        MONSTER_HUNTER, HERO_SLAYER, SIEGE_MASTER, DEFENDER, SURVIVOR,
        SCAVENGER, TREASURE_HUNTER, RELIC_HUNTER, PROSPECTOR, CASTELLAN,
        PILGRIM, SCHOLAR, INSPIRATION, RECRUITER, STORYKEEPER,
        WAYFARER, MARINER, CARTOGRAPHER, MERCHANT, GENERALIST
    };

    struct UpgradeInfo
    {
        const char * name;
        const char * description;
    };
    constexpr std::array<UpgradeInfo, upgradeCount> upgrades{
        UpgradeInfo{ "Might", "All troop damage" }, { "Guard", "Physical damage resistance" }, { "Marksman", "Ranged troop damage" },
        { "Duelist", "Melee troop damage" }, { "Underdog", "Damage while outnumbered" },
        { "Sorcery", "All damaging spells" }, { "Pyromancy", "Fireball and Fireblast" }, { "Cryomancy", "Cold Ray and Cold Ring" },
        { "Stormcraft", "Lightning and Chain Lightning" }, { "Cataclysm", "Storm and Armageddon" },
        { "Spell Ward", "All spell damage resistance" }, { "Fire Ward", "Fire spell resistance" }, { "Cold Ward", "Cold spell resistance" },
        { "Storm Ward", "Lightning resistance" }, { "Chaos Ward", "Storm and Armageddon resistance" },
        { "War College", "Troop + spell offense" }, { "Mystic Discipline", "Spell defense doctrine" }, { "Veteran Drills", "Physical defense doctrine" },
        { "Pathfinder", "All adventure reward bundles" }, { "Quartermaster", "All battle reward bundles" },
        { "Monster Hunter", "Neutral bounty: gold + gems" }, { "Hero Slayer", "Player bounty: gold + crystal" },
        { "Siege Master", "Siege salvage: gold + ore + wood" }, { "Defender", "Defense stores: wood + ore" }, { "Survivor", "Recovery: gold + wood" },
        { "Scavenger", "Resource sites: gold + wood" }, { "Treasure Hunter", "Treasure: gold + gems" }, { "Relic Hunter", "Relics: gems + crystal" },
        { "Prospector", "Production: ore + wood" }, { "Castellan", "Castle stipend: gold + wood" },
        { "Pilgrim", "Shrines: mercury + gold" }, { "Scholar", "Training: crystal + mercury" }, { "Inspiration", "Morale sites: gems + gold" },
        { "Recruiter", "Dwellings: gold + wood" }, { "Storykeeper", "Events: gold + gems" },
        { "Wayfarer", "Portals: sulfur + mercury" }, { "Mariner", "Sea sites: gems + gold" }, { "Cartographer", "Map sites: gold + crystal" },
        { "Merchant", "Trade stores: gold + ore + wood" }, { "Generalist", "Other sites: gold + wood" }
    };
    constexpr std::array<const char *, upgradeCount> upgradeDetails{
        "Increases physical damage dealt by every troop in your kingdom's battles.",
        "Reduces physical damage received by your troops. Its curve approaches 75% before other defensive doctrine bonuses are applied.",
        "Adds physical damage when one of your troops makes a ranged attack.",
        "Adds physical damage when one of your troops attacks in melee.",
        "Adds physical damage when the attacking troop has fewer creatures than its target.",
        "Increases damage from every damaging spell cast for your kingdom.",
        "Adds damage to Fireball and Fireblast spells.",
        "Adds damage to Cold Ray and Cold Ring spells.",
        "Adds damage to Lightning Bolt and Chain Lightning spells.",
        "Adds damage to Elemental Storm and Armageddon spells.",
        "Reduces damage received from every damaging spell. Combined spell resistance is capped at 90%.",
        "Adds resistance against Fireball and Fireblast damage.",
        "Adds resistance against Cold Ray and Cold Ring damage.",
        "Adds resistance against Lightning Bolt and Chain Lightning damage.",
        "Adds resistance against Elemental Storm and Armageddon damage.",
        "Adds 35% of its listed effect to both physical troop damage and damaging spell power, providing a broad offensive doctrine.",
        "Adds 50% of its listed effect to spell resistance. Combined spell resistance remains capped at 90%.",
        "Adds 50% of its listed effect to physical resistance. Combined physical resistance remains capped at 90%.",
        "Amplifies every resource bundle generated by Spoils, Sites, and Travel upgrades. It improves all resources in the bundle, not RPG experience.",
        "Amplifies every conditional battle bounty and salvage bundle generated by Battle upgrades. It improves all resources in the bundle.",
        "After winning against neutral monsters, grants a gold bounty plus a gem trophy cache. Both scale with battle experience, rank, and Quartermaster.",
        "After winning against another player's army, grants a larger gold bounty plus crystals recovered from the enemy force. Both scale with battle experience, rank, and Quartermaster.",
        "After winning a castle battle, grants gold, ore, and wood salvage for captured stores and siege materials. All three rewards scale with battle experience, rank, and Quartermaster.",
        "After winning while defending, grants wood and ore for repairs and fortification. Both rewards scale with battle experience, rank, and Quartermaster.",
        "After a battle loss, grants a rebuilding fund in gold plus emergency wood supplies. Both rewards scale with battle experience, rank, and Quartermaster.",
        "On the first rewarded resource or producing-site action on a tile, grants bonus gold and wood. Pathfinder increases both parts of the bundle.",
        "On the first rewarded treasure chest, sea chest, or wagon action on a tile, grants bonus gold and gems. Pathfinder increases both rewards.",
        "On the first rewarded artifact, skeleton, or shipwreck-survivor action on a tile, grants gems and crystals. Pathfinder increases both rewards.",
        "On the first rewarded mine, sawmill, laboratory, lighthouse, or abandoned-mine action on a tile, grants ore and wood. Pathfinder increases both rewards.",
        "On the first rewarded castle-tile action, grants a royal gold stipend and wood supplies. Pathfinder increases both rewards.",
        "On the first rewarded shrine or temple action, grants mercury and a small gold tithe. Pathfinder increases both rewards.",
        "On the first rewarded skill-training, arena, gazebo, or knowledge-tree action, grants crystals and mercury. Pathfinder increases both rewards.",
        "On the first rewarded morale or luck site action, grants gems and a gold purse. Pathfinder increases both rewards.",
        "On the first rewarded dwelling or recruitment-site action, grants recruitment gold and wood supplies. Pathfinder increases both rewards.",
        "On the first rewarded map event, sign, sphinx, or oracle action, grants gold and gems. Pathfinder increases both rewards.",
        "On the first rewarded stone-lith or whirlpool action, grants sulfur and mercury. Pathfinder increases both rewards.",
        "On the first rewarded shipwreck, derelict-ship, or siren action, grants gems and recovered gold. Pathfinder increases both rewards.",
        "On the first rewarded observation tower, obelisk, map, or Magi-site action, grants gold and crystals. Pathfinder increases both rewards.",
        "On the first rewarded trading-post or alchemist-tower action, grants a large gold cache plus ore and wood stores. Pathfinder increases all three rewards.",
        "On the first rewarded adventure action not covered by another specialty, grants gold and wood. Pathfinder increases both rewards."
    };
    constexpr std::array<const char *, 8> tabNames{ "WAR", "MAGIC", "WARDS", "GROWTH", "BATTLES", "SPOILS", "SITES", "TRAVEL" };
    constexpr std::array<const char *, 8> tabPanelNames{
        "WAR COUNCIL", "MAGE GUILD", "WARD HALL", "ROYAL ACADEMY", "BOUNTY BOARD", "TREASURY", "ADVENTURE GUILD", "WAYFARERS"
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

    long double effect( const size_t id, const uint64_t rank )
    {
        const long double value = static_cast<long double>( rank );
        // Upgrade ranks remain open-ended. Guard has its own asymptotic resistance curve;
        // all other percentages are constrained where they are applied.
        return id == GUARD ? 75.0L * value / ( value + 50.0L ) : 5.0L * std::log1p( value );
    }

    uint32_t scaledReward( const size_t id, const uint64_t rank, const uint64_t base, const long double amplifier = 0.0L )
    {
        if ( rank == 0 || base == 0 ) {
            return 0;
        }

        const long double raw = static_cast<long double>( base ) * effect( id, rank ) / 100.0L * ( 1.0L + amplifier / 100.0L );
        const long double capped = std::min<long double>( std::numeric_limits<int32_t>::max(), std::max<long double>( 1.0L, raw ) );
        return static_cast<uint32_t>( capped );
    }

    void grantResource( const PlayerColor color, const int resource, const uint32_t amount )
    {
        if ( color == PlayerColor::NONE || amount == 0 ) {
            return;
        }

        world.GetKingdom( color ).AddFundsResource( Funds( resource, amount ) );
    }

    uint64_t cost( const uint64_t rank )
    {
        return 1 + rank / 10;
    }

    bool buy( Profile & profile, const size_t id )
    {
        if ( id >= upgradeCount || profile.ranks[id] == std::numeric_limits<uint64_t>::max() ) {
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
        uint64_t battleUses = 0;
        uint64_t adventureUses = 0;
        for ( size_t id = MONSTER_HUNTER; id <= SURVIVOR; ++id ) {
            battleUses = saturatedAdd( battleUses, profile.useCounts[id] );
        }
        for ( size_t id = SCAVENGER; id < upgradeCount; ++id ) {
            adventureUses = saturatedAdd( adventureUses, profile.useCounts[id] );
        }

        for ( size_t purchases = 0; purchases < 100000; ++purchases ) {
            size_t bestId = upgradeCount;
            long double bestReturn = 0;

            for ( size_t id = 0; id < upgradeCount; ++id ) {
                const uint64_t rank = profile.ranks[id];
                if ( rank == std::numeric_limits<uint64_t>::max() || profile.points < cost( rank ) ) {
                    continue;
                }

                const long double weight = [id, battleUses, adventureUses, &profile]() -> long double {
                    if ( id == EXPLORER ) {
                        return adventureUses == 0 ? 0.8L : 1.25L;
                    }
                    if ( id == MENTOR ) {
                        return battleUses == 0 ? 0.8L : 1.25L;
                    }
                    if ( id >= MONSTER_HUNTER && id <= SURVIVOR ) {
                        return ( static_cast<long double>( profile.useCounts[id] ) + 1.0L )
                               / ( static_cast<long double>( battleUses ) + 5.0L ) * 5.0L;
                    }
                    if ( id >= SCAVENGER ) {
                        return ( static_cast<long double>( profile.useCounts[id] ) + 1.0L )
                               / ( static_cast<long double>( adventureUses ) + 15.0L ) * 15.0L;
                    }
                    return 1.0L;
                }();

                const long double marginalReturn
                    = weight * ( effect( id, rank + 1 ) - effect( id, rank ) ) / static_cast<long double>( cost( rank ) );
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
            candidate.ranks[MIGHT] = oldRanks[0];
            candidate.ranks[GUARD] = oldRanks[1];
            candidate.ranks[SORCERY] = oldRanks[2];
            candidate.ranks[WISDOM] = oldRanks[3];
            candidate.ranks[MEDITATION] = oldRanks[4];
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
        case MIGHT: return "+" + formatEffect( value ) + " troop damage";
        case GUARD: return formatEffect( value ) + " physical resistance";
        case MARKSMAN: return "+" + formatEffect( value ) + " ranged damage";
        case DUELIST: return "+" + formatEffect( value ) + " melee damage";
        case UNDERDOG: return "+" + formatEffect( value ) + " outnumbered damage";
        case SORCERY: return "+" + formatEffect( value ) + " spell damage";
        case PYROMANCY: return "+" + formatEffect( value ) + " fire spell damage";
        case CRYOMANCY: return "+" + formatEffect( value ) + " cold spell damage";
        case STORMCRAFT: return "+" + formatEffect( value ) + " lightning damage";
        case CATACLYSM: return "+" + formatEffect( value ) + " area spell damage";
        case SPELL_WARD: return formatEffect( value ) + " spell resistance";
        case FIRE_WARD: return formatEffect( value ) + " fire resistance";
        case COLD_WARD: return formatEffect( value ) + " cold resistance";
        case STORM_WARD: return formatEffect( value ) + " lightning resistance";
        case CATACLYSM_WARD: return formatEffect( value ) + " area spell resistance";
        case WISDOM: return "+" + formatEffect( value * 0.35L ) + " troop + spell damage";
        case MEDITATION: return formatEffect( value * 0.5L ) + " spell resistance";
        case VETERAN: return formatEffect( value * 0.5L ) + " physical resistance";
        case EXPLORER: return "+" + formatEffect( value ) + " adventure bundles";
        case MENTOR: return "+" + formatEffect( value ) + " battle bundles";
        case MONSTER_HUNTER: return "+" + formatEffect( value ) + " bounty: gold + gems";
        case HERO_SLAYER: return "+" + formatEffect( value ) + " bounty: gold + crystal";
        case SIEGE_MASTER: return "+" + formatEffect( value ) + " salvage: gold + ore + wood";
        case DEFENDER: return "+" + formatEffect( value ) + " stores: wood + ore";
        case SURVIVOR: return "+" + formatEffect( value ) + " recovery: gold + wood";
        case SCAVENGER:
            return formatNumber( scaledReward( id, rank, 1200 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 12 ) ) + " wood";
        case TREASURE_HUNTER:
            return formatNumber( scaledReward( id, rank, 2500 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 18 ) ) + " gems";
        case RELIC_HUNTER:
            return formatNumber( scaledReward( id, rank, 30 ) ) + " gems + " + formatNumber( scaledReward( id, rank, 12 ) ) + " crystal";
        case PROSPECTOR:
            return formatNumber( scaledReward( id, rank, 40 ) ) + " ore + " + formatNumber( scaledReward( id, rank, 20 ) ) + " wood";
        case CASTELLAN:
            return formatNumber( scaledReward( id, rank, 1800 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 15 ) ) + " wood";
        case PILGRIM:
            return formatNumber( scaledReward( id, rank, 20 ) ) + " mercury + " + formatNumber( scaledReward( id, rank, 700 ) ) + " gold";
        case SCHOLAR:
            return formatNumber( scaledReward( id, rank, 20 ) ) + " crystal + " + formatNumber( scaledReward( id, rank, 10 ) ) + " mercury";
        case INSPIRATION:
            return formatNumber( scaledReward( id, rank, 20 ) ) + " gems + " + formatNumber( scaledReward( id, rank, 900 ) ) + " gold";
        case RECRUITER:
            return formatNumber( scaledReward( id, rank, 1800 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 15 ) ) + " wood";
        case STORYKEEPER:
            return formatNumber( scaledReward( id, rank, 1400 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 12 ) ) + " gems";
        case WAYFARER:
            return formatNumber( scaledReward( id, rank, 20 ) ) + " sulfur + " + formatNumber( scaledReward( id, rank, 8 ) ) + " mercury";
        case MARINER:
            return formatNumber( scaledReward( id, rank, 25 ) ) + " gems + " + formatNumber( scaledReward( id, rank, 1200 ) ) + " gold";
        case CARTOGRAPHER:
            return formatNumber( scaledReward( id, rank, 1800 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 10 ) ) + " crystal";
        case MERCHANT:
            return formatNumber( scaledReward( id, rank, 3000 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 20 ) ) + " ore/wood";
        case GENERALIST:
            return formatNumber( scaledReward( id, rank, 1000 ) ) + " gold + " + formatNumber( scaledReward( id, rank, 10 ) ) + " wood";
        default: return formatEffect( value );
        }
    }

    void showUpgradeDetails( const size_t id )
    {
        const uint64_t rank = playerProfile.ranks[id];
        const long double currentEffect = effect( id, rank );
        const bool canAdvance = rank < std::numeric_limits<uint64_t>::max();
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
        message += "\nTimes triggered: " + formatNumber( playerProfile.useCounts[id] );
        if ( id >= MONSTER_HUNTER && id <= SURVIVOR ) {
            message += "\nBattle rewards are paid after the matching battle condition is resolved.";
        }
        else if ( id >= SCAVENGER ) {
            message += "\nAdventure resource rewards trigger at most once per map tile for this profile.";
        }
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
                                  const bool defending, const bool siege )
{
    if ( color != activePlayerColor ) {
        return;
    }

    const bool neutral = opponent == PlayerColor::NONE;
    const auto recordUse = []( const size_t id ) { playerProfile.useCounts[id] = saturatedAdd( playerProfile.useCounts[id], 1 ); };
    recordUse( neutral ? MONSTER_HUNTER : HERO_SLAYER );
    if ( siege ) {
        recordUse( SIEGE_MASTER );
    }
    if ( defending ) {
        recordUse( DEFENDER );
    }
    if ( !won ) {
        recordUse( SURVIVOR );
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

    const long double quartermasterBonus = effect( MENTOR, playerProfile.ranks[MENTOR] );
    if ( won && neutral ) {
        const uint64_t bountyBase = 1000ULL + battleExperience / 4ULL;
        const uint64_t trophyGems = 8ULL + battleExperience / 2200ULL;
        grantResource( color, Resource::GOLD, scaledReward( MONSTER_HUNTER, playerProfile.ranks[MONSTER_HUNTER], bountyBase, quartermasterBonus ) );
        grantResource( color, Resource::GEMS, scaledReward( MONSTER_HUNTER, playerProfile.ranks[MONSTER_HUNTER], trophyGems, quartermasterBonus ) );
    }
    if ( won && !neutral ) {
        const uint64_t bountyBase = 1500ULL + battleExperience / 3ULL;
        const uint64_t capturedCrystal = 8ULL + battleExperience / 2000ULL;
        grantResource( color, Resource::GOLD, scaledReward( HERO_SLAYER, playerProfile.ranks[HERO_SLAYER], bountyBase, quartermasterBonus ) );
        grantResource( color, Resource::CRYSTAL, scaledReward( HERO_SLAYER, playerProfile.ranks[HERO_SLAYER], capturedCrystal, quartermasterBonus ) );
    }
    if ( won && siege ) {
        const uint64_t salvageGold = 1200ULL + battleExperience / 5ULL;
        const uint64_t salvageOre = 18ULL + battleExperience / 1500ULL;
        const uint64_t salvageWood = 12ULL + battleExperience / 1800ULL;
        grantResource( color, Resource::GOLD, scaledReward( SIEGE_MASTER, playerProfile.ranks[SIEGE_MASTER], salvageGold, quartermasterBonus ) );
        grantResource( color, Resource::ORE, scaledReward( SIEGE_MASTER, playerProfile.ranks[SIEGE_MASTER], salvageOre, quartermasterBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( SIEGE_MASTER, playerProfile.ranks[SIEGE_MASTER], salvageWood, quartermasterBonus ) );
    }
    if ( won && defending ) {
        const uint64_t repairWood = 14ULL + battleExperience / 1800ULL;
        const uint64_t repairOre = 8ULL + battleExperience / 2600ULL;
        grantResource( color, Resource::WOOD, scaledReward( DEFENDER, playerProfile.ranks[DEFENDER], repairWood, quartermasterBonus ) );
        grantResource( color, Resource::ORE, scaledReward( DEFENDER, playerProfile.ranks[DEFENDER], repairOre, quartermasterBonus ) );
    }
    if ( !won ) {
        const uint64_t rebuildingFund = 750ULL + battleExperience / 6ULL;
        const uint64_t emergencyWood = 8ULL + battleExperience / 2600ULL;
        grantResource( color, Resource::GOLD, scaledReward( SURVIVOR, playerProfile.ranks[SURVIVOR], rebuildingFund, quartermasterBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( SURVIVOR, playerProfile.ranks[SURVIVOR], emergencyWood, quartermasterBonus ) );
    }
}

void fheroes2::RPG::awardAdventureAction( const PlayerColor color, const int objectType, const int32_t tileIndex )
{
    if ( color != activePlayerColor || tileIndex < 0 ) {
        return;
    }

    size_t upgrade = GENERALIST;
    uint64_t base = 70;
    switch ( objectType ) {
    case MP2::OBJ_MONSTER:
    case MP2::OBJ_HERO:
    case MP2::OBJ_BOAT: return;
    case MP2::OBJ_RESOURCE: case MP2::OBJ_BARREL: case MP2::OBJ_BOTTLE: case MP2::OBJ_CAMPFIRE: case MP2::OBJ_FLOTSAM:
    case MP2::OBJ_WINDMILL: case MP2::OBJ_WATER_WHEEL: case MP2::OBJ_MAGIC_GARDEN: case MP2::OBJ_LEAN_TO:
        upgrade = SCAVENGER; base = 80; break;
    case MP2::OBJ_TREASURE_CHEST: case MP2::OBJ_SEA_CHEST: case MP2::OBJ_WAGON:
        upgrade = TREASURE_HUNTER; base = 180; break;
    case MP2::OBJ_ARTIFACT: case MP2::OBJ_SHIPWRECK_SURVIVOR: case MP2::OBJ_SKELETON:
        upgrade = RELIC_HUNTER; base = 200; break;
    case MP2::OBJ_MINE: case MP2::OBJ_ALCHEMIST_LAB: case MP2::OBJ_SAWMILL: case MP2::OBJ_LIGHTHOUSE: case MP2::OBJ_ABANDONED_MINE:
        upgrade = PROSPECTOR; base = 180; break;
    case MP2::OBJ_CASTLE: upgrade = CASTELLAN; base = 140; break;
    case MP2::OBJ_SHRINE_FIRST_CIRCLE: case MP2::OBJ_SHRINE_SECOND_CIRCLE: case MP2::OBJ_SHRINE_THIRD_CIRCLE: case MP2::OBJ_TEMPLE:
        upgrade = PILGRIM; base = 120; break;
    case MP2::OBJ_FORT: case MP2::OBJ_MERCENARY_CAMP: case MP2::OBJ_WITCH_DOCTORS_HUT: case MP2::OBJ_STANDING_STONES:
    case MP2::OBJ_ARENA: case MP2::OBJ_GAZEBO: case MP2::OBJ_WITCHS_HUT: case MP2::OBJ_TREE_OF_KNOWLEDGE:
        upgrade = SCHOLAR; base = 150; break;
    case MP2::OBJ_FOUNTAIN: case MP2::OBJ_FAERIE_RING: case MP2::OBJ_IDOL: case MP2::OBJ_MERMAID:
    case MP2::OBJ_OASIS: case MP2::OBJ_WATERING_HOLE: case MP2::OBJ_BUOY:
        upgrade = INSPIRATION; base = 90; break;
    case MP2::OBJ_WATCH_TOWER: case MP2::OBJ_EXCAVATION: case MP2::OBJ_CAVE: case MP2::OBJ_TREE_HOUSE:
    case MP2::OBJ_ARCHER_HOUSE: case MP2::OBJ_GOBLIN_HUT: case MP2::OBJ_DWARF_COTTAGE: case MP2::OBJ_HALFLING_HOLE:
    case MP2::OBJ_PEASANT_HUT: case MP2::OBJ_RUINS: case MP2::OBJ_TREE_CITY: case MP2::OBJ_WAGON_CAMP:
    case MP2::OBJ_DESERT_TENT: case MP2::OBJ_GENIE_LAMP: case MP2::OBJ_DRAGON_CITY: case MP2::OBJ_CITY_OF_DEAD:
    case MP2::OBJ_TROLL_BRIDGE: case MP2::OBJ_WATER_ALTAR: case MP2::OBJ_AIR_ALTAR: case MP2::OBJ_FIRE_ALTAR:
    case MP2::OBJ_EARTH_ALTAR: case MP2::OBJ_BARROW_MOUNDS:
        upgrade = RECRUITER; base = 140; break;
    case MP2::OBJ_EVENT: case MP2::OBJ_SIGN: case MP2::OBJ_SPHINX: case MP2::OBJ_ORACLE:
        upgrade = STORYKEEPER; base = 100; break;
    case MP2::OBJ_STONE_LITHS: case MP2::OBJ_WHIRLPOOL:
        upgrade = WAYFARER; base = 90; break;
    case MP2::OBJ_SHIPWRECK: case MP2::OBJ_DERELICT_SHIP: case MP2::OBJ_SIRENS:
        upgrade = MARINER; base = 130; break;
    case MP2::OBJ_OBSERVATION_TOWER: case MP2::OBJ_MAGELLANS_MAPS: case MP2::OBJ_OBELISK:
    case MP2::OBJ_HUT_OF_MAGI: case MP2::OBJ_EYE_OF_MAGI:
        upgrade = CARTOGRAPHER; base = 150; break;
    case MP2::OBJ_TRADING_POST: case MP2::OBJ_ALCHEMIST_TOWER:
        upgrade = MERCHANT; base = 90; break;
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

    playerProfile.useCounts[upgrade] = saturatedAdd( playerProfile.useCounts[upgrade], 1 );

    const long double levelScale = 1.0L + std::log1p( static_cast<long double>( playerProfile.level ) ) / 5.0L;
    addExperience( color, static_cast<uint64_t>( static_cast<long double>( base ) * levelScale ), ExperienceKind::ADVENTURE );

    const long double pathfinderBonus = effect( EXPLORER, playerProfile.ranks[EXPLORER] );
    const uint64_t rank = playerProfile.ranks[upgrade];
    switch ( upgrade ) {
    case SCAVENGER:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1200, pathfinderBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( upgrade, rank, 12, pathfinderBonus ) );
        break;
    case TREASURE_HUNTER:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 2500, pathfinderBonus ) );
        grantResource( color, Resource::GEMS, scaledReward( upgrade, rank, 18, pathfinderBonus ) );
        break;
    case RELIC_HUNTER:
        grantResource( color, Resource::GEMS, scaledReward( upgrade, rank, 30, pathfinderBonus ) );
        grantResource( color, Resource::CRYSTAL, scaledReward( upgrade, rank, 12, pathfinderBonus ) );
        break;
    case PROSPECTOR:
        grantResource( color, Resource::ORE, scaledReward( upgrade, rank, 40, pathfinderBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        break;
    case CASTELLAN:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1800, pathfinderBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( upgrade, rank, 15, pathfinderBonus ) );
        break;
    case PILGRIM:
        grantResource( color, Resource::MERCURY, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 700, pathfinderBonus ) );
        break;
    case SCHOLAR:
        grantResource( color, Resource::CRYSTAL, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        grantResource( color, Resource::MERCURY, scaledReward( upgrade, rank, 10, pathfinderBonus ) );
        break;
    case INSPIRATION:
        grantResource( color, Resource::GEMS, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 900, pathfinderBonus ) );
        break;
    case RECRUITER:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1800, pathfinderBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( upgrade, rank, 15, pathfinderBonus ) );
        break;
    case STORYKEEPER:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1400, pathfinderBonus ) );
        grantResource( color, Resource::GEMS, scaledReward( upgrade, rank, 12, pathfinderBonus ) );
        break;
    case WAYFARER:
        grantResource( color, Resource::SULFUR, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        grantResource( color, Resource::MERCURY, scaledReward( upgrade, rank, 8, pathfinderBonus ) );
        break;
    case MARINER:
        grantResource( color, Resource::GEMS, scaledReward( upgrade, rank, 25, pathfinderBonus ) );
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1200, pathfinderBonus ) );
        break;
    case CARTOGRAPHER:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1800, pathfinderBonus ) );
        grantResource( color, Resource::CRYSTAL, scaledReward( upgrade, rank, 10, pathfinderBonus ) );
        break;
    case MERCHANT:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 3000, pathfinderBonus ) );
        grantResource( color, Resource::ORE, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( upgrade, rank, 20, pathfinderBonus ) );
        break;
    case GENERALIST:
        grantResource( color, Resource::GOLD, scaledReward( upgrade, rank, 1000, pathfinderBonus ) );
        grantResource( color, Resource::WOOD, scaledReward( upgrade, rank, 10, pathfinderBonus ) );
        break;
    default:
        break;
    }
}

double fheroes2::RPG::damageMultiplier( const PlayerColor attacker, const PlayerColor defender, const bool ranged, const bool outnumbered )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );
    long double attackBonus = 0;
    if ( attackProfile != nullptr ) {
        attackBonus += effect( MIGHT, attackProfile->ranks[MIGHT] );
        attackBonus += effect( WISDOM, attackProfile->ranks[WISDOM] ) * 0.35L;
        const size_t style = ranged ? MARKSMAN : DUELIST;
        attackBonus += effect( style, attackProfile->ranks[style] );
        if ( outnumbered ) {
            attackBonus += effect( UNDERDOG, attackProfile->ranks[UNDERDOG] );
        }
    }

    long double defenseReduction = 0;
    if ( defenseProfile != nullptr ) {
        defenseReduction += effect( GUARD, defenseProfile->ranks[GUARD] );
        defenseReduction += effect( VETERAN, defenseProfile->ranks[VETERAN] ) * 0.5L;
    }
    defenseReduction = std::min<long double>( defenseReduction, 90.0L );

    return static_cast<double>( ( 1.0L + attackBonus / 100.0L ) * ( 1.0L - defenseReduction / 100.0L ) );
}

double fheroes2::RPG::spellMultiplier( const PlayerColor attacker, const PlayerColor defender, const int spellId )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );
    const size_t specialization = [spellId]() -> size_t {
        switch ( spellId ) {
        case Spell::FIREBALL:
        case Spell::FIREBLAST: return PYROMANCY;
        case Spell::COLDRAY:
        case Spell::COLDRING: return CRYOMANCY;
        case Spell::LIGHTNINGBOLT:
        case Spell::CHAINLIGHTNING: return STORMCRAFT;
        case Spell::ELEMENTALSTORM:
        case Spell::ARMAGEDDON: return CATACLYSM;
        default: return upgradeCount;
        }
    }();
    long double spellBonus = attackProfile == nullptr ? 0 : effect( SORCERY, attackProfile->ranks[SORCERY] );
    long double defenseReduction = defenseProfile == nullptr ? 0 : effect( SPELL_WARD, defenseProfile->ranks[SPELL_WARD] );
    if ( attackProfile != nullptr ) {
        spellBonus += effect( WISDOM, attackProfile->ranks[WISDOM] ) * 0.35L;
    }
    if ( defenseProfile != nullptr ) {
        defenseReduction += effect( MEDITATION, defenseProfile->ranks[MEDITATION] ) * 0.5L;
    }
    if ( specialization != upgradeCount ) {
        if ( attackProfile != nullptr ) {
            spellBonus += effect( specialization, attackProfile->ranks[specialization] );
        }
        if ( defenseProfile != nullptr ) {
            const size_t ward = specialization + ( FIRE_WARD - PYROMANCY );
            defenseReduction += effect( ward, defenseProfile->ranks[ward] );
        }
    }
    defenseReduction = std::min<long double>( defenseReduction, 90.0L );
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
                drawBeveledPanel( buyAreas[row], canBuy );
                drawSingleLine( upgrades[i].name, textX, rowArea.y + 4, rowArea.width - 150, fheroes2::FontType::normalYellow() );
                drawSingleLine( "Rank " + formatNumber( playerProfile.ranks[i] ), rowArea.x + rowArea.width - 94, rowArea.y + 7, 84,
                                fheroes2::FontType::smallWhite() );
                drawSingleLine( upgrades[i].description, textX, rowArea.y + 21, rowArea.width - 126, fheroes2::FontType::smallWhite() );

                const uint64_t rank = playerProfile.ranks[i];
                const std::string current = shortEffectSummary( i, rank );
                const std::string next = rank == std::numeric_limits<uint64_t>::max() ? "MAX" : shortEffectSummary( i, rank + 1 );
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
                "Steward", "Automatically buys the next rank with the best marginal effect per point. Battle and adventure specialties are weighted by how often you actually trigger them.",
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

