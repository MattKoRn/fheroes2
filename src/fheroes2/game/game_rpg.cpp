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

            constexpr size_t maxLineLength = 96;
            if ( line.size() > maxLineLength ) {
                line.resize( maxLineLength - 3 );
                line += "...";
            }

            return line;
        }

        std::string compactRpgPopupBody( std::string body )
        {
            // Doctrine inspectors begin with an explanatory paragraph. For right-click
            // inspection cards (Dialog::ZERO), keeping it concise ensures zero vertical
            // overflow while keeping all information intact and unabbreviated.
            const size_t detailsEnd = body.find( "\n\n" );
            if ( detailsEnd != std::string::npos ) {
                const size_t firstPeriod = body.find( '.' );
                if ( firstPeriod != std::string::npos && firstPeriod < detailsEnd ) {
                    body.erase( firstPeriod + 1, detailsEnd - firstPeriod - 1 );
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
                // and consumes much less vertical space without overflow.
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

        return showStandardTextMessage( std::move( headerText ), std::move( messageBody ), buttons, elements );
    }
}

// Intercept only calls originating from the RPG implementation in this translation unit.
// The real shared dialog API is not modified.
#define showStandardTextMessage rpgShowStandardTextMessage
#include "game_rpg_impl.inc"
#undef showStandardTextMessage

namespace
{
    constexpr std::array<const char *, 8> heroRoleCallings{
        "Vanguard", "Reaver", "Strategist", "Sentinel", "Arcanist", "Warden", "Marshal", "Paragon"
    };

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
}

std::string fheroes2::RPG::heroLegacyText( const int32_t heroId )
{
    return heroLegacyProgressText( heroRenown( heroId ) );
}

std::string fheroes2::RPG::heroChronicleText( const int32_t heroId )
{
    const auto entry = heroChronicleLedger.find( heroId );
    if ( entry == heroChronicleLedger.end() ) {
        return heroChronicleSummary( HeroChronicle{} );
    }

    return heroChronicleSummary( entry->second );
}

std::string fheroes2::RPG::heroRoleText( const PlayerColor color, const int32_t heroId )
{
    const auto chronicleEntry = heroChronicleLedger.find( heroId );
    const HeroChronicle emptyChronicle{};
    const HeroChronicle & chronicle = chronicleEntry == heroChronicleLedger.end() ? emptyChronicle : chronicleEntry->second;

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
