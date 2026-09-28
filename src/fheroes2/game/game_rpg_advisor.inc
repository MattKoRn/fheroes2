// War Council v6: kingdom-wide objective coordination.
// v5 reasons through short sequences. This layer assigns contested objectives to the hero who
// can exploit them best and spreads the visible council across distinct useful work.
#include "game_rpg_advisor_v5.inc"
#undef showRpgStrategicAdvisor

namespace
{
    struct CouncilObjectiveAssignment
    {
        int32_t heroId{ -1 };
        double fit{ 0.0 };
        size_t contenders{ 0 };
    };

    bool isCouncilDecisiveObjective( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::DEFENSE:
            return true;
        default:
            return false;
        }
    }

    bool isCouncilSupportObjective( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
            return true;
        default:
            return false;
        }
    }

    bool isCouncilIncomeObjective( const StrategicAdviceKind kind )
    {
        return kind == StrategicAdviceKind::MINE_CAPTURE || kind == StrategicAdviceKind::ECONOMIC_EXPANSION;
    }

    bool isCouncilReservableObjective( const StrategicAdvice & advice )
    {
        return advice.action == AdviceAction::MOVE_HERO && advice.heroId >= 0 && advice.targetIndex >= 0 && advice.targetObjectType != MP2::OBJ_NONE;
    }

    double councilObjectiveFit( const StrategicAdvice & advice, const CouncilContext & context )
    {
        const Heroes * hero = world.GetHeroes( advice.heroId );
        if ( hero == nullptr || !hero->isActive() ) {
            return 0.0;
        }

        const double strength = std::max( 1.0, hero->GetArmy().GetStrength() );
        const double relativeStrength = strength / std::max( 1.0, context.strongestHeroStrength );
        const bool mainHero = hero->GetID() == context.mainHeroId;
        const double movementUse = hero->GetMovePoints() > 0
                                       ? std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 )
                                       : 1.0;

        double fit = advice.score * ( 1.18 - movementUse * 0.18 );

        if ( isCouncilDecisiveObjective( advice.kind ) ) {
            fit *= mainHero ? 1.24 : 0.94 + relativeStrength * 0.12;
        }
        else if ( isCouncilSupportObjective( advice.kind ) ) {
            fit *= mainHero ? 0.56 : ( relativeStrength < 0.72 ? 1.28 : 1.04 );
        }
        else if ( isCouncilIncomeObjective( advice.kind ) ) {
            fit *= mainHero ? 0.92 : ( relativeStrength < 0.78 ? 1.14 : 1.02 );
        }
        else if ( advice.kind == StrategicAdviceKind::TOWN_RESUPPLY ) {
            fit *= relativeStrength < 0.72 ? 1.28 : 0.90;
        }
        else if ( advice.kind == StrategicAdviceKind::GARRISON_REINFORCEMENT ) {
            fit *= mainHero ? 0.78 : 1.12;
        }

        const Maps::Tile & targetTile = world.getTile( advice.targetIndex );
        const double defenders = defenderStrengthForAdvice( targetTile, *hero );
        if ( defenders > 0.0 ) {
            const double combatEdge = strength / defenders;
            if ( combatEdge < 1.10 ) {
                fit *= 0.34;
            }
            else {
                fit *= std::clamp( 0.88 + combatEdge * 0.14, 1.02, 1.34 );
            }
        }

        return fit;
    }

    std::map<int32_t, CouncilObjectiveAssignment> buildCouncilObjectiveAssignments( const std::vector<StrategicAdvice> & candidates,
                                                                                    const CouncilContext & context )
    {
        std::map<int32_t, std::map<int32_t, double>> heroFitByObjective;
        for ( const StrategicAdvice & advice : candidates ) {
            if ( !isCouncilReservableObjective( advice ) ) {
                continue;
            }

            double & fit = heroFitByObjective[advice.targetIndex][advice.heroId];
            fit = std::max( fit, councilObjectiveFit( advice, context ) );
        }

        std::map<int32_t, CouncilObjectiveAssignment> assignments;
        for ( const auto & objective : heroFitByObjective ) {
            if ( objective.second.size() < 2 ) {
                continue;
            }

            CouncilObjectiveAssignment assignment;
            assignment.contenders = objective.second.size();
            for ( const auto & heroFit : objective.second ) {
                if ( heroFit.second > assignment.fit ) {
                    assignment.heroId = heroFit.first;
                    assignment.fit = heroFit.second;
                }
            }
            assignments.emplace( objective.first, assignment );
        }

        return assignments;
    }

    void applyCouncilObjectiveReservations( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        const std::map<int32_t, CouncilObjectiveAssignment> assignments = buildCouncilObjectiveAssignments( candidates, context );

        for ( StrategicAdvice & advice : candidates ) {
            if ( !isCouncilReservableObjective( advice ) ) {
                continue;
            }

            const auto assignmentIt = assignments.find( advice.targetIndex );
            if ( assignmentIt == assignments.end() ) {
                continue;
            }

            const CouncilObjectiveAssignment & assignment = assignmentIt->second;
            const Heroes * assignedHero = world.GetHeroes( assignment.heroId );
            const Heroes * candidateHero = world.GetHeroes( advice.heroId );
            const std::string assignedName = assignedHero != nullptr ? assignedHero->GetName() : "the better-suited hero";

            if ( advice.heroId == assignment.heroId ) {
                advice.score *= 1.11;
                advice.rationale = "Reserved for " + assignedName + ": best fit among " + std::to_string( assignment.contenders ) + " heroes. | " + advice.rationale;
            }
            else {
                advice.score *= 0.38;
                const std::string candidateName = candidateHero != nullptr ? candidateHero->GetName() : "This hero";
                advice.rationale = "Reserved for " + assignedName + "; redirect " + candidateName + " to a distinct profitable task. | " + advice.rationale;
            }
        }
    }

    std::vector<StrategicAdvice> chooseKingdomCouncilAdvice( std::vector<StrategicAdvice> candidates )
    {
        std::sort( candidates.begin(), candidates.end(), []( const StrategicAdvice & left, const StrategicAdvice & right ) {
            if ( left.score != right.score ) {
                return left.score > right.score;
            }
            return left.distance < right.distance;
        } );

        std::vector<StrategicAdvice> chosen;
        chosen.reserve( strategicAdviceCount );
        std::set<std::string> identities;
        std::set<int32_t> movementTargets;
        std::map<int32_t, size_t> heroActions;
        std::map<int32_t, size_t> townActions;

        const auto tryChoose = [&]( const StrategicAdvice & advice, const size_t heroLimit, const size_t townLimit ) {
            if ( chosen.size() >= strategicAdviceCount || !advice.reachableThisTurn || advice.score < 2500.0 ) {
                return;
            }

            const std::string identity = adviceIdentity( advice );
            if ( identities.count( identity ) > 0 ) {
                return;
            }

            const bool heroAction = advice.action == AdviceAction::MOVE_HERO || advice.action == AdviceAction::HERO_EXCHANGE;
            const bool townAction = advice.action == AdviceAction::RECRUIT_CREATURES || advice.action == AdviceAction::BUILD_STRUCTURE
                                    || advice.action == AdviceAction::TOWN_ARMY_MANAGEMENT;

            if ( heroAction && advice.heroId >= 0 && heroActions[advice.heroId] >= heroLimit ) {
                return;
            }
            if ( townAction && advice.targetIndex >= 0 && townActions[advice.targetIndex] >= townLimit ) {
                return;
            }
            if ( advice.action == AdviceAction::MOVE_HERO && advice.targetIndex >= 0 && movementTargets.count( advice.targetIndex ) > 0 ) {
                return;
            }

            identities.insert( identity );
            if ( advice.action == AdviceAction::MOVE_HERO && advice.targetIndex >= 0 ) {
                movementTargets.insert( advice.targetIndex );
            }
            if ( heroAction && advice.heroId >= 0 ) {
                ++heroActions[advice.heroId];
            }
            if ( townAction && advice.targetIndex >= 0 ) {
                ++townActions[advice.targetIndex];
            }
            chosen.emplace_back( advice );
        };

        // First pass spreads useful work across different heroes and towns.
        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, 1, 1 );
        }
        // Second pass allows a second genuinely strong option for the same owner when needed.
        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, 2, 2 );
        }
        // Final pass only relaxes owner distribution; objective uniqueness remains strict.
        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, strategicAdviceCount, strategicAdviceCount );
        }

        return chosen;
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV6( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        applyCouncilSequencePlanning( candidates );
        applyCouncilResourceReservation( candidates, context );
        applyCouncilObjectiveReservations( candidates, context );
        // Recompute per-hero opportunity cost after contested targets have been assigned. This promotes the
        // redirected hero's best distinct task instead of leaving its old duplicate chase artificially dominant.
        applyAlternativeComparison( candidates );

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );

        return chooseKingdomCouncilAdvice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV6()
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

        while ( hasMovableCouncilHeroes( kingdom ) ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV6( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   "The council assigned every worthwhile objective it could find, but no beneficial executable order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdvice( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const AdviceExecutionResult execution = executeStrategicAdvice( kingdom, *player, selected );
            councilReport = execution.message;
            if ( !execution.success ) {
                failedOrders.insert( adviceIdentity( selected ) );
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
                continue;
            }

            failedOrders.clear();
            if ( selected.heroId >= 0 ) {
                recentCouncilHeroKinds[selected.heroId] = selected.kind;
            }
            ++recentCouncilKinds[selected.kind];
            if ( selected.action == AdviceAction::MOVE_HERO && selected.targetIndex >= 0 ) {
                recentCouncilMovementTargets.insert( selected.targetIndex );
            }
        }

        fheroes2::showStandardTextMessage( "War Council",
                                           "Every active hero has spent the movement available to them. The council returns control without ending your turn.",
                                           Dialog::OK );
    }
}

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV6
