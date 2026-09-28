// War Council v2 overlay.
// Keep the previous implementation intact as a legacy implementation, then replace only
// candidate generation, presentation and the persistent council loop.
#define generateStrategicAdvice generateStrategicAdviceLegacy
#define selectStrategicAdvice selectStrategicAdviceLegacy
#define showRpgStrategicAdvisor showRpgStrategicAdvisorLegacy
#include "game_rpg_advisor_legacy.inc"
#undef showRpgStrategicAdvisor
#undef selectStrategicAdvice
#undef generateStrategicAdvice

namespace
{
    struct CouncilGoal
    {
        int32_t index{ -1 };
        MP2::MapObjectType objectType{ MP2::OBJ_NONE };
        uint32_t distance{ 0 };
        double score{ 0.0 };
    };

    struct FrontierMove
    {
        int32_t index{ -1 };
        uint32_t distance{ 0 };
        uint32_t fogEdges{ 0 };
        double score{ 0.0 };
    };

    bool hasMovableCouncilHeroes( Kingdom & kingdom )
    {
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr && hero->isActive() && hero->GetMovePoints() > 0 && hero->MayStillMove( false, false ) ) {
                return true;
            }
        }
        return false;
    }

    size_t movableCouncilHeroCount( Kingdom & kingdom )
    {
        size_t count = 0;
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero != nullptr && hero->isActive() && hero->GetMovePoints() > 0 && hero->MayStillMove( false, false ) ) {
                ++count;
            }
        }
        return count;
    }

    std::string councilTargetName( const MP2::MapObjectType objectType, const int32_t index )
    {
        if ( objectType == MP2::OBJ_CASTLE ) {
            if ( const Castle * castle = world.getCastleEntrance( Maps::GetPoint( index ) ) ) {
                return castle->GetName();
            }
        }
        else if ( objectType == MP2::OBJ_HERO ) {
            if ( const Heroes * hero = world.getTile( index ).getHero() ) {
                return hero->GetName();
            }
        }
        return MP2::StringObject( objectType );
    }

    uint32_t adjacentFogCount( const int32_t index, const PlayerColor color )
    {
        uint32_t count = 0;
        for ( const int32_t nearby : Maps::getAroundIndexes( index ) ) {
            if ( world.getTile( nearby ).isFog( color ) ) {
                ++count;
            }
        }
        return count;
    }

    bool isRepeatableLowValueCouncilAdvice( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::TOWN_OPTIMIZE_HERO:
        case StrategicAdviceKind::TOWN_OPTIMIZE_GARRISON:
        case StrategicAdviceKind::HERO_OPTIMIZE_PAIR:
            return true;
        default:
            return false;
        }
    }

    double councilBenefitScore( const StrategicAdvice & advice, const double strongestHeroStrength )
    {
        double score = advice.score;
        switch ( advice.kind ) {
        case StrategicAdviceKind::TRAVEL_TO_TOWN: {
            const Heroes * hero = world.GetHeroes( advice.heroId );
            if ( hero == nullptr ) {
                return 0.0;
            }
            const double relativeStrength = hero->GetArmy().GetStrength() / std::max( 1.0, strongestHeroStrength );
            score *= relativeStrength < 0.65 ? 0.90 : 0.38;
            break;
        }
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
            score *= 0.86;
            break;
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
            score *= 0.82;
            break;
        default:
            break;
        }
        return score;
    }

    void appendGoalDirectedStagingAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const double strongestHeroStrength )
    {
        const PlayerColor color = kingdom.GetColor();
        const int32_t mapSize = world.w() * world.h();

        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || hero->GetMovePoints() == 0 || !hero->MayStillMove( false, false ) ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
            pathfinder.setSpellPointsReserveRatio( 0.30 );
            pathfinder.reEvaluateIfNeeded( *hero );

            const uint32_t movePoints = hero->GetMovePoints();
            const double heroStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
            std::vector<CouncilGoal> goals;

            for ( int32_t index = 0; index < mapSize; ++index ) {
                if ( index == hero->GetIndex() ) {
                    continue;
                }
                const Maps::Tile & tile = world.getTile( index );
                if ( tile.isFog( color ) || !isCandidateObjectUseful( *hero, kingdom, tile ) ) {
                    continue;
                }

                const double baseValue = baseStrategicValue( tile.getMainObjectType() );
                if ( baseValue <= 0.0 ) {
                    continue;
                }

                const uint32_t distance = pathfinder.getDistance( index );
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance <= movePoints ) {
                    continue;
                }

                const double defenders = defenderStrengthForAdvice( tile, *hero );
                if ( defenders > 0.0 && heroStrength / defenders < 1.30 ) {
                    continue;
                }

                const double turnsAway = static_cast<double>( distance ) / std::max<uint32_t>( 1, movePoints );
                double goalScore = baseValue;
                if ( isPermanentStrategicObject( tile.getMainObjectType() ) ) {
                    goalScore *= 1.18;
                }
                if ( defenders > 0.0 ) {
                    goalScore *= std::clamp( heroStrength / defenders, 1.0, 2.0 );
                }
                goalScore /= 1.0 + std::max( 0.0, turnsAway - 1.0 ) * 0.24;
                goals.push_back( { index, tile.getMainObjectType(), distance, goalScore } );
            }

            if ( heroStrength < strongestHeroStrength * 0.66 ) {
                for ( Castle * castle : kingdom.GetCastles() ) {
                    if ( castle == nullptr || castle->GetColor() != color || castle->GetIndex() == hero->GetIndex() ) {
                        continue;
                    }
                    const uint32_t distance = pathfinder.getDistance( castle->GetIndex() );
                    if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance <= movePoints ) {
                        continue;
                    }
                    const double reinforcement = castle->GetArmy().GetStrength();
                    goals.push_back( { castle->GetIndex(), MP2::OBJ_CASTLE, distance,
                                       5600.0 + std::min( 3400.0, reinforcement / std::max( 1.0, strongestHeroStrength ) * 2200.0 ) } );
                }
            }

            std::sort( goals.begin(), goals.end(), []( const CouncilGoal & left, const CouncilGoal & right ) {
                if ( left.score != right.score ) {
                    return left.score > right.score;
                }
                return left.distance < right.distance;
            } );
            if ( goals.size() > 5 ) {
                goals.resize( 5 );
            }

            std::set<int32_t> usedStagingTiles;
            for ( const CouncilGoal & goal : goals ) {
                const std::list<Route::Step> route = pathfinder.buildPath( goal.index, false );
                if ( route.empty() ) {
                    continue;
                }

                uint32_t spent = 0;
                int32_t stagingIndex = -1;
                for ( const Route::Step & step : route ) {
                    if ( spent + step.GetPenalty() > movePoints ) {
                        break;
                    }
                    spent += step.GetPenalty();
                    const Maps::Tile & stagingTile = world.getTile( step.GetIndex() );
                    if ( !stagingTile.isFog( color ) && stagingTile.getMainObjectType() == MP2::OBJ_NONE ) {
                        stagingIndex = step.GetIndex();
                    }
                }

                if ( stagingIndex < 0 || !usedStagingTiles.insert( stagingIndex ).second ) {
                    continue;
                }
                const uint32_t stageDistance = pathfinder.getDistance( stagingIndex );
                if ( stageDistance == 0 || stageDistance == std::numeric_limits<uint32_t>::max() || stageDistance > movePoints ) {
                    continue;
                }

                const Maps::Tile & stagingTile = world.getTile( stagingIndex );
                StrategicAdvice advice;
                advice.kind = stagingTile.isRoad() ? StrategicAdviceKind::ROAD_MARCH
                                                   : ( heroStrength >= strongestHeroStrength * 0.90 ? StrategicAdviceKind::TERRITORIAL_PRESSURE
                                                                                                   : StrategicAdviceKind::SAFE_ADVANCE );
                advice.action = AdviceAction::MOVE_HERO;
                advice.heroId = hero->GetID();
                advice.targetIndex = stagingIndex;
                advice.targetObjectType = MP2::OBJ_NONE;
                const double movementUse = static_cast<double>( stageDistance ) / std::max<uint32_t>( 1, movePoints );
                advice.score = goal.score * 0.62 + movementUse * 2600.0 + ( stagingTile.isRoad() ? 650.0 : 0.0 );
                advice.distance = stageDistance;
                advice.reachableThisTurn = true;
                const std::string targetName = councilTargetName( goal.objectType, goal.index );
                advice.title = hero->GetName() + ": Advance toward " + targetName;
                advice.rationale = "Spend " + std::to_string( static_cast<int>( movementUse * 100.0 ) )
                                   + "% of remaining movement closing on a high-value objective";
                candidates.emplace_back( std::move( advice ) );
            }
        }
    }

    void appendFrontierBenefitAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom )
    {
        const PlayerColor color = kingdom.GetColor();
        const int32_t mapSize = world.w() * world.h();

        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || hero->GetMovePoints() == 0 || !hero->MayStillMove( false, false ) ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
            pathfinder.reEvaluateIfNeeded( *hero );
            const uint32_t movePoints = hero->GetMovePoints();
            std::vector<FrontierMove> frontier;

            for ( int32_t index = 0; index < mapSize; ++index ) {
                if ( index == hero->GetIndex() ) {
                    continue;
                }
                const Maps::Tile & tile = world.getTile( index );
                if ( tile.isFog( color ) || tile.getMainObjectType() != MP2::OBJ_NONE ) {
                    continue;
                }
                const uint32_t fogEdges = adjacentFogCount( index, color );
                if ( fogEdges == 0 ) {
                    continue;
                }
                const uint32_t distance = pathfinder.getDistance( index );
                if ( distance == 0 || distance == std::numeric_limits<uint32_t>::max() || distance > movePoints ) {
                    continue;
                }
                const double movementUse = static_cast<double>( distance ) / std::max<uint32_t>( 1, movePoints );
                const double score = 4800.0 + fogEdges * 900.0 + movementUse * 1400.0 + ( tile.isRoad() ? 250.0 : 0.0 );
                frontier.push_back( { index, distance, fogEdges, score } );
            }

            std::sort( frontier.begin(), frontier.end(), []( const FrontierMove & left, const FrontierMove & right ) {
                if ( left.score != right.score ) {
                    return left.score > right.score;
                }
                return left.distance > right.distance;
            } );
            if ( frontier.size() > 3 ) {
                frontier.resize( 3 );
            }

            for ( const FrontierMove & move : frontier ) {
                StrategicAdvice advice;
                advice.kind = StrategicAdviceKind::FOG_SWEEP;
                advice.action = AdviceAction::MOVE_HERO;
                advice.heroId = hero->GetID();
                advice.targetIndex = move.index;
                advice.targetObjectType = MP2::OBJ_NONE;
                advice.score = move.score;
                advice.distance = move.distance;
                advice.reachableThisTurn = true;
                advice.title = hero->GetName() + ": Scout the frontier";
                advice.rationale = "Reveal " + std::to_string( move.fogEdges ) + " adjacent fog sectors while using "
                                   + std::to_string( static_cast<int>( static_cast<double>( move.distance ) / std::max<uint32_t>( 1, hero->GetMovePoints() ) * 100.0 ) )
                                   + "% of remaining movement";
                candidates.emplace_back( std::move( advice ) );
            }
        }
    }

    std::vector<StrategicAdvice> chooseCouncilAdvice( std::vector<StrategicAdvice> candidates )
    {
        std::sort( candidates.begin(), candidates.end(), []( const StrategicAdvice & left, const StrategicAdvice & right ) {
            if ( left.score != right.score ) {
                return left.score > right.score;
            }
            return left.distance < right.distance;
        } );

        std::vector<StrategicAdvice> chosen;
        chosen.reserve( strategicAdviceCount );
        std::set<std::string> usedIdentities;
        std::set<int32_t> usedMovementTargets;
        std::map<int32_t, size_t> perHero;
        std::map<int32_t, size_t> perTown;

        const auto tryChoose = [&]( const StrategicAdvice & advice, const bool distribute ) {
            if ( chosen.size() >= strategicAdviceCount || !advice.reachableThisTurn || advice.score < 2500.0 ) {
                return;
            }
            const std::string identity = adviceIdentity( advice );
            if ( !usedIdentities.insert( identity ).second ) {
                return;
            }
            const bool movement = advice.action == AdviceAction::MOVE_HERO;
            const bool townAction = advice.action == AdviceAction::RECRUIT_CREATURES || advice.action == AdviceAction::BUILD_STRUCTURE
                                    || advice.action == AdviceAction::TOWN_ARMY_MANAGEMENT;
            if ( movement && !usedMovementTargets.insert( advice.targetIndex ).second ) {
                usedIdentities.erase( identity );
                return;
            }
            if ( distribute ) {
                if ( movement && perHero[advice.heroId] >= 2 ) {
                    usedIdentities.erase( identity );
                    usedMovementTargets.erase( advice.targetIndex );
                    return;
                }
                if ( townAction && perTown[advice.targetIndex] >= 2 ) {
                    usedIdentities.erase( identity );
                    if ( movement ) {
                        usedMovementTargets.erase( advice.targetIndex );
                    }
                    return;
                }
            }
            chosen.emplace_back( advice );
            if ( movement ) {
                ++perHero[advice.heroId];
            }
            else if ( townAction ) {
                ++perTown[advice.targetIndex];
            }
        };

        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, true );
        }
        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, false );
        }
        return chosen;
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
        appendTownTravelAdvice( candidates, kingdom, strongestHeroStrength );
        appendTownArmyManagementAdvice( candidates, kingdom );
        appendHeroExchangeAdvice( candidates, kingdom );
        appendRecruitmentAdvice( candidates, kingdom );
        appendBuildingAdvice( candidates, kingdom );

        const int32_t mapSize = world.w() * world.h();
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || hero->GetMovePoints() == 0 || !hero->MayStillMove( false, false ) ) {
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
        appendGoalDirectedStagingAdvice( candidates, kingdom, strongestHeroStrength );

        std::vector<StrategicAdvice> filtered;
        filtered.reserve( candidates.size() );
        for ( StrategicAdvice & advice : candidates ) {
            if ( isRepeatableLowValueCouncilAdvice( advice.kind ) ) {
                continue;
            }
            advice.score = councilBenefitScore( advice, strongestHeroStrength );
            if ( advice.score >= 2500.0 && std::isfinite( advice.score ) ) {
                filtered.emplace_back( std::move( advice ) );
            }
        }

        std::vector<StrategicAdvice> chosen = chooseCouncilAdvice( filtered );
        if ( chosen.size() < strategicAdviceCount && hasMovableCouncilHeroes( kingdom ) ) {
            appendFrontierBenefitAdvice( filtered, kingdom );
            chosen = chooseCouncilAdvice( std::move( filtered ) );
        }
        return chosen;
    }

    const char * councilPriorityLabel( const double score )
    {
        if ( score >= 14000.0 ) return "DECISIVE";
        if ( score >= 9500.0 ) return "STRONG";
        if ( score >= 6000.0 ) return "GOOD";
        return "USEFUL";
    }

    int selectStrategicAdvice( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
    {
        if ( advices.empty() ) return -1;
        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();
        fheroes2::StandardWindow window( 584, 398, true, display );
        const fheroes2::Rect area = window.activeArea();
        const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();
        std::array<fheroes2::Rect, strategicAdviceCount> rows{};
        fheroes2::Button closeButton;
        window.renderButton( closeButton, isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD, 0, 1, { 14, 8 },
                             fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );
        const size_t visibleCount = std::min( strategicAdviceCount, advices.size() );
        size_t hoverIndex = strategicAdviceCount;
        bool redraw = true;
        LocalEvent & event = LocalEvent::Get();
        while ( event.HandleEvents() ) {
            size_t newHoverIndex = strategicAdviceCount;
            for ( size_t index = 0; index < visibleCount; ++index ) {
                if ( event.isMouseCursorPosInArea( rows[index] ) ) { newHoverIndex = index; break; }
            }
            if ( newHoverIndex != hoverIndex ) { hoverIndex = newHoverIndex; redraw = true; }
            if ( redraw ) {
                window.render();
                window.applyGemDecoratedCorners();
                const fheroes2::Rect banner{ area.x + 8, area.y + 3, area.width - 16, 48 };
                window.applyTextBackgroundShading( banner );
                const fheroes2::Text title( "ROYAL WAR COUNCIL", fheroes2::FontType::normalYellow() );
                title.draw( banner.x + 8, banner.y + 4, banner.width - 16, display );
                const std::string movementLine = std::to_string( movableCouncilHeroCount( kingdom ) ) + " heroes still have movement - the council recommends only worthwhile orders.";
                fheroes2::Text subtitle( movementLine, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 16 );
                subtitle.draw( banner.x + 8, banner.y + 27, display );
                if ( !councilReport.empty() ) {
                    fheroes2::Text report( councilReport, fheroes2::FontType::smallYellow() );
                    report.fitToOneRow( area.width - 24 );
                    report.draw( area.x + 12, area.y + 54, display );
                }
                for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
                    rows[index] = { area.x + 12, area.y + 70 + static_cast<int32_t>( index ) * 53, area.width - 24, 47 };
                    window.applyTextBackgroundShading( rows[index] );
                    if ( index >= visibleCount ) {
                        const fheroes2::Text noFiller( "The council found no additional worthwhile order - no filler advice.", fheroes2::FontType::smallWhite() );
                        noFiller.draw( rows[index].x + 10, rows[index].y + 16, rows[index].width - 20, display );
                        continue;
                    }
                    const StrategicAdvice & advice = advices[index];
                    int32_t textX = rows[index].x + 9;
                    int32_t textWidth = rows[index].width - 18;
                    if ( const Heroes * hero = world.GetHeroes( advice.heroId ) ) {
                        hero->PortraitRedraw( rows[index].x + 6, rows[index].y + 5, PORT_SMALL, display );
                        textX = rows[index].x + 52;
                        textWidth = rows[index].width - 61;
                    }
                    std::string headingText = ( hoverIndex == index ? "> " : "" );
                    headingText += std::to_string( index + 1 ) + ". " + councilPriorityLabel( advice.score ) + "  [" + adviceKindName( advice.kind ) + "]  " + advice.title;
                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 5, display );
                    fheroes2::Text reason( "Advisor: " + advice.rationale, fheroes2::FontType::smallWhite() );
                    reason.fitToOneRow( textWidth );
                    reason.draw( textX, rows[index].y + 24, display );
                }
                const fheroes2::Text hint( "Choose 1-5 or click an order. Esc returns control; the council never ends your turn.", fheroes2::FontType::smallWhite() );
                hint.fitToOneRow( area.width - 132 );
                hint.draw( area.x + 12, area.y + area.height - 21, display );
                closeButton.draw();
                display.render( window.totalArea() );
                redraw = false;
            }
            closeButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( closeButton.area() ) );
            if ( Game::HotKeyCloseWindow() || event.MouseClickLeft( closeButton.area() ) ) return -1;
            for ( size_t index = 0; index < visibleCount; ++index ) {
                if ( event.MouseClickLeft( rows[index] ) ) return static_cast<int>( index );
                const fheroes2::Key numberKey = static_cast<fheroes2::Key>( static_cast<int32_t>( fheroes2::Key::KEY_1 ) + static_cast<int32_t>( index ) );
                if ( event.isKeyPressed( numberKey ) ) return static_cast<int>( index );
            }
        }
        return -1;
    }

    void showRpgStrategicAdvisor()
    {
        Settings & settings = Settings::Get();
        Player * player = settings.GetPlayers().GetCurrent();
        if ( player == nullptr || !player->isControlHuman() || Battle::GetArena() != nullptr ) return;
        const PlayerColor color = player->GetColor();
        if ( color == PlayerColor::NONE ) return;
        Kingdom & kingdom = world.GetKingdom( color );
        std::string councilReport;
        std::set<std::string> failedOrders;
        while ( hasMovableCouncilHeroes( kingdom ) ) {
            std::vector<StrategicAdvice> advices = generateStrategicAdvice( kingdom );
            if ( !failedOrders.empty() ) {
                advices.erase( std::remove_if( advices.begin(), advices.end(), [&]( const StrategicAdvice & advice ) { return failedOrders.count( adviceIdentity( advice ) ) > 0; } ), advices.end() );
            }
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council", "No beneficial executable order remains in the current position. Your turn is still active and the council has not ended it.", Dialog::OK );
                return;
            }
            const int selection = selectStrategicAdvice( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) return;
            const StrategicAdvice selected = advices[selection];
            const AdviceExecutionResult execution = executeStrategicAdvice( kingdom, *player, selected );
            councilReport = execution.message;
            if ( !execution.success ) {
                failedOrders.insert( adviceIdentity( selected ) );
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
            }
            else {
                failedOrders.clear();
            }
        }
        fheroes2::showStandardTextMessage( "War Council", "All active heroes have spent their available movement. The council yields control; your turn remains yours.", Dialog::OK );
    }
}
