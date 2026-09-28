// War Council v4: compare advice against the opportunity it replaces.
// v3 owns the strategic scoring and HOMM2-style presentation. This layer makes the council
// compare competing uses of each hero and town before it recommends anything.
#include "game_rpg_advisor_v3.inc"

namespace
{
    using CouncilDecisionOwner = std::pair<int32_t, int32_t>;

    struct CouncilAlternativeBenchmark
    {
        double best{ 0.0 };
        double second{ 0.0 };
        std::string bestIdentity;
    };

    CouncilDecisionOwner councilDecisionOwner( const StrategicAdvice & advice )
    {
        switch ( advice.action ) {
        case AdviceAction::MOVE_HERO:
        case AdviceAction::HERO_EXCHANGE:
            if ( advice.heroId >= 0 ) {
                return { 0, advice.heroId };
            }
            break;
        case AdviceAction::RECRUIT_CREATURES:
        case AdviceAction::BUILD_STRUCTURE:
        case AdviceAction::TOWN_ARMY_MANAGEMENT:
            if ( advice.targetIndex >= 0 ) {
                return { 1, advice.targetIndex };
            }
            break;
        default:
            break;
        }

        return { 2, advice.targetIndex };
    }

    std::string councilDecisionOwnerName( const StrategicAdvice & advice )
    {
        const CouncilDecisionOwner owner = councilDecisionOwner( advice );
        if ( owner.first == 0 ) {
            if ( const Heroes * hero = world.GetHeroes( owner.second ) ) {
                return hero->GetName();
            }
            return "this hero";
        }

        if ( owner.first == 1 && owner.second >= 0 ) {
            if ( const Castle * castle = world.getCastleEntrance( Maps::GetPoint( owner.second ) ) ) {
                return castle->GetName();
            }
            return "this town";
        }

        return "this position";
    }

    std::map<CouncilDecisionOwner, CouncilAlternativeBenchmark> buildAlternativeBenchmarks( const std::vector<StrategicAdvice> & candidates )
    {
        std::map<CouncilDecisionOwner, std::map<std::string, double>> uniqueScores;
        for ( const StrategicAdvice & advice : candidates ) {
            const std::string identity = adviceIdentity( advice );
            double & stored = uniqueScores[councilDecisionOwner( advice )][identity];
            stored = std::max( stored, advice.score );
        }

        std::map<CouncilDecisionOwner, CouncilAlternativeBenchmark> benchmarks;
        for ( const auto & ownerScores : uniqueScores ) {
            CouncilAlternativeBenchmark benchmark;
            for ( const auto & candidate : ownerScores.second ) {
                if ( candidate.second > benchmark.best ) {
                    benchmark.second = benchmark.best;
                    benchmark.best = candidate.second;
                    benchmark.bestIdentity = candidate.first;
                }
                else if ( candidate.second > benchmark.second ) {
                    benchmark.second = candidate.second;
                }
            }
            benchmarks.emplace( ownerScores.first, std::move( benchmark ) );
        }

        return benchmarks;
    }

    void applyAlternativeComparison( std::vector<StrategicAdvice> & candidates )
    {
        const auto benchmarks = buildAlternativeBenchmarks( candidates );

        for ( StrategicAdvice & advice : candidates ) {
            const auto benchmarkIt = benchmarks.find( councilDecisionOwner( advice ) );
            if ( benchmarkIt == benchmarks.end() || benchmarkIt->second.best <= 0.0 ) {
                continue;
            }

            const CouncilAlternativeBenchmark & benchmark = benchmarkIt->second;
            const std::string identity = adviceIdentity( advice );
            const double originalScore = advice.score;
            const double ratioToBest = std::clamp( originalScore / benchmark.best, 0.0, 1.0 );
            const std::string ownerName = councilDecisionOwnerName( advice );

            if ( identity == benchmark.bestIdentity ) {
                if ( benchmark.second <= 0.0 ) {
                    advice.score *= 1.07;
                    advice.rationale += " Best available use of " + ownerName + " right now.";
                }
                else {
                    const double margin = std::clamp( ( benchmark.best - benchmark.second ) / benchmark.best, 0.0, 1.0 );
                    advice.score *= 1.07 + std::min( 0.10, margin * 0.30 );
                    const int marginPercent = static_cast<int>( margin * 100.0 );
                    if ( margin >= 0.22 ) {
                        advice.rationale += " Best use of " + ownerName + "; it beats the next option by about " + std::to_string( marginPercent ) + "%.";
                    }
                    else if ( margin >= 0.08 ) {
                        advice.rationale += " Best use of " + ownerName + "; it has a meaningful edge over the next option.";
                    }
                    else {
                        advice.rationale += " Best use of " + ownerName + ", though another option is close.";
                    }
                }
                continue;
            }

            if ( ratioToBest >= 0.90 ) {
                advice.score *= 0.94;
                advice.rationale += " Close alternative for " + ownerName + "; choose it only if it fits your plan better.";
            }
            else if ( ratioToBest >= 0.72 ) {
                advice.score *= 0.74;
                advice.rationale += " Viable, but " + ownerName + " has a stronger competing play.";
            }
            else {
                advice.score *= 0.46;
                advice.rationale += " High opportunity cost: " + ownerName + " has a much stronger use available.";
            }

            if ( originalScore >= 3200.0 && advice.score < 2600.0 ) {
                advice.score = 2600.0;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV4( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        applyAlternativeComparison( candidates );
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );

        return chooseCouncilAdvice( std::move( candidates ) );
    }

    void showRpgStrategicAdvisorV4()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV4( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   "The council compared every legal opportunity it could find and none is worth forcing. Your turn remains active.",
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV4
