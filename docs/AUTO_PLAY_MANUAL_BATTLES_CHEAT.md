# Auto-play with manual battles cheat

This fork exposes the adventure-map AI auto-control feature in normal builds and keeps tactical battles under manual control.

## Command

While an adventure map is active, press **F8**.

- Confirm **Enable** to hand your adventure-map turns to the AI.
- Enabling the cheat ends the current human turn so the AI can take over immediately.
- Whenever your auto-play player enters a battle, the battle screen opens and your troops are controlled manually.
- Neutral or normal AI opponents remain AI-controlled in battle. If Auto Play-Test puts both map players under auto-control, both player sides are manual in their battle.
- **Left-click while your auto-controlled hero is moving to pause/stop auto-play.** The current hero stops immediately, no more AI hero or castle tasks are scheduled for your side that turn, and manual control is restored cleanly for your next turn.
- Press **F8** again to queue the same return to normal adventure-map control without clicking.
- While F8 auto-play is active, the adventure-map camera follows only your auto-controlled side. Enemy and unrelated AI turns still happen, but they do not steal camera focus.
- The command is ignored while a battle is already open.

The default hotkey is configurable through the normal fheroes2 hotkey file. Its entry is named **auto-play with manual battles**.

Auto Play-Test uses the same manual-battle path automatically, so its adventure map stays automated but its battles stop for player input.

## Offline progress and persistent resources

This fork also keeps one exact persistent human-player resource wallet across new maps and loaded games. Auto Play-Test is excluded so test runs cannot overwrite the real wallet.

Offline state is stored in the fheroes2 config directory as:

`offline_progress.dat`

The file stores the last-seen Unix timestamp, exact resource counts, the last known daily income, and per-resource fractional carry. Offline time is not capped. Rewards use the saved daily income prorated by real elapsed time, with fractional carry retained so repeated short offline sessions do not lose progress.

When a map starts or a save is loaded, the saved wallet replaces that map/save's resource counts and accrued rewards are applied. The return popup is presented as a **Kingdom Chronicle** with duration-based homecoming titles, themed activity text, the exact time away in days/hours/minutes/seconds, the number of resource types collected, a best-haul highlight, and the normal resource reward display.

Meaningful absences also earn a **Homecoming Reward**. Starting at 30 minutes away, the game creates deterministic expedition events that grant bonus shipments only from resources that actually produced offline income. Longer absences improve the chest tier and total bonus: Scout's Satchel (5%), Caravan Crate (10%), Royal Chest (15%), King's Vault (20%), and Legendary Hoard (25%). Royal Chest returns can produce two expedition events; King's Vault and Legendary Hoard returns can produce up to three, using different eligible resources when possible. Events are derived from the completed offline session, so reopening or reloading cannot reroll them.

Returns of at least six hours with actual production also build a persistent **Homecoming Streak**. Every 3rd qualifying return triggers a Guild Festival (+15% of the best-produced resource), every 5th triggers a Royal Jubilee (+25%), and every 10th triggers a Legendary Jubilee (+50%). The popup also tracks cumulative lifetime time spent offline. Shorter returns do not break the streak.

The offline system also has persistent **Offline Renown**. Meaningful returns earn Renown from time away, chest tier, expedition activity, streak milestones, and rare discoveries. Renown unlocks the titles Camp Steward, Road Warden, Caravan Master, Royal Quartermaster, Keeper of the Coffers, High Steward, and Legend of the Realm. Reaching a new title grants a one-time rank-up cache based on that session's strongest production.

Expeditions lasting at least one day can also produce a deterministic **Rare Discovery**. These favor rare resources that actually produced income and grant a 20% discovery cache; absences of 30 days or more guarantee a discovery and raise the discovery cache to 35%. The session seed fixes the result, so reloads cannot reroll it.

The system now also maintains a persistent **Kingdom Contract**. Five contracts rotate in order: Keep the Beacons Lit, Supply the Guilds, Caravan Charter, Prospector's Commission, and Royal Logistics. Each has a different objective based on productive offline hours, resource variety, chest strength, expedition events, rare-resource production, or a mix of those activities. Progress carries between sessions, targets scale gradually with completed contracts, and completing one grants a 20–35% cache based on that return's strongest production plus extra Renown. The next contract starts immediately and is shown in the Kingdom Chronicle.

Offline returns now also feed a persistent **Treasure Hunt**. Productive absences of at least two hours earn one map fragment; day-long absences earn an extra fragment, and a rare discovery or completed Kingdom Contract can add another, up to three fragments per return. Five fragments complete a treasure map. Overflow carries forward to the next map.

Completed maps rotate through The Cartographer's Secret, The Dragon Coast Cache, The Wizard's Lost Vault, and The Pirate King's Hoard. Opening one grants a deterministic 35–50% treasure cache on a resource that actually produced income, plus bonus Renown. The session seed fixes the reward so reloads cannot reroll it.

The state file is upgraded to version 5 to store treasure fragments and completed-map count alongside contracts, Renown, streaks, and lifetime offline seconds. Version-1 through version-4 files migrate automatically; exact resources, uncapped elapsed time, base offline-income calculation, and fractional carry remain compatible.
