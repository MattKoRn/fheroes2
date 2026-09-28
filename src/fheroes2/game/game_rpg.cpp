// RPG-only wrapper around the existing implementation.
//
// Keep right-click/hold help popups compact before handing them to the standard
// fheroes2 dialog renderer. This avoids tall inspector dialogs overflowing at
// smaller resolutions while leaving every non-RPG dialog untouched.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dialog.h"
#include "ui_dialog.h"

namespace fheroes2
{
    namespace
    {
        std::string compactRpgPopupLine( std::string line )
        {
            // Trim trailing whitespace while keeping all words fully unabbreviated.
            while ( !line.empty() && ( line.back() == ' ' || line.back() == '\r' || line.back() == '\t' ) ) {
                line.pop_back();
            }

            return line;
        }

        std::string compactRpgPopupBody( std::string body )
        {
            // For right-click inspection cards (Dialog::ZERO), keeping layout compact
            // ensures zero vertical overflow while keeping all information intact and unabbreviated.
            std::string compact;
            compact.reserve( body.size() );

            size_t cursor = 0;
            while ( cursor < body.size() ) {
                const size_t end = body.find( '\n', cursor );
                std::string line = body.substr( cursor, end == std::string::npos ? std::string::npos : end - cursor );

                line = compactRpgPopupLine( std::move( line ) );

                // Collapse repeated blank lines. Dense right-click help is easier to scan
                // and consumes much less vertical space without overflow.
                if ( !line.empty() || ( !compact.empty() && compact.back() != '\n' ) ) {
                    if ( !compact.empty() ) {
                        compact += '\n';
                    }
                    compact += std::move( line );
                }

                if ( end == std::string::npos ) {
                    break;
                }
                cursor = end + 1;
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

        return showStandardTextMessage( std::move( headerText ), std::move( messageBody ), buttons, elements );
    }
}

// Intercept only calls originating from the RPG implementation in this translation unit.
// The real shared dialog API is not modified. Battle and town-capture awards are wrapped below
// so Hero Mastery can feed milestone rewards back into the existing RPG XP progression loop.
// addExperience is also wrapped so external offline awards can recover a stalled timestamp handoff
// and make their rare doctrine-drop roll without changing any ordinary field XP paths.
#define showStandardTextMessage rpgShowStandardTextMessage
#define addExperience rpgAddExperienceImpl
#define awardBattle rpgAwardBattleImpl
#define awardTownCapture rpgAwardTownCaptureImpl
#include "game_rpg_impl.inc"
#undef awardTownCapture
#undef awardBattle
#undef addExperience
#undef showStandardTextMessage

namespace
{
    constexpr std::array<const char *, 8> heroRoleCallings{
        "Vanguard", "Reaver", "Strategist", "Sentinel", "Arcanist", "Warden", "Marshal", "Paragon"
    };

    constexpr std::array<uint64_t, 5> heroMasteryThresholds{ 0, 2500, 10000, 40000, 150000 };
    constexpr std::array<uint64_t, 5> heroMasteryRewardExperience{ 0, 1000, 2500, 6000, 12500 };
    constexpr std::array<const char *, 5> adventurerMasteryTitles{ "Wanderer", "Pathfinder", "Trailblazer", "Hero", "Living Legend" };
    constexpr std::array<std::array<const char *, 5>, 8> heroMasteryTitles{ {
        std::array<const char *, 5>{ "Line Recruit", "Shieldbearer", "Vanguard", "War Captain", "Iron Legend" },
        std::array<const char *, 5>{ "Skirmisher", "Ravager", "Reaver", "Bloodlord", "Doom Herald" },
        std::array<const char *, 5>{ "Scout", "Tactician", "Strategist", "Battle Sage", "Fatewright" },
        std::array<const char *, 5>{ "Guard", "Shieldbearer", "Sentinel", "High Sentinel", "Living Fortress" },
        std::array<const char *, 5>{ "Apprentice", "Spellbinder", "Magus", "Archmage", "Arcane Sovereign" },
        std::array<const char *, 5>{ "Watcher", "Runeguard", "Warden", "High Warden", "Eternal Aegis" },
        std::array<const char *, 5>{ "Officer", "Commander", "Marshal", "High Marshal", "Crown General" },
        std::array<const char *, 5>{ "Aspirant", "Champion", "Paragon", "Exemplar", "Living Myth" },
    } };

    // Campaign Momentum is a transparent streak-style identity layer derived from persistent
    // Chronicle deeds. It intentionally does not claim to be an undefeated battle counter because
    // the Chronicle stores aggregate victories rather than a full chronological win/loss history.
    constexpr std::array<uint64_t, 6> heroStreakThresholds{ 0, 5, 15, 40, 100, 250 };
    constexpr std::array<const char *, 6> heroStreakLabels{ "Quiet", "Gathering", "Hot", "Dominant", "Relentless", "Legendary Run" };

    constexpr uint64_t battleDoctrineDropDenominator = 500;
    constexpr uint64_t offlineDoctrineDropDenominator = 5000;
    constexpr uint64_t offlineDoctrineXpPerRoll = 2500;
    constexpr uint64_t maximumOfflineDoctrineRolls = 10;
    constexpr int64_t minimumOfflineRecoverySeconds = 5 * 60;
    constexpr int64_t offlineSecondsPerDayForRecovery = 24 * 60 * 60;

    static_assert( heroMasteryThresholds.size() == heroMasteryRewardExperience.size() );
    static_assert( heroMasteryThresholds[0] == 0 );
    static_assert( heroMasteryRewardExperience[0] == 0 );
    static_assert( heroMasteryThresholds[0] < heroMasteryThresholds[1] );
    static_assert( heroMasteryThresholds[1] < heroMasteryThresholds[2] );
    static_assert( heroMasteryThresholds[2] < heroMasteryThresholds[3] );
    static_assert( heroMasteryThresholds[3] < heroMasteryThresholds[4] );
    static_assert( heroMasteryRewardExperience[1] < heroMasteryRewardExperience[2] );
    static_assert( heroMasteryRewardExperience[2] < heroMasteryRewardExperience[3] );
    static_assert( heroMasteryRewardExperience[3] < heroMasteryRewardExperience[4] );
    static_assert( heroStreakThresholds[0] == 0 );
    static_assert( heroStreakThresholds[0] < heroStreakThresholds[1] );
    static_assert( heroStreakThresholds[1] < heroStreakThresholds[2] );
    static_assert( heroStreakThresholds[2] < heroStreakThresholds[3] );
    static_assert( heroStreakThresholds[3] < heroStreakThresholds[4] );
    static_assert( heroStreakThresholds[4] < heroStreakThresholds[5] );
    static_assert( battleDoctrineDropDenominator > 1 );
    static_assert( offlineDoctrineDropDenominator > battleDoctrineDropDenominator );
    static_assert( offlineDoctrineXpPerRoll > 0 );
    static_assert( maximumOfflineDoctrineRolls > 0 );
    static_assert( minimumOfflineRecoverySeconds > 0 );
    static_assert( offlineSecondsPerDayForRecovery > minimumOfflineRecoverySeconds );

    uint64_t recoverStalledOfflineExperience( const uint64_t requestedAmount )
    {
        if ( requestedAmount > 0 ) {
            return requestedAmount;
        }

        std::ifstream input( System::concatPath( fheroes2::RPG::dataDirectory(), "offline_progress.dat" ) );
        if ( !input ) {
            return 0;
        }

        int64_t lastSeenUnix = 0;
        std::array<int64_t, 7> dailyIncome{};
        uint32_t efficiencyPercent = 100;
        bool hasLastSeen = false;
        bool hasDailyIncome = false;

        std::string line;
        while ( std::getline( input, line ) ) {
            std::istringstream row( line );
            std::string key;
            row >> key;
            if ( key == "last_seen_unix" ) {
                hasLastSeen = static_cast<bool>( row >> lastSeenUnix );
            }
            else if ( key == "daily_income" ) {
                hasDailyIncome = true;
                for ( int64_t & value : dailyIncome ) {
                    if ( !( row >> value ) || value < 0 || value > std::numeric_limits<int32_t>::max() ) {
                        hasDailyIncome = false;
                        break;
                    }
                }
            }
            else if ( key == "state_efficiency_percent" ) {
                row >> efficiencyPercent;
            }
        }

        if ( !hasLastSeen || !hasDailyIncome || lastSeenUnix <= 0 ) {
            return 0;
        }

        const int64_t now
            = std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() ).count();
        if ( now <= lastSeenUnix ) {
            return 0;
        }

        const int64_t elapsedSeconds = now - lastSeenUnix;
        if ( elapsedSeconds < minimumOfflineRecoverySeconds ) {
            return 0;
        }

        // A zero incoming award can happen when the focus-resume interval was lost before the
        // adventure loop consumed it. Use the same persisted timestamp and baseline economy that
        // already drive offline progression, but only as a zero-award fallback so normal rewards
        // can never be doubled or inflated.
        static int64_t recoveredSnapshotUnix = 0;
        if ( recoveredSnapshotUnix == lastSeenUnix ) {
            return 0;
        }

        const long double commonIncome = static_cast<long double>( dailyIncome[0] + dailyIncome[2] ) * 100.0L;
        const long double rareIncome
            = static_cast<long double>( dailyIncome[1] + dailyIncome[3] + dailyIncome[4] + dailyIncome[5] ) * 500.0L;
        const long double goldIncome = static_cast<long double>( dailyIncome[6] ) * 15.0L;
        const long double baselineDailyEquivalent = std::max<long double>( 2500.0L, commonIncome + rareIncome + goldIncome );
        const uint32_t clampedEfficiency = std::clamp<uint32_t>( efficiencyPercent, 100, 150 );
        const long double recovered
            = baselineDailyEquivalent * static_cast<long double>( clampedEfficiency ) / 100.0L
              * static_cast<long double>( elapsedSeconds ) / static_cast<long double>( offlineSecondsPerDayForRecovery );

        if ( recovered < 1.0L ) {
            return 0;
        }

        recoveredSnapshotUnix = lastSeenUnix;
        if ( recovered >= static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) {
            return std::numeric_limits<uint64_t>::max();
        }
        return static_cast<uint64_t>( recovered );
    }

    void showOfflineRecoveryPopup( const uint64_t creditedExperience )
    {
        if ( creditedExperience == 0 ) {
            return;
        }

        std::string message = "Recovered +" + fheroes2::RPG::formatExperience( creditedExperience );
        message += " RPG XP from the saved offline timestamp.";
        const fheroes2::AutoPlayPopupTimeoutScope timeoutScope( true );
        fheroes2::showStandardTextMessage( "Offline Progress Recovered", std::move( message ), Dialog::ZERO );
    }

    std::mt19937_64 & doctrineDropRng()
    {
        static std::mt19937_64 rng = []() {
            std::random_device randomDevice;
            std::seed_seq seed{ randomDevice(), randomDevice(), randomDevice(), randomDevice() };
            return std::mt19937_64( seed );
        }();
        return rng;
    }

    bool rollOneIn( const uint64_t denominator )
    {
        if ( denominator <= 1 ) {
            return true;
        }

        std::uniform_int_distribution<uint64_t> distribution( 1, denominator );
        return distribution( doctrineDropRng() ) == 1;
    }

    size_t grantRandomDoctrineDrop( const PlayerColor color )
    {
        if ( color == PlayerColor::NONE || color != activePlayerColor ) {
            return upgradeCount;
        }

        std::vector<size_t> eligible;
        eligible.reserve( upgradeCount );
        for ( size_t id = 0; id < upgradeCount; ++id ) {
            const uint64_t rank = playerProfile.ranks[id];
            if ( !doctrineCanAdvance( id, rank ) ) {
                continue;
            }
            if ( id == BRUTAL_CRITICALS && playerProfile.ranks[CRITICAL_TRAINING] == 0 ) {
                continue;
            }
            eligible.emplace_back( id );
        }

        if ( eligible.empty() ) {
            return upgradeCount;
        }

        std::uniform_int_distribution<size_t> distribution( 0, eligible.size() - 1 );
        const size_t id = eligible[distribution( doctrineDropRng() )];
        ++playerProfile.ranks[id];
        saveProfile();
        return id;
    }

    void showDoctrineDropPopup( const size_t id, const bool offline )
    {
        if ( id >= upgradeCount ) {
            return;
        }

        std::string message = offline ? "Offline progress uncovered a rare doctrine: " : "A defeated enemy dropped a rare doctrine: ";
        message += upgrades[id].name;
        message += ".\nRank increased to " + formatNumber( playerProfile.ranks[id] ) + ".";

        // Reuse the engine's existing timed popup path. Dialog::ZERO keeps this compact and the
        // scoped timeout dismisses it automatically instead of blocking until the player clicks.
        const fheroes2::AutoPlayPopupTimeoutScope timeoutScope( true );
        fheroes2::showStandardTextMessage( "Doctrine Drop", std::move( message ), Dialog::ZERO );
    }

    void rollOfflineDoctrineDrop( const PlayerColor color, const uint64_t creditedExperience )
    {
        const uint64_t rolls = std::min<uint64_t>( maximumOfflineDoctrineRolls, creditedExperience / offlineDoctrineXpPerRoll );
        for ( uint64_t roll = 0; roll < rolls; ++roll ) {
            if ( !rollOneIn( offlineDoctrineDropDenominator ) ) {
                continue;
            }

            const size_t doctrine = grantRandomDoctrineDrop( color );
            if ( doctrine < upgradeCount ) {
                showDoctrineDropPopup( doctrine, true );
            }
            break;
        }
    }

    std::pair<size_t, uint64_t> strongestDoctrineHall( const PlayerColor color )
    {
        constexpr size_t doctrineHalls = heroRoleCallings.size();
        constexpr size_t doctrinesPerHall = static_cast<size_t>( fheroes2::RPG::UPGRADE_COUNT ) / doctrineHalls;
        static_assert( doctrinesPerHall * doctrineHalls == static_cast<size_t>( fheroes2::RPG::UPGRADE_COUNT ) );

        size_t strongestHall = 0;
        uint64_t strongestScore = 0;

        for ( size_t hall = 0; hall < doctrineHalls; ++hall ) {
            uint64_t score = 0;
            for ( size_t offset = 0; offset < doctrinesPerHall; ++offset ) {
                const uint64_t rank = fheroes2::RPG::doctrineRank( color, hall * doctrinesPerHall + offset );
                score = rank > std::numeric_limits<uint64_t>::max() - score ? std::numeric_limits<uint64_t>::max() : score + rank;
            }

            // Strict comparison intentionally gives ties a deterministic hall-order priority.
            if ( score > strongestScore ) {
                strongestScore = score;
                strongestHall = hall;
            }
        }

        return { strongestHall, strongestScore };
    }

    HeroChronicle heroChronicleFor( const int32_t heroId )
    {
        const auto entry = heroChronicleLedger.find( heroId );
        return entry == heroChronicleLedger.end() ? HeroChronicle{} : entry->second;
    }

    uint64_t heroMasteryScore( const int32_t heroId )
    {
        const HeroChronicle chronicle = heroChronicleFor( heroId );

        // Renown remains the backbone of mastery. Chronicle deeds then add explicit achievement
        // weight so two heroes with similar Renown can still develop visibly different standing.
        uint64_t score = fheroes2::RPG::heroRenown( heroId );
        score = saturatedAdd( score, saturatedMultiply( chronicle.battleVictories, 250 ) );
        score = saturatedAdd( score, saturatedMultiply( chronicle.castleCaptures, 1000 ) );
        score = saturatedAdd( score, saturatedMultiply( chronicle.eliteVictories, 3000 ) );
        return score;
    }

    uint64_t heroStreakScore( const int32_t heroId )
    {
        const HeroChronicle chronicle = heroChronicleFor( heroId );

        // Ordinary victories establish momentum, while major strategic victories accelerate it.
        // Elite victories are already battle victories; the additional weight is deliberate because
        // they represent a much more significant expedition achievement.
        uint64_t score = chronicle.battleVictories;
        score = saturatedAdd( score, saturatedMultiply( chronicle.castleCaptures, 3 ) );
        score = saturatedAdd( score, saturatedMultiply( chronicle.eliteVictories, 6 ) );
        return score;
    }

    size_t heroMasteryTierIndex( const uint64_t score )
    {
        for ( size_t i = heroMasteryThresholds.size(); i > 0; --i ) {
            if ( score >= heroMasteryThresholds[i - 1] ) {
                return i - 1;
            }
        }
        return 0;
    }

    uint64_t heroMasteryMilestoneReward( const uint64_t before, const uint64_t after )
    {
        if ( after <= before ) {
            return 0;
        }

        uint64_t reward = 0;
        for ( size_t tier = 1; tier < heroMasteryThresholds.size(); ++tier ) {
            if ( before < heroMasteryThresholds[tier] && after >= heroMasteryThresholds[tier] ) {
                reward = saturatedAdd( reward, heroMasteryRewardExperience[tier] );
            }
        }
        return reward;
    }

    uint64_t nextHeroMasteryReward( const uint64_t score )
    {
        const size_t tier = heroMasteryTierIndex( score );
        return tier + 1 < heroMasteryRewardExperience.size() ? heroMasteryRewardExperience[tier + 1] : 0;
    }

    size_t heroStreakTierIndex( const uint64_t score )
    {
        for ( size_t i = heroStreakThresholds.size(); i > 0; --i ) {
            if ( score >= heroStreakThresholds[i - 1] ) {
                return i - 1;
            }
        }
        return 0;
    }

    const char * heroMasteryTitle( const size_t hall, const uint64_t hallRanks, const size_t tier )
    {
        if ( hallRanks == 0 ) {
            return adventurerMasteryTitles[tier];
        }
        return heroMasteryTitles[hall][tier];
    }
}

namespace fheroes2::RPG
{
    uint64_t addExperience( const PlayerColor color, const uint64_t amount, const ExperienceKind kind )
    {
        const bool attemptedRecovery = kind == ExperienceKind::OFFLINE && amount == 0;
        const uint64_t effectiveAmount = attemptedRecovery ? recoverStalledOfflineExperience( amount ) : amount;
        const uint64_t credited = rpgAddExperienceImpl( color, effectiveAmount, kind );

        if ( attemptedRecovery && effectiveAmount > 0 && credited > 0 ) {
            showOfflineRecoveryPopup( credited );
        }
        if ( kind == ExperienceKind::OFFLINE && credited > 0 ) {
            rollOfflineDoctrineDrop( color, credited );
        }
        return credited;
    }

    void awardBattle( const PlayerColor color, const PlayerColor opponent, const uint32_t battleExperience, const bool won, const bool defending,
                      const bool siege, const int32_t heroId )
    {
        const uint64_t masteryBefore = heroMasteryScore( heroId );
        rpgAwardBattleImpl( color, opponent, battleExperience, won, defending, siege, heroId );
        const uint64_t milestoneReward = heroMasteryMilestoneReward( masteryBefore, heroMasteryScore( heroId ) );
        if ( milestoneReward > 0 ) {
            static_cast<void>( addExperience( color, milestoneReward, ExperienceKind::HERO ) );
        }

        if ( won && color == activePlayerColor && rollOneIn( battleDoctrineDropDenominator ) ) {
            const size_t doctrine = grantRandomDoctrineDrop( color );
            if ( doctrine < upgradeCount ) {
                showDoctrineDropPopup( doctrine, false );
            }
        }
    }

    void awardTownCapture( const PlayerColor color, const int32_t heroId )
    {
        const uint64_t masteryBefore = heroMasteryScore( heroId );
        rpgAwardTownCaptureImpl( color, heroId );
        const uint64_t milestoneReward = heroMasteryMilestoneReward( masteryBefore, heroMasteryScore( heroId ) );
        if ( milestoneReward > 0 ) {
            static_cast<void>( addExperience( color, milestoneReward, ExperienceKind::HERO ) );
        }
    }
}

std::string fheroes2::RPG::heroLegacyText( const int32_t heroId )
{
    return heroLegacyProgressText( heroRenown( heroId ) );
}

std::string fheroes2::RPG::heroChronicleText( const int32_t heroId )
{
    return heroChronicleSummary( heroChronicleFor( heroId ) );
}

std::string fheroes2::RPG::heroRoleText( const PlayerColor color, const int32_t heroId )
{
    const HeroChronicle chronicle = heroChronicleFor( heroId );

    std::string role = heroChronicleEpithet( chronicle );
    const auto [hall, hallRanks] = strongestDoctrineHall( color );
    if ( hallRanks == 0 ) {
        role += " Adventurer";
        return role;
    }

    role += ' ';
    role += heroRoleCallings[hall];
    role += " (" + formatNumber( hallRanks ) + " hall ranks)";
    return role;
}

std::string fheroes2::RPG::heroMasteryText( const PlayerColor color, const int32_t heroId )
{
    const auto [hall, hallRanks] = strongestDoctrineHall( color );
    const uint64_t score = heroMasteryScore( heroId );
    const size_t tier = heroMasteryTierIndex( score );

    std::string text = heroMasteryTitle( hall, hallRanks, tier );
    text += " - " + formatNumber( score );

    if ( tier + 1 < heroMasteryThresholds.size() ) {
        const uint64_t nextThreshold = heroMasteryThresholds[tier + 1];
        const uint64_t remaining = nextThreshold > score ? nextThreshold - score : 0;
        text += " / " + formatNumber( nextThreshold );
        text += " (" + formatNumber( remaining ) + " to " + heroMasteryTitle( hall, hallRanks, tier + 1 );
        text += "; +" + formatExperience( nextHeroMasteryReward( score ) ) + " RPG XP)";
    }
    else {
        text += " (maximum mastery tier; all mastery rewards earned)";
    }

    return text;
}

std::string fheroes2::RPG::heroAccoladeText( const int32_t heroId )
{
    const HeroChronicle chronicle = heroChronicleFor( heroId );
    const uint64_t renown = heroRenown( heroId );

    if ( chronicle.eliteVictories >= 5 ) {
        return "Nemesis of Elite Rivals";
    }
    if ( chronicle.eliteVictories >= 1 ) {
        return "Vanquisher of the Elite";
    }
    if ( chronicle.castleCaptures >= 10 ) {
        return "Conqueror of Realms";
    }
    if ( chronicle.castleCaptures >= 5 ) {
        return "Master Siege Commander";
    }
    if ( chronicle.castleCaptures >= 1 ) {
        return "Castle Breaker";
    }
    if ( chronicle.battleVictories >= 50 ) {
        return "Grand Centurion";
    }
    if ( chronicle.battleVictories >= 20 ) {
        return "Veteran of Twenty Battles";
    }
    if ( chronicle.battleVictories >= 5 ) {
        return "Seasoned Campaigner";
    }
    if ( renown >= 50000 ) {
        return "Paragon of Renown";
    }
    if ( renown >= 10000 ) {
        return "Hero of the Realm";
    }

    return "Initiate of the Expedition";
}

std::string fheroes2::RPG::heroStreakText( const int32_t heroId )
{
    const uint64_t score = heroStreakScore( heroId );
    const size_t tier = heroStreakTierIndex( score );

    std::string text = heroStreakLabels[tier];
    text += " - " + formatNumber( score ) + " momentum";

    if ( tier + 1 < heroStreakThresholds.size() ) {
        const uint64_t nextThreshold = heroStreakThresholds[tier + 1];
        const uint64_t remaining = nextThreshold > score ? nextThreshold - score : 0;
        text += " / " + formatNumber( nextThreshold );
        text += " (" + formatNumber( remaining ) + " to " + heroStreakLabels[tier + 1] + ")";
    }
    else {
        text += " (maximum momentum tier)";
    }

    return text;
}

std::string fheroes2::RPG::heroInspectionSummary( const PlayerColor color, const int32_t heroId )
{
    const HeroChronicle chronicle = heroChronicleFor( heroId );
    const auto [hall, hallRanks] = strongestDoctrineHall( color );
    const uint64_t score = heroMasteryScore( heroId );
    const size_t tier = heroMasteryTierIndex( score );
    const uint64_t renown = heroRenown( heroId );
    const size_t legacyTier = heroLegacyTierIndex( renown );

    std::string summary = "Role: ";
    summary += heroChronicleEpithet( chronicle );
    if ( hallRanks == 0 ) {
        summary += " Adventurer";
    }
    else {
        summary += ' ';
        summary += heroRoleCallings[hall];
        summary += " (" + formatNumber( hallRanks ) + " hall ranks)";
    }

    summary += "\nMastery: ";
    summary += heroMasteryTitle( hall, hallRanks, tier );
    summary += " - " + formatNumber( score );
    if ( tier + 1 < heroMasteryThresholds.size() ) {
        summary += " / " + formatNumber( heroMasteryThresholds[tier + 1] );
        summary += "; next reward +" + formatExperience( nextHeroMasteryReward( score ) ) + " RPG XP";
    }
    else {
        summary += "; all mastery rewards earned";
    }

    summary += "\nMomentum: " + heroStreakText( heroId );

    summary += "\nLegacy: ";
    summary += heroLegacyTiers[legacyTier].title;
    summary += " - " + formatNumber( renown );
    if ( legacyTier + 1 < heroLegacyTiers.size() ) {
        summary += " / " + formatNumber( heroLegacyTiers[legacyTier + 1].threshold );
    }
    summary += " Renown";

    summary += "\nChronicle: " + formatNumber( chronicle.battleVictories ) + " battle victories, "
               + formatNumber( chronicle.castleCaptures ) + " castle captures, "
               + formatNumber( chronicle.eliteVictories ) + " Elite victories";

    summary += "\nAccolade: " + heroAccoladeText( heroId );

    return summary;
}

void fheroes2::RPG::showFieldLedger()
{
    const PlayerColor color = activePlayerColor != PlayerColor::NONE ? activePlayerColor : PlayerColor::BLUE;
    std::string message = "Kingdom Level: " + formatNumber( playerProfile.level );
    const uint64_t prestige = prestigeRankForLevel( playerProfile.level );
    if ( prestige > 0 ) {
        message += " (Prestige " + formatNumber( prestige ) + ")";
    }
    message += "\nAvailable Points: " + formatNumber( playerProfile.points );

    const auto [focusHall, focusRanks] = strongestDoctrineHall( color );
    if ( focusRanks > 0 ) {
        message += "\nKingdom Calling: " + std::string( heroRoleCallings[focusHall] ) + " (" + formatNumber( focusRanks ) + " hall ranks)";
    }

    message += "\n\nExpedition Heroes:";

    size_t heroCount = 0;
    if ( color != PlayerColor::NONE ) {
        const VecHeroes & heroes = world.GetKingdom( color ).GetHeroes();
        for ( const Heroes * hero : heroes ) {
            if ( hero == nullptr ) {
                continue;
            }
            ++heroCount;
            const int32_t heroId = hero->GetID();
            const uint64_t score = heroMasteryScore( heroId );
            const size_t tier = heroMasteryTierIndex( score );
            const HeroChronicle chronicle = heroChronicleFor( heroId );

            message += "\n- " + hero->GetName() + ": " + heroMasteryTitle( focusHall, focusRanks, tier );
            message += " [" + heroAccoladeText( heroId ) + "]";
            message += "\n  Momentum: " + heroStreakText( heroId );
            message += "\n  " + formatNumber( chronicle.battleVictories ) + " battle victories, "
                       + formatNumber( chronicle.castleCaptures ) + " castle captures";
            if ( chronicle.eliteVictories > 0 ) {
                message += ", " + formatNumber( chronicle.eliteVictories ) + " Elite victories";
            }
        }
    }

    if ( heroCount == 0 ) {
        message += "\nNo active heroes in the field.";
    }

    fheroes2::showStandardTextMessage( "RPG Field Ledger", std::move( message ), Dialog::OK );
}
