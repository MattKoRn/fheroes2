// War Council v19: area-level hero coordination without widening the UI.
// v18 owns one-step follow-through. This layer adds a small local reservation radius around
// strong movement objectives so different heroes do not spend the shortlist doing the same
// practical job on adjacent tiles. Urgent defense and verified continuation remain authoritative.
#include "game_rpg_advisor_v18.inc"
#undef showRpgStrategicAdvisor

namespace
{
    struct CouncilV19AreaClaim
    {
        int32_t heroId{ -1 };
        int32_t targetIndex{ -1 };
        CouncilV18PlanFamily family{ CouncilV18PlanFamily::NONE };
        double score{ 0.0 };
    };

    bool councilV19CanClaimArea( const StrategicAdvice & advice )
    {
        if ( advice.action != AdviceAction::MOVE_HERO || advice.heroId < 0 || advice.targetIndex < 0
             || !Maps::isValidAbsIndex( advice.targetIndex ) || !advice.reachableThisTurn || !std::isfinite( advice.score ) ) {
            return false;
        }

        const CouncilV18PlanFamily family = councilV18PlanFamilyForKind( advice.kind );
        switch ( family ) {
        case CouncilV18PlanFamily::DEFENSE:
        case CouncilV18PlanFamily::STRIKE:
        case CouncilV18PlanFamily::ECONOMY:
        case CouncilV18PlanFamily::DEVELOPMENT:
            return advice.score >= 5200.0;

        case CouncilV18PlanFamily::POSITION:
            // Only strong staging orders reserve an area. Routine movement should not paint
            // broad exclusion zones across the map.
            return advice.score >= 8200.0;

        default:
            return false;
        }
    }

    uint32_t councilV19AreaDistance( const int32_t first, const int32_t second )
    {
        if ( first < 0 || second < 0 || !Maps::isValidAbsIndex( first ) || !Maps::isValidAbsIndex( second ) ) {
            return std::numeric_limits<uint32_t>::max();
        }
        return Maps::GetApproximateDistance( first, second );
    }

    bool councilV19RedundantNearClaim( const StrategicAdvice & advice, const CouncilV19AreaClaim & claim )
    {
        if ( advice.action != AdviceAction::MOVE_HERO || advice.heroId < 0 || advice.heroId == claim.heroId || advice.targetIndex < 0
             || !Maps::isValidAbsIndex( advice.targetIndex ) ) {
            return false;
        }

        const CouncilV18PlanFamily family = councilV18PlanFamilyForKind( advice.kind );
        if ( family == CouncilV18PlanFamily::NONE || councilV19AreaDistance( advice.targetIndex, claim.targetIndex ) > 4 ) {
            return false;
        }

        // Staging, scouting and generic support movement are the main dog-pile offenders: once a
        // stronger hero owns the nearby premium job, these heroes should fan out instead.
        if ( family == CouncilV18PlanFamily::POSITION || family == CouncilV18PlanFamily::SCOUT ) {
            return true;
        }

        if ( family == CouncilV18PlanFamily::SUPPORT ) {
            return claim.family == CouncilV18PlanFamily::DEFENSE || claim.family == CouncilV18PlanFamily::STRIKE
                   || claim.family == CouncilV18PlanFamily::ECONOMY || claim.family == CouncilV18PlanFamily::DEVELOPMENT;
        }

        // Adjacent economic/development objects often represent one practical sweep. Keep the
        // penalty modest later so a genuinely strong second pickup can still survive.
        return family == claim.family
               && ( family == CouncilV18PlanFamily::ECONOMY || family == CouncilV18PlanFamily::DEVELOPMENT );
    }

    bool councilV19MaterialReinforcement( const StrategicAdvice & advice, const CouncilV19AreaClaim & claim, const CouncilContext & context )
    {
        if ( claim.family != CouncilV18PlanFamily::DEFENSE ) {
            return false;
        }

        if ( councilV17IsDefenseOrder( advice ) ) {
            // Nearby defensive orders may correspond to separate threatened towns. Exact-target
            // competition was already resolved by v6, so do not suppress another live warning here.
            return true;
        }

        if ( advice.kind != StrategicAdviceKind::GARRISON_REINFORCEMENT && advice.kind != StrategicAdviceKind::TOWN_RESUPPLY ) {
            return false;
        }

        const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        if ( hero == nullptr || !hero->isActive() ) {
            return false;
        }

        const double relativeStrength = hero->GetArmy().GetStrength() / std::max( 1.0, context.strongestHeroStrength );
        return relativeStrength >= 0.45 && advice.score >= claim.score * 0.58;
    }

    bool councilV19ProtectedFollowThrough( const StrategicAdvice & advice, Kingdom & kingdom, const bool urgentDefense,
                                           const std::vector<const Heroes *> & visibleEnemies )
    {
        if ( recentCouncilV18Plan.heroId < 0 || advice.heroId != recentCouncilV18Plan.heroId ) {
            return false;
        }
        if ( urgentDefense && recentCouncilV18Plan.family != CouncilV18PlanFamily::DEFENSE ) {
            return false;
        }

        uint32_t proximityDistance = std::numeric_limits<uint32_t>::max();
        return councilV18IsContinuationCandidate( advice, kingdom, visibleEnemies, proximityDistance );
    }

    std::vector<CouncilV19AreaClaim> buildCouncilV19AreaClaims( const std::vector<StrategicAdvice> & candidates )
    {
        std::vector<size_t> ranked;
        ranked.reserve( candidates.size() );
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            if ( councilV19CanClaimArea( candidates[index] ) ) {
                ranked.emplace_back( index );
            }
        }

        std::sort( ranked.begin(), ranked.end(), [&]( const size_t left, const size_t right ) {
            return councilV14DeterministicLess( candidates[left], candidates[right] );
        } );

        std::vector<CouncilV19AreaClaim> claims;
        for ( const size_t index : ranked ) {
            const StrategicAdvice & advice = candidates[index];
            const CouncilV18PlanFamily family = councilV18PlanFamilyForKind( advice.kind );

            bool alreadyCovered = false;
            for ( const CouncilV19AreaClaim & claim : claims ) {
                if ( claim.family == family && councilV19AreaDistance( claim.targetIndex, advice.targetIndex ) <= 4 ) {
                    alreadyCovered = true;
                    break;
                }
            }
            if ( alreadyCovered ) {
                continue;
            }

            claims.push_back( { advice.heroId, advice.targetIndex, family, advice.score } );
        }
        return claims;
    }

    void applyCouncilV19AreaCoordination( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const CouncilContext & context )
    {
        const std::vector<CouncilV19AreaClaim> claims = buildCouncilV19AreaClaims( candidates );
        if ( claims.empty() ) {
            return;
        }

        const bool urgentDefense = councilV18HasUrgentDefense( candidates );
        const std::vector<const Heroes *> visibleEnemies = councilV17VisibleEnemyHeroes( kingdom );

        for ( StrategicAdvice & advice : candidates ) {
            if ( advice.action != AdviceAction::MOVE_HERO || !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                continue;
            }
            if ( councilV19ProtectedFollowThrough( advice, kingdom, urgentDefense, visibleEnemies ) ) {
                continue;
            }

            const CouncilV19AreaClaim * bestClaim = nullptr;
            uint32_t bestDistance = std::numeric_limits<uint32_t>::max();
            for ( const CouncilV19AreaClaim & claim : claims ) {
                if ( !councilV19RedundantNearClaim( advice, claim ) || councilV19MaterialReinforcement( advice, claim, context ) ) {
                    continue;
                }

                const uint32_t distance = councilV19AreaDistance( advice.targetIndex, claim.targetIndex );
                if ( bestClaim == nullptr || distance < bestDistance || ( distance == bestDistance && claim.score > bestClaim->score ) ) {
                    bestClaim = &claim;
                    bestDistance = distance;
                }
            }

            if ( bestClaim == nullptr ) {
                continue;
            }

            const CouncilV18PlanFamily family = councilV18PlanFamilyForKind( advice.kind );
            if ( family == CouncilV18PlanFamily::POSITION || family == CouncilV18PlanFamily::SCOUT ) {
                advice.score *= 0.46;
            }
            else if ( family == CouncilV18PlanFamily::SUPPORT ) {
                advice.score *= 0.58;
            }
            else {
                advice.score *= 0.74;
            }

            const Heroes * owner = world.GetHeroes( bestClaim->heroId );
            advice.rationale = "Area covered by " + std::string( owner != nullptr ? owner->GetName() : "another hero" )
                               + "; spread useful work. | " + advice.rationale;
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV19( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        // Exact-target ownership is already handled by v6. v19 coordinates the small surrounding
        // area before v18 chooses a single legitimate continuation.
        applyCouncilV19AreaCoordination( candidates, kingdom, context );
        applyCouncilV18FollowThrough( candidates, kingdom );

        applyCouncilV14ResourceJudgment( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );
        applyCouncilV15SpendCommitments( candidates, kingdom, context );
        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV19()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV19( kingdom, suppressedOrders );
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
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV19( kingdom, suppressedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV19
