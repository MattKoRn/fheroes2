// War Council v10: action-distinct, confidence-aware advice with compact decision signals.
// v9 supplies the mature candidate generation and execution machinery. This layer fixes
// over-aggressive duplicate suppression, adds safer comparison logic, introduces compact
// advice signals and keeps the entire presentation inside the existing single council window.
#include "game_rpg_advisor_v9.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV10AdviceType : uint8_t
    {
        GUARD,
        PUSH,
        CASH,
        BUILD,
        HANDOFF,
        SCOUT,
        QUICK,
        TEMPO,
        PLAN
    };

    bool isCouncilV10TownAction( const AdviceAction action )
    {
        return action == AdviceAction::RECRUIT_CREATURES || action == AdviceAction::BUILD_STRUCTURE || action == AdviceAction::TOWN_ARMY_MANAGEMENT;
    }

    std::string councilV10DuplicateKey( const StrategicAdvice & advice )
    {
        // Movement candidates frequently describe the same destination with slightly different
        // tactical wording. Keep one strong representative. Non-movement actions can have real,
        // distinct payloads (building, dwelling, recruitment or exchange mode), so preserve their
        // full executable identity instead of collapsing them by broad council type.
        if ( advice.action == AdviceAction::MOVE_HERO ) {
            return "move:" + std::to_string( advice.heroId ) + ':' + std::to_string( advice.targetIndex ) + ':'
                   + std::to_string( static_cast<int>( councilV9TypeForKind( advice.kind ) ) );
        }

        return adviceIdentity( advice );
    }

    void suppressCouncilV10NearDuplicates( std::vector<StrategicAdvice> & candidates )
    {
        std::sort( candidates.begin(), candidates.end(), []( const StrategicAdvice & left, const StrategicAdvice & right ) {
            if ( left.score != right.score ) {
                return left.score > right.score;
            }
            return left.distance < right.distance;
        } );

        std::set<std::string> seen;
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              return !seen.insert( councilV10DuplicateKey( advice ) ).second;
                          } ),
                          candidates.end() );
    }

    void applyCouncilV10Intelligence( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        double bestSaferScore = 0.0;
        for ( const StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                continue;
            }

            if ( !isCouncilV9CombatAdvice( advice.kind ) || councilV9RiskForAdvice( advice ) != CouncilV9Risk::RISK ) {
                bestSaferScore = std::max( bestSaferScore, advice.score );
            }
        }

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

            if ( isCouncilV10TownAction( advice.action ) && advice.targetIndex < 0 ) {
                advice.score = 0.0;
                continue;
            }

            const CouncilV9Type type = councilV9TypeForKind( advice.kind );
            const bool mainHero = hero != nullptr && hero->GetID() == context.mainHeroId;

            if ( isCouncilV9CombatAdvice( advice.kind ) ) {
                const CouncilV9Risk risk = councilV9RiskForAdvice( advice );
                if ( risk == CouncilV9Risk::RISK ) {
                    if ( bestSaferScore >= advice.score * 0.72 ) {
                        advice.score *= 0.46;
                        advice.rationale = "A safer order is close in value. | " + advice.rationale;
                    }
                    else {
                        advice.score *= 0.72;
                    }
                }
                else if ( risk == CouncilV9Risk::SOLID ) {
                    advice.score *= 1.04;
                }
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

    CouncilV10AdviceType councilV10AdviceTypeFor( const StrategicAdvice & advice, const CouncilContext & context )
    {
        const CouncilV9Type baseType = councilV9TypeForKind( advice.kind );
        if ( baseType == CouncilV9Type::DEFEND || baseType == CouncilV9Type::RECOVER ) {
            return CouncilV10AdviceType::GUARD;
        }

        if ( advice.action == AdviceAction::BUILD_STRUCTURE || advice.action == AdviceAction::RECRUIT_CREATURES ) {
            return CouncilV10AdviceType::BUILD;
        }

        if ( baseType == CouncilV9Type::SUPPORT ) {
            return CouncilV10AdviceType::HANDOFF;
        }
        if ( baseType == CouncilV9Type::GROW ) {
            return CouncilV10AdviceType::CASH;
        }
        if ( baseType == CouncilV9Type::SCOUT ) {
            return CouncilV10AdviceType::SCOUT;
        }
        if ( isCouncilV9CombatAdvice( advice.kind ) && councilV9RiskForAdvice( advice ) == CouncilV9Risk::SOLID ) {
            return CouncilV10AdviceType::PUSH;
        }
        if ( baseType == CouncilV9Type::TEMPO ) {
            return CouncilV10AdviceType::TEMPO;
        }

        if ( advice.action == AdviceAction::MOVE_HERO && advice.reachableThisTurn ) {
            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            if ( hero != nullptr && hero->GetMovePoints() > 0 ) {
                const double movementUse = std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
                if ( movementUse <= 0.25 && hero->GetID() != context.mainHeroId ) {
                    return CouncilV10AdviceType::QUICK;
                }
            }
        }

        if ( baseType == CouncilV9Type::POWER || baseType == CouncilV9Type::ARMY ) {
            return CouncilV10AdviceType::BUILD;
        }
        return CouncilV10AdviceType::PLAN;
    }

    const char * councilV10AdviceTypeLabel( const CouncilV10AdviceType type )
    {
        switch ( type ) {
        case CouncilV10AdviceType::GUARD: return "GUARD";
        case CouncilV10AdviceType::PUSH: return "PUSH";
        case CouncilV10AdviceType::CASH: return "CASH";
        case CouncilV10AdviceType::BUILD: return "BUILD";
        case CouncilV10AdviceType::HANDOFF: return "HANDOFF";
        case CouncilV10AdviceType::SCOUT: return "SCOUT";
        case CouncilV10AdviceType::QUICK: return "QUICK";
        case CouncilV10AdviceType::TEMPO: return "TEMPO";
        default: return "PLAN";
        }
    }

    void replaceCouncilV10Text( std::string & text, const std::string & from, const std::string & to )
    {
        size_t position = 0;
        while ( ( position = text.find( from, position ) ) != std::string::npos ) {
            text.replace( position, from.size(), to );
            position += to.size();
        }
    }

    std::string compactCouncilV10Title( const StrategicAdvice & advice )
    {
        std::string title = advice.title;
        const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        if ( hero != nullptr ) {
            const std::string heroPrefix = hero->GetName() + ": ";
            if ( title.rfind( heroPrefix, 0 ) == 0 ) {
                title.erase( 0, heroPrefix.size() );
            }
        }
        return compactCouncilV9Line( std::move( title ), 72 );
    }

    std::string compactCouncilV10Rationale( const StrategicAdvice & advice )
    {
        std::string text = compactCouncilV9Rationale( advice );
        replaceCouncilV10Text( text, "Combat margin is too thin; prefer a safer order.", "Combat margin too thin." );
        replaceCouncilV10Text( text, "Combat margin is narrow; treat this as a risky option.", "Narrow combat margin." );
        replaceCouncilV10Text( text, "Do not spend most of the main hero's movement on a minor errand.", "Preserve the main hero's movement." );
        replaceCouncilV10Text( text, "Leave scouting to a support hero when possible.", "Prefer a support hero for scouting." );
        replaceCouncilV10Text( text, "Recover position and strength before forcing another fight.", "Recover before forcing another fight." );
        replaceCouncilV10Text( text, "Keep the stronger plan unless this detour becomes clearly better.", "Avoid a weaker detour." );
        return compactCouncilV9Line( std::move( text ), 96 );
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV10( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        applyCouncilV10Intelligence( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );

        return chooseKingdomCouncilAdvice( std::move( candidates ) );
    }

    int selectStrategicAdviceV10( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
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
                    fheroes2::Text report( compactCouncilV9Line( councilReport, 96 ), fheroes2::FontType::smallYellow() );
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

                    std::string headingText = hoverIndex == index ? ">" : "";
                    headingText += std::to_string( index + 1 ) + ". " + councilV10AdviceTypeLabel( councilV10AdviceTypeFor( advice, context ) );
                    if ( isCouncilV9CombatAdvice( advice.kind ) ) {
                        const CouncilV9Risk risk = councilV9RiskForAdvice( advice );
                        if ( risk != CouncilV9Risk::SOLID ) {
                            headingText += " " + std::string( councilV9RiskLabel( risk ) );
                        }
                    }
                    headingText += "  " + compactCouncilV10Title( advice );

                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 3, display );

                    const fheroes2::FontType reasonFont = hoverIndex == index ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite();
                    fheroes2::Text reason( compactCouncilV10Rationale( advice ), reasonFont );
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

    void showRpgStrategicAdvisorV10()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV10( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV10( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV10( kingdom, failedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV10
