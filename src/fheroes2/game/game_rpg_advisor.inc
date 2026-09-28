// RPG Strategic Advisor implementation.
// This file is included only by game_hotkeys.cpp so the feature stays isolated from the base-game AI turn loop.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ai_hero_action.h"
#include "army.h"
#include "castle.h"
#include "color.h"
#include "cursor.h"
#include "game_mode.h"
#include "game_rpg.h"
#include "heroes.h"
#include "image.h"
#include "kingdom.h"
#include "maps.h"
#include "maps_tiles.h"
#include "maps_tiles_helper.h"
#include "mp2.h"
#include "players.h"
#include "screen.h"
#include "ui_text.h"
#include "ui_window.h"
#include "world.h"
#include "world_pathfinding.h"

namespace
{
    enum class StrategicAdviceKind : uint8_t
    {
        OBJECTIVE,
        DEFEND,
        EXPLORE,
        RESERVE
    };

    struct StrategicAdvice
    {
        StrategicAdviceKind kind{ StrategicAdviceKind::OBJECTIVE };
        int32_t heroId{ -1 };
        int32_t targetIndex{ -1 };
        double score{ 0.0 };
        uint32_t distance{ 0 };
        bool reachableThisTurn{ false };
        std::string title;
        std::string rationale;
    };

    constexpr size_t strategicAdviceCount = 5;

    std::string formatAdvisorRatio( const double ratio )
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision( 1 ) << ratio << 'x';
        return output.str();
    }

    bool areFriendlyColors( const PlayerColor first, const PlayerColor second )
    {
        if ( first == second ) {
            return true;
        }
        if ( first == PlayerColor::NONE || second == PlayerColor::NONE ) {
            return false;
        }
        return Players::isFriends( first, static_cast<PlayerColorsSet>( second ) );
    }

    bool isPermanentStrategicObject( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_LIGHTHOUSE:
        case MP2::OBJ_ABANDONED_MINE:
            return true;
        default:
            return false;
        }
    }

    bool isHeroGrowthObject( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE:
            return true;
        default:
            return false;
        }
    }

    double baseStrategicValue( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE: return 15000.0;
        case MP2::OBJ_HERO: return 13000.0;
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_LIGHTHOUSE:
        case MP2::OBJ_ABANDONED_MINE: return 9000.0;
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_SHIPWRECK_SURVIVOR: return 7200.0;
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES: return 6800.0;
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE: return 5600.0;
        case MP2::OBJ_MONSTER: return 5000.0;
        case MP2::OBJ_TREASURE_CHEST:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_WAGON: return 3300.0;
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
        case MP2::OBJ_FLOTSAM: return 2500.0;
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_LEAN_TO: return 2300.0;
        default: return 0.0;
        }
    }

    std::string strategicObjectName( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE: return "castle";
        case MP2::OBJ_HERO: return "enemy hero";
        case MP2::OBJ_MINE: return "mine";
        case MP2::OBJ_ALCHEMIST_LAB: return "alchemist lab";
        case MP2::OBJ_SAWMILL: return "sawmill";
        case MP2::OBJ_LIGHTHOUSE: return "lighthouse";
        case MP2::OBJ_ABANDONED_MINE: return "abandoned mine";
        case MP2::OBJ_ARTIFACT: return "artifact";
        case MP2::OBJ_SHIPWRECK_SURVIVOR: return "shipwreck survivor";
        case MP2::OBJ_FORT: return "fort";
        case MP2::OBJ_MERCENARY_CAMP: return "mercenary camp";
        case MP2::OBJ_WITCH_DOCTORS_HUT: return "witch doctor's hut";
        case MP2::OBJ_STANDING_STONES: return "standing stones";
        case MP2::OBJ_SHRINE_FIRST_CIRCLE: return "first-circle shrine";
        case MP2::OBJ_SHRINE_SECOND_CIRCLE: return "second-circle shrine";
        case MP2::OBJ_SHRINE_THIRD_CIRCLE: return "third-circle shrine";
        case MP2::OBJ_TEMPLE: return "temple";
        case MP2::OBJ_MONSTER: return "neutral army";
        case MP2::OBJ_TREASURE_CHEST: return "treasure chest";
        case MP2::OBJ_SEA_CHEST: return "sea chest";
        case MP2::OBJ_WAGON: return "wagon";
        case MP2::OBJ_RESOURCE: return "resource cache";
        case MP2::OBJ_BARREL: return "barrel";
        case MP2::OBJ_CAMPFIRE: return "campfire";
        case MP2::OBJ_FLOTSAM: return "flotsam";
        case MP2::OBJ_WINDMILL: return "windmill";
        case MP2::OBJ_WATER_WHEEL: return "water wheel";
        case MP2::OBJ_MAGIC_GARDEN: return "magic garden";
        case MP2::OBJ_LEAN_TO: return "lean-to";
        default: return "objective";
        }
    }

    std::string objectiveVerb( const MP2::MapObjectType objectType, const PlayerColor owner, const PlayerColor playerColor )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE: return owner == PlayerColor::NONE ? "Claim" : "Conquer";
        case MP2::OBJ_HERO: return "Intercept";
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_LIGHTHOUSE:
        case MP2::OBJ_ABANDONED_MINE: return areFriendlyColors( owner, playerColor ) ? "Reinforce" : "Capture";
        case MP2::OBJ_MONSTER: return "Defeat";
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_SHIPWRECK_SURVIVOR: return "Secure";
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE: return "Train at";
        default: return "Collect";
        }
    }

    double defenderStrengthForAdvice( const Maps::Tile & tile, const Heroes & attacker )
    {
        const MP2::MapObjectType objectType = tile.getMainObjectType();
        if ( objectType == MP2::OBJ_HERO ) {
            const Heroes * defender = tile.getHero();
            return defender != nullptr ? defender->GetArmy().GetStrength() : 0.0;
        }
        if ( objectType == MP2::OBJ_CASTLE ) {
            const Castle * castle = world.getCastleEntrance( Maps::GetPoint( tile.GetIndex() ) );
            return castle != nullptr ? castle->GetGarrisonStrength( attacker ) : 0.0;
        }
        if ( objectType == MP2::OBJ_MONSTER ) {
            const Army defenders( tile );
            return defenders.GetStrength();
        }
        return 0.0;
    }

    bool isCandidateObjectUseful( const Heroes & hero, const Kingdom & kingdom, const Maps::Tile & tile )
    {
        const MP2::MapObjectType objectType = tile.getMainObjectType();
        if ( baseStrategicValue( objectType ) <= 0.0 ) {
            return false;
        }

        if ( objectType == MP2::OBJ_HERO ) {
            const Heroes * targetHero = tile.getHero();
            return targetHero != nullptr && targetHero != &hero && !areFriendlyColors( kingdom.GetColor(), targetHero->GetColor() );
        }

        if ( objectType == MP2::OBJ_CASTLE ) {
            const Castle * castle = world.getCastleEntrance( Maps::GetPoint( tile.GetIndex() ) );
            return castle != nullptr && !areFriendlyColors( kingdom.GetColor(), castle->GetColor() );
        }

        if ( objectType == MP2::OBJ_MINE || objectType == MP2::OBJ_ALCHEMIST_LAB || objectType == MP2::OBJ_SAWMILL || objectType == MP2::OBJ_LIGHTHOUSE
             || objectType == MP2::OBJ_ABANDONED_MINE ) {
            return !areFriendlyColors( kingdom.GetColor(), Maps::getColorFromTile( tile ) );
        }

        if ( objectType == MP2::OBJ_ARTIFACT && hero.IsFullBagArtifacts() ) {
            return false;
        }

        if ( isHeroGrowthObject( objectType ) || objectType == MP2::OBJ_WINDMILL || objectType == MP2::OBJ_WATER_WHEEL || objectType == MP2::OBJ_MAGIC_GARDEN
             || objectType == MP2::OBJ_LEAN_TO ) {
            if ( hero.isVisited( tile ) || kingdom.isVisited( tile ) ) {
                return false;
            }
        }

        return true;
    }

    StrategicAdvice buildObjectiveAdvice( Heroes & hero, const Kingdom & kingdom, AIWorldPathfinder & pathfinder, const Maps::Tile & tile,
                                          const double strongestHeroStrength )
    {
        StrategicAdvice advice;
        const MP2::MapObjectType objectType = tile.getMainObjectType();
        const double heroStrength = std::max( 1.0, hero.GetArmy().GetStrength() );
        const uint32_t distance = pathfinder.getDistance( tile.GetIndex() );
        if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() ) {
            return advice;
        }

        const double defenderStrength = defenderStrengthForAdvice( tile, hero );
        const bool combat = defenderStrength > 0.0 || objectType == MP2::OBJ_HERO || objectType == MP2::OBJ_CASTLE || objectType == MP2::OBJ_MONSTER;
        const double matchup = defenderStrength > 0.0 ? heroStrength / defenderStrength : 0.0;

        // Refuse advice which is more likely to throw away the army than produce a strategic gain.
        if ( defenderStrength > 0.0 && matchup < 1.25 ) {
            return advice;
        }

        double score = baseStrategicValue( objectType );
        const bool permanent = isPermanentStrategicObject( objectType );
        if ( permanent ) {
            score *= 1.24;
        }

        if ( defenderStrength > 0.0 ) {
            score *= std::clamp( 0.80 + matchup * 0.18, 1.0, 1.65 );
        }

        const uint32_t currentMove = hero.GetMovePoints();
        const uint32_t maxMove = std::max<uint32_t>( 1, hero.GetMaxMovePoints() );
        const bool reachableThisTurn = distance <= currentMove;
        if ( reachableThisTurn ) {
            score *= 1.24;
        }

        const long double travelTurns = static_cast<long double>( distance ) / maxMove;
        score /= 1.0 + static_cast<double>( travelTurns ) * ( permanent ? 0.22 : 0.38 );

        const bool strongestHero = heroStrength >= strongestHeroStrength * 0.90;
        if ( combat ) {
            score *= strongestHero ? 1.22 : 0.92;
        }
        else if ( strongestHero ) {
            // Do not waste the kingdom's hammer on disposable pickups unless they are exceptionally close.
            score *= permanent ? 1.0 : 0.76;
        }
        else {
            score *= 1.10;
        }

        if ( objectType == MP2::OBJ_SHRINE_FIRST_CIRCLE || objectType == MP2::OBJ_SHRINE_SECOND_CIRCLE || objectType == MP2::OBJ_SHRINE_THIRD_CIRCLE ) {
            const double spellInvestment = fheroes2::RPG::spellcastingInvestmentPercent( kingdom.GetColor() );
            score *= 1.0 + std::min( 0.35, spellInvestment / 250.0 );
        }

        const Funds & funds = kingdom.GetFunds();
        if ( objectType == MP2::OBJ_MINE || objectType == MP2::OBJ_ALCHEMIST_LAB || objectType == MP2::OBJ_SAWMILL || objectType == MP2::OBJ_ABANDONED_MINE ) {
            if ( funds.gold < 5000 ) {
                score *= 1.15;
            }
            if ( funds.wood < 10 || funds.ore < 10 ) {
                score *= 1.12;
            }
        }

        advice.kind = StrategicAdviceKind::OBJECTIVE;
        advice.heroId = hero.GetID();
        advice.targetIndex = tile.GetIndex();
        advice.score = score;
        advice.distance = distance;
        advice.reachableThisTurn = reachableThisTurn;

        PlayerColor owner = PlayerColor::NONE;
        if ( objectType == MP2::OBJ_CASTLE ) {
            if ( const Castle * castle = world.getCastleEntrance( Maps::GetPoint( tile.GetIndex() ) ) ) {
                owner = castle->GetColor();
            }
        }
        else if ( objectType == MP2::OBJ_MINE || objectType == MP2::OBJ_ALCHEMIST_LAB || objectType == MP2::OBJ_SAWMILL || objectType == MP2::OBJ_LIGHTHOUSE
                 || objectType == MP2::OBJ_ABANDONED_MINE ) {
            owner = Maps::getColorFromTile( tile );
        }

        std::string targetName = strategicObjectName( objectType );
        if ( objectType == MP2::OBJ_HERO ) {
            if ( const Heroes * targetHero = tile.getHero() ) {
                targetName = targetHero->GetName();
            }
        }
        else if ( objectType == MP2::OBJ_CASTLE ) {
            if ( const Castle * castle = world.getCastleEntrance( Maps::GetPoint( tile.GetIndex() ) ) ) {
                targetName = castle->GetName();
            }
        }

        advice.title = hero.GetName() + ": " + objectiveVerb( objectType, owner, kingdom.GetColor() ) + " " + targetName;
        advice.rationale = permanent ? "Permanent strategic gain" : combat ? "High-value combat opportunity" : "Efficient progression gain";
        if ( defenderStrength > 0.0 ) {
            advice.rationale += " | " + formatAdvisorRatio( matchup ) + " army edge";
        }
        advice.rationale += reachableThisTurn ? " | reachable this turn" : " | " + std::to_string( std::max<uint32_t>( 1, ( distance + maxMove - 1 ) / maxMove ) ) + " turns";
        return advice;
    }

    void appendDefensiveAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        const PlayerColor color = kingdom.GetColor();
        std::vector<const Heroes *> visibleThreats;
        const int mapSize = world.w() * world.h();
        for ( int32_t index = 0; index < mapSize; ++index ) {
            const Maps::Tile & tile = world.getTile( index );
            if ( tile.isFog( color ) || tile.getMainObjectType() != MP2::OBJ_HERO ) {
                continue;
            }
            const Heroes * enemy = tile.getHero();
            if ( enemy != nullptr && !areFriendlyColors( color, enemy->GetColor() ) ) {
                visibleThreats.emplace_back( enemy );
            }
        }

        for ( Castle * castle : kingdom.GetCastles() ) {
            if ( castle == nullptr ) {
                continue;
            }

            const Heroes * closestThreat = nullptr;
            uint32_t closestTiles = std::numeric_limits<uint32_t>::max();
            for ( const Heroes * enemy : visibleThreats ) {
                const uint32_t tiles = Maps::GetApproximateDistance( enemy->GetIndex(), castle->GetIndex() );
                if ( tiles < closestTiles ) {
                    closestTiles = tiles;
                    closestThreat = enemy;
                }
            }
            if ( closestThreat == nullptr || closestTiles > 9 ) {
                continue;
            }

            const double enemyStrength = std::max( 1.0, closestThreat->GetArmy().GetStrength() );
            const double garrisonStrength = std::max( 1.0, castle->GetGarrisonStrength( closestThreat ) );
            if ( enemyStrength < garrisonStrength * 0.70 ) {
                continue;
            }

            for ( Heroes * hero : kingdom.GetHeroes() ) {
                if ( hero == nullptr || !hero->isActive() || hero->GetIndex() == castle->GetIndex() || !hero->MayStillMove( false, false ) ) {
                    continue;
                }

                AIWorldPathfinder pathfinder;
                pathfinder.setMinimalArmyStrengthAdvantage( 1.10 );
                pathfinder.reEvaluateIfNeeded( *hero );
                const uint32_t distance = pathfinder.getDistance( castle->GetIndex() );
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() ) {
                    continue;
                }

                const uint32_t maxMove = std::max<uint32_t>( 1, hero->GetMaxMovePoints() );
                const bool reachableThisTurn = distance <= hero->GetMovePoints();
                const double heroStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
                const double threatRatio = enemyStrength / garrisonStrength;
                double score = 17500.0 + threatRatio * 3500.0 + std::max( 0.0, heroStrength / strongestHeroStrength ) * 800.0;
                score /= 1.0 + static_cast<double>( distance ) / maxMove * 0.25;
                if ( reachableThisTurn ) {
                    score *= 1.35;
                }

                StrategicAdvice advice;
                advice.kind = StrategicAdviceKind::DEFEND;
                advice.heroId = hero->GetID();
                advice.targetIndex = castle->GetIndex();
                advice.score = score;
                advice.distance = distance;
                advice.reachableThisTurn = reachableThisTurn;
                advice.title = hero->GetName() + ": Reinforce " + castle->GetName();
                advice.rationale = "Urgent defense | enemy " + closestThreat->GetName() + " is " + std::to_string( closestTiles ) + " tiles away";
                advice.rationale += " | threat " + formatAdvisorRatio( threatRatio ) + " garrison";
                candidates.emplace_back( std::move( advice ) );
            }
        }
    }

    void appendExplorationAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
            pathfinder.reEvaluateIfNeeded( *hero );
            const auto [targetIndex, frontier] = pathfinder.getFogDiscoveryTile( *hero );
            if ( targetIndex < 0 || targetIndex == hero->GetIndex() ) {
                continue;
            }
            const uint32_t distance = pathfinder.getDistance( targetIndex );
            if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() ) {
                continue;
            }

            const double strength = std::max( 1.0, hero->GetArmy().GetStrength() );
            const uint32_t maxMove = std::max<uint32_t>( 1, hero->GetMaxMovePoints() );
            const bool reachableThisTurn = distance <= hero->GetMovePoints();
            double score = frontier ? 6000.0 : 4700.0;
            score *= strength < strongestHeroStrength * 0.75 ? 1.24 : 0.82;
            score /= 1.0 + static_cast<double>( distance ) / maxMove * 0.30;
            if ( reachableThisTurn ) {
                score *= 1.18;
            }

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::EXPLORE;
            advice.heroId = hero->GetID();
            advice.targetIndex = targetIndex;
            advice.score = score;
            advice.distance = distance;
            advice.reachableThisTurn = reachableThisTurn;
            advice.title = hero->GetName() + std::string( frontier ? ": Break into new territory" : ": Scout the fog frontier" );
            advice.rationale = frontier ? "Expansion route with likely new region access" : "Safe information gain without wasting the main army";
            advice.rationale += reachableThisTurn ? " | reachable this turn" : " | efficient scouting route";
            candidates.emplace_back( std::move( advice ) );
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdvice( Kingdom & kingdom )
    {
        std::vector<StrategicAdvice> candidates;
        double strongestHeroStrength = 1.0;
        for ( const Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr && hero->isActive() ) {
                strongestHeroStrength = std::max( strongestHeroStrength, hero->GetArmy().GetStrength() );
            }
        }

        appendDefensiveAdvice( candidates, kingdom, strongestHeroStrength );

        const int mapSize = world.w() * world.h();
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
            pathfinder.setSpellPointsReserveRatio( 0.30 );
            pathfinder.reEvaluateIfNeeded( *hero );

            for ( int32_t index = 0; index < mapSize; ++index ) {
                if ( index == hero->GetIndex() ) {
                    continue;
                }
                const Maps::Tile & tile = world.getTile( index );
                if ( tile.isFog( kingdom.GetColor() ) || !isCandidateObjectUseful( *hero, kingdom, tile ) ) {
                    continue;
                }

                StrategicAdvice advice = buildObjectiveAdvice( *hero, kingdom, pathfinder, tile, strongestHeroStrength );
                if ( advice.heroId >= 0 && advice.targetIndex >= 0 && advice.score > 0.0 && std::isfinite( advice.score ) ) {
                    candidates.emplace_back( std::move( advice ) );
                }
            }
        }

        appendExplorationAdvice( candidates, kingdom, strongestHeroStrength );

        std::sort( candidates.begin(), candidates.end(), []( const StrategicAdvice & left, const StrategicAdvice & right ) {
            if ( left.score != right.score ) {
                return left.score > right.score;
            }
            if ( left.reachableThisTurn != right.reachableThisTurn ) {
                return left.reachableThisTurn;
            }
            return left.distance < right.distance;
        } );

        std::vector<StrategicAdvice> chosen;
        chosen.reserve( strategicAdviceCount );
        std::set<int32_t> usedTargets;
        std::map<int32_t, size_t> perHero;

        // First pass favors strategic diversity: no duplicate destinations and no hero monopolizes the list.
        for ( const StrategicAdvice & advice : candidates ) {
            if ( chosen.size() >= strategicAdviceCount ) {
                break;
            }
            if ( usedTargets.count( advice.targetIndex ) > 0 || perHero[advice.heroId] >= 2 ) {
                continue;
            }
            chosen.emplace_back( advice );
            usedTargets.insert( advice.targetIndex );
            ++perHero[advice.heroId];
        }

        // Second pass relaxes only the hero cap while still avoiding duplicate destinations.
        for ( const StrategicAdvice & advice : candidates ) {
            if ( chosen.size() >= strategicAdviceCount ) {
                break;
            }
            if ( usedTargets.count( advice.targetIndex ) > 0 ) {
                continue;
            }
            chosen.emplace_back( advice );
            usedTargets.insert( advice.targetIndex );
        }

        // An exhausted map can genuinely have fewer than five safe actions. Keep the five-card contract
        // without inventing reckless targets: reserving a hero for defense is a valid strategic order.
        const VecHeroes & heroes = kingdom.GetHeroes();
        size_t fallbackIndex = 0;
        while ( chosen.size() < strategicAdviceCount && !heroes.empty() ) {
            Heroes * hero = heroes[fallbackIndex % heroes.size()];
            ++fallbackIndex;
            if ( hero == nullptr || !hero->isActive() ) {
                if ( fallbackIndex > heroes.size() * 3 ) {
                    break;
                }
                continue;
            }

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::RESERVE;
            advice.heroId = hero->GetID();
            advice.targetIndex = hero->GetIndex();
            advice.score = 0.0;
            advice.title = hero->GetName() + ": Hold as strategic reserve";
            advice.rationale = "No higher-value safe objective remains | preserve army and movement for the next turn";
            chosen.emplace_back( std::move( advice ) );
        }

        return chosen;
    }

    int selectStrategicAdvice( const std::vector<StrategicAdvice> & advices )
    {
        if ( advices.empty() ) {
            return -1;
        }

        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();
        fheroes2::StandardWindow window( 570, 372, true, display );
        const fheroes2::Rect area = window.activeArea();
        std::array<fheroes2::Rect, strategicAdviceCount> rows{};
        const fheroes2::Rect closeArea{ area.x + area.width - 104, area.y + area.height - 27, 92, 21 };

        window.render();
        window.applyGemDecoratedCorners();

        const fheroes2::Text title( "STRATEGIC ADVISOR", fheroes2::FontType::normalYellow() );
        title.draw( area.x + 10, area.y + 5, area.width - 20, display );
        const fheroes2::Text subtitle( "Five live recommendations - click one and the AI will execute only that order.", fheroes2::FontType::smallWhite() );
        subtitle.draw( area.x + 10, area.y + 25, area.width - 20, display );

        for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
            rows[index] = { area.x + 12, area.y + 49 + static_cast<int32_t>( index ) * 56, area.width - 24, 50 };
            window.applyTextBackgroundShading( rows[index] );

            if ( index >= advices.size() ) {
                continue;
            }

            fheroes2::Text heading( std::to_string( index + 1 ) + ". " + advices[index].title, fheroes2::FontType::smallYellow() );
            heading.fitToOneRow( rows[index].width - 16 );
            heading.draw( rows[index].x + 8, rows[index].y + 7, display );

            fheroes2::Text reason( advices[index].rationale, fheroes2::FontType::smallWhite() );
            reason.fitToOneRow( rows[index].width - 16 );
            reason.draw( rows[index].x + 8, rows[index].y + 27, display );
        }

        window.applyTextBackgroundShading( closeArea );
        const fheroes2::Text closeText( "Close", fheroes2::FontType::smallYellow() );
        closeText.draw( closeArea.x, closeArea.y + 5, closeArea.width, display );
        const fheroes2::Text hint( "Keys 1-5 also execute | F7/Esc closes", fheroes2::FontType::smallWhite() );
        hint.draw( area.x + 12, area.y + area.height - 22, area.width - 132, display );
        display.render( window.totalArea() );

        LocalEvent & event = LocalEvent::Get();
        while ( event.HandleEvents() ) {
            if ( Game::HotKeyCloseWindow() || event.isKeyPressed( fheroes2::Key::KEY_F7 ) || event.MouseClickLeft( closeArea ) ) {
                return -1;
            }

            for ( size_t index = 0; index < advices.size() && index < strategicAdviceCount; ++index ) {
                if ( event.MouseClickLeft( rows[index] ) ) {
                    return static_cast<int>( index );
                }

                const fheroes2::Key numberKey = static_cast<fheroes2::Key>( static_cast<int32_t>( fheroes2::Key::KEY_1 ) + static_cast<int32_t>( index ) );
                if ( event.isKeyPressed( numberKey ) ) {
                    return static_cast<int>( index );
                }
            }
        }

        return -1;
    }

    bool executeStrategicAdvice( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Heroes * hero = world.GetHeroes( advice.heroId );
        if ( hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() ) {
            return false;
        }

        if ( advice.kind == StrategicAdviceKind::RESERVE ) {
            hero->SetModes( Heroes::SLEEPER );
            return true;
        }

        if ( advice.targetIndex < 0 || advice.targetIndex >= world.w() * world.h() || hero->GetIndex() == advice.targetIndex ) {
            return false;
        }

        AIWorldPathfinder pathfinder;
        pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
        pathfinder.setSpellPointsReserveRatio( 0.30 );
        pathfinder.reEvaluateIfNeeded( *hero );
        std::list<Route::Step> route = pathfinder.buildPath( advice.targetIndex, true );
        if ( route.empty() ) {
            return false;
        }

        hero->GetPath().setPath( std::move( route ) );

        // The timeout scope lets AI-only decision dialogs resolve automatically while leaving the
        // player's adventure-map ownership untouched. This executes one order, not an AI turn.
        const fheroes2::AutoPlayPopupTimeoutScope timeoutScope( true );
        const fheroes2::GameMode result = AI::HeroesMove( *hero );
        return result == fheroes2::GameMode::END_TURN || hero->isActive();
    }

    void showRpgStrategicAdvisor()
    {
        Settings & settings = Settings::Get();
        Player * player = settings.GetPlayers().GetCurrent();
        if ( player == nullptr || !player->isControlHuman() || Battle::GetArena() != nullptr ) {
            return;
        }

        const PlayerColor color = player->GetColor();
        if ( color == PlayerColor::NONE ) {
            return;
        }

        Kingdom & kingdom = world.GetKingdom( color );
        for ( size_t refresh = 0; refresh < 64; ++refresh ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdvice( kingdom );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "Strategic Advisor", "No safe actionable advice is available right now.", Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdvice( advices );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            if ( !executeStrategicAdvice( kingdom, advices[selection] ) ) {
                const fheroes2::AutoPlayPopupTimeoutScope timeoutScope( true );
                fheroes2::showStandardTextMessage( "Strategic Advisor", "That order became unreachable. Recalculating recommendations.", Dialog::ZERO );
            }

            // Deliberately loop: every executed order changes the strategic position, so the next five
            // are regenerated from the new map, army, economy and movement state instead of going stale.
        }
    }
}
