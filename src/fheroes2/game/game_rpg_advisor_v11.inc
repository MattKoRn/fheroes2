// War Council v11: owner-aware risk comparison and clearer decision signals.
// v10 keeps the compact single-window presentation and action-distinct deduplication.
// This layer fixes cross-owner risk comparisons, rejects stale executable targets, removes
// weak same-owner leftovers and adds compact MUSTER / DEVELOP signals without widening the UI.
#include "game_rpg_advisor_v10.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV11AdviceType : uint8_t
    {
        GUARD,
        PUSH,
        CASH,
        BUILD,
        MUSTER,
        DEVELOP,
        HANDOFF,
        SCOUT,
        QUICK,
        TEMPO,
        PLAN
    };

    CouncilV9Risk councilV11RiskForAdvice( const StrategicAdvice & advice )
    {
        if ( isCouncilV9CombatAdvice( advice.kind ) && ( advice.targetIndex < 0 || !Maps::isValidAbsIndex( advice.targetIndex ) ) ) {
            return CouncilV9Risk::CHECK;
        }
        return councilV9RiskForAdvice( advice );
    }

    bool isCouncilV11ExecutableTargetValid( const StrategicAdvice & advice )
    {
        if ( advice.action == AdviceAction::MOVE_HERO ) {
            return advice.targetIndex >= 0 && Maps::isValidAbsIndex( advice.targetIndex );
        }

        if ( isCouncilV10TownAction( advice.action ) ) {
            return advice.targetIndex >= 0 && Maps::isValidAbsIndex( advice.targetIndex )
                   && world.getCastleEntrance( Maps::GetPoint( advice.targetIndex ) ) != nullptr;
        }

        return true;
    }

    std::map<CouncilDecisionOwner, double> buildCouncilV11SaferOwnerScores( const std::vector<StrategicAdvice> & candidates )
    {
        std::map<CouncilDecisionOwner, double> saferScores;
        for ( const StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 || !isCouncilV11ExecutableTargetValid( advice ) ) {
                continue;
            }

            if ( isCouncilV9CombatAdvice( advice.kind ) && councilV11RiskForAdvice( advice ) == CouncilV9Risk::RISK ) {
                continue;
            }

            double & best = saferScores[councilDecisionOwner( advice )];
            best = std::max( best, advice.score );
        }
        return saferScores;
    }

    void applyCouncilV11Intelligence( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        const std::map<CouncilDecisionOwner, double> saferOwnerScores = buildCouncilV11SaferOwnerScores( candidates );

        for ( StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                advice.score = 0.0;
                continue;
            }

            const bool heroAction = advice.action == AdviceAction::MOVE_HERO || advice.action == AdviceAction::HERO_EXCHANGE;
            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            if ( heroAction && ( hero == nullptr || !hero->isActive() ) ) {
                advice.score = 0.0;
                continue;
            }

            if ( !isCouncilV11ExecutableTargetValid( advice ) ) {
                advice.score = 0.0;
                continue;
            }

            if ( advice.action == AdviceAction::MOVE_HERO && ( hero->GetMovePoints() == 0 || !hero->MayStillMove( false, false ) ) ) {
                advice.score = 0.0;
                continue;
            }

            if ( advice.action == AdviceAction::HERO_EXCHANGE ) {
                const Heroes * partner = advice.secondaryHeroId >= 0 ? world.GetHeroes( advice.secondaryHeroId ) : nullptr;
                if ( partner == nullptr || !partner->isActive() || partner->GetID() == hero->GetID() ) {
                    advice.score = 0.0;
                    continue;
                }
            }

            const CouncilV9Type type = councilV9TypeForKind( advice.kind );
            const bool mainHero = hero != nullptr && hero->GetID() == context.mainHeroId;

            if ( isCouncilV9CombatAdvice( advice.kind ) ) {
                const CouncilV9Risk risk = councilV11RiskForAdvice( advice );
                if ( risk == CouncilV9Risk::RISK ) {
                    const auto saferIt = saferOwnerScores.find( councilDecisionOwner( advice ) );
                    const double saferScore = saferIt != saferOwnerScores.end() ? saferIt->second : 0.0;
                    if ( saferScore >= advice.score * 0.72 ) {
                        advice.score *= 0.46;
                        advice.rationale = "A safer use of this hero is close in value. | " + advice.rationale;
                    }
                    else if ( saferScore >= advice.score * 0.45 ) {
                        advice.score *= 0.68;
                        advice.rationale = "A safer alternative exists, but this attack has more upside. | " + advice.rationale;
                    }
                    else {
                        advice.score *= 0.82;
                    }
                }
                else if ( risk == CouncilV9Risk::SOLID ) {
                    advice.score *= 1.04;
                }
            }

            if ( type == CouncilV9Type::DEFEND && advice.reachableThisTurn ) {
                advice.score *= 1.08;
                advice.rationale = "Immediate defense outranks optional tempo. | " + advice.rationale;
            }

            if ( hero != nullptr && advice.action == AdviceAction::MOVE_HERO && advice.reachableThisTurn && hero->GetMovePoints() > 0 ) {
                const double movementUse = std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
                if ( movementUse <= 0.22 && type != CouncilV9Type::SCOUT && !mainHero ) {
                    advice.score *= 1.08;
                    advice.rationale = "Cheap move; keep most movement in reserve. | " + advice.rationale;
                }
                else if ( movementUse <= 0.18 && mainHero && ( type == CouncilV9Type::STRIKE || type == CouncilV9Type::POWER ) ) {
                    advice.score *= 1.06;
                }
            }

            if ( type == CouncilV9Type::GROW && ( context.lowGold || context.lowMaterials ) ) {
                advice.score *= 1.05;
            }

            if ( advice.action == AdviceAction::RECRUIT_CREATURES && type == CouncilV9Type::ARMY ) {
                advice.score *= 1.05;
            }
        }
    }

    void pruneCouncilV11WeakAlternatives( std::vector<StrategicAdvice> & candidates )
    {
        std::map<CouncilDecisionOwner, double> ownerBest;
        for ( const StrategicAdvice & advice : candidates ) {
            if ( std::isfinite( advice.score ) && advice.score > 0.0 ) {
                double & best = ownerBest[councilDecisionOwner( advice )];
                best = std::max( best, advice.score );
            }
        }

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              if ( !std::isfinite( advice.score ) || advice.score < 2500.0 ) {
                                  return true;
                              }

                              const auto bestIt = ownerBest.find( councilDecisionOwner( advice ) );
                              if ( bestIt == ownerBest.end() || bestIt->second <= 0.0 ) {
                                  return false;
                              }

                              if ( councilV9TypeForKind( advice.kind ) == CouncilV9Type::DEFEND && advice.reachableThisTurn ) {
                                  return false;
                              }

                              return advice.score < bestIt->second * 0.58;
                          } ),
                          candidates.end() );
    }

    CouncilV11AdviceType councilV11AdviceTypeFor( const StrategicAdvice & advice, const CouncilContext & context )
    {
        const CouncilV9Type baseType = councilV9TypeForKind( advice.kind );
        if ( baseType == CouncilV9Type::DEFEND || baseType == CouncilV9Type::RECOVER ) {
            return CouncilV11AdviceType::GUARD;
        }
        if ( advice.action == AdviceAction::BUILD_STRUCTURE ) {
            return CouncilV11AdviceType::BUILD;
        }
        if ( advice.action == AdviceAction::RECRUIT_CREATURES ) {
            return CouncilV11AdviceType::MUSTER;
        }
        if ( baseType == CouncilV9Type::SUPPORT ) {
            return CouncilV11AdviceType::HANDOFF;
        }
        if ( baseType == CouncilV9Type::GROW ) {
            return CouncilV11AdviceType::CASH;
        }
        if ( baseType == CouncilV9Type::SCOUT ) {
            return CouncilV11AdviceType::SCOUT;
        }
        if ( isCouncilV9CombatAdvice( advice.kind ) && councilV11RiskForAdvice( advice ) == CouncilV9Risk::SOLID ) {
            return CouncilV11AdviceType::PUSH;
        }
        if ( baseType == CouncilV9Type::TEMPO ) {
            return CouncilV11AdviceType::TEMPO;
        }
        if ( baseType == CouncilV9Type::POWER ) {
            return CouncilV11AdviceType::DEVELOP;
        }

        if ( advice.action == AdviceAction::MOVE_HERO && advice.reachableThisTurn ) {
            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            if ( hero != nullptr && hero->GetMovePoints() > 0 ) {
                const double movementUse = std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
                if ( movementUse <= 0.25 && hero->GetID() != context.mainHeroId ) {
                    return CouncilV11AdviceType::QUICK;
                }
            }
        }

        if ( baseType == CouncilV9Type::ARMY ) {
            return CouncilV11AdviceType::MUSTER;
        }
        return CouncilV11AdviceType::PLAN;
    }

    const char * councilV11AdviceTypeLabel( const CouncilV11AdviceType type )
    {
        switch ( type ) {
        case CouncilV11AdviceType::GUARD: return "GUARD";
        case CouncilV11AdviceType::PUSH: return "PUSH";
        case CouncilV11AdviceType::CASH: return "CASH";
        case CouncilV11AdviceType::BUILD: return "BUILD";
        case CouncilV11AdviceType::MUSTER: return "MUSTER";
        case CouncilV11AdviceType::DEVELOP: return "DEVELOP";
        case CouncilV11AdviceType::HANDOFF: return "HANDOFF";
        case CouncilV11AdviceType::SCOUT: return "SCOUT";
        case CouncilV11AdviceType::QUICK: return "QUICK";
        case CouncilV11AdviceType::TEMPO: return "TEMPO";
        default: return "PLAN";
        }
    }

    const char * councilV11TimingLabel( const StrategicAdvice & advice )
    {
        const CouncilV9Type type = councilV9TypeForKind( advice.kind );
        if ( type == CouncilV9Type::DEFEND && advice.reachableThisTurn ) {
            return "NOW";
        }
        if ( type == CouncilV9Type::RECOVER ) {
            return "RESET";
        }
        if ( isCouncilV9CombatAdvice( advice.kind ) && advice.reachableThisTurn && advice.score >= 9000.0
             && councilV11RiskForAdvice( advice ) == CouncilV9Risk::SOLID ) {
            return "ACT";
        }
        return "";
    }

    std::string compactCouncilV11Rationale( const StrategicAdvice & advice )
    {
        std::string text = compactCouncilV10Rationale( advice );
        replaceCouncilV10Text( text, "A safer use of this hero is close in value.", "Safer hero play is close in value." );
        replaceCouncilV10Text( text, "A safer alternative exists, but this attack has more upside.", "Safer option exists; attack has more upside." );
        replaceCouncilV10Text( text, "Immediate defense outranks optional tempo.", "Defend before spending optional tempo." );
        return compactCouncilV9Line( std::move( text ), 92 );
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV11( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );
        pruneCouncilV11WeakAlternatives( candidates );

        return chooseKingdomCouncilAdvice( std::move( candidates ) );
    }

    int selectStrategicAdviceV11( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
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
                std::string status = std::to_string( visibleCount ) + " orders | " + std::to_string( context.movableHeroes ) + " heroes ready";
                if ( context.lowGold ) {
                    status += " | gold tight";
                }
                if ( context.lowMaterials ) {
                    status += " | materials tight";
                }
                if ( const Heroes * mainHero = world.GetHeroes( context.mainHeroId ) ) {
                    status += " | main " + mainHero->GetName();
                }
                fheroes2::Text subtitle( status, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 18 );
                subtitle.draw( banner.x + 9, banner.y + 23, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( compactCouncilV9Line( councilReport, 92 ), fheroes2::FontType::smallYellow() );
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
                    headingText += councilV11AdviceTypeLabel( councilV11AdviceTypeFor( advice, context ) );
                    if ( isCouncilV9CombatAdvice( advice.kind ) ) {
                        const CouncilV9Risk risk = councilV11RiskForAdvice( advice );
                        if ( risk != CouncilV9Risk::SOLID ) {
                            headingText += " " + std::string( councilV9RiskLabel( risk ) );
                        }
                    }
                    headingText += "  " + compactCouncilV10Title( advice );

                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 3, display );

                    const fheroes2::FontType reasonFont = hoverIndex == index ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite();
                    fheroes2::Text reason( compactCouncilV11Rationale( advice ), reasonFont );
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

    void showRpgStrategicAdvisorV11()
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
        recentCouncilV9HeroIntents.clear();

        while ( true ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV11( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV11( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV11( kingdom, failedOrders );
            const auto refreshedIt = std::find_if( refreshed.begin(), refreshed.end(), [&]( const StrategicAdvice & advice ) {
                return adviceIdentity( advice ) == selectedIdentity;
            } );
            if ( refreshedIt == refreshed.end() ) {
                councilReport = "Situation changed; advice refreshed.";
                continue;
            }

            const StrategicAdvice current = *refreshedIt;
            const AdviceExecutionResult execution = executeStrategicAdvice( kingdom, *player, current );
            councilReport = execution.message;
            if ( !execution.success ) {
                failedOrders.insert( selectedIdentity );
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV11
