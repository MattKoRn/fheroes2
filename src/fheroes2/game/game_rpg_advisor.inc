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

            // Keep a genuinely beneficial fallback eligible when the map simply has fewer than five strong plays.
            // It will sit at the bottom of the council rather than disappearing and leaving an empty seat.
            if ( originalScore >= 3200.0 && advice.score < 2600.0 ) {
                advice.score = 2600.0;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV4( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
    {
        const CouncilContext context = buildCouncilContext( kingdom );
        std::vector<StrategicAdvice> rawCandidates = gatherCouncilCandidates( kingdom, context );

        // Always make live frontier/staging alternatives available to the comparison pass. They remain heavily
        // opportunity-costed for the main hero, so they only surface when they are genuinely among the best uses left.
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

<<<<<<< HEAD
    const char * councilPriorityLabel( const double score )
    {
        if ( score >= 17500.0 ) return "IMPERATIVE";
        if ( score >= 11500.0 ) return "RECOMMENDED";
        if ( score >= 7000.0 ) return "SOLID";
        return "SITUATIONAL";
    }

    int selectStrategicAdvice( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
    {
        if ( advices.empty() ) {
            return -1;
        }

        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();
        fheroes2::StandardWindow window( 604, 410, true, display );
        const fheroes2::Rect area = window.activeArea();
        const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();
        std::array<fheroes2::Rect, strategicAdviceCount> rows{};
        fheroes2::Button closeButton;
        window.renderButton( closeButton, isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD, 0, 1, { 14, 8 },
                             fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );

        const size_t visibleCount = std::min( strategicAdviceCount, advices.size() );
        size_t hoverIndex = strategicAdviceCount;
        bool redraw = true;
        LocalEvent & event = LocalEvent::Get();

        while ( event.HandleEvents() ) {
            size_t newHoverIndex = strategicAdviceCount;
            for ( size_t index = 0; index < visibleCount; ++index ) {
                if ( event.isMouseCursorPosInArea( rows[index] ) ) {
                    newHoverIndex = index;
                    break;
                }
            }
            if ( newHoverIndex != hoverIndex ) {
                hoverIndex = newHoverIndex;
                redraw = true;
            }

            if ( redraw ) {
                window.render();
                window.applyGemDecoratedCorners();

                const fheroes2::Rect banner{ area.x + 8, area.y + 4, area.width - 16, 50 };
                window.applyTextBackgroundShading( banner );
                const fheroes2::Text title( "THE KING'S WAR COUNCIL", fheroes2::FontType::normalYellow() );
                title.draw( banner.x + 9, banner.y + 4, banner.width - 18, display );

                const CouncilContext context = buildCouncilContext( kingdom );
                std::string status = std::to_string( context.movableHeroes ) + " heroes can still act";
                if ( const Heroes * mainHero = world.GetHeroes( context.mainHeroId ) ) {
                    status += "  |  Main army: " + mainHero->GetName();
                }
                fheroes2::Text subtitle( status, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 18 );
                subtitle.draw( banner.x + 9, banner.y + 29, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( councilReport, fheroes2::FontType::smallYellow() );
                    report.fitToOneRow( area.width - 26 );
                    report.draw( area.x + 13, area.y + 58, display );
                }

                for ( size_t index = 0; index < strategicAdviceCount; ++index ) {
                    rows[index] = { area.x + 12, area.y + 75 + static_cast<int32_t>( index ) * 54, area.width - 24, 48 };
                    window.applyTextBackgroundShading( rows[index] );

                    if ( index >= visibleCount ) {
                        const fheroes2::Text noFiller( "No additional advice is worth spending resources or movement on yet.", fheroes2::FontType::smallWhite() );
                        noFiller.draw( rows[index].x + 10, rows[index].y + 16, rows[index].width - 20, display );
                        continue;
                    }

                    const StrategicAdvice & advice = advices[index];
                    int32_t textX = rows[index].x + 9;
                    int32_t textWidth = rows[index].width - 18;
                    if ( const Heroes * hero = world.GetHeroes( advice.heroId ) ) {
                        hero->PortraitRedraw( rows[index].x + 6, rows[index].y + 5, PORT_SMALL, display );
                        textX = rows[index].x + 52;
                        textWidth = rows[index].width - 61;
                    }

                    std::string headingText = ( hoverIndex == index ? "> " : "" );
                    headingText += std::to_string( index + 1 ) + ". " + councilPriorityLabel( advice.score ) + "  " + advice.title;
                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 5, display );

                    fheroes2::Text reason( "Counsel: " + advice.rationale, fheroes2::FontType::smallWhite() );
                    reason.fitToOneRow( textWidth );
                    reason.draw( textX, rows[index].y + 25, display );
                }

                fheroes2::Text hint( "Choose the counsel that fits your plan. Esc returns control; the council never ends the turn.", fheroes2::FontType::smallWhite() );
                hint.fitToOneRow( area.width - 132 );
                hint.draw( area.x + 12, area.y + area.height - 21, display );
                closeButton.draw();
                display.render( window.totalArea() );
                redraw = false;
            }

            closeButton.drawOnState( event.isMouseLeftButtonPressedAndHeldInArea( closeButton.area() ) );
            if ( Game::HotKeyCloseWindow() || event.MouseClickLeft( closeButton.area() ) ) {
                return -1;
            }
            for ( size_t index = 0; index < visibleCount; ++index ) {
                if ( event.MouseClickLeft( rows[index] ) ) {
                    return static_cast<int>( index );
                }
                const fheroes2::Key numberKey = static_cast<fheroes2::Key>( static_cast<int32_t>( fheroes2::Key::KEY_1 ) + static_cast<int32_t>( index ) );
                if ( event.isKeyPressed( numberKey ) ) {
                    return static_cast<int>( index );
                }
            }
        }
        return -1;
    }

    void showRpgStrategicAdvisor()
=======
    void showRpgStrategicAdvisorV4()
>>>>>>> fcc81fc2a9152933cb0181d7e3762e359bdaf22b
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

// game_hotkeys.cpp includes this file before the F7 call site. Route that existing call to v4
// without changing the hotkey plumbing or exposing another global symbol.
#define showRpgStrategicAdvisor showRpgStrategicAdvisorV4
