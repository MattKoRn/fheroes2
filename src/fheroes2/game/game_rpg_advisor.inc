// War Council v7: commitment-aware strategic continuity.
// v6 coordinates objectives across heroes. This layer remembers each hero's strategic intent
// during the active council session so strong plans are followed through instead of being
// abandoned for low-value errands after every score reshuffle.
#include "game_rpg_advisor_v6.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilIntent
    {
        NONE,
        DECISIVE,
        ECONOMY,
        DEVELOPMENT,
        POSITIONING,
        LOGISTICS,
        ERRAND
    };

    std::map<int32_t, CouncilIntent> recentCouncilHeroIntents;

    CouncilIntent councilIntentForAdvice( const StrategicAdvice & advice )
    {
        switch ( advice.kind ) {
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::DEFENSE:
            return CouncilIntent::DECISIVE;

        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
            return CouncilIntent::ECONOMY;

        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
            return CouncilIntent::DEVELOPMENT;

        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
            return CouncilIntent::POSITIONING;

        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
        case StrategicAdviceKind::TOWN_FEED_GARRISON:
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
        case StrategicAdviceKind::HERO_FAST_RESERVE:
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
            return CouncilIntent::LOGISTICS;

        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
            return CouncilIntent::ERRAND;

        default:
            return CouncilIntent::NONE;
        }
    }

    int councilIntentPriority( const CouncilIntent intent )
    {
        switch ( intent ) {
        case CouncilIntent::DECISIVE:
            return 5;
        case CouncilIntent::ECONOMY:
        case CouncilIntent::DEVELOPMENT:
            return 4;
        case CouncilIntent::POSITIONING:
            return 3;
        case CouncilIntent::LOGISTICS:
            return 2;
        case CouncilIntent::ERRAND:
            return 1;
        default:
            return 0;
        }
    }

    bool isCouncilDurableIntent( const CouncilIntent intent )
    {
        return intent == CouncilIntent::DECISIVE || intent == CouncilIntent::ECONOMY || intent == CouncilIntent::DEVELOPMENT;
    }

    void applyCouncilCommitmentContinuity( std::vector<StrategicAdvice> & candidates )
    {
        for ( StrategicAdvice & advice : candidates ) {
            if ( advice.heroId < 0 || advice.action != AdviceAction::MOVE_HERO ) {
                continue;
            }

            const auto previousIt = recentCouncilHeroIntents.find( advice.heroId );
            if ( previousIt == recentCouncilHeroIntents.end() ) {
                continue;
            }

            const CouncilIntent previous = previousIt->second;
            const CouncilIntent next = councilIntentForAdvice( advice );
            if ( previous == CouncilIntent::NONE || next == CouncilIntent::NONE ) {
                continue;
            }

            if ( previous == next ) {
                if ( isCouncilDurableIntent( next ) ) {
                    advice.score *= 1.05;
                }
                continue;
            }

            if ( previous == CouncilIntent::POSITIONING && isCouncilDurableIntent( next ) ) {
                advice.score *= 1.12;
                advice.rationale = "Follows through on the staged plan. | " + advice.rationale;
                continue;
            }

            if ( previous == CouncilIntent::ERRAND && isCouncilDurableIntent( next ) ) {
                advice.score *= 1.08;
                advice.rationale = "Upgrades this hero to a more important objective. | " + advice.rationale;
                continue;
            }

            if ( previous == CouncilIntent::DECISIVE && next == CouncilIntent::ERRAND ) {
                advice.score *= 0.55;
                advice.rationale = "Avoid abandoning decisive work for a minor errand. | " + advice.rationale;
                continue;
            }

            if ( ( previous == CouncilIntent::ECONOMY || previous == CouncilIntent::DEVELOPMENT ) && next == CouncilIntent::ERRAND ) {
                advice.score *= 0.68;
                advice.rationale = "Keep the stronger strategic line unless this errand becomes clearly better. | " + advice.rationale;
                continue;
            }

            const int previousPriority = councilIntentPriority( previous );
            const int nextPriority = councilIntentPriority( next );
            if ( nextPriority >= previousPriority + 2 ) {
                advice.score *= 1.05;
            }
            else if ( previousPriority >= nextPriority + 2 ) {
                advice.score *= 0.78;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV7( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        applyCouncilCommitmentContinuity( candidates );
        applyCouncilObjectiveReservations( candidates, context );
        // Recompute per-owner opportunity cost after continuity and kingdom-wide reservations have changed priorities.
        applyAlternativeComparison( candidates );

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );

        return chooseKingdomCouncilAdvice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV7()
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
        recentCouncilHeroIntents.clear();

        while ( hasMovableCouncilHeroes( kingdom ) ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV7( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   "The council found no worthwhile order that improves the current plan. Your turn stays active.",
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
                const CouncilIntent intent = councilIntentForAdvice( selected );
                if ( intent != CouncilIntent::NONE ) {
                    recentCouncilHeroIntents[selected.heroId] = intent;
                }
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV7
