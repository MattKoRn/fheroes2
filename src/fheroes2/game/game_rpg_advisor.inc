// RPG Strategic Advisor implementation.
// This file is included only by game_hotkeys.cpp so the advisor remains isolated from the base game.

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
#include <utility>
#include <vector>

#include "ai_hero_action.h"
#include "army.h"
#include "battle_arena.h"
#include "castle.h"
#include "color.h"
#include "cursor.h"
#include "dialog.h"
#include "game_interface.h"
#include "game_mode.h"
#include "game_rpg.h"
#include "heroes.h"
#include "icn.h"
#include "image.h"
#include "kingdom.h"
#include "localevent.h"
#include "maps.h"
#include "maps_tiles.h"
#include "maps_tiles_helper.h"
#include "mp2.h"
#include "players.h"
#include "screen.h"
#include "settings.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "ui_window.h"
#include "world.h"
#include "world_pathfinding.h"

namespace
{
    enum class StrategicAdviceKind : uint8_t
    {
        OFFENSE,
        DEFENSE,
        ECONOMY,
        EXPLORATION,
        DEVELOPMENT,
        LOGISTICS,
        OPPORTUNITY,
        RESERVE
    };

    struct StrategicAdvice
    {
        StrategicAdviceKind kind{ StrategicAdviceKind::OPPORTUNITY };
        int32_t heroId{ -1 };
        int32_t targetIndex{ -1 };
        MP2::MapObjectType targetObjectType{ MP2::OBJ_NONE };
        double score{ 0.0 };
        uint32_t distance{ 0 };
        bool reachableThisTurn{ false };
        std::string title;
        std::string rationale;
    };

    struct AdviceExecutionResult
    {
        bool success{ false };
        std::string message;
    };

    constexpr size_t strategicAdviceCount = 5;

    const char * adviceKindName( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::OFFENSE:
            return "WAR";
        case StrategicAdviceKind::DEFENSE:
            return "DEFENSE";
        case StrategicAdviceKind::ECONOMY:
            return "ECONOMY";
        case StrategicAdviceKind::EXPLORATION:
            return "SCOUTING";
        case StrategicAdviceKind::DEVELOPMENT:
            return "HERO GROWTH";
        case StrategicAdviceKind::LOGISTICS:
            return "LOGISTICS";
        case StrategicAdviceKind::OPPORTUNITY:
            return "OPPORTUNITY";
        case StrategicAdviceKind::RESERVE:
            return "RESERVE";
        default:
            return "COUNCIL";
        }
    }

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

    StrategicAdviceKind classifyStrategicObject( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
        case MP2::OBJ_HERO:
        case MP2::OBJ_MONSTER:
            return StrategicAdviceKind::OFFENSE;

        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_LIGHTHOUSE:
        case MP2::OBJ_ABANDONED_MINE:
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
        case MP2::OBJ_FLOTSAM:
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_LEAN_TO:
            return StrategicAdviceKind::ECONOMY;

        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE:
            return StrategicAdviceKind::DEVELOPMENT;

        default:
            return StrategicAdviceKind::OPPORTUNITY;
        }
    }

    double baseStrategicValue( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
            return 16000.0;
        case MP2::OBJ_HERO:
            return 14500.0;
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_LIGHTHOUSE:
        case MP2::OBJ_ABANDONED_MINE:
            return 9800.0;
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
            return 7600.0;
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
            return 7200.0;
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE:
            return 6400.0;
        case MP2::OBJ_MONSTER:
            return 6100.0;
        case MP2::OBJ_TREASURE_CHEST:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_WAGON:
            return 4300.0;
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
        case MP2::OBJ_FLOTSAM:
            return 3500.0;
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_LEAN_TO:
            return 3200.0;
        default:
            return 0.0;
        }
    }

    std::string strategicObjectName( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
            return "castle";
        case MP2::OBJ_HERO:
            return "enemy hero";
        case MP2::OBJ_MINE:
            return "mine";
        case MP2::OBJ_ALCHEMIST_LAB:
            return "alchemist lab";
        case MP2::OBJ_SAWMILL:
            return "sawmill";
        case MP2::OBJ_LIGHTHOUSE:
            return "lighthouse";
        case MP2::OBJ_ABANDONED_MINE:
            return "abandoned mine";
        case MP2::OBJ_ARTIFACT:
            return "artifact";
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
            return "shipwreck survivor";
        case MP2::OBJ_FORT:
            return "fort";
        case MP2::OBJ_MERCENARY_CAMP:
            return "mercenary camp";
        case MP2::OBJ_WITCH_DOCTORS_HUT:
            return "witch doctor's hut";
        case MP2::OBJ_STANDING_STONES:
            return "standing stones";
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
            return "first-circle shrine";
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
            return "second-circle shrine";
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
            return "third-circle shrine";
        case MP2::OBJ_TEMPLE:
            return "temple";
        case MP2::OBJ_MONSTER:
            return "neutral army";
        case MP2::OBJ_TREASURE_CHEST:
            return "treasure chest";
        case MP2::OBJ_SEA_CHEST:
            return "sea chest";
        case MP2::OBJ_WAGON:
            return "wagon";
        case MP2::OBJ_RESOURCE:
            return "resource cache";
        case MP2::OBJ_BARREL:
            return "barrel";
        case MP2::OBJ_CAMPFIRE:
            return "campfire";
        case MP2::OBJ_FLOTSAM:
            return "flotsam";
        case MP2::OBJ_WINDMILL:
            return "windmill";
        case MP2::OBJ_WATER_WHEEL:
            return "water wheel";
        case MP2::OBJ_MAGIC_GARDEN:
            return "magic garden";
        case MP2::OBJ_LEAN_TO:
            return "lean-to";
        default:
            return "objective";
        }
    }

    std::string objectiveVerb( const MP2::MapObjectType objectType, const PlayerColor owner )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
            return owner == PlayerColor::NONE ? "Claim" : "Conquer";
        case MP2::OBJ_HERO:
            return "Intercept";
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_LIGHTHOUSE:
        case MP2::OBJ_ABANDONED_MINE:
            return "Capture";
        case MP2::OBJ_MONSTER:
            return "Defeat";
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
            return "Secure";
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE:
            return "Develop at";
        default:
            return "Collect";
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

        // The council should never recommend a routine attack that is more likely to destroy the kingdom's field army.
        if ( defenderStrength > 0.0 && matchup < 1.25 ) {
            return advice;
        }

        const StrategicAdviceKind kind = classifyStrategicObject( objectType );
        double score = baseStrategicValue( objectType );
        const bool permanent = isPermanentStrategicObject( objectType );

        if ( permanent ) {
            score *= 1.27;
        }
        if ( defenderStrength > 0.0 ) {
            score *= std::clamp( 0.82 + matchup * 0.17, 1.0, 1.70 );
        }

        const uint32_t currentMove = hero.GetMovePoints();
        const uint32_t maxMove = std::max<uint32_t>( 1, hero.GetMaxMovePoints() );
        const bool reachableThisTurn = distance <= currentMove;
        if ( reachableThisTurn ) {
            score *= 1.25;
        }

        const long double travelTurns = static_cast<long double>( distance ) / maxMove;
        score /= 1.0 + static_cast<double>( travelTurns ) * ( permanent ? 0.20 : 0.36 );

        const bool strongestHero = heroStrength >= strongestHeroStrength * 0.90;
        if ( combat ) {
            score *= strongestHero ? 1.25 : 0.88;
        }
        else if ( strongestHero ) {
            score *= permanent ? 1.0 : 0.72;
        }
        else {
            score *= 1.12;
        }

        if ( kind == StrategicAdviceKind::DEVELOPMENT ) {
            score *= strongestHero ? 1.08 : 1.16;
        }

        if ( objectType == MP2::OBJ_SHRINE_FIRST_CIRCLE || objectType == MP2::OBJ_SHRINE_SECOND_CIRCLE || objectType == MP2::OBJ_SHRINE_THIRD_CIRCLE ) {
            const double spellInvestment = fheroes2::RPG::spellcastingInvestmentPercent( kingdom.GetColor() );
            score *= 1.0 + std::min( 0.40, spellInvestment / 220.0 );
        }

        const Funds & funds = kingdom.GetFunds();
        if ( kind == StrategicAdviceKind::ECONOMY ) {
            if ( funds.gold < 5000 ) {
                score *= 1.18;
            }
            if ( funds.wood < 10 || funds.ore < 10 ) {
                score *= 1.14;
            }
        }

        advice.kind = kind;
        advice.heroId = hero.GetID();
        advice.targetIndex = tile.GetIndex();
        advice.targetObjectType = objectType;
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

        advice.title = hero.GetName() + ": " + objectiveVerb( objectType, owner ) + " " + targetName;

        switch ( kind ) {
        case StrategicAdviceKind::OFFENSE:
            advice.rationale = permanent ? "Decisive territorial gain" : "Favorable combat window";
            break;
        case StrategicAdviceKind::ECONOMY:
            advice.rationale = permanent ? "Permanent income and map control" : "Efficient resource pickup";
            break;
        case StrategicAdviceKind::DEVELOPMENT:
            advice.rationale = "Permanent hero development is worth the travel";
            break;
        case StrategicAdviceKind::OPPORTUNITY:
            advice.rationale = "High-value gain with low strategic commitment";
            break;
        default:
            advice.rationale = "Strong strategic value";
            break;
        }

        if ( defenderStrength > 0.0 ) {
            advice.rationale += " | " + formatAdvisorRatio( matchup ) + " army edge";
        }
        advice.rationale += reachableThisTurn
                                ? " | this turn"
                                : " | about " + std::to_string( std::max<uint32_t>( 1, ( distance + maxMove - 1 ) / maxMove ) ) + " turns";

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
            if ( castle == nullptr || ( castle->GetHero() != nullptr && castle->GetHero()->isActive() ) ) {
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

            if ( closestThreat == nullptr || closestTiles > 10 ) {
                continue;
            }

            const double enemyStrength = std::max( 1.0, closestThreat->GetArmy().GetStrength() );
            const double garrisonStrength = std::max( 1.0, castle->GetGarrisonStrength( closestThreat ) );
            if ( enemyStrength < garrisonStrength * 0.68 ) {
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

                double score = 18500.0 + threatRatio * 4000.0 + heroStrength / strongestHeroStrength * 1200.0;
                score /= 1.0 + static_cast<double>( distance ) / maxMove * 0.23;
                if ( reachableThisTurn ) {
                    score *= 1.38;
                }

                StrategicAdvice advice;
                advice.kind = StrategicAdviceKind::DEFENSE;
                advice.heroId = hero->GetID();
                advice.targetIndex = castle->GetIndex();
                advice.targetObjectType = MP2::OBJ_CASTLE;
                advice.score = score;
                advice.distance = distance;
                advice.reachableThisTurn = reachableThisTurn;
                advice.title = hero->GetName() + ": Defend " + castle->GetName();
                advice.rationale = "Enemy " + closestThreat->GetName() + " is only " + std::to_string( closestTiles ) + " tiles away";
                advice.rationale += " | threat is " + formatAdvisorRatio( threatRatio ) + " current garrison";
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

            double score = frontier ? 7200.0 : 5600.0;
            score *= strength < strongestHeroStrength * 0.75 ? 1.30 : 0.78;
            score /= 1.0 + static_cast<double>( distance ) / maxMove * 0.28;
            if ( reachableThisTurn ) {
                score *= 1.20;
            }

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::EXPLORATION;
            advice.heroId = hero->GetID();
            advice.targetIndex = targetIndex;
            advice.score = score;
            advice.distance = distance;
            advice.reachableThisTurn = reachableThisTurn;
            advice.title = hero->GetName() + std::string( frontier ? ": Open a new frontier" : ": Scout the fog" );
            advice.rationale = frontier ? "Best route toward undiscovered territory" : "Gain information without diverting the main army";
            advice.rationale += reachableThisTurn ? " | this turn" : " | low-cost scouting route";
            candidates.emplace_back( std::move( advice ) );
        }
    }

    void appendLogisticsAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) ) {
                continue;
            }

            const double heroStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
            if ( heroStrength > strongestHeroStrength * 0.72 ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.10 );
            pathfinder.reEvaluateIfNeeded( *hero );

            Castle * bestCastle = nullptr;
            uint32_t bestDistance = std::numeric_limits<uint32_t>::max();
            double bestCastleStrength = 0.0;

            for ( Castle * castle : kingdom.GetCastles() ) {
                if ( castle == nullptr || castle->GetIndex() == hero->GetIndex() || castle->GetHero() != nullptr ) {
                    continue;
                }

                const uint32_t distance = pathfinder.getDistance( castle->GetIndex() );
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() ) {
                    continue;
                }

                const double castleStrength = castle->GetArmy().GetStrength();
                const bool better = distance < bestDistance || ( distance == bestDistance && castleStrength > bestCastleStrength );
                if ( better ) {
                    bestCastle = castle;
                    bestDistance = distance;
                    bestCastleStrength = castleStrength;
                }
            }

            if ( bestCastle == nullptr ) {
                continue;
            }

            const uint32_t maxMove = std::max<uint32_t>( 1, hero->GetMaxMovePoints() );
            const bool reachableThisTurn = bestDistance <= hero->GetMovePoints();
            const double vulnerability = strongestHeroStrength / heroStrength;

            double score = 6100.0 + std::min( 4.0, vulnerability ) * 900.0;
            score += std::min( 2500.0, bestCastleStrength / std::max( 1.0, strongestHeroStrength ) * 1600.0 );
            score /= 1.0 + static_cast<double>( bestDistance ) / maxMove * 0.24;
            if ( reachableThisTurn ) {
                score *= 1.22;
            }

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::LOGISTICS;
            advice.heroId = hero->GetID();
            advice.targetIndex = bestCastle->GetIndex();
            advice.targetObjectType = MP2::OBJ_CASTLE;
            advice.score = score;
            advice.distance = bestDistance;
            advice.reachableThisTurn = reachableThisTurn;
            advice.title = hero->GetName() + ": Regroup at " + bestCastle->GetName();
            advice.rationale = "Vulnerable field army | consolidate at a friendly stronghold before taking larger risks";
            advice.rationale += reachableThisTurn ? " | this turn" : " | efficient withdrawal";
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
        appendLogisticsAdvice( candidates, kingdom, strongestHeroStrength );

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
        std::set<StrategicAdviceKind> usedKinds;
        std::map<int32_t, size_t> perHero;

        // The first pass is intentionally category-first. If the strategic situation supports five different
        // kinds of good moves, the council will show five different kinds instead of five copies of "capture X".
        for ( const StrategicAdvice & advice : candidates ) {
            if ( chosen.size() >= strategicAdviceCount ) {
                break;
            }
            if ( advice.kind == StrategicAdviceKind::RESERVE || usedKinds.count( advice.kind ) > 0 || usedTargets.count( advice.targetIndex ) > 0
                 || perHero[advice.heroId] >= 2 ) {
                continue;
            }

            chosen.emplace_back( advice );
            usedKinds.insert( advice.kind );
            usedTargets.insert( advice.targetIndex );
            ++perHero[advice.heroId];
        }

        // Fill remaining slots with the next best distinct destinations after diversity has been guaranteed.
        for ( const StrategicAdvice & advice : candidates ) {
            if ( chosen.size() >= strategicAdviceCount ) {
                break;
            }
            if ( usedTargets.count( advice.targetIndex ) > 0 || perHero[advice.heroId] >= 3 ) {
                continue;
            }

            chosen.emplace_back( advice );
            usedTargets.insert( advice.targetIndex );
            ++perHero[advice.heroId];
        }

        // A genuinely exhausted map can have fewer than five safe actions. Reserve orders keep the five-card
        // contract without inventing suicidal attacks or fake objectives.
        const VecHeroes & heroes = kingdom.GetHeroes();
        size_t fallbackIndex = 0;
        size_t fallbackAttempts = 0;
        while ( chosen.size() < strategicAdviceCount && !heroes.empty() && fallbackAttempts < heroes.size() * 4 ) {
            Heroes * hero = heroes[fallbackIndex % heroes.size()];
            ++fallbackIndex;
            ++fallbackAttempts;

            if ( hero == nullptr || !hero->isActive() ) {
                continue;
            }

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::RESERVE;
            advice.heroId = hero->GetID();
            advice.targetIndex = hero->GetIndex();
            advice.score = 0.0;
            advice.title = hero->GetName() + ": Hold the strategic reserve";
            advice.rationale = "No better safe order remains | preserve this army for next turn";
            chosen.emplace_back( std::move( advice ) );
        }

        return chosen;
    }

    int selectStrategicAdvice( const std::vector<StrategicAdvice> & advices, const std::string & councilReport )
    {
        if ( advices.empty() ) {
            return -1;
        }

        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();
        fheroes2::StandardWindow window( 584, 390, true, display );
        const fheroes2::Rect area = window.activeArea();
        const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();

        std::array<fheroes2::Rect, strategicAdviceCount> rows{};
        fheroes2::Button closeButton;
        window.renderButton( closeButton, isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD, 0, 1, { 14, 8 },
                             fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );

        size_t hoverIndex = strategicAdviceCount;
        bool redraw = true;

        LocalEvent & event = LocalEvent::Get();
        while ( event.HandleEvents() ) {
            size_t newHoverIndex = strategicAdviceCount;
            for ( size_t index = 0; index < advices.size() && index < strategicAdviceCount; ++index ) {
                if ( event.isMouseCursorPosInArea( rows[index] ) ) {
                    newHoverIndex = index;
                    break;
                }
            }
            if ( newHoverIndex != hoverIndex ) {
                hoverIndex = newHoverIndex;
                redraw = true;
            }

            if ( redraw ) {
                window.render();
                window.applyGemDecoratedCorners();

                const fheroes2::Text title( "WAR COUNCIL", fheroes2::FontType::normalYellow() );
                title.draw( area.x + 10, area.y + 4, area.width - 20, display );

                const fheroes2::Text subtitle( "Royal Strategist - choose one order for the AI to carry out.", fheroes2::FontType::smallWhite() );
                subtitle.draw( area.x + 10, area.y + 23, area.width - 20, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( councilReport, fheroes2::FontType::smallYellow() );
                    report.fitToOneRow( area.width - 24 );
                    report.draw( area.x + 12, area.y + 39, display );
                }

                for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
                    rows[index] = { area.x + 12, area.y + 56 + static_cast<int32_t>( index ) * 55, area.width - 24, 49 };
                    window.applyTextBackgroundShading( rows[index] );

                    if ( index >= advices.size() ) {
                        continue;
                    }

                    const StrategicAdvice & advice = advices[index];
                    if ( const Heroes * hero = world.GetHeroes( advice.heroId ) ) {
                        hero->PortraitRedraw( rows[index].x + 6, rows[index].y + 6, PORT_SMALL, display );
                    }

                    const int32_t textX = rows[index].x + 52;
                    const int32_t textWidth = rows[index].width - 62;

                    std::string headingText = ( hoverIndex == index ? "> " : "" );
                    headingText += std::to_string( index + 1 ) + ". [" + adviceKindName( advice.kind ) + "] " + advice.title;
                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 6, display );

                    fheroes2::Text reason( advice.rationale, fheroes2::FontType::smallWhite() );
                    reason.fitToOneRow( textWidth );
                    reason.draw( textX, rows[index].y + 25, display );
                }

                const fheroes2::Text hint( "Click an order or press 1-5.  Esc cancels.", fheroes2::FontType::smallWhite() );
                hint.draw( area.x + 12, area.y + area.height - 22, area.width - 126, display );

                closeButton.draw();
                display.render( window.totalArea() );
                redraw = false;
            }

            closeButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( closeButton.area() ) );

            if ( Game::HotKeyCloseWindow() || event.MouseClickLeft( closeButton.area() ) ) {
                return -1;
            }

            for ( size_t index = 0; index < advices.size() && index < strategicAdviceCount; ++index ) {
                if ( event.MouseClickLeft( rows[index] ) ) {
                    return static_cast<int>( index );
                }

                const fheroes2::Key numberKey
                    = static_cast<fheroes2::Key>( static_cast<int32_t>( fheroes2::Key::KEY_1 ) + static_cast<int32_t>( index ) );
                if ( event.isKeyPressed( numberKey ) ) {
                    return static_cast<int>( index );
                }
            }
        }

        return -1;
    }

    class ScopedAdvisorAIControl
    {
    public:
        explicit ScopedAdvisorAIControl( Player & player )
            : _player( player )
            , _originalControl( player.GetControl() )
        {
            // Keep the human bit so normal adventure-map rendering/focus remains valid, while the AI bit makes
            // Heroes::Action dispatch to AI::HeroesAction for this one delegated order.
            _player.SetControl( _originalControl | CONTROL_AI );
        }

        ScopedAdvisorAIControl( const ScopedAdvisorAIControl & ) = delete;
        ScopedAdvisorAIControl & operator=( const ScopedAdvisorAIControl & ) = delete;

        ~ScopedAdvisorAIControl()
        {
            _player.SetControl( _originalControl );
        }

    private:
        Player & _player;
        const int _originalControl;
    };

    AdviceExecutionResult executeStrategicAdvice( Kingdom & kingdom, Player & player, const StrategicAdvice & advice )
    {
        Heroes * hero = world.GetHeroes( advice.heroId );
        if ( hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() ) {
            return { false, "Order failed: the assigned hero is no longer available." };
        }

        Interface::AdventureMap & adventureMap = Interface::AdventureMap::Get();
        adventureMap.SetFocus( hero, true );
        adventureMap.RedrawFocus();
        adventureMap.getGameArea().SetCenter( hero->GetCenter() );
        adventureMap.redraw( Interface::REDRAW_GAMEAREA | Interface::REDRAW_HEROES | Interface::REDRAW_STATUS | Interface::REDRAW_BUTTONS );
        fheroes2::Display::instance().render();

        if ( advice.kind == StrategicAdviceKind::RESERVE ) {
            hero->SetModes( Heroes::SLEEPER );
            adventureMap.RedrawFocus();
            return { true, "Order carried out: " + hero->GetName() + " is holding in reserve." };
        }

        if ( advice.targetIndex < 0 || advice.targetIndex >= world.w() * world.h() || hero->GetIndex() == advice.targetIndex ) {
            return { false, "Order failed: the target is no longer valid." };
        }

        AIWorldPathfinder pathfinder;
        pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
        pathfinder.setSpellPointsReserveRatio( 0.30 );
        pathfinder.reEvaluateIfNeeded( *hero );

        // false is important here: this is a committed order, so build the complete route to the selected target
        // rather than truncating it at an incidental object encountered on the way.
        std::list<Route::Step> route = pathfinder.buildPath( advice.targetIndex, false );
        if ( route.empty() ) {
            return { false, "Order failed: the route became unreachable." };
        }

        const int32_t startingIndex = hero->GetIndex();
        hero->GetPath().setPath( std::move( route ) );

        fheroes2::GameMode result = fheroes2::GameMode::CANCEL;
        {
            ScopedAdvisorAIControl delegatedControl( player );
            const fheroes2::AutoPlayPopupTimeoutScope timeoutScope( true );
            result = AI::HeroesMove( *hero );
        }

        if ( hero->isActive() ) {
            adventureMap.SetFocus( hero, true );
            adventureMap.RedrawFocus();
            adventureMap.getGameArea().SetCenter( hero->GetCenter() );
        }
        else {
            adventureMap.ResetFocus( GameFocus::HEROES, true );
            adventureMap.RedrawFocus();
        }

        adventureMap.redraw( Interface::REDRAW_GAMEAREA | Interface::REDRAW_RADAR | Interface::REDRAW_HEROES | Interface::REDRAW_STATUS | Interface::REDRAW_BUTTONS );
        fheroes2::Display::instance().render();

        const bool positionChanged = !hero->isActive() || hero->GetIndex() != startingIndex;
        const bool targetReached = hero->isActive() && hero->GetIndex() == advice.targetIndex;
        const bool pathConsumed = !hero->isActive() || hero->GetPath().empty();

        if ( !positionChanged && !targetReached && !pathConsumed ) {
            return { false, "Order failed: " + hero->GetName() + " could not begin the assigned route." };
        }

        std::string message = "Executed [" + std::string( adviceKindName( advice.kind ) ) + "]: " + advice.title;
        if ( !hero->isActive() ) {
            message += " | hero lost during execution";
        }
        else if ( targetReached || pathConsumed ) {
            message += " | objective resolved";
        }
        else {
            message += " | advanced as far as this turn allowed";
        }

        if ( result != fheroes2::GameMode::END_TURN && result != fheroes2::GameMode::CANCEL ) {
            message += " | scenario state changed";
        }

        return { true, std::move( message ) };
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
        std::string councilReport;

        for ( size_t refresh = 0; refresh < 64; ++refresh ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdvice( kingdom );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council", "No safe actionable order is available right now.", Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdvice( advices, councilReport );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const AdviceExecutionResult execution = executeStrategicAdvice( kingdom, *player, advices[selection] );
            councilReport = execution.message;

            if ( !execution.success ) {
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
            }

            // Re-score after every delegated order. The next window therefore reflects the changed hero positions,
            // armies, fog, captured objects, economy and remaining movement rather than repeating a stale list.
        }
    }
}
