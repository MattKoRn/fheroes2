#include "game_rpg_events.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio_manager.h"
#include "battle_arena.h"
#include "castle.h"
#include "cursor.h"
#include "dialog.h"
#include "game_assets.h"
#include "game_hotkeys.h"
#include "game_rpg.h"
#include "heroes.h"
#include "icn.h"
#include "image.h"
#include "kingdom.h"
#include "localevent.h"
#include "logging.h"
#include "m82.h"
#include "math_base.h"
#include "monster.h"
#include "resource.h"
#include "screen.h"
#include "settings.h"
#include "skill.h"
#include "spell.h"
#include "system.h"
#include "tools.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "ui_window.h"
#include "world.h"

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#pragma comment( lib, "winhttp.lib" )
#pragma comment( lib, "ws2_32.lib" )
#if defined( BIGENDIAN )
#undef BIGENDIAN
#endif
#if defined( FAILURE )
#undef FAILURE
#endif
#if defined( ERROR )
#undef ERROR
#endif
#endif

namespace
{
    fheroes2::RPG::EventConfig activeConfig;
    bool configLoaded = false;
    uint32_t lastEventTurnDay = 0;
    uint32_t eventsInCurrentMonth = 0;
    uint32_t trackedMonthIndex = 0;
    std::vector<std::string> recentStorylineHistory;

    // Background Ollama generation and thread-safe event queue
    struct PendingEventData
    {
        fheroes2::RPG::RandomEvent event;
        PlayerColor color{ static_cast<PlayerColor>( 0 ) };
        bool ready{ false };
    };

    std::mutex eventMutex;
    PendingEventData pendingEvent;
    std::atomic<bool> isGenerating{ false };
    std::atomic<bool> shutdownWorker{ false };
    std::thread workerThread;

    std::mt19937_64 & eventRng()
    {
        static std::mt19937_64 rng = []() {
            std::random_device rd;
            std::seed_seq seed{ rd(), rd(), rd(), rd() };
            return std::mt19937_64( seed );
        }();
        return rng;
    }

    uint32_t randomRange( const uint32_t minVal, const uint32_t maxVal )
    {
        if ( minVal >= maxVal ) {
            return minVal;
        }
        std::uniform_int_distribution<uint32_t> dist( minVal, maxVal );
        return dist( eventRng() );
    }

    std::string trimString( std::string str )
    {
        while ( !str.empty() && std::isspace( static_cast<unsigned char>( str.front() ) ) ) {
            str.erase( str.begin() );
        }
        while ( !str.empty() && std::isspace( static_cast<unsigned char>( str.back() ) ) ) {
            str.pop_back();
        }
        return str;
    }

    bool parseBoolValue( const std::string & val, const bool fallback = false )
    {
        std::string lower;
        lower.reserve( val.size() );
        for ( const char c : val ) {
            lower += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
        }
        if ( lower == "true" || lower == "1" || lower == "yes" || lower == "on" ) {
            return true;
        }
        if ( lower == "false" || lower == "0" || lower == "no" || lower == "off" ) {
            return false;
        }
        return fallback;
    }

    std::string sanitizeToAscii( const std::string & text )
    {
        std::string out;
        out.reserve( text.size() );
        for ( size_t i = 0; i < text.size(); ++i ) {
            const unsigned char c = static_cast<unsigned char>( text[i] );
            if ( c == 0xE2 && i + 2 < text.size() ) {
                const unsigned char c1 = static_cast<unsigned char>( text[i + 1] );
                const unsigned char c2 = static_cast<unsigned char>( text[i + 2] );
                if ( c1 == 0x80 ) {
                    if ( c2 == 0x98 || c2 == 0x99 ) { // ‘ ’
                        out += '\'';
                        i += 2;
                        continue;
                    }
                    if ( c2 == 0x9C || c2 == 0x9D ) { // “ ”
                        out += '"';
                        i += 2;
                        continue;
                    }
                    if ( c2 == 0x93 || c2 == 0x94 ) { // – —
                        out += '-';
                        i += 2;
                        continue;
                    }
                    if ( c2 == 0xA6 ) { // …
                        out += "...";
                        i += 2;
                        continue;
                    }
                }
            }
            if ( c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>( text[i + 1] ) == 0xA0 ) {
                out += ' ';
                i += 1;
                continue;
            }
            out += text[i];
        }
        return out;
    }

    void drawBeveledBox( const fheroes2::Rect & roi, const bool inset )
    {
        fheroes2::Display & display = fheroes2::Display::instance();
        const uint8_t highlight = inset ? fheroes2::GetColorId( 70, 50, 32 ) : fheroes2::GetColorId( 210, 175, 80 );
        const uint8_t shadow = inset ? fheroes2::GetColorId( 200, 165, 75 ) : fheroes2::GetColorId( 35, 25, 16 );
        const uint8_t darkEdge = fheroes2::GetColorId( 20, 15, 10 );

        fheroes2::Fill( display, roi.x, roi.y, roi.width, 1, darkEdge );
        fheroes2::Fill( display, roi.x, roi.y, 1, roi.height, darkEdge );
        fheroes2::Fill( display, roi.x + 1, roi.y + 1, roi.width - 2, 1, highlight );
        fheroes2::Fill( display, roi.x + 1, roi.y + 1, 1, roi.height - 2, highlight );
        fheroes2::Fill( display, roi.x + 1, roi.y + roi.height - 2, roi.width - 2, 1, shadow );
        fheroes2::Fill( display, roi.x + roi.width - 2, roi.y + 1, 1, roi.height - 2, shadow );
        fheroes2::Fill( display, roi.x, roi.y + roi.height - 1, roi.width, 1, darkEdge );
        fheroes2::Fill( display, roi.x + roi.width - 1, roi.y, 1, roi.height, darkEdge );
    }

    void drawChoiceHighlightBox( const fheroes2::Rect & roi, const bool hovered )
    {
        fheroes2::Display & display = fheroes2::Display::instance();
        const uint8_t outerBorder = hovered ? fheroes2::GetColorId( 255, 220, 80 ) : fheroes2::GetColorId( 145, 115, 65 );
        const uint8_t innerBorder = hovered ? fheroes2::GetColorId( 220, 180, 50 ) : fheroes2::GetColorId( 85, 65, 35 );
        const uint8_t shadow = fheroes2::GetColorId( 25, 18, 10 );

        fheroes2::Fill( display, roi.x, roi.y, roi.width, 1, outerBorder );
        fheroes2::Fill( display, roi.x, roi.y, 1, roi.height, outerBorder );
        fheroes2::Fill( display, roi.x, roi.y + roi.height - 1, roi.width, 1, shadow );
        fheroes2::Fill( display, roi.x + roi.width - 1, roi.y, 1, roi.height, shadow );

        fheroes2::Fill( display, roi.x + 1, roi.y + 1, roi.width - 2, 1, innerBorder );
        fheroes2::Fill( display, roi.x + 1, roi.y + 1, 1, roi.height - 2, innerBorder );
        fheroes2::Fill( display, roi.x + 1, roi.y + roi.height - 2, roi.width - 2, 1, shadow );
        fheroes2::Fill( display, roi.x + roi.width - 2, roi.y + 1, 1, roi.height - 2, shadow );
    }

    std::string escapeJsonString( const std::string & input )
    {
        std::string output;
        output.reserve( input.size() + 16 );
        for ( const char c : input ) {
            switch ( c ) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += c; break;
            }
        }
        return output;
    }

    std::string extractJsonStringField( const std::string & json, const std::string & key )
    {
        const std::string needle = "\"" + key + "\"";
        size_t pos = json.find( needle );
        if ( pos == std::string::npos ) {
            return {};
        }
        pos = json.find( ':', pos + needle.size() );
        if ( pos == std::string::npos ) {
            return {};
        }
        pos = json.find( '"', pos + 1 );
        if ( pos == std::string::npos ) {
            return {};
        }
        const size_t start = pos + 1;
        std::string result;
        bool escaped = false;
        for ( size_t i = start; i < json.size(); ++i ) {
            const char c = json[i];
            if ( escaped ) {
                switch ( c ) {
                case 'n': result += '\n'; break;
                case 'r': break;
                case 't': result += '\t'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'u': {
                    if ( i + 4 < json.size() ) {
                        const std::string hexStr = json.substr( i + 1, 4 );
                        try {
                            const unsigned long code = std::stoul( hexStr, nullptr, 16 );
                            i += 4;
                            if ( code == 0x2018 || code == 0x2019 || code == 0x0027 ) {
                                result += '\'';
                            }
                            else if ( code == 0x201C || code == 0x201D || code == 0x0022 ) {
                                result += '"';
                            }
                            else if ( code == 0x2013 || code == 0x2014 ) {
                                result += '-';
                            }
                            else if ( code == 0x2026 ) {
                                result += "...";
                            }
                            else if ( code < 128 ) {
                                result += static_cast<char>( code );
                            }
                        }
                        catch ( ... ) {
                            // ignore malformed hex
                        }
                    }
                    break;
                }
                default: result += c; break;
                }
                escaped = false;
            }
            else if ( c == '\\' ) {
                escaped = true;
            }
            else if ( c == '"' ) {
                break;
            }
            else {
                result += c;
            }
        }
        return sanitizeToAscii( result );
    }

    int32_t extractJsonIntField( const std::string & json, const std::string & key, const int32_t defaultValue = 0 )
    {
        const std::string needle = "\"" + key + "\"";
        size_t pos = json.find( needle );
        if ( pos == std::string::npos ) {
            return defaultValue;
        }
        pos = json.find( ':', pos + needle.size() );
        if ( pos == std::string::npos ) {
            return defaultValue;
        }
        ++pos;
        while ( pos < json.size() && ( json[pos] == ' ' || json[pos] == '\t' || json[pos] == '"' ) ) {
            ++pos;
        }
        if ( pos >= json.size() ) {
            return defaultValue;
        }
        try {
            return std::stoi( json.substr( pos ) );
        }
        catch ( ... ) {
            return defaultValue;
        }
    }

    void logEventMessage( const std::string & msg )
    {
        if ( !activeConfig.ollama_debug_log ) {
            return;
        }
        const std::string logPath = System::concatPath( fheroes2::RPG::dataDirectory(), "events.log" );
        std::ofstream logFile( logPath, std::ios::app );
        if ( logFile ) {
            const auto now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
            logFile << "[" << now << "] " << msg << "\n";
        }
    }

#if defined( _WIN32 )
    std::string queryOllamaHttpWin32( const std::string & endpointUrl, const std::string & postData, const uint32_t timeoutMs )
    {
        std::string host = "localhost";
        int port = 11434;
        std::string path = "/api/generate";

        if ( endpointUrl.rfind( "http://", 0 ) == 0 ) {
            const std::string rest = endpointUrl.substr( 7 );
            const size_t slashPos = rest.find( '/' );
            const std::string hostAndPort = ( slashPos == std::string::npos ) ? rest : rest.substr( 0, slashPos );
            if ( slashPos != std::string::npos ) {
                path = rest.substr( slashPos );
            }
            const size_t colonPos = hostAndPort.find( ':' );
            if ( colonPos != std::string::npos ) {
                host = hostAndPort.substr( 0, colonPos );
                try {
                    port = std::stoi( hostAndPort.substr( colonPos + 1 ) );
                }
                catch ( ... ) {
                    port = 11434;
                }
            }
            else {
                host = hostAndPort;
            }
        }

        if ( shutdownWorker.load() ) {
            return {};
        }

        std::wstring wHost( host.begin(), host.end() );
        std::wstring wPath( path.begin(), path.end() );

        HINTERNET hSession = WinHttpOpen( L"HOMM2-RPG-Ollama/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if ( !hSession ) {
            return {};
        }

        WinHttpSetTimeouts( hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs );

        HINTERNET hConnect = WinHttpConnect( hSession, wHost.c_str(), static_cast<INTERNET_PORT>( port ), 0 );
        if ( !hConnect ) {
            WinHttpCloseHandle( hSession );
            return {};
        }

        HINTERNET hRequest = WinHttpOpenRequest( hConnect, L"POST", wPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, 0 );
        if ( !hRequest ) {
            WinHttpCloseHandle( hConnect );
            WinHttpCloseHandle( hSession );
            return {};
        }

        WinHttpSetTimeouts( hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs );

        const std::wstring headers = L"Content-Type: application/json\r\n";
        const BOOL bSend = WinHttpSendRequest( hRequest, headers.c_str(), static_cast<DWORD>( headers.length() ),
                                               const_cast<char *>( postData.data() ), static_cast<DWORD>( postData.size() ),
                                               static_cast<DWORD>( postData.size() ), 0 );

        std::string response;
        if ( bSend && WinHttpReceiveResponse( hRequest, nullptr ) ) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                if ( shutdownWorker.load() ) {
                    break;
                }
                dwSize = 0;
                if ( !WinHttpQueryDataAvailable( hRequest, &dwSize ) ) {
                    break;
                }
                if ( dwSize == 0 ) {
                    break;
                }
                std::vector<char> buffer( dwSize + 1, 0 );
                if ( WinHttpReadData( hRequest, buffer.data(), dwSize, &dwDownloaded ) ) {
                    response.append( buffer.data(), dwDownloaded );
                }
            } while ( dwSize > 0 );
        }

        WinHttpCloseHandle( hRequest );
        WinHttpCloseHandle( hConnect );
        WinHttpCloseHandle( hSession );
        return response;
    }
#endif

    std::string requestOllamaGeneration( const std::string & prompt, const fheroes2::RPG::EventConfig & conf )
    {
        std::ostringstream jsonStream;
        jsonStream << "{\n"
                   << "  \"model\": \"" << escapeJsonString( conf.ollama_model ) << "\",\n"
                   << "  \"prompt\": \"" << escapeJsonString( prompt ) << "\",\n"
                   << "  \"stream\": false,\n"
                   << "  \"format\": \"json\",\n"
                   << "  \"keep_alive\": \"" << escapeJsonString( conf.ollama_keep_alive ) << "\",\n"
                   << "  \"options\": {\n"
                   << "    \"temperature\": " << conf.ollama_temperature << ",\n"
                   << "    \"top_p\": " << conf.ollama_top_p << ",\n"
                   << "    \"num_predict\": " << conf.ollama_max_tokens << "\n"
                   << "  }\n"
                   << "}";

        const std::string postPayload = jsonStream.str();
        logEventMessage( "Sending prompt to Ollama (" + conf.ollama_model + ")" );

#if defined( _WIN32 )
        for ( uint32_t retry = 0; retry <= conf.ollama_retry_count; ++retry ) {
            const std::string rawResp = queryOllamaHttpWin32( conf.ollama_url, postPayload, conf.ollama_timeout_ms );
            if ( !rawResp.empty() ) {
                logEventMessage( "Received Ollama response (" + std::to_string( rawResp.size() ) + " bytes)" );
                return rawResp;
            }
        }
#endif
        logEventMessage( "Ollama query failed or timed out; falling back to procedural engine." );
        return {};
    }

    bool parseOllamaEventJson( const std::string & rawResponse, fheroes2::RPG::RandomEvent & outEvent )
    {
        if ( rawResponse.empty() ) {
            return false;
        }

        std::string body = rawResponse;

        // Check if Ollama returned the wrapped {"response": "..."} format
        const std::string nestedResponse = extractJsonStringField( rawResponse, "response" );
        if ( !nestedResponse.empty() ) {
            body = nestedResponse;
        }

        // Strip markdown code block fences if present (e.g. ```json ... ```)
        const size_t fencePos = body.find( "```" );
        if ( fencePos != std::string::npos ) {
            const size_t start = body.find( '\n', fencePos );
            if ( start != std::string::npos ) {
                const size_t end = body.rfind( "```" );
                if ( end != std::string::npos && end > start ) {
                    body = body.substr( start + 1, end - start - 1 );
                }
            }
        }

        outEvent.title = extractJsonStringField( body, "title" );
        outEvent.description = extractJsonStringField( body, "description" );
        if ( outEvent.title.empty() || outEvent.description.empty() ) {
            return false;
        }

        // Find choices array in body
        const size_t choicesPos = body.find( "\"choices\"" );
        if ( choicesPos == std::string::npos ) {
            return false;
        }
        const size_t arrStart = body.find( '[', choicesPos );
        const size_t arrEnd = body.rfind( ']' );
        if ( arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart ) {
            return false;
        }

        const std::string choicesBlock = body.substr( arrStart, arrEnd - arrStart + 1 );
        size_t cursor = 0;
        outEvent.choices.clear();

        while ( cursor < choicesBlock.size() ) {
            const size_t objStart = choicesBlock.find( '{', cursor );
            if ( objStart == std::string::npos ) {
                break;
            }
            const size_t objEnd = choicesBlock.find( '}', objStart );
            if ( objEnd == std::string::npos ) {
                break;
            }

            const std::string choiceObj = choicesBlock.substr( objStart, objEnd - objStart + 1 );
            fheroes2::RPG::EventChoice choice;
            choice.text = extractJsonStringField( choiceObj, "text" );
            choice.outcome_text = extractJsonStringField( choiceObj, "outcome_text" );
            if ( choice.text.empty() ) {
                cursor = objEnd + 1;
                continue;
            }

            choice.gold = extractJsonIntField( choiceObj, "gold", 0 );
            choice.wood = extractJsonIntField( choiceObj, "wood", 0 );
            choice.ore = extractJsonIntField( choiceObj, "ore", 0 );
            choice.mercury = extractJsonIntField( choiceObj, "mercury", 0 );
            choice.sulfur = extractJsonIntField( choiceObj, "sulfur", 0 );
            choice.crystal = extractJsonIntField( choiceObj, "crystal", 0 );
            choice.gems = extractJsonIntField( choiceObj, "gems", 0 );

            choice.attack = extractJsonIntField( choiceObj, "attack", 0 );
            choice.defense = extractJsonIntField( choiceObj, "defense", 0 );
            choice.power = extractJsonIntField( choiceObj, "power", 0 );
            choice.knowledge = extractJsonIntField( choiceObj, "knowledge", 0 );

            choice.morale = extractJsonIntField( choiceObj, "morale", 0 );
            choice.luck = extractJsonIntField( choiceObj, "luck", 0 );

            choice.hero_xp = static_cast<uint32_t>( std::max( 0, extractJsonIntField( choiceObj, "hero_xp", 0 ) ) );
            choice.rpg_xp = static_cast<uint64_t>( std::max( 0, extractJsonIntField( choiceObj, "rpg_xp", 0 ) ) );

            choice.mana = extractJsonIntField( choiceObj, "mana", 0 );
            choice.spell_id = extractJsonIntField( choiceObj, "spell_id", 0 );

            choice.creature_id = extractJsonIntField( choiceObj, "creature_id", 0 );
            choice.creature_count = extractJsonIntField( choiceObj, "creature_count", 0 );

            // Auto-generate clean bracketed consequence preview if LLM left outcome_text blank
            if ( choice.outcome_text.empty() ) {
                std::vector<std::string> parts;
                if ( choice.gold != 0 ) parts.push_back( ( choice.gold > 0 ? "+" : "" ) + std::to_string( choice.gold ) + " Gold" );
                if ( choice.wood != 0 ) parts.push_back( ( choice.wood > 0 ? "+" : "" ) + std::to_string( choice.wood ) + " Wood" );
                if ( choice.ore != 0 ) parts.push_back( ( choice.ore > 0 ? "+" : "" ) + std::to_string( choice.ore ) + " Ore" );
                if ( choice.mercury != 0 ) parts.push_back( ( choice.mercury > 0 ? "+" : "" ) + std::to_string( choice.mercury ) + " Mercury" );
                if ( choice.sulfur != 0 ) parts.push_back( ( choice.sulfur > 0 ? "+" : "" ) + std::to_string( choice.sulfur ) + " Sulfur" );
                if ( choice.crystal != 0 ) parts.push_back( ( choice.crystal > 0 ? "+" : "" ) + std::to_string( choice.crystal ) + " Crystal" );
                if ( choice.gems != 0 ) parts.push_back( ( choice.gems > 0 ? "+" : "" ) + std::to_string( choice.gems ) + " Gems" );
                if ( choice.attack != 0 ) parts.push_back( ( choice.attack > 0 ? "+" : "" ) + std::to_string( choice.attack ) + " Atk" );
                if ( choice.defense != 0 ) parts.push_back( ( choice.defense > 0 ? "+" : "" ) + std::to_string( choice.defense ) + " Def" );
                if ( choice.power != 0 ) parts.push_back( ( choice.power > 0 ? "+" : "" ) + std::to_string( choice.power ) + " Pow" );
                if ( choice.knowledge != 0 ) parts.push_back( ( choice.knowledge > 0 ? "+" : "" ) + std::to_string( choice.knowledge ) + " Know" );
                if ( choice.morale != 0 ) parts.push_back( ( choice.morale > 0 ? "+" : "" ) + std::to_string( choice.morale ) + " Morale" );
                if ( choice.luck != 0 ) parts.push_back( ( choice.luck > 0 ? "+" : "" ) + std::to_string( choice.luck ) + " Luck" );
                if ( choice.hero_xp != 0 ) parts.push_back( "+" + std::to_string( choice.hero_xp ) + " XP" );
                if ( choice.rpg_xp != 0 ) parts.push_back( "+" + std::to_string( choice.rpg_xp ) + " RPG XP" );
                if ( !parts.empty() ) {
                    choice.outcome_text = "[";
                    for ( size_t p = 0; p < parts.size(); ++p ) {
                        if ( p > 0 ) choice.outcome_text += ", ";
                        choice.outcome_text += parts[p];
                    }
                    choice.outcome_text += "]";
                }
            }

            outEvent.choices.emplace_back( std::move( choice ) );
            cursor = objEnd + 1;
        }

        return outEvent.choices.size() >= 2;
    }

    std::string buildCkPrompt( const Kingdom & kingdom )
    {
        const PlayerColor color = kingdom.GetColor();
        const std::string colorName = Color::String( color );
        const Funds & funds = kingdom.GetFunds();

        std::string heroName = "Lord Commander";
        std::string heroClass = "Warlord";
        int heroAttack = 1;
        int heroDefense = 1;
        int heroPower = 1;
        int heroKnowledge = 1;
        int heroLevel = 1;
        std::string armySummary = "royal escort";

        if ( !kingdom.GetHeroes().empty() && kingdom.GetHeroes().front() != nullptr ) {
            const Heroes * hero = kingdom.GetHeroes().front();
            heroName = hero->GetName();
            heroClass = Race::String( hero->GetRace() );
            heroAttack = hero->GetAttack();
            heroDefense = hero->GetDefense();
            heroPower = hero->GetPower();
            heroKnowledge = hero->GetKnowledge();
            heroLevel = hero->GetLevel();

            for ( size_t i = 0; i < hero->GetArmy().Size(); ++i ) {
                const Troop * tr = hero->GetArmy().GetTroop( i );
                if ( tr != nullptr && tr->isValid() && tr->GetCount() > 0 ) {
                    armySummary = std::to_string( tr->GetCount() ) + " " + std::string( tr->GetPluralName( tr->GetCount() ) );
                    break;
                }
            }
        }

        std::string castleName = "Royal Citadel";
        std::string castleRace = "Feudal Stronghold";
        if ( !kingdom.GetCastles().empty() && kingdom.GetCastles().front() != nullptr ) {
            const Castle * c = kingdom.GetCastles().front();
            castleName = c->GetName();
            castleRace = Race::String( c->GetRace() );
        }

        // Dynamic situational thematic focus for the Ollama freestyle prompt
        std::vector<std::string> thematicPool;
        if ( funds.gold < 1500 ) {
            thematicPool.push_back( "Treasury Insolvency: Extortionate dwarven bankers, desperate peasant tithes, or black market contraband." );
            thematicPool.push_back( "Mutinous Mercenaries: Unpaid sellswords threatening to sack frontier settlements unless placated." );
        }
        else if ( funds.gold > 10000 ) {
            thematicPool.push_back( "Lavish Court Ambition: Ostentatious tourney, imperial cathedral founding, or foreign ambassadors seeking bribes." );
        }

        if ( heroPower >= heroAttack + 2 || heroClass == "Sorceress" || heroClass == "Wizard" || heroClass == "Warlock" ) {
            thematicPool.push_back( "Arcane Cataclysm: Unstable ley line rift, forbidden planar grimoire, or celestial dragon omen." );
            thematicPool.push_back( "Witch Coven Bargain: Hooded hags demanding sulfur and crystal in exchange for eldritch sorceries." );
        }

        if ( heroClass == "Necromancer" ) {
            thematicPool.push_back( "Restless Tombs: Ancient liches demanding kingdom fealty or cemetery desecration riots." );
        }
        else if ( heroClass == "Barbarian" ) {
            thematicPool.push_back( "Blood Oath Challenge: Chieftains demanding a trial of combat, wild beast taming, or war tribute." );
        }
        else if ( heroClass == "Knight" ) {
            thematicPool.push_back( "Chivalric Oath of Honor: Disgraced paladin seeking redemption, saintly relic theft, or vassal treason." );
        }

        if ( kingdom.GetCountCastle() >= 3 ) {
            thematicPool.push_back( "Imperial Intrigue: Disputed royal succession, poisoned royal chalice, or rebellious marcher lords." );
        }

        thematicPool.push_back( "Frontier Enigma: Sleeping colossal golem unearthed in royal quarry, or sphinx demanding answers." );
        thematicPool.push_back( "Smuggler's Cove: Shadowy thieves guild offering stolen war supplies at grave risk to realm reputation." );

        const size_t themePick = randomRange( 0, static_cast<uint32_t>( thematicPool.size() - 1 ) );
        const std::string selectedTheme = thematicPool[themePick];

        std::ostringstream ss;
        ss << "[HOMM2 CHRONICLER: HIGH-STAKES REALM DILEMMA]\n"
           << "You are the royal chronicler of Enroth during the Succession Wars. A critical dilemma has arisen demanding the crown's immediate decree.\n"
           << "Write a vivid, gritty, authentic medieval-fantasy Crusader Kings style event popup.\n\n"
           << "REALM STATUS:\n"
           << "- Faction: " << colorName << " Kingdom | Calendar: " << world.DateString()
           << " | Strongholds: " << kingdom.GetCountCastle() << " (" << castleName << " - " << castleRace << ")\n"
           << "- Sovereign Commander: " << heroName << " (" << heroClass << " Lv." << heroLevel
           << ", Atk: " << heroAttack << ", Def: " << heroDefense << ", Pow: " << heroPower << ", Know: " << heroKnowledge
           << " | Army: " << armySummary << ")\n"
           << "- Treasury: " << funds.gold << " Gold, " << funds.wood << " Wood, " << funds.ore << " Ore, "
           << funds.mercury << " Mercury, " << funds.sulfur << " Sulfur, " << funds.crystal << " Crystal, " << funds.gems << " Gems\n"
           << "- Dilemma Theme: " << selectedTheme << "\n";

        if ( !recentStorylineHistory.empty() ) {
            ss << "- Chronicle Legacy: ";
            for ( size_t i = 0; i < recentStorylineHistory.size(); ++i ) {
                if ( i > 0 ) ss << "; ";
                ss << recentStorylineHistory[i];
            }
            ss << "\n";
        }

        if ( !activeConfig.ollama_custom_system_prompt.empty() ) {
            ss << "- Custom Mandate: " << activeConfig.ollama_custom_system_prompt << "\n";
        }

        ss << "\nWRITING INSTRUCTIONS (STRICT COMPACT CRUSADER KINGS STYLE):\n"
           << "1. TONE: Noble, feudal, suspenseful, archaic. Mention castles, vassals, guilds, sorcerers, dragons, or ancient barrows. NO modern slang, NO cheerful banter, NO filler.\n"
           << "2. TITLE: 2 to 5 evocative words (e.g., 'The Usurer's Tithe', 'Embers of the Dragon Roost', 'Vassal's Defiance', 'The Whispering Chalice').\n"
           << "3. DESCRIPTION: Strictly 2 to 3 punchy, atmospheric sentences. Set up an immediate moral or strategic crisis.\n"
           << "4. CHOICES: Exactly 2 to 3 distinct, fraught options with meaningful trade-offs:\n"
           << "   - Choice 1: Ruthless Force / Pragmatism (gain gold/power, lose morale/luck)\n"
           << "   - Choice 2: Chivalry / Piety / Charity (spend treasury/resources, gain morale/luck/recruits)\n"
           << "   - Choice 3: Arcane Gamble / Cunning Opportunism (risk reagents for knowledge/spells/XP)\n"
           << "5. OUTCOMES: Explicitly specify tangible numeric trade-offs with bracketed outcome_text for immediate UI display.\n"
           << "6. ZERO BLOAT: Pure JSON ONLY. No markdown ticks, no preamble, no conversation.\n\n"
           << "JSON SCHEMA:\n"
           << "{\n"
           << "  \"title\": \"Short Dramatic Title\",\n"
           << "  \"description\": \"Two to three punchy medieval sentences.\",\n"
           << "  \"choices\": [\n"
           << "    {\n"
           << "      \"text\": \"Concise decree title\",\n"
           << "      \"outcome_text\": \"[+1,200 Gold, -1 Morale, +1 Attack]\",\n"
           << "      \"gold\": 1200,\n"
           << "      \"wood\": 0,\n"
           << "      \"ore\": 0,\n"
           << "      \"mercury\": 0,\n"
           << "      \"sulfur\": 0,\n"
           << "      \"crystal\": 0,\n"
           << "      \"gems\": 0,\n"
           << "      \"attack\": 1,\n"
           << "      \"defense\": 0,\n"
           << "      \"power\": 0,\n"
           << "      \"knowledge\": 0,\n"
           << "      \"morale\": -1,\n"
           << "      \"luck\": 0,\n"
           << "      \"hero_xp\": 200,\n"
           << "      \"rpg_xp\": 800,\n"
           << "      \"mana\": 0,\n"
           << "      \"spell_id\": 0,\n"
           << "      \"creature_id\": 0,\n"
           << "      \"creature_count\": 0\n"
           << "    }\n"
           << "  ]\n"
           << "}";

        return ss.str();
    }

    // Comprehensive catalog of procedural Crusader Kings events
    std::vector<fheroes2::RPG::RandomEvent> buildProceduralEventCatalog()
    {
        std::vector<fheroes2::RPG::RandomEvent> catalog;
        catalog.reserve( 32 );

        // 1. Alchemical Crucible
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "alchemist_transmutation";
            ev.title = "The Alchemist's Transmutation";
            ev.description = "A disheveled sage with singed robes and glittering violet eyes requests audience. He claims to have perfected an alchemical crucible that converts raw sulfur and mercury into shimmering royal gold, but requires capital to ignite the kiln.";
            ev.choices.push_back( { "Finance the crucible with royal reagents.", "[-1,200 Gold, -2 Sulfur, -2 Mercury | +3,500 Gold, +1,000 RPG XP]", 2300, 0, 0, -2, -2, 0, 0, 0, 0, 0, 0, 0, 1, 200, 1000, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Confiscate his research notes for the realm.", "[+1 Hero Knowledge, +500 Gold | -1 Morale]", 500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1, 0, 300, 600, 10, 0, 0, 0 } );
            ev.choices.push_back( { "Expel him from the realm as a charlatan.", "[+1 Hero Luck | No expenses]", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 200, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 2. Peasant Uprising
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "peasant_revolt";
            ev.title = "Peasant Revolt in the Marches";
            ev.description = "Tax collectors have pushed the serfs of the western shires beyond endurance. Hundreds have taken up scythes and pitchforks, barricading the royal granaries and demanding tax relief.";
            ev.choices.push_back( { "Dispatch the knights to crush the rebellion.", "[+1 Hero Attack | -600 Gold, -1 Morale]", -600, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 400, 800, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Open the granaries and forgive their arrears.", "[-1,500 Gold | +2 Morale, +25 Peasant Recruits]", -1500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 200, 1200, 0, 0, Monster::PEASANT, 25 } );
            ev.choices.push_back( { "Grant them a chartered borough in exchange for militia levy.", "[-800 Gold | +1 Hero Defense, +10 Halberdiers]", -800, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 250, 900, 0, 0, Monster::PIKEMAN, 10 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 3. Whispering Grimoire
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "whispering_grimoire";
            ev.title = "The Whispering Grimoire";
            ev.description = "Diggers unearthing foundations near the castle cemetery have uncovered a brass-bound tome wrapped in rusted chains. The book faintly hums with necromantic power, whispering ancient incantations.";
            ev.choices.push_back( { "Order your wizard to break the seals and transcribe its magic.", "[Hero learns Lightning Bolt, +1 Spell Power | -1 Morale]", 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, -1, 0, 500, 1500, 25, Spell::LIGHTNINGBOLT, 0, 0 } );
            ev.choices.push_back( { "Purge the tome with holy fire on the grand altar.", "[+2 Morale, +1 Luck, +1,800 RPG XP]", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 150, 1800, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Sell the dark tome to a masked foreign collector.", "[+2,800 Gold, +3 Mercury | -1 Luck]", 2800, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 500, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 4. Knight of the Sunlit Order
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "knight_errant";
            ev.title = "The Wandering Paladin";
            ev.description = "A lone champion clad in polished silver plate rides beneath your banners. He has sworn a solemn crusade against lawlessness and offers his holy sword to your kingdom's expedition.";
            ev.choices.push_back( { "Induct him into your vanguard command.", "[-900 Gold for armaments | +1 Hero Attack, +1 Defense, +4 Swordsmen]", -900, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 600, 1400, 0, 0, Monster::SWORDSMAN, 4 } );
            ev.choices.push_back( { "Commission him to train the realm's garrison officers.", "[-500 Gold | +1,200 Hero XP, +1,500 RPG XP]", -500, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1200, 1500, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Bless his pilgrimage with travel supplies.", "[-300 Gold | +2 Luck for the expedition]", -300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 100, 400, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 5. Dragon Lair Discovered
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "dragon_roost";
            ev.title = "Dragon Roost in the Caldera";
            ev.description = "Rangers have located an abandoned caldera where a slain dragon once nested. Inside the smoking cavern lie unhatched clutches, glittering rubies, and slumbering drakes.";
            ev.choices.push_back( { "Tame and recruit a young Green Dragon.", "[-2,500 Gold in provisions | +1 Green Dragon, +2,000 RPG XP]", -2500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 800, 2000, 0, 0, Monster::GREEN_DRAGON, 1 } );
            ev.choices.push_back( { "Loot the caldera's gemstone hoard.", "[+4,200 Gold, +8 Gems, +6 Sulfur]", 4200, 0, 0, 0, 6, 0, 8, 0, 0, 0, 0, 0, 1, 400, 1200, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Consecrate the roost as an elemental sanctum.", "[Hero gains +2 Spell Power, +20 Mana | -5 Gems]", 0, 0, 0, 0, 0, 0, -5, 0, 0, 2, 0, 1, 1, 500, 1600, 20, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 6. Feast of Autumn Harvest
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "harvest_feast";
            ev.title = "Feast of the Autumn Harvest";
            ev.description = "The realm's baronies have brought in a record harvest of grain and timber. The High Steward inquires how to mark this auspicious season.";
            ev.choices.push_back( { "Host an extravagant jousting tournament and feast.", "[-1,800 Gold | +2 Morale, +1 Hero Attack, +10 Pikemen]", -1800, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 1, 450, 1200, 0, 0, Monster::PIKEMAN, 10 } );
            ev.choices.push_back( { "Stockpile the surplus for siege preparations.", "[+2,000 Gold, +12 Wood | -1 Morale]", 2000, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 100, 600, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Tithe the surplus to the frontier villages.", "[+1 Morale, +1 Luck, +1,400 RPG XP]", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 200, 1400, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 7. Mercenary Company
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "mercenary_offer";
            ev.title = "The Free Company of Iron";
            ev.description = "A renowned mercenary company has arrived at your border after completing their contract across the sea. Their condottiere offers their veteran lances at a bulk discount.";
            ev.choices.push_back( { "Sign their whole regiment into royal service.", "[-3,000 Gold | +6 Cavalry, +1 Hero Attack]", -3000, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 600, 1600, 0, 0, Monster::CAVALRY, 6 } );
            ev.choices.push_back( { "Hire their armorers to refit our legion.", "[-1,200 Gold | +1 Hero Attack, +1 Hero Defense]", -1200, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 300, 1000, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Politely decline their services.", "[No effect]", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 8. Chancellor Embezzlement
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "court_embezzlement";
            ev.title = "Embezzlement in the Treasury";
            ev.description = "A surprise audit reveals that the royal treasurer has pocketed thousands of ducats into a clandestine manor. The court waits in silence for your decree.";
            ev.choices.push_back( { "Confiscate his hidden estate and hang him publicly.", "[+3,800 Gold, +1 Hero Defense | -1 Morale]", 3800, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 200, 800, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Pardon him in exchange for a ruinous restitution fine.", "[+5,200 Gold | -1 Luck]", 5200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 400, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Strip his title and banish him to the penal legion.", "[+1 Morale, +800 RPG XP]", 800, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 150, 800, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 9. Witch Coven at the Crossroads
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "witch_coven";
            ev.title = "Witches at the Crossroads";
            ev.description = "Three hooded sisters tend a boiling cauldron beneath the ancient gallows tree. They offer prophecies and mystical elixirs in exchange for rare reagents.";
            ev.choices.push_back( { "Offer sulfur and crystal for arcane knowledge.", "[-2 Sulfur, -2 Crystal | Hero gains +2 Spell Power, Full Mana]", 0, 0, 0, 0, -2, -2, 0, 0, 0, 2, 0, 0, 0, 400, 1400, 50, 0, 0, 0 } );
            ev.choices.push_back( { "Demand a charm of good fortune for the army.", "[-500 Gold | +2 Luck, +1 Morale]", -500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 150, 900, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Drive the coven away and burn their effigies.", "[+1 Hero Attack, +1,100 RPG XP | -1 Luck]", 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 300, 1100, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 10. Astrologer's Conjunction
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "astrologer_omen";
            ev.title = "The Celestial Conjunction";
            ev.description = "The Royal Astrologer announces that the twin red comets have aligned with the Dragon star. He foretells a brief window where fate bends to audacious wills.";
            ev.choices.push_back( { "Declare a crusade under the celestial banner.", "[+2 Luck, +1 Hero Attack, +1,500 Hero XP]", 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 2, 1500, 1600, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Conduct ritual wards across all strongholds.", "[-1,000 Gold | +2 Hero Defense, +1,800 RPG XP]", -1000, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 400, 1800, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Sacrifice gemstones to the heavens.", "[-4 Gems | +2 Luck, +2 Morale, Hero learns Fireball]", 0, 0, 0, 0, 0, 0, -4, 0, 0, 1, 0, 2, 2, 500, 1200, 15, Spell::FIREBALL, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 11. Forgotten Vault
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "forgotten_vault";
            ev.title = "The Subterranean Vault";
            ev.description = "A collapse in the lower cellars has breached the entrance to an ancient dwarven vault, sealed during the Succession Wars. Runic locks protect chests of ore and gems.";
            ev.choices.push_back( { "Pry the vault open with brute force.", "[+2,500 Gold, +15 Ore, +5 Gems | -1 Luck]", 2500, 0, 15, 0, 0, 0, 5, 0, 0, 0, 0, 0, -1, 300, 1000, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Hire master lockpicks to open it carefully.", "[-800 Gold | +4,000 Gold, +8 Gems, +1,600 RPG XP]", 3200, 0, 8, 0, 0, 0, 8, 0, 0, 0, 0, 1, 1, 500, 1600, 0, 0, 0, 0 } );
            ev.choices.push_back( { "Study the runic wards for magical theory.", "[+2 Hero Knowledge, +20 Mana, +1,200 RPG XP]", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 600, 1200, 20, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        // 12. Ancient Golem Sentry
        {
            fheroes2::RPG::RandomEvent ev;
            ev.id = "golem_sentry";
            ev.title = "The Dormant Iron Golem";
            ev.description = "Explorers in the quarry have unearthed a colossal Iron Golem half-buried in granite. Its arcane core is intact but requires an infusion of crystal and gold to awaken.";
            ev.choices.push_back( { "Reactivate the automaton for your army.", "[-1,500 Gold, -4 Crystal | +3 Iron Golems, +1 Hero Defense]", -1500, 0, 0, 0, 0, -4, 0, 0, 1, 0, 0, 0, 0, 400, 1500, 0, 0, Monster::IRON_GOLEM, 3 } );
            ev.choices.push_back( { "Dismantle the golem for metals and enchantment.", "[+1,800 Gold, +18 Ore, +1 Hero Spell Power]", 1800, 0, 18, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 300, 1100, 10, 0, 0, 0 } );
            ev.choices.push_back( { "Enshrine the sentinel as a town guardian.", "[+1 Morale to the realm, +1,200 RPG XP]", 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 200, 1200, 0, 0, 0, 0 } );
            catalog.emplace_back( std::move( ev ) );
        }

        return catalog;
    }

    fheroes2::RPG::RandomEvent generateProceduralCkEvent( const Kingdom & kingdom )
    {
        std::vector<fheroes2::RPG::RandomEvent> catalog = buildProceduralEventCatalog();
        if ( catalog.empty() ) {
            return {};
        }

        // Filter out recently seen storylines if configured
        if ( !activeConfig.allow_repeat_storylines && !recentStorylineHistory.empty() ) {
            catalog.erase( std::remove_if( catalog.begin(), catalog.end(),
                                           []( const fheroes2::RPG::RandomEvent & ev ) {
                                               return std::find( recentStorylineHistory.begin(), recentStorylineHistory.end(), ev.id ) != recentStorylineHistory.end();
                                           } ),
                           catalog.end() );
            if ( catalog.empty() ) {
                catalog = buildProceduralEventCatalog();
            }
        }

        const size_t pick = randomRange( 0, static_cast<uint32_t>( catalog.size() - 1 ) );
        fheroes2::RPG::RandomEvent event = std::move( catalog[pick] );

        // Replace contextual tokens
        std::string heroName = "Commander";
        if ( !kingdom.GetHeroes().empty() && kingdom.GetHeroes().front() != nullptr ) {
            heroName = kingdom.GetHeroes().front()->GetName();
        }
        std::string castleName = "Royal Citadel";
        if ( !kingdom.GetCastles().empty() && kingdom.GetCastles().front() != nullptr ) {
            castleName = kingdom.GetCastles().front()->GetName();
        }

        StringReplace( event.description, "{HERO}", heroName );
        StringReplace( event.description, "{CASTLE}", castleName );
        StringReplace( event.description, "{REALM}", Color::String( kingdom.GetColor() ) );

        for ( auto & choice : event.choices ) {
            StringReplace( choice.text, "{HERO}", heroName );
            StringReplace( choice.text, "{CASTLE}", castleName );
        }

        return event;
    }
}

namespace fheroes2::RPG
{
    const EventConfig & getEventConfig()
    {
        if ( !configLoaded ) {
            loadEventConfig();
        }
        return activeConfig;
    }

    void loadEventConfig()
    {
        configLoaded = true;
        const std::string configDir = dataDirectory();
        System::MakeDirectory( configDir );
        const std::string configPath = System::concatPath( configDir, "config.ini" );

        std::ifstream file( configPath );
        if ( !file ) {
            saveDefaultEventConfigFile( configPath );
            return;
        }

        std::string line;
        while ( std::getline( file, line ) ) {
            line = trimString( line );
            if ( line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[' ) {
                continue;
            }

            const size_t eqPos = line.find( '=' );
            if ( eqPos == std::string::npos ) {
                continue;
            }

            const std::string key = trimString( line.substr( 0, eqPos ) );
            const std::string val = trimString( line.substr( eqPos + 1 ) );

            // [Ollama]
            if ( key == "ollama_enabled" ) activeConfig.ollama_enabled = parseBoolValue( val, true );
            else if ( key == "ollama_url" ) activeConfig.ollama_url = val;
            else if ( key == "ollama_model" ) activeConfig.ollama_model = val;
            else if ( key == "ollama_timeout_ms" ) activeConfig.ollama_timeout_ms = std::stoul( val );
            else if ( key == "ollama_temperature" ) activeConfig.ollama_temperature = std::stod( val );
            else if ( key == "ollama_top_p" ) activeConfig.ollama_top_p = std::stod( val );
            else if ( key == "ollama_max_tokens" ) activeConfig.ollama_max_tokens = std::stoul( val );
            else if ( key == "ollama_keep_alive" ) activeConfig.ollama_keep_alive = val;
            else if ( key == "ollama_retry_count" ) activeConfig.ollama_retry_count = std::stoul( val );
            else if ( key == "ollama_async_prefetch" ) activeConfig.ollama_async_prefetch = parseBoolValue( val, true );
            else if ( key == "ollama_context_memory" ) activeConfig.ollama_context_memory = parseBoolValue( val, true );
            else if ( key == "ollama_max_history_events" ) activeConfig.ollama_max_history_events = std::stoul( val );
            else if ( key == "ollama_custom_system_prompt" ) activeConfig.ollama_custom_system_prompt = val;
            else if ( key == "ollama_debug_log" ) activeConfig.ollama_debug_log = parseBoolValue( val, false );

            // [Events_General]
            else if ( key == "events_master_enabled" ) activeConfig.events_master_enabled = parseBoolValue( val, true );
            else if ( key == "daily_event_chance_percent" ) activeConfig.daily_event_chance_percent = std::stoul( val );
            else if ( key == "min_days_between_events" ) activeConfig.min_days_between_events = std::stoul( val );
            else if ( key == "max_days_between_events" ) activeConfig.max_days_between_events = std::stoul( val );
            else if ( key == "first_event_day" ) activeConfig.first_event_day = std::stoul( val );
            else if ( key == "max_events_per_month" ) activeConfig.max_events_per_month = std::stoul( val );
            else if ( key == "trigger_on_new_week" ) activeConfig.trigger_on_new_week = parseBoolValue( val, true );
            else if ( key == "trigger_on_new_month" ) activeConfig.trigger_on_new_month = parseBoolValue( val, true );
            else if ( key == "trigger_on_hero_level_up" ) activeConfig.trigger_on_hero_level_up = parseBoolValue( val, true );
            else if ( key == "trigger_on_castle_capture" ) activeConfig.trigger_on_castle_capture = parseBoolValue( val, true );
            else if ( key == "trigger_on_battle_victory" ) activeConfig.trigger_on_battle_victory = parseBoolValue( val, true );
            else if ( key == "allow_repeat_storylines" ) activeConfig.allow_repeat_storylines = parseBoolValue( val, false );

            // [AutoPlay_Interruption]
            else if ( key == "autoplay_force_manual_decision" ) activeConfig.autoplay_force_manual_decision = parseBoolValue( val, true );
            else if ( key == "autoplay_alert_sound" ) activeConfig.autoplay_alert_sound = parseBoolValue( val, true );
            else if ( key == "autoplay_flash_window" ) activeConfig.autoplay_flash_window = parseBoolValue( val, true );
            else if ( key == "autoplay_auto_resume" ) activeConfig.autoplay_auto_resume = parseBoolValue( val, true );
            else if ( key == "autoplay_timeout_fallback_seconds" ) activeConfig.autoplay_timeout_fallback_seconds = std::stoul( val );
            else if ( key == "autoplay_show_interruption_banner" ) activeConfig.autoplay_show_interruption_banner = parseBoolValue( val, true );

            // [Outcomes_Resources]
            else if ( key == "enable_resource_outcomes" ) activeConfig.enable_resource_outcomes = parseBoolValue( val, true );
            else if ( key == "gold_reward_multiplier" ) activeConfig.gold_reward_multiplier = std::stod( val );
            else if ( key == "gold_cost_multiplier" ) activeConfig.gold_cost_multiplier = std::stod( val );
            else if ( key == "rare_resource_multiplier" ) activeConfig.rare_resource_multiplier = std::stod( val );
            else if ( key == "max_gold_reward" ) activeConfig.max_gold_reward = std::stoi( val );
            else if ( key == "max_gold_cost" ) activeConfig.max_gold_cost = std::stoi( val );
            else if ( key == "prevent_bankruptcy" ) activeConfig.prevent_bankruptcy = parseBoolValue( val, true );
            else if ( key == "allow_resource_theft_events" ) activeConfig.allow_resource_theft_events = parseBoolValue( val, true );

            // [Outcomes_Armies]
            else if ( key == "enable_army_outcomes" ) activeConfig.enable_army_outcomes = parseBoolValue( val, true );
            else if ( key == "troop_reward_multiplier" ) activeConfig.troop_reward_multiplier = std::stod( val );
            else if ( key == "troop_loss_multiplier" ) activeConfig.troop_loss_multiplier = std::stod( val );
            else if ( key == "prefer_faction_creatures" ) activeConfig.prefer_faction_creatures = parseBoolValue( val, true );
            else if ( key == "allow_high_tier_creatures" ) activeConfig.allow_high_tier_creatures = parseBoolValue( val, true );
            else if ( key == "max_creature_reward_tier" ) activeConfig.max_creature_reward_tier = std::stoul( val );
            else if ( key == "require_free_army_slot" ) activeConfig.require_free_army_slot = parseBoolValue( val, false );
            else if ( key == "grant_to_garrison_if_hero_full" ) activeConfig.grant_to_garrison_if_hero_full = parseBoolValue( val, true );

            // [Outcomes_Stats_Magic]
            else if ( key == "enable_hero_stat_outcomes" ) activeConfig.enable_hero_stat_outcomes = parseBoolValue( val, true );
            else if ( key == "max_primary_stat_gain" ) activeConfig.max_primary_stat_gain = std::stoi( val );
            else if ( key == "allow_stat_penalties" ) activeConfig.allow_stat_penalties = parseBoolValue( val, true );
            else if ( key == "enable_hero_xp_outcomes" ) activeConfig.enable_hero_xp_outcomes = parseBoolValue( val, true );
            else if ( key == "hero_xp_multiplier" ) activeConfig.hero_xp_multiplier = std::stod( val );
            else if ( key == "enable_rpg_xp_outcomes" ) activeConfig.enable_rpg_xp_outcomes = parseBoolValue( val, true );
            else if ( key == "rpg_xp_multiplier" ) activeConfig.rpg_xp_multiplier = std::stod( val );
            else if ( key == "enable_spell_teaching" ) activeConfig.enable_spell_teaching = parseBoolValue( val, true );
            else if ( key == "enable_mana_restore" ) activeConfig.enable_mana_restore = parseBoolValue( val, true );

            // [Outcomes_Morale_Luck]
            else if ( key == "enable_morale_luck_outcomes" ) activeConfig.enable_morale_luck_outcomes = parseBoolValue( val, true );
            else if ( key == "max_morale_bonus" ) activeConfig.max_morale_bonus = std::stoi( val );
            else if ( key == "max_luck_bonus" ) activeConfig.max_luck_bonus = std::stoi( val );
            else if ( key == "include_rpg_doctrine_buffs" ) activeConfig.include_rpg_doctrine_buffs = parseBoolValue( val, true );
            else if ( key == "allow_cursed_events" ) activeConfig.allow_cursed_events = parseBoolValue( val, true );

            // [UI_Display]
            else if ( key == "show_detailed_consequences" ) activeConfig.show_detailed_consequences = parseBoolValue( val, true );
            else if ( key == "choice_button_font_size" ) activeConfig.choice_button_font_size = std::stoul( val );
            else if ( key == "play_event_fanfare" ) activeConfig.play_event_fanfare = parseBoolValue( val, true );
            else if ( key == "play_choice_confirm_sound" ) activeConfig.play_choice_confirm_sound = parseBoolValue( val, true );
            else if ( key == "event_dialog_width" ) activeConfig.event_dialog_width = std::stoul( val );
            else if ( key == "event_dialog_min_height" ) activeConfig.event_dialog_min_height = std::stoul( val );
        }
    }

    void saveDefaultEventConfigFile( const std::string & filePath )
    {
        std::ofstream out( filePath );
        if ( !out ) {
            return;
        }

        out << "# ==============================================================================\n"
            << "# Homm2RPG - Crusader Kings Random Events & Ollama AI Configuration\n"
            << "# Location: Documents/Homm2RPG/config.ini\n"
            << "# Total Settings: 68\n"
            << "# ==============================================================================\n\n"
            << "[Ollama]\n"
            << "# Master toggle for local LLM generation via Ollama.\n"
            << "ollama_enabled = true\n\n"
            << "# The HTTP API endpoint for Ollama generation.\n"
            << "ollama_url = http://localhost:11434/api/generate\n\n"
            << "# The Ollama model to use (e.g., llama3, llama3.2, mistral, gemma2, phi3, qwen2).\n"
            << "ollama_model = llama3\n\n"
            << "# Maximum HTTP timeout in milliseconds. If Ollama takes longer, fallback events are used.\n"
            << "ollama_timeout_ms = 15000\n\n"
            << "# LLM sampling temperature (0.0 = deterministic, 1.0 = creative fantasy prose).\n"
            << "ollama_temperature = 0.80\n\n"
            << "# Top-p nucleus sampling probability.\n"
            << "ollama_top_p = 0.90\n\n"
            << "# Maximum token count generated per event.\n"
            << "ollama_max_tokens = 400\n\n"
            << "# Keep-alive duration to keep model in Ollama memory (e.g., 5m, 10m, 1h).\n"
            << "ollama_keep_alive = 5m\n\n"
            << "# Number of retry attempts on network error before falling back to procedural engine.\n"
            << "ollama_retry_count = 1\n\n"
            << "# Pre-generate upcoming events in a background thread to prevent game hitching.\n"
            << "ollama_async_prefetch = true\n\n"
            << "# Include past event choices in prompt for narrative continuity across the campaign.\n"
            << "ollama_context_memory = true\n\n"
            << "# Maximum number of past events remembered for narrative continuity.\n"
            << "ollama_max_history_events = 5\n\n"
            << "# Custom system prompt override (leave empty to use default Crusader Kings persona).\n"
            << "ollama_custom_system_prompt = \n\n"
            << "# Write Ollama prompts, raw JSON, and latency to Documents/Homm2RPG/events.log.\n"
            << "ollama_debug_log = false\n\n\n"
            << "[Events_General]\n"
            << "# Master toggle for all Crusader Kings style random events.\n"
            << "events_master_enabled = true\n\n"
            << "# Base percentage chance each day for a random event to trigger (1-100).\n"
            << "daily_event_chance_percent = 25\n\n"
            << "# Minimum days cooldown between any two random events.\n"
            << "min_days_between_events = 3\n\n"
            << "# Maximum days before an event is forced to trigger (guarantees pacing).\n"
            << "max_days_between_events = 7\n\n"
            << "# Earliest map day before any event can occur (lets kingdom get established).\n"
            << "first_event_day = 2\n\n"
            << "# Maximum number of random events allowed per in-game month.\n"
            << "max_events_per_month = 6\n\n"
            << "# Guaranteed event check on Day 1 of each new week.\n"
            << "trigger_on_new_week = true\n\n"
            << "# Guaranteed event check on Day 1 of each new month (Astrologers Proclaim).\n"
            << "trigger_on_new_month = true\n\n"
            << "# Opportunity for a dilemma when a kingdom hero gains a level.\n"
            << "trigger_on_hero_level_up = true\n\n"
            << "# Opportunity for a conquest event upon capturing an enemy castle or town.\n"
            << "trigger_on_castle_capture = true\n\n"
            << "# Opportunity for an event dilemma after winning significant battles.\n"
            << "trigger_on_battle_victory = true\n\n"
            << "# Allow the exact same procedural storyline to trigger more than once per map.\n"
            << "allow_repeat_storylines = false\n\n\n"
            << "[AutoPlay_Interruption]\n"
            << "# CRITICAL: Even if auto-play is ON, pause AI execution and force manual player decision.\n"
            << "autoplay_force_manual_decision = true\n\n"
            << "# Play alert sound / fanfare when an event interrupts auto-play.\n"
            << "autoplay_alert_sound = true\n\n"
            << "# Signal window attention when a royal decision is awaiting the player.\n"
            << "autoplay_flash_window = true\n\n"
            << "# Automatically resume AI auto-play immediately after the player confirms a decision.\n"
            << "autoplay_auto_resume = true\n\n"
            << "# Timeout fallback in seconds (0 = wait indefinitely for the player to click).\n"
            << "autoplay_timeout_fallback_seconds = 0\n\n"
            << "# Display prominent header banner indicating Auto-Play is paused for Royal Decision.\n"
            << "autoplay_show_interruption_banner = true\n\n\n"
            << "[Outcomes_Resources]\n"
            << "# Enable events and choices that grant or consume kingdom gold and resources.\n"
            << "enable_resource_outcomes = true\n\n"
            << "# Multiplier applied to all gold rewards from event choices (e.g., 1.5 = +50%).\n"
            << "gold_reward_multiplier = 1.0\n\n"
            << "# Multiplier applied to all gold costs from event choices.\n"
            << "gold_cost_multiplier = 1.0\n\n"
            << "# Multiplier for rare resources (wood, ore, mercury, sulfur, crystal, gems).\n"
            << "rare_resource_multiplier = 1.0\n\n"
            << "# Maximum gold that can be awarded in a single event choice.\n"
            << "max_gold_reward = 10000\n\n"
            << "# Maximum gold that can be deducted in a single event choice.\n"
            << "max_gold_cost = 5000\n\n"
            << "# Prevent gold costs from driving the kingdom treasury below zero.\n"
            << "prevent_bankruptcy = true\n\n"
            << "# Allow bandit/saboteur events that demand tribute or steal supplies.\n"
            << "allow_resource_theft_events = true\n\n\n"
            << "[Outcomes_Armies]\n"
            << "# Enable choices that recruit or eliminate creature troops.\n"
            << "enable_army_outcomes = true\n\n"
            << "# Multiplier applied to creature troop rewards.\n"
            << "troop_reward_multiplier = 1.0\n\n"
            << "# Multiplier applied to creature troop losses / casualties.\n"
            << "troop_loss_multiplier = 1.0\n\n"
            << "# Bias creature recruits to match the hero's faction alignment (Knight, Barbarian, etc.).\n"
            << "prefer_faction_creatures = true\n\n"
            << "# Allow rare choices to grant tier-6 creatures (Dragons, Titans, Phoenixes, Crusaders).\n"
            << "allow_high_tier_creatures = true\n\n"
            << "# Maximum creature tier that can be awarded (1 to 6).\n"
            << "max_creature_reward_tier = 6\n\n"
            << "# Require an empty army slot or matching stack before granting troops.\n"
            << "require_free_army_slot = false\n\n"
            << "# If hero army slots are full, deposit recruited creatures into nearest castle garrison.\n"
            << "grant_to_garrison_if_hero_full = true\n\n\n"
            << "[Outcomes_Stats_Magic]\n"
            << "# Enable choices that modify primary skills (Attack, Defense, Spell Power, Knowledge).\n"
            << "enable_hero_stat_outcomes = true\n\n"
            << "# Maximum primary skill points gained in a single event choice.\n"
            << "max_primary_stat_gain = 2\n\n"
            << "# Allow dangerous choices that risk temporary or permanent stat penalties.\n"
            << "allow_stat_penalties = true\n\n"
            << "# Enable choices that grant Hero experience points.\n"
            << "enable_hero_xp_outcomes = true\n\n"
            << "# Multiplier for Hero experience rewards.\n"
            << "hero_xp_multiplier = 1.0\n\n"
            << "# Enable choices that grant Kingdom RPG Profile experience.\n"
            << "enable_rpg_xp_outcomes = true\n\n"
            << "# Multiplier for Kingdom RPG Profile experience rewards.\n"
            << "rpg_xp_multiplier = 1.0\n\n"
            << "# Allow event choices to teach new spells directly into the hero's spellbook.\n"
            << "enable_spell_teaching = true\n\n"
            << "# Allow event choices to restore hero spell points / mana.\n"
            << "enable_mana_restore = true\n\n\n"
            << "[Outcomes_Morale_Luck]\n"
            << "# Enable choices that grant or reduce hero morale and luck.\n"
            << "enable_morale_luck_outcomes = true\n\n"
            << "# Maximum morale bonus or penalty awarded by an event choice.\n"
            << "max_morale_bonus = 3\n\n"
            << "# Maximum luck bonus or penalty awarded by an event choice.\n"
            << "max_luck_bonus = 3\n\n"
            << "# Allow rare event choices to award temporary RPG doctrine combat bonuses.\n"
            << "include_rpg_doctrine_buffs = true\n\n"
            << "# Allow cursed / dark omens that inflict negative luck and morale.\n"
            << "allow_cursed_events = true\n\n\n"
            << "[UI_Display]\n"
            << "# Show Crusader Kings style detailed consequence previews on choice buttons (false hides outcomes until selected).\n"
            << "show_detailed_consequences = false\n\n"
            << "# Font style for choice options (0 = auto-fit, 1 = normal, 2 = small).\n"
            << "choice_button_font_size = 0\n\n"
            << "# Play fanfare or medieval theme when the event dialog window opens.\n"
            << "play_event_fanfare = true\n\n"
            << "# Play confirmation click sound when player confirms a royal decision.\n"
            << "play_choice_confirm_sound = true\n\n"
            << "# Width of the Crusader Kings event dialog window in pixels.\n"
            << "event_dialog_width = 560\n\n"
            << "# Minimum height of the event dialog window in pixels.\n"
            << "event_dialog_min_height = 380\n";
    }

    void onTurnStart( Kingdom & kingdom )
    {
        const EventConfig & conf = getEventConfig();
        if ( !conf.events_master_enabled ) {
            return;
        }

        const uint32_t currentDay = world.CountDay();
        if ( currentDay < conf.first_event_day ) {
            return;
        }

        const uint32_t currentMonth = ( currentDay - 1 ) / 28 + 1;
        if ( currentMonth != trackedMonthIndex ) {
            trackedMonthIndex = currentMonth;
            eventsInCurrentMonth = 0;
        }

        if ( eventsInCurrentMonth >= conf.max_events_per_month ) {
            return;
        }

        const uint32_t daysSinceLast = lastEventTurnDay == 0 ? currentDay : ( currentDay - lastEventTurnDay );
        if ( daysSinceLast < conf.min_days_between_events ) {
            return;
        }

        bool trigger = false;
        const bool isNewWeek = ( ( currentDay - 1 ) % 7 == 0 );
        const bool isNewMonth = ( ( currentDay - 1 ) % 28 == 0 );

        if ( isNewMonth && conf.trigger_on_new_month ) {
            trigger = true;
        }
        else if ( isNewWeek && conf.trigger_on_new_week ) {
            trigger = true;
        }
        else if ( daysSinceLast >= conf.max_days_between_events ) {
            trigger = true;
        }
        else if ( randomRange( 1, 100 ) <= conf.daily_event_chance_percent ) {
            trigger = true;
        }

        if ( trigger ) {
            startBackgroundEventGeneration( kingdom, false );
        }
    }

    void onBattleVictory( const PlayerColor color, const int32_t /*heroId*/ )
    {
        const EventConfig & conf = getEventConfig();
        if ( !conf.events_master_enabled || !conf.trigger_on_battle_victory ) {
            return;
        }
        if ( color == static_cast<PlayerColor>( 0 ) ) {
            return;
        }
        Kingdom & kingdom = world.GetKingdom( color );
        if ( randomRange( 1, 100 ) <= 35 ) {
            startBackgroundEventGeneration( kingdom, true );
        }
    }

    void onCastleCapture( const PlayerColor color, const int32_t /*heroId*/ )
    {
        const EventConfig & conf = getEventConfig();
        if ( !conf.events_master_enabled || !conf.trigger_on_castle_capture ) {
            return;
        }
        if ( color == static_cast<PlayerColor>( 0 ) ) {
            return;
        }
        Kingdom & kingdom = world.GetKingdom( color );
        startBackgroundEventGeneration( kingdom, true );
    }

    void onHeroLevelUp( const PlayerColor color, const int32_t /*heroId*/ )
    {
        const EventConfig & conf = getEventConfig();
        if ( !conf.events_master_enabled || !conf.trigger_on_hero_level_up ) {
            return;
        }
        if ( color == static_cast<PlayerColor>( 0 ) ) {
            return;
        }
        Kingdom & kingdom = world.GetKingdom( color );
        if ( randomRange( 1, 100 ) <= 30 ) {
            startBackgroundEventGeneration( kingdom, true );
        }
    }

    bool startBackgroundEventGeneration( Kingdom & kingdom, const bool force )
    {
        const EventConfig & conf = getEventConfig();
        if ( !conf.events_master_enabled && !force ) {
            return false;
        }

        const PlayerColor color = kingdom.GetColor();
        if ( color == static_cast<PlayerColor>( 0 ) ) {
            return false;
        }

        // If an event is already ready, do not overwrite it
        {
            std::lock_guard<std::mutex> lock( eventMutex );
            if ( pendingEvent.ready ) {
                return false;
            }
        }

        // If background generation is already in flight, avoid duplicate worker threads
        if ( isGenerating.load() ) {
            return false;
        }

        // Capture kingdom context and procedural fallback on the main thread
        const std::string prompt = buildCkPrompt( kingdom );
        RandomEvent fallback = generateProceduralCkEvent( kingdom );
        EventConfig threadConf = conf;

        if ( !conf.ollama_enabled ) {
            std::lock_guard<std::mutex> lock( eventMutex );
            pendingEvent.event = std::move( fallback );
            pendingEvent.color = color;
            pendingEvent.ready = true;
            logEventMessage( "Procedural Crusader Kings event ready: " + pendingEvent.event.title );
            return true;
        }

        if ( workerThread.joinable() ) {
            workerThread.join();
        }

        isGenerating = true;
        logEventMessage( "Initiating background Ollama generation for " + Color::String( color ) + " kingdom..." );

        workerThread = std::thread( [prompt, threadConf, fallback = std::move( fallback ), color]() mutable {
            RandomEvent event;
            bool success = false;

            if ( !shutdownWorker.load() ) {
                const std::string response = requestOllamaGeneration( prompt, threadConf );
                if ( !response.empty() && parseOllamaEventJson( response, event ) ) {
                    success = true;
                    logEventMessage( "Background Ollama generation succeeded: " + event.title );
                }
            }

            if ( !success ) {
                event = std::move( fallback );
                logEventMessage( "Background Ollama query fell back to procedural event: " + event.title );
            }

            if ( !shutdownWorker.load() ) {
                std::lock_guard<std::mutex> lock( eventMutex );
                pendingEvent.event = std::move( event );
                pendingEvent.color = color;
                pendingEvent.ready = true;
            }

            isGenerating = false;
        } );

        return true;
    }

    bool hasReadyEvent( const PlayerColor color )
    {
        std::lock_guard<std::mutex> lock( eventMutex );
        return pendingEvent.ready && ( color == static_cast<PlayerColor>( 0 ) || pendingEvent.color == color );
    }

    bool isEventGenerationInProgress()
    {
        return isGenerating.load();
    }

    void cancelBackgroundEventGeneration()
    {
        shutdownWorker = true;
        if ( workerThread.joinable() ) {
            workerThread.join();
        }
    }

    void resetPendingEvents()
    {
        std::lock_guard<std::mutex> lock( eventMutex );
        pendingEvent.ready = false;
        pendingEvent.color = static_cast<PlayerColor>( 0 );
        pendingEvent.event = RandomEvent{};
    }

    bool isCombatActive()
    {
        return Battle::GetArena() != nullptr;
    }

    bool processPendingEvents( Kingdom & kingdom )
    {
        const EventConfig & conf = getEventConfig();
        if ( !conf.events_master_enabled ) {
            return false;
        }

        // CRITICAL REQUIREMENT:
        // Do NOT show events while the player is in combat!
        if ( Battle::GetArena() != nullptr ) {
            return false;
        }

        if ( kingdom.isLoss() || kingdom.GetColor() == static_cast<PlayerColor>( 0 ) ) {
            return false;
        }

        RandomEvent event;
        {
            std::lock_guard<std::mutex> lock( eventMutex );
            if ( !pendingEvent.ready || pendingEvent.color != kingdom.GetColor() ) {
                return false;
            }
            event = std::move( pendingEvent.event );
            pendingEvent.ready = false;
            pendingEvent.color = static_cast<PlayerColor>( 0 );
        }

        if ( event.title.empty() || event.choices.size() < 2 ) {
            return false;
        }

        logEventMessage( "Displaying ready Crusader Kings event (player not in combat): " + event.title );

        // Display the Crusader Kings Event Dialog
        const int chosenIndex = showCkEventDialog( kingdom, event );
        if ( chosenIndex >= 0 && static_cast<size_t>( chosenIndex ) < event.choices.size() ) {
            applyEventChoiceOutcomes( kingdom, event.choices[chosenIndex] );

            // Record history
            lastEventTurnDay = world.CountDay();
            ++eventsInCurrentMonth;
            if ( !event.id.empty() ) {
                recentStorylineHistory.push_back( event.id );
                if ( recentStorylineHistory.size() > conf.ollama_max_history_events ) {
                    recentStorylineHistory.erase( recentStorylineHistory.begin() );
                }
            }
            return true;
        }

        return false;
    }

    bool triggerRandomEvent( Kingdom & kingdom, const bool force )
    {
        // First check if an event is already ready and player is not in combat
        if ( processPendingEvents( kingdom ) ) {
            return true;
        }

        // If background generation is already in progress, avoid duplicate request
        if ( isEventGenerationInProgress() ) {
            logEventMessage( "Crusader event is already generating in the background." );
            return false;
        }

        // Kick off background generation
        return startBackgroundEventGeneration( kingdom, force );
    }

    void applyEventChoiceOutcomes( Kingdom & kingdom, const EventChoice & choice )
    {
        const EventConfig & conf = getEventConfig();

        // 1. Resources & Gold
        if ( conf.enable_resource_outcomes ) {
            // Gold
            int32_t goldChange = choice.gold;
            if ( goldChange > 0 ) {
                goldChange = static_cast<int32_t>( goldChange * conf.gold_reward_multiplier );
                goldChange = std::min( goldChange, conf.max_gold_reward );
                kingdom.AddFundsResource( Funds( Resource::GOLD, goldChange ) );
            }
            else if ( goldChange < 0 ) {
                goldChange = static_cast<int32_t>( goldChange * conf.gold_cost_multiplier );
                int32_t deduction = std::abs( goldChange );
                deduction = std::min( deduction, conf.max_gold_cost );
                if ( conf.prevent_bankruptcy ) {
                    deduction = std::min( deduction, kingdom.GetFunds().gold );
                }
                kingdom.OddFundsResource( Funds( Resource::GOLD, deduction ) );
            }

            // Rare Resources
            const auto applyResource = [&]( const int type, const int32_t rawAmount ) {
                if ( rawAmount > 0 ) {
                    const int32_t amount = std::max<int32_t>( 1, static_cast<int32_t>( rawAmount * conf.rare_resource_multiplier ) );
                    kingdom.AddFundsResource( Funds( type, amount ) );
                }
                else if ( rawAmount < 0 ) {
                    int32_t deduction = std::abs( rawAmount );
                    if ( conf.prevent_bankruptcy ) {
                        deduction = std::min( deduction, kingdom.GetFunds().Get( type ) );
                    }
                    kingdom.OddFundsResource( Funds( type, deduction ) );
                }
            };

            applyResource( Resource::WOOD, choice.wood );
            applyResource( Resource::ORE, choice.ore );
            applyResource( Resource::MERCURY, choice.mercury );
            applyResource( Resource::SULFUR, choice.sulfur );
            applyResource( Resource::CRYSTAL, choice.crystal );
            applyResource( Resource::GEMS, choice.gems );
        }

        // Active Hero selection for hero-specific outcomes
        Heroes * targetHero = nullptr;
        if ( !kingdom.GetHeroes().empty() ) {
            targetHero = kingdom.GetHeroes().front();
        }

        // 2. Hero Primary Skills
        if ( conf.enable_hero_stat_outcomes && targetHero != nullptr ) {
            const auto applySkill = [&]( const int skillType, const int32_t delta ) {
                if ( delta > 0 ) {
                    const int clamped = std::min( delta, conf.max_primary_stat_gain );
                    for ( int i = 0; i < clamped; ++i ) {
                        targetHero->IncreasePrimarySkill( skillType );
                    }
                }
            };

            applySkill( Skill::Primary::ATTACK, choice.attack );
            applySkill( Skill::Primary::DEFENSE, choice.defense );
            applySkill( Skill::Primary::POWER, choice.power );
            applySkill( Skill::Primary::KNOWLEDGE, choice.knowledge );
        }

        // 3. Experience (Hero XP & Kingdom RPG XP)
        if ( conf.enable_hero_xp_outcomes && targetHero != nullptr && choice.hero_xp > 0 ) {
            const uint32_t heroXp = static_cast<uint32_t>( choice.hero_xp * conf.hero_xp_multiplier );
            targetHero->IncreaseExperience( heroXp );
        }
        if ( conf.enable_rpg_xp_outcomes && choice.rpg_xp > 0 ) {
            const uint64_t rpgXp = static_cast<uint64_t>( choice.rpg_xp * conf.rpg_xp_multiplier );
            static_cast<void>( fheroes2::RPG::addExperience( kingdom.GetColor(), rpgXp, ExperienceKind::HERO ) );
        }

        // 4. Armies / Troops
        if ( conf.enable_army_outcomes && choice.creature_id != 0 && choice.creature_count != 0 ) {
            const Monster creature( choice.creature_id );
            if ( creature.isValid() ) {
                if ( choice.creature_count > 0 ) {
                    const int32_t count = std::max<int32_t>( 1, static_cast<int32_t>( choice.creature_count * conf.troop_reward_multiplier ) );
                    const Troop troop( creature, count );
                    bool added = false;
                    if ( targetHero != nullptr && targetHero->GetArmy().CanJoinTroop( troop ) ) {
                        targetHero->GetArmy().JoinTroop( troop );
                        added = true;
                    }
                    else if ( conf.grant_to_garrison_if_hero_full && !kingdom.GetCastles().empty() ) {
                        Castle * castle = kingdom.GetCastles().front();
                        if ( castle != nullptr && castle->GetArmy().CanJoinTroop( troop ) ) {
                            castle->GetArmy().JoinTroop( troop );
                            added = true;
                        }
                    }
                    (void)added;
                }
            }
        }

        // 5. Spells & Mana
        if ( targetHero != nullptr ) {
            if ( conf.enable_spell_teaching && choice.spell_id != 0 ) {
                if ( !targetHero->HaveSpellBook() ) {
                    targetHero->SpellBookActivate();
                }
                targetHero->AppendSpellToBook( Spell( choice.spell_id ), true );
            }
            if ( conf.enable_mana_restore && choice.mana != 0 ) {
                const int currentMana = targetHero->GetSpellPoints();
                const int maxMana = targetHero->GetMaxSpellPoints();
                if ( choice.mana > 0 ) {
                    targetHero->SetSpellPoints( std::min( currentMana + choice.mana, maxMana ) );
                }
                else {
                    targetHero->SetSpellPoints( std::max( 0, currentMana + choice.mana ) );
                }
            }
        }

        // 6. Audio / Fanfare & Morale/Luck
        if ( choice.morale > 0 || choice.luck > 0 ) {
            AudioManager::PlaySound( M82::GOODMRLE );
        }
        else if ( choice.morale < 0 || choice.luck < 0 ) {
            AudioManager::PlaySound( M82::BADMRLE );
        }
        else if ( conf.play_choice_confirm_sound ) {
            AudioManager::PlaySound( M82::KILLFADE );
        }

        // Brief royal feedback popup revealing outcomes only AFTER selection
        std::string consequences = choice.outcome_text;
        if ( consequences.empty() ) {
            std::vector<std::string> parts;
            if ( choice.gold != 0 ) parts.push_back( ( choice.gold > 0 ? "+" : "" ) + std::to_string( choice.gold ) + " Gold" );
            if ( choice.wood != 0 ) parts.push_back( ( choice.wood > 0 ? "+" : "" ) + std::to_string( choice.wood ) + " Wood" );
            if ( choice.ore != 0 ) parts.push_back( ( choice.ore > 0 ? "+" : "" ) + std::to_string( choice.ore ) + " Ore" );
            if ( choice.mercury != 0 ) parts.push_back( ( choice.mercury > 0 ? "+" : "" ) + std::to_string( choice.mercury ) + " Mercury" );
            if ( choice.sulfur != 0 ) parts.push_back( ( choice.sulfur > 0 ? "+" : "" ) + std::to_string( choice.sulfur ) + " Sulfur" );
            if ( choice.crystal != 0 ) parts.push_back( ( choice.crystal > 0 ? "+" : "" ) + std::to_string( choice.crystal ) + " Crystal" );
            if ( choice.gems != 0 ) parts.push_back( ( choice.gems > 0 ? "+" : "" ) + std::to_string( choice.gems ) + " Gems" );
            if ( choice.attack != 0 ) parts.push_back( ( choice.attack > 0 ? "+" : "" ) + std::to_string( choice.attack ) + " Attack" );
            if ( choice.defense != 0 ) parts.push_back( ( choice.defense > 0 ? "+" : "" ) + std::to_string( choice.defense ) + " Defense" );
            if ( choice.power != 0 ) parts.push_back( ( choice.power > 0 ? "+" : "" ) + std::to_string( choice.power ) + " Spell Power" );
            if ( choice.knowledge != 0 ) parts.push_back( ( choice.knowledge > 0 ? "+" : "" ) + std::to_string( choice.knowledge ) + " Knowledge" );
            if ( choice.morale != 0 ) parts.push_back( ( choice.morale > 0 ? "+" : "" ) + std::to_string( choice.morale ) + " Morale" );
            if ( choice.luck != 0 ) parts.push_back( ( choice.luck > 0 ? "+" : "" ) + std::to_string( choice.luck ) + " Luck" );
            if ( choice.hero_xp != 0 ) parts.push_back( "+" + std::to_string( choice.hero_xp ) + " Hero XP" );
            if ( choice.rpg_xp != 0 ) parts.push_back( "+" + std::to_string( choice.rpg_xp ) + " RPG XP" );
            if ( choice.creature_id != 0 && choice.creature_count != 0 ) {
                const Monster m( choice.creature_id );
                if ( m.isValid() ) {
                    parts.push_back( ( choice.creature_count > 0 ? "+" : "" ) + std::to_string( choice.creature_count ) + " " + m.GetName() );
                }
            }
            if ( !parts.empty() ) {
                consequences = "[";
                for ( size_t p = 0; p < parts.size(); ++p ) {
                    if ( p > 0 ) consequences += ", ";
                    consequences += parts[p];
                }
                consequences += "]";
            }
        }

        std::string feedback = "Royal Decree Enacted:\n\"" + choice.text + "\"";
        if ( !consequences.empty() ) {
            feedback += "\n\nConsequences:\n" + consequences;
        }
        fheroes2::showStandardTextMessage( "Decree Enacted", std::move( feedback ), Dialog::OK );
    }

    int showCkEventDialog( Kingdom & kingdom, const RandomEvent & event )
    {
        const EventConfig & conf = getEventConfig();

        // CRITICAL REQUIREMENT:
        // Even if auto-play is on, force the player to make manual event decisions.
        // AutoPlayForceManualScope guarantees isAutoPlayPopupTimeoutEnabled() returns false!
        const AutoPlayForceManualScope forceManualScope( conf.autoplay_force_manual_decision );

        if ( conf.play_event_fanfare ) {
            AudioManager::PlaySound( M82::GOODMRLE );
        }

        const Player * currentPlayer = Settings::Get().GetPlayers().GetCurrent();
        const bool isAutoPlayActive = currentPlayer != nullptr && currentPlayer->isAIAutoControlMode();

        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );
        fheroes2::Display & display = fheroes2::Display::instance();

        const size_t choiceCount = std::clamp<size_t>( event.choices.size(), 2, 4 );
        const int32_t cardHeight = 44;
        const int32_t cardSpacing = 8;
        const int32_t choicesTotalHeight = static_cast<int32_t>( choiceCount ) * ( cardHeight + cardSpacing );
        const int32_t windowWidth = std::clamp<int32_t>( conf.event_dialog_width, 500, 620 );

        fheroes2::Text narrative( event.description, fheroes2::FontType::smallWhite() );
        const int32_t storyTextHeight = narrative.height( windowWidth - 48 );
        const int32_t storyBoxHeight = std::max<int32_t>( 52, storyTextHeight + 16 );
        const int32_t bannerHeight = ( isAutoPlayActive && conf.autoplay_show_interruption_banner ) ? 46 : 40;
        const int32_t minRequiredHeight = bannerHeight + storyBoxHeight + choicesTotalHeight + 48;
        const int32_t windowHeight = std::max<int32_t>( conf.event_dialog_min_height, minRequiredHeight );

        fheroes2::StandardWindow window( windowWidth, windowHeight, true, display );
        const fheroes2::Rect area = window.activeArea();

        // Calculate card bounding boxes
        const int32_t choicesStartY = area.y + area.height - choicesTotalHeight - 24;
        std::vector<fheroes2::Rect> choiceCards;
        choiceCards.reserve( choiceCount );
        for ( size_t i = 0; i < choiceCount; ++i ) {
            choiceCards.push_back( { area.x + 16, choicesStartY + static_cast<int32_t>( i ) * ( cardHeight + cardSpacing ),
                                     area.width - 32, cardHeight } );
        }

        size_t hoveredCard = choiceCount;
        int selectedChoice = -1;
        bool redraw = true;
        LocalEvent & localEv = LocalEvent::Get();

        while ( selectedChoice < 0 && localEv.HandleEvents() ) {
            size_t newHovered = choiceCount;
            for ( size_t i = 0; i < choiceCount; ++i ) {
                if ( localEv.isMouseCursorPosInArea( choiceCards[i] ) ) {
                    newHovered = i;
                    break;
                }
            }
            if ( newHovered != hoveredCard ) {
                hoveredCard = newHovered;
                redraw = true;
            }

            if ( redraw ) {
                window.render();
                window.applyGemDecoratedCorners();

                // Top Header Banner
                const fheroes2::Rect banner{ area.x + 8, area.y + 4, area.width - 16, isAutoPlayActive ? 46 : 40 };
                window.applyTextBackgroundShading( banner );
                drawBeveledBox( banner, true );

                const fheroes2::Text title( "CRUSADER KINGS EVENT: " + event.title, fheroes2::FontType::normalYellow() );
                title.draw( banner.x + 10, banner.y + 4, banner.width - 20, display );

                std::string realmStatus = world.DateString() + " | Realm: " + Color::String( kingdom.GetColor() )
                                          + " | Treasury: " + std::to_string( kingdom.GetFunds().gold ) + " Gold";
                fheroes2::Text subtitle( realmStatus, fheroes2::FontType::smallWhite() );
                subtitle.fitToOneRow( banner.width - 20 );
                subtitle.draw( banner.x + 10, banner.y + 22, display );

                if ( isAutoPlayActive && conf.autoplay_show_interruption_banner ) {
                    fheroes2::Text alertText( "*** AUTO-PLAY PAUSED: AWAITING ROYAL DECISION ***", fheroes2::FontType::smallYellow() );
                    alertText.fitToOneRow( banner.width - 20 );
                    alertText.draw( banner.x + 10, banner.y + 34, display );
                }

                // Story Narrative Description Box
                const int32_t storyTop = banner.y + banner.height + 6;
                const int32_t storyHeight = choicesStartY - storyTop - 8;
                const fheroes2::Rect storyBox{ area.x + 12, storyTop, area.width - 24, storyHeight };
                window.applyTextBackgroundShading( storyBox );
                drawBeveledBox( storyBox, true );

                narrative.draw( storyBox.x + 10, storyBox.y + 8, storyBox.width - 20, display );

                // Render Interactive Choice Cards (outcomes hidden until choice is selected)
                for ( size_t i = 0; i < choiceCount; ++i ) {
                    const bool isHovered = ( hoveredCard == i );
                    const fheroes2::Rect & cardRoi = choiceCards[i];
                    window.applyTextBackgroundShading( cardRoi );
                    drawChoiceHighlightBox( cardRoi, isHovered );

                    // Hotkey badge and action text
                    std::string actionLabel = "[ " + std::to_string( i + 1 ) + " ] " + event.choices[i].text;
                    const fheroes2::FontType actionFont = isHovered ? fheroes2::FontType::smallYellow() : fheroes2::FontType::smallWhite();
                    fheroes2::Text actionText( actionLabel, actionFont );
                    const int32_t textHeight = actionText.height( cardRoi.width - 24 );
                    const int32_t textY = cardRoi.y + std::max<int32_t>( 4, ( cardRoi.height - textHeight ) / 2 );
                    actionText.draw( cardRoi.x + 12, textY, cardRoi.width - 24, display );
                }

                // Status footer
                fheroes2::Text footer( "Click a royal decree or press [1-" + std::to_string( choiceCount ) + "] on your keyboard.",
                                       fheroes2::FontType::smallWhite() );
                footer.fitToOneRow( area.width - 24 );
                footer.draw( area.x + 14, area.y + area.height - 18, display );

                display.render( window.totalArea() );
                redraw = false;
            }

            // Mouse click handling
            for ( size_t i = 0; i < choiceCount; ++i ) {
                if ( localEv.MouseClickLeft( choiceCards[i] ) ) {
                    selectedChoice = static_cast<int>( i );
                    break;
                }
            }

            // Keyboard number shortcuts [1-4]
            if ( selectedChoice < 0 ) {
                if ( localEv.isKeyPressed( fheroes2::Key::KEY_1 ) && choiceCount >= 1 ) selectedChoice = 0;
                else if ( localEv.isKeyPressed( fheroes2::Key::KEY_2 ) && choiceCount >= 2 ) selectedChoice = 1;
                else if ( localEv.isKeyPressed( fheroes2::Key::KEY_3 ) && choiceCount >= 3 ) selectedChoice = 2;
                else if ( localEv.isKeyPressed( fheroes2::Key::KEY_4 ) && choiceCount >= 4 ) selectedChoice = 3;
            }
        }

        return selectedChoice;
    }
}
