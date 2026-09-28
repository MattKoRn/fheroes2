// RPG-only wrapper around the existing implementation.
//
// Keep right-click/hold help popups compact before handing them to the standard
// fheroes2 dialog renderer. This avoids tall inspector dialogs overflowing at
// smaller resolutions while leaving every non-RPG dialog untouched.

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
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
#define showStandardTextMessage rpgShowStandardTextMessage
#define awardBattle rpgAwardBattleImpl
#define awardTownCapture rpgAwardTownCaptureImpl
#include "game_rpg_impl.inc"
#undef awardTownCapture
#undef awardBattle
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
    void awardBattle( const PlayerColor color, const PlayerColor opponent, const uint32_t battleExperience, const bool won, const bool defending,
                      const bool siege, const int32_t heroId )
    {
        const uint64_t masteryBefore = heroMasteryScore( heroId );
        rpgAwardBattleImpl( color, opponent, battleExperience, won, defending, siege, heroId );
        const uint64_t milestoneReward = heroMasteryMilestoneReward( masteryBefore, heroMasteryScore( heroId ) );
        if ( milestoneReward > 0 ) {
            static_cast<void>( addExperience( color, milestoneReward, ExperienceKind::HERO ) );
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