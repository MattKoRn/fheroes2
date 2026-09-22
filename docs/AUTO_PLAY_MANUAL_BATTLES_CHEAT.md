# Auto-play with visible AI battles

This fork exposes the adventure-map AI auto-control feature in normal builds. Tactical battles for auto-play players are AI-controlled, but they are shown in full instead of being skipped or auto-resolved.

## Command

While an adventure map is active, press **F8**.

- Confirm **Enable** to hand your adventure-map turns to the AI.
- Enabling the cheat ends the current human turn so the AI can take over immediately.
- Whenever your auto-play player enters a battle, the battle screen opens and the battle AI controls your troops.
- Auto-play battles are never skipped or instant-resolved, even if the normal instant-battle option is enabled.
- Auto-play battles run at the engine's normal/default battle animation speed. Your configured battle-speed setting is restored when the battle ends.
- Neutral and normal AI opponents remain AI-controlled. If Auto Play-Test puts both map players under auto-control, both sides are AI-controlled while the full battle remains visible.
- **Left-click while your auto-controlled hero is moving to pause/stop auto-play.** The current hero stops immediately, no more AI hero or castle tasks are scheduled for your side that turn, and manual control is restored cleanly for your next turn.
- Press **F8** again to queue the same return to normal adventure-map control without clicking.
- While F8 auto-play is active, the adventure-map camera follows only your auto-controlled side. Enemy and unrelated AI turns still happen, but they do not steal camera focus.
- The command is ignored while a battle is already open.

The default hotkey is configurable through the normal fheroes2 hotkey file. Its legacy entry name remains **auto-play with manual battles** so existing hotkey files stay compatible.

Auto Play-Test uses the same visible-AI-battle path automatically, so its adventure map and battles stay automated while every battle is still shown at normal speed.

## Offline progress and persistent resources

This fork also keeps one exact persistent human-player resource wallet across new maps and loaded games. Auto Play-Test is excluded so test runs cannot overwrite the real wallet.

Offline state is stored in the fheroes2 config directory as:

`offline_progress.dat`

The file stores the last-seen Unix timestamp, exact resource counts, the last known daily income, per-resource fractional carry, and a snapshot of the player's map position. Offline time is not capped. The base resource mix still comes from the engine's real kingdom income, which already includes owned mines, settlements, hero Estates, resource artifacts, campaign bonuses, and player handicap. That income is then multiplied by a saved **offline state efficiency** based on owned castles, towns, heroes, mines, and artifacts, capped at 150%. Fractional carry is retained so repeated short offline sessions do not lose progress.

When a map starts or a save is loaded, the saved wallet replaces that map/save's resource counts and accrued rewards are applied. The offline popup uses a fixed compact layout so it stays inside the game window: time/lifetime, economy/state/Rush, streak/Renown, title progress, one concise bonus line, one contract/treasure line, an optional one-line creature-recruitment result, and the normal resource reward display. Narrative/story text is not shown.

Meaningful absences also earn a **Homecoming Reward**. Starting at 30 minutes away, the game creates deterministic expedition events that grant bonus shipments only from resources that actually produced offline income. Longer absences improve the chest tier and total bonus: Scout's Satchel (5%), Caravan Crate (10%), Royal Chest (15%), King's Vault (20%), and Legendary Hoard (25%). Royal Chest returns can produce two expedition events; King's Vault and Legendary Hoard returns can produce up to three, using different eligible resources when possible. Events are derived from the completed offline session, so reopening or reloading cannot reroll them.

Returns of at least six hours with actual production also build a persistent **Homecoming Streak**. Every 3rd qualifying return triggers a Guild Festival (+15% of the best-produced resource), every 5th triggers a Royal Jubilee (+25%), and every 10th triggers a Legendary Jubilee (+50%). The popup also tracks cumulative lifetime time spent offline. Shorter returns do not break the streak.

The offline system also has persistent **Offline Renown**. Meaningful returns earn Renown from time away, chest tier, expedition activity, streak milestones, and rare discoveries. Renown unlocks the titles Camp Steward, Road Warden, Caravan Master, Royal Quartermaster, Keeper of the Coffers, High Steward, and Legend of the Realm. Reaching a new title grants a one-time rank-up cache based on that session's strongest production.

Expeditions lasting at least one day can also produce a deterministic **Rare Discovery**. These favor rare resources that actually produced income and grant a 20% discovery cache; absences of 30 days or more guarantee a discovery and raise the discovery cache to 35%. The session seed fixes the result, so reloads cannot reroll it.

The system now also maintains a persistent **Kingdom Contract**. Five contracts rotate in order: Keep the Beacons Lit, Supply the Guilds, Caravan Charter, Prospector's Commission, and Royal Logistics. Each has a different objective based on productive offline hours, resource variety, chest strength, expedition events, rare-resource production, or a mix of those activities. Progress carries between sessions, targets scale gradually with completed contracts, and completing one grants a 20–35% cache based on that return's strongest production plus extra Renown. The next contract starts immediately and is shown in the Kingdom Chronicle.

Offline returns now also feed a persistent **Treasure Hunt**. Productive absences of at least two hours earn one map fragment; day-long absences earn an extra fragment, and a rare discovery or completed Kingdom Contract can add another, up to three fragments per return. Five fragments complete a treasure map. Overflow carries forward to the next map.

Completed treasure maps rotate through four reward tiers. Opening one grants a deterministic 35–50% treasure cache on a resource that actually produced income, plus bonus Renown. The session seed fixes the reward so reloads cannot reroll it.

Productive returns of at least 30 minutes also build a persistent **Supply Rush** meter from 0 to 100. Every return grants meter progress from time away, homecoming tier, and saved map/player-state efficiency, so stronger kingdoms charge the meter faster. When it reaches 100, Supply Rush triggers automatically, consumes 100 meter points, and grants +20% of that session's base offline production across every resource that produced income. The meter carries its remainder forward, the payout respects resource caps, and triggering a Rush also grants bonus Renown. The compact popup shows the current Rush meter and only adds one extra line when a Rush fires.

Offline progress can also **mobilize creatures from owned settlements**. This is deliberately not extra dwelling growth: recruitment only uses creatures already available in built dwellings and pays their normal resource cost. Recruitment starts after 12 hours for tier 1, then unlocks tiers 2–5 at 18, 24, 36, and 72 hours; tier 6 requires a full 7-day absence. Per dwelling, a return is capped at roughly half one normal week's base growth, with saved kingdom-state efficiency increasing the quantity cap but never unlocking a tier earlier; the system also reserves at least 80% of the current treasury by limiting offline recruitment to a 20% mobilization budget. It prefers stronger creatures first, respects garrison/hero army space, and never recruits from more settlements than were present in the saved offline state.

The state file is upgraded to version 7 to store the Supply Rush meter alongside the map/player-state snapshot, treasure fragments, contracts, Renown, streaks, and lifetime offline seconds. Version-1 through version-6 files migrate automatically; older state snapshots still begin at 100% state efficiency until the next live kingdom snapshot, and the new Rush meter starts at 0. Exact resources, uncapped elapsed time, and fractional carry remain compatible.
