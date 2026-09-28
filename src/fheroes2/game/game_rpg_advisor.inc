// War Council v8: concise, risk-aware strategic signals.
// v7 keeps heroes committed to strong plans. This layer adds compact advice types, sharper
// risk and tempo scoring, recommendation diversity, town-only continuity and a tighter council UI.
#include "game_rpg_advisor_v6.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilIntent
    {
        NONE,
        DECISIVE,
        ECONOMY,
        DEVELOPMENT,
        POSITIONING,
        LOGISTICS,
        ERRAND
    };

    enum class CouncilAdviceType : uint8_t
    {
        THREAT,
        ATTACK,
        ECONOMY,
        POWER,
        ARMY,
        TEMPO,
        SCOUT,
        LOGISTICS,
        GENERAL
    };

    std::map<int32_t, CouncilIntent> recentCouncilHeroIntents;

    CouncilIntent councilIntentForAdvice( const StrategicAdvice & advice )
    {
        switch ( advice.kind ) {
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::DEFENSE:
            return CouncilIntent::DECISIVE;

        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
            return CouncilIntent::ECONOMY;

        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
            return CouncilIntent::DEVELOPMENT;

        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
            return CouncilIntent::POSITIONING;

        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
        case StrategicAdviceKind::TOWN_FEED_GARRISON:
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
        case StrategicAdviceKind::HERO_FAST_RESERVE:
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
            return CouncilIntent::LOGISTICS;

        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
            return CouncilIntent::ERRAND;

        default:
            return CouncilIntent::NONE;
        }
    }

    int councilIntentPriority( const CouncilIntent intent )
    {
        switch ( intent ) {
        case CouncilIntent::DECISIVE:
            return 5;
        case CouncilIntent::ECONOMY:
        case CouncilIntent::DEVELOPMENT:
            return 4;
        case CouncilIntent::POSITIONING:
            return 3;
        case CouncilIntent::LOGISTICS:
            return 2;
        case CouncilIntent::ERRAND:
            return 1;
        default:
            return 0;
        }
    }

    bool isCouncilDurableIntent( const CouncilIntent intent )
    {
        return intent == CouncilIntent::DECISIVE || intent == CouncilIntent::ECONOMY || intent == CouncilIntent::DEVELOPMENT;
    }

    void applyCouncilCommitmentContinuity( std::vector<StrategicAdvice> & candidates )
    {
        for ( StrategicAdvice & advice : candidates ) {
            if ( advice.heroId < 0 || advice.action != AdviceAction::MOVE_HERO ) {
                continue;
            }

            const auto previousIt = recentCouncilHeroIntents.find( advice.heroId );
            if ( previousIt == recentCouncilHeroIntents.end() ) {
                continue;
            }

            const CouncilIntent previous = previousIt->second;
            const CouncilIntent next = councilIntentForAdvice( advice );
            if ( previous == CouncilIntent::NONE || next == CouncilIntent::NONE ) {
                continue;
            }

            if ( previous == next ) {
                if ( isCouncilDurableIntent( next ) ) {
                    advice.score *= 1.05;
                }
                continue;
            }

            if ( previous == CouncilIntent::POSITIONING && isCouncilDurableIntent( next ) ) {
                advice.score *= 1.12;
                advice.rationale = "Follows through on the staged plan. | " + advice.rationale;
                continue;
            }

            if ( previous == CouncilIntent::ERRAND && isCouncilDurableIntent( next ) ) {
                advice.score *= 1.08;
                advice.rationale = "Upgrades this hero to a more important objective. | " + advice.rationale;
                continue;
            }

            if ( previous == CouncilIntent::DECISIVE && next == CouncilIntent::ERRAND ) {
                advice.score *= 0.55;
                advice.rationale = "Avoid abandoning decisive work for a minor errand. | " + advice.rationale;
                continue;
            }

            if ( ( previous == CouncilIntent::ECONOMY || previous == CouncilIntent::DEVELOPMENT ) && next == CouncilIntent::ERRAND ) {
                advice.score *= 0.68;
                advice.rationale = "Keep the stronger strategic line unless this errand becomes clearly better. | " + advice.rationale;
                continue;
            }

            const int previousPriority = councilIntentPriority( previous );
            const int nextPriority = councilIntentPriority( next );
            if ( nextPriority >= previousPriority + 2 ) {
                advice.score *= 1.05;
            }
            else if ( previousPriority >= nextPriority + 2 ) {
                advice.score *= 0.78;
            }
        }
    }

    CouncilAdviceType councilAdviceTypeForKind( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::DEFENSE:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::INTERCEPTION:
            return CouncilAdviceType::THREAT;

        case StrategicAdviceKind::OFFENSE:
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
            return CouncilAdviceType::ATTACK;

        case StrategicAdviceKind::ECONOMY:
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::BUILD_ECONOMY:
        case StrategicAdviceKind::BUILD_MARKETPLACE:
            return CouncilAdviceType::ECONOMY;

        case StrategicAdviceKind::DEVELOPMENT:
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
        case StrategicAdviceKind::BUILD_MAGIC_GUILD:
            return CouncilAdviceType::POWER;

        case StrategicAdviceKind::RECRUIT_TIER1:
        case StrategicAdviceKind::RECRUIT_TIER2:
        case StrategicAdviceKind::RECRUIT_TIER3:
        case StrategicAdviceKind::RECRUIT_TIER4:
        case StrategicAdviceKind::RECRUIT_TIER5:
        case StrategicAdviceKind::RECRUIT_TIER6:
        case StrategicAdviceKind::BUILD_CASTLE:
        case StrategicAdviceKind::BUILD_DWELLING:
        case StrategicAdviceKind::UPGRADE_DWELLING:
        case StrategicAdviceKind::BUILD_WELL:
        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
            return CouncilAdviceType::ARMY;

        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
            return CouncilAdviceType::TEMPO;

        case StrategicAdviceKind::EXPLORATION:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::FLANKING_MARCH:
            return CouncilAdviceType::SCOUT;

        case StrategicAdviceKind::LOGISTICS:
        case StrategicAdviceKind::REGROUP:
        case StrategicAdviceKind::TOWN_PULL_STRONGEST:
        case StrategicAdviceKind::TOWN_SEND_SLOWEST:
        case StrategicAdviceKind::TOWN_SWAP_ARMIES:
        case StrategicAdviceKind::TOWN_FEED_GARRISON:
        case StrategicAdviceKind::TOWN_MERGE_HERO:
        case StrategicAdviceKind::TOWN_MERGE_GARRISON:
        case StrategicAdviceKind::TOWN_SPLIT_HERO:
        case StrategicAdviceKind::TOWN_SPLIT_GARRISON:
        case StrategicAdviceKind::TOWN_OPTIMIZE_HERO:
        case StrategicAdviceKind::TOWN_OPTIMIZE_GARRISON:
        case StrategicAdviceKind::HERO_ARMY_TO_MAIN:
        case StrategicAdviceKind::HERO_ARTIFACTS_TO_MAIN:
        case StrategicAdviceKind::HERO_FULL_EXCHANGE:
        case StrategicAdviceKind::HERO_SWAP_ARMIES:
        case StrategicAdviceKind::HERO_MERGE_MAIN:
        case StrategicAdviceKind::HERO_MERGE_SUPPORT:
        case StrategicAdviceKind::HERO_SPLIT_MAIN:
        case StrategicAdviceKind::HERO_SPLIT_SUPPORT:
        case StrategicAdviceKind::HERO_OPTIMIZE_PAIR:
        case StrategicAdviceKind::HERO_FAST_RESERVE:
            return CouncilAdviceType::LOGISTICS;

        default:
            return CouncilAdviceType::GENERAL;
        }
    }

    const char * councilAdviceTypeLabel( const CouncilAdviceType type )
    {
        switch ( type ) {
        case CouncilAdviceType::THREAT: return "THREAT";
        case CouncilAdviceType::ATTACK: return "ATTACK";
        case CouncilAdviceType::ECONOMY: return "ECON";
        case CouncilAdviceType::POWER: return "POWER";
        case CouncilAdviceType::ARMY: return "ARMY";
        case CouncilAdviceType::TEMPO: return "TEMPO";
        case CouncilAdviceType::SCOUT: return "SCOUT";
        case CouncilAdviceType::LOGISTICS: return "LOGISTICS";
        default: return "GENERAL";
        }
    }

    const char * councilUrgencyLabel( const double score )
    {
        if ( score >= 17500.0 ) return "NOW";
        if ( score >= 11500.0 ) return "HIGH";
        if ( score >= 7000.0 ) return "GOOD";
        return "OPTION";
    }

    bool isCouncilCombatAdvice( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::OFFENSE:
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
            return true;
        default:
            return false;
        }
    }

    bool isCouncilLowerTierRecruitment( const StrategicAdviceKind kind )
    {
        return kind == StrategicAdviceKind::RECRUIT_TIER1 || kind == StrategicAdviceKind::RECRUIT_TIER2 || kind == StrategicAdviceKind::RECRUIT_TIER3;
    }

    bool isCouncilUpperTierRecruitment( const StrategicAdviceKind kind )
    {
        return kind == StrategicAdviceKind::RECRUIT_TIER4 || kind == StrategicAdviceKind::RECRUIT_TIER5 || kind == StrategicAdviceKind::RECRUIT_TIER6;
    }

    void applyCouncilSignalIntelligence( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        for ( StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                advice.score = 0.0;
                continue;
            }

            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            const bool mainHero = hero != nullptr && hero->GetID() == context.mainHeroId;
            const double relativeStrength = hero != nullptr ? hero->GetArmy().GetStrength() / std::max( 1.0, context.strongestHeroStrength ) : 0.0;

            switch ( councilAdviceTypeForKind( advice.kind ) ) {
            case CouncilAdviceType::THREAT:
                advice.score *= 1.12;
                break;

            case CouncilAdviceType::ATTACK:
                if ( mainHero ) {
                    advice.score *= 1.05;
                }
                break;

            case CouncilAdviceType::ECONOMY:
                if ( context.lowGold || context.lowMaterials ) {
                    advice.score *= 1.10;
                }
                break;

            case CouncilAdviceType::POWER:
                if ( mainHero ) {
                    advice.score *= 1.08;
                }
                break;

            case CouncilAdviceType::ARMY:
                if ( context.lowGold && isCouncilLowerTierRecruitment( advice.kind ) ) {
                    advice.score *= 0.78;
                }
                else if ( isCouncilUpperTierRecruitment( advice.kind ) ) {
                    advice.score *= 1.04;
                }
                break;

            case CouncilAdviceType::TEMPO:
                if ( advice.action == AdviceAction::MOVE_HERO && hero != nullptr && hero->GetMovePoints() > 0 ) {
                    const double movementUse = std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
                    if ( movementUse <= 0.35 ) {
                        advice.score *= 1.08;
                    }
                    else if ( mainHero && movementUse >= 0.82 ) {
                        advice.score *= 0.88;
                    }
                }
                break;

            case CouncilAdviceType::SCOUT:
                if ( mainHero ) {
                    advice.score *= 0.80;
                }
                else if ( relativeStrength < 0.70 ) {
                    advice.score *= 1.08;
                }
                break;

            case CouncilAdviceType::LOGISTICS:
                if ( advice.kind == StrategicAdviceKind::TOWN_PULL_STRONGEST || advice.kind == StrategicAdviceKind::HERO_ARMY_TO_MAIN
                     || advice.kind == StrategicAdviceKind::HERO_FULL_EXCHANGE ) {
                    advice.score *= 1.04;
                }
                break;

            default:
                break;
            }

            if ( isCouncilCombatAdvice( advice.kind ) && hero != nullptr && advice.targetIndex >= 0 ) {
                const Maps::Tile & targetTile = world.getTile( advice.targetIndex );
                const double defenders = defenderStrengthForAdvice( targetTile, *hero );
                if ( defenders > 0.0 ) {
                    const double combatEdge = hero->GetArmy().GetStrength() / defenders;
                    if ( combatEdge < 1.10 ) {
                        advice.score *= 0.20;
                    }
                    else if ( combatEdge < 1.25 ) {
                        advice.score *= 0.64;
                    }
                    else if ( combatEdge < 1.45 ) {
                        advice.score *= 0.88;
                    }
                    else if ( combatEdge >= 2.25 ) {
                        advice.score *= 1.05;
                    }
                }
            }
        }
    }

    void applyCouncilTypeDiversity( std::vector<StrategicAdvice> & candidates )
    {
        std::vector<size_t> rankedIndexes;
        rankedIndexes.reserve( candidates.size() );
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            rankedIndexes.emplace_back( index );
        }

        std::sort( rankedIndexes.begin(), rankedIndexes.end(), [&]( const size_t left, const size_t right ) {
            if ( candidates[left].score != candidates[right].score ) {
                return candidates[left].score > candidates[right].score;
            }
            return candidates[left].distance < candidates[right].distance;
        } );

        std::map<CouncilAdviceType, size_t> seenTypes;
        for ( const size_t index : rankedIndexes ) {
            StrategicAdvice & advice = candidates[index];
            const CouncilAdviceType type = councilAdviceTypeForKind( advice.kind );
            if ( type == CouncilAdviceType::THREAT || type == CouncilAdviceType::GENERAL ) {
                continue;
            }

            const size_t occurrence = seenTypes[type]++;
            if ( occurrence == 1 ) {
                advice.score *= 0.94;
            }
            else if ( occurrence == 2 ) {
                advice.score *= 0.86;
            }
            else if ( occurrence >= 3 ) {
                advice.score *= 0.78;
            }
        }
    }

    std::string firstCouncilSentence( const std::string & text )
    {
        const size_t sentenceEnd = text.find( ". " );
        if ( sentenceEnd == std::string::npos ) {
            return text;
        }
        return text.substr( 0, sentenceEnd + 1 );
    }

    std::string compactCouncilLine( std::string text, const size_t maxLength )
    {
        std::replace( text.begin(), text.end(), '\n', ' ' );
        if ( text.size() <= maxLength || maxLength < 4 ) {
            return text;
        }

        size_t cut = text.rfind( ' ', maxLength - 3 );
        if ( cut == std::string::npos || cut < maxLength / 2 ) {
            cut = maxLength - 3;
        }
        return text.substr( 0, cut ) + "...";
    }

    std::string compactCouncilRationale( const StrategicAdvice & advice )
    {
        static const std::string separator = " | ";
        const std::string & source = advice.rationale;
        const size_t firstSeparator = source.find( separator );
        if ( firstSeparator == std::string::npos ) {
            return compactCouncilLine( firstCouncilSentence( source ), 118 );
        }

        const size_t lastSeparator = source.rfind( separator );
        const std::string first = firstCouncilSentence( source.substr( 0, firstSeparator ) );
        const std::string last = firstCouncilSentence( source.substr( lastSeparator + separator.size() ) );
        if ( first.empty() || first == last ) {
            return compactCouncilLine( last, 118 );
        }
        return compactCouncilLine( first + " " + last, 118 );
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV8( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        applyCouncilCommitmentContinuity( candidates );
        applyCouncilObjectiveReservations( candidates, context );
        applyCouncilSignalIntelligence( candidates, context );
        applyCouncilTypeDiversity( candidates );
        // Recompute per-owner opportunity cost after every kingdom-wide modifier has settled.
        applyAlternativeComparison( candidates );

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );

        return chooseKingdomCouncilAdvice( std::move( candidates ) );
    }

    int selectStrategicAdviceV8( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
    {
        if ( advices.empty() ) {
            return -1;
        }

        const size_t visibleCount = std::min( strategicAdviceCount, advices.size() );
        const int32_t windowHeight = std::max<int32_t>( 226, 124 + static_cast<int32_t>( visibleCount ) * 46 );
        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();
        fheroes2::StandardWindow window( 604, windowHeight, true, display );
        const fheroes2::Rect area = window.activeArea();
        const bool isEvilInterface = Settings::Get().isEvilInterfaceEnabled();
        std::array<fheroes2::Rect, strategicAdviceCount> rows{};
        for ( size_t index = 0; index < visibleCount; ++index ) {
            rows[index] = { area.x + 12, area.y + 65 + static_cast<int32_t>( index ) * 46, area.width - 24, 42 };
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

                const fheroes2::Rect banner{ area.x + 8, area.y + 4, area.width - 16, 42 };
                window.applyTextBackgroundShading( banner );
                const fheroes2::Text title( "WAR COUNCIL", fheroes2::FontType::normalYellow() );
                title.draw( banner.x + 9, banner.y + 3, banner.width - 18, display );

                const CouncilContext context = buildCouncilContext( kingdom );
                std::string status = std::to_string( visibleCount ) + " options  |  " + std::to_string( context.movableHeroes ) + " heroes mobile";
                if ( const Heroes * mainHero = world.GetHeroes( context.mainHeroId ) ) {
                    status += "  |  Main: " + mainHero->GetName();
                }
                fheroes2::Text subtitle( status, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 18 );
                subtitle.draw( banner.x + 9, banner.y + 23, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( compactCouncilLine( councilReport, 104 ), fheroes2::FontType::smallYellow() );
                    report.fitToOneRow( area.width - 26 );
                    report.draw( area.x + 13, area.y + 49, display );
                }

                for ( size_t index = 0; index < visibleCount; ++index ) {
                    window.applyTextBackgroundShading( rows[index] );
                    const StrategicAdvice & advice = advices[index];
                    int32_t textX = rows[index].x + 9;
                    int32_t textWidth = rows[index].width - 18;
                    if ( const Heroes * hero = world.GetHeroes( advice.heroId ) ) {
                        hero->PortraitRedraw( rows[index].x + 6, rows[index].y + 2, PORT_SMALL, display );
                        textX = rows[index].x + 52;
                        textWidth = rows[index].width - 61;
                    }

                    std::string headingText = hoverIndex == index ? "> " : "";
                    headingText += std::to_string( index + 1 ) + ". " + councilUrgencyLabel( advice.score ) + " "
                                   + councilAdviceTypeLabel( councilAdviceTypeForKind( advice.kind ) ) + "  " + advice.title;
                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 4, display );

                    fheroes2::Text reason( compactCouncilRationale( advice ), fheroes2::FontType::smallWhite() );
                    reason.fitToOneRow( textWidth );
                    reason.draw( textX, rows[index].y + 22, display );
                }

                fheroes2::Text hint( "1-5 choose  |  Esc closes  |  Turn stays active", fheroes2::FontType::smallWhite() );
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

    void showRpgStrategicAdvisorV8()
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
        recentCouncilHeroIntents.clear();

        while ( true ) {
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV8( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV8( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const AdviceExecutionResult execution = executeStrategicAdvice( kingdom, *player, selected );
            councilReport = execution.message;
            if ( !execution.success ) {
                // Keep a failed order quarantined for the rest of this council session so an unrelated success
                // cannot immediately reintroduce the same broken recommendation.
                failedOrders.insert( adviceIdentity( selected ) );
                fheroes2::showStandardTextMessage( "War Council", execution.message, Dialog::OK );
                continue;
            }

            if ( selected.heroId >= 0 ) {
                recentCouncilHeroKinds[selected.heroId] = selected.kind;
                const CouncilIntent intent = councilIntentForAdvice( selected );
                if ( intent != CouncilIntent::NONE ) {
                    recentCouncilHeroIntents[selected.heroId] = intent;
                }
            }
            ++recentCouncilKinds[selected.kind];
            if ( selected.action == AdviceAction::MOVE_HERO && selected.targetIndex >= 0 ) {
                recentCouncilMovementTargets.insert( selected.targetIndex );
            }
        }
    }
}

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV8
