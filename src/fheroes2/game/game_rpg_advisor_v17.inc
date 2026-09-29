// War Council v17: live combat matchup signals and threat-aware castle defense.
// v16 owns live hero executability. This layer adds one final combat read using the existing
// defender-strength model, extends that reasoning to DEFENSE / CASTLE_RELIEF, removes stale
// defensive orders after the threat changes, and keeps urgent relief above ordinary errands.
#include "game_rpg_advisor_v16.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV17CombatSignal : uint8_t
    {
        NONE,
        EDGE,
        EVEN,
        DANGER
    };

    struct CouncilV17DefenseMatchup
    {
        const Heroes * threat{ nullptr };
        uint32_t threatDistance{ std::numeric_limits<uint32_t>::max() };
        double enemyStrength{ 0.0 };
        double garrisonStrength{ 0.0 };
    };

    CouncilV17CombatSignal councilV17SignalForEdge( const double edge )
    {
        if ( edge >= 1.45 ) {
            return CouncilV17CombatSignal::EDGE;
        }
        if ( edge >= 1.15 ) {
            return CouncilV17CombatSignal::EVEN;
        }
        return CouncilV17CombatSignal::DANGER;
    }

    const char * councilV17SignalLabel( const CouncilV17CombatSignal signal )
    {
        switch ( signal ) {
        case CouncilV17CombatSignal::EDGE: return "EDGE";
        case CouncilV17CombatSignal::EVEN: return "EVEN";
        case CouncilV17CombatSignal::DANGER: return "DANGER";
        default: return "";
        }
    }

    std::vector<const Heroes *> councilV17VisibleEnemyHeroes( const Kingdom & kingdom )
    {
        std::vector<const Heroes *> enemies;
        const PlayerColor color = kingdom.GetColor();
        const int32_t mapSize = world.w() * world.h();
        for ( int32_t index = 0; index < mapSize; ++index ) {
            const Maps::Tile & tile = world.getTile( index );
            if ( tile.isFog( color ) || tile.getMainObjectType() != MP2::OBJ_HERO ) {
                continue;
            }

            const Heroes * enemy = tile.getHero();
            if ( enemy != nullptr && enemy->isActive() && !areFriendlyColors( color, enemy->GetColor() ) ) {
                enemies.emplace_back( enemy );
            }
        }
        return enemies;
    }

    CouncilV17DefenseMatchup councilV17DefenseMatchup( const Kingdom & kingdom, const StrategicAdvice & advice,
                                                        const std::vector<const Heroes *> & visibleEnemies )
    {
        CouncilV17DefenseMatchup matchup;
        if ( advice.targetIndex < 0 || !Maps::isValidAbsIndex( advice.targetIndex ) ) {
            return matchup;
        }

        const Castle * castle = world.getCastleEntrance( Maps::GetPoint( advice.targetIndex ) );
        if ( castle == nullptr || !areFriendlyColors( kingdom.GetColor(), castle->GetColor() ) ) {
            return matchup;
        }

        for ( const Heroes * enemy : visibleEnemies ) {
            if ( enemy == nullptr || !enemy->isActive() ) {
                continue;
            }
            const uint32_t distance = Maps::GetApproximateDistance( enemy->GetIndex(), castle->GetIndex() );
            if ( distance < matchup.threatDistance ) {
                matchup.threat = enemy;
                matchup.threatDistance = distance;
            }
        }

        if ( matchup.threat == nullptr || matchup.threatDistance > 10 ) {
            return matchup;
        }

        matchup.enemyStrength = std::max( 1.0, matchup.threat->GetArmy().GetStrength() );
        matchup.garrisonStrength = std::max( 1.0, castle->GetGarrisonStrength( *matchup.threat ) );
        return matchup;
    }

    bool councilV17IsDefenseOrder( const StrategicAdvice & advice )
    {
        return advice.kind == StrategicAdviceKind::DEFENSE || advice.kind == StrategicAdviceKind::CASTLE_RELIEF;
    }

    void prependCouncilV17MatchupReason( StrategicAdvice & advice, const CouncilV17CombatSignal signal, const double edge, const std::string & detail )
    {
        std::string reason = std::string( councilV17SignalLabel( signal ) ) + " " + formatAdvisorRatio( edge ) + ".";
        if ( !detail.empty() ) {
            reason += " " + detail;
        }
        advice.rationale = std::move( reason ) + " | " + advice.rationale;
    }

    void applyCouncilV17LiveCombat( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom )
    {
        const std::vector<const Heroes *> visibleEnemies = councilV17VisibleEnemyHeroes( kingdom );
        double bestErrandScore = 0.0;
        for ( const StrategicAdvice & advice : candidates ) {
            if ( isCouncilV9Errand( advice.kind ) && std::isfinite( advice.score ) ) {
                bestErrandScore = std::max( bestErrandScore, advice.score );
            }
        }

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( StrategicAdvice & advice ) {
                              if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                                  return true;
                              }

                              Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
                              if ( councilV17IsDefenseOrder( advice ) ) {
                                  if ( hero == nullptr || !hero->isActive() || hero->GetColor() != kingdom.GetColor() ) {
                                      return true;
                                  }

                                  const CouncilV17DefenseMatchup matchup = councilV17DefenseMatchup( kingdom, advice, visibleEnemies );
                                  if ( matchup.threat == nullptr || matchup.enemyStrength <= 0.0 || matchup.garrisonStrength <= 0.0 ) {
                                      return true;
                                  }

                                  // If the visible threat is no longer meaningful relative to the current garrison,
                                  // the original defensive recommendation is stale and should disappear.
                                  if ( matchup.enemyStrength < matchup.garrisonStrength * 0.68 ) {
                                      return true;
                                  }

                                  const double combinedDefense = std::max( 1.0, hero->GetArmy().GetStrength() + matchup.garrisonStrength );
                                  const double edge = combinedDefense / matchup.enemyStrength;
                                  const CouncilV17CombatSignal signal = councilV17SignalForEdge( edge );
                                  const bool relief = matchup.enemyStrength >= matchup.garrisonStrength;
                                  const bool closeThreat = matchup.threatDistance <= 6;

                                  if ( signal == CouncilV17CombatSignal::EDGE ) {
                                      advice.score *= 1.08;
                                      prependCouncilV17MatchupReason( advice, signal, edge, "Arrival should stabilize the town." );
                                  }
                                  else if ( signal == CouncilV17CombatSignal::EVEN ) {
                                      advice.score *= 1.03;
                                      prependCouncilV17MatchupReason( advice, signal, edge, "Relief is useful but the margin stays narrow." );
                                  }
                                  else {
                                      // Do not bury an urgent defense simply because the arriving army is still
                                      // outmatched. The player needs to see the danger, not lose the warning.
                                      advice.score *= relief ? 1.02 : 0.86;
                                      prependCouncilV17MatchupReason( advice, signal, edge, "Even after arrival the local defense is strained." );
                                  }

                                  if ( relief && closeThreat && advice.reachableThisTurn ) {
                                      advice.score *= 1.12;
                                      if ( bestErrandScore > 0.0 ) {
                                          advice.score = std::max( advice.score, bestErrandScore * 1.10 );
                                      }
                                  }
                                  return false;
                              }

                              if ( !isCouncilV9CombatAdvice( advice.kind ) ) {
                                  return false;
                              }
                              if ( hero == nullptr || !hero->isActive() || advice.targetIndex < 0 || !Maps::isValidAbsIndex( advice.targetIndex ) ) {
                                  return true;
                              }

                              const Maps::Tile & targetTile = world.getTile( advice.targetIndex );
                              const double defenders = defenderStrengthForAdvice( targetTile, *hero );
                              if ( defenders <= 0.0 ) {
                                  return false;
                              }

                              const double edge = std::max( 1.0, hero->GetArmy().GetStrength() ) / defenders;
                              const CouncilV17CombatSignal signal = councilV17SignalForEdge( edge );
                              if ( edge < 0.90 ) {
                                  return true;
                              }

                              // v9/v11 already own the main risk penalty. v17 only adds a small final
                              // live correction and a concise matchup signal, avoiding double-scoring.
                              if ( signal == CouncilV17CombatSignal::EDGE ) {
                                  advice.score *= edge >= 2.20 ? 1.04 : 1.02;
                                  prependCouncilV17MatchupReason( advice, signal, edge, "Live matchup favors the attack." );
                              }
                              else if ( signal == CouncilV17CombatSignal::EVEN ) {
                                  advice.score *= 0.96;
                                  prependCouncilV17MatchupReason( advice, signal, edge, "Live margin is playable but not comfortable." );
                              }
                              else {
                                  advice.score *= 0.72;
                                  prependCouncilV17MatchupReason( advice, signal, edge, "Live matchup has become fragile." );
                              }
                              return false;
                          } ),
                          candidates.end() );
    }

    std::string compactCouncilV17Rationale( const StrategicAdvice & advice )
    {
        std::string text = compactCouncilV14Rationale( advice );
        replaceCouncilV10Text( text, "Arrival should stabilize the town.", "Arrival should stabilize town." );
        replaceCouncilV10Text( text, "Relief is useful but the margin stays narrow.", "Useful relief; margin stays narrow." );
        replaceCouncilV10Text( text, "Even after arrival the local defense is strained.", "Arrival still leaves defense strained." );
        replaceCouncilV10Text( text, "Live matchup favors the attack.", "Live matchup favors attack." );
        replaceCouncilV10Text( text, "Live margin is playable but not comfortable.", "Playable margin; not comfortable." );
        replaceCouncilV10Text( text, "Live matchup has become fragile.", "Live matchup is fragile." );
        return compactCouncilV9Line( std::move( text ), 88 );
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV17( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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

        // Re-read live combat state after the established risk model has done its main scoring.
        // This provides compact matchup signals and covers defensive threat orders that v9 does not
        // classify as combat, without replacing the older model.
        applyCouncilV17LiveCombat( candidates, kingdom );

        applyCouncilV14ResourceJudgment( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );
        applyCouncilV15SpendCommitments( candidates, kingdom, context );
        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    int selectStrategicAdviceV17( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
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
                    const bool emphasize = index == 0 || hoverIndex == index || signal != CouncilV14Signal::BASE || councilV17IsDefenseOrder( advice );
                    fheroes2::Text heading( headingText, emphasize ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 3, display );

                    const fheroes2::FontType reasonFont = hoverIndex == index ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite();
                    fheroes2::Text reason( compactCouncilV17Rationale( advice ), reasonFont );
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

    void showRpgStrategicAdvisorV17()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV17( kingdom, suppressedOrders );
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
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV17( kingdom, suppressedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV17
