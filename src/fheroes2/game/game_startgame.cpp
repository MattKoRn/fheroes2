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
#include "rand.h"
#include "resource.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "tools.h"
#include "translations.h"
#include "ui_dialog.h"
#include "ui_language.h"
#include "ui_text.h"
#include "ui_tool.h"
#include "week.h"
#include "world.h"

namespace
{
    constexpr int64_t offlineSecondsPerDay{ 24 * 60 * 60 };
    constexpr char offlineProgressFileName[]{ "offline_progress.dat" };

    using FundsMember = int32_t Funds::*;

    constexpr std::array<FundsMember, 7> offlineFundMembers{ &Funds::wood, &Funds::mercury, &Funds::ore, &Funds::sulfur,
                                                             &Funds::crystal, &Funds::gems, &Funds::gold };

    constexpr std::array<int, 7> offlineResourceTypes{ Resource::WOOD, Resource::MERCURY, Resource::ORE, Resource::SULFUR,
                                                       Resource::CRYSTAL, Resource::GEMS, Resource::GOLD };

    constexpr size_t persistentCreatureTypeCount = static_cast<size_t>( Monster::MONSTER_COUNT );
    using PersistentCreatureRoster = std::array<uint64_t, persistentCreatureTypeCount>;

    struct OfflineProgressData
    {
        int64_t lastSeenUnix{ 0 };
        Funds resources;
        Funds dailyIncome;
        std::array<int64_t, 7> carry{};
        uint32_t homecomingStreak{ 0 };
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
        PersistentCreatureRoster creatureRoster{};
        PersistentCreatureRoster creatureReserve{};
        PersistentCreatureRoster creatureRecruitCarry{};
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
        int rankUpPercent{ 0 };
        int rankUpResource{ Resource::UNKNOWN };
        int32_t rankUpBonus{ 0 };
        int contractId{ -1 };
        uint64_t contractProgressBefore{ 0 };
        uint64_t contractProgressAfter{ 0 };
        uint64_t contractTarget{ 0 };
        bool contractCompleted{ false };
        int contractRewardResource{ Resource::UNKNOWN };
        int32_t contractRewardBonus{ 0 };
        uint32_t contractsCompleted{ 0 };
        int nextContractId{ -1 };
        uint64_t nextContractTarget{ 0 };
        uint32_t treasureFragmentsBefore{ 0 };
        uint32_t treasureFragmentsEarned{ 0 };
        uint32_t treasureFragmentsAfter{ 0 };
        uint32_t treasureMapsCompleted{ 0 };
        bool treasureMapCompleted{ false };
        int treasureMapId{ -1 };
        int treasureRewardResource{ Resource::UNKNOWN };
        int32_t treasureRewardBonus{ 0 };
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
        uint64_t recruitedCreatures{ 0 };
        uint32_t recruitedStacks{ 0 };
        uint32_t recruitmentSettlements{ 0 };
        Funds recruitmentSpent;
        bool showPopup{ false };
    };

    int64_t getCurrentUnixTime()
    {
        return std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() ).count();
    }

    std::string getOfflineProgressFilePath()
    {
        return System::concatPath( System::GetConfigDirectory( "fheroes2" ), offlineProgressFileName );
    }

    int32_t clampResourceValue( const int64_t value )
    {
        return static_cast<int32_t>( std::clamp<int64_t>( value, 0, std::numeric_limits<int32_t>::max() ) );
    }

    bool loadOfflineProgressData( OfflineProgressData & data )
    {
        const std::string filePath = getOfflineProgressFilePath();
        std::ifstream input( filePath );
        if ( !input ) {
            // A backup can exist if the process was interrupted while replacing the state file.
            input.open( filePath + ".bak" );
            if ( !input ) {
                return false;
            }
        }

        int version = 0;
        bool hasTimestamp = false;
        bool hasResources = false;
        bool hasIncome = false;
        bool hasCarry = false;
        bool hasStreak = false;
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
                input >> data.lastSeenUnix;
                hasTimestamp = true;
            }
            else if ( key == "resources" ) {
                hasResources = readFunds( data.resources );
            }
            else if ( key == "daily_income" ) {
                hasIncome = readFunds( data.dailyIncome );
            }
            else if ( key == "carry" ) {
                hasCarry = true;
                for ( int64_t & value : data.carry ) {
                    if ( !( input >> value ) ) {
                        return false;
                    }

                    value = std::clamp<int64_t>( value, 0, offlineSecondsPerDay - 1 );
                }
            }
            else if ( key == "homecoming_streak" ) {
                input >> data.homecomingStreak;
                hasStreak = true;
            }
            else if ( key == "total_offline_seconds" ) {
                input >> data.totalOfflineSeconds;
                hasTotalOfflineSeconds = true;
            }
            else if ( key == "offline_renown" ) {
                input >> data.offlineRenown;
                hasOfflineRenown = true;
            }
            else if ( key == "contract_id" ) {
                input >> data.contractId;
                hasContractId = true;
            }
            else if ( key == "contract_progress" ) {
                input >> data.contractProgress;
                hasContractProgress = true;
            }
            else if ( key == "contract_target" ) {
                input >> data.contractTarget;
                hasContractTarget = true;
            }
            else if ( key == "contracts_completed" ) {
                input >> data.contractsCompleted;
                hasContractsCompleted = true;
            }
            else if ( key == "treasure_fragments" ) {
                input >> data.treasureFragments;
                hasTreasureFragments = true;
            }
            else if ( key == "treasure_maps_completed" ) {
                input >> data.treasureMapsCompleted;
                hasTreasureMapsCompleted = true;
            }
            else if ( key == "state_castles" ) {
                input >> data.stateCastles;
                hasStateCastles = true;
            }
            else if ( key == "state_towns" ) {
                input >> data.stateTowns;
                hasStateTowns = true;
            }
            else if ( key == "state_heroes" ) {
                input >> data.stateHeroes;
                hasStateHeroes = true;
            }
            else if ( key == "state_mines" ) {
                input >> data.stateMines;
                hasStateMines = true;
            }
            else if ( key == "state_artifacts" ) {
                input >> data.stateArtifacts;
                hasStateArtifacts = true;
            }
            else if ( key == "state_efficiency_percent" ) {
                input >> data.stateEfficiencyPercent;
                data.stateEfficiencyPercent = std::clamp<uint32_t>( data.stateEfficiencyPercent, 100, 150 );
                hasStateEfficiency = true;
            }
            else if ( key == "supply_rush_meter" ) {
                input >> data.supplyRushMeter;
                data.supplyRushMeter = std::min<uint32_t>( data.supplyRushMeter, 99 );
                hasSupplyRushMeter = true;
            }
            else if ( key == "creature_roster" ) {
                hasCreatureRoster = true;
                for ( uint64_t & count : data.creatureRoster ) {
                    if ( !( input >> count ) ) {
                        return false;
                    }
                }
            }
            else if ( key == "creature_reserve" ) {
                hasCreatureReserve = true;
                for ( uint64_t & count : data.creatureReserve ) {
                    if ( !( input >> count ) ) {
                        return false;
                    }
                }
            }
            else if ( key == "creature_recruit_carry" ) {
                hasCreatureRecruitCarry = true;
                for ( uint64_t & value : data.creatureRecruitCarry ) {
                    if ( !( input >> value ) ) {
                        return false;
                    }
                }
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
                   && hasCreatureRecruitCarry );
        return ( version >= 1 && version <= 9 ) && hasVersionSpecificFields && hasTimestamp && hasResources && hasIncome && hasCarry && data.lastSeenUnix > 0;
    }

    void saveOfflineProgressData( const OfflineProgressData & data )
    {
        const std::string filePath = getOfflineProgressFilePath();
        const std::string tempFilePath = filePath + ".tmp";
        const std::string backupFilePath = filePath + ".bak";

        std::ofstream output( tempFilePath, std::ios::trunc );
        if ( !output ) {
            ERROR_LOG( "Unable to write temporary offline progress data." )
            return;
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

        output << "version 9\n";
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

        output << "creature_roster";
        for ( const uint64_t count : data.creatureRoster ) {
            output << ' ' << count;
        }
        output << '\n';

        output << "creature_reserve";
        for ( const uint64_t count : data.creatureReserve ) {
            output << ' ' << count;
        }
        output << '\n';

        output << "creature_recruit_carry";
        for ( const uint64_t value : data.creatureRecruitCarry ) {
            output << ' ' << value;
        }
        output << '\n';

        output.flush();
        if ( !output ) {
            ERROR_LOG( "Unable to flush temporary offline progress data." )
            output.close();
            System::Unlink( tempFilePath );
            return;
        }

        output.close();

        // Replace the live file only after the temporary file is fully written. Keep one backup
        // during the swap so an interrupted rename cannot destroy the last valid snapshot.
        System::Unlink( backupFilePath );
        const bool hadOriginal = System::IsFile( filePath );
        if ( hadOriginal && std::rename( filePath.c_str(), backupFilePath.c_str() ) != 0 ) {
            ERROR_LOG( "Unable to back up offline progress data." )
            System::Unlink( tempFilePath );
            return;
        }

        if ( std::rename( tempFilePath.c_str(), filePath.c_str() ) != 0 ) {
            ERROR_LOG( "Unable to replace offline progress data." )
            if ( hadOriginal ) {
                std::rename( backupFilePath.c_str(), filePath.c_str() );
            }
            System::Unlink( tempFilePath );
            return;
        }

        System::Unlink( backupFilePath );
    }

    void setKingdomFundsExact( Kingdom & kingdom, const Funds & target )
    {
        const Funds current = kingdom.GetFunds();
        Funds toAdd;
        Funds toRemove;

        for ( const FundsMember member : offlineFundMembers ) {
            if ( target.*member >= current.*member ) {
                toAdd.*member = target.*member - current.*member;
            }
            else {
                toRemove.*member = current.*member - target.*member;
            }
        }

        kingdom.AddFundsResource( toAdd );
        kingdom.OddFundsResource( toRemove );
    }

    PlayerColor getPersistentResourcePlayerColor( Settings & conf )
    {
        Player * currentPlayer = conf.GetPlayers().GetCurrent();
        if ( currentPlayer != nullptr && ( currentPlayer->isControlHuman() || currentPlayer->isAIAutoControlMode() ) ) {
            return currentPlayer->GetColor();
        }

        for ( Player * player : conf.GetPlayers().getVector() ) {
            if ( player != nullptr && ( player->isControlHuman() || player->isAIAutoControlMode() ) ) {
                return player->GetColor();
            }
        }

        return PlayerColor::NONE;
    }

    uint32_t getOfflineMineCount( const Kingdom & kingdom )
    {
        uint32_t mineCount = 0;
        for ( const int resourceType : offlineResourceTypes ) {
            mineCount += world.CountCapturedMines( resourceType, kingdom.GetColor() );
        }

        return mineCount;
    }

    uint32_t getOfflineStateEfficiencyPercent( const uint32_t castles, const uint32_t towns, const uint32_t heroes, const uint32_t mines,
                                               const uint32_t artifacts )
    {
        const uint32_t castleBonus = std::min<uint32_t>( 16, castles * 4 );
        const uint32_t townBonus = std::min<uint32_t>( 8, towns * 2 );
        const uint32_t heroBonus = std::min<uint32_t>( 12, heroes * 2 );
        const uint32_t mineBonus = std::min<uint32_t>( 12, mines );
        const uint32_t artifactBonus = std::min<uint32_t>( 2, artifacts / 5 );

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

    void addArmyToPersistentRoster( PersistentCreatureRoster & roster, const Army & army )
    {
        for ( size_t slot = 0; slot < army.Size(); ++slot ) {
            const Troop * troop = army.GetTroop( slot );
            if ( troop == nullptr || !troop->isValid() ) {
                continue;
            }

            const int monsterId = troop->GetID();
            if ( monsterId <= Monster::UNKNOWN || static_cast<size_t>( monsterId ) >= roster.size() ) {
                continue;
            }

            uint64_t & count = roster[static_cast<size_t>( monsterId )];
            const uint64_t troopCount = troop->GetCount();
            count = std::numeric_limits<uint64_t>::max() - count < troopCount ? std::numeric_limits<uint64_t>::max() : count + troopCount;
        }
    }

    void capturePersistentCreatureRoster( OfflineProgressData & data, const Kingdom & kingdom )
    {
        PersistentCreatureRoster roster{};

        for ( const Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr ) {
                addArmyToPersistentRoster( roster, hero->GetArmy() );
            }
        }

        for ( const Castle * castle : kingdom.GetCastles() ) {
            if ( castle != nullptr ) {
                addArmyToPersistentRoster( roster, castle->GetArmy() );
            }
        }

        for ( size_t i = 0; i < roster.size(); ++i ) {
            const uint64_t reserve = data.creatureReserve[i];
            roster[i] = std::numeric_limits<uint64_t>::max() - roster[i] < reserve ? std::numeric_limits<uint64_t>::max() : roster[i] + reserve;
        }

        data.creatureRoster = roster;
    }

    double getPersistentRosterStrength( const PersistentCreatureRoster & roster )
    {
        double strength = 0.0;
        for ( size_t i = 0; i < roster.size(); ++i ) {
            const uint64_t count = roster[i];
            if ( count == 0 ) {
                continue;
            }

            const Monster monster( static_cast<int>( i ) );
            if ( !monster.isValid() ) {
                continue;
            }

            strength += monster.GetMonsterStrength() * static_cast<double>( count );
        }

        return strength;
    }

    double getKingdomArmyStrength( const Kingdom & kingdom )
    {
        double strength = 0.0;
        for ( const Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr ) {
                strength += hero->GetArmy().GetStrength();
            }
        }
        for ( const Castle * castle : kingdom.GetCastles() ) {
            if ( castle != nullptr ) {
                strength += castle->GetArmy().GetStrength();
            }
        }
        return strength;
    }

    double getRandomizedEnemyMultiplier( const double baseMultiplier, const uint32_t seed, const uint32_t minBonusPercent,
                                         const uint32_t maxBonusPercent, const double maxMultiplier )
    {
        if ( baseMultiplier <= 1.0 ) {
            return 1.0;
        }

        const uint32_t bonusPercent = Rand::GetWithSeed( minBonusPercent, maxBonusPercent, seed );
        const double randomizedMultiplier = 1.0 + ( baseMultiplier - 1.0 ) * static_cast<double>( bonusPercent ) / 100.0;

        return std::clamp( randomizedMultiplier, 1.0, maxMultiplier );
    }

    void scaleArmyStrength( Army & army, const double multiplier, const uint32_t seed )
    {
        if ( multiplier <= 1.0 ) {
            return;
        }

        for ( size_t slot = 0; slot < army.Size(); ++slot ) {
            Troop * troop = army.GetTroop( slot );
            if ( troop == nullptr || !troop->isValid() ) {
                continue;
            }

            // Keep a smaller second layer of variance inside each army so different creature
            // stacks do not all grow by exactly the same percentage.
            const uint32_t slotSeed = seed ^ static_cast<uint32_t>( 0x9E3779B9u + slot * 0x85EBCA6Bu );
            const double stackMultiplier = getRandomizedEnemyMultiplier( multiplier, slotSeed, 90, 110, 2.85 );

            const uint64_t scaledCount = static_cast<uint64_t>( std::ceil( static_cast<double>( troop->GetCount() ) * stackMultiplier ) );
            troop->SetCount( static_cast<uint32_t>( std::min<uint64_t>( scaledCount, std::numeric_limits<uint32_t>::max() ) ) );
        }
    }

    void scaleNewMapEnemies( const PlayerColor playerColor, const double carriedStrength, const double mapStartingStrength )
    {
        if ( carriedStrength <= 0.0 ) {
            return;
        }

        const double baseline = std::max( 1000.0, mapStartingStrength );
        const double rawStrengthRatio = carriedStrength / baseline;
        if ( rawStrengthRatio <= 1.25 ) {
            return;
        }

        const double strengthRatio = rawStrengthRatio / 1.25;

        // A 25% dead-zone lets modest carry-over feel rewarding. Beyond that, square-root
        // scaling progressively strengthens opposition without matching the player 1:1.
        const double enemyMultiplier = std::clamp( 1.0 + ( std::sqrt( strengthRatio ) - 1.0 ) * 0.75, 1.0, 2.5 );
        const double neutralMultiplier = std::clamp( 1.0 + ( enemyMultiplier - 1.0 ) * 0.70, 1.0, 2.0 );

        if ( enemyMultiplier > 1.0 ) {
            for ( Player * player : Settings::Get().GetPlayers().getVector() ) {
                if ( player == nullptr || player->GetColor() == playerColor || player->isControlHuman()
                     || Players::isFriends( playerColor, static_cast<PlayerColorsSet>( player->GetColor() ) ) ) {
                    continue;
                }

                Kingdom & enemyKingdom = world.GetKingdom( player->GetColor() );
                const uint32_t mapSeed = world.GetMapSeed();

                for ( Heroes * hero : enemyKingdom.GetHeroes() ) {
                    if ( hero != nullptr ) {
                        const uint32_t seed = mapSeed ^ static_cast<uint32_t>( hero->GetID() * 0x45D9F3Bu )
                                              ^ static_cast<uint32_t>( player->GetColor() ) * 0x27D4EB2Du;
                        const double randomizedMultiplier = getRandomizedEnemyMultiplier( enemyMultiplier, seed, 70, 140, 2.85 );
                        scaleArmyStrength( hero->GetArmy(), randomizedMultiplier, seed );
                    }
                }

                for ( Castle * castle : enemyKingdom.GetCastles() ) {
                    if ( castle != nullptr ) {
                        const uint32_t seed = mapSeed ^ static_cast<uint32_t>( castle->GetIndex() ) * 0x165667B1u
                                              ^ static_cast<uint32_t>( player->GetColor() ) * 0x9E3779B9u;
                        const double randomizedMultiplier = getRandomizedEnemyMultiplier( enemyMultiplier, seed, 70, 140, 2.85 );
                        scaleArmyStrength( castle->GetArmy(), randomizedMultiplier, seed );
                    }
                }
            }
        }

        if ( neutralMultiplier > 1.0 ) {
            for ( size_t tileIndex = 0; tileIndex < world.getSize(); ++tileIndex ) {
                Maps::Tile & tile = world.getTile( static_cast<int32_t>( tileIndex ) );
                if ( tile.getMainObjectType( false ) != MP2::OBJ_MONSTER ) {
                    continue;
                }

                const uint32_t count = Maps::getMonsterCountFromTile( tile );
                if ( count == 0 ) {
                    continue;
                }

                const uint32_t seed = world.GetMapSeed() ^ static_cast<uint32_t>( tileIndex ) * 0x7FEB352Du ^ 0xA24BAED5u;
                const double randomizedMultiplier = getRandomizedEnemyMultiplier( neutralMultiplier, seed, 60, 150, 2.35 );

                const uint64_t scaledCount = static_cast<uint64_t>( std::ceil( static_cast<double>( count ) * randomizedMultiplier ) );
                Maps::setMonsterCountOnTile( tile, static_cast<uint32_t>( std::min<uint64_t>( scaledCount, std::numeric_limits<uint32_t>::max() ) ) );
            }
        }
    }

    uint64_t deployCreatureCountToArmy( Army & army, const Monster & monster, uint64_t remaining )
    {
        if ( remaining == 0 || !monster.isValid() ) {
            return remaining;
        }

        // Fill existing stacks first without ever overflowing the engine's uint32 troop count.
        for ( size_t slot = 0; slot < army.Size() && remaining > 0; ++slot ) {
            Troop * troop = army.GetTroop( slot );
            if ( troop == nullptr || !troop->isValid() || !troop->isMonster( monster.GetID() ) ) {
                continue;
            }

            const uint64_t capacity = std::numeric_limits<uint32_t>::max() - static_cast<uint64_t>( troop->GetCount() );
            const uint32_t amount = static_cast<uint32_t>( std::min<uint64_t>( remaining, capacity ) );
            if ( amount == 0 ) {
                continue;
            }

            troop->SetCount( troop->GetCount() + amount );
            remaining -= amount;
        }

        // If one stack reaches uint32 max, continue into free army slots instead of wrapping
        // the existing stack or silently banking creatures that can still be deployed.
        for ( size_t slot = 0; slot < army.Size() && remaining > 0; ++slot ) {
            Troop * troop = army.GetTroop( slot );
            if ( troop == nullptr || troop->isValid() ) {
                continue;
            }

            const uint32_t amount = static_cast<uint32_t>( std::min<uint64_t>( remaining, std::numeric_limits<uint32_t>::max() ) );
            if ( amount == 0 ) {
                break;
            }

            troop->Set( monster, amount );
            remaining -= amount;
        }

        return remaining;
    }

    void restorePersistentCreaturesForNewMap( Kingdom & kingdom )
    {
        OfflineProgressData data;
        if ( !loadOfflineProgressData( data ) ) {
            return;
        }

        const bool hasRoster = std::any_of( data.creatureRoster.cbegin(), data.creatureRoster.cend(), []( const uint64_t count ) { return count > 0; } );
        if ( !hasRoster ) {
            return;
        }

        const double mapStartingStrength = getKingdomArmyStrength( kingdom );
        const double carriedStrength = getPersistentRosterStrength( data.creatureRoster );

        std::vector<Army *> targetArmies;
        targetArmies.reserve( kingdom.GetHeroes().size() + kingdom.GetCastles().size() );

        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr ) {
                hero->GetArmy().Clean();
                targetArmies.push_back( &hero->GetArmy() );
            }
        }
        for ( Castle * castle : kingdom.GetCastles() ) {
            if ( castle != nullptr ) {
                castle->GetArmy().Clean();
                targetArmies.push_back( &castle->GetArmy() );
            }
        }

        PersistentCreatureRoster reserve{};
        PersistentCreatureRoster remainingRoster = data.creatureRoster;
        std::vector<size_t> monsterIds;
        monsterIds.reserve( data.creatureRoster.size() );
        for ( size_t i = 0; i < data.creatureRoster.size(); ++i ) {
            if ( data.creatureRoster[i] > 0 ) {
                const Monster monster( static_cast<int>( i ) );
                if ( monster.isValid() ) {
                    monsterIds.push_back( i );
                }
            }
        }

        std::sort( monsterIds.begin(), monsterIds.end(), []( const size_t lhs, const size_t rhs ) {
            return Monster( static_cast<int>( lhs ) ).GetMonsterStrength() > Monster( static_cast<int>( rhs ) ).GetMonsterStrength();
        } );

        if ( !monsterIds.empty() ) {
            const size_t seedMonsterId = monsterIds.front();
            const Monster seedMonster( static_cast<int>( seedMonsterId ) );
            uint64_t & seedCount = remainingRoster[seedMonsterId];

            for ( Heroes * hero : kingdom.GetHeroes() ) {
                if ( hero == nullptr || seedCount == 0 ) {
                    continue;
                }

                if ( hero->GetArmy().JoinTroop( seedMonster, 1, false ) ) {
                    --seedCount;
                }
            }
        }

        for ( const size_t monsterId : monsterIds ) {
            uint64_t remaining = remainingRoster[monsterId];
            const Monster monster( static_cast<int>( monsterId ) );

            for ( Army * army : targetArmies ) {
                if ( army == nullptr || remaining == 0 ) {
                    continue;
                }

                remaining = deployCreatureCountToArmy( *army, monster, remaining );
            }

            reserve[monsterId] = remaining;
        }

        data.creatureReserve = reserve;
        saveOfflineProgressData( data );

        scaleNewMapEnemies( kingdom.GetColor(), carriedStrength, mapStartingStrength );
    }

    void assignPersistentCreatureReservesToHeroes( Kingdom & kingdom )
    {
        OfflineProgressData data;
        if ( !loadOfflineProgressData( data ) ) {
            return;
        }

        if ( std::none_of( data.creatureReserve.cbegin(), data.creatureReserve.cend(), []( const uint64_t count ) { return count > 0; } ) ) {
            return;
        }

        std::vector<Heroes *> heroes;
        heroes.reserve( kingdom.GetHeroes().size() );
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr ) {
                heroes.push_back( hero );
            }
        }

        // Freshly recruited heroes tend to have the weakest armies, so fill them first. Castles
        // are still valid reserve destinations when the kingdom currently has no heroes.
        std::sort( heroes.begin(), heroes.end(), []( const Heroes * lhs, const Heroes * rhs ) {
            return lhs->GetArmy().GetStrength() < rhs->GetArmy().GetStrength();
        } );

        std::vector<size_t> monsterIds;
        for ( size_t i = 0; i < data.creatureReserve.size(); ++i ) {
            if ( data.creatureReserve[i] == 0 ) {
                continue;
            }

            const Monster monster( static_cast<int>( i ) );
            if ( monster.isValid() ) {
                monsterIds.push_back( i );
            }
        }

        std::sort( monsterIds.begin(), monsterIds.end(), []( const size_t lhs, const size_t rhs ) {
            return Monster( static_cast<int>( lhs ) ).GetMonsterStrength() > Monster( static_cast<int>( rhs ) ).GetMonsterStrength();
        } );

        bool changed = false;

        for ( const size_t monsterId : monsterIds ) {
            uint64_t & reserveCount = data.creatureReserve[monsterId];
            const Monster monster( static_cast<int>( monsterId ) );
            const uint64_t originalReserve = reserveCount;

            for ( Heroes * hero : heroes ) {
                if ( reserveCount == 0 ) {
                    break;
                }

                reserveCount = deployCreatureCountToArmy( hero->GetArmy(), monster, reserveCount );
            }

            if ( reserveCount > 0 ) {
                for ( Castle * castle : kingdom.GetCastles() ) {
                    if ( castle == nullptr ) {
                        continue;
                    }

                    reserveCount = deployCreatureCountToArmy( castle->GetArmy(), monster, reserveCount );
                    if ( reserveCount == 0 ) {
                        break;
                    }
                }
            }

            changed = changed || reserveCount != originalReserve;
        }

        if ( changed ) {
            // Keep the aggregate roster synchronized with the reserve mutation. Otherwise a crash
            // between this save and the normal end-of-turn snapshot can restore already-deployed
            // reserve creatures again on the next map.
            capturePersistentCreatureRoster( data, kingdom );
            saveOfflineProgressData( data );
        }
    }

    void persistOfflineProgressSnapshot( Kingdom & kingdom )
    {
        OfflineProgressData data;
        const bool hasSavedData = loadOfflineProgressData( data );
        const int64_t now = getCurrentUnixTime();

        // Never move the offline clock backwards. A temporary system-clock rollback would
        // otherwise become a fake offline interval after the clock returns to normal.
        data.lastSeenUnix = hasSavedData ? std::max( data.lastSeenUnix, now ) : now;
        data.resources = kingdom.GetFunds();
        captureOfflineKingdomState( data, kingdom );
        capturePersistentCreatureRoster( data, kingdom );

        saveOfflineProgressData( data );
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

    void applyOfflineStreakProgress( OfflineProgressSummary & summary, OfflineProgressData & data )
    {
        const uint64_t elapsedSeconds = summary.elapsedSeconds > 0 ? static_cast<uint64_t>( summary.elapsedSeconds ) : 0;
        if ( std::numeric_limits<uint64_t>::max() - data.totalOfflineSeconds < elapsedSeconds ) {
            data.totalOfflineSeconds = std::numeric_limits<uint64_t>::max();
        }
        else {
            data.totalOfflineSeconds += elapsedSeconds;
        }
        summary.totalOfflineSeconds = data.totalOfflineSeconds;

        if ( summary.elapsedSeconds < 6 * 60 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            summary.homecomingStreak = data.homecomingStreak;
            return;
        }

        if ( data.homecomingStreak < std::numeric_limits<uint32_t>::max() ) {
            ++data.homecomingStreak;
        }
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

        summary.contractProgressAfter = std::min( data.contractProgress, data.contractTarget );

        if ( data.contractProgress < data.contractTarget ) {
            return;
        }

        summary.contractCompleted = true;

        size_t bestIndex = offlineFundMembers.size();
        int32_t bestProduction = 0;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const int32_t production = summary.productionRewards.*offlineFundMembers[i];
            if ( production > bestProduction ) {
                bestProduction = production;
                bestIndex = i;
            }
        }

        if ( data.contractsCompleted < std::numeric_limits<uint32_t>::max() ) {
            ++data.contractsCompleted;
        }
        summary.contractsCompleted = data.contractsCompleted;

        if ( bestIndex != offlineFundMembers.size() ) {
            const int rewardPercent = 20 + static_cast<int>( ( data.contractsCompleted - 1 ) % 4 ) * 5;
            const FundsMember member = offlineFundMembers[bestIndex];
            const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
            const int64_t calculatedBonus = std::max<int64_t>( 1, ( static_cast<int64_t>( bestProduction ) * rewardPercent ) / 100 );
            const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );

            if ( grantedBonus > 0 ) {
                summary.contractRewardResource = offlineResourceTypes[bestIndex];
                summary.contractRewardBonus = static_cast<int32_t>( grantedBonus );
                summary.bonusRewards.*member += summary.contractRewardBonus;
                summary.rewards.*member += summary.contractRewardBonus;
                data.resources.*member += summary.contractRewardBonus;
            }
        }

        data.contractId = getOfflineContractId( data.contractsCompleted );
        data.contractProgress = 0;
        data.contractTarget = getOfflineContractTarget( data.contractId, data.contractsCompleted );

        summary.nextContractId = data.contractId;
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
        summary.treasureFragmentsBefore = data.treasureFragments;
        summary.treasureFragmentsAfter = data.treasureFragments;
        summary.treasureMapsCompleted = data.treasureMapsCompleted;

        if ( summary.elapsedSeconds < 2 * 60 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return;
        }

        uint32_t fragmentsEarned = 1;
        if ( summary.elapsedSeconds >= offlineSecondsPerDay ) {
            ++fragmentsEarned;
        }
        if ( summary.rareDiscovery || summary.contractCompleted ) {
            ++fragmentsEarned;
        }
        fragmentsEarned = std::min<uint32_t>( fragmentsEarned, 3 );

        summary.treasureFragmentsEarned = fragmentsEarned;
        data.treasureFragments = std::min<uint32_t>( 8, data.treasureFragments + fragmentsEarned );
        summary.treasureFragmentsAfter = data.treasureFragments;

        if ( data.treasureFragments < 5 ) {
            return;
        }

        data.treasureFragments -= 5;
        summary.treasureFragmentsAfter = data.treasureFragments;
        summary.treasureMapCompleted = true;
        summary.treasureMapId = static_cast<int>( data.treasureMapsCompleted % 4 );

        std::array<size_t, 7> eligibleIndices{};
        size_t eligibleCount = 0;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            if ( summary.productionRewards.*offlineFundMembers[i] > 0 ) {
                eligibleIndices[eligibleCount] = i;
                ++eligibleCount;
            }
        }

        if ( eligibleCount > 0 ) {
            const uint64_t seed = getOfflineEventSeed( previousLastSeenUnix,
                                                        summary.elapsedSeconds + static_cast<int64_t>( data.treasureMapsCompleted + 1 ) * 7919 );
            const size_t selectedIndex = eligibleIndices[( seed >> 24 ) % eligibleCount];
            const FundsMember member = offlineFundMembers[selectedIndex];
            const int rewardPercent = getTreasureMapRewardPercent( summary.treasureMapId );
            const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
            const int64_t baseReward = summary.productionRewards.*member;
            const int64_t calculatedBonus = std::max<int64_t>( 1, ( baseReward * rewardPercent ) / 100 );
            const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );

            if ( grantedBonus > 0 ) {
                summary.treasureRewardResource = offlineResourceTypes[selectedIndex];
                summary.treasureRewardBonus = static_cast<int32_t>( grantedBonus );
                summary.bonusRewards.*member += summary.treasureRewardBonus;
                summary.rewards.*member += summary.treasureRewardBonus;
                data.resources.*member += summary.treasureRewardBonus;
            }
        }

        if ( data.treasureMapsCompleted < std::numeric_limits<uint32_t>::max() ) {
            ++data.treasureMapsCompleted;
        }
        summary.treasureMapsCompleted = data.treasureMapsCompleted;
    }

    void applyOfflineSupplyRush( OfflineProgressSummary & summary, OfflineProgressData & data )
    {
        summary.supplyRushBefore = data.supplyRushMeter;
        summary.supplyRushAfter = data.supplyRushMeter;

        if ( summary.elapsedSeconds < 30 * 60 || summary.productionRewards.GetValidItemsCount() == 0 ) {
            return;
        }

        const uint64_t elapsedHours = std::max<uint64_t>( 1, static_cast<uint64_t>( summary.elapsedSeconds ) / ( 60 * 60 ) );
        const uint32_t timePoints = static_cast<uint32_t>( std::min<uint64_t>( 20, elapsedHours / 6 ) );
        const uint32_t statePoints = ( summary.stateEfficiencyPercent - 100 ) / 5;
        const uint32_t tierPoints = static_cast<uint32_t>( summary.homecomingTier * 3 );

        summary.supplyRushEarned = 5 + timePoints + statePoints + tierPoints;

        const uint32_t accumulated = data.supplyRushMeter + summary.supplyRushEarned;
        if ( accumulated < 100 ) {
            data.supplyRushMeter = accumulated;
            summary.supplyRushAfter = data.supplyRushMeter;
            return;
        }

        summary.supplyRushTriggered = true;
        data.supplyRushMeter = accumulated - 100;
        summary.supplyRushAfter = data.supplyRushMeter;

        constexpr int rushBonusPercent = 20;
        for ( size_t i = 0; i < offlineFundMembers.size(); ++i ) {
            const FundsMember member = offlineFundMembers[i];
            const int64_t baseReward = summary.productionRewards.*member;
            if ( baseReward <= 0 ) {
                continue;
            }

            const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
            const int64_t calculatedBonus = std::max<int64_t>( 1, ( baseReward * rushBonusPercent ) / 100 );
            const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );
            if ( grantedBonus <= 0 ) {
                continue;
            }

            const int32_t bonus = static_cast<int32_t>( grantedBonus );
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
            earned += 8;
        }
        if ( summary.treasureMapCompleted ) {
            earned += 12;
        }
        if ( summary.supplyRushTriggered ) {
            earned += 10;
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

        summary.rankUpPercent = 10 + summary.rankAfter * 5;
        const FundsMember member = offlineFundMembers[bestIndex];
        const int64_t capacity = std::numeric_limits<int32_t>::max() - static_cast<int64_t>( data.resources.*member );
        const int64_t calculatedBonus = std::max<int64_t>( 1, ( static_cast<int64_t>( bestProduction ) * summary.rankUpPercent ) / 100 );
        const int64_t grantedBonus = std::min<int64_t>( calculatedBonus, capacity );

        if ( grantedBonus <= 0 ) {
            return;
        }

        summary.rankUpResource = offlineResourceTypes[bestIndex];
        summary.rankUpBonus = static_cast<int32_t>( grantedBonus );
        summary.bonusRewards.*member += summary.rankUpBonus;
        summary.rewards.*member += summary.rankUpBonus;
        data.resources.*member += summary.rankUpBonus;
    }

    void applyOfflineCreatureRecruitment( OfflineProgressSummary & summary, OfflineProgressData & data, Kingdom & kingdom )
    {
        if ( summary.elapsedSeconds <= 0 ) {
            return;
        }

        const uint64_t savedSettlementCount = static_cast<uint64_t>( summary.stateCastles ) + summary.stateTowns;
        if ( savedSettlementCount == 0 ) {
            return;
        }

        VecCastles & castles = kingdom.GetCastles();
        const size_t settlementLimit = std::min<size_t>( castles.size(), static_cast<size_t>( savedSettlementCount ) );
        if ( settlementLimit == 0 ) {
            return;
        }

        constexpr std::array<uint32_t, 6> baseDwellings{ DWELLING_MONSTER1, DWELLING_MONSTER2, DWELLING_MONSTER3,
                                                         DWELLING_MONSTER4, DWELLING_MONSTER5, DWELLING_MONSTER6 };

        data.creatureRecruitCarry.fill( 0 );

        for ( size_t castleIndex = 0; castleIndex < settlementLimit; ++castleIndex ) {
            Castle * castle = castles[castleIndex];
            if ( castle == nullptr || castle->GetColor() != kingdom.GetColor() ) {
                continue;
            }

            bool recruitedAtSettlement = false;

            for ( int tier = Castle::maxNumOfDwellings - 1; tier >= 0; --tier ) {
                const uint32_t baseDwelling = baseDwellings[static_cast<size_t>( tier )];
                if ( !castle->isBuild( baseDwelling ) ) {
                    continue;
                }

                const uint32_t actualDwelling = castle->GetActualDwelling( baseDwelling );
                if ( actualDwelling == BUILD_NOTHING ) {
                    continue;
                }

                const Monster monster( castle->GetRace(), actualDwelling );
                if ( !monster.isValid() ) {
                    continue;
                }

                const int affordable = kingdom.GetFunds().getLowestQuotient( monster.GetCost() );
                if ( affordable <= 0 ) {
                    continue;
                }

                const uint32_t recruitCount = static_cast<uint32_t>( affordable );
                const Funds cost = monster.GetCost() * recruitCount;
                if ( !kingdom.AllowPayment( cost ) ) {
                    continue;
                }

                uint64_t remaining = recruitCount;

                // Prefer the settlement that produced the creatures, then its visiting hero.
                remaining = deployCreatureCountToArmy( castle->GetArmy(), monster, remaining );

                Heroes * guestHero = castle->GetHero();
                if ( remaining > 0 && guestHero != nullptr ) {
                    remaining = deployCreatureCountToArmy( guestHero->GetArmy(), monster, remaining );
                }

                // If that settlement is full, immediately place the paid creatures into any
                // other owned army with capacity instead of leaving them invisible until turn end.
                if ( remaining > 0 ) {
                    for ( Heroes * hero : kingdom.GetHeroes() ) {
                        if ( hero == nullptr || hero == guestHero ) {
                            continue;
                        }

                        remaining = deployCreatureCountToArmy( hero->GetArmy(), monster, remaining );
                        if ( remaining == 0 ) {
                            break;
                        }
                    }
                }

                if ( remaining > 0 ) {
                    for ( Castle * otherCastle : castles ) {
                        if ( otherCastle == nullptr || otherCastle == castle ) {
                            continue;
                        }

                        remaining = deployCreatureCountToArmy( otherCastle->GetArmy(), monster, remaining );
                        if ( remaining == 0 ) {
                            break;
                        }
                    }
                }

                if ( remaining > 0 ) {
                    const size_t monsterId = static_cast<size_t>( monster.GetID() );
                    if ( monsterId < data.creatureReserve.size() ) {
                        uint64_t & reserve = data.creatureReserve[monsterId];
                        reserve = std::numeric_limits<uint64_t>::max() - reserve < remaining ? std::numeric_limits<uint64_t>::max() : reserve + remaining;
                    }
                    else {
                        // Never charge for creatures that cannot be represented by persistent storage.
                        const uint64_t deployedCount = static_cast<uint64_t>( recruitCount ) - remaining;
                        if ( deployedCount == 0 ) {
                            continue;
                        }

                        const Funds deployedCost = monster.GetCost() * static_cast<uint32_t>( deployedCount );
                        kingdom.OddFundsResource( deployedCost );
                        summary.recruitmentSpent += deployedCost;
                        summary.recruitedCreatures += deployedCount;
                        ++summary.recruitedStacks;
                        recruitedAtSettlement = true;
                        continue;
                    }
                }

                kingdom.OddFundsResource( cost );
                summary.recruitmentSpent += cost;
                summary.recruitedCreatures += recruitCount;
                ++summary.recruitedStacks;
                recruitedAtSettlement = true;
            }

            if ( recruitedAtSettlement ) {
                ++summary.recruitmentSettlements;
            }
        }

        data.resources = kingdom.GetFunds();
        capturePersistentCreatureRoster( data, kingdom );
    }

    OfflineProgressSummary applyOfflineProgress( Kingdom & kingdom )
    {
        OfflineProgressData data;
        const int64_t now = getCurrentUnixTime();

        if ( !loadOfflineProgressData( data ) ) {
            data.lastSeenUnix = now;
            data.resources = kingdom.GetFunds();
            captureOfflineKingdomState( data, kingdom );
            capturePersistentCreatureRoster( data, kingdom );
            saveOfflineProgressData( data );
            return {};
        }

        // The persistent wallet is authoritative across new maps and loaded games.
        setKingdomFundsExact( kingdom, data.resources );

        OfflineProgressSummary summary;
        const int64_t previousLastSeenUnix = data.lastSeenUnix;
        summary.elapsedSeconds = now > previousLastSeenUnix ? now - previousLastSeenUnix : 0;
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
                savedDailyIncome = savedDailyIncome * 750 / 100;
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
        applyOfflineStreakProgress( summary, data );
        applyOfflineRareDiscovery( summary, data, previousLastSeenUnix );
        applyOfflineContractProgress( summary, data );
        applyOfflineTreasureHunt( summary, data, previousLastSeenUnix );
        applyOfflineSupplyRush( summary, data );
        applyOfflineRenownProgress( summary, data );

        setKingdomFundsExact( kingdom, data.resources );
        applyOfflineCreatureRecruitment( summary, data, kingdom );

        // The next offline interval uses the active map and player state at this snapshot.
        // Preserve a future saved timestamp if the local system clock temporarily moves backwards.
        data.lastSeenUnix = std::max( previousLastSeenUnix, now );
        captureOfflineKingdomState( data, kingdom );
        capturePersistentCreatureRoster( data, kingdom );
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

        std::string message = _( "Away %{days}d %{hours}h %{minutes}m" );
        StringReplace( message, "%{days}", std::to_string( days ) );
        StringReplace( message, "%{hours}", std::to_string( hours ) );
        StringReplace( message, "%{minutes}", std::to_string( minutes ) );

        message += "\n";

        if ( summary.rewards.GetValidItemsCount() == 0 ) {
            message += _( "Rewards: none." );
            fheroes2::showStandardTextMessage( _( "Offline Progress" ), std::move( message ), Dialog::OK );
            return;
        }

        message += _( "Rewards:" );

        fheroes2::showResourceMessage( fheroes2::Text( _( "Offline Progress" ), fheroes2::FontType::normalYellow() ),
                                       fheroes2::Text( std::move( message ), fheroes2::FontType::normalWhite() ), Dialog::OK, summary.rewards );
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
    OfflineProgressSummary offlineProgressSummary;
    if ( persistentResourcePlayerColor != PlayerColor::NONE ) {
        Kingdom & persistentKingdom = world.GetKingdom( persistentResourcePlayerColor );
        if ( !conf.LoadedGameVersion() ) {
            restorePersistentCreaturesForNewMap( persistentKingdom );
        }
        offlineProgressSummary = applyOfflineProgress( persistentKingdom );
    }

    // Prepare for render the whole game interface with adventure map filled with fog as it was not uncovered by 'updateMapFogDirections()'.
    redraw( REDRAW_GAMEAREA | REDRAW_RADAR | REDRAW_ICONS | REDRAW_BUTTONS | REDRAW_STATUS | REDRAW_BORDER );

    showOfflineProgressPopup( offlineProgressSummary );

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

                if ( !isAutoPlaytest && playerColor == persistentResourcePlayerColor ) {
                    assignPersistentCreatureReservesToHeroes( kingdom );
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

    // Capture the final surviving roster as well. This matters when a victory/defeat or
    // menu transition ends the map before another normal end-of-turn snapshot can happen.
    if ( !isAutoPlaytest && persistentResourcePlayerColor != PlayerColor::NONE ) {
        persistOfflineProgressSnapshot( world.GetKingdom( persistentResourcePlayerColor ) );
    }

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
