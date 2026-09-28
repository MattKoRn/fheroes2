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
    void showRpgStrategicAdvisorV5()
>>>>>>> 182d4e1bbfb8deaa6418fb3e296355db32e85e25
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
