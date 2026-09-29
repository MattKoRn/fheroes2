// War Council v16: live hero executability before strategic scoring.
// v15 owns exact town feasibility and resource commitments. This layer filters stale movement
// and hero-exchange orders using the same live map, path and adjacency rules execution relies on,
// so impossible hero orders cannot consume shortlist slots or earn compact READY signals.
#include "game_rpg_advisor_v15.inc"
#undef showRpgStrategicAdvisor

namespace
{
    bool councilV16TargetStillMatches( const StrategicAdvice & advice )
    {
        if ( advice.targetIndex < 0 || !Maps::isValidAbsIndex( advice.targetIndex ) ) {
            return false;
        }

        const Maps::Tile & tile = world.getTile( advice.targetIndex );
        switch ( advice.targetObjectType ) {
        case MP2::OBJ_NONE:
            return tile.getMainObjectType() == MP2::OBJ_NONE;
        case MP2::OBJ_CASTLE:
            return world.getCastleEntrance( Maps::GetPoint( advice.targetIndex ) ) != nullptr;
        case MP2::OBJ_HERO:
            return tile.getHero() != nullptr;
        default:
            return tile.getMainObjectType() == advice.targetObjectType;
        }
    }

    bool isCouncilV16MovementFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        if ( hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() || hero->GetMovePoints() == 0
             || !hero->MayStillMove( false, false ) ) {
            return false;
        }
        if ( advice.targetIndex < 0 || !Maps::isValidAbsIndex( advice.targetIndex ) || hero->GetIndex() == advice.targetIndex ) {
            return false;
        }
        if ( !councilV16TargetStillMatches( advice ) ) {
            return false;
        }

        // If the normal object-state resolver already considers this target handled, recommending
        // it again is stale even if a route still exists (captured mine, visited object, friendly
        // former combat target, and similar state changes).
        if ( advice.targetObjectType != MP2::OBJ_NONE && isMovementInteractionResolved( kingdom, *hero, advice ) ) {
            return false;
        }

        AIWorldPathfinder pathfinder;
        pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
        pathfinder.setSpellPointsReserveRatio( 0.30 );
        pathfinder.reEvaluateIfNeeded( *hero );

        const uint32_t liveDistance = pathfinder.getDistance( advice.targetIndex );
        if ( liveDistance == 0 || liveDistance == std::numeric_limits<uint32_t>::max() || liveDistance > hero->GetMovePoints() ) {
            return false;
        }

        return !pathfinder.buildPath( advice.targetIndex, false ).empty();
    }

    bool isCouncilV16HeroExchangeFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Heroes * mainHero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        Heroes * supportHero = advice.secondaryHeroId >= 0 ? world.GetHeroes( advice.secondaryHeroId ) : nullptr;
        if ( mainHero == nullptr || supportHero == nullptr || mainHero == supportHero || !mainHero->isActive() || !supportHero->isActive()
             || mainHero->GetColor() != kingdom.GetColor() || supportHero->GetColor() != kingdom.GetColor()
             || !heroesAreAdjacent( *mainHero, *supportHero ) ) {
            return false;
        }

        Army & mainArmy = mainHero->GetArmy();
        Army & supportArmy = supportHero->GetArmy();
        switch ( advice.kind ) {
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
            return supportArmy.isValid();
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
            return supportHero->GetBagArtifacts().CountArtifacts() > 0;
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
            return true;
        case StrategicAdviceKind::HERO_SWAP_ARMIES:
            return mainArmy.isValid() && supportArmy.isValid();
        case StrategicAdviceKind::HERO_MERGE_MAIN:
            return armyHasDuplicateStacks( mainArmy );
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
            return armyHasDuplicateStacks( supportArmy );
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
            return armyCanSplitWeakest( mainArmy );
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
            return armyCanSplitWeakest( supportArmy );
        case StrategicAdviceKind::HERO_OPTIMIZE_PAIR:
            return true;
        case StrategicAdviceKind::HERO_FAST_RESERVE: {
            const Troop * fast = mainArmy.GetFastestTroop();
            const Troop * supportFast = supportArmy.GetFastestTroop();
            if ( fast == nullptr || !fast->isValid() || fast->GetCount() <= 1
                 || ( supportFast != nullptr && supportFast->isValid() && supportFast->GetSpeed() >= fast->GetSpeed() )
                 || !supportArmy.CanJoinTroop( fast->GetMonster() ) ) {
                return false;
            }

            const uint32_t count = std::min<uint32_t>( 3, fast->GetCount() - 1 );
            const Troop reserve( fast->GetMonster(), count );
            return reserve.GetStrength() <= std::max( 1.0, mainArmy.GetStrength() ) / 20.0;
        }
        default:
            return false;
        }
    }

    bool isCouncilV16LiveHeroFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        if ( advice.action == AdviceAction::MOVE_HERO ) {
            return isCouncilV16MovementFeasible( kingdom, advice );
        }
        if ( advice.action == AdviceAction::HERO_EXCHANGE ) {
            return isCouncilV16HeroExchangeFeasible( kingdom, advice );
        }
        return true;
    }

    void filterCouncilV16LiveHeroFeasibility( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom )
    {
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              return !isCouncilV16LiveHeroFeasible( kingdom, advice );
                          } ),
                          candidates.end() );
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV16( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
    {
        const CouncilContext context = buildCouncilContext( kingdom );
        std::vector<StrategicAdvice> rawCandidates = gatherCouncilCandidates( kingdom, context );
        appendFrontierBenefitAdvice( rawCandidates, kingdom );

        std::vector<StrategicAdvice> candidates = scoreCouncilCandidates( std::move( rawCandidates ), kingdom, context );
        if ( !excludedOrders.empty() ) {
            candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                                  return excludedOrders.count( adviceIdentity( advice ) ) > 0;
                              } ),
                              candidates.end() );
        }

        // Reject stale hero and town orders before they can influence sequences, reservations,
        // opportunity-cost comparisons or the visible shortlist.
        filterCouncilV16LiveHeroFeasibility( candidates, kingdom );
        filterCouncilV15LiveFeasibility( candidates, kingdom );

        applyCouncilSequencePlanning( candidates );
        applyCouncilResourceReservation( candidates, context );
        applyCouncilV9Continuity( candidates );
        applyCouncilObjectiveReservations( candidates, context );
        applyCouncilV9Intelligence( candidates, context );
        applyCouncilV11Intelligence( candidates, context );
        applyCouncilV14ResourceJudgment( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );
        applyCouncilV15SpendCommitments( candidates, kingdom, context );
        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV16()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV16( kingdom, suppressedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV14( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV16( kingdom, suppressedOrders );
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

            showCouncilV13ExecutionFeedback( current, execution );
            councilReport = councilV13ReportAfterExecution( current, execution, objectResolved );

            if ( !execution.success ) {
                suppressedOrders.insert( selectedIdentity );
                continue;
            }

            if ( councilV13UsesNativeObjectFeedback( current ) && !objectResolved ) {
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV16
