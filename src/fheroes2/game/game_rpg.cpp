// RPG-only wrapper around the existing implementation.
//
// Keep right-click/hold help popups compact before handing them to the standard
// fheroes2 dialog renderer. This avoids tall inspector dialogs overflowing at
// smaller resolutions while leaving every non-RPG dialog untouched.

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "dialog.h"
#include "ui_dialog.h"

namespace fheroes2
{
    namespace
    {
    namespace
    {
        void replaceAll( std::string & text, const std::string & from, const std::string & to )
        {
            if ( from.empty() ) {
                return;
            }

            size_t offset = 0;
            while ( ( offset = text.find( from, offset ) ) != std::string::npos ) {
                text.replace( offset, from.length(), to );
                offset += to.length();
            }
        }

        const char * title;
        uint64_t threshold;
    };
            }

            size_t offset = 0;
            while ( ( offset = text.find( from, offset ) ) != std::string::npos ) {
                text.replace( offset, from.size(), to );
                offset += to.size();
            }
        }

        std::string compactRpgPopupLine( std::string line )
        {
            replaceAll( line, " (Rank ", " R" );
            replaceAll( line, "): ", " | " );
            replaceAll( line, "Current: Rank ", "R" );
            replaceAll( line, "Ascension: ", "Asc: " );
            replaceAll( line, "Next Ascension: Rank ", "Next Asc R" );
            replaceAll( line, "Next: Rank ", "Next R" );
            replaceAll( line, "Next: MAX rank reached", "Next: MAX" );
            replaceAll( line, "Next: LOCKED (requires Critical Training)", "Next: LOCKED - Critical Training" );
            replaceAll( line, "Unlocks: ", "Unlock: " );
            replaceAll( line, "Available points: ", "Points: " );
            replaceAll( line, " points (Ready)", " | READY" );
            replaceAll( line, " points (Need ", " | NEED " );

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

    std::string compactRpgPopupBody( std::string body )
    {
        // Doctrine inspectors begin with a long explanatory paragraph. The first
        // sentence is enough for quick right-click context; keyboard/left-click
        // inspectors still use the normal full dialog path.
        const size_t detailsEnd = body.find( "\n\n" );
        if ( detailsEnd != std::string::npos ) {
            const size_t firstPeriod = body.find( '.' );
            if ( firstPeriod != std::string::npos && firstPeriod < detailsEnd ) {
                body.erase( firstPeriod + 1, detailsEnd - firstPeriod - 1 );
            }
        }

        return body;
    }
            }

            return line;
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
            }

            std::string compact;
            compact.reserve( std::min<size_t>( body.size(), 720 ) );

            size_t cursor = 0;
            size_t lineCount = 0;
            constexpr size_t maxLines = 12;
            while ( cursor <= body.size() && lineCount < maxLines ) {
                const size_t end = body.find( '\n', cursor );
                std::string line = body.substr( cursor, end == std::string::npos ? std::string::npos : end - cursor );

                // Collapse repeated blank lines. Dense right-click help is easier to scan
                // and consumes much less vertical space.
                if ( !line.empty() || compact.empty() || compact.back() != '\n' ) {
                    if ( !compact.empty() ) {
                        compact += '\n';
                    }
                    compact += compactRpgPopupLine( std::move( line ) );
                    ++lineCount;
                }

                if ( end == std::string::npos ) {
                    break;
                }
                cursor = end + 1;
                    break;
                }
                cursor = end + 1;
            }

            if ( cursor < body.size() ) {
                compact += "\n...";
            }

            constexpr size_t maxBodyLength = 720;
            if ( compact.size() > maxBodyLength ) {
                compact.resize( maxBodyLength - 3 );
                compact += "...";
            }

            return compact;
        }
    }

    int rpgShowStandardTextMessage( std::string headerText, std::string messageBody, const int buttons,
                                    const std::vector<const DialogElement *> & elements = {} )
    {
        // Dialog::ZERO is the existing fheroes2 convention used by these RPG
        // right-click/hold quick-info popups. Dialogs with explicit buttons keep
        // their full text so no detailed information is lost.
        if ( buttons == Dialog::ZERO ) {
            messageBody = compactRpgPopupBody( std::move( messageBody ) );
        }

        messageBody = compactRpgPopupBody( std::move( messageBody ) );
        }

<<<<<<< Updated upstream
        return showStandardTextMessage( std::move( headerText ), std::move( messageBody ), buttons, elements );
=======
        std::string message = tabDescriptions[tabIndex];
        message += "\nInvested: " + formatNumber( hallInvestment ) + " points";
        for ( size_t offset = 0; offset < upgradesPerTab; ++offset ) {
            const size_t id = tabIndex * upgradesPerTab + offset;
            const uint64_t rank = playerProfile.ranks[id];
            const uint8_t ascensionTier = ascensionTierForRank( rank );
            message += "\n" + std::string( upgrades[id].name ) + " Rank " + formatNumber( rank ) + ": " + shortEffectSummary( id, rank );
            if ( ascensionTier > 0 ) {
                message += " [" + std::string( ascensionTierLabel( ascensionTier ) ) + "]";
            }
            if ( !doctrineCanAdvance( id, rank ) ) {
                message += " [Maximum]";
            }
            else if ( id == BRUTAL_CRITICALS && playerProfile.ranks[CRITICAL_TRAINING] == 0 ) {
                message += " [Locked]";
            }
            else {
                message += " (" + formatNumber( cost( id, rank ) ) + " points)";
            }
        }
        fheroes2::showStandardTextMessage( tabNames[tabIndex], std::move( message ), buttons );
    }

    void showRoyalGuildHelp( const int buttons = Dialog::ZERO )
    {
        std::string message = "Keyboard Controls:\n";
        message += "1 to 8 or Tab: Switch Doctrine Halls\n";
        message += "Up or Down Arrow: Select Doctrine\n";
        message += "B, Enter, or Space: Purchase Selected Doctrine\n";
        message += "I: Doctrine Details | O: Overview | C: Realm Crests\n";
        message += "K: Build Analytics | L: Hall of Legends | V: Rival Intel\n";
        message += "S or A: Toggle Steward | R: Respec Points | Escape: Exit\n\n";
        message += "Mouse Controls:\n";
        message += "Left-Click: Purchase, Select, or Toggle Steward\n";
        message += "Right-Click: Quick Inspect without closing dialog";

        fheroes2::showStandardTextMessage( "Royal Guild Help", std::move( message ), buttons );
    }

    void showBuildAnalytics( const int buttons = Dialog::ZERO )
    {
        const uint64_t totalInvested = totalSpentPoints( playerProfile );
        const uint64_t prestigeRank = prestigeRankForLevel( playerProfile.level );

        std::string message = "Level " + formatNumber( playerProfile.level );
        if ( prestigeRank > 0 ) {
            const int prestigeBonusPct = static_cast<int>( std::round( ( prestigeBattleRenownMultiplier( playerProfile.level ) - 1.0L ) * 100.0L ) );
            message += " (Prestige " + formatNumber( prestigeRank ) + " +" + std::to_string( prestigeBonusPct ) + " percent)";
        }
        message += "\nPoints: " + formatNumber( playerProfile.points ) + " available, " + formatNumber( totalInvested ) + " invested";
        message += "\nRenown: Battle " + formatNumber( playerProfile.battleExperience )
                   + " | Adventure " + formatNumber( playerProfile.adventureExperience )
                   + " | Hero " + formatNumber( playerProfile.heroExperience );

        std::array<uint64_t, tabNames.size()> investedByTab{};
        std::array<uint64_t, tabNames.size()> triggersByTab{};
        uint64_t totalTriggers = 0;
        size_t activeDoctrines = 0;
        size_t ascendedDoctrines = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            investedByTab[id / upgradesPerTab] = saturatedAdd( investedByTab[id / upgradesPerTab], rankInvestment( id, playerProfile.ranks[id] ) );
            triggersByTab[id / upgradesPerTab] = saturatedAdd( triggersByTab[id / upgradesPerTab], playerProfile.useCounts[id] );
            totalTriggers = saturatedAdd( totalTriggers, playerProfile.useCounts[id] );
            if ( playerProfile.ranks[id] > 0 ) {
                ++activeDoctrines;
            }
            if ( ascensionTierForRank( playerProfile.ranks[id] ) > 0 ) {
                ++ascendedDoctrines;
            }
        }

        message += "\nDoctrines: " + formatNumber( activeDoctrines ) + "/" + formatNumber( upgradeCount )
                   + " (" + formatNumber( ascendedDoctrines ) + " ascended) | Triads: " + formatNumber( activeTriadCount( playerProfile ) ) + "/6";
        message += " | Crests: " + formatNumber( activeRealmCrestsCount( playerProfile ) ) + "/5";

        const auto investmentFocus = std::max_element( investedByTab.begin(), investedByTab.end() );
        const auto activityFocus = std::max_element( triggersByTab.begin(), triggersByTab.end() );
        std::string focusSummary;
        if ( investmentFocus != investedByTab.end() && *investmentFocus > 0 ) {
            focusSummary += "Build Focus: " + std::string( tabNames[static_cast<size_t>( std::distance( investedByTab.begin(), investmentFocus ) )] );
        }
        if ( activityFocus != triggersByTab.end() && *activityFocus > 0 ) {
            if ( !focusSummary.empty() ) {
                focusSummary += " | ";
            }
            focusSummary += "Combat Focus: " + std::string( tabNames[static_cast<size_t>( std::distance( triggersByTab.begin(), activityFocus ) )] );
        }
        if ( !focusSummary.empty() ) {
            message += "\n" + focusSummary;
        }

        const StewardGoal stewardGoal = findStewardGoal( playerProfile );
        message += "\nSteward: ";
        message += playerProfile.autoBuy ? "Active" : "Inactive";
        if ( stewardGoal.id < upgradeCount ) {
            message += " -> " + std::string( upgrades[stewardGoal.id].name ) + " Rank " + formatNumber( stewardGoal.targetRank );
        }

        if ( !heroRenownLedger.empty() ) {
            const auto topHero = std::max_element( heroRenownLedger.begin(), heroRenownLedger.end(), []( const auto & left, const auto & right ) {
                return left.second < right.second;
            } );
            const Heroes * hero = world.GetHeroes( topHero->first );
            const std::string heroName = hero != nullptr ? hero->GetName() : "Hero #" + std::to_string( topHero->first );
            message += "\nChampion: " + heroName + " (" + heroLegacyProgressText( topHero->second ) + ")";
        }

        const auto honors = activeHeroicHonors();
        message += " | Honors: " + formatNumber( honors.size() ) + "/7";

        fheroes2::showStandardTextMessage( "Build Analytics", std::move( message ), buttons );
    }

    void showRivalIntel( const int buttons = Dialog::ZERO )
    {
        std::string message = "Encounter Chance: " + std::to_string( eliteRivalChanceForLevel( playerProfile.level ) ) + " percent";
        message += " | Focus Halls: " + formatNumber( eliteRivalFocusHallLimit( playerProfile.level ) );
        const long double prestigeEliteBonus
            = std::min<long double>( 0.15L, static_cast<long double>( prestigeRankForLevel( playerProfile.level ) ) * 0.01L );
        const int eliteVictoryBonusPct = static_cast<int>( std::round( ( 0.35L + prestigeEliteBonus ) * 100.0L ) );
        message += " | Victory Bonus: +" + std::to_string( eliteVictoryBonusPct ) + " percent";

        if ( !eliteEnemyColors.empty() ) {
            std::string rivalList;
            for ( const PlayerColor color : eliteEnemyColors ) {
                if ( !rivalList.empty() ) {
                    rivalList += ", ";
                }
                rivalList += Color::String( color );
                const auto archIt = eliteRivalArchetypes.find( color );
                if ( archIt != eliteRivalArchetypes.end() && archIt->second != RivalArchetype::NONE ) {
                    rivalList += " (" + std::string( rivalArchetypeNames[static_cast<size_t>( archIt->second ) - 1] ) + ")";
                }
            }
            message += "\nRivals: " + rivalList;
        }
        else {
            message += "\nRivals: None active";
        }

        if ( !eliteRivalMutations.empty() ) {
            std::set<EliteMutation> uniqueMutations;
            for ( const auto & [color, mutations] : eliteRivalMutations ) {
                static_cast<void>( color );
                for ( const EliteMutation mutation : mutations ) {
                    uniqueMutations.insert( mutation );
                }
            }
            if ( !uniqueMutations.empty() ) {
                std::string mutationNames;
                for ( const EliteMutation mutation : uniqueMutations ) {
                    if ( !mutationNames.empty() ) {
                        mutationNames += ", ";
                    }
                    mutationNames += eliteMutationName( mutation );
                }
                message += "\nMutations (" + std::to_string( uniqueMutations.size() ) + "): " + mutationNames;
            }
        }
        message += "\nBonus: +5 percent Renown per active mutation.";

        fheroes2::showStandardTextMessage( "Elite Rival Intel", std::move( message ), buttons );
    }

    void showHallOfLegends( const int buttons = Dialog::ZERO )
    {
        const uint64_t topRenown = highestHeroRenown();
        const size_t topTier = highestHeroLegacyTierIndex();
        std::string message = "Honors of the Realm | Top: " + std::string( heroLegacyTiers[topTier].title ) + " (" + formatNumber( topRenown ) + " Renown)\n";

        if ( !heroRenownLedger.empty() ) {
            const auto topHero = std::max_element( heroRenownLedger.begin(), heroRenownLedger.end(), []( const auto & left, const auto & right ) {
                return left.second < right.second;
            } );
            const Heroes * hero = world.GetHeroes( topHero->first );
            const std::string heroName = hero != nullptr ? hero->GetName() : "Hero #" + std::to_string( topHero->first );
            message += "Champion: " + heroName;
            const auto chronicleIt = heroChronicleLedger.find( topHero->first );
            if ( chronicleIt != heroChronicleLedger.end() ) {
                message += " (" + heroChronicleSummary( chronicleIt->second ) + ")";
            }
            message += "\n";
        }

        const auto honors = activeHeroicHonors();
        message += "Active Honors (" + formatNumber( honors.size() ) + "/7):\n";
        message += std::string( hasHeroicHonor( 1 ) ? "[*] Proven (+1 Morale)  " : "[-] Proven (1,000 Renown)  " )
                   + ( hasHeroicHonor( 2 ) ? "[*] Famous (+1% Damage)\n" : "[-] Famous (5,000 Renown)\n" );
        message += std::string( hasHeroicHonor( 3 ) ? "[*] Legend (+2% Critical) " : "[-] Legend (25,000 Renown) " )
                   + ( hasHeroicHonor( 4 ) ? "[*] Mythic (+1 All Primary Skills)\n" : "[-] Mythic (100,000 Renown)\n" );
        message += std::string( hasRivalBaneHonor() ? "[*] Rival-Bane (+5% Renown)  " : "[-] Rival-Bane (Epithet)  " )
                   + ( hasCastlebreakerHonor() ? "[*] Castlebreaker (+2.5% Damage)\n" : "[-] Castlebreaker (Siege Victory)\n" );
        message += hasVeteranHonor() ? "[*] Veteran (+1.5% Regeneration)" : "[-] Veteran (10 Victories)";

        fheroes2::showStandardTextMessage( "Hall of Legends", std::move( message ), buttons );
    }

    void showKingdomOverview( const int buttons = Dialog::ZERO )
    {
        const uint64_t remainingXP = xpToNextLevel( playerProfile.level ) > playerProfile.progress
                                          ? xpToNextLevel( playerProfile.level ) - playerProfile.progress
                                          : 0;
        const uint64_t totalInvested = totalSpentPoints( playerProfile );
        const uint64_t nextLevelCost = xpToNextLevel( playerProfile.level );

        std::string message = "Kingdom Level " + formatNumber( playerProfile.level );
        const uint64_t prestigeRank = prestigeRankForLevel( playerProfile.level );
        if ( prestigeRank > 0 ) {
            const int prestigeBonusPct = static_cast<int>( std::round( ( prestigeBattleRenownMultiplier( playerProfile.level ) - 1.0L ) * 100.0L ) );
            message += " (Prestige " + formatNumber( prestigeRank ) + " +" + std::to_string( prestigeBonusPct ) + " percent)";
        }
        message += "\nExperience: " + formatNumber( playerProfile.progress ) + "/" + formatNumber( nextLevelCost )
                   + " (" + formatNumber( remainingXP ) + " to next level)";
        message += "\nPoints: " + formatNumber( playerProfile.points ) + " available, " + formatNumber( totalInvested ) + " invested";

        size_t activeDoctrines = 0;
        size_t ascendedDoctrines = 0;
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            if ( playerProfile.ranks[id] > 0 ) {
                ++activeDoctrines;
            }
            if ( ascensionTierForRank( playerProfile.ranks[id] ) > 0 ) {
                ++ascendedDoctrines;
            }
        }
        message += "\nDoctrines: " + formatNumber( activeDoctrines ) + "/" + formatNumber( upgradeCount )
                   + " (" + formatNumber( ascendedDoctrines ) + " ascended) | Triads: " + formatNumber( activeTriadCount( playerProfile ) ) + "/6";
        message += " | Crests: " + formatNumber( activeRealmCrestsCount( playerProfile ) ) + "/5";

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
            message += "\nChampion: " + heroName + " (" + heroLegacyProgressText( topHero->second ) + ")";
        }

        const auto honors = activeHeroicHonors();
        if ( !honors.empty() ) {
            message += " | Honors: " + formatNumber( honors.size() ) + "/7";
        }
        message += " | Crests: " + formatNumber( activeRealmCrestsCount( playerProfile ) ) + "/5";

        if ( !eliteEnemyColors.empty() ) {
            std::string rivalList;
            for ( const PlayerColor color : eliteEnemyColors ) {
                if ( !rivalList.empty() ) {
                    rivalList += ", ";
                }
                rivalList += Color::String( color );
            }
            message += "\nElite Rivals: " + rivalList;
        }

        message += "\nSteward: ";
        if ( playerProfile.autoBuy ) {
            message += "Active";
            const StewardGoal goal = findStewardGoal( playerProfile );
            if ( goal.id < upgradeCount ) {
                message += " -> " + std::string( upgrades[goal.id].name ) + " Rank " + formatNumber( goal.targetRank );
            }
        }
        else {
            message += "Inactive";
        }

        fheroes2::showStandardTextMessage( "Royal Guild Overview", std::move( message ), buttons );
    }

    void showRealmCrests( const int buttons = Dialog::ZERO )
    {
        const size_t activeCount = activeRealmCrestsCount( playerProfile );
        std::string message = "Royal Reliquary (" + std::to_string( activeCount ) + "/5 Active)\n";
        for ( size_t i = 0; i < realmCrests.size(); ++i ) {
            const bool active = hasRealmCrest( playerProfile, i );
            message += "\n";
            message += active ? "[*] " : "[-] ";
            message += realmCrests[i].name;
            if ( active ) {
                message += ": " + std::string( realmCrests[i].boon );
            }
            else {
                message += " (Requirement: " + std::string( realmCrests[i].req ) + ")";
            }
        }
        fheroes2::showStandardTextMessage( "Realm Crests", std::move( message ), buttons );
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
        if ( hasRealmCrest( playerProfile, 4 ) ) {
            base *= 1.15L;
        }
    }
    if ( won && hasHeroicHonor( 4 ) ) {
        base *= 1.10L;
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
    const uint64_t captureReward = hasCastlebreakerHonor() ? ( heroCaptureRenown * 3 / 2 ) : heroCaptureRenown;
    if ( color == activePlayerColor ) {
        static_cast<void>( addExperience( color, captureReward, ExperienceKind::ADVENTURE ) );
    }
    static_cast<void>( addHeroRenown( color, heroId, captureReward ) );
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

    long double honorScale = 1.0L;
    if ( hasHeroicHonor( 1 ) ) honorScale += 0.05L;
    if ( hasHeroicHonor( 2 ) ) honorScale += 0.05L;
    if ( hasHeroicHonor( 3 ) ) honorScale += 0.05L;
    if ( hasHeroicHonor( 4 ) ) honorScale += 0.10L;

    const long double levelScale = ( 1.0L + std::log1p( static_cast<long double>( playerProfile.level - 1 ) ) / 5.0L )
                                   * prestigeBattleRenownMultiplier( playerProfile.level ) * honorScale;
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
    const uint64_t triad = martialFoundationTriadBonus( *profile );
    const uint64_t mythicBonus = ( color == activePlayerColor && hasHeroicHonor( 4 ) ) ? 1 : 0;
    const uint64_t mutation = hasEliteMutation( color, EliteMutation::WARFORGED ) ? 3 : 0;
    return saturatedAdd( saturatedAdd( saturatedAdd( saturatedAdd( base, ascension ), triad ), mutation ), mythicBonus );
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
    const uint64_t triad = martialFoundationTriadBonus( *profile );
    const uint64_t mythicBonus = ( color == activePlayerColor && hasHeroicHonor( 4 ) ) ? 1 : 0;
    const uint64_t mutation = hasEliteMutation( color, EliteMutation::WARFORGED ) ? 3 : 0;
    return saturatedAdd( saturatedAdd( saturatedAdd( saturatedAdd( base, ascension ), triad ), mutation ), mythicBonus );
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
    if ( profile == nullptr ) {
        return 0;
    }
    const int base = static_cast<int>( effect( LEADERSHIP, profile->ranks[LEADERSHIP] ) );
    const int provenBonus = ( color == activePlayerColor && hasHeroicHonor( 1 ) ) ? 1 : 0;
    int total = base + provenBonus;
    if ( color != activePlayerColor && eliteEnemyColors.count( color ) > 0 && hasRealmCrest( playerProfile, 4 ) ) {
        total -= 1;
    }
    return std::clamp( total, -3, 3 );
}

int fheroes2::RPG::luckBonus( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr ? 0 : static_cast<int>( effect( FORTUNE, profile->ranks[FORTUNE] ) );
}

double fheroes2::RPG::lifeStealPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    const double veteranBonus = ( color == activePlayerColor && hasVeteranHonor() ) ? 1.5 : 0.0;
    return profile == nullptr
               ? 0.0
               : static_cast<double>( effect( BLOOD_DRINKER, profile->ranks[BLOOD_DRINKER] )
                                      + ascensionChannelBonus( *profile, AscensionChannel::LIFE_STEAL )
                                      + sustainTriadResonance( *profile )
                                      + veteranBonus
                                      + ( hasEliteMutation( color, EliteMutation::VAMPIRIC ) ? 8.0L : 0.0L ) );
}

double fheroes2::RPG::killHealPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    const double veteranBonus = ( color == activePlayerColor && hasVeteranHonor() ) ? 1.5 : 0.0;
    return profile == nullptr ? 0.0 : static_cast<double>( effect( REAPER, profile->ranks[REAPER] )
                                                          + sustainTriadResonance( *profile ) * 1.25L
                                                          + veteranBonus );
}

double fheroes2::RPG::regenerationPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    const double veteranBonus = ( color == activePlayerColor && hasVeteranHonor() ) ? 1.5 : 0.0;
    return profile == nullptr
               ? 0.0
               : static_cast<double>( effect( REGENERATION, profile->ranks[REGENERATION] )
                                      + ascensionChannelBonus( *profile, AscensionChannel::REGENERATION )
                                      + sustainTriadResonance( *profile ) * 0.75L
                                      + veteranBonus
                                      + ( hasEliteMutation( color, EliteMutation::REGENERATING ) ? 5.0L : 0.0L ) );
}

double fheroes2::RPG::criticalChance( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    const double legendaryBonus = ( color == activePlayerColor && hasHeroicHonor( 3 ) ) ? 2.0 : 0.0;
    return profile == nullptr
               ? 0.0
               : static_cast<double>( std::min<long double>(
                     100.0L, effect( CRITICAL_TRAINING, profile->ranks[CRITICAL_TRAINING] )
                                 + ascensionChannelBonus( *profile, AscensionChannel::CRITICAL_CHANCE )
                                 + doctrinePairResonance( *profile, CRITICAL_TRAINING, FORTUNE, 0.15L, 3.0L )
                                 + criticalPrecisionTriadResonance( *profile )
                                 + legendaryBonus
                                 + ( hasEliteMutation( color, EliteMutation::DEADLY ) ? 4.0L : 0.0L ) ) );
}

double fheroes2::RPG::criticalDamageBonusPercent( const PlayerColor color )
{
    const Profile * profile = getProfile( color );
    return profile == nullptr
               ? 50.0
               : 50.0 + static_cast<double>( effect( BRUTAL_CRITICALS, profile->ranks[BRUTAL_CRITICALS] )
                                             + ascensionChannelBonus( *profile, AscensionChannel::CRITICAL_DAMAGE )
                                             + criticalPrecisionTriadResonance( *profile ) * 3.0L
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
    const double legendaryBonus = ( color == activePlayerColor && hasHeroicHonor( 3 ) ) ? 2.0 : 0.0;
    return profile == nullptr
               ? 0.0
               : static_cast<double>( std::min<long double>(
                     100.0L, effect( EVASION, profile->ranks[EVASION] )
                                 + ascensionChannelBonus( *profile, AscensionChannel::EVASION )
                                 + legendaryBonus
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
        if ( !ranged ) {
            attackBonus += doctrinePairResonance( *attackProfile, FEROCITY, BRAWLER, 0.20L, 5.0L );
        }
        else {
            attackBonus += doctrinePairResonance( *attackProfile, MARKSMAN, ARMOR_PIERCING, 0.20L, 5.0L );
        }

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
                attackBonus += doctrinePairResonance( *attackProfile, OPENING_BLOW, DISCIPLINE, 0.20L, 5.0L );
                attackBonus += doctrineTripleResonance( *attackProfile, OPENING_BLOW, DISCIPLINE, UNYIELDING, 0.22L, 6.5L );
            }
        }
        if ( defenderBelowHalf ) {
            attackBonus += effect( RUTHLESS, attackProfile->ranks[RUTHLESS] );
            attackBonus += finisherTriadResonance( *attackProfile );
        }
        if ( attacker == activePlayerColor ) {
            if ( hasHeroicHonor( 2 ) ) {
                attackBonus += 1.0L;
            }
            if ( defender != PlayerColor::NONE && eliteEnemyColors.count( defender ) > 0 && hasRivalBaneHonor() ) {
                attackBonus += 5.0L;
            }
            if ( hasCastlebreakerHonor() ) {
                attackBonus += 2.5L;
            }
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
        if ( !ranged ) {
            defenseReduction += doctrinePairResonance( *defenseProfile, IRON_SKIN, MELEE_GUARD, 0.20L, 5.0L );
        }
        else {
            defenseReduction += doctrinePairResonance( *defenseProfile, IRON_SKIN, ARROW_WARD, 0.20L, 5.0L );
        }

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
        defenseReduction += defensiveBastionTriadResonance( *defenseProfile );
        if ( defender == activePlayerColor && hasHeroicHonor( 2 ) ) {
            defenseReduction += 1.0L;
        }
        if ( defender == activePlayerColor && hasRealmCrest( *defenseProfile, 2 ) ) {
            defenseReduction += 3.0L;
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
                    + elementalConvergenceTriadResonance( *attackProfile )
                    + ( ( attacker == activePlayerColor && hasRealmCrest( *attackProfile, 3 ) ) ? 5.0L : 0.0L )
                    + ( hasEliteMutation( attacker, EliteMutation::UNSTABLE_MAGIC ) ? 12.0L : 0.0L );
    long double defenseReduction
        = defenseProfile == nullptr
              ? 0
              : effect( SPELL_WARD, defenseProfile->ranks[SPELL_WARD] )
                    + ascensionChannelBonus( *defenseProfile, AscensionChannel::SPELL_REDUCTION )
                    + wardMatrixTriadResonance( *defenseProfile )
                    + ( ( defender == activePlayerColor && hasHeroicHonor( 2 ) ) ? 1.0L : 0.0L )
                    + ( ( defender == activePlayerColor && hasRealmCrest( *defenseProfile, 2 ) ) ? 3.0L : 0.0L )
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

    if ( attackProfile != nullptr ) {
        spellBonus += doctrinePairResonance( *attackProfile, SORCERY, ARCANE_PIERCING, 0.20L, 5.0L );
    }
    if ( defenseProfile != nullptr ) {
        defenseReduction += doctrinePairResonance( *defenseProfile, SPELL_WARD, IRON_SKIN, 0.20L, 5.0L );
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
    if ( profile == nullptr ) {
        return 0.0;
    }

    if ( upgradeId == ARMS_TRAINING || upgradeId == ARMOR_TRAINING || upgradeId == VETERAN_CORE ) {
        return static_cast<double>( diminishingFlatStatBonus( profile->ranks[upgradeId] ) );
    }

    return static_cast<double>( effect( upgradeId, profile->ranks[upgradeId] ) );
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

                const uint64_t rank = playerProfile.ranks[i];
                const bool prerequisiteMet = i != BRUTAL_CRITICALS || playerProfile.ranks[CRITICAL_TRAINING] > 0;
                const bool rankCanAdvance = doctrineCanAdvance( i, rank );
                const uint64_t price = rankCanAdvance ? cost( i, rank ) : 0;
                const bool canBuy = rankCanAdvance && prerequisiteMet && playerProfile.points >= price;
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
                drawSingleLine( "Rank " + formatNumber( rank ), rowArea.x + rowArea.width - 94, rowArea.y + 7, 84,
                                fheroes2::FontType::smallWhite() );
                drawSingleLine( upgrades[i].description, textX, rowArea.y + 21, rowArea.width - 126, fheroes2::FontType::smallWhite() );

                const std::string current = shortEffectSummary( i, rank );
                std::string next = "MAX EFFECT";
                if ( rankCanAdvance ) {
                    if ( effect( i, rank + 1 ) > effect( i, rank ) ) {
                        next = shortEffectSummary( i, rank + 1 );
                    }
                    else {
                        next = "Ascension R" + formatNumber( nextAscensionRank( rank ) );
                    }
                }
                drawSingleLine( current + " -> " + next, textX, rowArea.y + 36, rowArea.width - 130,
                                canBuy ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
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
            drawSingleLine( "1-8 Tabs  O Overview  C Crests  K Analytics  L Legends  V Rivals  H Help  S Steward  R Respec", area.x + 12, area.y + 344,
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
            fheroes2::showStandardTextMessage( "Close", "Exit the Royal Guild menu (Esc or F9).", Dialog::ZERO );
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
            fheroes2::showStandardTextMessage( "Scroll Up", "Scroll up one doctrine row (Up Arrow).", Dialog::ZERO );
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
            fheroes2::showStandardTextMessage( "Scroll Down", "Scroll down one doctrine row (Down Arrow).", Dialog::ZERO );
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
            fheroes2::showStandardTextMessage( "Scrollbar", "Click track above/below slider to page scroll.", Dialog::ZERO );
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

        if ( event.isKeyPressed( fheroes2::Key::KEY_H ) || event.isKeyPressed( fheroes2::Key::KEY_F1 )
             || event.isKeyPressed( fheroes2::Key::KEY_QUESTION ) || event.isKeyPressed( fheroes2::Key::KEY_SLASH ) ) {
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

        if ( event.isKeyPressed( fheroes2::Key::KEY_L ) ) {
            showHallOfLegends( Dialog::OK );
            redraw = true;
            continue;
        }

        if ( event.isKeyPressed( fheroes2::Key::KEY_C ) ) {
            showRealmCrests( Dialog::OK );
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
                const uint8_t oldAscension = ascensionTierForRank( playerProfile.ranks[i] );
                if ( buy( playerProfile, i ) ) {
                    saveProfile();
                    if ( ascensionTierForRank( playerProfile.ranks[i] ) > oldAscension ) {
                        AudioManager::PlaySound( M82::GOODMRLE );
                    }
                    else {
                        AudioManager::PlaySound( M82::EXPERNCE );
                    }
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
            const uint8_t oldAscension = ascensionTierForRank( playerProfile.ranks[i] );
            if ( buy( playerProfile, i ) ) {
                saveProfile();
                if ( ascensionTierForRank( playerProfile.ranks[i] ) > oldAscension ) {
                    AudioManager::PlaySound( M82::GOODMRLE );
                }
                else {
                    AudioManager::PlaySound( M82::EXPERNCE );
                }
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
                "Auto-allocates points based on combat focus.\nToggle: Left-click, S, or A.",
                Dialog::ZERO );
            redraw = true;
            continue;
        }
        if ( event.MouseClickLeft( autoButton.area() ) || event.isKeyPressed( fheroes2::Key::KEY_S ) || event.isKeyPressed( fheroes2::Key::KEY_A ) ) {
            playerProfile.autoBuy = !playerProfile.autoBuy;
            if ( playerProfile.autoBuy ) {
                AudioManager::PlaySound( M82::BUILDTWN );
                autoBuy( playerProfile );
            }
            else {
                AudioManager::PlaySound( M82::DISRUPTR );
            }
            saveProfile();
            redraw = true;
        }

        if ( event.isMouseRightButtonPressedInArea( respecButton.area() ) || event.MouseLongPressLeft( respecButton.area() ) ) {
            fheroes2::showStandardTextMessage(
                "Respec Doctrines",
                "Refund all spent points and reset ranks to reallocate freely. Click or press R.",
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
                    AudioManager::PlaySound( M82::TREASURE );
                    redraw = true;
                }
            }
        }
>>>>>>> Stashed changes
    }
}

// Intercept only calls originating from the RPG implementation in this translation unit.
// The real shared dialog API is not modified.
#define showStandardTextMessage rpgShowStandardTextMessage
#include "game_rpg_impl.inc"
#undef showStandardTextMessage
