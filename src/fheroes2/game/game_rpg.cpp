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
        UpgradeInfo{ "Might", "All army damage" }, { "Guard", "Physical resistance" }, { "Marksman", "Ranged damage" },
        { "Duelist", "Melee damage" }, { "Underdog", "Outnumbered damage" },
        { "Sorcery", "All spell damage" }, { "Pyromancy", "Fire spell damage" }, { "Cryomancy", "Cold spell damage" },
        { "Stormcraft", "Lightning damage" }, { "Cataclysm", "Wide-area spell damage" },
        { "Spell Ward", "All spell resistance" }, { "Fire Ward", "Fire resistance" }, { "Cold Ward", "Cold resistance" },
        { "Storm Ward", "Lightning resistance" }, { "Chaos Ward", "Wide-area resistance" },
        { "Wisdom", "All RPG experience" }, { "Meditation", "Offline experience" }, { "Veteran", "Battle experience" },
        { "Explorer", "Adventure experience" }, { "Mentor", "Hero-earned experience" },
        { "Monster Hunter", "Neutral battle experience" }, { "Hero Slayer", "Enemy hero battle XP" },
        { "Siege Master", "Castle battle experience" }, { "Defender", "Defensive battle XP" }, { "Survivor", "Lost battle experience" },
        { "Scavenger", "Resource pickup XP" }, { "Treasure Hunter", "Chest experience" }, { "Relic Hunter", "Artifact experience" },
        { "Prospector", "Mine capture experience" }, { "Castellan", "Castle visit experience" },
        { "Pilgrim", "Shrine experience" }, { "Scholar", "Skill-site experience" }, { "Inspiration", "Morale and luck XP" },
        { "Recruiter", "Dwelling experience" }, { "Storykeeper", "Map-event experience" },
        { "Wayfarer", "Teleport experience" }, { "Mariner", "Sea travel experience" }, { "Cartographer", "Map discovery XP" },
        { "Merchant", "Trading-post experience" }, { "Generalist", "Other adventure XP" }
    };
    constexpr std::array<const char *, upgradeCount> upgradeDetails{
        "Increases physical damage dealt by every troop in your kingdom's battles.",
        "Reduces physical damage received by your troops. Its effect approaches 75% resistance, so armies cannot become invulnerable.",
        "Adds physical damage when one of your troops makes a ranged attack.",
        "Adds physical damage when one of your troops attacks in melee.",
        "Adds physical damage when the attacking troop has fewer creatures than its target.",
        "Increases damage from every damaging spell cast for your kingdom.",
        "Adds damage to Fireball and Fireblast spells.",
        "Adds damage to Cold Ray and Cold Ring spells.",
        "Adds damage to Lightning Bolt and Chain Lightning spells.",
        "Adds damage to Elemental Storm and Armageddon spells.",
        "Reduces damage received from every damaging spell. Total spell resistance cannot exceed 90%.",
        "Adds resistance against Fireball and Fireblast damage.",
        "Adds resistance against Cold Ray and Cold Ring damage.",
        "Adds resistance against Lightning Bolt and Chain Lightning damage.",
        "Adds resistance against Elemental Storm and Armageddon damage.",
        "Adds a bonus to all RPG experience earned by this kingdom.",
        "Adds a bonus to RPG experience earned from the full offline interval.",
        "Adds a bonus to RPG experience earned from battles.",
        "Adds a bonus to RPG experience earned from adventure-map actions.",
        "Adds a bonus when a hero gains the game's normal hero experience.",
        "Adds RPG experience for battles against neutral monsters.",
        "Adds RPG experience for battles against another player's army.",
        "Adds RPG experience when fighting at a castle, attacking or defending.",
        "Adds RPG experience when your army defends in battle.",
        "Adds RPG experience even when your army loses a battle.",
        "Adds RPG experience from resource pickups and producing map objects.",
        "Adds RPG experience from treasure and sea chests and wagons.",
        "Adds RPG experience from artifacts, skeletons, and shipwreck survivors.",
        "Adds RPG experience from mines, sawmills, labs, and lighthouses.",
        "Adds RPG experience on your first action at a castle tile on a map.",
        "Adds RPG experience from shrines and temples.",
        "Adds RPG experience from skill-training sites, arenas, gazebos, and knowledge trees.",
        "Adds RPG experience from morale and luck sites.",
        "Adds RPG experience from creature dwellings and recruitment sites.",
        "Adds RPG experience from map events, signs, sphinxes, and oracles.",
        "Adds RPG experience from stone liths and whirlpools.",
        "Adds RPG experience from shipwrecks, derelict ships, and sirens.",
        "Adds RPG experience from observation towers, obelisks, maps, and Magi sites.",
        "Adds RPG experience from trading posts and alchemist towers.",
        "Adds RPG experience from other actionable adventure-map sites."
    };
    constexpr std::array<const char *, 8> tabNames{ "War", "Magic", "Wards", "Growth", "Battles", "Spoils", "Sites", "Travel" };

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
        // Damage and XP ranks remain open-ended; only resistance approaches a safety limit.
        return id == GUARD ? 75.0L * value / ( value + 50.0L ) : 5.0L * std::log1p( value );
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
        // Marginal effect per point is the same quantity displayed in the upgrade tabs.
        // XP upgrades receive a small weight because they compound into future level-ups.
        // Offline-only XP is valued by the profile's observed offline share.
        const long double totalExperience = static_cast<long double>( profile.fieldExperience ) + profile.offlineExperience;
        const long double offlineShare = totalExperience > 0 ? static_cast<long double>( profile.offlineExperience ) / totalExperience : 0.5L;
        const long double battleShare = totalExperience > 0 ? static_cast<long double>( profile.battleExperience ) / totalExperience : 0.3L;
        const long double adventureShare = totalExperience > 0 ? static_cast<long double>( profile.adventureExperience ) / totalExperience : 0.3L;
        const long double heroShare = totalExperience > 0 ? static_cast<long double>( profile.heroExperience ) / totalExperience : 0.2L;
        uint64_t battleUses = 0;
        uint64_t adventureUses = 0;
        for ( size_t id = MONSTER_HUNTER; id <= SURVIVOR; ++id ) battleUses = saturatedAdd( battleUses, profile.useCounts[id] );
        for ( size_t id = SCAVENGER; id < upgradeCount; ++id ) adventureUses = saturatedAdd( adventureUses, profile.useCounts[id] );
        for ( size_t purchases = 0; purchases < 100000; ++purchases ) {
            size_t bestId = upgradeCount;
            long double bestReturn = 0;

            for ( size_t id = 0; id < upgradeCount; ++id ) {
                const uint64_t rank = profile.ranks[id];
                if ( rank == std::numeric_limits<uint64_t>::max() || profile.points < cost( rank ) ) {
                    continue;
                }

                const long double weight = [id, offlineShare, battleShare, adventureShare, heroShare, battleUses, adventureUses, &profile]() -> long double {
                    if ( id == WISDOM ) return 1.2L;
                    if ( id == MEDITATION ) return 1.2L * offlineShare;
                    if ( id == VETERAN ) return 1.2L * battleShare;
                    if ( id == EXPLORER ) return 1.2L * adventureShare;
                    if ( id == MENTOR ) return 1.2L * heroShare;
                    if ( id >= MONSTER_HUNTER && id <= SURVIVOR )
                        return battleShare * ( static_cast<long double>( profile.useCounts[id] ) + 1 ) / ( static_cast<long double>( battleUses ) + 5 ) * 2.5L;
                    if ( id >= SCAVENGER )
                        return adventureShare * ( static_cast<long double>( profile.useCounts[id] ) + 1 ) / ( static_cast<long double>( adventureUses ) + 15 ) * 5.25L;
                    return 1.0L;
                }();
                const long double marginalReturn = weight * ( effect( id, rank + 1 ) - effect( id, rank ) ) / static_cast<long double>( cost( rank ) );
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

    void showUpgradeDetails( const size_t id )
    {
        const uint64_t rank = playerProfile.ranks[id];
        const long double currentEffect = effect( id, rank );
        const bool canAdvance = rank < std::numeric_limits<uint64_t>::max();
        std::string message = upgradeDetails[id];
        message += "\n\nCurrent rank: " + formatNumber( rank );
        message += "\nCurrent effect: +" + formatEffect( currentEffect );
        if ( canAdvance ) {
            const long double nextEffect = effect( id, rank + 1 );
            message += "\nNext effect: +" + formatEffect( nextEffect );
            message += "\nNext rank adds: +" + formatEffect( nextEffect - currentEffect );
            message += "\nNext rank costs: " + formatNumber( cost( rank ) ) + " points";
        }
        message += "\nAvailable points: " + formatNumber( playerProfile.points );
        if ( id >= SCAVENGER ) {
            message += "\nEach adventure tile can award RPG XP once per profile.";
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

    long double bonus = effect( WISDOM, playerProfile.ranks[WISDOM] );
    switch ( kind ) {
    case ExperienceKind::OFFLINE: bonus += effect( MEDITATION, playerProfile.ranks[MEDITATION] ); break;
    case ExperienceKind::BATTLE: bonus += effect( VETERAN, playerProfile.ranks[VETERAN] ); break;
    case ExperienceKind::ADVENTURE: bonus += effect( EXPLORER, playerProfile.ranks[EXPLORER] ); break;
    case ExperienceKind::HERO: bonus += effect( MENTOR, playerProfile.ranks[MENTOR] ); break;
    }
    const long double boosted = static_cast<long double>( amount ) * ( 1.0L + bonus / 100.0L );
    const uint64_t gained = boosted >= static_cast<long double>( std::numeric_limits<uint64_t>::max() )
                                ? std::numeric_limits<uint64_t>::max()
                                : static_cast<uint64_t>( boosted );
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

    long double bonus = 0;
    const bool neutral = opponent == PlayerColor::NONE;
    bonus += effect( neutral ? MONSTER_HUNTER : HERO_SLAYER, playerProfile.ranks[neutral ? MONSTER_HUNTER : HERO_SLAYER] );
    if ( siege ) bonus += effect( SIEGE_MASTER, playerProfile.ranks[SIEGE_MASTER] );
    if ( defending ) bonus += effect( DEFENDER, playerProfile.ranks[DEFENDER] );
    if ( !won ) bonus += effect( SURVIVOR, playerProfile.ranks[SURVIVOR] );
    const auto recordUse = []( const size_t id ) { playerProfile.useCounts[id] = saturatedAdd( playerProfile.useCounts[id], 1 ); };
    recordUse( neutral ? MONSTER_HUNTER : HERO_SLAYER );
    if ( siege ) recordUse( SIEGE_MASTER );
    if ( defending ) recordUse( DEFENDER );
    if ( !won ) recordUse( SURVIVOR );

    const Profile * opponentProfile = getProfile( opponent );
    const long double challenge = opponentProfile == nullptr ? 1.0L : std::clamp( static_cast<long double>( opponentProfile->level )
                                                                            / std::max<long double>( 1.0L, static_cast<long double>( playerProfile.level ) ), 0.5L, 2.0L );
    const long double base = ( won ? 250.0L : 100.0L ) + static_cast<long double>( battleExperience ) * ( won ? 0.5L : 0.2L );
    const long double earned = base * challenge * ( 1.0L + bonus / 100.0L );
    addExperience( color, static_cast<uint64_t>( std::min( earned, static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) ),
                   ExperienceKind::BATTLE );
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
        if ( !MP2::isInGameActionObject( static_cast<MP2::MapObjectType>( objectType ), false ) ) return;
        break;
    }

    const uint64_t key = ( static_cast<uint64_t>( world.GetMapSeed() ) << 32 ) | static_cast<uint32_t>( tileIndex );
    if ( !visitedActionTiles.insert( key ).second ) {
        return;
    }
    playerProfile.useCounts[upgrade] = saturatedAdd( playerProfile.useCounts[upgrade], 1 );
    const long double levelScale = 1.0L + std::log1p( static_cast<long double>( playerProfile.level ) ) / 5.0L;
    const long double bonus = 1.0L + effect( upgrade, playerProfile.ranks[upgrade] ) / 100.0L;
    addExperience( color, static_cast<uint64_t>( static_cast<long double>( base ) * levelScale * bonus ), ExperienceKind::ADVENTURE );
}

double fheroes2::RPG::damageMultiplier( const PlayerColor attacker, const PlayerColor defender, const bool ranged, const bool outnumbered )
{
    const Profile * attackProfile = getProfile( attacker );
    const Profile * defenseProfile = getProfile( defender );
    long double attackBonus = 0;
    if ( attackProfile != nullptr ) {
        attackBonus += effect( MIGHT, attackProfile->ranks[MIGHT] );
        const size_t style = ranged ? MARKSMAN : DUELIST;
        attackBonus += effect( style, attackProfile->ranks[style] );
        if ( outnumbered ) {
            attackBonus += effect( UNDERDOG, attackProfile->ranks[UNDERDOG] );
        }
    }
    const long double defenseReduction = defenseProfile == nullptr ? 0 : effect( GUARD, defenseProfile->ranks[GUARD] );
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
    fheroes2::StandardWindow window( 420, 378, true, display );
    const fheroes2::Rect area = window.activeArea();
    const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();
    const int scrollIcn = isEvilInterface ? ICN::SCROLLE : ICN::SCROLL;
    constexpr size_t upgradesPerTab = 5;
    constexpr size_t visibleRows = 3;
    std::array<fheroes2::Rect, tabNames.size()> tabAreas{};
    std::array<fheroes2::Rect, visibleRows> visibleUpgradeAreas{};
    std::array<size_t, tabNames.size()> scrollOffsets{};
    const fheroes2::Rect statsArea( area.x + 12, area.y + 29, area.width - 24, 39 );
    const fheroes2::Rect listArea( area.x + 12, area.y + 130, area.width - 24, 193 );
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
            drawText( "KINGDOM RPG", area.x + 12, area.y + 6, area.width - 24, fheroes2::FontType::normalYellow() );

            window.applyTextBackgroundShading( statsArea );
            drawSingleLine( "LEVEL  " + formatNumber( playerProfile.level ), statsArea.x + 12, statsArea.y + 5, statsArea.width / 2 - 18,
                            fheroes2::FontType::smallYellow() );
            drawSingleLine( "POINTS  " + formatNumber( playerProfile.points ), statsArea.x + statsArea.width / 2 + 5, statsArea.y + 5,
                            statsArea.width / 2 - 17, fheroes2::FontType::smallYellow() );
            const uint64_t remainingXP = xpToNextLevel( playerProfile.level ) > playerProfile.progress
                                             ? xpToNextLevel( playerProfile.level ) - playerProfile.progress
                                             : 0;
            drawText( "XP " + formatExperience( playerProfile.experience ) + "    " + formatExperience( remainingXP ) + " to next", statsArea.x + 8,
                      statsArea.y + 18, statsArea.width - 16,
                      fheroes2::FontType::smallWhite() );
            const int32_t barWidth = statsArea.width - 24;
            const uint64_t nextLevelCost = xpToNextLevel( playerProfile.level );
            const long double progressRatio = nextLevelCost == 0 ? 0.0L : std::min( 1.0L, static_cast<long double>( playerProfile.progress ) / nextLevelCost );
            fheroes2::Fill( display, statsArea.x + 12, statsArea.y + 34, barWidth, 2, fheroes2::GetColorId( 53, 42, 32 ) );
            fheroes2::Fill( display, statsArea.x + 12, statsArea.y + 34, static_cast<int32_t>( barWidth * progressRatio ), 2,
                            fheroes2::GetColorId( 219, 175, 66 ) );

            for ( size_t i = 0; i < tabNames.size(); ++i ) {
                tabAreas[i] = { area.x + 12 + static_cast<int32_t>( i % 4 ) * 99, area.y + 74 + static_cast<int32_t>( i / 4 ) * 25, 96, 23 };
                window.applyTextBackgroundShading( tabAreas[i] );
                if ( i == tab ) {
                    fheroes2::Fill( display, tabAreas[i].x + 5, tabAreas[i].y + 2, tabAreas[i].width - 10, 2,
                                    fheroes2::GetColorId( 219, 175, 66 ) );
                }
                drawText( tabNames[i], tabAreas[i].x + 4, tabAreas[i].y + 5, tabAreas[i].width - 8,
                          i == tab ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
            }

            window.applyTextBackgroundShading( listArea );
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
                rowArea = { listArea.x + 5, listArea.y + 5 + static_cast<int32_t>( row * 61 ), listArea.width - 31, 56 };
                window.applyTextBackgroundShading( rowArea );
                const bool canBuy = playerProfile.ranks[i] < std::numeric_limits<uint64_t>::max()
                                    && playerProfile.points >= cost( playerProfile.ranks[i] );
                if ( canBuy ) {
                    fheroes2::Fill( display, rowArea.x + 3, rowArea.y + 7, 2, rowArea.height - 14, fheroes2::GetColorId( 219, 175, 66 ) );
                }
                drawSingleLine( upgrades[i].name, rowArea.x + 10, rowArea.y + 5, rowArea.width - 110, fheroes2::FontType::normalYellow() );
                drawSingleLine( "Rank " + formatNumber( playerProfile.ranks[i] ), rowArea.x + rowArea.width - 96, rowArea.y + 8, 86,
                                fheroes2::FontType::smallWhite() );
                drawSingleLine( upgrades[i].description, rowArea.x + 10, rowArea.y + 26, rowArea.width - 20, fheroes2::FontType::smallWhite() );
                drawSingleLine( "+" + formatEffect( effect( i, playerProfile.ranks[i] ) ) + "   Cost "
                                    + formatNumber( cost( playerProfile.ranks[i] ) ) + " pt",
                                rowArea.x + 10, rowArea.y + 42, rowArea.width - 76,
                                canBuy ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
                drawText( "BUY", rowArea.x + rowArea.width - 60, rowArea.y + 41, 50,
                          canBuy ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
            }

            drawText( std::to_string( scrollOffsets[tab] + 1 ) + "-" + std::to_string( scrollOffsets[tab] + visibleRows )
                          + " of 5   Wheel to scroll   Right-click for details",
                      area.x + 12, area.y + 327, area.width - 24, fheroes2::FontType::smallWhite() );
            window.renderTextAdaptedButtonSprite( autoButton, playerProfile.autoBuy ? "Auto-buy ON" : "Auto-buy OFF", { 18, 6 },
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
        if ( tabChanged ) continue;

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
            if ( event.MouseClickLeft( visibleUpgradeAreas[row] ) && buy( playerProfile, i ) ) {
                saveProfile();
                redraw = true;
                break;
            }
        }
        if ( event.isMouseRightButtonPressedInArea( autoButton.area() ) ) {
            fheroes2::showStandardTextMessage( "Auto-buy ROI", "Automatically buys the upgrade with the best next effect per point. Battle and adventure choices also use your activity history.",
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
