// War Council v20: clean visuals, remaining-budget planning with partial recruitment, new strategic advice types, bug fixes, compact writing, and zero UI bloat.
// v19 owns area coordination. This layer introduces:
// 1. Visual icons: Aligned castle locator icons for town/castle orders matching hero portraits.
// 2. Budget-aware planning: Live remaining-resource simulation allows partial recruitment and stronger spending to capture the best plan without all-or-nothing commitment.
// 3. New advice types: Tavern Hero Recruitment (RECRUIT_HERO), Mana Restoration (MANA_RESTORATION), and Spell Shrine Study (SHRINE_STUDY).
// 4. AI Bug Fixes: Pruning zero-movement orders, full-bag artifact deadlocks, mana well waste, and shrine wisdom validation.
// 5. Compact Writing: Eliminating redundant boilerplate across titles and rationales.
// 6. Streamlined Feedback: In-dialog banner feedback replacing modal popup interruptions on successful orders.
// 7. Right-Click Inspection: Classic HOMM2 detail popup for order inspection without screen bloat.

#include "game_rpg_advisor_v19.inc"
#undef showRpgStrategicAdvisor

namespace
{
    void appendCouncilV20HeroRecruitmentAdvice( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const CouncilContext & /* context */ )
    {
        const size_t heroCount = kingdom.GetHeroes().size();
        if ( heroCount >= 8 || heroCount >= std::max<size_t>( 3, kingdom.GetCastles().size() * 2 ) ) {
            return;
        }

        const Funds & funds = kingdom.GetFunds();
        if ( funds.gold < 3500 ) {
            return;
        }

        for ( Castle * castle : kingdom.GetCastles() ) {
            if ( castle == nullptr || castle->GetColor() != kingdom.GetColor() || !castle->AllowBuyHero() ) {
                continue;
            }

            Heroes * recruit = kingdom.GetRecruits().GetHero1();
            if ( recruit == nullptr ) {
                recruit = kingdom.GetRecruits().GetHero2();
            }
            if ( recruit == nullptr ) {
                continue;
            }

            StrategicAdvice advice;
            advice.kind = StrategicAdviceKind::RECRUIT_HERO;
            advice.action = AdviceAction::RECRUIT_HERO;
            advice.targetIndex = castle->GetIndex();
            advice.targetObjectType = MP2::OBJ_CASTLE;
            // A second hero is exceptionally valuable for early tempo, ferrying, and scouting.
            const double heroBonus = ( heroCount == 1 ) ? 4200.0 : ( ( heroCount == 2 ) ? 2400.0 : 600.0 );
            advice.score = 8000.0 + heroBonus;
            advice.reachableThisTurn = true;
            advice.title = castle->GetName() + ": Recruit " + recruit->GetName();
            advice.rationale = "Expand scouting, ferrying and speed (2500 gold).";
            candidates.emplace_back( std::move( advice ) );
            break;
        }
    }

    void filterCouncilV20SanityAndPruning( std::vector<StrategicAdvice> & candidates, Kingdom & /* kingdom */ )
    {
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              if ( advice.action == AdviceAction::MOVE_HERO ) {
                                  const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
                                  if ( hero == nullptr || !hero->isActive() || hero->GetMovePoints() == 0 || !hero->MayStillMove( false, false ) ) {
                                      return true;
                                  }

                                  if ( advice.targetIndex >= 0 && Maps::isValidAbsIndex( advice.targetIndex ) ) {
                                      const Maps::Tile & tile = world.getTile( advice.targetIndex );
                                      const MP2::MapObjectType objType = tile.getMainObjectType();

                                      // Bug fix: Do not advise artifact recovery when hero bag is already full
                                      if ( ( objType == MP2::OBJ_ARTIFACT || objType == MP2::OBJ_SHIPWRECK_SURVIVOR ) && hero->IsFullBagArtifacts() ) {
                                          return true;
                                      }

                                      // Bug fix: Do not advise magic well / spring if mana is already full
                                      if ( objType == MP2::OBJ_MAGIC_WELL && hero->GetSpellPoints() >= hero->GetMaxSpellPoints() ) {
                                          return true;
                                      }
                                      if ( objType == MP2::OBJ_ARTESIAN_SPRING && hero->GetSpellPoints() >= 2 * hero->GetMaxSpellPoints() ) {
                                          return true;
                                      }

                                      // Bug fix: Do not advise spell shrines without spellbook or missing wisdom
                                      if ( ( objType == MP2::OBJ_SHRINE_FIRST_CIRCLE || objType == MP2::OBJ_SHRINE_SECOND_CIRCLE
                                             || objType == MP2::OBJ_SHRINE_THIRD_CIRCLE )
                                           && !hero->HaveSpellBook() ) {
                                          return true;
                                      }
                                      if ( objType == MP2::OBJ_SHRINE_THIRD_CIRCLE && hero->GetLevelSkill( Skill::Secondary::WISDOM ) == Skill::Level::NONE ) {
                                          return true;
                                      }

                                      // Bug fix: Do not advise Witch's hut if hero already has the skill or has 8 secondary skills
                                      if ( objType == MP2::OBJ_WITCHS_HUT ) {
                                          const Skill::Secondary skill = Maps::getSecondarySkillFromWitchsHut( tile );
                                          if ( hero->GetLevelSkill( skill.Skill() ) != Skill::Level::NONE || hero->HasMaxSecondarySkill() ) {
                                              return true;
                                          }
                                      }
                                  }
                              }
                              return false;
                          } ),
                          candidates.end() );
    }

    std::string compactCouncilV20Rationale( const StrategicAdvice & advice )
    {
        std::string text = compactCouncilV17Rationale( advice );
        replaceCouncilV10Text( text, "Expand kingdom mobility, scouting and logistics", "Expand scouting, ferrying and speed" );
        replaceCouncilV10Text( text, "Legal and affordable construction using normal town rules", "Legal, funded construction" );
        replaceCouncilV10Text( text, "Move the strongest useful garrison stacks onto the visiting hero", "Transfer best garrison troops to hero" );
        replaceCouncilV10Text( text, "Move slow stacks into the garrison while preserving a fast field stack", "Deposit slow troops into garrison" );
        replaceCouncilV10Text( text, "Transfer field stacks into the garrison while retaining a fast reserve on the hero", "Fortify garrison; keep fast reserve" );
        replaceCouncilV10Text( text, "Acquire new tactical magic; wisdom requirement met.", "Learn new spell; wisdom verified" );
        replaceCouncilV10Text( text, "Spell points depleted; restore for battle casting.", "Restore depleted spell points" );
        replaceCouncilV10Text( text, "Safe army advantage converts movement into experience and access", "Safe combat; gain exp and map access" );
        replaceCouncilV10Text( text, "Permanent income and territorial control available this turn", "Permanent income and territorial control" );
        replaceCouncilV10Text( text, "Low-commitment resource gain inside the current movement budget", "Low-commitment resource sweep" );
        replaceCouncilV10Text( text, "Convert spare movement into durable hero power", "Acquire artifact; build hero power" );
        replaceCouncilV10Text( text, "Immediate treasure without sacrificing the turn to travel", "Immediate gold and exp pickup" );
        replaceCouncilV10Text( text, "Take a permanent stronghold while route and matchup are favorable", "Capture stronghold; favorable matchup" );
        replaceCouncilV10Text( text, "Remove a mobile enemy threat before it chooses the engagement", "Intercept enemy hero; gain initiative" );
        replaceCouncilV10Text( text, " | executable this turn", "" );
        return compactCouncilV9Line( std::move( text ), 88 );
    }

    const char * councilV20TypeTag( const StrategicAdvice & advice, const CouncilContext & context, const CouncilV14ResourceState & state )
    {
        if ( advice.kind == StrategicAdviceKind::RECRUIT_HERO ) {
            return "[HERO]";
        }
        if ( advice.kind == StrategicAdviceKind::MANA_RESTORATION ) {
            return "[MANA]";
        }
        if ( advice.kind == StrategicAdviceKind::SHRINE_STUDY ) {
            return "[SPELL]";
        }
        return councilV14TypeLabel( advice, context, state );
    }

    enum class CouncilV20SpendMode : uint8_t
    {
        NONE,
        FULL,
        PARTIAL,
        DEFER
    };

    struct CouncilV20SpendPlan
    {
        CouncilV20SpendMode mode{ CouncilV20SpendMode::NONE };
        Funds cost;
        uint32_t requestedRecruitCount{ 0 };
        uint32_t plannedRecruitCount{ 0 };
    };

    CouncilV20SpendPlan councilV20PlanSpend( Kingdom & kingdom, const StrategicAdvice & advice, const Funds & remaining )
    {
        CouncilV20SpendPlan plan;
        Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex );
        if ( castle == nullptr ) {
            return plan;
        }

        if ( advice.action == AdviceAction::BUILD_STRUCTURE ) {
            if ( advice.buildingType == BUILD_NOTHING || !castle->AllowBuyBuilding( advice.buildingType ) ) {
                return plan;
            }

            plan.cost = PaymentConditions::BuyBuilding( castle->GetRace(), advice.buildingType );
            plan.mode = remaining >= plan.cost ? CouncilV20SpendMode::FULL : CouncilV20SpendMode::DEFER;
            return plan;
        }

        if ( advice.action != AdviceAction::RECRUIT_CREATURES ) {
            return plan;
        }

        const uint32_t actualDwelling = castle->GetActualDwelling( advice.dwellingType );
        if ( actualDwelling == BUILD_NOTHING || !castle->isBuild( actualDwelling ) ) {
            return plan;
        }

        const Monster monster( castle->GetRace(), actualDwelling );
        if ( !hasRecruitmentRoom( *castle, monster ) ) {
            return plan;
        }

        const uint32_t available = castle->getMonstersInDwelling( actualDwelling );
        plan.requestedRecruitCount = std::min( advice.recruitCount, available );
        if ( plan.requestedRecruitCount == 0 ) {
            return plan;
        }

        const uint32_t affordableFromRemaining = castle->getRecruitLimit( monster, remaining );
        plan.plannedRecruitCount = std::min( plan.requestedRecruitCount, affordableFromRemaining );
        if ( plan.plannedRecruitCount == 0 ) {
            plan.mode = CouncilV20SpendMode::DEFER;
            return plan;
        }

        plan.cost = monster.GetCost() * plan.plannedRecruitCount;
        plan.mode = plan.plannedRecruitCount >= plan.requestedRecruitCount ? CouncilV20SpendMode::FULL : CouncilV20SpendMode::PARTIAL;
        return plan;
    }

    void applyCouncilV20SpendCommitments( std::vector<StrategicAdvice> & candidates, Kingdom & kingdom, const CouncilContext & context )
    {
        std::vector<size_t> spendIndexes;
        spendIndexes.reserve( candidates.size() );
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            if ( isCouncilResourceSpend( candidates[index] ) && std::isfinite( candidates[index].score ) && candidates[index].score > 0.0 ) {
                spendIndexes.emplace_back( index );
            }
        }
        if ( spendIndexes.size() < 2 ) {
            return;
        }

        std::sort( spendIndexes.begin(), spendIndexes.end(), [&]( const size_t left, const size_t right ) {
            return councilV14DeterministicLess( candidates[left], candidates[right] );
        } );

        const double bestSpendScore = candidates[spendIndexes.front()].score;
        Funds remaining = kingdom.GetFunds();
        size_t committedCount = 0;

        for ( const size_t index : spendIndexes ) {
            StrategicAdvice & advice = candidates[index];

            // Weak alternatives should not consume the simulated pool ahead of stronger work.
            const bool worthPlanning = advice.score >= std::max( 3200.0, bestSpendScore * 0.70 );
            if ( !worthPlanning ) {
                advice.score *= 0.82;
                continue;
            }

            const CouncilV20SpendPlan plan = councilV20PlanSpend( kingdom, advice, remaining );
            if ( plan.mode == CouncilV20SpendMode::NONE ) {
                advice.score = 0.0;
                continue;
            }

            if ( plan.mode == CouncilV20SpendMode::DEFER ) {
                advice.score *= ( context.lowGold || context.lowMaterials ) ? 0.46 : 0.64;
                advice.rationale = "DEFER. Stronger spending uses this budget. | " + advice.rationale;
                continue;
            }

            remaining -= plan.cost;
            ++committedCount;

            if ( plan.mode == CouncilV20SpendMode::PARTIAL ) {
                const double fraction = static_cast<double>( plan.plannedRecruitCount ) / std::max<uint32_t>( 1, plan.requestedRecruitCount );
                advice.score *= 0.78 + fraction * 0.18;
                advice.rationale = "PARTIAL " + std::to_string( plan.plannedRecruitCount ) + "/" + std::to_string( plan.requestedRecruitCount )
                                   + " fits after earlier spend. | " + advice.rationale;
            }
            else if ( committedCount > 1 ) {
                advice.rationale = "FULL. Fits after earlier spend. | " + advice.rationale;
            }
        }
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV20( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
    {
        const CouncilContext context = buildCouncilContext( kingdom );
        std::vector<StrategicAdvice> rawCandidates = gatherCouncilCandidates( kingdom, context );
        appendCouncilV20HeroRecruitmentAdvice( rawCandidates, kingdom, context );

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
        filterCouncilV20SanityAndPruning( candidates, kingdom );


        applyCouncilSequencePlanning( candidates );
        applyCouncilResourceReservation( candidates, context );
        applyCouncilV9Continuity( candidates );
        applyCouncilObjectiveReservations( candidates, context );
        applyCouncilV9Intelligence( candidates, context );
        applyCouncilV11Intelligence( candidates, context );
        applyCouncilV17LiveCombat( candidates, kingdom );
        applyCouncilV19AreaCoordination( candidates, kingdom, context );
        applyCouncilV18FollowThrough( candidates, kingdom );

        applyCouncilV14ResourceJudgment( candidates, context );
        suppressCouncilV10NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );
        // Unlike v15, recruitment affordability is recalculated against the resources remaining
        // after each stronger commitment. Do not mutate advice.recruitCount: the order is fully
        // affordable if selected now; PARTIAL describes compatibility with the simulated prior plan.
        applyCouncilV20SpendCommitments( candidates, kingdom, context );

        pruneCouncilV11WeakAlternatives( candidates );
        pruneCouncilV14OwnerEchoes( candidates );

        return chooseCouncilV14Advice( std::move( candidates ) );
    }

    int selectStrategicAdviceV20( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
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

                    // Crisp visual representation: Hero portrait for heroes, Castle icon for towns/castles
                    if ( const Heroes * hero = world.GetHeroes( advice.heroId ) ) {
                        hero->PortraitRedraw( rows[index].x + 6, rows[index].y + 1, PORT_SMALL, display );
                        textX = rows[index].x + 52;
                        textWidth = rows[index].width - 61;
                    }
                    else if ( const Castle * castle = getOwnedAdviceCastle( kingdom, advice.targetIndex ) ) {
                        Interface::redrawCastleIcon( *castle, rows[index].x + 6, rows[index].y + 4 );
                        textX = rows[index].x + 52;
                        textWidth = rows[index].width - 61;
                    }

                    std::string headingText = hoverIndex == index ? "> " : "";
                    headingText += std::to_string( index + 1 ) + ". ";
                    const char * timing = councilV11TimingLabel( advice );
                    if ( timing[0] != '\0' ) {
                        headingText += std::string( timing ) + " ";
                    }
                    headingText += councilV20TypeTag( advice, context, resourceState );
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
                    fheroes2::Text reason( compactCouncilV20Rationale( advice ), reasonFont );
                    reason.fitToOneRow( textWidth );
                    reason.draw( textX, rows[index].y + 20, display );
                }

                fheroes2::Text hint( "1-5 execute | Right-click inspect | Esc close", fheroes2::FontType::smallWhite() );
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

            // Right-click order inspection (zero extra screen bloat)
            for ( size_t index = 0; index < visibleCount; ++index ) {
                if ( event.MouseClickRight( rows[index] ) ) {
                    const StrategicAdvice & advice = advices[index];
                    std::string detail = advice.title + "\n\n" + advice.rationale;
                    if ( advice.distance > 0 ) {
                        detail += "\n\nEstimated March: " + std::to_string( advice.distance ) + " movement points";
                        if ( advice.reachableThisTurn ) {
                            detail += " (Reachable this turn)";
                        }
                    }
                    fheroes2::showStandardTextMessage( "Council Order Details", detail, Dialog::OK );
                    redraw = true;
                    break;
                }
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

    AdviceExecutionResult executeStrategicAdviceV20( Kingdom & kingdom, Player & player, const StrategicAdvice & advice )
    {
        if ( advice.action == AdviceAction::RECRUIT_HERO ) {
            return executeRecruitHeroAdvice( kingdom, advice );
        }
        return executeStrategicAdviceV12( kingdom, player, advice );
    }

    void showRpgStrategicAdvisorV20()
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
        recentCouncilV18Plan = {};

        while ( true ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV20( kingdom, suppressedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV20( advices, councilReport, kingdom );

            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV20( kingdom, suppressedOrders );
            const auto refreshedIt = std::find_if( refreshed.begin(), refreshed.end(), [&]( const StrategicAdvice & advice ) {
                return adviceIdentity( advice ) == selectedIdentity;
            } );
            if ( refreshedIt == refreshed.end() ) {
                councilReport = "Situation changed; advice refreshed.";
                fheroes2::showStandardTextMessage( "War Council", councilReport, Dialog::OK );

                recentCouncilV18Plan = {};
                continue;
            }

            const StrategicAdvice current = *refreshedIt;
            const AdviceExecutionResult execution = executeStrategicAdviceV20( kingdom, *player, current );
            const bool objectResolved = councilV13ObjectInteractionResolved( kingdom, current );

            showCouncilV13ExecutionFeedback( current, execution );
            councilReport = councilV13ReportAfterExecution( current, execution, objectResolved );

            if ( !execution.success ) {
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
                suppressedOrders.insert( selectedIdentity );
                recentCouncilV18Plan = {};
                councilReport = "Order failed; advice refreshed.";
                continue;
            }

            // Clean, non-blocking in-dialog report feedback
            councilReport = execution.message.empty() ? "Order complete; advice refreshed." : execution.message;

                continue;
            }

            if ( councilV13UsesNativeObjectFeedback( current ) && !objectResolved ) {
                suppressedOrders.insert( selectedIdentity );
                recentCouncilV18Plan = {};
                councilReport = "Skipped that object choice for this council session.";
                continue;
            }

            // Clean, non-blocking in-dialog report feedback
            councilReport = execution.message.empty() ? "Order complete; advice refreshed." : execution.message;

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

            rememberCouncilV18Plan( current, kingdom );
        }
    }
}

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV20
