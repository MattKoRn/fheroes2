/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2026                                             *
 *                                                                         *
 *   Free Heroes2 Engine: http://sourceforge.net/projects/fheroes2         *
 *   Copyright (C) 2009 by Andrey Afletdinov <fheroes2@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "game.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ai_planner.h"
#include "army.h"
#include "audio.h"
#include "audio_manager.h"
#include "battle_only.h"
#include "castle.h"
#include "color.h"
#include "cursor.h"
#include "dialog.h"
#include "direction.h"
#include "game_assets.h"
#include "game_auto_playtest.h"
#include "game_delays.h"
#include "game_exit.h"
#include "game_hotkeys.h"
#include "game_interface.h" // IWYU pragma: associated
#include "game_io.h"
#include "game_mode.h"
#include "game_rpg.h"
#include "game_over.h"
#include "heroes.h"
#include "icn.h"
#include "image.h"
#include "interface_base.h"
#include "interface_buttons.h"
#include "interface_cpanel.h"
#include "interface_gamearea.h"
#include "interface_icons.h"
#include "interface_radar.h"
#include "interface_status.h"
#include "kingdom.h"
#include "localevent.h"
#include "logging.h"
#include "m82.h"
#include "maps.h"
#include "maps_fileinfo.h"
#include "maps_tiles.h"
#include "maps_tiles_helper.h"
#include "math_base.h"
#include "monster.h"
#include "mp2.h"
#include "mus.h"
#include "players.h"
#include "resource.h"
#include "resource_trading.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "tools.h"
#include "translations.h"
#include "ui_constants.h"
#include "ui_dialog.h"
#include "ui_language.h"
#include "ui_monster.h"
#include "ui_text.h"
#include "ui_tool.h"
#include "week.h"
#include "world.h"

namespace
{
    constexpr int64_t offlineSecondsPerDay{ 24 * 60 * 60 };
    constexpr char offlineProgressFileName[]{ "offline_progress.dat" };

    PlayerColor offlineProgressActivePlayerColor{ PlayerColor::NONE };
    int64_t offlineSuspendStartedUnix{ 0 };
    int64_t offlineResumeElapsedSeconds{ 0 };
    bool offlineResumePending{ false };

    int64_t getLocalCalendarDayIndex( const int64_t unixTime )
    {
        if ( unixTime <= 0 ) {
            return 0;
        }

        const tm localTime = System::GetTM( static_cast<time_t>( unixTime ) );
        int64_t year = static_cast<int64_t>( localTime.tm_year ) + 1900;
        const int64_t month = static_cast<int64_t>( localTime.tm_mon ) + 1;
        const int64_t day = localTime.tm_mday;

        // Convert a Gregorian civil date to a monotonic day index. Unlike unixTime / 86400,
        // this follows the player's local calendar across time zones and daylight-saving changes.
        year -= month <= 2;
        const int64_t era = ( year >= 0 ? year : year - 399 ) / 400;
        const int64_t yearOfEra = year - era * 400;
        const int64_t dayOfYear = ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
        const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        return era * 146097 + dayOfEra;
    }

    using FundsMember = int32_t Funds::*;

    constexpr std::array<FundsMember, 7> offlineFundMembers{ &Funds::wood, &Funds::mercury, &Funds::ore, &Funds::sulfur,
                                                             &Funds::crystal, &Funds::gems, &Funds::gold };

    constexpr std::array<int, 7> offlineResourceTypes{ Resource::WOOD, Resource::MERCURY, Resource::ORE, Resource::SULFUR,
                                                       Resource::CRYSTAL, Resource::GEMS, Resource::GOLD };

    struct OfflineProgressData
    {
        int64_t lastSeenUnix{ 0 };
        Funds resources;
        Funds dailyIncome;
        std::array<int64_t, 7> carry{};
        uint32_t homecomingStreak{ 0 };
        int64_t lastHomecomingStreakUnix{ 0 };
        int64_t lastHomecomingStreakDay{ 0 };
        int64_t pendingResumeSeconds{ 0 };
        uint64_t totalOfflineSeconds{ 0 };
        uint64_t offlineRenown{ 0 };
        int contractId{ -1 };
        uint64_t contractProgress{ 0 };
        uint64_t contractTarget{ 0 };
        uint32_t contractsCompleted{ 0 };
        uint32_t treasureFragments{ 0 };
        uint32_t treasureMapsCompleted{ 0 };
        uint32_t stateCastles{ 0 };
        uint32_t stateTowns{ 0 };
        uint32_t stateHeroes{ 0 };
        uint32_t stateMines{ 0 };
        uint32_t stateArtifacts{ 0 };
        uint32_t stateEfficiencyPercent{ 100 };
        uint32_t supplyRushMeter{ 0 };
    };

    struct OfflineEvent
    {
        int eventId{ -1 };
        int resource{ Resource::UNKNOWN };
        int32_t bonus{ 0 };
    };

    struct OfflineProgressSummary
    {
        int64_t elapsedSeconds{ 0 };
        uint64_t xpEarned{ 0 };
        Funds productionRewards;
        Funds bonusRewards;
        Funds rewards;
        int homecomingTier{ 0 };
        uint32_t homecomingStreak{ 0 };
        uint64_t totalOfflineSeconds{ 0 };
        std::array<OfflineEvent, 3> events{};
        size_t eventCount{ 0 };
        int milestonePercent{ 0 };
        int milestoneResource{ Resource::UNKNOWN };
        int32_t milestoneBonus{ 0 };
        bool rareDiscovery{ false };
        int rareDiscoveryId{ -1 };
        int rareDiscoveryResource{ Resource::UNKNOWN };
        int32_t rareDiscoveryBonus{ 0 };
        uint64_t renownEarned{ 0 };
        uint64_t renownTotal{ 0 };
        int rankBefore{ 0 };
        int rankAfter{ 0 };
        uint32_t ranksEarned{ 0 };
        Funds rankRewards;
        int contractId{ -1 };
        uint64_t contractProgressBefore{ 0 };
        uint64_t contractProgressAfter{ 0 };
        uint64_t contractTarget{ 0 };
        bool contractCompleted{ false };
        uint32_t contractsCompletedThisSession{ 0 };
        Funds contractRewards;
        uint32_t contractsCompleted{ 0 };
        int nextContractId{ -1 };
        uint64_t nextContractProgress{ 0 };
        uint64_t nextContractTarget{ 0 };
        uint32_t treasureFragmentsBefore{ 0 };
        uint32_t treasureFragmentsEarned{ 0 };
        uint32_t treasureFragmentsAfter{ 0 };
        uint32_t treasureMapsCompleted{ 0 };
        bool treasureMapCompleted{ false };
        uint32_t treasureMapsCompletedThisSession{ 0 };
        int treasureMapId{ -1 };
        Funds treasureRewards;
        uint32_t stateCastles{ 0 };
        uint32_t stateTowns{ 0 };
        uint32_t stateHeroes{ 0 };
        uint32_t stateMines{ 0 };
        uint32_t stateArtifacts{ 0 };
        uint32_t stateEfficiencyPercent{ 100 };
        uint32_t supplyRushBefore{ 0 };
        uint32_t supplyRushEarned{ 0 };
        uint32_t supplyRushAfter{ 0 };
        bool supplyRushTriggered{ false };
        uint32_t supplyRushesTriggered{ 0 };
        Funds supplyRushRewards;
        bool showPopup{ false };
    };

    int64_t getCurrentUnixTime()
    {
        return std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() ).count();
    }

    std::string getOfflineProgressFilePath()
    {
        return System::concatPath( fheroes2::RPG::dataDirectory(), offlineProgressFileName );
    }

    int32_t clampResourceValue( const int64_t value )
    {
        return static_cast<int32_t>( std::clamp<int64_t>( value, 0, std::numeric_limits<int32_t>::max() ) );
    }

    bool loadOfflineProgressData( OfflineProgressData & data )
    {
        const auto loadFromPath = []( const std::string & candidatePath, OfflineProgressData & candidate ) {
            std::ifstream input( candidatePath );
            if ( !input ) {
                return false;
            }

        int version = 0;
        bool hasTimestamp = false;
        bool hasResources = false;
        bool hasIncome = false;
        bool hasCarry = false;
        bool hasStreak = false;
        bool hasLastHomecomingStreakUnix = false;
        bool hasLastHomecomingStreakDay = false;
        bool hasPendingResumeSeconds = false;
        bool hasTotalOfflineSeconds = false;
        bool hasOfflineRenown = false;
        bool hasContractId = false;
        bool hasContractProgress = false;
        bool hasContractTarget = false;
        bool hasContractsCompleted = false;
        bool hasTreasureFragments = false;
        bool hasTreasureMapsCompleted = false;
        bool hasStateCastles = false;
        bool hasStateTowns = false;
        bool hasStateHeroes = false;
        bool hasStateMines = false;
        bool hasStateArtifacts = false;
        bool hasStateEfficiency = false;
        bool hasSupplyRushMeter = false;
        bool hasCreatureRoster = false;
        bool hasCreatureReserve = false;
        bool hasCreatureRecruitCarry = false;

        const auto readFunds = [&input]( Funds & funds ) {
            for ( const FundsMember member : offlineFundMembers ) {
                int64_t value = 0;
                if ( !( input >> value ) ) {
                    return false;
                }

                funds.*member = clampResourceValue( value );
            }

            return true;
        };

        std::string key;
        while ( input >> key ) {
            if ( key == "version" ) {
                input >> version;
            }
            else if ( key == "last_seen_unix" ) {
                input >> candidate.lastSeenUnix;
                hasTimestamp = true;
            }
            else if ( key == "resources" ) {
                hasResources = readFunds( candidate.resources );
            }
            else if ( key == "daily_income" ) {
                hasIncome = readFunds( candidate.dailyIncome );
            }
            else if ( key == "carry" ) {
                hasCarry = true;
                for ( int64_t & value : candidate.carry ) {
                    if ( !( input >> value ) ) {
                        return false;
                    }

                    value = std::clamp<int64_t>( value, 0, offlineSecondsPerDay - 1 );
                }
            }
            else if ( key == "homecoming_streak" ) {
                input >> candidate.homecomingStreak;
                hasStreak = true;
            }
            else if ( key == "last_homecoming_streak_unix" ) {
                input >> candidate.lastHomecomingStreakUnix;
                hasLastHomecomingStreakUnix = true;
            }
            else if ( key == "last_homecoming_streak_day" ) {
                input >> candidate.lastHomecomingStreakDay;
                hasLastHomecomingStreakDay = true;
            }
            else if ( key == "pending_resume_seconds" ) {
                input >> candidate.pendingResumeSeconds;
                hasPendingResumeSeconds = true;
            }
            else if ( key == "total_offline_seconds" ) {
                input >> candidate.totalOfflineSeconds;
                hasTotalOfflineSeconds = true;
            }
            else if ( key == "offline_renown" ) {
                input >> candidate.offlineRenown;
                hasOfflineRenown = true;
            }
            else if ( key == "contract_id" ) {
                input >> candidate.contractId;
                hasContractId = true;
            }
            else if ( key == "contract_progress" ) {
                input >> candidate.contractProgress;
                hasContractProgress = true;
            }
            else if ( key == "contract_target" ) {
                input >> candidate.contractTarget;
                hasContractTarget = true;
            }
            else if ( key == "contracts_completed" ) {
                input >> candidate.contractsCompleted;
                hasContractsCompleted = true;
            }
            else if ( key == "treasure_fragments" ) {
                input >> candidate.treasureFragments;
                hasTreasureFragments = true;
            }
            else if ( key == "treasure_maps_completed" ) {
                input >> candidate.treasureMapsCompleted;
                hasTreasureMapsCompleted = true;
            }
            else if ( key == "state_castles" ) {
                input >> candidate.stateCastles;
                hasStateCastles = true;
            }
            else if ( key == "state_towns" ) {
                input >> candidate.stateTowns;
                hasStateTowns = true;
            }
            else if ( key == "state_heroes" ) {
                input >> candidate.stateHeroes;
                hasStateHeroes = true;
            }
            else if ( key == "state_mines" ) {
                input >> candidate.stateMines;
                hasStateMines = true;
            }
            else if ( key == "state_artifacts" ) {
                input >> candidate.stateArtifacts;
                hasStateArtifacts = true;
            }
            else if ( key == "state_efficiency_percent" ) {
                input >> candidate.stateEfficiencyPercent;
                candidate.stateEfficiencyPercent = std::clamp<uint32_t>( candidate.stateEfficiencyPercent, 100, 150 );
                hasStateEfficiency = true;
            }
            else if ( key == "supply_rush_meter" ) {
                input >> candidate.supplyRushMeter;
                candidate.supplyRushMeter = std::min<uint32_t>( candidate.supplyRushMeter, 99 );
                hasSupplyRushMeter = true;
            }
            else if ( key == "creature_roster" ) {
                hasCreatureRoster = true;
                std::string ignoredLine;
                std::getline( input, ignoredLine );
            }
            else if ( key == "creature_reserve" ) {
                hasCreatureReserve = true;
                std::string ignoredLine;
                std::getline( input, ignoredLine );
            }
            else if ( key == "creature_recruit_carry" ) {
                hasCreatureRecruitCarry = true;
                std::string ignoredLine;
                std::getline( input, ignoredLine );
            }
            else {
                std::string ignoredLine;
                std::getline( input, ignoredLine );
            }

            if ( !input ) {
                return false;
            }
        }

        const bool hasVersionSpecificFields
            = version == 1 || ( version == 2 && hasStreak && hasTotalOfflineSeconds )
              || ( version == 3 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown )
              || ( version == 4 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted )
              || ( version == 5 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted )
              || ( version == 6 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns && hasStateHeroes
                   && hasStateMines && hasStateArtifacts && hasStateEfficiency )
              || ( version == 7 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns && hasStateHeroes
                   && hasStateMines && hasStateArtifacts && hasStateEfficiency && hasSupplyRushMeter )
              || ( version == 8 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns && hasStateHeroes
                   && hasStateMines && hasStateArtifacts && hasStateEfficiency && hasSupplyRushMeter && hasCreatureRoster && hasCreatureReserve )
              || ( version == 9 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns && hasStateHeroes
                   && hasStateMines && hasStateArtifacts && hasStateEfficiency && hasSupplyRushMeter && hasCreatureRoster && hasCreatureReserve
                   && hasCreatureRecruitCarry )
              || ( version == 10 && hasStreak && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns && hasStateHeroes
                   && hasStateMines && hasStateArtifacts && hasStateEfficiency && hasSupplyRushMeter )
              || ( version == 11 && hasStreak && hasLastHomecomingStreakUnix && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId
                   && hasContractProgress && hasContractTarget && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted
                   && hasStateCastles && hasStateTowns && hasStateHeroes && hasStateMines && hasStateArtifacts && hasStateEfficiency
                   && hasSupplyRushMeter )
              || ( version == 12 && hasStreak && hasLastHomecomingStreakUnix && hasPendingResumeSeconds && hasTotalOfflineSeconds
                   && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget && hasContractsCompleted
                   && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns && hasStateHeroes
                   && hasStateMines && hasStateArtifacts && hasStateEfficiency && hasSupplyRushMeter )
              || ( version == 13 && hasStreak && hasLastHomecomingStreakUnix && hasLastHomecomingStreakDay && hasPendingResumeSeconds
                   && hasTotalOfflineSeconds && hasOfflineRenown && hasContractId && hasContractProgress && hasContractTarget
                   && hasContractsCompleted && hasTreasureFragments && hasTreasureMapsCompleted && hasStateCastles && hasStateTowns
                   && hasStateHeroes && hasStateMines && hasStateArtifacts && hasStateEfficiency && hasSupplyRushMeter );
        if ( !( version >= 1 && version <= 13 ) || !hasVersionSpecificFields || !hasTimestamp || !hasResources || !hasIncome || !hasCarry
             || candidate.lastSeenUnix <= 0 ) {
            return false;
        }

        // Normalize persistent counters before any arithmetic uses them. These limits are
        // structural invariants of the current format, not progression caps.
        candidate.treasureFragments = std::min<uint32_t>( candidate.treasureFragments, 4 );
        candidate.pendingResumeSeconds = std::max<int64_t>( 0, candidate.pendingResumeSeconds );
        if ( version < 11 ) {
            // Version 10 and earlier tracked only the count. Anchor an existing streak to the
            // last valid snapshot so migration preserves it while preventing another same-day increment.
            candidate.lastHomecomingStreakUnix = candidate.homecomingStreak > 0 ? candidate.lastSeenUnix : 0;
        }
        else {
            candidate.lastHomecomingStreakUnix = std::clamp<int64_t>( candidate.lastHomecomingStreakUnix, 0, candidate.lastSeenUnix );
            if ( candidate.homecomingStreak == 0 ) {
                candidate.lastHomecomingStreakUnix = 0;
            }
        }

        if ( candidate.homecomingStreak == 0 ) {
            candidate.lastHomecomingStreakDay = 0;
        }
        else if ( version < 13 || candidate.lastHomecomingStreakDay <= 0 ) {
            // Older formats stored only a timestamp. Convert it once to the local calendar date
            // so future timezone or DST changes cannot reinterpret which streak day was earned.
            candidate.lastHomecomingStreakDay = getLocalCalendarDayIndex( candidate.lastHomecomingStreakUnix );
        }
        candidate.stateCastles = std::min<uint32_t>( candidate.stateCastles, 255 );
        candidate.stateTowns = std::min<uint32_t>( candidate.stateTowns, 255 );
        candidate.stateHeroes = std::min<uint32_t>( candidate.stateHeroes, 255 );
        candidate.stateMines = std::min<uint32_t>( candidate.stateMines, 255 );
        candidate.stateArtifacts = std::min<uint32_t>( candidate.stateArtifacts, 255 );

        constexpr uint64_t maximumContractTarget = 48;
        if ( candidate.contractId < 0 || candidate.contractId >= 5 || candidate.contractTarget == 0
             || candidate.contractTarget > maximumContractTarget ) {
            candidate.contractId = -1;
            candidate.contractProgress = 0;
            candidate.contractTarget = 0;
        }
        return true;

        };

        const std::string filePath = getOfflineProgressFilePath();
        // A crash can leave more than one valid snapshot. Select the newest timestamp rather
        // than trusting a stale primary merely because it still parses. For equal timestamps,
        // prefer backup < primary < temporary: a fully written .tmp is the newest atomic-write
        // candidate, while the backup is deliberately the previous committed state.
        const std::array<std::string, 3> candidatePaths{ filePath + ".bak", filePath, filePath + ".tmp" };

        bool foundSnapshot = false;
        int64_t newestTimestamp = 0;
        for ( const std::string & candidatePath : candidatePaths ) {
            OfflineProgressData candidate;
            if ( loadFromPath( candidatePath, candidate ) && ( !foundSnapshot || candidate.lastSeenUnix >= newestTimestamp ) ) {
                data = std::move( candidate );
                newestTimestamp = data.lastSeenUnix;
                foundSnapshot = true;
            }
        }

        return foundSnapshot;
    }

    bool saveOfflineProgressData( const OfflineProgressData & data )
    {
        const std::string filePath = getOfflineProgressFilePath();
        const std::string tempFilePath = filePath + ".tmp";
        const std::string backupFilePath = filePath + ".bak";

        std::ofstream output( tempFilePath, std::ios::trunc );
        if ( !output ) {
            ERROR_LOG( "Unable to write temporary offline progress data." )
            return false;
        }

        const auto writeFunds = [&output]( const Funds & funds ) {
            for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
                if ( i != 0 ) {
                    output << ' ';
                }
                output << funds.*offlineFundMembers[i];
            }
            output << '\n';
        };

        output << "version 13\n";
        output << "last_seen_unix " << data.lastSeenUnix << '\n';
        output << "resources ";
        writeFunds( data.resources );
        output << "daily_income ";
        writeFunds( data.dailyIncome );
        output << "carry ";
        for ( size_t i = 0; i < data.carry.size(); ++i ) {
            if ( i != 0 ) {
                output << ' ';
            }
            output << data.carry[i];
        }
        output << '\n';
        output << "homecoming_streak " << data.homecomingStreak << '\n';
        output << "last_homecoming_streak_unix " << data.lastHomecomingStreakUnix << '\n';
        output << "last_homecoming_streak_day " << data.lastHomecomingStreakDay << '\n';
        output << "pending_resume_seconds " << data.pendingResumeSeconds << '\n';
        output << "total_offline_seconds " << data.totalOfflineSeconds << '\n';
        output << "offline_renown " << data.offlineRenown << '\n';
        output << "contract_id " << data.contractId << '\n';
        output << "contract_progress " << data.contractProgress << '\n';
        output << "contract_target " << data.contractTarget << '\n';
        output << "contracts_completed " << data.contractsCompleted << '\n';
        output << "treasure_fragments " << data.treasureFragments << '\n';
        output << "treasure_maps_completed " << data.treasureMapsCompleted << '\n';
        output << "state_castles " << data.stateCastles << '\n';
        output << "state_towns " << data.stateTowns << '\n';
        output << "state_heroes " << data.stateHeroes << '\n';
        output << "state_mines " << data.stateMines << '\n';
        output << "state_artifacts " << data.stateArtifacts << '\n';
        output << "state_efficiency_percent " << data.stateEfficiencyPercent << '\n';
        output << "supply_rush_meter " << data.supplyRushMeter << '\n';

        output.flush();
        if ( !output ) {
            ERROR_LOG( "Unable to flush temporary offline progress data." )
            output.close();
            System::Unlink( tempFilePath );
            return false;
        }

        output.close();
        if ( !output ) {
            ERROR_LOG( "Unable to close temporary offline progress data." )
            System::Unlink( tempFilePath );
            return false;
        }

        // Replace the live file only after the temporary file is fully written. Keep one backup
        // during the swap so an interrupted rename cannot destroy the last valid snapshot.
        const bool hadOriginal = System::IsFile( filePath );
        if ( hadOriginal ) {
            System::Unlink( backupFilePath );
            if ( std::rename( filePath.c_str(), backupFilePath.c_str() ) != 0 ) {
                ERROR_LOG( "Unable to back up offline progress data." )
                System::Unlink( tempFilePath );
                return false;
            }
        }

        if ( std::rename( tempFilePath.c_str(), filePath.c_str() ) != 0 ) {
            ERROR_LOG( "Unable to replace offline progress data." )
            if ( hadOriginal ) {
                std::rename( backupFilePath.c_str(), filePath.c_str() );
            }
            System::Unlink( tempFilePath );
            return false;
        }

        // Keep the previous valid offline snapshot at backupFilePath as a resilient fallback in case of corruption or crash.
        return true;
    }

    PlayerColor getPersistentResourcePlayerColor( Settings & conf )
    {
        // Offline progression belongs to one deterministic local-human kingdom. Do not prefer
        // the current turn's player: in Hot Seat that would make offline progress jump
        // between colors depending on whose turn a save was made on.
        for ( Player * player : conf.GetPlayers().getVector() ) {
            if ( player != nullptr && player->isPlay() && ( player->isControlHuman() || player->isAIAutoControlMode() ) ) {
                return player->GetColor();
            }
        }

        return PlayerColor::NONE;
    }

    uint32_t getOfflineMineCount( const Kingdom & kingdom )
    {
        uint32_t mineCount = 0;
        for ( const int resourceType : offlineResourceTypes ) {
            const uint32_t resourceMineCount = world.CountCapturedMines( resourceType, kingdom.GetColor() );
            mineCount = resourceMineCount > std::numeric_limits<uint32_t>::max() - mineCount ? std::numeric_limits<uint32_t>::max()
                                                                                              : mineCount + resourceMineCount;
        }

        return mineCount;
    }

    uint32_t getOfflineStateEfficiencyPercent( const uint32_t castles, const uint32_t towns, const uint32_t heroes, const uint32_t mines,
                                               const uint32_t artifacts )
    {
        const uint32_t castleBonus = std::min<uint32_t>( 4, castles ) * 4;
        const uint32_t townBonus = std::min<uint32_t>( 4, towns ) * 2;
        const uint32_t heroBonus = std::min<uint32_t>( 6, heroes ) * 2;
        const uint32_t mineBonus = std::min<uint32_t>( 12, mines );
        const uint32_t artifactBonus = std::min<uint32_t>( 10, artifacts ) / 5;

        return std::min<uint32_t>( 150, 100 + castleBonus + townBonus + heroBonus + mineBonus + artifactBonus );
    }

    void captureOfflineKingdomState( OfflineProgressData & data, const Kingdom & kingdom )
    {
        data.dailyIncome = kingdom.GetIncome();
        data.stateCastles = kingdom.GetCountCastle();
        data.stateTowns = kingdom.GetCountTown();
        data.stateHeroes = static_cast<uint32_t>( kingdom.GetHeroes().size() );
        data.stateMines = getOfflineMineCount( kingdom );
        data.stateArtifacts = kingdom.GetCountArtifacts();
        data.stateEfficiencyPercent
            = getOfflineStateEfficiencyPercent( data.stateCastles, data.stateTowns, data.stateHeroes, data.stateMines, data.stateArtifacts );
    }

    void persistOfflineProgressSnapshot( Kingdom & kingdom, const int64_t snapshotUnix = 0 )
    {
        OfflineProgressData data;
        const bool hasSavedData = loadOfflineProgressData( data );
        const int64_t now = snapshotUnix > 0 ? snapshotUnix : getCurrentUnixTime();

        const bool hasPendingResume = offlineResumePending && offlineResumeElapsedSeconds > 0;
        if ( hasPendingResume ) {
            data.pendingResumeSeconds
                = offlineResumeElapsedSeconds > std::numeric_limits<int64_t>::max() - data.pendingResumeSeconds
                      ? std::numeric_limits<int64_t>::max()
                      : data.pendingResumeSeconds + offlineResumeElapsedSeconds;
        }

        // Persist the pending resume interval and the advanced offline clock in one atomic
        // snapshot. If the write fails, keep the interval in memory and do not acknowledge it.
        data.lastSeenUnix = hasSavedData ? std::max( data.lastSeenUnix, now ) : now;
        data.resources = kingdom.GetFunds();
        captureOfflineKingdomState( data, kingdom );

        if ( saveOfflineProgressData( data ) && hasPendingResume ) {
            offlineResumeElapsedSeconds = 0;
            offlineResumePending = false;
        }
    }

    int getHomecomingTier( const int64_t elapsedSeconds )
    {
        if ( elapsedSeconds < 30 * 60 ) {
            return 0;
        }
        if ( elapsedSeconds < 6 * 60 * 60 ) {
            return 1;
        }
        if ( elapsedSeconds < offlineSecondsPerDay ) {
            return 2;
        }
        if ( elapsedSeconds < 7 * offlineSecondsPerDay ) {
            return 3;
        }
        if ( elapsedSeconds < 30 * offlineSecondsPerDay ) {
            return 4;
        }

        return 5;
    }

    int getHomecomingBonusPercent( const int tier )
    {
        constexpr std::array<int, 6> bonusPercent{ 0, 5, 10, 15, 20, 25 };
        assert( tier >= 0 && static_cast<size_t>( tier ) < bonusPercent.size() );

        return bonusPercent[tier];
    }

    uint64_t getOfflineEventSeed( const int64_t lastSeenUnix, const int64_t elapsedSeconds )
    {
        uint64_t seed = static_cast<uint64_t>( lastSeenUnix ) ^ ( static_cast<uint64_t>( elapsedSeconds ) + 0x9E3779B97F4A7C15ULL );
        seed ^= seed >> 30;
        seed *= 0xBF58476D1CE4E5B9ULL;
        seed ^= seed >> 27;
        seed *= 0x94D049BB133111EBULL;
        seed ^= seed >> 31;

        return seed;
    }

    void applyOfflineHomecomingBonus( OfflineProgressSummary & summary, OfflineProgressData & data, const int64_t previousLastSeenUnix )
    {
        summary.homecomingTier = getHomecomingTier( summary.elapsedSeconds );
        const int bonusPercent = getHomecomingBonusPercent( summary.homecomingTier );
        if ( bonusPercent == 0 ) {
            return;
        }

        std::array<size_t, 7> eligibleResourceIndices{};
        size_t eligibleResourceCount = 0;

        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            if ( summary.productionRewards.*offlineFundMembers[i] > 0 ) {
                eligibleResourceIndices[eligibleResourceCount] = i;
                ++eligibleResourceCount;
            }
        }

        if ( eligibleResourceCount == 0 ) {
            return;
        }

        const size_t desiredEventCount = summary.homecomingTier >= 4 ? 3 : ( summary.homecomingTier >= 3 ? 2 : 1 );
        const size_t eventCount = std::min( desiredEventCount, eligibleResourceCount );
        const uint64_t seed = getOfflineEventSeed( previousLastSeenUnix, summary.elapsedSeconds );

        for ( size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex ) {
            const uint64_t eventSeed = seed + 0x9E3779B97F4A7C15ULL * ( eventIndex + 1 );
            const size_t choice = static_cast<size_t>( eventSeed % eligibleResourceCount );
            const size_t selectedIndex = eligibleResourceIndices[choice];

            eligibleResourceIndices[choice] = eligibleResourceIndices[eligibleResourceCount - 1];
            --eligibleResourceCount;

            const FundsMember selectedMember = offlineFundMembers[selectedIndex];
            const int eventPercent = bonusPercent / static_cast<int>( eventCount )
                                     + ( eventIndex < static_cast<size_t>( bonusPercent % static_cast<int>( eventCount ) ) ? 1 : 0 );

            const int64_t baseReward = summary.productionRewards.*selectedMember;
            const int64_t currentResource = data.resources.*selectedMember;
            const int64_t capacity = std::numeric_limits<int32_t>::max() - currentResource;
            const int64_t calculatedBonus = std::max<int64_t>( 1, ( baseReward * eventPercent ) / 100 );
            const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );

            if ( grantedBonus <= 0 ) {
                continue;
            }

            OfflineEvent & event = summary.events[summary.eventCount];
            event.eventId = static_cast<int>( ( eventSeed >> 8 ) % 8 );
            event.resource = offlineResourceTypes[selectedIndex];
            event.bonus = static_cast<int32_t>( grantedBonus );
            ++summary.eventCount;

            summary.bonusRewards.*selectedMember += event.bonus;
            summary.rewards.*selectedMember += event.bonus;
            data.resources.*selectedMember += event.bonus;
        }
    }

    int getHomecomingMilestonePercent( const uint32_t streak )
    {
        if ( streak > 0 && streak % 10 == 0 ) {
            return 50;
        }
        if ( streak > 0 && streak % 5 == 0 ) {
            return 25;
        }
        if ( streak > 0 && streak % 3 == 0 ) {
            return 15;
        }

        return 0;
    }

    void applyOfflineStreakProgress( OfflineProgressSummary & summary, OfflineProgressData & data, const int64_t returnUnix )
    {
        const uint64_t elapsedSeconds = summary.elapsedSeconds > 0 ? static_cast<uint64_t>( summary.elapsedSeconds ) : 0;
        if ( std::numeric_limits<uint64_t>::max() - data.totalOfflineSeconds < elapsedSeconds ) {
            data.totalOfflineSeconds = std::numeric_limits<uint64_t>::max();
        }
        else {
            data.totalOfflineSeconds += elapsedSeconds;
        }
        summary.totalOfflineSeconds = data.totalOfflineSeconds;

        if ( summary.elapsedSeconds < 6 * 60 * 60 || summary.productionRewards.GetValidItemsCount() == 0 || returnUnix <= 0 ) {
            summary.homecomingStreak = data.homecomingStreak;
            return;
        }

        const int64_t returnDay = getLocalCalendarDayIndex( returnUnix );
        if ( data.lastHomecomingStreakUnix > 0 && data.lastHomecomingStreakDay > 0 ) {
            const int64_t previousStreakDay = data.lastHomecomingStreakDay;
            if ( returnDay <= previousStreakDay ) {
                // Same-local-day returns (and temporary clock rollbacks) must not farm the daily streak.
                summary.homecomingStreak = data.homecomingStreak;
                return;
            }

            if ( returnDay == previousStreakDay + 1 ) {
                if ( data.homecomingStreak < std::numeric_limits<uint32_t>::max() ) {
                    ++data.homecomingStreak;
                }
            }
            else {
                // Missing one or more real-world days breaks the consecutive homecoming streak.
                data.homecomingStreak = 1;
            }
        }
        else {
            data.homecomingStreak = 1;
        }

        data.lastHomecomingStreakUnix = returnUnix;
        data.lastHomecomingStreakDay = returnDay;
        summary.homecomingStreak = data.homecomingStreak;
        summary.milestonePercent = getHomecomingMilestonePercent( data.homecomingStreak );

        if ( summary.milestonePercent == 0 ) {
            return;
        }

        size_t bestIndex = offlineFundMembers.size();
        int32_t bestProduction = 0;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const int32_t production = summary.productionRewards.*offlineFundMembers[i];
            if ( production > bestProduction ) {
                bestProduction = production;
                bestIndex = i;
            }
        }

        if ( bestIndex == offlineFundMembers.size() ) {
            return;
        }

        const FundsMember member = offlineFundMembers[bestIndex];
        const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
        const int64_t calculatedBonus = std::max<int64_t>( 1, ( static_cast<int64_t>( bestProduction ) * summary.milestonePercent ) / 100 );
        const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );

        if ( grantedBonus <= 0 ) {
            return;
        }

        summary.milestoneResource = offlineResourceTypes[bestIndex];
        summary.milestoneBonus = static_cast<int32_t>( grantedBonus );
        summary.bonusRewards.*member += summary.milestoneBonus;
        summary.rewards.*member += summary.milestoneBonus;
        data.resources.*member += summary.milestoneBonus;
    }

    int getOfflineRank( const uint64_t renown )
    {
        constexpr std::array<uint64_t, 7> thresholds{ 0, 25, 75, 150, 300, 600, 1200 };

        int rank = 0;
        for ( size_t i = 1; i < thresholds.size(); ++i ) {
            if ( renown < thresholds[i] ) {
                break;
            }
            rank = static_cast<int>( i );
        }

        return rank;
    }

    uint64_t getOfflineRankThreshold( const int rank )
    {
        constexpr std::array<uint64_t, 7> thresholds{ 0, 25, 75, 150, 300, 600, 1200 };
        assert( rank >= 0 && static_cast<size_t>( rank ) < thresholds.size() );

        return thresholds[rank];
    }

    std::string getOfflineRankName( const int rank )
    {
        switch ( rank ) {
        case 0:
            return _( "Camp Steward" );
        case 1:
            return _( "Road Warden" );
        case 2:
            return _( "Caravan Master" );
        case 3:
            return _( "Royal Quartermaster" );
        case 4:
            return _( "Keeper of the Coffers" );
        case 5:
            return _( "High Steward" );
        case 6:
            return _( "Legend of the Realm" );
        default:
            return _( "Camp Steward" );
        }
    }

    void applyOfflineRareDiscovery( OfflineProgressSummary & summary, OfflineProgressData & data, const int64_t previousLastSeenUnix )
    {
        if ( summary.elapsedSeconds < offlineSecondsPerDay || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return;
        }

        const uint64_t seed = getOfflineEventSeed( previousLastSeenUnix, summary.elapsedSeconds );
        const bool guaranteedDiscovery = summary.elapsedSeconds >= 30 * offlineSecondsPerDay;
        if ( !guaranteedDiscovery && ( ( seed >> 32 ) % 4 ) != 0 ) {
            return;
        }

        std::array<size_t, 7> eligibleIndices{};
        size_t eligibleCount = 0;

        constexpr std::array<size_t, 4> rareResourceIndices{ 1, 3, 4, 5 };
        for ( const size_t index : rareResourceIndices ) {
            if ( summary.productionRewards.*offlineFundMembers[index] > 0 ) {
                eligibleIndices[eligibleCount] = index;
                ++eligibleCount;
            }
        }

        if ( eligibleCount == 0 ) {
            for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
                if ( summary.productionRewards.*offlineFundMembers[i] > 0 ) {
                    eligibleIndices[eligibleCount] = i;
                    ++eligibleCount;
                }
            }
        }

        if ( eligibleCount == 0 ) {
            return;
        }

        const size_t selectedIndex = eligibleIndices[( seed >> 40 ) % eligibleCount];
        const FundsMember member = offlineFundMembers[selectedIndex];
        const int discoveryPercent = guaranteedDiscovery ? 35 : 20;
        const int64_t baseReward = summary.productionRewards.*member;
        const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
        const int64_t calculatedBonus = std::max<int64_t>( 1, ( baseReward * discoveryPercent ) / 100 );
        const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );

        if ( grantedBonus <= 0 ) {
            return;
        }

        summary.rareDiscovery = true;
        summary.rareDiscoveryId = static_cast<int>( ( seed >> 48 ) % 4 );
        summary.rareDiscoveryResource = offlineResourceTypes[selectedIndex];
        summary.rareDiscoveryBonus = static_cast<int32_t>( grantedBonus );
        summary.bonusRewards.*member += summary.rareDiscoveryBonus;
        summary.rewards.*member += summary.rareDiscoveryBonus;
        data.resources.*member += summary.rareDiscoveryBonus;
    }

    int getOfflineContractId( const uint32_t contractsCompleted )
    {
        return static_cast<int>( contractsCompleted % 5 );
    }

    uint64_t getOfflineContractTarget( const int contractId, const uint32_t contractsCompleted )
    {
        constexpr std::array<uint64_t, 5> baseTargets{ 12, 18, 20, 16, 24 };
        assert( contractId >= 0 && static_cast<size_t>( contractId ) < baseTargets.size() );

        return baseTargets[contractId] + std::min<uint64_t>( 24, static_cast<uint64_t>( contractsCompleted ) * 2 );
    }

    void initializeOfflineContract( OfflineProgressData & data )
    {
        if ( data.contractId >= 0 && data.contractId < 5 && data.contractTarget > 0 ) {
            return;
        }

        data.contractId = getOfflineContractId( data.contractsCompleted );
        data.contractProgress = 0;
        data.contractTarget = getOfflineContractTarget( data.contractId, data.contractsCompleted );
    }

    uint64_t getOfflineContractSessionProgress( const int contractId, const OfflineProgressSummary & summary )
    {
        if ( summary.elapsedSeconds < 30 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return 0;
        }

        const uint64_t elapsedHours = std::max<uint64_t>( 1, ( static_cast<uint64_t>( summary.elapsedSeconds ) + 60 * 60 - 1 ) / ( 60 * 60 ) );
        const uint64_t resourceTypes = summary.productionRewards.GetValidItemsCount();

        switch ( contractId ) {
        case 0:
            return elapsedHours;
        case 1:
            return resourceTypes * 3 + static_cast<uint64_t>( summary.homecomingTier );
        case 2:
            return static_cast<uint64_t>( summary.homecomingTier * 3 ) + static_cast<uint64_t>( summary.eventCount * 2 )
                   + ( summary.rareDiscovery ? 6 : 0 );
        case 3: {
            uint64_t rareResourceTypes = 0;
            constexpr std::array<size_t, 4> rareIndices{ 1, 3, 4, 5 };
            for ( const size_t index : rareIndices ) {
                if ( summary.productionRewards.*offlineFundMembers[index] > 0 ) {
                    ++rareResourceTypes;
                }
            }
            return rareResourceTypes * 3 + ( summary.elapsedSeconds >= offlineSecondsPerDay ? 6 : 0 ) + ( summary.rareDiscovery ? 8 : 0 );
        }
        case 4:
            return std::max<uint64_t>( 1, elapsedHours / 2 ) + resourceTypes * 2 + static_cast<uint64_t>( summary.eventCount * 2 );
        default:
            return 0;
        }
    }

    void applyOfflineContractProgress( OfflineProgressSummary & summary, OfflineProgressData & data )
    {
        constexpr uint32_t maximumContractCompletionsPerSession = 3;

        initializeOfflineContract( data );

        summary.contractId = data.contractId;
        summary.contractProgressBefore = data.contractProgress;
        summary.contractTarget = data.contractTarget;
        summary.contractsCompleted = data.contractsCompleted;

        const uint64_t gainedProgress = getOfflineContractSessionProgress( data.contractId, summary );
        if ( gainedProgress > 0 ) {
            if ( std::numeric_limits<uint64_t>::max() - data.contractProgress < gainedProgress ) {
                data.contractProgress = std::numeric_limits<uint64_t>::max();
            }
            else {
                data.contractProgress += gainedProgress;
            }
        }

        if ( data.contractProgress < data.contractTarget ) {
            summary.contractProgressAfter = data.contractProgress;
            return;
        }

        size_t bestIndex = offlineFundMembers.size();
        int32_t bestProduction = 0;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const int32_t production = summary.productionRewards.*offlineFundMembers[i];
            if ( production > bestProduction ) {
                bestProduction = production;
                bestIndex = i;
            }
        }

        while ( data.contractProgress >= data.contractTarget && summary.contractsCompletedThisSession < maximumContractCompletionsPerSession ) {
            data.contractProgress -= data.contractTarget;
            summary.contractCompleted = true;
            ++summary.contractsCompletedThisSession;

            if ( data.contractsCompleted < std::numeric_limits<uint32_t>::max() ) {
                ++data.contractsCompleted;
            }

            if ( bestIndex != offlineFundMembers.size() ) {
                const int rewardPercent = 20 + static_cast<int>( ( data.contractsCompleted - 1 ) % 4 ) * 5;
                const FundsMember member = offlineFundMembers[bestIndex];
                const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
                const int64_t calculatedBonus = std::max<int64_t>( 1, ( static_cast<int64_t>( bestProduction ) * rewardPercent ) / 100 );
                const int32_t grantedBonus = static_cast<int32_t>( std::min<int64_t>( calculatedBonus, capacity ) );

                if ( grantedBonus > 0 ) {
                    summary.contractRewards.*member += grantedBonus;
                    summary.bonusRewards.*member += grantedBonus;
                    summary.rewards.*member += grantedBonus;
                    data.resources.*member += grantedBonus;
                }
            }

            data.contractId = getOfflineContractId( data.contractsCompleted );
            data.contractTarget = getOfflineContractTarget( data.contractId, data.contractsCompleted );
        }

        summary.contractsCompleted = data.contractsCompleted;
        summary.contractProgressAfter = std::min( data.contractProgress, data.contractTarget );
        summary.nextContractId = data.contractId;
        summary.nextContractProgress = summary.contractProgressAfter;
        summary.nextContractTarget = data.contractTarget;
    }

    int getTreasureMapRewardPercent( const int mapId )
    {
        constexpr std::array<int, 4> rewardPercent{ 35, 40, 45, 50 };
        assert( mapId >= 0 && static_cast<size_t>( mapId ) < rewardPercent.size() );

        return rewardPercent[mapId];
    }

    void applyOfflineTreasureHunt( OfflineProgressSummary & summary, OfflineProgressData & data, const int64_t previousLastSeenUnix )
    {
        constexpr uint32_t maximumMapsPerSession = 2;

        summary.treasureFragmentsBefore = data.treasureFragments;
        summary.treasureFragmentsAfter = data.treasureFragments;
        summary.treasureMapsCompleted = data.treasureMapsCompleted;

        if ( summary.elapsedSeconds < 2 * 60 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return;
        }

        const uint32_t offlineDays
            = static_cast<uint32_t>( std::min<int64_t>( 4, std::max<int64_t>( 0, summary.elapsedSeconds / offlineSecondsPerDay ) ) );
        uint32_t fragmentsEarned = 1 + offlineDays;
        fragmentsEarned += summary.rareDiscovery ? 1 : 0;
        fragmentsEarned += summary.contractsCompletedThisSession;
        fragmentsEarned = std::min<uint32_t>( fragmentsEarned, 8 );

        summary.treasureFragmentsEarned = fragmentsEarned;
        data.treasureFragments += fragmentsEarned;
        summary.treasureFragmentsAfter = data.treasureFragments;

        if ( data.treasureFragments < 5 ) {
            return;
        }

        std::array<size_t, 7> eligibleIndices{};
        size_t eligibleCount = 0;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            if ( summary.productionRewards.*offlineFundMembers[i] > 0 ) {
                eligibleIndices[eligibleCount] = i;
                ++eligibleCount;
            }
        }

        while ( data.treasureFragments >= 5 && summary.treasureMapsCompletedThisSession < maximumMapsPerSession ) {
            data.treasureFragments -= 5;
            summary.treasureMapCompleted = true;
            ++summary.treasureMapsCompletedThisSession;

            const int mapId = static_cast<int>( data.treasureMapsCompleted % 4 );
            if ( summary.treasureMapId < 0 ) {
                summary.treasureMapId = mapId;
            }

            if ( eligibleCount > 0 ) {
                const uint64_t seed = getOfflineEventSeed( previousLastSeenUnix,
                                                            summary.elapsedSeconds + static_cast<int64_t>( data.treasureMapsCompleted + 1 ) * 7919 );
                const size_t selectedIndex = eligibleIndices[( seed >> 24 ) % eligibleCount];
                const FundsMember member = offlineFundMembers[selectedIndex];
                const int rewardPercent = getTreasureMapRewardPercent( mapId );
                const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
                const int64_t baseReward = summary.productionRewards.*member;
                const int64_t calculatedBonus = std::max<int64_t>( 1, ( baseReward * rewardPercent ) / 100 );
                const int32_t grantedBonus = static_cast<int32_t>( std::min<int64_t>( calculatedBonus, capacity ) );

                if ( grantedBonus > 0 ) {
                    summary.treasureRewards.*member += grantedBonus;
                    summary.bonusRewards.*member += grantedBonus;
                    summary.rewards.*member += grantedBonus;
                    data.resources.*member += grantedBonus;
                }
            }

            if ( data.treasureMapsCompleted < std::numeric_limits<uint32_t>::max() ) {
                ++data.treasureMapsCompleted;
            }
        }

        summary.treasureFragmentsAfter = data.treasureFragments;
        summary.treasureMapsCompleted = data.treasureMapsCompleted;
    }

    void applyOfflineSupplyRush( OfflineProgressSummary & summary, OfflineProgressData & data )
    {
        constexpr uint32_t maximumRushesPerSession = 2;

        summary.supplyRushBefore = data.supplyRushMeter;
        summary.supplyRushAfter = data.supplyRushMeter;

        if ( summary.elapsedSeconds < 30 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return;
        }

        const uint64_t elapsedHours = std::max<uint64_t>( 1, static_cast<uint64_t>( summary.elapsedSeconds ) / ( 60 * 60 ) );
        const uint32_t timePoints = static_cast<uint32_t>( std::min<uint64_t>( 20, elapsedHours / 6 ) );
        const uint32_t statePoints = ( summary.stateEfficiencyPercent - 100 ) / 5;
        const uint32_t tierPoints = static_cast<uint32_t>( summary.homecomingTier * 3 );

        const uint32_t achievementPoints = summary.contractsCompletedThisSession * 8 + summary.treasureMapsCompletedThisSession * 10
                                           + ( summary.rareDiscovery ? 10 : 0 ) + ( summary.milestoneBonus > 0 ? 10 : 0 );
        summary.supplyRushEarned = 5 + timePoints + statePoints + tierPoints + achievementPoints;

        const uint64_t accumulated = static_cast<uint64_t>( data.supplyRushMeter ) + summary.supplyRushEarned;
        if ( accumulated < 100 ) {
            data.supplyRushMeter = static_cast<uint32_t>( accumulated );
            summary.supplyRushAfter = data.supplyRushMeter;
            return;
        }

        summary.supplyRushTriggered = true;
        summary.supplyRushesTriggered = std::min<uint32_t>( maximumRushesPerSession, static_cast<uint32_t>( accumulated / 100 ) );
        data.supplyRushMeter = static_cast<uint32_t>( accumulated - static_cast<uint64_t>( summary.supplyRushesTriggered ) * 100 );
        summary.supplyRushAfter = data.supplyRushMeter;

        constexpr int rushBonusPercent = 20;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const FundsMember member = offlineFundMembers[i];
            const int64_t baseReward = summary.productionRewards.*member;
            if ( baseReward <= 0 ) {
                continue;
            }

            const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
            const int64_t calculatedBonus
                = std::max<int64_t>( 1, ( baseReward * rushBonusPercent * summary.supplyRushesTriggered ) / 100 );
            const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );
            if ( grantedBonus <= 0 ) {
                continue;
            }

            const int32_t bonus = static_cast<int32_t>( grantedBonus );
            summary.supplyRushRewards.*member += bonus;
            summary.bonusRewards.*member += bonus;
            summary.rewards.*member += bonus;
            data.resources.*member += bonus;
        }
    }

    void applyOfflineRenownProgress( OfflineProgressSummary & summary, OfflineProgressData & data )
    {
        summary.rankBefore = getOfflineRank( data.offlineRenown );
        summary.rankAfter = summary.rankBefore;
        summary.renownTotal = data.offlineRenown;

        if ( summary.elapsedSeconds < 30 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return;
        }

        const uint64_t elapsedHours = std::max<uint64_t>( 1, static_cast<uint64_t>( summary.elapsedSeconds ) / ( 60 * 60 ) );
        uint64_t earned = elapsedHours + static_cast<uint64_t>( summary.homecomingTier * 2 ) + static_cast<uint64_t>( summary.eventCount * 2 );

        if ( summary.milestoneBonus > 0 ) {
            earned += 5;
        }
        if ( summary.rareDiscovery ) {
            earned += 10;
        }
        if ( summary.contractCompleted ) {
            earned += static_cast<uint64_t>( summary.contractsCompletedThisSession ) * 8;
        }
        if ( summary.treasureMapCompleted ) {
            earned += static_cast<uint64_t>( summary.treasureMapsCompletedThisSession ) * 12;
        }
        if ( summary.supplyRushTriggered ) {
            earned += static_cast<uint64_t>( summary.supplyRushesTriggered ) * 10;
        }

        summary.renownEarned = earned;

        if ( std::numeric_limits<uint64_t>::max() - data.offlineRenown < earned ) {
            data.offlineRenown = std::numeric_limits<uint64_t>::max();
        }
        else {
            data.offlineRenown += earned;
        }

        summary.renownTotal = data.offlineRenown;
        summary.rankAfter = getOfflineRank( data.offlineRenown );

        if ( summary.rankAfter <= summary.rankBefore ) {
            return;
        }

        size_t bestIndex = offlineFundMembers.size();
        int32_t bestProduction = 0;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const int32_t production = summary.productionRewards.*offlineFundMembers[i];
            if ( production > bestProduction ) {
                bestProduction = production;
                bestIndex = i;
            }
        }

        if ( bestIndex == offlineFundMembers.size() ) {
            return;
        }

        const FundsMember member = offlineFundMembers[bestIndex];
        summary.ranksEarned = static_cast<uint32_t>( summary.rankAfter - summary.rankBefore );

        // Grant every promotion crossed during a long offline interval. Previously only the final
        // rank paid out, making a large homecoming less rewarding than several short ones.
        for ( int rank = summary.rankBefore + 1; rank <= summary.rankAfter; ++rank ) {
            const int rewardPercent = 10 + rank * 5;
            const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
            const int64_t calculatedBonus = std::max<int64_t>( 1, ( static_cast<int64_t>( bestProduction ) * rewardPercent ) / 100 );
            const int32_t grantedBonus = static_cast<int32_t>( std::min<int64_t>( calculatedBonus, capacity ) );

            if ( grantedBonus <= 0 ) {
                break;
            }

            summary.rankRewards.*member += grantedBonus;
            summary.bonusRewards.*member += grantedBonus;
            summary.rewards.*member += grantedBonus;
            data.resources.*member += grantedBonus;
        }
    }

    OfflineProgressSummary applyOfflineProgress( Kingdom & kingdom, const int64_t elapsedOverrideSeconds = -1 )
    {
        OfflineProgressData data;
        const int64_t now = getCurrentUnixTime();

        if ( !loadOfflineProgressData( data ) ) {
            data.lastSeenUnix = now;
            data.resources = kingdom.GetFunds();
            captureOfflineKingdomState( data, kingdom );
            saveOfflineProgressData( data );
            return {};
        }

        // The map or loaded save owns the treasury. Offline production is calculated in a
        // temporary wallet solely to determine RPG XP and never changes game resources.
        data.resources = {};

        OfflineProgressSummary summary;
        const int64_t previousLastSeenUnix = data.lastSeenUnix;
        const int64_t currentElapsedSeconds
            = elapsedOverrideSeconds >= 0 ? elapsedOverrideSeconds : ( now > previousLastSeenUnix ? now - previousLastSeenUnix : 0 );
        summary.elapsedSeconds = data.pendingResumeSeconds > std::numeric_limits<int64_t>::max() - currentElapsedSeconds
                                     ? std::numeric_limits<int64_t>::max()
                                     : currentElapsedSeconds + data.pendingResumeSeconds;
        data.pendingResumeSeconds = 0;
        summary.stateCastles = data.stateCastles;
        summary.stateTowns = data.stateTowns;
        summary.stateHeroes = data.stateHeroes;
        summary.stateMines = data.stateMines;
        summary.stateArtifacts = data.stateArtifacts;
        summary.stateEfficiencyPercent = std::clamp<uint32_t>( data.stateEfficiencyPercent, 100, 150 );
        summary.showPopup = summary.elapsedSeconds > 0;

        const int64_t wholeDays = summary.elapsedSeconds / offlineSecondsPerDay;
        const int64_t remainingSeconds = summary.elapsedSeconds % offlineSecondsPerDay;

        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const FundsMember member = offlineFundMembers[i];
            const int64_t baseResource = data.resources.*member;
            int64_t savedDailyIncome = std::max<int64_t>( 0, data.dailyIncome.*member );
            if ( offlineResourceTypes[i] == Resource::GOLD ) {
                savedDailyIncome = savedDailyIncome * 1500 / 100;
            }
            const int64_t dailyIncome = savedDailyIncome * summary.stateEfficiencyPercent / 100;
            const int64_t capacity = std::numeric_limits<int32_t>::max() - baseResource;

            // Carry is measured in resource-seconds, modulo one real-world day. This preserves
            // fractional rewards exactly across arbitrarily many offline sessions.
            const int64_t partialNumerator = dailyIncome * remainingSeconds + data.carry[i];
            const int64_t partialReward = partialNumerator / offlineSecondsPerDay;
            data.carry[i] = partialNumerator % offlineSecondsPerDay;

            int64_t reward = std::min<int64_t>( partialReward, capacity );
            if ( reward < capacity && dailyIncome > 0 ) {
                const int64_t remainingCapacity = capacity - reward;
                if ( wholeDays > remainingCapacity / dailyIncome ) {
                    reward = capacity;
                }
                else {
                    reward += wholeDays * dailyIncome;
                }
            }

            summary.productionRewards.*member = static_cast<int32_t>( reward );
            summary.rewards.*member = static_cast<int32_t>( reward );
            data.resources.*member = static_cast<int32_t>( baseResource + reward );
        }

        applyOfflineHomecomingBonus( summary, data, previousLastSeenUnix );
        applyOfflineStreakProgress( summary, data, std::max( previousLastSeenUnix, now ) );
        applyOfflineRareDiscovery( summary, data, previousLastSeenUnix );
        applyOfflineContractProgress( summary, data );
        applyOfflineTreasureHunt( summary, data, previousLastSeenUnix );
        applyOfflineSupplyRush( summary, data );
        applyOfflineRenownProgress( summary, data );

        // Compute XP from the entire elapsed interval. The virtual resource wallet above is
        // limited to int32 for legacy event calculations, so it cannot set an XP duration cap.
        const long double dailyGoldEquivalent = static_cast<long double>( std::max( 0, data.dailyIncome.gold ) ) * 15
                                                + ( static_cast<long double>( std::max( 0, data.dailyIncome.wood ) )
                                                    + std::max( 0, data.dailyIncome.ore ) ) * 100
                                                + ( static_cast<long double>( std::max( 0, data.dailyIncome.mercury ) )
                                                    + std::max( 0, data.dailyIncome.sulfur ) + std::max( 0, data.dailyIncome.crystal )
                                                    + std::max( 0, data.dailyIncome.gems ) ) * 500;
        const long double bonusEquivalent = static_cast<long double>( summary.bonusRewards.gold )
                                            + ( static_cast<long double>( summary.bonusRewards.wood ) + summary.bonusRewards.ore ) * 100
                                            + ( static_cast<long double>( summary.bonusRewards.mercury ) + summary.bonusRewards.sulfur
                                                + summary.bonusRewards.crystal + summary.bonusRewards.gems ) * 500;
        const long double baselineDailyEquivalent = std::max<long double>( 2500.0L, dailyGoldEquivalent );
        const long double xpValue = baselineDailyEquivalent * summary.stateEfficiencyPercent / 100
                                    * static_cast<long double>( summary.elapsedSeconds ) / offlineSecondsPerDay + bonusEquivalent;
        summary.xpEarned = static_cast<uint64_t>( std::min( xpValue, static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) );
        summary.xpEarned = fheroes2::RPG::addExperience( kingdom.GetColor(), summary.xpEarned, fheroes2::RPG::ExperienceKind::OFFLINE );
        data.resources = kingdom.GetFunds();

        // The next offline interval uses the active map and player state at this snapshot.
        // Preserve a future saved timestamp if the local system clock temporarily moves backwards.
        data.lastSeenUnix = std::max( previousLastSeenUnix, now );
        captureOfflineKingdomState( data, kingdom );
        saveOfflineProgressData( data );

        return summary;
    }

    std::string getHomecomingChestName( const int tier )
    {
        switch ( tier ) {
        case 1:
            return _( "Scout's Satchel" );
        case 2:
            return _( "Caravan Crate" );
        case 3:
            return _( "Royal Chest" );
        case 4:
            return _( "King's Vault" );
        case 5:
            return _( "Legendary Hoard" );
        default:
            return {};
        }
    }

    std::string getOfflineContractName( const int contractId )
    {
        switch ( contractId ) {
        case 0:
            return _( "Watchman's Vigil" );
        case 1:
            return _( "Caravan Logistics" );
        case 2:
            return _( "Royal Emissary" );
        case 3:
            return _( "Alchemical Assay" );
        case 4:
            return _( "Grand Charter" );
        default:
            return _( "Royal Commission" );
        }
    }

    std::string getOfflineTreasureMapName( const int mapId )
    {
        switch ( mapId ) {
        case 0:
            return _( "Corsair's Chart" );
        case 1:
            return _( "Crypt Map" );
        case 2:
            return _( "Sunken Galleon Route" );
        case 3:
            return _( "Archmage's Cache" );
        default:
            return _( "Ancient Parchment" );
        }
    }

    std::string getOfflineHomecomingEventName( const int eventId )
    {
        switch ( eventId ) {
        case 0:
            return _( "Returning Caravan" );
        case 1:
            return _( "Forgotten Cache" );
        case 2:
            return _( "Grateful Settlers" );
        case 3:
            return _( "Veteran Patrol" );
        case 4:
            return _( "Merchant Windfall" );
        case 5:
            return _( "Recovered Tribute" );
        case 6:
            return _( "Guild Shipment" );
        case 7:
            return _( "Royal Bounty" );
        default:
            return _( "Homecoming Gift" );
        }
    }

    std::string getOfflineRareDiscoveryName( const int discoveryId )
    {
        switch ( discoveryId ) {
        case 0:
            return _( "Hidden Vein" );
        case 1:
            return _( "Alchemist's Cache" );
        case 2:
            return _( "Buried Tribute" );
        case 3:
            return _( "Ancient Storehouse" );
        default:
            return _( "Rare Discovery" );
        }
    }

    std::string formatOfflineReward( const int resource, const int32_t amount )
    {
        if ( resource == Resource::UNKNOWN || amount <= 0 ) {
            return {};
        }

        return "+" + std::to_string( amount ) + " " + Resource::String( resource );
    }

    std::string formatOfflineRewards( const Funds & rewards )
    {
        std::string result;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const std::string reward = formatOfflineReward( offlineResourceTypes[i], rewards.*offlineFundMembers[i] );
            if ( reward.empty() ) {
                continue;
            }
            if ( !result.empty() ) {
                result += ", ";
            }
            result += reward;
        }

        return result;
    }

    void showOfflineProgressPopup( const OfflineProgressSummary & summary )
    {
        if ( !summary.showPopup ) {
            return;
        }

        int64_t seconds = summary.elapsedSeconds;
        const int64_t days = seconds / offlineSecondsPerDay;
        seconds %= offlineSecondsPerDay;
        const int64_t hours = seconds / ( 60 * 60 );
        const int64_t minutes = ( seconds % ( 60 * 60 ) ) / 60;

        std::string message;
        if ( days == 0 && hours == 0 && minutes == 0 ) {
            message = _( "Away < 1m" );
        }
        else {
            message = _( "Away %{days}d %{hours}h %{minutes}m" );
            StringReplace( message, "%{days}", std::to_string( days ) );
            StringReplace( message, "%{hours}", std::to_string( hours ) );
            StringReplace( message, "%{minutes}", std::to_string( minutes ) );
        }




        message += "\n";

        message += _( "RPG XP earned: " );
        message += fheroes2::RPG::formatExperience( summary.xpEarned );

        const std::string supplies = formatOfflineRewards( summary.rewards );
        if ( !supplies.empty() ) {
            message += "\n";
            // Offline progression intentionally does not modify the active map's treasury.
            // These are the virtual production values used to calculate the RPG XP above, so
            // labelling them as received supplies made the popup promise rewards it never gave.
            message += _( "Virtual supplies used for XP: " );
            message += supplies;
        }

        message += "\n";
        message += _( "Treasury unchanged." );

        if ( summary.homecomingTier > 0 ) {
            const std::string chestName = getHomecomingChestName( summary.homecomingTier );
            if ( !chestName.empty() ) {
                message += "\n";
                message += _( "Homecoming Chest: " );
                message += chestName;
                if ( summary.eventCount > 0 ) {
                    message += " (";
                    for ( size_t i = 0; i < summary.eventCount; ++i ) {
                        if ( i != 0 ) {
                            message += ", ";
                        }
                        message += getOfflineHomecomingEventName( summary.events[i].eventId ) + " "
                                   + formatOfflineReward( summary.events[i].resource, summary.events[i].bonus );
                    }
                    message += ")";
                }
            }
        }

        if ( summary.homecomingStreak > 0 ) {
            message += "\n";
            message += _( "Homecoming Streak: " );
            message += std::to_string( summary.homecomingStreak );
            message += _( " d" );
            if ( summary.milestonePercent > 0 ) {
                message += " (+" + std::to_string( summary.milestonePercent ) + "% bonus)";
                const std::string milestoneReward = formatOfflineReward( summary.milestoneResource, summary.milestoneBonus );
                if ( !milestoneReward.empty() ) {
                    message += ": " + milestoneReward;
                }
            }
        }

        if ( summary.contractCompleted ) {
            message += "\n";
            if ( summary.contractsCompletedThisSession == 1 ) {
                message += _( "Steward Contract Fulfilled: " );
                message += getOfflineContractName( summary.contractId );
            }
            else {
                message += _( "Steward Contracts Fulfilled: " );
                message += std::to_string( summary.contractsCompletedThisSession );
                message += _( " (starting with " );
                message += getOfflineContractName( summary.contractId ) + ")";
            }
            const std::string contractReward = formatOfflineRewards( summary.contractRewards );
            if ( !contractReward.empty() ) {
                message += " (" + contractReward + ")";
            }
            if ( summary.nextContractId >= 0 && summary.nextContractTarget > 0 ) {
                message += "\n";
                message += _( "Next Contract: " );
                message += getOfflineContractName( summary.nextContractId ) + " (" + std::to_string( summary.nextContractProgress ) + "/"
                           + std::to_string( summary.nextContractTarget ) + ")";
            }
        }
        else if ( summary.contractProgressAfter > summary.contractProgressBefore ) {
            message += "\n";
            message += _( "Contract Progress: " );
            message += getOfflineContractName( summary.contractId );
            message += " (" + std::to_string( summary.contractProgressAfter ) + "/" + std::to_string( summary.contractTarget ) + ")";
        }

        if ( summary.treasureMapCompleted ) {
            message += "\n";
            if ( summary.treasureMapsCompletedThisSession == 1 ) {
                message += _( "Treasure Map Deciphered: " );
                message += getOfflineTreasureMapName( summary.treasureMapId );
            }
            else {
                message += _( "Treasure Maps Deciphered: " );
                message += std::to_string( summary.treasureMapsCompletedThisSession );
                message += _( " (starting with " );
                message += getOfflineTreasureMapName( summary.treasureMapId ) + ")";
            }
            const std::string treasureReward = formatOfflineRewards( summary.treasureRewards );
            if ( !treasureReward.empty() ) {
                message += " (" + treasureReward + ")";
            }
            message += "; " + std::to_string( summary.treasureMapsCompleted ) + _( " total maps" );
            message += _( "; fragments: " ) + std::to_string( summary.treasureFragmentsAfter ) + "/5";
        }
        else if ( summary.treasureFragmentsEarned > 0 ) {
            message += "\n";
            message += _( "Map Fragments Found: " );
            message += "+" + std::to_string( summary.treasureFragmentsEarned ) + " (" + std::to_string( summary.treasureFragmentsAfter );
            message += " / 5";
            message += ")";
        }

        if ( summary.rareDiscovery ) {
            message += "\n";
            message += getOfflineRareDiscoveryName( summary.rareDiscoveryId ) + ": "
                       + formatOfflineReward( summary.rareDiscoveryResource, summary.rareDiscoveryBonus );
        }

        if ( summary.supplyRushTriggered ) {
            message += "\n";
            const std::string rushRewards = formatOfflineRewards( summary.supplyRushRewards );
            if ( summary.supplyRushesTriggered == 1 ) {
                message += rushRewards.empty() ? _( "Supply Rush triggered; virtual stores were full." ) : _( "Supply Rush: " ) + rushRewards;
            }
            else {
                message += _( "Supply Rushes: " ) + std::to_string( summary.supplyRushesTriggered );
                message += rushRewards.empty() ? _( "; virtual stores were full." ) : " (" + rushRewards + ")";
            }
        }
        if ( summary.supplyRushEarned > 0 ) {
            message += "\n";
            message += _( "Supply Rush Meter: " );
            message += std::to_string( summary.supplyRushAfter ) + "/100 (+" + std::to_string( summary.supplyRushEarned ) + ")";
        }

        if ( summary.renownEarned > 0 ) {
            message += "\n";
            message += _( "Steward Renown: " );
            message += "+" + std::to_string( summary.renownEarned ) + " (" + std::to_string( summary.renownTotal ) + ")";
        }

        if ( summary.rankAfter > summary.rankBefore ) {
            message += "\n";
            if ( summary.ranksEarned > 1 ) {
                message += _( "Steward Promotions: " );
                message += std::to_string( summary.ranksEarned ) + _( ", now " );
            }
            else {
                message += _( "Steward Promoted: " );
            }
            message += getOfflineRankName( summary.rankAfter );
            const std::string rankReward = formatOfflineRewards( summary.rankRewards );
            if ( !rankReward.empty() ) {
                message += " (" + rankReward + ")";
            }
        }

        message += "\n";
        message += _( "Realm Readiness: " );
        message += std::to_string( summary.stateEfficiencyPercent ) + "% (" + std::to_string( summary.stateCastles ) + " castles, "
                   + std::to_string( summary.stateTowns ) + " towns, " + std::to_string( summary.stateHeroes ) + " heroes, "
                   + std::to_string( summary.stateMines ) + " mines)";

        fheroes2::showStandardTextMessage( _( "Offline Progress" ), std::move( message ), Dialog::OK );
    }

    void showOfflineProgressPopups( const OfflineProgressSummary & summary )
    {
        showOfflineProgressPopup( summary );
    }

    OfflineProgressSummary consumePendingOfflineResumeProgress( Kingdom & kingdom )
    {
        if ( !offlineResumePending || kingdom.GetColor() != offlineProgressActivePlayerColor ) {
            return {};
        }

        const int64_t elapsedSeconds = offlineResumeElapsedSeconds;
        offlineResumePending = false;
        offlineResumeElapsedSeconds = 0;

        return applyOfflineProgress( kingdom, elapsedSeconds );
    }

    bool SortPlayers( const Player * player1, const Player * player2 )
    {
        return ( player1->isControlHuman() && !player2->isControlHuman() )
               || ( ( player1->isControlHuman() == player2->isControlHuman() ) && ( player1->GetColor() < player2->GetColor() ) );
    }

    // Get colors value of players to use in fog directions update.
    // For human allied AI returns colors of this alliance, for hostile AI - colors of all human players and their allies.
    PlayerColorsSet hotSeatAIFogColors( const Player * player )
    {
        assert( player != nullptr );

        // This function should be called when AI makes a move.
        assert( world.GetKingdom( player->GetColor() ).GetControl() == CONTROL_AI );

        const PlayerColorsSet humanColors = Players::HumanColors();
        // Check if the current AI player is a friend of any of human players to fully show his move and revealed map,
        // otherwise his revealed map will not be shown - instead of it we will show the revealed map by all human players.
        const bool isFriendlyAI = Players::isFriends( player->GetColor(), humanColors );

        if ( isFriendlyAI || player->isAIAutoControlMode() ) {
            // Fully update fog directions for allied AI players in Hot Seat mode as the previous move could be done by opposing player.
            return player->GetFriends();
        }

        // If AI is hostile for all human players then fully update fog directions for all human players to see enemy AI hero move on tiles with
        // discovered fog.

        PlayerColorsSet friendColors = 0;

        for ( const PlayerColor color : PlayerColorsVector( humanColors ) ) {
            const Player * humanPlayer = Players::Get( color );
            if ( humanPlayer ) {
                friendColors |= humanPlayer->GetFriends();
            }
        }

        return friendColors;
    }

    void ShowNewWeekDialog()
    {
        // Restore the original music on exit
        const AudioManager::MusicRestorer musicRestorer;

        const bool isNewMonth = world.BeginMonth();

        AudioManager::PlayMusic( isNewMonth ? MUS::NEW_MONTH : MUS::NEW_WEEK, Music::PlaybackMode::PLAY_ONCE );

        auto [headerText, messageText] = [isNewMonth]() -> std::pair<std::string, std::string> {
            if ( isNewMonth ) {
                return { _( "New Month!" ), _( "Astrologers proclaim the Month of the %{name}." ) };
            }

            return { _( "New Week!" ), _( "Astrologers proclaim the Week of the %{name}." ) };
        }();

        const Week & week = world.GetWeekType();

        StringReplace( messageText, "%{name}", week.GetName() );
        messageText += "\n\n";

        std::unique_ptr<const fheroes2::MonsterDialogElement> monsterDialogElement;

        switch ( week.GetType() ) {
        case WeekName::MONSTERS: {
            const Monster monster( week.GetMonster() );
            assert( monster.isValid() );

            const uint32_t count = isNewMonth ? Castle::GetGrownMonthOf() : Castle::GetGrownWeekOf();
            assert( count > 0 );

            if ( isNewMonth ) {
                messageText += ( count == 100 ) ? _( "After regular growth, the population of %{monster} is doubled!" )
                                                : _n( "After regular growth, the population of %{monster} increases by %{count} percent!",
                                                      "After regular growth, the population of %{monster} increases by %{count} percent!", count );
            }
            else {
                messageText += _( "%{monster} growth +%{count}." );
            }

            StringReplaceWithLowercase( messageText, "%{monster}", monster.GetMultiName() );
            StringReplace( messageText, "%{count}", count );

            monsterDialogElement = std::make_unique<const fheroes2::MonsterDialogElement>( monster );

            break;
        }
        case WeekName::PLAGUE:
            messageText += _( "All populations are halved." );
            break;
        default:
            messageText += _( "All dwellings increase population." );
            break;
        }

        fheroes2::showStandardTextMessage( std::move( headerText ), std::move( messageText ), Dialog::OK,
                                           monsterDialogElement ? std::vector<const fheroes2::DialogElement *>{ monsterDialogElement.get() }
                                                                : std::vector<const fheroes2::DialogElement *>{} );
    }

    void ShowWarningLostTownsDialog()
    {
        const Kingdom & myKingdom = world.GetKingdom( Settings::Get().CurrentColor() );
        const uint32_t lostTownDays = myKingdom.GetLostTownDays();

        if ( lostTownDays == 1 ) {
            Game::DialogPlayers( myKingdom.GetColor(), _( "Beware!" ),
                                 _( "%{color} player, this is your last day to capture a town, or you will be banished from this land." ) );
        }
        else if ( lostTownDays > 0 && lostTownDays <= Game::GetLostTownDays() ) {
            std::string str = _( "%{color} player, you only have %{day} days left to capture a town, or you will be banished from this land." );
            StringReplace( str, "%{day}", lostTownDays );
            Game::DialogPlayers( myKingdom.GetColor(), _( "Beware!" ), str );
        }
    }
}

void Game::onApplicationFocusChanged( const bool isFocused )
{
    if ( offlineProgressActivePlayerColor == PlayerColor::NONE ) {
        return;
    }

    if ( !isFocused ) {
        if ( offlineSuspendStartedUnix > 0 ) {
            return;
        }

        const int64_t now = getCurrentUnixTime();
        persistOfflineProgressSnapshot( world.GetKingdom( offlineProgressActivePlayerColor ), now );

        offlineSuspendStartedUnix = now;
        // Keep any earlier foregrounded interval pending. A player can background the app again
        // before a battle or AI turn reaches the safe point where rewards are applied.
        return;
    }

    if ( offlineSuspendStartedUnix <= 0 ) {
        return;
    }

    const int64_t now = getCurrentUnixTime();
    const int64_t resumedInterval = now > offlineSuspendStartedUnix ? now - offlineSuspendStartedUnix : 0;
    offlineSuspendStartedUnix = 0;

    if ( resumedInterval > 0 ) {
        if ( offlineResumeElapsedSeconds > std::numeric_limits<int64_t>::max() - resumedInterval ) {
            offlineResumeElapsedSeconds = std::numeric_limits<int64_t>::max();
        }
        else {
            offlineResumeElapsedSeconds += resumedInterval;
        }
    }

    offlineResumePending = offlineResumeElapsedSeconds > 0;
}

fheroes2::GameMode Game::StartBattleOnly()
{
    static Battle::Only battleOnlySetup;

    world.generateBattleOnlyMap( battleOnlySetup.terrainType() );

    bool reset = false;
    bool allowBackup = true;

    while ( battleOnlySetup.setup( allowBackup, reset ) ) {
        allowBackup = false;

        if ( reset ) {
            world.generateBattleOnlyMap( battleOnlySetup.terrainType() );
            battleOnlySetup.reset();
            reset = false;
            continue;
        }

        world.setUniformTerrain( battleOnlySetup.terrainType() );
        battleOnlySetup.StartBattle();
        break;
    }

    return fheroes2::GameMode::MAIN_MENU;
}

fheroes2::GameMode Game::StartGame()
{
    const Settings & conf = Settings::Get();

    // setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    if ( !conf.LoadedGameVersion() )
        GameOver::Result::Get().Reset();

    return Interface::AdventureMap::Get().StartGame();
}

void Game::DialogPlayers( const PlayerColor color, std::string title, std::string message )
{
    const Player * player = Players::Get( color );
    StringReplace( message, "%{color}", ( player ? player->GetName() : Color::String( color ) ) );

    const fheroes2::Sprite & border = Assets::getImage( ICN::BRCREST, 6 );
    fheroes2::Sprite sign = border;

    switch ( color ) {
    case PlayerColor::BLUE:
        fheroes2::Blit( Assets::getImage( ICN::BRCREST, 0 ), sign, 4, 4 );
        break;
    case PlayerColor::GREEN:
        fheroes2::Blit( Assets::getImage( ICN::BRCREST, 1 ), sign, 4, 4 );
        break;
    case PlayerColor::RED:
        fheroes2::Blit( Assets::getImage( ICN::BRCREST, 2 ), sign, 4, 4 );
        break;
    case PlayerColor::YELLOW:
        fheroes2::Blit( Assets::getImage( ICN::BRCREST, 3 ), sign, 4, 4 );
        break;
    case PlayerColor::ORANGE:
        fheroes2::Blit( Assets::getImage( ICN::BRCREST, 4 ), sign, 4, 4 );
        break;
    case PlayerColor::PURPLE:
        fheroes2::Blit( Assets::getImage( ICN::BRCREST, 5 ), sign, 4, 4 );
        break;
    default:
        // Did you add a new color? Add the logic for it!
        assert( 0 );
        break;
    }

    const fheroes2::CustomImageDialogElement imageUI( std::move( sign ) );
    fheroes2::showStandardTextMessage( std::move( title ), std::move( message ), Dialog::OK, { &imageUI } );
}

void Game::OpenCastleDialog( Castle & castle, bool updateFocus /* = true */, const bool renderBackgroundDialog /* = true */ )
{
    // setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    // Stop all sounds, but not the music - it will be replaced by the music of the castle
    AudioManager::stopSounds();

    const Settings & conf = Settings::Get();
    Kingdom & myKingdom = world.GetKingdom( conf.CurrentColor() );
    const VecCastles & myCastles = myKingdom.GetCastles();
    VecCastles::const_iterator it = std::find( myCastles.begin(), myCastles.end(), &castle );

    const size_t heroCountBefore = myKingdom.GetHeroes().size();

    assert( it != myCastles.end() );

    bool openConstructionWindow{ false };
    bool openMageGuildWindow{ false };
    Castle::CastleDialogReturnValue result = ( *it )->OpenDialog( openConstructionWindow, openMageGuildWindow, true, renderBackgroundDialog );

    while ( result != Castle::CastleDialogReturnValue::Close ) {
        switch ( result ) {
        case Castle::CastleDialogReturnValue::PreviousCastle:
        case Castle::CastleDialogReturnValue::PreviousConstructionWindow:
        case Castle::CastleDialogReturnValue::PreviousMageGuildWindow:
            if ( it == myCastles.begin() ) {
                it = myCastles.end();
            }
            --it;
            break;
        case Castle::CastleDialogReturnValue::NextCastle:
        case Castle::CastleDialogReturnValue::NextConstructionWindow:
        case Castle::CastleDialogReturnValue::NextMageGuildWindow:
            ++it;
            if ( it == myCastles.end() ) {
                it = myCastles.begin();
            }
            break;
        default:
            break;
        }

        assert( it != myCastles.end() );

        openConstructionWindow
            = ( result == Castle::CastleDialogReturnValue::PreviousConstructionWindow ) || ( result == Castle::CastleDialogReturnValue::NextConstructionWindow );

        openMageGuildWindow
            = ( result == Castle::CastleDialogReturnValue::PreviousMageGuildWindow ) || ( result == Castle::CastleDialogReturnValue::NextMageGuildWindow );

        result = ( *it )->OpenDialog( openConstructionWindow, openMageGuildWindow, false, renderBackgroundDialog );
    }

    // If Castle dialog background was not rendered than we have opened it from other dialog (Kingdom Overview)
    // and there is no need update Adventure map interface at this time.
    if ( renderBackgroundDialog ) {
        Interface::AdventureMap & adventureMapInterface = Interface::AdventureMap::Get();

        if ( heroCountBefore != myKingdom.GetHeroes().size() ) {
            // A hero could be recruited in the castle or a hero could be dismissed by opening hero's dialog
            // and switching to the other hero and dismissing this hero. We need to update the hero list scrollbar.
            // NOTICE: we can update the scrollbar only here - after exiting the castle screen not to interfere with castle screen image.
            adventureMapInterface.GetIconsPanel().resetIcons( ICON_HEROES );
        }

        if ( updateFocus ) {
            assert( it != myCastles.end() );

            // When exiting the castle, we must focus on it or on the hero visiting this castle.
            Heroes * heroInCastle = world.getTile( ( *it )->GetIndex() ).getHero();
            if ( heroInCastle == nullptr ) {
                adventureMapInterface.SetFocus( *it );
            }
            else {
                adventureMapInterface.SetFocus( heroInCastle, false );
            }
        }
        else {
            // If we don't update focus, we still have to restore environment sounds and terrain music theme
            restoreSoundsForCurrentFocus();
        }

        // The castle garrison can change
        adventureMapInterface.RedrawFocus();

        // Fade-in game screen only for 640x480 resolution.
        if ( fheroes2::Display::instance().isDefaultSize() ) {
            setDisplayFadeIn();
        }
    }
    else {
        // If we opened the castle dialog from other dialog, we have to restore environment sounds and terrain music theme instead of the castle's music theme
        restoreSoundsForCurrentFocus();
    }
}

void Game::OpenHeroesDialog( Heroes & hero, bool updateFocus, const bool renderBackgroundDialog, const bool disableDismiss /* = false */ )
{
    // setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    Interface::AdventureMap & adventureMapInterface = Interface::AdventureMap::Get();

    const VecHeroes & myHeroes = hero.GetKingdom().GetHeroes();
    VecHeroes::const_iterator it = std::find( myHeroes.begin(), myHeroes.end(), &hero );

    const bool isDefaultScreenSize = fheroes2::Display::instance().isDefaultSize();
    bool needFade = true;
    int result = Dialog::ZERO;

    while ( it != myHeroes.end() && result != Dialog::CANCEL ) {
        result = ( *it )->OpenDialog( false, needFade, disableDismiss, false, renderBackgroundDialog, false,
                                      fheroes2::getLanguageFromAbbreviation( Settings::Get().getGameLanguage() ) );

        if ( needFade ) {
            needFade = false;
        }

        switch ( result ) {
        case Dialog::PREV:
            if ( it == myHeroes.begin() ) {
                it = myHeroes.end();
            }
            --it;
            break;

        case Dialog::NEXT:
            ++it;
            if ( it == myHeroes.end() ) {
                it = myHeroes.begin();
            }
            break;

        case Dialog::DISMISS:
            AudioManager::PlaySound( M82::KILLFADE );

            ( *it )->ShowPath( false );

            // Check if this dialog is not opened from the other dialog and we will be exiting to the Adventure map.
            if ( renderBackgroundDialog ) {
                // Redraw Adventure map with hidden hero path.
                adventureMapInterface.redraw( Interface::REDRAW_GAMEAREA );

                // Fade-in game screen only for 640x480 resolution.
                if ( isDefaultScreenSize ) {
                    fheroes2::fadeInDisplay();
                }

                ( *it )->FadeOut();
                updateFocus = true;
            }

            ( *it )->Dismiss( 0 );
            it = myHeroes.end();

            result = Dialog::CANCEL;
            break;

        case Dialog::CANCEL:
            needFade = true;
            break;

        default:
            break;
        }
    }

    // If Hero dialog background was not rendered than we have opened it from other dialog (Kingdom Overview or Castle dialog)
    // and there is no need update Adventure map interface at this time.
    if ( renderBackgroundDialog ) {
        if ( updateFocus ) {
            if ( it != myHeroes.end() ) {
                adventureMapInterface.SetFocus( *it, false );
            }
            else {
                adventureMapInterface.ResetFocus( GameFocus::HEROES, false );
            }
        }
        // The hero's army can change
        adventureMapInterface.RedrawFocus();

        // Fade-in game screen only for 640x480 resolution.
        if ( needFade && renderBackgroundDialog && isDefaultScreenSize ) {
            setDisplayFadeIn();
        }
    }
}

int Interface::AdventureMap::GetCursorFocusCastle( const Castle & castle, const Maps::Tile & tile )
{
    switch ( tile.getMainObjectType() ) {
    case MP2::OBJ_NON_ACTION_CASTLE:
    case MP2::OBJ_CASTLE: {
        const Castle * otherCastle = world.getCastle( tile.GetCenter() );

        if ( otherCastle ) {
            return otherCastle->GetColor() == castle.GetColor() ? Cursor::CASTLE : Cursor::POINTER;
        }

        break;
    }

    case MP2::OBJ_HERO: {
        const Heroes * hero = tile.getHero();

        if ( hero ) {
            return hero->GetColor() == castle.GetColor() ? Cursor::HEROES : Cursor::POINTER;
        }

        break;
    }

    default:
        break;
    }

    return Cursor::POINTER;
}

int Interface::AdventureMap::GetCursorFocusShipmaster( const Heroes & hero, const Maps::Tile & tile )
{
    const bool isWater = tile.isWater();

    switch ( tile.getMainObjectType() ) {
    case MP2::OBJ_NON_ACTION_CASTLE:
    case MP2::OBJ_CASTLE: {
        const Castle * castle = world.getCastle( tile.GetCenter() );

        if ( castle ) {
            if ( tile.getMainObjectType() == MP2::OBJ_NON_ACTION_CASTLE && isWater && tile.isPassableFrom( Direction::CENTER, true, false, hero.GetColor() ) ) {
                return Cursor::DistanceThemes( Cursor::CURSOR_HERO_BOAT, hero.getNumOfTravelDays( tile.GetIndex() ) );
            }

            return hero.GetColor() == castle->GetColor() ? Cursor::CASTLE : Cursor::POINTER;
        }

        break;
    }

    case MP2::OBJ_HERO: {
        const Heroes * otherHero = tile.getHero();

        if ( otherHero ) {
            if ( !otherHero->isShipMaster() ) {
                return hero.GetColor() == otherHero->GetColor() ? Cursor::HEROES : Cursor::POINTER;
            }

            if ( otherHero->GetCenter() == hero.GetCenter() ) {
                return Cursor::HEROES;
            }

            if ( hero.GetColor() == otherHero->GetColor() ) {
                return Cursor::DistanceThemes( Cursor::CURSOR_HERO_MEET, hero.getNumOfTravelDays( tile.GetIndex() ) );
            }

            if ( hero.isFriends( otherHero->GetColor() ) ) {
                return Cursor::POINTER;
            }

            return Cursor::DistanceThemes( Cursor::CURSOR_HERO_FIGHT, hero.getNumOfTravelDays( tile.GetIndex() ) );
        }

        break;
    }

    // Some map editors allow to place monsters on water tiles
    case MP2::OBJ_MONSTER:
        return isWater ? Cursor::DistanceThemes( Cursor::CURSOR_HERO_FIGHT, hero.getNumOfTravelDays( tile.GetIndex() ) ) : Cursor::POINTER;

    default:
        if ( isWater ) {
            if ( MP2::isWaterActionObject( tile.getMainObjectType() ) ) {
                return Cursor::DistanceThemes( Cursor::CURSOR_HERO_BOAT_ACTION, hero.getNumOfTravelDays( tile.GetIndex() ) );
            }

            if ( tile.isPassableFrom( Direction::CENTER, true, false, hero.GetColor() ) ) {
                return Cursor::DistanceThemes( Cursor::CURSOR_HERO_BOAT, hero.getNumOfTravelDays( tile.GetIndex() ) );
            }
        }
        else {
            return Cursor::DistanceThemes( Cursor::CURSOR_HERO_ANCHOR, hero.getNumOfTravelDays( tile.GetIndex() ) );
        }

        break;
    }

    return Cursor::POINTER;
}

int Interface::AdventureMap::_getCursorNoFocus( const Maps::Tile & tile )
{
    switch ( tile.getMainObjectType() ) {
    case MP2::OBJ_NON_ACTION_CASTLE:
    case MP2::OBJ_CASTLE: {
        const Castle * castle = world.getCastle( tile.GetCenter() );
        if ( castle && castle->GetColor() == Settings::Get().CurrentColor() ) {
            return Cursor::CASTLE;
        }
        break;
    }
    case MP2::OBJ_HERO: {
        const Heroes * hero = tile.getHero();
        if ( hero && hero->GetColor() == Settings::Get().CurrentColor() ) {
            return Cursor::HEROES;
        }
        break;
    }
    default:
        break;
    }

    return Cursor::POINTER;
}

int Interface::AdventureMap::GetCursorFocusHeroes( const Heroes & hero, const Maps::Tile & tile )
{
    if ( hero.Modes( Heroes::ENABLEMOVE ) ) {
        return Cursor::Get().Themes();
    }

    if ( hero.isShipMaster() ) {
        return GetCursorFocusShipmaster( hero, tile );
    }

    switch ( tile.getMainObjectType() ) {
    case MP2::OBJ_NON_ACTION_CASTLE:
    case MP2::OBJ_CASTLE: {
        const Castle * castle = world.getCastle( tile.GetCenter() );

        if ( castle ) {
            if ( tile.getMainObjectType() == MP2::OBJ_NON_ACTION_CASTLE ) {
                if ( tile.GetPassable() == 0 ) {
                    return ( hero.GetColor() == castle->GetColor() ) ? Cursor::CASTLE : Cursor::POINTER;
                }

                return Cursor::DistanceThemes( Maps::isTileUnderProtection( tile.GetIndex() ) ? Cursor::CURSOR_HERO_FIGHT : Cursor::CURSOR_HERO_MOVE,
                                               hero.getNumOfTravelDays( tile.GetIndex() ) );
            }

            if ( hero.GetIndex() == castle->GetIndex() ) {
                return hero.GetColor() == castle->GetColor() ? Cursor::CASTLE : Cursor::POINTER;
            }

            if ( hero.GetColor() == castle->GetColor() ) {
                return Cursor::DistanceThemes( Cursor::CURSOR_HERO_ACTION, hero.getNumOfTravelDays( castle->GetIndex() ) );
            }

            if ( hero.isFriends( castle->GetColor() ) ) {
                return Cursor::POINTER;
            }

            if ( castle->GetActualArmy().isValid() ) {
                return Cursor::DistanceThemes( Cursor::CURSOR_HERO_FIGHT, hero.getNumOfTravelDays( castle->GetIndex() ) );
            }

            return Cursor::DistanceThemes( Cursor::CURSOR_HERO_ACTION, hero.getNumOfTravelDays( castle->GetIndex() ) );
        }

        break;
    }

    case MP2::OBJ_HERO: {
        const Heroes * otherHero = tile.getHero();

        if ( otherHero ) {
            if ( otherHero->GetCenter() == hero.GetCenter() ) {
                return Cursor::HEROES;
            }

            if ( hero.GetColor() == otherHero->GetColor() ) {
                if ( HotKeyHoldEvent( Game::HotKeyEvent::WORLD_QUICK_SELECT_HERO ) ) {
                    return Cursor::HEROES;
                }
                const int cursor = Cursor::DistanceThemes( Cursor::CURSOR_HERO_MEET, hero.getNumOfTravelDays( tile.GetIndex() ) );

                return cursor != Cursor::POINTER ? cursor : Cursor::HEROES;
            }

            if ( hero.isFriends( otherHero->GetColor() ) ) {
                return Cursor::POINTER;
            }

            return Cursor::DistanceThemes( Cursor::CURSOR_HERO_FIGHT, hero.getNumOfTravelDays( tile.GetIndex() ) );
        }
        break;
    }

    case MP2::OBJ_BOAT:
        return Cursor::DistanceThemes( Cursor::CURSOR_HERO_BOAT, hero.getNumOfTravelDays( tile.GetIndex() ) );

    default:
        if ( MP2::isInGameActionObject( tile.getMainObjectType() ) ) {
            const bool isProtected
                = ( Maps::isTileUnderProtection( tile.GetIndex() ) || ( !hero.isFriends( getColorFromTile( tile ) ) && isCaptureObjectProtected( tile ) ) );

            return Cursor::DistanceThemes( isProtected ? Cursor::CURSOR_HERO_FIGHT : Cursor::CURSOR_HERO_ACTION, hero.getNumOfTravelDays( tile.GetIndex() ) );
        }

        if ( tile.isPassableFrom( Direction::CENTER, hero.isShipMaster(), false, hero.GetColor() ) ) {
            return Cursor::DistanceThemes( Maps::isTileUnderProtection( tile.GetIndex() ) ? Cursor::CURSOR_HERO_FIGHT : Cursor::CURSOR_HERO_MOVE,
                                           hero.getNumOfTravelDays( tile.GetIndex() ) );
        }

        break;
    }

    return Cursor::POINTER;
}

void Interface::AdventureMap::updateCursor( const int32_t tileIndex )
{
    Cursor::Get().SetThemes( GetCursorTileIndex( tileIndex ) );
}

int Interface::AdventureMap::GetCursorTileIndex( int32_t dstIndex )
{
    if ( !Maps::isValidAbsIndex( dstIndex ) ) {
        return Cursor::POINTER;
    }

    const Maps::Tile & tile = world.getTile( dstIndex );

    if ( tile.isFog( Settings::Get().CurrentColor() ) ) {
        return Cursor::POINTER;
    }

    switch ( GetFocusType() ) {
    case GameFocus::HEROES:
        return GetCursorFocusHeroes( *GetFocusHeroes(), tile );

    case GameFocus::CASTLE:
        return GetCursorFocusCastle( *GetFocusCastle(), tile );

    case GameFocus::UNSEL:
        return _getCursorNoFocus( tile );

    default:
        break;
    }

    return Cursor::POINTER;
}

fheroes2::GameMode Interface::AdventureMap::StartGame()
{
    Settings & conf = Settings::Get();

    const bool isAutoPlaytest{ conf.IsGameType( Game::TYPE_AUTO_PLAYTEST ) };
    const bool isAutoPlaytestAnimationEnabled{ fheroes2::AutoPlaytest::instance().isAnimationEnabled() };

    const bool isHotSeatGame = conf.IsGameType( Game::TYPE_HOTSEAT );
    if ( !isHotSeatGame ) {
        // It is not a Hot Seat (multiplayer) game so we set current color to the only human player.
        conf.SetCurrentColor( static_cast<PlayerColor>( Players::HumanColors() ) );
    }

    reset();

    _radar.Build();
    _radar.SetHide( true );
    _iconsPanel.hideIcons( ICON_ANY );
    _statusPanel.Reset();

    const PlayerColor persistentResourcePlayerColor = isAutoPlaytest ? PlayerColor::NONE : getPersistentResourcePlayerColor( conf );

    fheroes2::RPG::beginMap( persistentResourcePlayerColor );

    offlineProgressActivePlayerColor = persistentResourcePlayerColor;
    offlineSuspendStartedUnix = 0;
    offlineResumeElapsedSeconds = 0;
    offlineResumePending = false;

    OfflineProgressSummary offlineProgressSummary;
    if ( persistentResourcePlayerColor != PlayerColor::NONE ) {
        Kingdom & persistentKingdom = world.GetKingdom( persistentResourcePlayerColor );
        offlineProgressSummary = applyOfflineProgress( persistentKingdom );
    }

    // Prepare for render the whole game interface with adventure map filled with fog as it was not uncovered by 'updateMapFogDirections()'.
    redraw( REDRAW_GAMEAREA | REDRAW_RADAR | REDRAW_ICONS | REDRAW_BUTTONS | REDRAW_STATUS | REDRAW_BORDER );

    showOfflineProgressPopups( offlineProgressSummary );

    bool isLoadedFromSave = conf.LoadedGameVersion();
    bool skipTurns = isLoadedFromSave;

    // Set need of fade-in of game screen.
    Game::setDisplayFadeIn();

    GameOver::Result & gameResult = GameOver::Result::Get();
    fheroes2::GameMode res = fheroes2::GameMode::END_TURN;

    std::vector<Player *> sortedPlayers = conf.GetPlayers().getVector();
    std::sort( sortedPlayers.begin(), sortedPlayers.end(), SortPlayers );
    if ( !isLoadedFromSave || world.CountDay() == 1 ) {
        // Clear fog around heroes, castles and mines for all players when starting a new map or if the save was done at the first day.
        for ( Player * player : sortedPlayers ) {
            world.ClearFog( player->GetColor() );

            if ( isAutoPlaytest ) {
                // Every player for auto playtest mode is set as human player controlled by AI.
                player->SetControl( CONTROL_HUMAN );
                player->setAIAutoControlMode( true );
            }
        }
    }

    if ( !isHotSeatGame ) {
        // Fully update fog directions if there will be only one human player.
        Interface::GameArea::updateMapFogDirections();
    }

    if ( isAutoPlaytest && !isAutoPlaytestAnimationEnabled ) {
        // Move the game area to the center of the map and render it once only. We don't need to render the fog again.
        _gameArea.SetCenter( fheroes2::Point{ world.w() / 2, world.h() / 2 } );
        _gameArea.redrawOnlyFog( fheroes2::Display::instance() );
    }

    while ( res == fheroes2::GameMode::END_TURN ) {
        if ( !isLoadedFromSave ) {
            world.NewDay();
        }

        // Check if the game is over at the beginning of a new day
        res = gameResult.checkGameOver();

        if ( res != fheroes2::GameMode::CANCEL ) {
            break;
        }

        if ( isAutoPlaytest ) {
            auto & autoPlaytest = fheroes2::AutoPlaytest::instance();
            if ( static_cast<int32_t>( world.CountDay() ) > autoPlaytest.getMaxDaysInPlaythrough() ) {
                autoPlaytest.markTimeLimit();
                res = fheroes2::GameMode::MAIN_MENU;
                break;
            }

            Game::SetUpdateSoundsOnFocusUpdate( autoPlaytest.areEnvironmentSoundsEnabled() );
        }

        res = fheroes2::GameMode::END_TURN;

        for ( const Player * player : sortedPlayers ) {
            assert( player != nullptr );

            if ( skipTurns ) {
                // Game saves can only be performed during a human player's turn (including when it is under temporary AI control
                // in the case of a debug build), and human players always go first in the turn queue. If we skipped all the human
                // players and still haven't found the current player, then something is clearly wrong here.
                if ( player->GetControl() == CONTROL_AI ) {
                    break;
                }

                if ( !player->isColor( conf.CurrentColor() ) ) {
                    continue;
                }
            }

            // Player with a color equal to conf.CurrentColor() has been found, there is no need for further skips
            skipTurns = false;

            const PlayerColor playerColor = player->GetColor();
            Kingdom & kingdom = world.GetKingdom( playerColor );

            if ( kingdom.isPlay() ) {
                DEBUG_LOG( DBG_GAME, DBG_INFO, world.DateString() << ", color: " << Color::String( playerColor ) << ", resource: " << kingdom.GetFunds().String() )

                _radar.SetHide( true );
                _radar.SetRedraw( REDRAW_RADAR_CURSOR );

                switch ( kingdom.GetControl() ) {
                case CONTROL_HUMAN:
                    // Reset environment sounds and music theme at the beginning of the human turn
                    AudioManager::ResetAudio();

                    conf.SetCurrentColor( playerColor );

                    if ( isHotSeatGame ) {
                        // Move the area to the center of the map to avoid showing map borders.
                        _gameArea.SetCenter( fheroes2::Point{ world.w() / 2, world.h() / 2 } );

                        if ( conf.getInterfaceType() == InterfaceType::DYNAMIC && _isCurrentInterfaceEvil != conf.isEvilInterfaceEnabled() ) {
                            reset();
                        }

                        _iconsPanel.hideIcons( ICON_ANY );
                        _statusPanel.Reset();

                        // Fully update fog directions in Hot Seat mode to cover the map with fog on player change.
                        // TODO: Cover the Adventure map area with fog sprites without rendering the "Game Area" for player change.
                        Maps::updateFogDirectionsInArea( { 0, 0 }, { world.w(), world.h() }, 0 );

                        redraw( REDRAW_GAMEAREA | REDRAW_ICONS | REDRAW_BUTTONS | REDRAW_STATUS | REDRAW_BORDER );

                        validateFadeInAndRender();

                        // Reset the music after closing the dialog
                        const AudioManager::MusicRestorer musicRestorer;

                        AudioManager::PlayMusic( MUS::NEW_MONTH, Music::PlaybackMode::PLAY_ONCE );

                        Game::DialogPlayers( playerColor, "", _( "%{color} player's turn." ) );
                    }

                    kingdom.ActionBeforeTurn();

                    _iconsPanel.showIcons( ICON_ANY );
                    _iconsPanel.setRedraw();

                    res = HumanTurn( isLoadedFromSave );

                    // Skip resetting Audio after winning scenario because MUS::VICTORY should continue playing.
                    if ( res == fheroes2::GameMode::HIGHSCORES_STANDARD ) {
                        break;
                    }

                    // Reset environment sounds and music theme at the end of the human turn.
                    AudioManager::ResetAudio();

                    break;
                case CONTROL_AI:
                    // TODO: remove this temporary assertion
                    assert( res == fheroes2::GameMode::END_TURN );

                    Cursor::Get().SetThemes( Cursor::WAIT );

                    conf.SetCurrentColor( playerColor );

                    _statusPanel.Reset();

                    if ( player->isAIAutoControlMode() && ( !isAutoPlaytest || isAutoPlaytestAnimationEnabled ) ) {
                        // If player gave control to AI we show the radar image and update it fully at the start of player's turn.
                        _radar.SetHide( false );
                        _radar.SetRedraw( REDRAW_RADAR );

                        // We also update the state of castles and heroes to have better visibility of the kingdom state.
                        _iconsPanel.resetIcons( ICON_ANY );
                        _iconsPanel.showIcons( ICON_ANY );
                        _iconsPanel.setRedraw();
                    }
                    else {
                        _statusPanel.SetState( StatusType::STATUS_AITURN );
                    }

                    redraw( 0 );
                    validateFadeInAndRender();

                    // In Hot Seat mode there could be different alliances so we have to update fog directions for some cases.
                    if ( isHotSeatGame || ( isAutoPlaytest && isAutoPlaytestAnimationEnabled ) ) {
                        Maps::updateFogDirectionsInArea( { 0, 0 }, { world.w(), world.h() }, hotSeatAIFogColors( player ) );
                    }

                    if ( !isLoadedFromSave ) {
                        kingdom.ActionNewDayResourceUpdate( nullptr );
                    }

                    kingdom.ActionBeforeTurn();

                    if ( !isAutoPlaytest && !isLoadedFromSave && player->isAIAutoControlMode() && conf.isAutoSaveAtBeginningOfTurnEnabled() ) {
                        // This is a human player which gave control to AI so we need to do autosave here.
                        Game::AutoSave();
                    }

                    res = AI::Planner::Get().KingdomTurn( kingdom );
                    // This function must return only game state related values.
                    assert( res != fheroes2::GameMode::CANCEL );

                    if ( !isAutoPlaytest && !isLoadedFromSave && player->isAIAutoControlMode() && !conf.isAutoSaveAtBeginningOfTurnEnabled() ) {
                        // This is a human player which gave control to AI so we need to do autosave here.
                        Game::AutoSave();
                    }
                    if ( isAutoPlaytest && kingdom.GetControl() != CONTROL_AI ) {
                        res = fheroes2::GameMode::MAIN_MENU;
                        break;
                    }

                    break;
                default:
                    // So far no other player type is supported so this should not happen.
                    assert( 0 );
                    break;
                }

                if ( !isAutoPlaytest && offlineResumePending && playerColor == persistentResourcePlayerColor ) {
                    const OfflineProgressSummary resumeSummary = consumePendingOfflineResumeProgress( kingdom );
                    showOfflineProgressPopups( resumeSummary );
                }

                if ( !isAutoPlaytest && playerColor == persistentResourcePlayerColor ) {
                    persistOfflineProgressSnapshot( kingdom );
                }

                if ( res != fheroes2::GameMode::END_TURN ) {
                    break;
                }

                // Check if the game is over after each player's turn
                res = gameResult.checkGameOver();

                if ( res != fheroes2::GameMode::CANCEL ) {
                    break;
                }

                res = fheroes2::GameMode::END_TURN;
            }

            // Reset this after potential HumanTurn() call, but regardless of whether current kingdom
            // is vanquished - next alive kingdom should start a new day from scratch
            isLoadedFromSave = false;
        }

        // We went through all the players, but the current player from the save file is still not found,
        // something is clearly wrong here
        if ( skipTurns ) {
            DEBUG_LOG( DBG_GAME, DBG_WARN,
                       "the current player from the save file was not found"
                           << ", player color: " << Color::String( conf.CurrentColor() ) )

            res = fheroes2::GameMode::MAIN_MENU;
        }

        // Don't carry the current player color to the next turn.
        conf.SetCurrentColor( PlayerColor::NONE );
    }

    // Capture the final offline-income state when a victory, defeat, or menu transition
    // ends the map before another normal end-of-turn snapshot can happen.
    if ( !isAutoPlaytest && persistentResourcePlayerColor != PlayerColor::NONE ) {
        Kingdom & persistentKingdom = world.GetKingdom( persistentResourcePlayerColor );

        if ( offlineResumePending ) {
            consumePendingOfflineResumeProgress( persistentKingdom );
        }

        // If the app is still backgrounded, the focus-loss snapshot is the correct start of the
        // next offline interval. Do not replace it with a later shutdown timestamp.
        if ( offlineSuspendStartedUnix == 0 ) {
            persistOfflineProgressSnapshot( persistentKingdom );
        }
    }

    offlineProgressActivePlayerColor = PlayerColor::NONE;
    fheroes2::RPG::endMap();
    offlineSuspendStartedUnix = 0;
    offlineResumeElapsedSeconds = 0;
    offlineResumePending = false;

    // If we are here, the res value should never be fheroes2::GameMode::END_TURN
    assert( res != fheroes2::GameMode::END_TURN );

    Game::setDisplayFadeIn();

    // Do not use fade-out effect when exiting to Highscores screen as in this case name input dialog will be rendered next
    // or when running in auto playtest mode.
    if ( res != fheroes2::GameMode::HIGHSCORES_STANDARD && !isAutoPlaytest ) {
        fheroes2::fadeOutDisplay();
    }

    return res;
}

fheroes2::GameMode Interface::AdventureMap::HumanTurn( const bool isLoadedFromSave )
{
    if ( isLoadedFromSave ) {
        updateFocus();
    }
    else {
        ResetFocus( GameFocus::FIRSTHERO, false );
    }

    _radar.SetHide( false );
    _statusPanel.Reset();
    _gameArea.SetUpdateCursor();

    const Settings & conf = Settings::Get();
    if ( conf.IsGameType( Game::TYPE_HOTSEAT ) ) {
        // TODO: Cache fog directions for all Human players in array to not perform full update at every turn start.

        // Fully update fog directions at the start of player's move in Hot Seat mode as the previous move could be done by opposing player.
        Interface::GameArea::updateMapFogDirections();
    }

    redraw( REDRAW_GAMEAREA | REDRAW_RADAR | REDRAW_ICONS | REDRAW_BUTTONS | REDRAW_STATUS | REDRAW_BORDER );

    validateFadeInAndRender();

    Kingdom & myKingdom = world.GetKingdom( conf.CurrentColor() );

    if ( !isLoadedFromSave ) {
        if ( 1 < world.CountWeek() && world.BeginWeek() ) {
            ShowNewWeekDialog();
        }

        myKingdom.ActionNewDayResourceUpdate( []( const EventDate & event, const Funds & funds ) {
            const auto & language = Settings::Get().getCurrentMapInfo().getSupportedLanguage();

            if ( funds.GetValidItemsCount() ) {
                fheroes2::showResourceMessage( fheroes2::Text( event.title, fheroes2::FontType::normalYellow(), language ),
                                               fheroes2::Text( event.message, fheroes2::FontType::normalWhite(), language ), Dialog::OK, funds );
            }
            else if ( !event.message.empty() ) {
                const fheroes2::Text header( event.title, fheroes2::FontType::normalYellow(), language );
                const fheroes2::Text body( event.message, fheroes2::FontType::normalWhite(), language );
                fheroes2::showMessage( header, body, Dialog::OK );
            }
        } );

        // The amount of the kingdom resources has changed, the status panel needs to be updated
        redraw( REDRAW_STATUS );
        fheroes2::Display::instance().render();

        if ( conf.isAutoSaveAtBeginningOfTurnEnabled() ) {
            Game::AutoSave();
        }
    }

    GameOver::Result & gameResult = GameOver::Result::Get();

    // Check if the game is over at the beginning of each human-controlled player's turn
    fheroes2::GameMode res = gameResult.checkGameOver();

    const VecCastles & myCastles = myKingdom.GetCastles();
    if ( res == fheroes2::GameMode::CANCEL && myCastles.empty() ) {
        ShowWarningLostTownsDialog();
    }

    int fastScrollRepeatCount = 0;
    const int fastScrollStartThreshold = 2;

    bool isHeroMoving = false;
    bool stopHero = false;

    int heroAnimationFrameCount = 0;
    fheroes2::Point heroAnimationOffset;
    int heroAnimationSpriteId = 0;

    const std::vector<Game::DelayType> delayTypes = { Game::DelayType::CURRENT_HERO_DELAY, Game::DelayType::MAPS_DELAY };

    LocalEvent & le = LocalEvent::Get();
    Cursor & cursor = Cursor::Get();

    // Resets the cursor to a regular pointer and instructs the game area to update the cursor at the first opportunity
    const auto resetCursor = [this, &cursor]() {
        cursor.SetThemes( Cursor::POINTER );

        _gameArea.SetUpdateCursor();
    };

    // Resets the cursor to a regular pointer and instructs the game area to update the cursor at the first opportunity,
    // but only if the game area does not need to be scrolled
    const auto resetCursorIfNoNeedToScroll = [this, &resetCursor]() {
        if ( _gameArea.NeedScroll() ) {
            return;
        }

        resetCursor();
    };

    while ( res == fheroes2::GameMode::CANCEL ) {
        if ( !le.HandleEvents( Game::isDelayNeeded( delayTypes ), true ) ) {
            if ( Game::processExitEvent() == fheroes2::GameMode::QUIT_GAME ) {
                res = fheroes2::GameMode::QUIT_GAME;

                break;
            }

            continue;
        }

        if ( offlineResumePending && myKingdom.GetColor() == offlineProgressActivePlayerColor ) {
            const OfflineProgressSummary resumeSummary = consumePendingOfflineResumeProgress( myKingdom );
            showOfflineProgressPopups( resumeSummary );

            _iconsPanel.resetIcons( ICON_ANY );
            _iconsPanel.showIcons( ICON_ANY );
            redraw( REDRAW_GAMEAREA | REDRAW_ICONS | REDRAW_STATUS );
            validateFadeInAndRender();
        }

        {
            const Player * player = Players::Get( myKingdom.GetColor() );
            assert( player != nullptr );

            // Control has just been transferred to AI, end the turn immediately.
            if ( player->isAIAutoControlMode() ) {
                return fheroes2::GameMode::END_TURN;
            }
        }

        // Pending timer events
        _statusPanel.TimerEventProcessing();

        if ( isHeroMoving ) {
            // Hero is moving, set the appropriate cursor
            cursor.SetThemes( Cursor::WAIT );

            // If the hero is currently moving, pressing any key or mouse button should stop him. No other actions are possible at this time.
            if ( le.isAnyKeyPressed() || le.MouseClickLeft() || le.isMouseRightButtonPressed() ) {
                stopHero = true;
            }
        }
        else {
            // Hotkeys
            if ( le.isAnyKeyPressed() ) {
                // Adventure map control
                if ( HotKeyPressEvent( Game::HotKeyEvent::GLOBAL_APP_QUIT ) || HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_CANCEL ) ) {
                    res = Game::processExitEvent();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_TRANSFER_CONTROL_TO_AI ) ) {
                    Player * player = Settings::Get().GetPlayers().GetCurrent();
                    if ( player != nullptr && player->isControlHuman()
                         && fheroes2::showStandardTextMessage(
                                _( "Auto-play" ),
                                _( "Enable auto-play? The AI controls your adventure-map turns and battles. Battles are shown in full at normal speed." ),
                                Dialog::YES | Dialog::NO )
                                == Dialog::YES ) {
                        player->setAIAutoControlMode( true );
                        return fheroes2::GameMode::END_TURN;
                    }
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_RPG_MENU ) ) {
                    fheroes2::RPG::showMenu();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_END_TURN ) ) {
                    res = EventEndTurn();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_NEXT_HERO ) ) {
                    EventNextHero();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_NEXT_TOWN ) ) {
                    EventNextTown();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::MAIN_MENU_NEW_GAME ) ) {
                    res = EventNewGame();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SAVE_GAME ) ) {
                    EventSaveGame();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_QUICK_SAVE ) ) {
                    if ( !Game::QuickSave() ) {
                        fheroes2::showStandardTextMessage( "", _( "There was an issue during saving." ), Dialog::OK );
                    }
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::MAIN_MENU_LOAD_GAME ) ) {
                    res = EventLoadGame();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_FILE_OPTIONS ) ) {
                    res = EventFileDialog();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_ADVENTURE_OPTIONS ) ) {
                    res = EventAdventureDialog();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SYSTEM_OPTIONS ) ) {
                    EventSystemDialog();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_PUZZLE_MAP ) ) {
                    EventPuzzleMaps();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SCENARIO_INFORMATION ) ) {
                    res = EventScenarioInformation();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_CAST_SPELL ) ) {
                    EventCastSpell();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_KINGDOM_SUMMARY ) ) {
                    EventKingdomInfo();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_VIEW_WORLD ) ) {
                    EventViewWorld();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_TOGGLE_CONTROL_PANEL ) ) {
                    EventSwitchShowControlPanel();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_TOGGLE_RADAR ) ) {
                    EventSwitchShowRadar();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_TOGGLE_BUTTONS ) ) {
                    EventSwitchShowButtons();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_TOGGLE_STATUS ) ) {
                    EventSwitchShowStatus();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_TOGGLE_ICONS ) ) {
                    EventSwitchShowIcons();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_START_HERO_MOVEMENT ) ) {
                    res = EventHeroMovement();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_DIG_ARTIFACT ) ) {
                    res = EventDigArtifact();
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SLEEP_HERO ) ) {
                    EventSwitchHeroSleeping();
                }
                // Hero movement control
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_LEFT ) ) {
                    EventKeyArrowPress( Direction::LEFT );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_RIGHT ) ) {
                    EventKeyArrowPress( Direction::RIGHT );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_UP ) ) {
                    EventKeyArrowPress( Direction::TOP );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_DOWN ) ) {
                    EventKeyArrowPress( Direction::BOTTOM );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_UP_LEFT ) ) {
                    EventKeyArrowPress( Direction::TOP_LEFT );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_UP_RIGHT ) ) {
                    EventKeyArrowPress( Direction::TOP_RIGHT );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_DOWN_LEFT ) ) {
                    EventKeyArrowPress( Direction::BOTTOM_LEFT );
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_DOWN_RIGHT ) ) {
                    EventKeyArrowPress( Direction::BOTTOM_RIGHT );
                }
                // Adventure map scrolling control
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SCROLL_LEFT ) ) {
                    if ( !_gameArea.isDragScroll() && conf.ScrollSpeed() != SCROLL_SPEED_NONE ) {
                        _gameArea.SetScroll( SCROLL_LEFT );
                    }
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SCROLL_RIGHT ) ) {
                    if ( !_gameArea.isDragScroll() && conf.ScrollSpeed() != SCROLL_SPEED_NONE ) {
                        _gameArea.SetScroll( SCROLL_RIGHT );
                    }
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SCROLL_UP ) ) {
                    if ( !_gameArea.isDragScroll() && conf.ScrollSpeed() != SCROLL_SPEED_NONE ) {
                        _gameArea.SetScroll( SCROLL_TOP );
                    }
                }
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_SCROLL_DOWN ) ) {
                    if ( !_gameArea.isDragScroll() && conf.ScrollSpeed() != SCROLL_SPEED_NONE ) {
                        _gameArea.SetScroll( SCROLL_BOTTOM );
                    }
                }
                // Default action
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_DEFAULT_ACTION ) ) {
                    res = EventDefaultAction();
                }
                // Open the focused object (hero or castle)
                else if ( HotKeyPressEvent( Game::HotKeyEvent::WORLD_OPEN_FOCUS ) ) {
                    EventOpenFocus();
                }
                else if ( HotKeyHoldEvent( Game::HotKeyEvent::WORLD_QUICK_SELECT_HERO ) ) {
                    const int32_t index = _gameArea.GetValidTileIdFromPoint( le.getMouseCursorPos() );

                    // This tells us that this is a hero owned by the current player and that they can meet, so we switch to the helmet cursor.
                    if ( cursor.Themes() == Cursor::CURSOR_HERO_MEET ) {
                        cursor.SetThemes( GetCursorTileIndex( index ) );
                    }

                    if ( le.MouseClickLeft() ) {
                        EventSwitchFocusedHero( index );
                    }
                }
            }

            if ( res != fheroes2::GameMode::CANCEL ) {
                break;
            }

            const bool isHiddenInterface = conf.isHideInterfaceEnabled();

            // When processing events in the "no interface" mode, care should be taken about the order in which events are handled by different
            // UI elements, since they may overlap. The order of their rendering on the screen is as follows: the status panel is the topmost,
            // followed by the buttons panel, followed by the icons panel, followed by the radar, followed by the control panel, and under all
            // of them there is a game area. It is necessary to process events in exactly the same order in which all these UI elements overlap.
            //
            // When the mouse is captured by any UI element, events should not be handled by other UI elements.
            //
            // Mouse is captured by the status panel
            if ( _statusPanel.isMouseCaptured() ) {
                resetCursor();

                _statusPanel.QueueEventProcessing();
            }
            // Mouse is captured by the buttons panel
            else if ( _buttonsPanel.isMouseCaptured() ) {
                resetCursor();

                res = _buttonsPanel.queueEventProcessing();
            }
            // Mouse is captured by the icons panel
            else if ( _iconsPanel.isMouseCaptured() ) {
                resetCursor();

                _iconsPanel.queueEventProcessing();
            }
            // Mouse is captured by radar
            else if ( _radar.isMouseCaptured() ) {
                resetCursor();

                _radar.QueueEventProcessing();
            }
            // Mouse is captured by the game area for scrolling by dragging
            else if ( _gameArea.isDragScroll() ) {
                _gameArea.QueueEventProcessing();
            }
            else {
                if ( fheroes2::Cursor::isFocusActive() && conf.ScrollSpeed() != SCROLL_SPEED_NONE ) {
                    int scrollDirection = SCROLL_NONE;

                    if ( isScrollLeft( le.getMouseCursorPos() ) ) {
                        scrollDirection |= SCROLL_LEFT;
                    }
                    else if ( isScrollRight( le.getMouseCursorPos() ) ) {
                        scrollDirection |= SCROLL_RIGHT;
                    }
                    if ( isScrollTop( le.getMouseCursorPos() ) ) {
                        scrollDirection |= SCROLL_TOP;
                    }
                    else if ( isScrollBottom( le.getMouseCursorPos() ) ) {
                        scrollDirection |= SCROLL_BOTTOM;
                    }

                    if ( scrollDirection != SCROLL_NONE && _gameArea.isFastScrollEnabled() ) {
                        if ( Game::validateAnimationDelay( Game::DelayType::SCROLL_START_DELAY ) && fastScrollRepeatCount < fastScrollStartThreshold ) {
                            ++fastScrollRepeatCount;
                        }

                        if ( fastScrollRepeatCount >= fastScrollStartThreshold ) {
                            _gameArea.SetScroll( scrollDirection );
                        }
                    }
                    else {
                        fastScrollRepeatCount = 0;
                    }
                }
                else {
                    fastScrollRepeatCount = 0;
                }

                // Re-enable fast scrolling if the cursor movement indicates the need
                if ( !_gameArea.isFastScrollEnabled() && _gameArea.mouseIndicatesFastScroll( le.getMouseCursorPos() ) ) {
                    _gameArea.setFastScrollStatus( true );
                }

                // Cursor is over the status panel
                if ( ( !isHiddenInterface || conf.ShowStatus() ) && le.isMouseCursorPosInArea( _statusPanel.GetRect() ) ) {
                    resetCursorIfNoNeedToScroll();

                    _statusPanel.QueueEventProcessing();
                }
                // Cursor is over the buttons panel
                else if ( ( !isHiddenInterface || conf.ShowButtons() ) && le.isMouseCursorPosInArea( _buttonsPanel.GetRect() ) ) {
                    resetCursorIfNoNeedToScroll();

                    res = _buttonsPanel.queueEventProcessing();
                }
                // Cursor is over the icons panel
                else if ( ( !isHiddenInterface || conf.ShowIcons() ) && le.isMouseCursorPosInArea( _iconsPanel.GetRect() ) ) {
                    resetCursorIfNoNeedToScroll();

                    _iconsPanel.queueEventProcessing();
                }
                // Cursor is over the radar
                else if ( ( !isHiddenInterface || conf.ShowRadar() ) && le.isMouseCursorPosInArea( _radar.GetRect() ) ) {
                    resetCursorIfNoNeedToScroll();

                    _radar.QueueEventProcessing();
                }
                // Cursor is over the control panel
                else if ( isHiddenInterface && conf.ShowControlPanel() && le.isMouseCursorPosInArea( _controlPanel.GetArea() ) ) {
                    resetCursorIfNoNeedToScroll();

                    res = _controlPanel.QueueEventProcessing();
                }
                else if ( !_gameArea.NeedScroll() ) {
                    // Cursor is over the game area
                    if ( le.isMouseCursorPosInArea( _gameArea.GetROI() ) ) {
                        _gameArea.QueueEventProcessing();
                    }
                    // Cursor is somewhere else
                    else {
                        resetCursor();
                    }
                }
            }

            if ( res != fheroes2::GameMode::CANCEL ) {
                break;
            }
        }

        // Animation of the hero's movement
        if ( Game::validateAnimationDelay( Game::DelayType::CURRENT_HERO_DELAY ) ) {
            Heroes * hero = GetFocusHeroes();

            if ( hero ) {
                bool resetHeroSprite = false;
                if ( heroAnimationFrameCount > 0 ) {
                    const int32_t heroMovementSkipValue = Game::HumanHeroAnimSpeedMultiplier();

                    _gameArea.ShiftCenter( { heroAnimationOffset.x * heroMovementSkipValue, heroAnimationOffset.y * heroMovementSkipValue } );
                    _gameArea.SetRedraw();

                    if ( heroAnimationOffset != fheroes2::Point() ) {
                        Game::EnvironmentSoundMixer();
                    }

                    heroAnimationFrameCount -= heroMovementSkipValue;
                    if ( ( heroAnimationFrameCount & 0x3 ) == 0 ) { // % 4
                        hero->SetSpriteIndex( heroAnimationSpriteId );

                        if ( heroAnimationFrameCount == 0 ) {
                            resetHeroSprite = true;
                        }
                        else {
                            ++heroAnimationSpriteId;
                        }
                    }
                    const int offsetStep = ( ( 4 - ( heroAnimationFrameCount & 0x3 ) ) & 0x3 ); // % 4
                    hero->SetOffset( { heroAnimationOffset.x * offsetStep, heroAnimationOffset.y * offsetStep } );
                }

                if ( heroAnimationFrameCount == 0 ) {
                    if ( resetHeroSprite ) {
                        hero->SetSpriteIndex( heroAnimationSpriteId - 1 );
                    }

                    if ( hero->isMoveEnabled() ) {
                        if ( hero->Move( 10 == conf.HeroesMoveSpeed() ) ) {
                            // Do not generate a frame as we are going to do it later.
                            Interface::AdventureMap::RedrawLocker redrawLocker( Interface::AdventureMap::Get() );

                            _gameArea.SetCenter( hero->GetCenter() );

                            if ( stopHero ) {
                                hero->SetMove( false );

                                stopHero = false;
                            }
                        }
                        else {
                            // Don't waste resources if there is no movement
                            if ( const fheroes2::Point movement( hero->MovementDirection() ); movement != fheroes2::Point() ) {
                                // Do not generate a frame as we are going to do it later.
                                Interface::AdventureMap::RedrawLocker redrawLocker( Interface::AdventureMap::Get() );

                                const int32_t heroMovementSkipValue = Game::HumanHeroAnimSpeedMultiplier();

                                heroAnimationOffset = movement;
                                _gameArea.ShiftCenter( movement );

                                heroAnimationFrameCount = 32 - heroMovementSkipValue;
                                heroAnimationSpriteId = hero->GetSpriteIndex();
                                if ( heroMovementSkipValue < 4 ) {
                                    hero->SetSpriteIndex( heroAnimationSpriteId - 1 );
                                    hero->SetOffset( { heroAnimationOffset.x * heroMovementSkipValue, heroAnimationOffset.y * heroMovementSkipValue } );
                                }
                                else {
                                    ++heroAnimationSpriteId;
                                }
                            }

                            _gameArea.SetRedraw();
                        }

                        // Update the hero's move status.
                        isHeroMoving = hero->isMoveEnabled();

                        if ( hero->isAction() ) {
                            // The action can not be performed while moving, only after the move is ended.
                            assert( !isHeroMoving );

                            // Check if the game is over after the hero's action.
                            res = gameResult.checkGameOver();

                            hero->ResetAction();
                        }

                        if ( !isHeroMoving ) {
                            // Reset the 'ENABLEMOVE' state on this loop to properly update the cursor in this frame and not in the next.
                            hero->SetMove( false );

                            // During the action and/or movement the adventure map and/or cursor position may have changed, so we should update the cursor image.
                            if ( Game::isFadeInNeeded() ) {
                                // Do not change cursor right now because fade-in is scheduled.
                                _gameArea.SetUpdateCursor();
                            }
                            else {
                                if ( le.isMouseCursorPosInArea( _gameArea.GetROI() ) ) {
                                    // We do not use '_gameArea.SetUpdateCursor()' here because we need to update the cursor before rendering the current frame
                                    // and '_gameArea.QueueEventProcessing()' was called earlier in this loop and will only be able to update the cursor in the
                                    // next loop for the next frame.
                                    cursor.SetThemes( GetCursorTileIndex( _gameArea.GetValidTileIdFromPoint( le.getMouseCursorPos() ) ) );
                                }
                                else {
                                    // When the cursor is not over the game area we use the Pointer cursor.
                                    resetCursor();
                                }
                            }
                        }
                    }
                    else {
                        hero->SetMove( false );

                        isHeroMoving = false;
                        stopHero = false;

                        _gameArea.SetUpdateCursor();
                    }
                }
            }
            else {
                isHeroMoving = false;
                stopHero = false;
            }
        }

        // Scrolling the game area
        if ( !isHeroMoving ) {
            if ( _gameArea.NeedScroll() && Game::validateAnimationDelay( Game::DelayType::SCROLL_DELAY ) ) {
                assert( !_gameArea.isDragScroll() );

                if ( isScrollLeft( le.getMouseCursorPos() ) || isScrollRight( le.getMouseCursorPos() ) || isScrollTop( le.getMouseCursorPos() )
                     || isScrollBottom( le.getMouseCursorPos() ) ) {
                    cursor.SetThemes( _gameArea.GetScrollCursor() );
                }

                _gameArea.Scroll();

                setRedraw( REDRAW_GAMEAREA | REDRAW_RADAR_CURSOR );
            }
            else if ( _gameArea.needDragScrollRedraw() || _gameArea.updateInertia() ) {
                setRedraw( REDRAW_GAMEAREA | REDRAW_RADAR_CURSOR );
            }
        }

        // Check that the kingdom is not vanquished yet (has at least one hero or castle).
        if ( res == fheroes2::GameMode::CANCEL && !myKingdom.isPlay() ) {
            res = fheroes2::GameMode::END_TURN;
        }

        // Render map only if the turn is not over.
        if ( res != fheroes2::GameMode::CANCEL ) {
            break;
        }

        // Map objects animation
        if ( Game::validateAnimationDelay( Game::DelayType::MAPS_DELAY ) ) {
            Game::updateAdventureMapAnimationIndex();

            _gameArea.SetRedraw();
        }

        if ( needRedraw() ) {
            redraw( 0 );

            // If this assertion blows up it means that we are holding a RedrawLocker lock for rendering which should not happen.
            assert( getRedrawMask() == 0 );

            validateFadeInAndRender();
        }
    }

    if ( res == fheroes2::GameMode::END_TURN ) {
        if ( GetFocusHeroes() ) {
            GetFocusHeroes()->ShowPath( false );

            setRedraw( REDRAW_GAMEAREA );
        }

        if ( myKingdom.isPlay() ) {
            // These warnings should be shown at the end of the turn
            if ( myCastles.empty() ) {
                const uint32_t lostTownDays = myKingdom.GetLostTownDays();

                if ( lostTownDays > Game::GetLostTownDays() ) {
                    Game::DialogPlayers(
                        conf.CurrentColor(), _( "Beware!" ),
                        _( "%{color} player, you have lost your last town. If you do not conquer another town in the next week, you will be eliminated." ) );
                }
                else if ( lostTownDays == 1 ) {
                    Game::DialogPlayers( conf.CurrentColor(), _( "Defeat!" ), _( "%{color} player, your heroes abandon you, and you are banished from this land." ) );
                }
            }

            if ( !conf.isAutoSaveAtBeginningOfTurnEnabled() ) {
                Game::AutoSave();
            }
        }
    }

    return res;
}

void Interface::AdventureMap::mouseCursorAreaClickLeft( const int32_t tileIndex )
{
    Heroes * focusedHero = GetFocusHeroes();
    assert( focusedHero == nullptr || !focusedHero->Modes( Heroes::ENABLEMOVE ) );

    const Maps::Tile & tile = world.getTile( tileIndex );

    switch ( Cursor::WithoutDistanceThemes( Cursor::Get().Themes() ) ) {
    case Cursor::HEROES: {
        Heroes * otherHero = tile.getHero();
        if ( otherHero == nullptr ) {
            break;
        }

        if ( focusedHero == nullptr || focusedHero != otherHero ) {
            SetFocus( otherHero, false );
            RedrawFocus();
        }
        else {
            Game::OpenHeroesDialog( *otherHero, true, true );
        }

        break;
    }

    case Cursor::CASTLE: {
        const MP2::MapObjectType objectType = tile.getMainObjectType();
        if ( MP2::OBJ_NON_ACTION_CASTLE != objectType && MP2::OBJ_CASTLE != objectType ) {
            break;
        }

        Castle * otherCastle = world.getCastle( tile.GetCenter() );
        if ( otherCastle == nullptr ) {
            break;
        }

        const Castle * focusedCastle = GetFocusCastle();

        if ( focusedCastle == nullptr || focusedCastle != otherCastle ) {
            SetFocus( otherCastle );
            RedrawFocus();
        }
        else {
            Game::OpenCastleDialog( *otherCastle );
        }

        break;
    }
    case Cursor::CURSOR_HERO_FIGHT:
    case Cursor::CURSOR_HERO_MOVE:
    case Cursor::CURSOR_HERO_BOAT:
    case Cursor::CURSOR_HERO_ANCHOR:
    case Cursor::CURSOR_HERO_MEET:
    case Cursor::CURSOR_HERO_ACTION:
    case Cursor::CURSOR_HERO_BOAT_ACTION: {
        if ( focusedHero == nullptr ) {
            break;
        }

        ShowPathOrStartMoveHero( focusedHero, tileIndex );

        break;
    }

    default:
        break;
    }
}

void Interface::AdventureMap::mouseCursorAreaPressRight( const int32_t tileIndex ) const
{
#ifndef NDEBUG
    const Heroes * focusedHero = GetFocusHeroes();
#endif
    assert( focusedHero == nullptr || !focusedHero->Modes( Heroes::ENABLEMOVE ) );

    const Settings & conf = Settings::Get();
    const Maps::Tile & tile = world.getTile( tileIndex );

    DEBUG_LOG( DBG_DEVEL, DBG_INFO, '\n' << tile.String() )

    if ( !IS_DEVEL() && tile.isFog( conf.CurrentColor() ) ) {
        Dialog::QuickInfo( tile );
    }
    else {
        switch ( tile.getMainObjectType() ) {
        case MP2::OBJ_NON_ACTION_CASTLE:
        case MP2::OBJ_CASTLE: {
            const Castle * castle = world.getCastle( tile.GetCenter() );

            if ( castle ) {
                Dialog::QuickInfo( *castle );
            }
            else {
                Dialog::QuickInfo( tile );
            }

            break;
        }

        case MP2::OBJ_HERO: {
            const Heroes * heroes = tile.getHero();

            if ( heroes ) {
                Dialog::QuickInfo( *heroes );
            }

            break;
        }

        default:
            Dialog::QuickInfo( tile );
            break;
        }
    }
}

void Interface::AdventureMap::mouseCursorAreaLongPressLeft( const int32_t tileIndex )
{
    EventSwitchFocusedHero( tileIndex );
}
