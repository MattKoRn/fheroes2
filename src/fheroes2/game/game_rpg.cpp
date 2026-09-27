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

        return showStandardTextMessage( std::move( headerText ), std::move( messageBody ), buttons, elements );
    }
}

// Intercept only calls originating from the RPG implementation in this translation unit.
// The real shared dialog API is not modified.
#define showStandardTextMessage rpgShowStandardTextMessage
#include "game_rpg_impl.inc"
#undef showStandardTextMessage
