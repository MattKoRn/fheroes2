// War Council v12: normal human action feedback before Advice returns.
// v11 owns ranking, compact presentation and decision signals. This layer changes only
// execution orchestration: movement stays advisor-directed, but map interactions run through
// the normal human Heroes::Action path, all standard dialogs finish before War Council reopens,
// and successful direct Advice actions receive a normal blocking completion popup as well.
#include "game_rpg_advisor_v11.inc"
#undef showRpgStrategicAdvisor

namespace
{
    AdviceExecutionResult executeMovementAdviceV12( Kingdom & kingdom, Player & player, const StrategicAdvice & advice )
    {
        Heroes * hero = world.GetHeroes( advice.heroId );
        if ( hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() ) {
            return { false, "Order failed: the assigned hero is no longer available." };
        }
        if ( !player.isControlHuman() ) {
            return { false, "Order failed: normal player control is required for this Advice action." };
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

        // Important: do not temporarily mark the human player as AI-controlled here.
        // Heroes::Move() calls Heroes::Action() at the destination. With normal human control
        // preserved, Heroes::Action() uses the game's regular object handlers and therefore
        // shows the real blocking dialogs, battles, choices and reward popups before returning.
        // Also do not enable AutoPlayPopupTimeoutScope: Advice must wait for those dialogs.
        const fheroes2::GameMode result = AI::HeroesMove( *hero );

        bool interactionResolved = advice.targetObjectType == MP2::OBJ_NONE;
        if ( hero->isActive() && advice.targetObjectType != MP2::OBJ_NONE ) {
            interactionResolved = isMovementInteractionResolved( kingdom, *hero, advice );

            // v11 retried unresolved interactions through AI::HeroesAction(). That is wrong once
            // normal human dialogs are restored: a deliberate No/Cancel must remain the player's
            // decision instead of immediately forcing the same object interaction a second time.
            if ( interactionResolved ) {
                completeTownArrivalLogistics( kingdom, *hero, advice );
            }
        }
        else if ( !hero->isActive() ) {
            interactionResolved = true;
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
        redrawAfterAdviceAction();

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
        else if ( advice.kind == StrategicAdviceKind::TOWN_RESUPPLY || advice.kind == StrategicAdviceKind::GARRISON_REINFORCEMENT ) {
            message += interactionResolved ? " | town army logistics completed" : " | town interaction left to your choice";
        }
        else if ( advice.targetObjectType != MP2::OBJ_NONE ) {
            message += interactionResolved ? " | target interaction completed" : " | target interaction left to your choice";
        }
        else if ( targetReached || pathConsumed ) {
            message += " | maneuver completed this turn";
        }
        else {
            message += " | order executed as far as the action allowed";
        }

        if ( result != fheroes2::GameMode::END_TURN && result != fheroes2::GameMode::CANCEL ) {
            message += " | scenario state changed";
        }
        return { true, std::move( message ) };
    }

    AdviceExecutionResult executeStrategicAdviceV12( Kingdom & kingdom, Player & player, const StrategicAdvice & advice )
    {
        switch ( advice.action ) {
        case AdviceAction::RECRUIT_CREATURES:
            return executeRecruitmentAdvice( kingdom, advice );
        case AdviceAction::BUILD_STRUCTURE:
            return executeBuildingAdvice( kingdom, advice );
        case AdviceAction::TOWN_ARMY_MANAGEMENT:
            return executeTownArmyAdvice( kingdom, advice );
        case AdviceAction::HERO_EXCHANGE:
            return executeHeroExchangeAdvice( kingdom, advice );
        case AdviceAction::MOVE_HERO:
        default:
            return executeMovementAdviceV12( kingdom, player, advice );
        }
    }

    void showCouncilV12ExecutionFeedback( const StrategicAdvice & advice, const AdviceExecutionResult & execution )
    {
        if ( !execution.success ) {
            fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
            return;
        }

        // Movement to an action object has already run through the game's normal human object
        // handler above. Keep one ordinary completion popup after every Advice action so there is
        // always a clear blocking boundary before the War Council is regenerated and shown again.
        std::string title = "Order Complete";
        if ( advice.action == AdviceAction::MOVE_HERO && advice.targetObjectType != MP2::OBJ_NONE ) {
            title = "Action Complete";
        }
        fheroes2::showStandardTextMessage( std::move( title ), execution.message, Dialog::OK );
    }

    void showRpgStrategicAdvisorV12()
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
        std::set<std::string> failedOrders;
        recentCouncilHeroKinds.clear();
        recentCouncilKinds.clear();
        recentCouncilMovementTargets.clear();
        recentCouncilV9HeroIntents.clear();

        while ( true ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV11( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV11( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV11( kingdom, failedOrders );
            const auto refreshedIt = std::find_if( refreshed.begin(), refreshed.end(), [&]( const StrategicAdvice & advice ) {
                return adviceIdentity( advice ) == selectedIdentity;
            } );
            if ( refreshedIt == refreshed.end() ) {
                councilReport = "Situation changed; advice refreshed.";
                fheroes2::showStandardTextMessage( "War Council", councilReport, Dialog::OK );
                continue;
            }

            const StrategicAdvice current = *refreshedIt;
            const AdviceExecutionResult execution = executeStrategicAdviceV12( kingdom, *player, current );
            councilReport = execution.message;

            // This is intentionally before the next generate/select cycle. Native map-object
            // dialogs have already completed inside Heroes::Action(), then this normal blocking
            // completion/failure popup completes the feedback sequence. Only after it closes can
            // the War Council appear again.
            showCouncilV12ExecutionFeedback( current, execution );

            if ( !execution.success ) {
                failedOrders.insert( selectedIdentity );
                continue;
            }

            if ( current.heroId >= 0 ) {
                recentCouncilHeroKinds[current.heroId] = current.kind;
                const CouncilV9Intent intent = councilV9IntentForAdvice( current );
                if ( intent != CouncilV9Intent::NONE ) {
                    recentCouncilV9HeroIntents[current.heroId] = intent;
                }
            }
            ++recentCouncilKinds[current.kind];
            if ( current.action == AdviceAction::MOVE_HERO && current.targetIndex >= 0 ) {
                recentCouncilMovementTargets.insert( current.targetIndex );
            }
        }
    }
}

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV12
