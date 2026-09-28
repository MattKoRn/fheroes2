// War Council v9: confidence-aware, compact strategic advice.
// This layer stays isolated from the base game. It refines v6 candidate generation with
// continuity, contextual urgency, risk-aware combat scoring, near-duplicate suppression,
// support-hero specialization and a compact single-window presentation.
#include "game_rpg_advisor_v6.inc"
#undef showRpgStrategicAdvisor

namespace
{
    enum class CouncilV9Intent : uint8_t
    {
        NONE,
        DECISIVE,
        ECONOMY,
        DEVELOPMENT,
        POSITIONING,
        SUPPORT,
        ERRAND,
        RECOVERY
    };

    enum class CouncilV9Type : uint8_t
    {
        DEFEND,
        STRIKE,
        GROW,
        POWER,
        ARMY,
        TEMPO,
        SCOUT,
        SUPPORT,
        RECOVER,
        GENERAL
    };

    enum class CouncilV9Risk : uint8_t
    {
        SOLID,
        CHECK,
        RISK
    };

    std::map<int32_t, CouncilV9Intent> recentCouncilV9HeroIntents;

    CouncilV9Intent councilV9IntentForAdvice( const StrategicAdvice & advice )
    {
        switch ( advice.kind ) {
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::DEFENSE:
        case StrategicAdviceKind::OFFENSE:
            return CouncilV9Intent::DECISIVE;

        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
        case StrategicAdviceKind::BUILD_ECONOMY:
        case StrategicAdviceKind::BUILD_MARKETPLACE:
            return CouncilV9Intent::ECONOMY;

        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
        case StrategicAdviceKind::BUILD_MAGIC_GUILD:
            return CouncilV9Intent::DEVELOPMENT;

        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
            return CouncilV9Intent::POSITIONING;

        case StrategicAdviceKind::REGROUP:
            return CouncilV9Intent::RECOVERY;

        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
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
            return CouncilV9Intent::SUPPORT;

        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::EXPLORATION:
            return CouncilV9Intent::ERRAND;

        default:
            return CouncilV9Intent::NONE;
        }
    }

    CouncilV9Type councilV9TypeForKind( const StrategicAdviceKind kind )
    {
        switch ( kind ) {
        case StrategicAdviceKind::DEFENSE:
        case StrategicAdviceKind::CASTLE_RELIEF:
        case StrategicAdviceKind::INTERCEPTION:
        case StrategicAdviceKind::BUILD_DEFENSE:
            return CouncilV9Type::DEFEND;

        case StrategicAdviceKind::OFFENSE:
        case StrategicAdviceKind::SIEGE:
        case StrategicAdviceKind::MONSTER_HUNT:
        case StrategicAdviceKind::FRONTIER_BREAKTHROUGH:
        case StrategicAdviceKind::TERRITORIAL_PRESSURE:
            return CouncilV9Type::STRIKE;

        case StrategicAdviceKind::ECONOMY:
        case StrategicAdviceKind::MINE_CAPTURE:
        case StrategicAdviceKind::ECONOMIC_EXPANSION:
        case StrategicAdviceKind::RESOURCE_RUN:
        case StrategicAdviceKind::TREASURE_HUNT:
        case StrategicAdviceKind::SUPPLY_SWEEP:
        case StrategicAdviceKind::BUILD_ECONOMY:
        case StrategicAdviceKind::BUILD_MARKETPLACE:
        case StrategicAdviceKind::BUILD_TAVERN:
        case StrategicAdviceKind::BUILD_SPECIAL:
            return CouncilV9Type::GROW;

        case StrategicAdviceKind::DEVELOPMENT:
        case StrategicAdviceKind::ARTIFACT_RECOVERY:
        case StrategicAdviceKind::HERO_TRAINING:
        case StrategicAdviceKind::SPELL_PILGRIMAGE:
        case StrategicAdviceKind::MAGIC_DEVELOPMENT:
        case StrategicAdviceKind::BUILD_MAGIC_GUILD:
            return CouncilV9Type::POWER;

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
            return CouncilV9Type::ARMY;

        case StrategicAdviceKind::ROAD_MARCH:
        case StrategicAdviceKind::SAFE_ADVANCE:
            return CouncilV9Type::TEMPO;

        case StrategicAdviceKind::EXPLORATION:
        case StrategicAdviceKind::FOG_SWEEP:
        case StrategicAdviceKind::FLANKING_MARCH:
            return CouncilV9Type::SCOUT;

        case StrategicAdviceKind::REGROUP:
            return CouncilV9Type::RECOVER;

        case StrategicAdviceKind::LOGISTICS:
        case StrategicAdviceKind::TRAVEL_TO_TOWN:
        case StrategicAdviceKind::TOWN_RESUPPLY:
        case StrategicAdviceKind::GARRISON_REINFORCEMENT:
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
            return CouncilV9Type::SUPPORT;

        default:
            return CouncilV9Type::GENERAL;
        }
    }

    const char * councilV9TypeLabel( const CouncilV9Type type )
    {
        switch ( type ) {
        case CouncilV9Type::DEFEND: return "DEFEND";
        case CouncilV9Type::STRIKE: return "STRIKE";
        case CouncilV9Type::GROW: return "GROW";
        case CouncilV9Type::POWER: return "POWER";
        case CouncilV9Type::ARMY: return "ARMY";
        case CouncilV9Type::TEMPO: return "TEMPO";
        case CouncilV9Type::SCOUT: return "SCOUT";
        case CouncilV9Type::SUPPORT: return "SUPPORT";
        case CouncilV9Type::RECOVER: return "RECOVER";
        default: return "GENERAL";
        }
    }

    bool isCouncilV9CombatAdvice( const StrategicAdviceKind kind )
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

    bool isCouncilV9Errand( const StrategicAdviceKind kind )
    {
        return kind == StrategicAdviceKind::RESOURCE_RUN || kind == StrategicAdviceKind::TREASURE_HUNT || kind == StrategicAdviceKind::SUPPLY_SWEEP
               || kind == StrategicAdviceKind::FOG_SWEEP || kind == StrategicAdviceKind::EXPLORATION;
    }

    int councilV9IntentPriority( const CouncilV9Intent intent )
    {
        switch ( intent ) {
        case CouncilV9Intent::DECISIVE: return 6;
        case CouncilV9Intent::RECOVERY: return 5;
        case CouncilV9Intent::ECONOMY:
        case CouncilV9Intent::DEVELOPMENT: return 4;
        case CouncilV9Intent::POSITIONING: return 3;
        case CouncilV9Intent::SUPPORT: return 2;
        case CouncilV9Intent::ERRAND: return 1;
        default: return 0;
        }
    }

    void applyCouncilV9Continuity( std::vector<StrategicAdvice> & candidates )
    {
        for ( StrategicAdvice & advice : candidates ) {
            if ( advice.heroId < 0 || advice.action != AdviceAction::MOVE_HERO ) {
                continue;
            }

            const auto previousIt = recentCouncilV9HeroIntents.find( advice.heroId );
            if ( previousIt == recentCouncilV9HeroIntents.end() ) {
                continue;
            }

            const CouncilV9Intent previous = previousIt->second;
            const CouncilV9Intent next = councilV9IntentForAdvice( advice );
            if ( previous == CouncilV9Intent::NONE || next == CouncilV9Intent::NONE ) {
                continue;
            }

            if ( previous == next ) {
                advice.score *= ( next == CouncilV9Intent::DECISIVE || next == CouncilV9Intent::ECONOMY || next == CouncilV9Intent::DEVELOPMENT ) ? 1.05 : 1.02;
                continue;
            }

            const int previousPriority = councilV9IntentPriority( previous );
            const int nextPriority = councilV9IntentPriority( next );
            if ( nextPriority >= previousPriority + 2 ) {
                advice.score *= 1.08;
                advice.rationale = "A stronger objective is now available. | " + advice.rationale;
            }
            else if ( previousPriority >= nextPriority + 2 ) {
                advice.score *= 0.70;
                advice.rationale = "Keep the stronger plan unless this detour becomes clearly better. | " + advice.rationale;
            }
        }
    }

    void applyCouncilV9Intelligence( std::vector<StrategicAdvice> & candidates, const CouncilContext & context )
    {
        for ( StrategicAdvice & advice : candidates ) {
            if ( !std::isfinite( advice.score ) || advice.score <= 0.0 ) {
                advice.score = 0.0;
                continue;
            }

            const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
            if ( advice.action == AdviceAction::MOVE_HERO && ( hero == nullptr || !hero->isActive() || hero->GetMovePoints() == 0 ) ) {
                advice.score = 0.0;
                continue;
            }

            const CouncilV9Type type = councilV9TypeForKind( advice.kind );
            const bool mainHero = hero != nullptr && hero->GetID() == context.mainHeroId;
            const double relativeStrength = hero != nullptr ? hero->GetArmy().GetStrength() / std::max( 1.0, context.strongestHeroStrength ) : 0.0;

            switch ( type ) {
            case CouncilV9Type::DEFEND:
                advice.score *= 1.16;
                advice.rationale = "Protect the kingdom before spending tempo elsewhere. | " + advice.rationale;
                break;

            case CouncilV9Type::STRIKE:
                if ( mainHero ) {
                    advice.score *= 1.05;
                }
                break;

            case CouncilV9Type::GROW:
                if ( context.lowGold || context.lowMaterials ) {
                    advice.score *= 1.10;
                }
                break;

            case CouncilV9Type::POWER:
                if ( mainHero ) {
                    advice.score *= 1.06;
                }
                break;

            case CouncilV9Type::SCOUT:
                if ( mainHero ) {
                    advice.score *= 0.72;
                    advice.rationale = "Leave scouting to a support hero when possible. | " + advice.rationale;
                }
                else if ( relativeStrength < 0.72 ) {
                    advice.score *= 1.12;
                    advice.rationale = "This support hero can scout without slowing the main force. | " + advice.rationale;
                }
                break;

            case CouncilV9Type::SUPPORT:
                if ( mainHero ) {
                    advice.score *= 0.88;
                }
                else {
                    advice.score *= 1.06;
                }
                break;

            case CouncilV9Type::RECOVER:
                advice.score *= relativeStrength < 0.55 ? 1.34 : 1.10;
                advice.rationale = "Recover position and strength before forcing another fight. | " + advice.rationale;
                break;

            default:
                break;
            }

            if ( hero != nullptr && advice.action == AdviceAction::MOVE_HERO && hero->GetMovePoints() > 0 ) {
                const double movementUse = std::clamp( static_cast<double>( advice.distance ) / hero->GetMovePoints(), 0.0, 1.0 );
                if ( movementUse <= 0.35 && ( type == CouncilV9Type::TEMPO || type == CouncilV9Type::DEFEND ) ) {
                    advice.score *= 1.08;
                }
                else if ( movementUse >= 0.85 && mainHero && isCouncilV9Errand( advice.kind ) ) {
                    advice.score *= 0.62;
                    advice.rationale = "Do not spend most of the main hero's movement on a minor errand. | " + advice.rationale;
                }
            }

            if ( isCouncilV9CombatAdvice( advice.kind ) && hero != nullptr && advice.targetIndex >= 0 ) {
                const Maps::Tile & targetTile = world.getTile( advice.targetIndex );
                const double defenders = defenderStrengthForAdvice( targetTile, *hero );
                if ( defenders > 0.0 ) {
                    const double combatEdge = hero->GetArmy().GetStrength() / defenders;
                    if ( combatEdge < 1.05 ) {
                        advice.score *= 0.08;
                        advice.rationale = "Combat margin is too thin; prefer a safer order. | " + advice.rationale;
                    }
                    else if ( combatEdge < 1.20 ) {
                        advice.score *= 0.45;
                        advice.rationale = "Combat margin is narrow; treat this as a risky option. | " + advice.rationale;
                    }
                    else if ( combatEdge < 1.45 ) {
                        advice.score *= 0.82;
                    }
                    else if ( combatEdge >= 2.20 ) {
                        advice.score *= 1.06;
                    }
                }
            }
        }
    }

    std::string councilV9DuplicateKey( const StrategicAdvice & advice )
    {
        return std::to_string( static_cast<int>( advice.action ) ) + ":" + std::to_string( advice.heroId ) + ":" + std::to_string( advice.secondaryHeroId ) + ":"
               + std::to_string( advice.targetIndex ) + ":" + std::to_string( static_cast<int>( councilV9TypeForKind( advice.kind ) ) );
    }

    void suppressCouncilV9NearDuplicates( std::vector<StrategicAdvice> & candidates )
    {
        std::sort( candidates.begin(), candidates.end(), []( const StrategicAdvice & left, const StrategicAdvice & right ) {
            if ( left.score != right.score ) {
                return left.score > right.score;
            }
            return left.distance < right.distance;
        } );

        std::set<std::string> seen;
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), [&]( const StrategicAdvice & advice ) {
                              const std::string key = councilV9DuplicateKey( advice );
                              if ( seen.count( key ) > 0 ) {
                                  return true;
                              }
                              seen.insert( key );
                              return false;
                          } ),
                          candidates.end() );
    }

    void applyCouncilV9TypeDiversity( std::vector<StrategicAdvice> & candidates )
    {
        std::vector<size_t> rankedIndexes;
        rankedIndexes.reserve( candidates.size() );
        for ( size_t index = 0; index < candidates.size(); ++index ) {
            rankedIndexes.emplace_back( index );
        }

        std::sort( rankedIndexes.begin(), rankedIndexes.end(), [&]( const size_t left, const size_t right ) {
            return candidates[left].score == candidates[right].score ? candidates[left].distance < candidates[right].distance
                                                                      : candidates[left].score > candidates[right].score;
        } );

        std::map<CouncilV9Type, size_t> seenTypes;
        for ( const size_t index : rankedIndexes ) {
            StrategicAdvice & advice = candidates[index];
            const CouncilV9Type type = councilV9TypeForKind( advice.kind );
            if ( type == CouncilV9Type::DEFEND || type == CouncilV9Type::GENERAL ) {
                continue;
            }

            const size_t occurrence = seenTypes[type]++;
            if ( occurrence == 1 ) {
                advice.score *= 0.92;
            }
            else if ( occurrence == 2 ) {
                advice.score *= 0.82;
            }
            else if ( occurrence >= 3 ) {
                advice.score *= 0.70;
            }
        }
    }

    CouncilV9Risk councilV9RiskForAdvice( const StrategicAdvice & advice )
    {
        if ( !isCouncilV9CombatAdvice( advice.kind ) ) {
            return advice.reachableThisTurn ? CouncilV9Risk::SOLID : CouncilV9Risk::CHECK;
        }

        const Heroes * hero = advice.heroId >= 0 ? world.GetHeroes( advice.heroId ) : nullptr;
        if ( hero == nullptr || advice.targetIndex < 0 ) {
            return CouncilV9Risk::CHECK;
        }

        const Maps::Tile & targetTile = world.getTile( advice.targetIndex );
        const double defenders = defenderStrengthForAdvice( targetTile, *hero );
        if ( defenders <= 0.0 ) {
            return CouncilV9Risk::SOLID;
        }

        const double edge = hero->GetArmy().GetStrength() / defenders;
        if ( edge < 1.20 ) {
            return CouncilV9Risk::RISK;
        }
        if ( edge < 1.45 ) {
            return CouncilV9Risk::CHECK;
        }
        return CouncilV9Risk::SOLID;
    }

    const char * councilV9RiskLabel( const CouncilV9Risk risk )
    {
        switch ( risk ) {
        case CouncilV9Risk::SOLID: return "SOLID";
        case CouncilV9Risk::RISK: return "RISK";
        default: return "CHECK";
        }
    }

    const char * councilV9UrgencyLabel( const StrategicAdvice & advice )
    {
        const CouncilV9Type type = councilV9TypeForKind( advice.kind );
        if ( type == CouncilV9Type::DEFEND ) {
            return advice.reachableThisTurn ? "NOW" : "HIGH";
        }
        if ( type == CouncilV9Type::STRIKE && advice.reachableThisTurn && advice.score >= 12000.0 ) {
            return "ACT";
        }
        if ( type == CouncilV9Type::RECOVER && advice.score >= 9000.0 ) {
            return "RESET";
        }
        if ( advice.score >= 15000.0 ) {
            return "HIGH";
        }
        if ( advice.score >= 7500.0 ) {
            return "GOOD";
        }
        return "OPTION";
    }

    std::string councilV9FirstSentence( const std::string & text )
    {
        const size_t sentenceEnd = text.find( ". " );
        return sentenceEnd == std::string::npos ? text : text.substr( 0, sentenceEnd + 1 );
    }

    bool isCouncilV9TechnicalRationale( const std::string & text )
    {
        return text.find( "best fit among" ) != std::string::npos || text.find( "Reserved for" ) != std::string::npos
               || text.find( "opportunity cost" ) != std::string::npos || text.find( "score" ) != std::string::npos;
    }

    std::string compactCouncilV9Line( std::string text, const size_t maxLength )
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

    std::string compactCouncilV9Rationale( const StrategicAdvice & advice )
    {
        static const std::string separator = " | ";
        std::vector<std::string> fragments;
        size_t start = 0;
        while ( start <= advice.rationale.size() ) {
            const size_t end = advice.rationale.find( separator, start );
            const std::string fragment = councilV9FirstSentence( advice.rationale.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
            if ( !fragment.empty() && !isCouncilV9TechnicalRationale( fragment )
                 && std::find( fragments.begin(), fragments.end(), fragment ) == fragments.end() ) {
                fragments.emplace_back( fragment );
            }
            if ( end == std::string::npos ) {
                break;
            }
            start = end + separator.size();
        }

        if ( fragments.empty() ) {
            return compactCouncilV9Line( councilV9FirstSentence( advice.rationale ), 112 );
        }

        std::string result = fragments.front();
        if ( fragments.size() > 1 && result.size() < 70 ) {
            result += " " + fragments.back();
        }
        return compactCouncilV9Line( result, 112 );
    }

    std::vector<StrategicAdvice> generateStrategicAdviceV9( Kingdom & kingdom, const std::set<std::string> & excludedOrders )
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
        suppressCouncilV9NearDuplicates( candidates );
        applyCouncilV9TypeDiversity( candidates );
        applyAlternativeComparison( candidates );

        candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const StrategicAdvice & advice ) {
                              return advice.score < 2500.0 || !std::isfinite( advice.score );
                          } ),
                          candidates.end() );

        return chooseKingdomCouncilAdvice( std::move( candidates ) );
    }

    int selectStrategicAdviceV9( const std::vector<StrategicAdvice> & advices, const std::string & councilReport, Kingdom & kingdom )
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
                std::string status = std::to_string( visibleCount ) + " orders  |  " + std::to_string( context.movableHeroes ) + " heroes ready";
                if ( const Heroes * mainHero = world.GetHeroes( context.mainHeroId ) ) {
                    status += "  |  Main: " + mainHero->GetName();
                }
                fheroes2::Text subtitle( status, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 18 );
                subtitle.draw( banner.x + 9, banner.y + 23, display );

                if ( !councilReport.empty() ) {
                    fheroes2::Text report( compactCouncilV9Line( councilReport, 104 ), fheroes2::FontType::smallYellow() );
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
                    headingText += std::to_string( index + 1 ) + ". " + councilV9UrgencyLabel( advice ) + " "
                                   + councilV9TypeLabel( councilV9TypeForKind( advice.kind ) ) + " " + councilV9RiskLabel( councilV9RiskForAdvice( advice ) ) + "  "
                                   + advice.title;
                    fheroes2::Text heading( headingText, fheroes2::FontType::smallYellow() );
                    heading.fitToOneRow( textWidth );
                    heading.draw( textX, rows[index].y + 4, display );

                    fheroes2::Text reason( compactCouncilV9Rationale( advice ), fheroes2::FontType::smallWhite() );
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

    void showRpgStrategicAdvisorV9()
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
            const std::vector<StrategicAdvice> advices = generateStrategicAdviceV9( kingdom, failedOrders );
            if ( advices.empty() ) {
                fheroes2::showStandardTextMessage( "War Council",
                                                   councilReport.empty() ? "No worthwhile order is available right now. Your turn stays active."
                                                                         : "No worthwhile order remains. Your turn stays active.",
                                                   Dialog::OK );
                return;
            }

            const int selection = selectStrategicAdviceV9( advices, councilReport, kingdom );
            if ( selection < 0 || static_cast<size_t>( selection ) >= advices.size() ) {
                return;
            }

            const StrategicAdvice selected = advices[selection];
            const std::string selectedIdentity = adviceIdentity( selected );
            const std::vector<StrategicAdvice> refreshed = generateStrategicAdviceV9( kingdom, failedOrders );
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

#define showRpgStrategicAdvisor showRpgStrategicAdvisorV9
