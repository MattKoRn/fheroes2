// War Council v13: native-feedback-aware refresh and declined-object memory.
// v12 restores normal human map-object dialogs before Advice returns. This layer keeps those
// native dialogs authoritative, avoids duplicating them with a second completion popup, and
// suppresses an exact map-object recommendation for the current council session when the player
// deliberately leaves that interaction unresolved.
#include "game_rpg_advisor_v12.inc"
#undef showRpgStrategicAdvisor

namespace
{
    bool councilV13UsesNativeObjectFeedback( const StrategicAdvice & advice )
    {
        return advice.action == AdviceAction::MOVE_HERO && advice.targetObjectType != MP2::OBJ_NONE;
    }

    bool councilV13ObjectInteractionResolved( const Kingdom & kingdom, const StrategicAdvice & advice )
    {
        if ( !councilV13UsesNativeObjectFeedback( advice ) ) {
            return true;
        }

        const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        if ( hero == nullptr || !hero->isActive() ) {
            return true;
        }

        return isMovementInteractionResolved( kingdom, *hero, advice );
    }

    void showCouncilV13ExecutionFeedback( const StrategicAdvice & advice, const AdviceExecutionResult & execution )
    {
        if ( !execution.success ) {
            fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
            return;
        }

        // Map-object movement has already gone through Heroes::Action(), which owns the game's
        // normal blocking object/battle/reward/choice dialogs. Do not echo that native feedback
        // with another generic "Action Complete" popup before War Council returns.
        if ( councilV13UsesNativeObjectFeedback( advice ) ) {
            return;
        }

        // Direct Advice operations do not necessarily own a native game dialog, so retain one
        // ordinary blocking completion popup to preserve the v12 "feedback before Advice" rule.
        fheroes2::showStandardTextMessage( "Order Complete", execution.message, Dialog::OK );
    }

    std::string councilV13ReportAfterExecution( const StrategicAdvice & advice, const AdviceExecutionResult & execution, const bool objectResolved )
    {
        if ( !execution.success ) {
            return "Order failed; advice refreshed.";
        }

        if ( councilV13UsesNativeObjectFeedback( advice ) ) {
            if ( !objectResolved ) {
                return "Skipped that object choice for this council session.";
            }

            // The normal game dialog already explained the object result. Avoid repeating it in
            // the War Council banner as well.
            return {};
        }

        return "Order complete; advice refreshed.";
    }

    void showRpgStrategicAdvisorV13()
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
        std::set<std::string> suppressedOrders;
        recentCouncilHeroKinds.clear();
        recentCouncilKinds.clear();
        recentCouncilMovementTargets.clear();
        recentCouncilV9HeroIntents.clear();

        while ( true ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV11( kingdom, suppressedOrders );
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
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV11( kingdom, suppressedOrders );
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
            const bool objectResolved = councilV13ObjectInteractionResolved( kingdom, current );

            // Native game dialogs have already completed inside the action by this point. Only
            // non-native Advice operations receive an extra completion popup.
            showCouncilV13ExecutionFeedback( current, execution );
            councilReport = councilV13ReportAfterExecution( current, execution, objectResolved );

            if ( !execution.success ) {
                suppressedOrders.insert( selectedIdentity );
                continue;
            }

            if ( councilV13UsesNativeObjectFeedback( current ) && !objectResolved ) {
                // A remaining unresolved object after normal human interaction means the player
                // chose not to resolve it (or otherwise left it untouched). Respect that choice:
                // do not immediately recommend the exact same order again in this council session.
                suppressedOrders.insert( selectedIdentity );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV13
