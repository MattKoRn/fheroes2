// War Council v15: live town feasibility and coherent resource commitments.
// v14 owns deterministic ranking and compact SAVE / READY signals. This layer filters stale
// town actions using the same live checks as execution and reserves exact kingdom resource vectors
// so the shortlist does not advertise mutually exclusive build/recruit spends as one coherent plan.
#include "game_rpg_advisor_v14.inc"
#include "payment.h"
#undef showRpgStrategicAdvisor

namespace
{
    bool isCouncilV15RecruitmentFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex );
        if ( castle == nullptr ) {
            return false;
        }

        const uint32_t actualDwelling = castle->GetActualDwelling( advice.dwellingType );
        if ( actualDwelling == BUILD_NOTHING || !castle->isBuild( actualDwelling ) ) {
            return false;
        }

        const Monster monster( castle->GetRace(), actualDwelling );
        if ( !hasRecruitmentRoom( *castle, monster ) ) {
            return false;
        }

        const uint32_t available = castle->getMonstersInDwelling( actualDwelling );
        const uint32_t affordable = castle->getRecruitLimit( monster, kingdom.GetFunds() );
        return std::min( advice.recruitCount, std::min( available, affordable ) ) > 0;
    }

    bool isCouncilV15BuildingFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex );
        return castle != nullptr && advice.buildingType != BUILD_NOTHING && castle->AllowBuyBuilding( advice.buildingType );
    }

    bool isCouncilV15TownArmyFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex );
        Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        if ( castle == nullptr || hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() || castle->GetHero() != hero ) {
            return false;
        }

        const Army & heroArmy = hero->GetArmy();
        const Army & garrison = castle->GetArmy();
        switch ( advice.kind ) {
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
            return garrison.isValid();
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
        case StrategicAdviceKind::TOWN_FEED_GARRISON:
            return heroArmy.GetOccupiedSlotCount() > 1;
        case StrategicAdviceKind::TOWN_MERGE_HERO:
            return armyHasDuplicateStacks( heroArmy );
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
            return armyHasDuplicateStacks( garrison );
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
            return armyCanSplitWeakest( heroArmy );
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
            return armyCanSplitWeakest( garrison );
        case StrategicAdviceKind::TOWN_OPTIMIZE_HERO:
        case StrategicAdviceKind::TOWN_OPTIMIZE_GARRISON:
            return true;
        default:
            return false;
        }
    }

    bool isCouncilV15LiveFeasible( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        switch ( advice.action ) {
        case AdviceAction::RECRUIT_CREATURES:
            return isCouncilV15RecruitmentFeasible( kingdom, advice );
        case AdviceAction::BUILD_STRUCTURE:
            return isCouncilV15BuildingFeasible( kingdom, advice );
        case AdviceAction::TOWN_ARMY_MANAGEMENT:
            return isCouncilV15TownArmyFeasible( kingdom, advice );
        default:
            return true;
        }
    }

    Funds councilV15SpendCost( Kingdom & kingdom, const StrategicAdvice & advice )
    {
        Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex );
        if ( castle == nullptr ) {
            return {};
        }

        if ( advice.action == AdviceAction::BUILD_STRUCTURE ) {
            if ( advice.buildingType == BUILD_NOTHING || !castle->AllowBuyBuilding( advice.buildingType ) ) {
                return {};
            }
            return PaymentConditions::BuyBuilding( castle->GetRace(), advice.buildingType );
        }

        if ( advice.action == AdviceAction::RECRUIT_CREATURES ) {
            const uint32_t actualDwelling = castle->GetActualDwelling( advice.dwellingType );
            if ( actualDwelling == BUILD_NOTHING || !castle->isBuild( actualDwelling ) ) {
                return {};
            }

            const Monster monster( castle->GetRace(), actualDwelling );
            const uint32_t available = castle->getMonstersInDwelling( actualDwelling );
            const uint32_t affordable = castle->getRecruitLimit( monster, kingdom.GetFunds() );
            const uint32_t count = std::min( advice.recruitCount, std::min( available, affordable ) );
            if ( count == 0 ) {
                return {};
            }
            return monster.GetCost() * count;
        }

        return {};
    }

    void filterCouncilV15LiveFeasibility( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom )
    {
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              return !isCouncilV15LiveFeasible( kingdom, advice );
                          } ),
                          candidates.end() );
    }

    void applyCouncilV15SpendCommitments( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const CouncilContext & context )
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
            const Funds cost = councilV15SpendCost( kingdom, advice );
            if ( cost.GetValidItemsCount() == 0 ) {
                advice.score = 0.0;
                continue;
            }

            // Very weak spend alternatives should not reserve budget ahead of stronger non-spend work.
            const bool worthPlanning = advice.score >= std::max( 3200.0, bestSpendScore * 0.70 );
            if ( worthPlanning && remaining >= cost ) {
                remaining -= cost;
                ++committedCount;
                if ( committedCount > 1 ) {
                    advice.rationale = "Fits after higher-priority spending. | " + advice.rationale;
                }
                continue;
            }

            if ( committedCount > 0 && !( remaining >= cost ) ) {
                advice.score *= ( context.lowGold || context.lowMaterials ) ? 0.44 : 0.62;
                advice.rationale = "Competes with higher-priority spending; current resources cannot cover both. | " + advice.rationale;
            }
            else if ( !worthPlanning ) {
                advice.score *= 0.82;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV15( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        // Eliminate stale town actions before they influence sequence planning, resource judgment,
        // owner alternatives or visible shortlist diversity.
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

        // Use exact current resource vectors after all strategic scoring so the visible spending
        // plan reflects priority while still respecting gold, wood, ore and rare-resource conflicts.
        applyCouncilV15SpendCommitments( candidates, kingdom, context );
        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV15()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV15( kingdom, suppressedOrders );
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
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV15( kingdom, suppressedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV15
