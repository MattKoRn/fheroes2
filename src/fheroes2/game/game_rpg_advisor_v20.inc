// War Council v20: simulated remaining-budget planning with partial recruitment.
// v19 coordinates heroes by local objective area. This layer replaces v15's all-or-nothing
// spend commitment estimate with a live remaining-resource simulation so recruitment can remain
// useful when only part of the original stack fits after stronger planned spending.
#include "game_rpg_advisor_v19.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV20SpendMode : uint8_t
    {
        NONE,
        FULL,
        PARTIAL,
        DEFER
    };

    struct CouncilV20SpendPlan
    {
        CouncilV20SpendMode mode{ CouncilV20SpendMode::NONE };
        Funds cost;
        uint32_t requestedRecruitCount{ 0 };
        uint32_t plannedRecruitCount{ 0 };
    };

    CouncilV20SpendPlan councilV20PlanSpend( Kingdom & kingdom, const StrategicAdvice & advice, const Funds & remaining )
    {
        CouncilV20SpendPlan plan;
        Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex );
        if ( castle == nullptr ) {
            return plan;
        }

        if ( advice.action == AdviceAction::BUILD_STRUCTURE ) {
            if ( advice.buildingType == BUILD_NOTHING || !castle->AllowBuyBuilding( advice.buildingType ) ) {
                return plan;
            }

            plan.cost = PaymentConditions::BuyBuilding( castle->GetRace(), advice.buildingType );
            plan.mode = remaining >= plan.cost ? CouncilV20SpendMode::FULL : CouncilV20SpendMode::DEFER;
            return plan;
        }

        if ( advice.action != AdviceAction::RECRUIT_CREATURES ) {
            return plan;
        }

        const uint32_t actualDwelling = castle->GetActualDwelling( advice.dwellingType );
        if ( actualDwelling == BUILD_NOTHING || !castle->isBuild( actualDwelling ) ) {
            return plan;
        }

        const Monster monster( castle->GetRace(), actualDwelling );
        if ( !hasRecruitmentRoom( *castle, monster ) ) {
            return plan;
        }

        const uint32_t available = castle->getMonstersInDwelling( actualDwelling );
        plan.requestedRecruitCount = std::min( advice.recruitCount, available );
        if ( plan.requestedRecruitCount == 0 ) {
            return plan;
        }

        const uint32_t affordableFromRemaining = castle->getRecruitLimit( monster, remaining );
        plan.plannedRecruitCount = std::min( plan.requestedRecruitCount, affordableFromRemaining );
        if ( plan.plannedRecruitCount == 0 ) {
            plan.mode = CouncilV20SpendMode::DEFER;
            return plan;
        }

        plan.cost = monster.GetCost() * plan.plannedRecruitCount;
        plan.mode = plan.plannedRecruitCount >= plan.requestedRecruitCount ? CouncilV20SpendMode::FULL : CouncilV20SpendMode::PARTIAL;
        return plan;
    }

    void applyCouncilV20SpendCommitments( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const CouncilContext & context )
    {
        std::vector<size_t> spendIndexes;
        spendIndexes.reserve( candidates.size() );
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            if ( isCouncilResourceSpend( candidates[index] ) && std::isfinite( candidates[index].score ) && candidates[index].score > 0.0 ) {
                spendIndexes.emplace_back( index );
            }
        }
        if ( spendIndexes.size() < 2 ) {
            return;
        }

        std::sort( spendIndexes.begin(), spendIndexes.end(), [&]( const size_t left, const size_t right ) {
            return councilV14DeterministicLess( candidates[left], candidates[right] );
        } );

        const double bestSpendScore = candidates[spendIndexes.front()].score;
        Funds remaining = kingdom.GetFunds();
        size_t committedCount = 0;

        for ( const size_t index : spendIndexes ) {
            StrategicAdvice & advice = candidates[index];

            // Weak alternatives should not consume the simulated pool ahead of stronger work.
            const bool worthPlanning = advice.score >= std::max( 3200.0, bestSpendScore * 0.70 );
            if ( !worthPlanning ) {
                advice.score *= 0.82;
                continue;
            }

            const CouncilV20SpendPlan plan = councilV20PlanSpend( kingdom, advice, remaining );
            if ( plan.mode == CouncilV20SpendMode::NONE ) {
                advice.score = 0.0;
                continue;
            }

            if ( plan.mode == CouncilV20SpendMode::DEFER ) {
                advice.score *= ( context.lowGold || context.lowMaterials ) ? 0.46 : 0.64;
                advice.rationale = "DEFER. Stronger spending uses this budget. | " + advice.rationale;
                continue;
            }

            remaining -= plan.cost;
            ++committedCount;

            if ( plan.mode == CouncilV20SpendMode::PARTIAL ) {
                const double fraction = static_cast<double>( plan.plannedRecruitCount ) / std::max<uint32_t>( 1, plan.requestedRecruitCount );
                advice.score *= 0.78 + fraction * 0.18;
                advice.rationale = "PARTIAL " + std::to_string( plan.plannedRecruitCount ) + "/" + std::to_string( plan.requestedRecruitCount )
                                   + " fits after earlier spend. | " + advice.rationale;
            }
            else if ( committedCount > 1 ) {
                advice.rationale = "FULL. Fits after earlier spend. | " + advice.rationale;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV20( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        filterCouncilV16LiveHeroFeasibility( candidates, kingdom );
        filterCouncilV15LiveFeasibility( candidates, kingdom );

        applyCouncilSequencePlanning( candidates );
        applyCouncilResourceReservation( candidates, context );
        applyCouncilV9Continuity( candidates );
        applyCouncilObjectiveReservations( candidates, context );
        applyCouncilV9Intelligence( candidates, context );
        applyCouncilV11Intelligence( candidates, context );
        applyCouncilV17LiveCombat( candidates, kingdom );
        applyCouncilV19AreaCoordination( candidates, kingdom, context );
        applyCouncilV18FollowThrough( candidates, kingdom );

        applyCouncilV14ResourceJudgment( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );

        // Unlike v15, recruitment affordability is recalculated against the resources remaining
        // after each stronger commitment. Do not mutate advice.recruitCount: the order is fully
        // affordable if selected now; PARTIAL describes compatibility with the simulated prior plan.
        applyCouncilV20SpendCommitments( candidates, kingdom, context );
        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV20()
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
        recentCouncilV18Plan = {};

        while ( true ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV20( kingdom, suppressedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV17( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV20( kingdom, suppressedOrders );
            const auto refreshedIt = std::find_if( refreshed.begin(), refreshed.end(), [&]( const StrategicAdvice & advice ) {
                return adviceIdentity( advice ) == selectedIdentity;
            } );
            if ( refreshedIt == refreshed.end() ) {
                councilReport = "Situation changed; advice refreshed.";
                fheroes2::showStandardTextMessage( "War Council", councilReport, Dialog::OK );
                recentCouncilV18Plan = {};
                continue;
            }

            const StrategicAdvice current = *refreshedIt;
            const AdviceExecutionResult execution = executeStrategicAdviceV12( kingdom, *player, current );
            const bool objectResolved = councilV13ObjectInteractionResolved( kingdom, current );

            showCouncilV13ExecutionFeedback( current, execution );
            councilReport = councilV13ReportAfterExecution( current, execution, objectResolved );

            if ( !execution.success ) {
                suppressedOrders.insert( selectedIdentity );
                recentCouncilV18Plan = {};
                continue;
            }

            if ( councilV13UsesNativeObjectFeedback( current ) && !objectResolved ) {
                suppressedOrders.insert( selectedIdentity );
                recentCouncilV18Plan = {};
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

            rememberCouncilV18Plan( current, kingdom );
        }
    }
}

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV20
