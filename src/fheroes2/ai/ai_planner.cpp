/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2024 - 2026                                             *
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

#include "ai_planner.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "ai_common.h"
#include "army.h"
#include "heroes.h"
#include "kingdom.h"
#include "maps.h"
#include "maps_tiles.h"
#include "mp2.h"
#include "profit.h"
#include "resource.h"
#include "route.h"
#include "world.h"

namespace
{
    bool isStrategicDiscovery( const MP2::MapObjectType object )
    {
        switch ( object ) {
        case MP2::OBJ_CASTLE:
        case MP2::OBJ_HERO:
        case MP2::OBJ_MINE:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_ABANDONED_MINE:
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_LIGHTHOUSE:
            return true;
        default:
            return false;
        }
    }

    bool isCooldownEligibleStrategicTarget( const MP2::MapObjectType object )
    {
        // Failed-plan cooldowns are deliberately limited to non-critical economic/exploration
        // targets. Hero and castle objectives can become urgent between turns and should remain
        // available to the kingdom-level ATTACK/DEFEND logic at all times.
        switch ( object ) {
        case MP2::OBJ_MINE:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_ABANDONED_MINE:
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_LIGHTHOUSE:
            return true;
        default:
            return false;
        }
    }

    uint32_t getStrategicResponderRolePenalty( const Heroes::Role role, const MP2::MapObjectType object, const bool criticalDiscovery )
    {
        // Combat-capable heroes should answer urgent hero/castle discoveries, while scouts and
        // hunters are better suited to ordinary economic or exploration opportunities. This is only
        // a tie-break style penalty: distance remains the dominant assignment signal.
        if ( criticalDiscovery || object == MP2::OBJ_HERO || object == MP2::OBJ_CASTLE ) {
            switch ( role ) {
            case Heroes::Role::CHAMPION:
                return 0;
            case Heroes::Role::FIGHTER:
                return 10;
            case Heroes::Role::HUNTER:
                return 25;
            case Heroes::Role::SCOUT:
                return 55;
            case Heroes::Role::COURIER:
                return 75;
            default:
                return 40;
            }
        }

        switch ( role ) {
        case Heroes::Role::SCOUT:
            return 0;
        case Heroes::Role::HUNTER:
            return 10;
        case Heroes::Role::FIGHTER:
            return 30;
        case Heroes::Role::COURIER:
            return 45;
        case Heroes::Role::CHAMPION:
            return 60;
        default:
            return 35;
        }
    }
}

AI::Planner & AI::Planner::Get()
{
    static Planner ai;
    return ai;
}

void AI::Planner::revealFog( const Maps::Tile & tile, const Kingdom & kingdom )
{
    const MP2::MapObjectType object = tile.getMainObjectType();
    if ( !MP2::isInGameActionObject( object ) ) {
        return;
    }

    const int32_t discoveryIndex = tile.GetIndex();
    const uint32_t currentDay = world.CountDay();

    updateMapActionObjectCache( kingdom, discoveryIndex );
    updatePriorityAttackTarget( kingdom, tile );

    const bool criticalDiscovery = isCriticalTask( discoveryIndex );

    // A target which has previously caused a failed diversion is temporarily suppressed from normal
    // kingdom planning. A critical task always overrides this memory, while a hero already very close
    // to the target is allowed to retry early because the old failure conditions no longer apply.
    if ( const auto cooldownIt = _strategicTargetCooldowns.find( discoveryIndex ); cooldownIt != _strategicTargetCooldowns.end() ) {
        if ( criticalDiscovery ) {
            _strategicTargetCooldowns.erase( cooldownIt );
            updateMapActionObjectCache( kingdom, discoveryIndex );
        }
        else if ( cooldownIt->second.untilDay >= currentDay ) {
            const VecHeroes & cooldownHeroes = kingdom.GetHeroes();
            const bool closeEnoughToRetry = std::any_of( cooldownHeroes.begin(), cooldownHeroes.end(), [discoveryIndex]( const Heroes * hero ) {
                return hero != nullptr && hero->isActive() && Maps::GetApproximateDistance( hero->GetIndex(), discoveryIndex ) <= 2;
            } );

            if ( !closeEnoughToRetry ) {
                return;
            }

            _strategicTargetCooldowns.erase( cooldownIt );
            updateMapActionObjectCache( kingdom, discoveryIndex );
        }
    }

    // Routine pickups should not make a hero abandon a good multi-turn plan. Strategic discoveries
    // can interrupt a route, while critical attack/defence tasks always remain immediately reactive.
    if ( !criticalDiscovery && !isStrategicDiscovery( object ) ) {
        return;
    }

    const VecHeroes & heroes = kingdom.GetHeroes();

    if ( !criticalDiscovery ) {
        // A live route is also a zero-cost reservation. If another hero is already committed to this
        // discovery, do not interrupt a second hero just to make it reconsider the same objective.
        // The reservation disappears automatically when the route changes, is consumed or reset.
        const bool alreadyReserved = std::any_of( heroes.begin(), heroes.end(), [discoveryIndex]( const Heroes * hero ) {
            return hero != nullptr && hero->isActive() && hero->GetPath().GetDestinationIndex() == discoveryIndex;
        } );

        if ( alreadyReserved ) {
            return;
        }
    }

    Heroes * bestResponder = nullptr;
    uint64_t bestResponderScore = std::numeric_limits<uint64_t>::max();

    for ( Heroes * hero : heroes ) {
        if ( hero == nullptr ) {
            // How is it even possible?
            assert( 0 );
            continue;
        }

        // Critical discoveries must affect the hero who is actually in motion right now so the
        // emergency is reacted to immediately. Ordinary opportunities are compared against every
        // hero that can still act this turn: if a better responder exists, the moving hero is not
        // distracted and the better hero can pick up the newly cached target during normal planning.
        if ( criticalDiscovery ? !hero->isMoveEnabled() : !hero->MayStillMove( false, false ) ) {
            continue;
        }

        Route::Path & path = hero->GetPath();
        const int32_t committedTarget = path.GetDestinationIndex();

        // Do not throw away a plan that is already about to complete. Empty routes have no commitment
        // and are ideal future responders, so only apply the near-completion guard to real paths.
        if ( !criticalDiscovery && !path.empty() && path.size() <= 2 ) {
            continue;
        }

        // Once a hero is already committed to a critical ATTACK/DEFEND objective, ordinary mines,
        // artifacts and similar opportunities are not allowed to peel it away. A newly critical
        // discovery can still pre-empt the route immediately, which is how rising threats break
        // strategic commitment without making the AI indecisive.
        if ( !criticalDiscovery && committedTarget != -1 && isCriticalTask( committedTarget ) ) {
            continue;
        }

        // If the newly revealed strategic object is already the route destination, there is no need
        // to force another planning pass.
        if ( committedTarget == discoveryIndex ) {
            continue;
        }

        HeroPlanMemory & memory = _heroPlanMemory[hero->GetID()];

        if ( !criticalDiscovery ) {
            // Opportunity pressure is a bounded anti-distraction memory. Every ordinary strategic
            // reroute makes the next one require a tighter geographic window. Three quiet days remove
            // one point of pressure, so heroes become curious again instead of staying permanently rigid.
            if ( memory.opportunityPressureDay == 0 ) {
                memory.opportunityPressureDay = currentDay;
            }
            else if ( currentDay > memory.opportunityPressureDay ) {
                const uint32_t elapsedDays = currentDay - memory.opportunityPressureDay;
                const uint32_t decay = elapsedDays / 3;
                if ( decay > 0 ) {
                    const uint32_t pressure = memory.opportunityPressure;
                    memory.opportunityPressure = static_cast<uint8_t>( pressure > decay ? pressure - decay : 0 );
                    memory.opportunityPressureDay = currentDay;
                }
            }

            // Nearby strategic discoveries are genuine opportunity windows; distant ones can wait for
            // normal target selection. Repeated diversions shrink the window from six tiles down to two.
            // Enemy heroes/castles get two extra tiles because their vulnerability can disappear quickly.
            const uint32_t opportunityPressure = std::min<uint32_t>( memory.opportunityPressure, 4 );
            uint32_t opportunityRadius = 6 - opportunityPressure;
            if ( object == MP2::OBJ_HERO || object == MP2::OBJ_CASTLE ) {
                opportunityRadius += 2;
            }

            const uint32_t approximateDistance = Maps::GetApproximateDistance( hero->GetIndex(), discoveryIndex );
            if ( committedTarget != -1 && approximateDistance > opportunityRadius ) {
                continue;
            }
        }

        const bool alreadyReplannedToday = memory.lastStrategicInterruptTile != -1 && memory.lastStrategicInterruptDay == currentDay;

        // Preserve plan stickiness for ordinary strategic discoveries: at most one route interruption
        // per hero per day. Critical defence/attack tasks are allowed to override this guard.
        if ( !criticalDiscovery && alreadyReplannedToday ) {
            continue;
        }

        const uint64_t approximateDistance = Maps::GetApproximateDistance( hero->GetIndex(), discoveryIndex );
        const uint64_t rolePenalty = getStrategicResponderRolePenalty( hero->getAIRole(), object, criticalDiscovery );

        // Empty routes are cheap to redirect. Among heroes that already have plans, protect routes
        // that are close to completion more strongly than long-range plans. Distance still dominates,
        // and hero ID gives deterministic ordering when two candidates are otherwise equivalent.
        const uint64_t commitmentPenalty = path.empty() ? 0 : 180 / std::min<std::size_t>( path.size(), 6 );
        const uint64_t responderScore = approximateDistance * 100 + commitmentPenalty + rolePenalty;

        if ( bestResponder == nullptr || responderScore < bestResponderScore
             || ( responderScore == bestResponderScore && hero->GetID() < bestResponder->GetID() ) ) {
            bestResponder = hero;
            bestResponderScore = responderScore;
        }
    }

    // For an ordinary opportunity, a non-moving hero can be the best responder. In that case the
    // current hero deliberately keeps moving; the discovered object is already in the shared cache,
    // so the better responder can select it normally when its planning slot arrives.
    if ( bestResponder == nullptr || ( !criticalDiscovery && !bestResponder->isMoveEnabled() ) ) {
        return;
    }

    HeroPlanMemory & memory = _heroPlanMemory[bestResponder->GetID()];
    const int32_t abandonedTarget = bestResponder->GetPath().GetDestinationIndex();

    if ( !criticalDiscovery && abandonedTarget >= 0 && abandonedTarget != discoveryIndex && abandonedTarget == memory.lastStrategicInterruptTile
         && !isCriticalTask( abandonedTarget ) ) {
        const MP2::MapObjectType abandonedObject = world.getTile( abandonedTarget ).getMainObjectType();
        if ( isCooldownEligibleStrategicTarget( abandonedObject ) ) {
            StrategicTargetCooldown & cooldown = _strategicTargetCooldowns[abandonedTarget];
            cooldown.failureCount = static_cast<uint8_t>( std::min<uint32_t>( 3, cooldown.failureCount + 1 ) );
            cooldown.untilDay = currentDay + 1 + cooldown.failureCount;

            // Remove the failed plan from the current turn's shared candidate set immediately. The
            // kingdom cache is rebuilt on later turns and updateMapActionObjectCache() will restore it
            // automatically once the cooldown expires.
            _mapActionObjects.erase( abandonedTarget );
        }
    }

    memory.lastStrategicInterruptDay = currentDay;
    memory.lastStrategicInterruptTile = discoveryIndex;

    if ( !criticalDiscovery ) {
        memory.opportunityPressure = static_cast<uint8_t>( std::min<uint32_t>( 4, memory.opportunityPressure + 1 ) );
        memory.opportunityPressureDay = currentDay;
    }

    bestResponder->GetPath().Truncate();
}

double AI::Planner::getTileArmyStrength( const Maps::Tile & tile )
{
    const auto [iter, inserted] = _tileArmyStrengthValues.try_emplace( tile.GetIndex(), 0.0 );
    if ( inserted ) {
        // Creating an Army instance is a relatively heavy operation, so cache it to speed up calculations
        static Army tileArmy;
        tileArmy.setFromTile( tile );

        iter->second = tileArmy.GetStrength();
    }

    return iter->second;
}

double AI::Planner::getResourcePriorityModifier( const int resource, const bool isMine ) const
{
    // Not all resources are equally valuable: 1 gold does not have the same value as 1 gemstone, so we need to
    // normalize the value of various resources.

    // For mines, let's determine the default relative priority based on the ratio of the amount of resources
    // extracted by these mines. For example, if a gold mine produces 1000 gold per day, an ore mine produces 2
    // units of ore per day, and a gem mine produces 1 gem per day, then the priority of one unit of ore will
    // correspond to the priority of 500 gold, and the priority of one gemstone will correspond to the priority
    // of 1000 gold. Evaluate the resources from mines in proportion to the amount of resources that these mines
    // bring in 2 days (mines should be more valuable than just resource piles).
    static const std::map<int, double> minePriorities = []() {
        std::map<int, double> result;

        const double goldMineIncome = ProfitConditions::FromMine( Resource::GOLD ).Get( Resource::GOLD );
        assert( goldMineIncome > 0 );

        Resource::forEach( Resource::ALL, [&result, goldMineIncome]( const int res ) {
            const int32_t resMineIncome = ProfitConditions::FromMine( res ).Get( res );
            assert( resMineIncome > 0 );
            if ( resMineIncome <= 0 ) {
                return;
            }

            result[res] = goldMineIncome / resMineIncome * 2;
        } );

        return result;
    }();

    // For one-time resource sources (such as piles, chests, campfires and so on), let's determine the default
    // relative priority based on the ratio of the usual amount of resources in these sources.
    static const std::map<int, double> pilePriorities = { // The amount of gold on the map is usually ~500-1500
                                                          { Resource::GOLD, 1 },
                                                          // The amount of wood and ore on the map is usually ~5-10
                                                          { Resource::WOOD, 125 },
                                                          { Resource::ORE, 125 },
                                                          // The amount of other resources on the map is usually ~2-5
                                                          { Resource::MERCURY, 250 },
                                                          { Resource::SULFUR, 250 },
                                                          { Resource::CRYSTAL, 250 },
                                                          { Resource::GEMS, 250 } };

    const std::map<int, double> & resourcePriorities = isMine ? minePriorities : pilePriorities;

    double prio = 1.0;

    const auto prioIter = resourcePriorities.find( resource );
    if ( prioIter != resourcePriorities.end() ) {
        prio = prioIter->second;
    }
    else {
        // This function has been called for an unknown resource, this should never happen
        assert( 0 );
    }

    for ( const BudgetEntry & budget : _budget ) {
        if ( budget.resource != resource ) {
            continue;
        }

        if ( budget.recurringCost ) {
            prio *= 1.5;
        }

        return ( budget.priority ) ? prio * 2.0 : prio;
    }

    return prio;
}

double AI::Planner::getFundsValueBasedOnPriority( const Funds & funds ) const
{
    double value = 0;

    Resource::forEach( funds.GetValidItems(), [this, &funds, &value]( const int res ) {
        const int amount = funds.Get( res );
        if ( amount <= 0 ) {
            return;
        }

        value += amount * getResourcePriorityModifier( res, false );
    } );

    return value;
}

void AI::Planner::updateMapActionObjectCache( const Kingdom & kingdom, const int mapIndex )
{
    const MP2::MapObjectType objectType = world.getTile( mapIndex ).getMainObjectType();

    if ( !isValuableAdventureMapObject( kingdom, objectType, mapIndex ) ) {
        _mapActionObjects.erase( mapIndex );
        _strategicTargetCooldowns.erase( mapIndex );

        return;
    }

    if ( auto cooldownIt = _strategicTargetCooldowns.find( mapIndex ); cooldownIt != _strategicTargetCooldowns.end() ) {
        const uint32_t currentDay = world.CountDay();

        if ( isCriticalTask( mapIndex ) ) {
            // A newly critical objective invalidates all previous failed-plan assumptions.
            _strategicTargetCooldowns.erase( cooldownIt );
        }
        else if ( cooldownIt->second.untilDay >= currentDay ) {
            _mapActionObjects.erase( mapIndex );
            return;
        }
        else {
            // Keep a tiny failure history after expiry so a target which repeatedly causes failed
            // diversions receives a slightly longer cooldown next time. The counter is capped at 3.
            cooldownIt->second.untilDay = 0;
        }
    }

    if ( const auto [iter, inserted] = _mapActionObjects.try_emplace( mapIndex, objectType ); !inserted ) {
        iter->second = objectType;
    }
}
