// RPG Strategic Advisor implementation.
// This file is included only by game_hotkeys.cpp so the advisor remains isolated from the base game.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <list>
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
        // Original broad council families.
        OFFENSE,
        DEFENSE,
        ECONOMY,
        EXPLORATION,
        DEVELOPMENT,
        LOGISTICS,
        OPPORTUNITY,

        // Twenty additional executable order types.
        SIEGE,
        INTERCEPTION,
        MONSTER_HUNT,
        MINE_CAPTURE,
        RESOURCE_RUN,
        ARTIFACT_RECOVERY,
        TREASURE_HUNT,
        HERO_TRAINING,
        SPELL_PILGRIMAGE,
        CASTLE_RELIEF,
        FRONTIER_BREAKTHROUGH,
        FOG_SWEEP,
        REGROUP,
        ROAD_MARCH,
        FLANKING_MARCH,
        SAFE_ADVANCE,
        ECONOMIC_EXPANSION,
        SUPPLY_SWEEP,
        MAGIC_DEVELOPMENT,
        TERRITORIAL_PRESSURE
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
            return "DEVELOP";
        case StrategicAdviceKind::LOGISTICS:
            return "LOGISTICS";
        case StrategicAdviceKind::OPPORTUNITY:
            return "OPPORTUNITY";
        case StrategicAdviceKind::SIEGE:
            return "SIEGE";
        case StrategicAdviceKind::INTERCEPTION:
            return "INTERCEPT";
        case StrategicAdviceKind::MONSTER_HUNT:
            return "CLEARANCE";
        case StrategicAdviceKind::MINE_CAPTURE:
            return "MINE";
        case StrategicAdviceKind::RESOURCE_RUN:
            return "RESOURCES";
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
            return "ARTIFACT";
        case StrategicAdviceKind::TREASURE_HUNT:
            return "TREASURE";
        case StrategicAdviceKind::HERO_TRAINING:
            return "TRAINING";
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
            return "SPELLS";
        case StrategicAdviceKind::CASTLE_RELIEF:
            return "RELIEF";
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
            return "FRONTIER";
        case StrategicAdviceKind::FOG_SWEEP:
            return "FOG SWEEP";
        case StrategicAdviceKind::REGROUP:
            return "REGROUP";
        case StrategicAdviceKind::ROAD_MARCH:
            return "ROAD MARCH";
        case StrategicAdviceKind::FLANKING_MARCH:
            return "FLANK";
        case StrategicAdviceKind::SAFE_ADVANCE:
            return "ADVANCE";
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
            return "EXPANSION";
        case StrategicAdviceKind::SUPPLY_SWEEP:
            return "SUPPLY";
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
            return "MAGIC";
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
            return "PRESSURE";
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

    bool isVisitFilteredObject( const MP2::MapObjectType objectType )
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
        case MP2::OBJ_GAZEBO:
        case MP2::OBJ_TREE_OF_KNOWLEDGE:
        case MP2::OBJ_ARENA:
        case MP2::OBJ_XANADU:
        case MP2::OBJ_MAGIC_WELL:
        case MP2::OBJ_ARTESIAN_SPRING:
        case MP2::OBJ_OASIS:
        case MP2::OBJ_WATERING_HOLE:
        case MP2::OBJ_STABLES:
        case MP2::OBJ_OBSERVATION_TOWER:
        case MP2::OBJ_HUT_OF_MAGI:
        case MP2::OBJ_EYE_OF_MAGI:
        case MP2::OBJ_MAGELLANS_MAPS:
        case MP2::OBJ_OBELISK:
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_LEAN_TO:
            return true;
        default:
            return false;
        }
    }

    StrategicAdviceKind classifyStrategicObject( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
            return StrategicAdviceKind::SIEGE;
        case MP2::OBJ_HERO:
            return StrategicAdviceKind::INTERCEPTION;
        case MP2::OBJ_MONSTER:
            return StrategicAdviceKind::MONSTER_HUNT;
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_ABANDONED_MINE:
            return StrategicAdviceKind::MINE_CAPTURE;
        case MP2::OBJ_LIGHTHOUSE:
            return StrategicAdviceKind::ECONOMIC_EXPANSION;
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
        case MP2::OBJ_FLOTSAM:
            return StrategicAdviceKind::RESOURCE_RUN;
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_LEAN_TO:
            return StrategicAdviceKind::SUPPLY_SWEEP;
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
        case MP2::OBJ_SKELETON:
            return StrategicAdviceKind::ARTIFACT_RECOVERY;
        case MP2::OBJ_TREASURE_CHEST:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_WAGON:
            return StrategicAdviceKind::TREASURE_HUNT;
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_GAZEBO:
        case MP2::OBJ_TREE_OF_KNOWLEDGE:
        case MP2::OBJ_ARENA:
        case MP2::OBJ_XANADU:
            return StrategicAdviceKind::HERO_TRAINING;
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
            return StrategicAdviceKind::SPELL_PILGRIMAGE;
        case MP2::OBJ_MAGIC_WELL:
        case MP2::OBJ_ARTESIAN_SPRING:
            return StrategicAdviceKind::MAGIC_DEVELOPMENT;
        case MP2::OBJ_OASIS:
        case MP2::OBJ_WATERING_HOLE:
        case MP2::OBJ_STABLES:
            return StrategicAdviceKind::LOGISTICS;
        case MP2::OBJ_OBSERVATION_TOWER:
        case MP2::OBJ_HUT_OF_MAGI:
        case MP2::OBJ_EYE_OF_MAGI:
        case MP2::OBJ_MAGELLANS_MAPS:
        case MP2::OBJ_OBELISK:
            return StrategicAdviceKind::FOG_SWEEP;
        default:
            return StrategicAdviceKind::OPPORTUNITY;
        }
    }

    double baseStrategicValue( const MP2::MapObjectType objectType )
    {
        switch ( objectType ) {
        case MP2::OBJ_CASTLE:
            return 16500.0;
        case MP2::OBJ_HERO:
            return 14800.0;
        case MP2::OBJ_MINE:
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_SAWMILL:
        case MP2::OBJ_ABANDONED_MINE:
            return 9900.0;
        case MP2::OBJ_LIGHTHOUSE:
            return 9400.0;
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
            return 7800.0;
        case MP2::OBJ_TREE_OF_KNOWLEDGE:
        case MP2::OBJ_XANADU:
            return 7600.0;
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_ARENA:
            return 7200.0;
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_TEMPLE:
            return 6600.0;
        case MP2::OBJ_MONSTER:
            return 6300.0;
        case MP2::OBJ_MAGELLANS_MAPS:
            return 6200.0;
        case MP2::OBJ_MAGIC_WELL:
        case MP2::OBJ_ARTESIAN_SPRING:
            return 5400.0;
        case MP2::OBJ_OBSERVATION_TOWER:
        case MP2::OBJ_HUT_OF_MAGI:
        case MP2::OBJ_EYE_OF_MAGI:
        case MP2::OBJ_OBELISK:
            return 5100.0;
        case MP2::OBJ_GAZEBO:
            return 5000.0;
        case MP2::OBJ_STABLES:
        case MP2::OBJ_OASIS:
        case MP2::OBJ_WATERING_HOLE:
            return 4700.0;
        case MP2::OBJ_TREASURE_CHEST:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_WAGON:
            return 4400.0;
        case MP2::OBJ_SKELETON:
            return 4100.0;
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
        case MP2::OBJ_FLOTSAM:
            return 3600.0;
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_LEAN_TO:
            return 3300.0;
        default:
            return 0.0;
        }
    }

    std::string strategicObjectName( const MP2::MapObjectType objectType )
    {
        return MP2::StringObject( objectType );
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
        case MP2::OBJ_SKELETON:
            return "Recover";
        case MP2::OBJ_TREASURE_CHEST:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_WAGON:
            return "Secure";
        case MP2::OBJ_FORT:
        case MP2::OBJ_MERCENARY_CAMP:
        case MP2::OBJ_WITCH_DOCTORS_HUT:
        case MP2::OBJ_STANDING_STONES:
        case MP2::OBJ_GAZEBO:
        case MP2::OBJ_TREE_OF_KNOWLEDGE:
        case MP2::OBJ_ARENA:
        case MP2::OBJ_XANADU:
            return "Train at";
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
            return "Study at";
        case MP2::OBJ_MAGIC_WELL:
        case MP2::OBJ_ARTESIAN_SPRING:
            return "Restore at";
        case MP2::OBJ_OBSERVATION_TOWER:
        case MP2::OBJ_HUT_OF_MAGI:
        case MP2::OBJ_EYE_OF_MAGI:
        case MP2::OBJ_MAGELLANS_MAPS:
        case MP2::OBJ_OBELISK:
            return "Scout from";
        default:
            return "Visit";
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

        if ( ( objectType == MP2::OBJ_ARTIFACT || objectType == MP2::OBJ_SHIPWRECK_SURVIVOR ) && hero.IsFullBagArtifacts() ) {
            return false;
        }

        if ( isVisitFilteredObject( objectType ) && ( hero.isVisited( tile ) || kingdom.isVisited( tile ) ) ) {
            return false;
        }

        return true;
    }

    bool isFrontierTile( const int32_t tileIndex, const PlayerColor color )
    {
        for ( const int32_t nearbyIndex : Maps::getAroundIndexes( tileIndex ) ) {
            if ( world.getTile( nearbyIndex ).isFog( color ) ) {
                return true;
            }
        }
        return false;
    }

    StrategicAdvice buildObjectiveAdvice( Heroes & hero, const Kingdom & kingdom, AIWorldPathfinder & pathfinder, const Maps::Tile & tile,
                                          const double strongestHeroStrength )
    {
        StrategicAdvice advice;
        const MP2::MapObjectType objectType = tile.getMainObjectType();
        const uint32_t distance = pathfinder.getDistance( tile.GetIndex() );
        const uint32_t currentMove = hero.GetMovePoints();

        // Every card shown by the council must be executable now, not merely a long-range aspiration.
        if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || currentMove == 0 || distance > currentMove ) {
            return advice;
        }

        const double heroStrength = std::max( 1.0, hero.GetArmy().GetStrength() );
        const double defenderStrength = defenderStrengthForAdvice( tile, hero );
        const bool combat = defenderStrength > 0.0 || objectType == MP2::OBJ_HERO || objectType == MP2::OBJ_CASTLE || objectType == MP2::OBJ_MONSTER;
        const double matchup = defenderStrength > 0.0 ? heroStrength / defenderStrength : 0.0;

        if ( defenderStrength > 0.0 && matchup < 1.25 ) {
            return advice;
        }

        const StrategicAdviceKind kind = classifyStrategicObject( objectType );
        double score = baseStrategicValue( objectType );
        const bool permanent = isPermanentStrategicObject( objectType );

        if ( permanent ) {
            score *= 1.28;
        }
        if ( defenderStrength > 0.0 ) {
            score *= std::clamp( 0.82 + matchup * 0.17, 1.0, 1.70 );
        }

        const double movementFraction = static_cast<double>( distance ) / std::max<uint32_t>( 1, currentMove );
        score *= 1.30 - std::min( 0.35, movementFraction * 0.30 );

        const bool strongestHero = heroStrength >= strongestHeroStrength * 0.90;
        if ( combat ) {
            score *= strongestHero ? 1.25 : 0.88;
        }
        else if ( strongestHero ) {
            score *= permanent ? 1.0 : 0.76;
        }
        else {
            score *= 1.12;
        }

        if ( kind == StrategicAdviceKind::SPELL_PILGRIMAGE || kind == StrategicAdviceKind::MAGIC_DEVELOPMENT ) {
            const double spellInvestment = fheroes2::RPG::spellcastingInvestmentPercent( kingdom.GetColor() );
            score *= 1.0 + std::min( 0.42, spellInvestment / 220.0 );
        }

        const Funds & funds = kingdom.GetFunds();
        if ( kind == StrategicAdviceKind::MINE_CAPTURE || kind == StrategicAdviceKind::RESOURCE_RUN || kind == StrategicAdviceKind::SUPPLY_SWEEP
             || kind == StrategicAdviceKind::ECONOMIC_EXPANSION ) {
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
        advice.reachableThisTurn = true;

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
        case StrategicAdviceKind::SIEGE:
            advice.rationale = "Take a permanent stronghold while the route and matchup are favorable";
            break;
        case StrategicAdviceKind::INTERCEPTION:
            advice.rationale = "Remove a mobile enemy threat before it can choose the engagement";
            break;
        case StrategicAdviceKind::MONSTER_HUNT:
            advice.rationale = "Safe army advantage converts movement into experience and map access";
            break;
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
            advice.rationale = "Permanent income and territorial control available this turn";
            break;
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::SUPPLY_SWEEP:
            advice.rationale = "Low-commitment resource gain within the remaining movement budget";
            break;
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
            advice.rationale = "Convert spare movement into durable hero power";
            break;
        case StrategicAdviceKind::TREASURE_HUNT:
            advice.rationale = "Immediate treasure without sacrificing the turn to travel";
            break;
        case StrategicAdviceKind::HERO_TRAINING:
            advice.rationale = "Permanent hero development is reachable before the turn ends";
            break;
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
            advice.rationale = "Improve magical readiness while the detour still fits this turn";
            break;
        case StrategicAdviceKind::FOG_SWEEP:
            advice.rationale = "Turn movement into map intelligence and future route quality";
            break;
        case StrategicAdviceKind::LOGISTICS:
            advice.rationale = "Gain movement or operational efficiency before committing elsewhere";
            break;
        default:
            advice.rationale = "High-value objective that can be completed with movement remaining now";
            break;
        }

        if ( defenderStrength > 0.0 ) {
            advice.rationale += " | " + formatAdvisorRatio( matchup ) + " army edge";
        }
        advice.rationale += " | executable this turn";

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
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance > hero->GetMovePoints() ) {
                    continue;
                }

                const double heroStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
                const double threatRatio = enemyStrength / garrisonStrength;
                double score = 18800.0 + threatRatio * 4200.0 + heroStrength / strongestHeroStrength * 1200.0;
                score *= 1.25;

                StrategicAdvice advice;
                advice.kind = threatRatio >= 1.0 ? StrategicAdviceKind::CASTLE_RELIEF : StrategicAdviceKind::DEFENSE;
                advice.heroId = hero->GetID();
                advice.targetIndex = castle->GetIndex();
                advice.targetObjectType = MP2::OBJ_CASTLE;
                advice.score = score;
                advice.distance = distance;
                advice.reachableThisTurn = true;
                advice.title = hero->GetName() + ": Relieve " + castle->GetName();
                advice.rationale = "Enemy " + closestThreat->GetName() + " is " + std::to_string( closestTiles ) + " tiles away";
                advice.rationale += " | threat " + formatAdvisorRatio( threatRatio ) + " garrison | executable this turn";
                candidates.emplace_back( std::move( advice ) );
            }
        }
    }

    void appendExplorationAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) || hero->GetMovePoints() == 0 ) {
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
            if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance > hero->GetMovePoints() ) {
                continue;
            }

            const double strength = std::max( 1.0, hero->GetArmy().GetStrength() );
            double score = frontier ? 7600.0 : 5900.0;
            score *= strength < strongestHeroStrength * 0.75 ? 1.30 : 0.80;

            StrategicAdvice advice;
            advice.kind = frontier ? StrategicAdviceKind::FRONTIER_BREAKTHROUGH : StrategicAdviceKind::FOG_SWEEP;
            advice.heroId = hero->GetID();
            advice.targetIndex = targetIndex;
            advice.score = score;
            advice.distance = distance;
            advice.reachableThisTurn = true;
            advice.title = hero->GetName() + std::string( frontier ? ": Break the frontier" : ": Sweep the fog" );
            advice.rationale = frontier ? "Reveal a new region before this turn ends" : "Convert spare movement into immediate map intelligence";
            advice.rationale += " | executable this turn";
            candidates.emplace_back( std::move( advice ) );
        }
    }

    void appendLogisticsAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) || hero->GetMovePoints() == 0 ) {
                continue;
            }

            const double heroStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
            if ( heroStrength > strongestHeroStrength * 0.78 ) {
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
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance > hero->GetMovePoints() ) {
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

            const double vulnerability = strongestHeroStrength / heroStrength;
            double score = 6500.0 + std::min( 4.0, vulnerability ) * 900.0;
            score += std::min( 2500.0, bestCastleStrength / std::max( 1.0, strongestHeroStrength ) * 1600.0 );

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::REGROUP;
            advice.heroId = hero->GetID();
            advice.targetIndex = bestCastle->GetIndex();
            advice.targetObjectType = MP2::OBJ_CASTLE;
            advice.score = score;
            advice.distance = bestDistance;
            advice.reachableThisTurn = true;
            advice.title = hero->GetName() + ": Regroup at " + bestCastle->GetName();
            advice.rationale = "Vulnerable field army can reach friendly cover before the turn ends";
            candidates.emplace_back( std::move( advice ) );
        }
    }

    void appendManeuverAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        const PlayerColor color = kingdom.GetColor();
        const int mapSize = world.w() * world.h();

        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) || hero->GetMovePoints() == 0 ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
            pathfinder.setSpellPointsReserveRatio( 0.30 );
            pathfinder.reEvaluateIfNeeded( *hero );

            const double heroStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
            const uint32_t movePoints = hero->GetMovePoints();

            for ( int32_t index = 0; index < mapSize; ++index ) {
                if ( index == hero->GetIndex() ) {
                    continue;
                }

                const Maps::Tile & tile = world.getTile( index );
                if ( tile.isFog( color ) || tile.getMainObjectType() != MP2::OBJ_NONE ) {
                    continue;
                }

                const uint32_t distance = pathfinder.getDistance( index );
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance > movePoints ) {
                    continue;
                }

                const bool frontier = isFrontierTile( index, color );
                StrategicAdviceKind kind = StrategicAdviceKind::SAFE_ADVANCE;
                std::string maneuver = "Take a safe forward position";
                std::string rationale = "Real reachable ground that improves next-turn options";

                if ( frontier ) {
                    kind = StrategicAdviceKind::FRONTIER_BREAKTHROUGH;
                    maneuver = "Probe the frontier";
                    rationale = "End this turn on the edge of unrevealed territory";
                }
                else if ( tile.isRoad() ) {
                    kind = StrategicAdviceKind::ROAD_MARCH;
                    maneuver = "March along the road";
                    rationale = "Use efficient terrain to bank positional value this turn";
                }
                else if ( heroStrength >= strongestHeroStrength * 0.90 ) {
                    kind = StrategicAdviceKind::TERRITORIAL_PRESSURE;
                    maneuver = "Take an aggressive forward position";
                    rationale = "Put the kingdom's strongest mobile army deeper into contested space";
                }
                else if ( ( index + hero->GetID() ) % 2 == 0 ) {
                    kind = StrategicAdviceKind::FLANKING_MARCH;
                    maneuver = "Shift onto a flanking line";
                    rationale = "Reposition without committing to a risky object or fight";
                }

                const double moveUse = static_cast<double>( distance ) / std::max<uint32_t>( 1, movePoints );
                double score = 2200.0 + moveUse * 2200.0;
                if ( frontier ) {
                    score += 1600.0;
                }
                if ( tile.isRoad() ) {
                    score += 700.0;
                }
                if ( kind == StrategicAdviceKind::TERRITORIAL_PRESSURE ) {
                    score += 900.0;
                }

                StrategicAdvice advice;
                advice.kind = kind;
                advice.heroId = hero->GetID();
                advice.targetIndex = index;
                advice.targetObjectType = MP2::OBJ_NONE;
                advice.score = score;
                advice.distance = distance;
                advice.reachableThisTurn = true;
                advice.title = hero->GetName() + ": " + maneuver;
                advice.rationale = rationale + " | executable this turn";
                candidates.emplace_back( std::move( advice ) );
            }
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
            if ( hero == nullptr || !hero->isActive() || !hero->MayStillMove( false, false ) || hero->GetMovePoints() == 0 ) {
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
                if ( advice.heroId >= 0 && advice.targetIndex >= 0 && advice.reachableThisTurn && advice.score > 0.0 && std::isfinite( advice.score ) ) {
                    candidates.emplace_back( std::move( advice ) );
                }
            }
        }

        appendExplorationAdvice( candidates, kingdom, strongestHeroStrength );
        appendManeuverAdvice( candidates, kingdom, strongestHeroStrength );

        std::sort( candidates.begin(), candidates.end(), []( const StrategicAdvice & left, const StrategicAdvice & right ) {
            if ( left.score != right.score ) {
                return left.score > right.score;
            }
            return left.distance < right.distance;
        } );

        std::vector<StrategicAdvice> chosen;
        chosen.reserve( strategicAdviceCount );
        std::set<int32_t> usedTargets;
        std::set<StrategicAdviceKind> usedKinds;
        std::map<int32_t, size_t> perHero;

        // Pass one: maximum strategic variety, two cards per hero at most.
        for ( const StrategicAdvice & advice : candidates ) {
            if ( chosen.size() >= strategicAdviceCount ) {
                break;
            }
            if ( !advice.reachableThisTurn || usedKinds.count( advice.kind ) > 0 || usedTargets.count( advice.targetIndex ) > 0 || perHero[advice.heroId] >= 2 ) {
                continue;
            }

            chosen.emplace_back( advice );
            usedKinds.insert( advice.kind );
            usedTargets.insert( advice.targetIndex );
            ++perHero[advice.heroId];
        }

        // Pass two: keep unique destinations, but allow one hero to provide all five choices if it is the only active hero.
        for ( const StrategicAdvice & advice : candidates ) {
            if ( chosen.size() >= strategicAdviceCount ) {
                break;
            }
            if ( !advice.reachableThisTurn || usedTargets.count( advice.targetIndex ) > 0 ) {
                continue;
            }

            chosen.emplace_back( advice );
            usedTargets.insert( advice.targetIndex );
        }

        // Extreme geometry can expose fewer than five unique reachable destinations. In that case, repeat a real
        // executable route rather than inventing a Hold/Reserve card. The menu still offers five orders and every
        // one can actually be issued this turn; after any choice the whole set is regenerated from the new position.
        size_t repeatIndex = 0;
        while ( chosen.size() < strategicAdviceCount && !candidates.empty() ) {
            StrategicAdvice repeated = candidates[repeatIndex % candidates.size()];
            ++repeatIndex;
            if ( !repeated.reachableThisTurn ) {
                continue;
            }

            if ( repeated.targetObjectType == MP2::OBJ_NONE ) {
                static constexpr std::array<StrategicAdviceKind, 4> maneuverKinds{ StrategicAdviceKind::SAFE_ADVANCE, StrategicAdviceKind::FLANKING_MARCH,
                                                                                  StrategicAdviceKind::ROAD_MARCH,
                                                                                  StrategicAdviceKind::TERRITORIAL_PRESSURE };
                repeated.kind = maneuverKinds[chosen.size() % maneuverKinds.size()];
                repeated.title = world.GetHeroes( repeated.heroId )->GetName() + std::string( ": " ) + adviceKindName( repeated.kind );
                repeated.rationale = "Alternative executable maneuver on the same proven-safe route | this turn";
            }
            chosen.emplace_back( std::move( repeated ) );
        }

        return chosen;
    }

    int selectStrategicAdvice( const std::vector<StrategicAdvice> & advices, const std::string & councilReport )
    {
        if ( advices.size() < strategicAdviceCount ) {
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
            for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
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

                const fheroes2::Text subtitle( "Five executable orders for this turn - choose one for the AI to carry out.", fheroes2::FontType::smallWhite() );
                subtitle.draw( area.x + 10, area.y + 23, area.width - 20, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( councilReport, fheroes2::FontType::smallYellow() );
                    report.fitToOneRow( area.width - 24 );
                    report.draw( area.x + 12, area.y + 39, display );
                }

                for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
                    rows[index] = { area.x + 12, area.y + 56 + static_cast<int32_t>( index ) * 55, area.width - 24, 49 };
                    window.applyTextBackgroundShading( rows[index] );

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

            for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
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

        if ( advice.targetIndex < 0 || advice.targetIndex >= world.w() * world.h() || hero->GetIndex() == advice.targetIndex ) {
            return { false, "Order failed: the target is no longer valid." };
        }

        AIWorldPathfinder pathfinder;
        pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
        pathfinder.setSpellPointsReserveRatio( 0.30 );
        pathfinder.reEvaluateIfNeeded( *hero );

        const uint32_t liveDistance = pathfinder.getDistance( advice.targetIndex );
        if ( liveDistance == 0 || liveDistance == std::numeric_limits<uint32_t>::max() || liveDistance > hero->GetMovePoints() ) {
            return { false, "Order expired: it can no longer be completed with this turn's remaining movement." };
        }

        std::list<Route::Step> route = pathfinder.buildPath( advice.targetIndex, false );
        if ( route.empty() ) {
            return { false, "Order failed: the route became unreachable." };
        }

        Interface::AdventureMap & adventureMap = Interface::AdventureMap::Get();
        adventureMap.SetFocus( hero, true );
        adventureMap.RedrawFocus();
        adventureMap.getGameArea().SetCenter( hero->GetCenter() );
        adventureMap.redraw( Interface::REDRAW_GAMEAREA | Interface::REDRAW_HEROES | Interface::REDRAW_STATUS | Interface::REDRAW_BUTTONS );
        fheroes2::Display::instance().render();

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
            message += " | objective resolved this turn";
        }
        else {
            message += " | order executed as far as the action allowed";
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
            if ( advices.size() < strategicAdviceCount ) {
                fheroes2::showStandardTextMessage( "War Council", "No hero has enough remaining movement for five executable orders this turn.", Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdvice( advices, councilReport );
            if ( selection < 0 || static_cast<size_t>( selection ) >= strategicAdviceCount ) {
                return;
            }

            const AdviceExecutionResult execution = executeStrategicAdvice( kingdom, *player, advices[selection] );
            councilReport = execution.message;

            if ( !execution.success ) {
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
            }
        }
    }
}
