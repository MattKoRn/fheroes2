// War Council v14: deterministic, resource-aware shortlist with compact decision signals.
// v13 owns native popup sequencing and declined-object memory. This layer stabilizes equal-score
// ordering, protects scarce resources from marginal spending, trims same-owner echoes and adds
// SAVE / READY signals inside the existing compact War Council window.
#include "game_rpg_advisor_v13.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV14Signal : uint8_t
    {
        BASE,
        SAVE,
        READY
    };

    struct CouncilV14ResourceState
    {
        double bestSpend{ 0.0 };
        double bestNoSpend{ 0.0 };
        std::string bestSpendIdentity;
    };

    CouncilV14ResourceState buildCouncilV14ResourceState( const std::vector<StrategicAdvice> & candidates )
    {
        CouncilV14ResourceState state;
        for ( const StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                continue;
            }

            if ( isCouncilResourceSpend( advice ) ) {
                if ( advice.score > state.bestSpend ) {
                    state.bestSpend = advice.score;
                    state.bestSpendIdentity = adviceIdentity( advice );
                }
            }
            else {
                state.bestNoSpend = std::max( state.bestNoSpend, advice.score );
            }
        }
        return state;
    }

    void applyCouncilV14ResourceJudgment( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        const bool scarce = context.lowGold || context.lowMaterials;
        if ( !scarce ) {
            return;
        }

        const CouncilV14ResourceState state = buildCouncilV14ResourceState( candidates );
        for ( StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                continue;
            }

            if ( isCouncilResourceSpend( advice ) ) {
                const bool bestSpend = adviceIdentity( advice ) == state.bestSpendIdentity;
                if ( state.bestNoSpend >= advice.score * 0.82 ) {
                    advice.score *= bestSpend ? 0.82 : 0.70;
                    advice.rationale = "Resources tight; stronger no-spend play exists. | " + advice.rationale;
                }
                else if ( bestSpend && advice.score >= std::max( 5200.0, state.bestNoSpend * 1.20 ) ) {
                    advice.score *= 1.05;
                    advice.rationale = "Worth spending now; payoff clears the reserve bar. | " + advice.rationale;
                }
                continue;
            }

            const CouncilV9Type type = councilV9TypeForKind( advice.kind );
            const bool urgent = type == CouncilV9Type::DEFEND && advice.reachableThisTurn;
            const bool decisiveCombat = isCouncilV9CombatAdvice( advice.kind ) && advice.reachableThisTurn
                                        && councilV11RiskForAdvice( advice ) == CouncilV9Risk::SOLID;
            if ( urgent || decisiveCombat ) {
                continue;
            }

            if ( advice.score >= 5200.0 && ( state.bestSpend <= 0.0 || advice.score >= state.bestSpend * 0.88 ) ) {
                advice.score *= 1.04;
                advice.rationale = "Preserves scarce resources without giving up much value. | " + advice.rationale;
            }
        }
    }

    int councilV14UrgencyRank( const StrategicAdvice & advice )
    {
        const CouncilV9Type type = councilV9TypeForKind( advice.kind );
        if ( type == CouncilV9Type::DEFEND && advice.reachableThisTurn ) {
            return 5;
        }
        if ( isCouncilV9CombatAdvice( advice.kind ) && advice.reachableThisTurn ) {
            const CouncilV9Risk risk = councilV11RiskForAdvice( advice );
            if ( risk == CouncilV9Risk::SOLID ) {
                return 4;
            }
            if ( risk == CouncilV9Risk::CHECK ) {
                return 2;
            }
            return 0;
        }
        if ( type == CouncilV9Type::RECOVER ) {
            return 3;
        }
        if ( advice.reachableThisTurn ) {
            return 1;
        }
        return 0;
    }

    int64_t councilV14ScoreBucket( const StrategicAdvice & advice )
    {
        return static_cast<int64_t>( std::llround( advice.score * 100.0 ) );
    }

    bool councilV14DeterministicLess( const StrategicAdvice & left, const StrategicAdvice & right )
    {
        const int64_t leftScore = councilV14ScoreBucket( left );
        const int64_t rightScore = councilV14ScoreBucket( right );
        if ( leftScore != rightScore ) {
            return leftScore > rightScore;
        }

        const int leftUrgency = councilV14UrgencyRank( left );
        const int rightUrgency = councilV14UrgencyRank( right );
        if ( leftUrgency != rightUrgency ) {
            return leftUrgency > rightUrgency;
        }

        if ( left.distance != right.distance ) {
            return left.distance < right.distance;
        }
        if ( left.action != right.action ) {
            return static_cast<int>( left.action ) < static_cast<int>( right.action );
        }
        if ( left.kind != right.kind ) {
            return static_cast<int>( left.kind ) < static_cast<int>( right.kind );
        }
        if ( left.heroId != right.heroId ) {
            return left.heroId < right.heroId;
        }
        if ( left.secondaryHeroId != right.secondaryHeroId ) {
            return left.secondaryHeroId < right.secondaryHeroId;
        }
        if ( left.targetIndex != right.targetIndex ) {
            return left.targetIndex < right.targetIndex;
        }
        if ( left.buildingType != right.buildingType ) {
            return left.buildingType < right.buildingType;
        }
        if ( left.dwellingType != right.dwellingType ) {
            return left.dwellingType < right.dwellingType;
        }
        if ( left.recruitCount != right.recruitCount ) {
            return left.recruitCount > right.recruitCount;
        }
        return adviceIdentity( left ) < adviceIdentity( right );
    }

    void pruneCouncilV14OwnerEchoes( std::vector<StrategicAdvice> & candidates )
    {
        std::sort( candidates.begin(), candidates.end(), councilV14DeterministicLess );

        struct OwnerChoice
        {
            double bestScore{ 0.0 };
            CouncilV9Type bestType{ CouncilV9Type::GENERAL };
            size_t kept{ 0 };
        };

        std::map<CouncilDecisionOwner, OwnerChoice> ownerChoices;
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              OwnerChoice & owner = ownerChoices[councilDecisionOwner( advice )];
                              const CouncilV9Type type = councilV9TypeForKind( advice.kind );
                              if ( owner.kept == 0 ) {
                                  owner.bestScore = advice.score;
                                  owner.bestType = type;
                                  owner.kept = 1;
                                  return false;
                              }

                              const double ratio = owner.bestScore > 0.0 ? advice.score / owner.bestScore : 0.0;
                              if ( type == owner.bestType && ratio >= 0.88 ) {
                                  return true;
                              }

                              if ( owner.kept >= 2 && ratio < 0.92 ) {
                                  return true;
                              }

                              ++owner.kept;
                              return false;
                          } ),
                          candidates.end() );
    }

    std::vector<StrategicAdvice> chooseCouncilV14Advice( std::vector<StrategicAdvice> candidates )
    {
        std::sort( candidates.begin(), candidates.end(), councilV14DeterministicLess );

        std::vector<StrategicAdvice> chosen;
        chosen.reserve( strategicAdviceCount );
        std::set<std::string> identities;
        std::set<int32_t> movementTargets;
        std::map<int32_t, size_t> heroActions;
        std::map<int32_t, size_t> townActions;

        const auto tryChoose = [&]( const StrategicAdvice & advice, const size_t heroLimit, const size_t townLimit ) {
            if ( chosen.size() >= strategicAdviceCount || !advice.reachableThisTurn || advice.score < 2500.0 || !std::isfinite( advice.score ) ) {
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

        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, 1, 1 );
        }
        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, 2, 2 );
        }
        for ( const StrategicAdvice & advice : candidates ) {
            tryChoose( advice, strategicAdviceCount, strategicAdviceCount );
        }

        return chosen;
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV14( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        applyCouncilV9Continuity( candidates );
        applyCouncilObjectiveReservations( candidates, context );
        applyCouncilV9Intelligence( candidates, context );
        applyCouncilV11Intelligence( candidates, context );
        applyCouncilV14ResourceJudgment( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );
        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    CouncilV14ResourceState councilV14VisibleResourceState( const std::vector<StrategicAdvice> & advices )
    {
        return buildCouncilV14ResourceState( advices );
    }

    CouncilV14Signal councilV14SignalFor( const StrategicAdvice & advice, const CouncilContext & context, const CouncilV14ResourceState & state )
    {
        const bool scarce = context.lowGold || context.lowMaterials;
        const CouncilV9Type type = councilV9TypeForKind( advice.kind );
        const bool urgent = type == CouncilV9Type::DEFEND && advice.reachableThisTurn;

        if ( scarce && !isCouncilResourceSpend( advice ) && !urgent && advice.score >= 5200.0
             && ( state.bestSpend <= 0.0 || advice.score >= state.bestSpend * 0.88 ) ) {
            return CouncilV14Signal::SAVE;
        }

        const bool safeCombat = !isCouncilV9CombatAdvice( advice.kind ) || councilV11RiskForAdvice( advice ) == CouncilV9Risk::SOLID;
        if ( advice.reachableThisTurn && safeCombat && advice.score >= 7600.0 ) {
            return CouncilV14Signal::READY;
        }
        return CouncilV14Signal::BASE;
    }

    const char * councilV14TypeLabel( const StrategicAdvice & advice, const CouncilContext & context, const CouncilV14ResourceState & state )
    {
        switch ( councilV14SignalFor( advice, context, state ) ) {
        case CouncilV14Signal::SAVE: return "SAVE";
        case CouncilV14Signal::READY: return "READY";
        default: return councilV11AdviceTypeLabel( councilV11AdviceTypeFor( advice, context ) );
        }
    }

    std::string compactCouncilV14Rationale( const StrategicAdvice & advice )
    {
        std::string text = compactCouncilV11Rationale( advice );
        replaceCouncilV10Text( text, "Resources tight; stronger no-spend play exists.", "Resources tight; prefer a no-spend play." );
        replaceCouncilV10Text( text, "Worth spending now; payoff clears the reserve bar.", "Spend now; payoff clears the reserve bar." );
        replaceCouncilV10Text( text, "Preserves scarce resources without giving up much value.", "Preserves scarce resources with little tradeoff." );
        replaceCouncilV10Text( text, "Close alternative for", "Close option for" );
        return compactCouncilV9Line( std::move( text ), 88 );
    }

    int selectStrategicAdviceV14( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
    {
        if ( advices.empty() ) {
            return -1;
        }

        const size_t visibleCount = std::min( strategicAdviceCount, advices.size() );
        const int32_t windowHeight = std::max<int32_t>( 224, 124 + static_cast<int32_t>( visibleCount ) * 44 );
        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();
        fheroes2::StandardWindow window( 604, windowHeight, true, display );
        const fheroes2::Rect area = window.activeArea();
        const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();
        std::array<fheroes2::Rect, strategicAdviceCount> rows{};
        for ( size_t index = 0; index < visibleCount; ++index ) {
            rows[index] = { area.x + 12, area.y + 62 + static_cast<int32_t>( index ) * 44, area.width - 24, 40 };
        }

        fheroes2::Button closeButton;
        window.renderButton( closeButton, isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD, 0, 1, { 14, 8 },
                             fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );

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

                const fheroes2::Rect banner{ area.x + 8, area.y + 4, area.width - 16, 40 };
                window.applyTextBackgroundShading( banner );
                const fheroes2::Text title( "WAR COUNCIL", fheroes2::FontType::normalYellow() );
                title.draw( banner.x + 9, banner.y + 3, banner.width - 18, display );

                const CouncilContext context = buildCouncilContext( kingdom );
                const CouncilV14ResourceState resourceState = councilV14VisibleResourceState( advices );
                std::string status = std::to_string( visibleCount ) + " orders | " + std::to_string( context.movableHeroes ) + " heroes ready";
                if ( context.lowGold || context.lowMaterials ) {
                    status += " | reserve";
                }
                if ( const Heroes * mainHero = world.GetHeroes( context.mainHeroId ) ) {
                    status += " | main " + mainHero->GetName();
                }
                fheroes2::Text subtitle( status, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 18 );
                subtitle.draw( banner.x + 9, banner.y + 23, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( compactCouncilV9Line( councilReport, 88 ), fheroes2::FontType::smallYellow() );
                    report.fitToOneRow( area.width - 26 );
                    report.draw( area.x + 13, area.y + 47, display );
                }

                for ( size_t index = 0; index < visibleCount; ++index ) {
                    window.applyTextBackgroundShading( rows[index] );
                    const StrategicAdvice & advice = advices[index];
                    int32_t textX = rows[index].x + 9;
                    int32_t textWidth = rows[index].width - 18;
                    if ( const Heroes * hero = world.GetHeroes( advice.heroId ) ) {
                        hero->PortraitRedraw( rows[index].x + 6, rows[index].y + 1, PORT_SMALL, display );
                        textX = rows[index].x + 52;
                        textWidth = rows[index].width - 61;
                    }

                    std::string headingText = hoverIndex == index ? "> " : "";
                    headingText += std::to_string( index + 1 ) + ". ";
                    const char * timing = councilV11TimingLabel( advice );
                    if ( timing[0] != '\0' ) {
                        headingText += std::string( timing ) + " ";
                    }
                    headingText += councilV14TypeLabel( advice, context, resourceState );
                    if ( isCouncilV9CombatAdvice( advice.kind ) ) {
                        const CouncilV9Risk risk = councilV11RiskForAdvice( advice );
                        if ( risk != CouncilV9Risk::SOLID ) {
                            headingText += " " + std::string( councilV9RiskLabel( risk ) );
                        }
                    }
                    headingText += "  " + compactCouncilV10Title( advice );

                    const CouncilV14Signal signal = councilV14SignalFor( advice, context, resourceState );
                    const bool emphasize = index == 0 || hoverIndex == index || signal != CouncilV14Signal::BASE;
                    fheroes2::Text heading( headingText, emphasize ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 3, display );

                    const fheroes2::FontType reasonFont = hoverIndex == index ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite();
                    fheroes2::Text reason( compactCouncilV14Rationale( advice ), reasonFont );
                    reason.fitToOneRow( textWidth );
                    reason.draw( textX, rows[index].y + 20, display );
                }

                fheroes2::Text hint( "1-5 choose | Esc closes | turn stays active", fheroes2::FontType::smallWhite() );
                hint.fitToOneRow( area.width - 132 );
                hint.draw( area.x + 12, area.y + area.height - 19, display );
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

    void showRpgStrategicAdvisorV14()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV14( kingdom, suppressedOrders );
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
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV14( kingdom, suppressedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV14
