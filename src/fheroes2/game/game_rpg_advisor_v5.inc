// War Council v5: sequence-aware strategic advice.
// v4 compares competing uses of heroes and towns. This layer evaluates what each order enables
// next, protects scarce kingdom resources for stronger spending, and penalizes moves that strand tempo.
#include "game_rpg_advisor_v4.inc"
#undef showRpgStrategicAdvisor

namespace
{
    struct CouncilFollowUp
    {
        const StrategicAdvice * advice{ nullptr };
        double value{ 0.0 };
        uint32_t proximity{ std::numeric_limits<uint32_t>::max() };
        bool likelySameTurn{ false };
    };

    bool isCouncilResourceSpend( const StrategicAdvice & advice )
    {
        return advice.action == AdviceAction::RECRUIT_CREATURES || advice.action == AdviceAction::BUILD_STRUCTURE;
    }

    std::string councilFollowUpPhrase( const StrategicAdvice & advice )
    {
        switch ( advice.kind ) {
        case StrategicAdviceKind::SIEGE: return "press the nearby enemy town";
        case StrategicAdviceKind::INTERCEPTION: return "intercept the nearby enemy hero";
        case StrategicAdviceKind::MONSTER_HUNT: return "take the nearby favorable fight";
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION: return "secure the next income source";
        case StrategicAdviceKind::ARTIFACT_RECOVERY: return "recover the nearby artifact";
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::SUPPLY_SWEEP: return "collect the nearby value";
        case StrategicAdviceKind::HERO_TRAINING: return "take the nearby permanent hero upgrade";
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT: return "take the nearby magic upgrade";
        case StrategicAdviceKind::TOWN_RESUPPLY: return "resupply before the next field objective";
        case StrategicAdviceKind::TOWN_PULL_STRONGEST: return "pull the recruited troops onto the visiting hero";
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
        case StrategicAdviceKind::HERO_FULL_EXCHANGE: return "continue with the newly concentrated main army";
        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE: return "continue toward the staged objective";
        default: return advice.title;
        }
    }

    int32_t councilFollowUpHeroId( const StrategicAdvice & advice )
    {
        if ( advice.heroId >= 0 ) {
            return advice.heroId;
        }
        if ( advice.targetIndex < 0 || ( advice.action != AdviceAction::TOWN_ARMY_MANAGEMENT && advice.action != AdviceAction::RECRUIT_CREATURES ) ) {
            return -1;
        }

        const Castle * castle = world.getCastleEntrance( Maps::GetPoint( advice.targetIndex ) );
        const Heroes * hero = castle != nullptr ? castle->GetHero() : nullptr;
        return hero != nullptr && hero->isActive() ? hero->GetID() : -1;
    }

    CouncilFollowUp findCouncilFollowUp( const StrategicAdvice & advice, const std::vector<StrategicAdvice> & candidates )
    {
        CouncilFollowUp best;
        const std::string identity = adviceIdentity( advice );

        if ( advice.action == AdviceAction::RECRUIT_CREATURES && advice.targetIndex >= 0 ) {
            for ( const StrategicAdvice & other : candidates ) {
                if ( other.targetIndex != advice.targetIndex || other.kind != StrategicAdviceKind::TOWN_PULL_STRONGEST || adviceIdentity( other ) == identity ) {
                    continue;
                }
                const double value = other.score * 1.22;
                if ( value > best.value ) {
                    best = { &other, value, 0, true };
                }
            }
        }

        const int32_t heroId = councilFollowUpHeroId( advice );
        if ( heroId < 0 ) {
            return best;
        }
        const Heroes * hero = world.GetHeroes( heroId );
        if ( hero == nullptr || !hero->isActive() ) {
            return best;
        }

        double remainingFraction = 1.0;
        if ( advice.action == AdviceAction::MOVE_HERO ) {
            if ( hero->GetMovePoints() == 0 ) {
                return best;
            }
            remainingFraction = std::clamp( 1.0 - static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
        }

        for ( const StrategicAdvice & other : candidates ) {
            if ( other.action != AdviceAction::MOVE_HERO || other.heroId != heroId || adviceIdentity( other ) == identity || other.targetIndex < 0 ) {
                continue;
            }

            uint32_t proximity = 0;
            if ( advice.action == AdviceAction::MOVE_HERO ) {
                if ( advice.targetIndex < 0 || remainingFraction < 0.12 ) {
                    continue;
                }
                proximity = Maps::GetApproximateDistance( advice.targetIndex, other.targetIndex );
                const uint32_t usefulRadius = 2 + static_cast<uint32_t>( remainingFraction * 7.0 );
                if ( proximity > usefulRadius ) {
                    continue;
                }
            }
            else {
                proximity = Maps::GetApproximateDistance( hero->GetIndex(), other.targetIndex );
            }

            double value = other.score / ( 1.0 + static_cast<double>( proximity ) * 0.14 );
            if ( isPremiumMovementKind( other.kind ) ) {
                value *= 1.12;
            }

            const bool likelySameTurn = advice.action != AdviceAction::MOVE_HERO || ( remainingFraction >= 0.34 && proximity <= 3 );
            if ( value > best.value ) {
                best = { &other, value, proximity, likelySameTurn };
            }
        }

        return best;
    }

    void applyCouncilSequencePlanning( std::vector<StrategicAdvice> & candidates )
    {
        if ( candidates.empty() ) {
            return;
        }

        // Freeze the board before applying sequence bonuses so candidate iteration order cannot affect the result.
        const std::vector<StrategicAdvice> snapshot = candidates;
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            StrategicAdvice & advice = candidates[index];
            const StrategicAdvice & source = snapshot[index];
            const CouncilFollowUp followUp = findCouncilFollowUp( source, snapshot );

            if ( followUp.advice != nullptr && followUp.value >= 3200.0 ) {
                const double sequenceRatio = std::clamp( followUp.value / std::max( 3200.0, source.score ), 0.0, 1.35 );
                advice.score *= 1.04 + std::min( 0.16, sequenceRatio * 0.10 );
                const std::string phrase = councilFollowUpPhrase( *followUp.advice );
                advice.rationale = std::string( followUp.likelySameTurn ? "Then " : "Sets up " ) + phrase + ". | " + advice.rationale;
                continue;
            }

            if ( source.action != AdviceAction::MOVE_HERO ) {
                continue;
            }
            const Heroes * hero = world.GetHeroes( source.heroId );
            if ( hero == nullptr || hero->GetMovePoints() == 0 ) {
                continue;
            }

            const double movementUse = std::clamp( static_cast<double>( source.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
            const bool weakContinuationKind = isErrandMovementKind( source.kind ) || source.targetObjectType == MP2::OBJ_NONE;
            if ( movementUse >= 0.72 && weakContinuationKind && !isPremiumMovementKind( source.kind ) ) {
                advice.score *= 0.78;
                advice.rationale = "Limited continuation after this move. | " + advice.rationale;
            }
        }
    }

    void applyCouncilResourceReservation( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        size_t bestSpendIndex = candidates.size();
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            if ( !isCouncilResourceSpend( candidates[index] ) ) {
                continue;
            }
            if ( bestSpendIndex == candidates.size() || candidates[index].score > candidates[bestSpendIndex].score ) {
                bestSpendIndex = index;
            }
        }
        if ( bestSpendIndex == candidates.size() || candidates[bestSpendIndex].score <= 0.0 ) {
            return;
        }

        const double bestScore = candidates[bestSpendIndex].score;
        const std::string bestIdentity = adviceIdentity( candidates[bestSpendIndex] );
        const std::string bestTitle = candidates[bestSpendIndex].title;
        const bool scarce = context.lowGold || context.lowMaterials;

        for ( StrategicAdvice & advice : candidates ) {
            if ( !isCouncilResourceSpend( advice ) || adviceIdentity( advice ) == bestIdentity ) {
                continue;
            }

            const double ratio = std::clamp( advice.score / bestScore, 0.0, 1.0 );
            if ( scarce && ratio < 0.86 ) {
                advice.score *= 0.54 + ratio * 0.34;
                advice.rationale = "Save scarce resources for " + bestTitle + ". | " + advice.rationale;
            }
            else if ( ratio < 0.62 ) {
                advice.score *= 0.84;
                advice.rationale = "Lower-impact spend than " + bestTitle + ". | " + advice.rationale;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV5( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        applyAlternativeComparison( candidates );

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );
        return chooseCouncilAdvice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV5()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV5( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   "The council found no beneficial sequence worth forcing from the current position. Your turn remains active.",
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV5
