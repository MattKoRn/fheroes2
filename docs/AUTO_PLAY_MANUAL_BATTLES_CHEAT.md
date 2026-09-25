# Auto-play with visible AI battles

This fork exposes the adventure-map AI auto-control feature in normal builds. Tactical battles for auto-play players are AI-controlled, but they are shown in full instead of being skipped or auto-resolved.

## Command

While an adventure map is active, press **F8**.

- Confirm **Enable** to hand your adventure-map turns to the AI.
- Enabling the cheat ends the current human turn so the AI can take over immediately.
- Whenever your auto-play player enters a battle, the battle screen opens and the battle AI controls your troops.
- Auto-play battles are never skipped or instant-resolved, even if the normal instant-battle option is enabled.
- Auto-play battles run at the engine's normal/default battle animation speed. Your configured battle-speed setting is restored when the battle ends.
- Popups and reports that are shown during auto-play remain visible for **5 seconds**, then close automatically so automation continues. This includes battle results, captured-artifact pages, Eagle Eye, Necromancy, assembled-artifact notices, and standard informational dialogs.
- Town decisions made by auto-play now show the normal player-facing dialog before execution. This includes **building construction, creature recruitment, hero recruitment, and boat purchases**. Creature recruitment opens with the **exact count already selected by the AI**, and that count is locked while the 5-second auto-play popup is displayed.
- Significant adventure-map interactions show a **5-second auto-choice popup** before execution. This covers chests, artifacts, mines, shrines, stat/luck/morale sites, dwellings, events, teleports, encounters, barriers, and other consequential actions. Routine visits to owned castles or friendly heroes, simple resource pickups, boat travel, and disembarking proceed without a popup.
- Choice-heavy adventure interactions show additional detail: map events display their authored event text, treasure chests display the reward selected by the AI, recruitable adventure dwellings use the normal creature-recruit window, neutral monster encounters show whether the AI will fight/recruit/accept/flee, Arena visits show the chosen primary skill, and hero level-ups show the selected secondary skill.
- Auto-play popups return the AI's planned result when the timer expires without displaying an extra decision line.
- If an unexpected popup has no explicit AI plan attached, auto-play uses a conservative fallback: unknown **Yes/No** questions default to **No**, while ordinary **OK** informational dialogs continue automatically.
- Neutral and normal AI opponents remain AI-controlled. If Auto Play-Test puts both map players under auto-control, both sides are AI-controlled while the full battle remains visible.
- **Left-click while your auto-controlled hero is moving to pause/stop auto-play.** The current hero stops immediately, no more AI hero or castle tasks are scheduled for your side that turn, and manual control is restored cleanly for your next turn.
- Press **F8** again to queue the same return to normal adventure-map control without clicking.
- While F8 auto-play is active, the adventure-map camera follows only your auto-controlled side. Enemy and unrelated AI turns still happen, but they do not steal camera focus.
- Battles involving your F8-controlled side are shown even when your kingdom is **defending during an enemy AI turn**. Those defensive battle summaries and post-battle notices use the same 5-second timed flow when no manual human side is participating.
- The command is ignored while a battle is already open.

The default hotkey is configurable through the normal fheroes2 hotkey file. Its legacy entry name remains **auto-play with manual battles** so existing hotkey files stay compatible.

Auto Play-Test uses the same visible-AI-battle path automatically, so its adventure map and battles stay automated while every battle is still shown at normal speed.

## Kingdom RPG and offline XP

Press **F9** during an adventure map to open the kingdom-wide RPG menu. Its eight tabs hold 40 upgrades, with five points awarded per level and no fixed RPG level or upgrade rank cap. The menu uses a Heroes II-style royal-guild ledger with themed council, guild, bounty-board, treasury, and travel panels. Combat, hero experience, and adventure actions still award RPG XP for leveling, but most progression upgrades now change gameplay directly rather than multiplying XP: Growth adds hybrid offense, resistance, and reward amplifiers; Battles pay conditional gold/material bounties; and Spoils, Sites, and Travel grant direct resource caches from matching adventure actions. No RPG upgrade changes movement or animation speed. See [RPG_README.md](RPG_README.md) for every upgrade and exact trigger.

The Steward auto-buyer compares each next rank's marginal effect with its point cost, weights conditional specialties by how often the profile actually triggers them, recognizes complementary sustain, critical, finishing, full-health, ward, ranged, and spell packages, and learns a hall-level playstyle from battle usage plus existing investment. It also plans a three-rank doctrine horizon and applies a modest commitment bonus to that long-term target; **Steward Emerging Focus** and **Steward Long-Term Goal** are visible in the Royal Guild Overview. RPG number labels generate suffixes as needed. The local kingdom profile is stored in `Documents\\Homm2RPG\\rpg_profile.dat` and survives map changes and save loading. Opposing players and neutral monsters receive deterministic temporary profiles derived from the local profile at map start. Enemy kingdoms use sophistication tiers based on their existing strength roll, with occasional elite rivals at RPG level 5+, while neutral monsters remain simpler. These profiles are discarded when the map ends.

Offline production awards **RPG XP only**, based on the full elapsed interval. The former saved resource wallet is never restored onto a new map or loaded game, and offline progress does not add or subtract game resources. Each map/save uses its normal treasury. Old offline snapshots with creature-roster fields remain readable, but those fields are ignored and no longer written. Offline creature auto-recruitment and its popup have been removed. Returning from the background or loading a map shows a compact popup with only the time away and the earned RPG XP / virtual offline reward values as native sprites.

## Enemy scaling

Creatures and garrisons do not carry between maps. Each new map starts with its normal armies and neutral stack counts. Opposing kingdoms receive deterministic temporary RPG profiles derived from the player's persistent RPG level and purchased upgrade ranks. A weaker ordinary kingdom roll coordinates one invested doctrine hall, a roughly equal roll coordinates two, and a 105%+ roll can coordinate up to three. At RPG level 5+, hostile kingdoms can deterministically roll as **Elite Rivals**; elite profiles use a 100–115% strength band, the highest sophistication tier, up to four invested halls, and stronger package reinforcement while still never exceeding the existing 115% cap or gaining an unpurchased doctrine. Victories against elite rivals pay +35% RPG Renown. Neutral monsters receive their own simpler one-hall profile inside the weaker 60–90% range. These profiles alter combat effects and RPG battle challenge; they never add creatures to enemy armies or neutral stacks. Allied kingdoms keep their normal combat rules.

Large creature-count displays use an algorithmic suffix formatter. Compact army and battle stack labels use K, M, B, then generated suffixes aa, ab, ... zz, aaa, and onward rather than a fixed suffix table. Actual troop counts still obey the engine's 32-bit stack limit. RPG XP labels also generate suffixes without a fixed suffix list; stored RPG XP uses 64-bit counters.


## Battle presentation

The battlefield hex grid is forced off in this fork. Movement-area and cursor-shadow visuals remain separate and are not disabled. The engine's normal default battle animation speed remains 5, while visible auto-play battles run one step faster at speed 6.


Enemy RPG profile levels and upgrade ranks vary around the player's profile at map start. Ordinary enemy kingdoms keep the 85–115% rank envelope, with one focused hall below 95%, two halls from 95–104%, and up to three halls plus stronger package reinforcement from 105% upward. Elite rivals begin appearing from RPG level 5, with a per-kingdom chance of `10 + level / 4` percent capped at 30%; they use the 100–115% upper band, can coordinate up to four halls, and receive stronger doctrine-package weighting. Neutral profiles remain a weaker 60–90% one-hall archetype. Elite status, like all temporary profiles, is regenerated on map entry and is not stored in the save file.


Offline persistence keeps the last-seen timestamp monotonic so system-clock rollback cannot create duplicate offline time. State-file saves use a temporary file and backup to protect timestamps if the process stops during a save.
