# Homm2RPG

Homm2RPG adds a persistent, kingdom-wide RPG profile to fheroes2. Press **F9** on the adventure map to open the menu. The RPG screen now uses a Heroes II-style royal-guild presentation: gold-framed panels, a ledger header, themed guild/board section names, eight tabs of five upgrades, and the normal good/evil interface treatment. Each tab shows three upgrades at a time; use the mouse wheel or its scrollbar to reach the others. Every level awards five upgrade points. Level 2 requires 250,000 RPG XP, and each later level requires another 100,000 XP. RPG levels and upgrade ranks have no fixed cap; saved numbers use 64-bit storage. Resistance is limited so armies cannot become invulnerable. No upgrade changes movement or game speed.

## Tabs and upgrades

| Tab | Upgrades |
| --- | --- |
| War | Might: army damage; Guard: physical resistance; Marksman: ranged damage; Duelist: melee damage; Underdog: damage while outnumbered |
| Magic | Sorcery: spell damage; Pyromancy: fire; Cryomancy: cold; Stormcraft: lightning; Cataclysm: wide-area spells |
| Wards | Spell Ward: all spell resistance; Fire Ward; Cold Ward; Storm Ward; Chaos Ward: matching spell resistance |
| Growth | War College: hybrid physical/spell offense; Mystic Discipline: spell resistance; Veteran Drills: physical resistance; Pathfinder: stronger adventure resource caches; Quartermaster: stronger battle bounties |
| Battles | Monster Hunter: gold for neutral victories; Hero Slayer: gold for player victories; Siege Master: gold + ore for castle victories; Defender: wood for defensive victories; Survivor: rebuilding gold after losses |
| Spoils | Scavenger: gold from resource sites; Treasure Hunter: gold from treasure sites; Relic Hunter: gems from relic sites; Prospector: ore from production sites; Castellan: gold from castle visits |
| Sites | Pilgrim: mercury from shrines; Scholar: crystal from training sites; Inspiration: gems from morale/luck sites; Recruiter: gold from dwellings; Storykeeper: gold from events |
| Travel | Wayfarer: sulfur from portals; Mariner: gems from sea sites; Cartographer: gold from map-discovery sites; Merchant: large gold caches from trade sites; Generalist: gold from other actionable sites |

Left-click an upgrade to buy its next rank. Hold right-click on an upgrade for a creature-style info popup with its trigger, current and next effects, point cost, and available points. The **Steward** auto-buyer compares each available rank's next marginal effect with its cost. Conditional battle and adventure specialties are weighted by how often the profile actually triggers them, so it does not blindly favor a rarely used branch. It spends points when RPG XP arrives.

## XP and economy

RPG XP comes from hero XP awards, battles, and adventure actions. Upgrade ranks no longer multiply those XP sources: progression XP is now the baseline leveling currency, while most upgrade ranks change combat or grant concrete game resources. The menu shows total XP and XP needed for the next level with algorithmically generated number suffixes; the suffix formatter has no fixed suffix list. Battle XP scales with battle experience, outcome, and the opposing profile's level. Adventure XP scales with your RPG level and is awarded once per map tile for each profile.

Battle upgrades pay their rewards after the matching battle condition resolves. Monster Hunter and Hero Slayer pay gold bounties, Siege Master pays gold and ore salvage, Defender pays wood for a defensive win, and Survivor pays rebuilding gold after a loss. Quartermaster amplifies all of those rewards.

Spoils, Sites, and Travel upgrades generate direct resource caches after the first matching action on a map tile for the profile. Pathfinder amplifies these caches. A rank of zero grants no resource cache, and revisiting the same tile cannot repeatedly farm the RPG reward.

Opposing kingdoms receive temporary randomized RPG profiles based on your level and upgrade ranks when a map starts. Neutral monsters receive a separate temporary randomized profile from the same data. These profiles affect RPG combat bonuses and resistance and are discarded when the map ends. Enemy and neutral army sizes remain those supplied by the map and the game's normal rules.

Offline progress grants RPG XP for the full elapsed interval, with no duration cap. Saved kingdom income determines the XP rate; gold uses the increased offline weighting. Offline progress does not change normal wood, ore, gold, or other game resources, and it does not recruit creatures. The game's regular economy and recruitment still apply during play. Creatures and garrisons do not transfer between maps; each new map supplies its normal starting armies. The offline summary popup reports RPG XP earned.

## Saves

On Windows, RPG data, offline state, and standard game saves live in **Documents\\Homm2RPG**. `rpg_profile.dat` contains the RPG profile and the adventure tiles already rewarded. `offline_progress.dat` contains the offline timestamp and state. Normal game save files are in the same folder. The game copies older RPG and save files into this folder on first use without overwriting files already there. RPG profile writes use a temporary file and backup during replacement. The kingdom profile persists across maps and loaded saves.

The RPG profile uses 64-bit counters, so its practical maximum is the 64-bit numeric limit even though the suffix formatter itself has no fixed suffix list. The game's original hero levels and resources retain their own engine limits. Older offline snapshots with creature-roster fields remain readable, but those fields are ignored and are omitted from new snapshots.
