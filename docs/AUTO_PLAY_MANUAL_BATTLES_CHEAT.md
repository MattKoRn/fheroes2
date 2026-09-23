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
- Town decisions made by auto-play now show the normal player-facing dialog before execution. This includes **building construction, creature recruitment, hero recruitment, and boat purchases**. Creature recruitment opens with the AI's maximum intended affordable count selected.
- Adventure-map interactions also show a **5-second auto-choice popup** before execution. This covers pickups, chests, artifacts, mines, shrines, stat/luck/morale sites, dwellings, events, teleports, monster/hero encounters, boats, barriers, and other actionable adventure objects. Generic interactions show the AI's planned interaction, while richer choice screens show the specific action or reward the AI selected.
- Choice-heavy adventure interactions show additional detail: map events display their authored event text, treasure chests display the reward selected by the AI, recruitable adventure dwellings use the normal creature-recruit window, neutral monster encounters show whether the AI will fight/recruit/accept/flee, Arena visits show the chosen primary skill, and hero level-ups show the selected secondary skill.
- Auto-play popups now display **"AI will choose in 5 seconds: ..."** with the actual planned action. The popup returns that planned result when the timer expires instead of blindly choosing Yes.
- If an unexpected popup has no explicit AI plan attached, auto-play uses a conservative fallback: unknown **Yes/No** questions default to **No**, while ordinary **OK** informational dialogs continue automatically.
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

When a map starts or a save is loaded, the saved wallet replaces that map/save's resource counts and accrued rewards are applied. The offline popup is intentionally minimal and shows only time away and the resulting resource rewards. Narrative/story text is not shown.

Meaningful absences also earn a **Homecoming Reward**. Starting at 30 minutes away, the game creates deterministic expedition events that grant bonus shipments only from resources that actually produced offline income. Longer absences improve the chest tier and total bonus: Scout's Satchel (5%), Caravan Crate (10%), Royal Chest (15%), King's Vault (20%), and Legendary Hoard (25%). Royal Chest returns can produce two expedition events; King's Vault and Legendary Hoard returns can produce up to three, using different eligible resources when possible. Events are derived from the completed offline session, so reopening or reloading cannot reroll them.

Returns of at least six hours with actual production also build a persistent **Homecoming Streak**. Every 3rd qualifying return triggers a Guild Festival (+15% of the best-produced resource), every 5th triggers a Royal Jubilee (+25%), and every 10th triggers a Legendary Jubilee (+50%). The popup also tracks cumulative lifetime time spent offline. Shorter returns do not break the streak.

The offline system also has persistent **Offline Renown**. Meaningful returns earn Renown from time away, chest tier, expedition activity, streak milestones, and rare discoveries. Renown unlocks the titles Camp Steward, Road Warden, Caravan Master, Royal Quartermaster, Keeper of the Coffers, High Steward, and Legend of the Realm. Reaching a new title grants a one-time rank-up cache based on that session's strongest production.

Expeditions lasting at least one day can also produce a deterministic **Rare Discovery**. These favor rare resources that actually produced income and grant a 20% discovery cache; absences of 30 days or more guarantee a discovery and raise the discovery cache to 35%. The session seed fixes the result, so reloads cannot reroll it.

The system now also maintains a persistent **Kingdom Contract**. Five contracts rotate in order: Keep the Beacons Lit, Supply the Guilds, Caravan Charter, Prospector's Commission, and Royal Logistics. Each has a different objective based on productive offline hours, resource variety, chest strength, expedition events, rare-resource production, or a mix of those activities. Progress carries between sessions, targets scale gradually with completed contracts, and completing one grants a 20–35% cache based on that return's strongest production plus extra Renown. The next contract starts immediately and is shown in the Kingdom Chronicle.

Offline returns now also feed a persistent **Treasure Hunt**. Productive absences of at least two hours earn one map fragment; day-long absences earn an extra fragment, and a rare discovery or completed Kingdom Contract can add another, up to three fragments per return. Five fragments complete a treasure map. Overflow carries forward to the next map.

Completed treasure maps rotate through four reward tiers. Opening one grants a deterministic 35–50% treasure cache on a resource that actually produced income, plus bonus Renown. The session seed fixes the reward so reloads cannot reroll it.

Productive returns of at least 30 minutes also build a persistent **Supply Rush** meter from 0 to 100. Every return grants meter progress from time away, homecoming tier, and saved map/player-state efficiency, so stronger kingdoms charge the meter faster. When it reaches 100, Supply Rush triggers automatically, consumes 100 meter points, and grants +20% of that session's base offline production across every resource that produced income. The meter carries its remainder forward, the payout respects resource caps, and triggering a Rush also grants bonus Renown. The offline popup is intentionally minimal and shows only time away and the resulting resource rewards.

Offline creature recruitment now works as an **unlimited-stock auto-buy pass on every offline return**. A built dwelling unlocks its creature type, but its current population is ignored. Higher tiers are considered first across all eligible settlements, and the system buys the maximum count the current treasury can afford at normal creature prices. Purchased creatures are now deployed immediately across owned castle and hero armies using overflow-safe 32-bit stack handling. Existing matching stacks are filled only to the uint32 limit, then additional free slots and other owned armies are used. Only creatures that genuinely cannot fit anywhere remain in the persistent reserve. No treasury percentage is reserved. The old version-9 fractional creature-recruitment bank is cleared and no longer affects purchases. Offline gold income remains boosted to 750% of normal saved gold income.

The state file remains version 9 for compatibility. Its legacy fractional creature-recruitment field is retained in the file format but is cleared by the new auto-buy system and no longer controls recruitment. Versions 1 through 8 still migrate automatically. Exact resources, uncapped elapsed time, and resource fractional carry remain compatible.


## Persistent creatures and adaptive enemy scaling

Every surviving creature in the persistent player's hero armies and castle garrisons is snapshotted into the offline state at turn boundaries and again when the map/session exits. On a genuinely new map, that roster replaces the map's starting player troops. If the new map does not provide enough army slots for every distinct carried creature type, the remainder stays in a persistent reserve instead of being deleted. When the persistent kingdom recruits extra heroes during a map—manually or while under AI auto-play—reserve stacks are deployed at the end of that kingdom's turn, prioritizing weaker/new heroes first. Later snapshots combine deployed creatures and any remaining reserve again. Loading an ordinary save does not re-import the roster, preventing duplication.

New-map enemy scaling compares the carried roster's actual monster strength with the map's normal starting player army strength. The multiplier has a 25% strength dead-zone, then follows a softened square-root curve and is capped at 2.5x for AI enemy hero/castle armies; neutral wandering monster stacks receive 70% of that extra scaling and are capped at 2.0x. Other human players are never scaled. Small carry-over armies therefore keep their advantage, while extremely powerful persistent armies face progressively stronger opposition.


Large creature-count displays now use an algorithmic suffix formatter. Compact army and battle stack labels use K, M, B, then generated suffixes aa, ab, ... zz, aaa, and onward rather than a fixed suffix table. The formatter itself has no fixed suffix list; actual deployed troop counts still obey the engine's 32-bit stack limit, with excess persistent creatures kept in reserve.


## Battle presentation

The battlefield hex grid is forced off in this fork. Movement-area and cursor-shadow visuals remain separate and are not disabled. The engine's normal default battle animation speed remains 5, while visible auto-play battles run one step faster at speed 6.


Enemy scaling is intentionally varied. Enemy heroes and castle garrisons receive deterministic per-map random variation around the strength-based base multiplier (roughly 70–140% of the scaling bonus), plus smaller per-stack variation while preserving the same 2.85x hard cap. Neutral wandering stacks use a wider 60–150% spread and are capped at 2.35x. Seeds are based on the map and entity/tile so reloads do not reroll the result.


Offline persistence now keeps the last-seen timestamp monotonic so system-clock rollback cannot create duplicate offline time. State-file saves are written to a temporary file and swapped into place with a backup, reducing the chance of losing the offline wallet/roster if the process stops during a save.


Offline creature recruitment now evaluates every built dwelling in every currently owned town as one kingdom-wide pool. Purchases are ranked by combat strength per gold-equivalent resource cost using the kingdom's marketplace exchange rates; tier is only a tie-breaker.
