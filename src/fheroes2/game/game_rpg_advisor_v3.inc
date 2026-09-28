// War Council v3: context-aware strategic advice.
// The v2 advisor keeps all executable action generation and UI helpers; this layer replaces
// recommendation quality, wording, ranking history and the persistent council presentation.
#include "game_rpg_advisor_v2.inc"

namespace
{
    struct CouncilContext
    {
        double strongestHeroStrength{ 1.0 };
        int32_t mainHeroId{ -1 };
        bool lowGold{ false };
        bool lowMaterials{ false };
        size_t movableHeroes{ 0 };
    };

    std::map<int32_t, StrategicAdviceKind> recentCouncilHeroKinds;
    std::map<StrategicAdviceKind, size_t> recentCouncilKinds;
    std::set<int32_t> recentCouncilMovementTargets;

    CouncilContext buildCouncilContext( Kingdom & kingdom )
    {
        CouncilContext context;
        double bestHeroValue = -1.0;

        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() ) {
                continue;
            }

            const double armyStrength = std::max( 1.0, hero->GetArmy().GetStrength() );
            context.strongestHeroStrength = std::max( context.strongestHeroStrength, armyStrength );

            const double heroValue = armyStrength + static_cast<double>( hero->getStatsValue() ) * std::max( 25.0, armyStrength * 0.015 );
            if ( heroValue > bestHeroValue ) {
                bestHeroValue = heroValue;
                context.mainHeroId = hero->GetID();
            }

            if ( hero->GetMovePoints() > 0 && hero->MayStillMove( false, false ) ) {
                ++context.movableHeroes;
            }
        }

        const Funds & funds = kingdom.GetFunds();
        context.lowGold = funds.gold < 5000;
        context.lowMaterials = funds.wood < 10 || funds.ore < 10;
        return context;
    }

    bool isPremiumMovementKind( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
            return true;
        default:
            return false;
        }
    }

    bool isErrandMovementKind( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
            return true;
        default:
            return false;
        }
    }

    double movementOpportunityFactor( const StrategicAdvice & advice, const CouncilContext & context )
    {
        if ( advice.action != AdviceAction::MOVE_HERO ) {
            return 1.0;
        }

        const Heroes * hero = world.GetHeroes( advice.heroId );
        if ( hero == nullptr || hero->GetMovePoints() == 0 ) {
            return 0.0;
        }

        const double movementUse = std::clamp( static_cast<double>( advice.distance ) / std::max<uint32_t>( 1, hero->GetMovePoints() ), 0.0, 1.0 );
        double factor = isPremiumMovementKind( advice.kind ) ? 1.08 - movementUse * 0.10 : 1.08 - movementUse * 0.33;

        const double relativeStrength = hero->GetArmy().GetStrength() / std::max( 1.0, context.strongestHeroStrength );
        const bool mainHero = hero->GetID() == context.mainHeroId;

        if ( mainHero && isErrandMovementKind( advice.kind ) ) {
            factor *= movementUse > 0.45 ? 0.58 : 0.76;
        }
        else if ( !mainHero && relativeStrength < 0.68 && isErrandMovementKind( advice.kind ) ) {
            factor *= 1.20;
        }

        if ( advice.targetObjectType == MP2::OBJ_NONE
             && ( advice.kind == StrategicAdviceKind::ROAD_MARCH || advice.kind == StrategicAdviceKind::SAFE_ADVANCE
                  || advice.kind == StrategicAdviceKind::TERRITORIAL_PRESSURE ) ) {
            factor *= movementUse >= 0.60 ? 1.12 : 0.92;
        }

        return std::max( 0.15, factor );
    }

    double councilBenefitScore( const StrategicAdvice & advice, Kingdom & kingdom, const CouncilContext & context )
    {
        double score = advice.score;
        const Heroes * hero = world.GetHeroes( advice.heroId );
        const double relativeStrength = hero != nullptr ? hero->GetArmy().GetStrength() / std::max( 1.0, context.strongestHeroStrength ) : 0.0;
        const bool mainHero = hero != nullptr && hero->GetID() == context.mainHeroId;

        switch ( advice.kind ) {
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::DEFENSE:
            score *= 1.55;
            break;
        case StrategicAdviceKind::SIEGE:
            score *= 1.42;
            break;
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
            score *= 1.30;
            break;
        case StrategicAdviceKind::MONSTER_HUNT:
            score *= mainHero ? 1.22 : 1.08;
            break;
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
            score *= mainHero ? 1.34 : 1.16;
            break;
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
            score *= mainHero ? 1.25 : 1.08;
            break;
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
            score *= mainHero ? 0.66 : ( relativeStrength < 0.70 ? 1.15 : 0.92 );
            break;
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
            score *= mainHero ? 0.62 : ( relativeStrength < 0.70 ? 1.18 : 0.90 );
            break;
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
            score *= relativeStrength < 0.62 ? 1.04 : 0.34;
            break;
        case StrategicAdviceKind::TOWN_RESUPPLY:
            score *= relativeStrength < 0.78 ? 1.34 : 0.88;
            break;
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
            score *= mainHero ? 0.86 : 1.08;
            break;
        case StrategicAdviceKind::RECRUIT_TIER6:
            score *= 1.48;
            break;
        case StrategicAdviceKind::RECRUIT_TIER5:
            score *= 1.38;
            break;
        case StrategicAdviceKind::RECRUIT_TIER4:
            score *= 1.29;
            break;
        case StrategicAdviceKind::RECRUIT_TIER3:
            score *= 1.19;
            break;
        case StrategicAdviceKind::RECRUIT_TIER2:
            score *= 1.10;
            break;
        case StrategicAdviceKind::RECRUIT_TIER1:
            score *= 1.02;
            break;
        case StrategicAdviceKind::BUILD_CASTLE:
            score *= 1.42;
            break;
        case StrategicAdviceKind::BUILD_DWELLING:
        case StrategicAdviceKind::UPGRADE_DWELLING:
            score *= 1.26;
            break;
        case StrategicAdviceKind::BUILD_ECONOMY:
        case StrategicAdviceKind::BUILD_MARKETPLACE:
            score *= ( context.lowGold || context.lowMaterials ) ? 1.34 : 0.88;
            break;
        case StrategicAdviceKind::BUILD_DEFENSE:
            score *= 0.82;
            break;
        case StrategicAdviceKind::BUILD_MAGIC_GUILD:
            score *= fheroes2::RPG::spellcastingInvestmentPercent( kingdom.GetColor() ) >= 25.0 ? 1.16 : 0.92;
            break;
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
            score *= 1.34;
            break;
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
            score *= 1.22;
            break;
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
            score *= 1.10;
            break;
        case StrategicAdviceKind::TOWN_FEED_GARRISON:
            score *= 0.92;
            break;
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
            score *= 1.38;
            break;
        case StrategicAdviceKind::HERO_FAST_RESERVE:
            score *= 1.03;
            break;
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
            score *= 0.58;
            break;
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
            score *= 0.66;
            break;
        default:
            break;
        }

        score *= movementOpportunityFactor( advice, context );

        const auto recentHero = recentCouncilHeroKinds.find( advice.heroId );
        if ( recentHero != recentCouncilHeroKinds.end() && recentHero->second == advice.kind ) {
            score *= 0.48;
        }
        const auto recentKind = recentCouncilKinds.find( advice.kind );
        if ( recentKind != recentCouncilKinds.end() ) {
            score *= 1.0 / ( 1.0 + static_cast<double>( recentKind->second ) * 0.28 );
        }
        if ( advice.action == AdviceAction::MOVE_HERO && recentCouncilMovementTargets.count( advice.targetIndex ) > 0 ) {
            score *= 0.40;
        }

        return score;
    }

    std::string councilAdvisorNarrative( const StrategicAdvice & advice, const CouncilContext & context )
    {
        const Heroes * hero = world.GetHeroes( advice.heroId );
        const bool mainHero = hero != nullptr && hero->GetID() == context.mainHeroId;
        std::string text;

        switch ( advice.kind ) {
        case StrategicAdviceKind::SIEGE:
            text = "Take permanent control now; the route and army edge justify committing this hero.";
            break;
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::DEFENSE:
            text = "Protect the kingdom first; losing this position costs more than any nearby pickup.";
            break;
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
            text = "Secure permanent income now; this keeps paying after today's movement is gone.";
            break;
        case StrategicAdviceKind::MONSTER_HUNT:
            text = "Take the favorable fight; gain experience and access without gambling the army.";
            break;
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
            text = mainHero ? "Put permanent power on the main hero while the detour still fits this turn."
                            : "Use a support hero to recover durable power without slowing the main army.";
            break;
        case StrategicAdviceKind::HERO_TRAINING:
            text = "Convert movement into permanent hero stats; this has better long-term value than empty travel.";
            break;
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
            text = "Improve spell readiness now while the visit is efficient and immediately reachable.";
            break;
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
            text = mainHero ? "Only take this because it is cheap; do not sacrifice main-army tempo for small loot."
                            : "Good support-hero work: collect value while preserving the main hero's tempo.";
            break;
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
            text = mainHero ? "Scout only because no stronger main-army objective currently beats this route."
                            : "Use spare support movement to reveal choices for the next action, not just empty ground.";
            break;
        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
            text = "Spend today's remaining movement closing on a real high-value objective for next turn.";
            break;
        case StrategicAdviceKind::TOWN_RESUPPLY:
            text = "Concentrate useful troops on the field hero; army strength is worth more than idle garrison power.";
            break;
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
            text = "Leave a useful defender only where it improves kingdom safety without crippling the field army.";
            break;
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
            text = "Move the best idle town troops onto the active hero so combat power is actually usable.";
            break;
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
            text = "Shed slow stacks to improve next-day movement while preserving a legal fast field army.";
            break;
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
            text = "Put the stronger complete army on the hero who can use it on the map immediately.";
            break;
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
            text = "Concentrate combat power on the better hero; avoid splitting strength between two mediocre armies.";
            break;
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
            text = "Concentrate valuable artifacts on the hero most likely to fight decisive battles.";
            break;
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
            text = "Consolidate troops and artifacts together so the main hero gets the full power spike now.";
            break;
        case StrategicAdviceKind::RECRUIT_TIER4:
        case StrategicAdviceKind::RECRUIT_TIER5:
        case StrategicAdviceKind::RECRUIT_TIER6:
            text = "Buy high-impact creatures while they are affordable; unused growth is wasted combat power.";
            break;
        case StrategicAdviceKind::RECRUIT_TIER1:
        case StrategicAdviceKind::RECRUIT_TIER2:
        case StrategicAdviceKind::RECRUIT_TIER3:
            text = "Recruit because the creatures fit the army and the purchase gives useful strength immediately.";
            break;
        case StrategicAdviceKind::BUILD_CASTLE:
        case StrategicAdviceKind::BUILD_DWELLING:
        case StrategicAdviceKind::UPGRADE_DWELLING:
            text = "Invest in army growth now; this structure increases future combat power rather than just convenience.";
            break;
        case StrategicAdviceKind::BUILD_ECONOMY:
        case StrategicAdviceKind::BUILD_MARKETPLACE:
            text = ( context.lowGold || context.lowMaterials ) ? "Fix the current resource bottleneck so future recruiting and building do not stall."
                                                               : "Take this economic upgrade only because stronger military spending is not available now.";
            break;
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
            text = "Clean the army layout only because it frees a slot for more valuable troop management.";
            break;
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
            text = "Create tactical 1-stacks only when there is spare room and no stronger strategic action competes.";
            break;
        default:
            text = advice.rationale;
            break;
        }

        if ( advice.action == AdviceAction::MOVE_HERO && hero != nullptr && hero->GetMovePoints() > 0 ) {
            const int movementPercent = static_cast<int>( std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 ) * 100.0 );
            text += " (" + std::to_string( movementPercent ) + "% move)";
        }

        return text;
    }

    std::vector<StrategicAdvice> gatherCouncilCandidates( Kingdom & kingdom, const CouncilContext & context )
    {
        std::vector<StrategicAdvice> candidates;
        appendDefensiveAdvice( candidates, kingdom, context.strongestHeroStrength );
        appendLogisticsAdvice( candidates, kingdom, context.strongestHeroStrength );
        appendTownTravelAdvice( candidates, kingdom, context.strongestHeroStrength );
        appendTownArmyManagementAdvice( candidates, kingdom );
        appendHeroExchangeAdvice( candidates, kingdom );
        appendRecruitmentAdvice( candidates, kingdom );
        appendBuildingAdvice( candidates, kingdom );

        const int32_t mapSize = world.w() * world.h();
        for ( Heroes * hero : kingdom.GetHeroes() ) {
            if ( hero == nullptr || !hero->isActive() || hero->GetMovePoints() == 0 || !hero->MayStillMove( false, false ) ) {
                continue;
            }

            AIWorldPathfinder pathfinder;
            pathfinder.setMinimalArmyStrengthAdvantage( 1.15 );
            pathfinder.setSpellPointsReserveRatio( 0.30 );
            pathfinder.reEvaluateIfNeeded( *hero );

            for ( int32_t index = 0; index < mapSize; ++index ) {
                if ( index == hero->GetIndex() ) {
                    continue;
                }
                const Maps::Tile & tile = world.getTile( index );
                if ( tile.isFog( kingdom.GetColor() ) || !isCandidateObjectUseful( *hero, kingdom, tile ) ) {
                    continue;
                }

                StrategicAdvice advice = buildObjectiveAdvice( *hero, kingdom, pathfinder, tile, context.strongestHeroStrength );
                if ( advice.heroId >= 0 && advice.targetIndex >= 0 && advice.reachableThisTurn && advice.score > 0.0 && std::isfinite( advice.score ) ) {
                    candidates.emplace_back( std::move( advice ) );
                }
            }
        }

        appendExplorationAdvice( candidates, kingdom, context.strongestHeroStrength );
        appendGoalDirectedStagingAdvice( candidates, kingdom, context.strongestHeroStrength );
        return candidates;
    }

    std::vector<StrategicAdvice> scoreCouncilCandidates( std::vector<StrategicAdvice> candidates, Kingdom & kingdom, const CouncilContext & context )
    {
        std::vector<StrategicAdvice> filtered;
        filtered.reserve( candidates.size() );

        for ( StrategicAdvice & advice : candidates ) {
            if ( isRepeatableLowValueCouncilAdvice( advice.kind ) ) {
                continue;
            }

            advice.score = councilBenefitScore( advice, kingdom, context );
            if ( advice.score < 2800.0 || !std::isfinite( advice.score ) ) {
                continue;
            }

            advice.rationale = councilAdvisorNarrative( advice, context );
            filtered.emplace_back( std::move( advice ) );
        }

        return filtered;
    }

    std::vector<StrategicAdvice> generateStrategicAdvice( Kingdom & kingdom )
    {
        const CouncilContext context = buildCouncilContext( kingdom );
        std::vector<StrategicAdvice> filtered = scoreCouncilCandidates( gatherCouncilCandidates( kingdom, context ), kingdom, context );
        std::vector<StrategicAdvice> chosen = chooseCouncilAdvice( filtered );

        if ( chosen.size() < strategicAdviceCount && hasMovableCouncilHeroes( kingdom ) ) {
            const size_t oldSize = filtered.size();
            appendFrontierBenefitAdvice( filtered, kingdom );

            for ( size_t index = oldSize; index < filtered.size(); ++index ) {
                StrategicAdvice & advice = filtered[index];
                advice.score = councilBenefitScore( advice, kingdom, context );
                advice.rationale = councilAdvisorNarrative( advice, context );
            }
            chosen = chooseCouncilAdvice( std::move( filtered ) );
        }

        return chosen;
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
            std::vector<StrategicAdvice> advices = generateStrategicAdvice( kingdom );
            if ( !failedOrders.empty() ) {
                advices.erase( std::remove_if( advices.begin(), advices.end(), [&]( const StrategicAdvice & advice ) {
                                   return failedOrders.count( adviceIdentity( advice ) ) > 0;
                               } ),
                               advices.end() );
            }

            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   "The council can find no action with a worthwhile expected benefit from the current position. Your turn remains active.",
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
