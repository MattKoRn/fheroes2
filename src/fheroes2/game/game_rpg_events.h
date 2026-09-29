#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Kingdom;
enum class PlayerColor : uint8_t;

namespace fheroes2::RPG
{
    // Full configuration structure for Crusader Kings style Random Events & Ollama AI
    // Loaded from and saved to Documents/Homm2RPG/config.ini
    struct EventConfig
    {
        // [Ollama] (14 settings)
        bool ollama_enabled{ true };
        std::string ollama_url{ "http://localhost:11434/api/generate" };
        std::string ollama_model{ "llama3" };
        uint32_t ollama_timeout_ms{ 15000 };
        double ollama_temperature{ 0.80 };
        double ollama_top_p{ 0.90 };
        uint32_t ollama_max_tokens{ 400 };
        std::string ollama_keep_alive{ "5m" };
        uint32_t ollama_retry_count{ 1 };
        bool ollama_async_prefetch{ true };
        bool ollama_context_memory{ true };
        uint32_t ollama_max_history_events{ 5 };
        std::string ollama_custom_system_prompt{};
        bool ollama_debug_log{ false };

        // [Events_General] (12 settings)
        bool events_master_enabled{ true };
        uint32_t daily_event_chance_percent{ 25 };
        uint32_t min_days_between_events{ 3 };
        uint32_t max_days_between_events{ 7 };
        uint32_t first_event_day{ 2 };
        uint32_t max_events_per_month{ 6 };
        bool trigger_on_new_week{ true };
        bool trigger_on_new_month{ true };
        bool trigger_on_hero_level_up{ true };
        bool trigger_on_castle_capture{ true };
        bool trigger_on_battle_victory{ true };
        bool allow_repeat_storylines{ false };

        // [AutoPlay_Interruption] (6 settings)
        bool autoplay_force_manual_decision{ true };
        bool autoplay_alert_sound{ true };
        bool autoplay_flash_window{ true };
        bool autoplay_auto_resume{ true };
        uint32_t autoplay_timeout_fallback_seconds{ 0 };
        bool autoplay_show_interruption_banner{ true };

        // [Outcomes_Resources] (8 settings)
        bool enable_resource_outcomes{ true };
        double gold_reward_multiplier{ 1.0 };
        double gold_cost_multiplier{ 1.0 };
        double rare_resource_multiplier{ 1.0 };
        int32_t max_gold_reward{ 10000 };
        int32_t max_gold_cost{ 5000 };
        bool prevent_bankruptcy{ true };
        bool allow_resource_theft_events{ true };

        // [Outcomes_Armies] (8 settings)
        bool enable_army_outcomes{ true };
        double troop_reward_multiplier{ 1.0 };
        double troop_loss_multiplier{ 1.0 };
        bool prefer_faction_creatures{ true };
        bool allow_high_tier_creatures{ true };
        uint32_t max_creature_reward_tier{ 6 };
        bool require_free_army_slot{ false };
        bool grant_to_garrison_if_hero_full{ true };

        // [Outcomes_Stats_Magic] (9 settings)
        bool enable_hero_stat_outcomes{ true };
        int32_t max_primary_stat_gain{ 2 };
        bool allow_stat_penalties{ true };
        bool enable_hero_xp_outcomes{ true };
        double hero_xp_multiplier{ 1.0 };
        bool enable_rpg_xp_outcomes{ true };
        double rpg_xp_multiplier{ 1.0 };
        bool enable_spell_teaching{ true };
        bool enable_mana_restore{ true };

        // [Outcomes_Morale_Luck] (5 settings)
        bool enable_morale_luck_outcomes{ true };
        int32_t max_morale_bonus{ 3 };
        int32_t max_luck_bonus{ 3 };
        bool include_rpg_doctrine_buffs{ true };
        bool allow_cursed_events{ true };

        // [UI_Display] (6 settings)
        bool show_detailed_consequences{ false };
        uint32_t choice_button_font_size{ 0 };
        bool play_event_fanfare{ true };
        bool play_choice_confirm_sound{ true };
        uint32_t event_dialog_width{ 560 };
        uint32_t event_dialog_min_height{ 380 };
    };

    struct EventChoice
    {
        std::string text;
        std::string outcome_text;

        // Resource outcomes
        int32_t gold{ 0 };
        int32_t wood{ 0 };
        int32_t ore{ 0 };
        int32_t mercury{ 0 };
        int32_t sulfur{ 0 };
        int32_t crystal{ 0 };
        int32_t gems{ 0 };

        // Hero primary skill outcomes
        int32_t attack{ 0 };
        int32_t defense{ 0 };
        int32_t power{ 0 };
        int32_t knowledge{ 0 };

        // Temporary morale and luck
        int32_t morale{ 0 };
        int32_t luck{ 0 };

        // Progression experience
        uint32_t hero_xp{ 0 };
        uint64_t rpg_xp{ 0 };

        // Magic
        int32_t mana{ 0 };
        int32_t spell_id{ 0 };

        // Creature recruitment or loss
        int32_t creature_id{ 0 };
        int32_t creature_count{ 0 };
    };

    struct RandomEvent
    {
        std::string id;
        std::string title;
        std::string description;
        std::vector<EventChoice> choices;
    };

    [[nodiscard]] const EventConfig & getEventConfig();
    void loadEventConfig();
    void saveDefaultEventConfigFile( const std::string & filePath );

    void onTurnStart( Kingdom & kingdom );
    void onBattleVictory( PlayerColor color, int32_t heroId );
    void onCastleCapture( PlayerColor color, int32_t heroId );
    void onHeroLevelUp( PlayerColor color, int32_t heroId );

    bool startBackgroundEventGeneration( Kingdom & kingdom, bool force = false );
    bool processPendingEvents( Kingdom & kingdom );
    bool hasReadyEvent( PlayerColor color = static_cast<PlayerColor>( 0 ) );
    bool isEventGenerationInProgress();
    void cancelBackgroundEventGeneration();
    void resetPendingEvents();
    bool isCombatActive();
    bool triggerRandomEvent( Kingdom & kingdom, bool force = false );
    void applyEventChoiceOutcomes( Kingdom & kingdom, const EventChoice & choice );
    int showCkEventDialog( Kingdom & kingdom, const RandomEvent & event );
}
