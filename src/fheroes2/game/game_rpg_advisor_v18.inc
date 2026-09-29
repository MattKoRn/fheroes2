// War Council v18: coherent one-step follow-through without sticky repetition.
// v17 owns live combat signals. This layer remembers the most recent successful hero plan for
// the current War Council session and selectively restores value to one genuine continuation,
// while yielding immediately to urgent defense, stale state or unsafe combat.
#include "game_rpg_advisor_v17.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV18PlanFamily : uint8_t
    {
        NONE,
        DEFENSE,
        STRIKE,
        POSITION,
        SCOUT,
        ECONOMY,
        DEVELOPMENT,
        SUPPORT,
        RECOVERY
    };

    CouncilV18PlanFamily councilV18PlanFamilyForKind( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::DEFENSE:
        case StrategicAdviceKind::CASTLE_RELIEF:
            return CouncilV18PlanFamily::DEFENSE;

        case StrategicAdviceKind::OFFENSE:
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
            return CouncilV18PlanFamily::STRIKE;

        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
        case StrategicAdviceKind::FLANKING_MARCH:
        case StrategicAdviceKind::LOGISTICS:
        case StrategicAdviceKind::OPPORTUNITY:
            return CouncilV18PlanFamily::POSITION;

        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
            return CouncilV18PlanFamily::SCOUT;

        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
            return CouncilV18PlanFamily::ECONOMY;

        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::SHRINE_STUDY:
        case StrategicAdviceKind::MANA_RESTORATION:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
        case StrategicAdviceKind::DEVELOPMENT:
            return CouncilV18PlanFamily::DEVELOPMENT;

        case StrategicAdviceKind::TRAVEL_TO_TOWN:
        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
        case StrategicAdviceKind::TOWN_FEED_GARRISON:
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::TOWN_OPTIMIZE_HERO:
        case StrategicAdviceKind::TOWN_OPTIMIZE_GARRISON:
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
        case StrategicAdviceKind::HERO_SWAP_ARMIES:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
        case StrategicAdviceKind::HERO_OPTIMIZE_PAIR:
        case StrategicAdviceKind::HERO_FAST_RESERVE:
        case StrategicAdviceKind::RECRUIT_HERO:
            return CouncilV18PlanFamily::SUPPORT;

        case StrategicAdviceKind::REGROUP:
            return CouncilV18PlanFamily::RECOVERY;

        default:
            return CouncilV18PlanFamily::NONE;
        }
    }

    struct CouncilV18PlanMemory
    {
        int32_t heroId{ -1 };
        int32_t anchorIndex{ -1 };
        StrategicAdviceKind kind{ StrategicAdviceKind::OFFENSE };
        CouncilV9Intent intent{ CouncilV9Intent::NONE };
        CouncilV18PlanFamily family{ CouncilV18PlanFamily::NONE };
    };

    CouncilV18PlanMemory recentCouncilV18Plan;

    bool councilV18HasUrgentDefense( const std::vector<StrategicAdvice> & candidates )
    {
        for ( const StrategicAdvice & advice : candidates ) {
            if ( councilV17IsDefenseOrder( advice ) && advice.reachableThisTurn && std::isfinite( advice.score ) && advice.score >= 2500.0 ) {
                return true;
            }
        }
        return false;
    }

    bool councilV18ContinuationAction( const StrategicAdvice & advice )
    {
        return advice.action == AdviceAction::MOVE_HERO || advice.action == AdviceAction::HERO_EXCHANGE
               || advice.action == AdviceAction::TOWN_ARMY_MANAGEMENT;
    }

    bool councilV18CombatContinuationSafe( const StrategicAdvice & advice, Kingdom & kingdom,
                                           const std::vector<const Heroes *> & visibleEnemies )
    {
        if ( councilV17IsDefenseOrder( advice ) ) {
            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            if ( hero == nullptr || !hero->isActive() ) {
                return false;
            }

            const CouncilV17DefenseMatchup matchup = councilV17DefenseMatchup( kingdom, advice, visibleEnemies );
            if ( matchup.threat == nullptr || matchup.enemyStrength <= 0.0 || matchup.garrisonStrength <= 0.0 ) {
                return false;
            }

            const double combinedDefense = std::max( 1.0, hero->GetArmy().GetStrength() + matchup.garrisonStrength );
            return councilV17SignalForEdge( combinedDefense / matchup.enemyStrength ) != CouncilV17CombatSignal::DANGER;
        }

        if ( isCouncilV9CombatAdvice( advice.kind ) ) {
            return councilV11RiskForAdvice( advice ) == CouncilV9Risk::SOLID;
        }
        return true;
    }

    uint32_t councilV18ContinuationDistance( const StrategicAdvice & advice )
    {
        if ( recentCouncilV18Plan.anchorIndex < 0 || !Maps::isValidAbsIndex( recentCouncilV18Plan.anchorIndex ) ) {
            return std::numeric_limits<uint32_t>::max();
        }

        int32_t candidateIndex = advice.targetIndex;
        if ( candidateIndex < 0 || !Maps::isValidAbsIndex( candidateIndex ) ) {
            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            if ( hero == nullptr || !hero->isActive() ) {
                return std::numeric_limits<uint32_t>::max();
            }
            candidateIndex = hero->GetIndex();
        }

        return Maps::GetApproximateDistance( recentCouncilV18Plan.anchorIndex, candidateIndex );
    }

    bool councilV18IsContinuationCandidate( const StrategicAdvice & advice, Kingdom & kingdom,
                                            const std::vector<const Heroes *> & visibleEnemies, uint32_t & proximityDistance )
    {
        if ( recentCouncilV18Plan.heroId < 0 || recentCouncilV18Plan.intent == CouncilV9Intent::NONE
             || recentCouncilV18Plan.family == CouncilV18PlanFamily::NONE || advice.heroId != recentCouncilV18Plan.heroId
             || !councilV18ContinuationAction( advice ) || !advice.reachableThisTurn || !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
            return false;
        }

        const CouncilV9Intent nextIntent = councilV9IntentForAdvice( advice );
        const CouncilV18PlanFamily nextFamily = councilV18PlanFamilyForKind( advice.kind );
        if ( nextIntent == CouncilV9Intent::NONE || nextIntent != recentCouncilV18Plan.intent || nextFamily != recentCouncilV18Plan.family ) {
            return false;
        }

        if ( !councilV18CombatContinuationSafe( advice, kingdom, visibleEnemies ) ) {
            return false;
        }

        proximityDistance = councilV18ContinuationDistance( advice );
        return proximityDistance <= 6;
    }

    void applyCouncilV18FollowThrough( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom )
    {
        if ( recentCouncilV18Plan.heroId < 0 || recentCouncilV18Plan.intent == CouncilV9Intent::NONE
             || recentCouncilV18Plan.family == CouncilV18PlanFamily::NONE ) {
            return;
        }

        const bool urgentDefense = councilV18HasUrgentDefense( candidates );
        // A new emergency defense is a strategic interruption, not a continuation. Only an
        // already-defensive plan may carry FOLLOW through an urgent-defense refresh.
        if ( urgentDefense && recentCouncilV18Plan.family != CouncilV18PlanFamily::DEFENSE ) {
            return;
        }

        const std::vector<const Heroes *> visibleEnemies = councilV17VisibleEnemyHeroes( kingdom );
        size_t bestIndex = candidates.size();
        double bestFit = 0.0;
        uint32_t bestDistance = std::numeric_limits<uint32_t>::max();

        for ( size_t index = 0; index < candidates.size(); ++index ) {
            StrategicAdvice & advice = candidates[index];
            uint32_t proximityDistance = std::numeric_limits<uint32_t>::max();
            if ( !councilV18IsContinuationCandidate( advice, kingdom, visibleEnemies, proximityDistance ) ) {
                continue;
            }

            const double proximity = 1.0 - std::min( 6.0, static_cast<double>( proximityDistance ) ) / 6.0;
            // v3 deliberately suppresses immediate same-kind repeats. Compensate only for a verified
            // same-hero, same-intent, same-family nearby continuation so anti-spam remains intact elsewhere.
            const double repeatRecovery = advice.kind == recentCouncilV18Plan.kind ? 1.85 : 1.0;
            const double fit = advice.score * repeatRecovery * ( 1.10 + proximity * 0.10 );

            if ( bestIndex == candidates.size() || fit > bestFit
                 || ( fit == bestFit && ( proximityDistance < bestDistance
                                          || ( proximityDistance == bestDistance && councilV14DeterministicLess( advice, candidates[bestIndex] ) ) ) ) ) {
                bestIndex = index;
                bestFit = fit;
                bestDistance = proximityDistance;
            }
        }

        if ( bestIndex == candidates.size() ) {
            return;
        }

        StrategicAdvice & follow = candidates[bestIndex];
        const double proximity = 1.0 - std::min( 6.0, static_cast<double>( bestDistance ) ) / 6.0;
        double boost = 1.10 + proximity * 0.10;
        if ( follow.kind == recentCouncilV18Plan.kind ) {
            boost *= 1.85;
        }
        follow.score *= boost;
        follow.title = "FOLLOW | " + follow.title;
        follow.rationale = "FOLLOW. Same hero and strategic line still make sense. | " + follow.rationale;
    }

    void rememberCouncilV18Plan( const StrategicAdvice & advice, Kingdom & kingdom )
    {
        recentCouncilV18Plan = {};
        if ( advice.heroId < 0 || !councilV18ContinuationAction( advice ) ) {
            return;
        }

        Heroes * hero = world.GetHeroes( advice.heroId );
        if ( hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() ) {
            return;
        }

        const CouncilV9Intent intent = councilV9IntentForAdvice( advice );
        const CouncilV18PlanFamily family = councilV18PlanFamilyForKind( advice.kind );
        if ( intent == CouncilV9Intent::NONE || family == CouncilV18PlanFamily::NONE ) {
            return;
        }

        recentCouncilV18Plan.heroId = advice.heroId;
        recentCouncilV18Plan.anchorIndex = hero->GetIndex();
        recentCouncilV18Plan.kind = advice.kind;
        recentCouncilV18Plan.intent = intent;
        recentCouncilV18Plan.family = family;
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV18( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        // Apply exactly one verified continuation after live validity/risk checks, but before
        // diversity, opportunity-cost and resource planning can shape the final shortlist.
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

    void showRpgStrategicAdvisorV18()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV18( kingdom, suppressedOrders );
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
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV18( kingdom, suppressedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV18
