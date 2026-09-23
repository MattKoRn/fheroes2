# Homm2RPG

Homm2RPG adds a persistent, kingdom-wide RPG profile to fheroes2. Press **F9** on the adventure map to open the menu. The RPG screen uses a Heroes II-style royal-guild presentation: gem-decorated window framing, embossed/inset brown-and-gold panels, pressed category plaques, framed rank crests, guild-themed section names, and compact BUY plaques. Every visible upgrade row shows its current effect and the exact next-rank effect before you spend a point. The normal good/evil interface treatment is preserved. Each tab contains five upgrades and shows three at a time; use the mouse wheel or scrollbar to reach the others. Every level awards five guild points. Level 2 requires 250,000 RPG XP, and each later level requires another 100,000 XP. RPG levels and upgrade ranks have no fixed cap; saved numbers use 64-bit storage. Resistance is limited so armies cannot become invulnerable. No upgrade changes movement or game speed.

## Tabs and upgrades

| Tab | Upgrades |
| --- | --- |
| WAR | Might: all troop damage; Guard: physical resistance; Marksman: ranged damage; Duelist: melee damage; Underdog: bonus damage when a smaller stack attacks a larger one |
| MAGIC | Sorcery: all damaging spells; Pyromancy: Fireball/Fireblast; Cryomancy: Cold Ray/Cold Ring; Stormcraft: Lightning Bolt/Chain Lightning; Cataclysm: Elemental Storm/Armageddon |
| WARDS | Spell Ward: all spell resistance; Fire Ward; Cold Ward; Storm Ward; Chaos Ward: matching specialized resistance |
| GROWTH | War College: combined troop/spell offense; Mystic Discipline: spell defense; Veteran Drills: physical defense; Pathfinder: amplifies every adventure reward bundle; Quartermaster: amplifies every battle reward bundle |
| BATTLES | Monster Hunter: neutral wins pay gold + gems; Hero Slayer: player wins pay gold + crystal; Siege Master: castle wins pay gold + ore + wood; Defender: defensive wins pay wood + ore; Survivor: losses pay rebuilding gold + wood |
| SPOILS | Scavenger: gold + wood; Treasure Hunter: gold + gems; Relic Hunter: gems + crystal; Prospector: ore + wood; Castellan: gold + wood |
| SITES | Pilgrim: mercury + gold; Scholar: crystal + mercury; Inspiration: gems + gold; Recruiter: gold + wood; Storykeeper: gold + gems |
| TRAVEL | Wayfarer: sulfur + mercury; Mariner: gems + gold; Cartographer: gold + crystal; Merchant: gold + ore + wood; Generalist: gold + wood |

Left-click the **BUY** plaque on an upgrade to purchase its next rank. The rest of the row is no longer an accidental purchase target. Hold right-click on an upgrade for a creature-style info popup with its trigger, current effect, next-rank effect, scaling gained, point cost, available points, and recorded trigger count. The **Steward** auto-buyer compares each available rank's next marginal effect with its cost. Conditional battle and adventure specialties are weighted by how often the profile actually triggers them, so it does not blindly favor a rarely used branch. It spends points when RPG XP arrives.

## XP and economy

RPG XP comes from hero XP awards, battles, and adventure actions. Upgrade ranks no longer multiply those XP sources: progression XP is now the baseline leveling currency, while most upgrade ranks change combat or grant concrete game resources. The menu shows total XP and XP needed for the next level with algorithmically generated number suffixes; the suffix formatter has no fixed suffix list. Battle XP scales with battle experience, outcome, and the opposing profile's level. Adventure XP scales with your RPG level and is awarded once per map tile for each profile.

Battle upgrades pay their rewards after the matching battle condition resolves. The rewards are deliberately different bundles rather than reskinned XP bonuses: Monster Hunter pays gold plus gem trophies, Hero Slayer pays gold plus crystal, Siege Master pays gold/ore/wood salvage, Defender pays wood plus ore repair stores, and Survivor pays rebuilding gold plus emergency wood. Battle experience influences the base size of each bundle, the specialty rank scales it, and Quartermaster amplifies every resource in the bundle.

Spoils, Sites, and Travel upgrades generate direct, themed resource bundles after the first matching action on a map tile for the profile. For example, relic hunting yields gems plus crystal, scholarship yields crystal plus mercury, portal travel yields sulfur plus mercury, and trading yields gold plus ore and wood. Pathfinder amplifies every resource in these bundles. A rank of zero grants no RPG resource bundle, and revisiting the same tile cannot repeatedly farm the reward.

Opposing kingdoms receive temporary randomized RPG profiles based on your level and upgrade ranks when a map starts. Neutral monsters receive a separate temporary randomized profile from the same data. These profiles affect RPG combat bonuses and resistance and are discarded when the map ends. Enemy and neutral army sizes remain those supplied by the map and the game's normal rules.

Offline progress grants RPG XP for the full elapsed interval, with no duration cap. Saved kingdom income determines the XP rate; gold uses the increased offline weighting. Offline progress does not change normal wood, ore, gold, or other game resources, and it does not recruit creatures. The game's regular economy and recruitment still apply during play. Creatures and garrisons do not transfer between maps; each new map supplies its normal starting armies. The offline summary popup reports RPG XP earned.

## Saves

On Windows, RPG data, offline state, and standard game saves live in **Documents\\Homm2RPG**. `rpg_profile.dat` contains the RPG profile and the adventure tiles already rewarded. `offline_progress.dat` contains the offline timestamp and state. Normal game save files are in the same folder. The game copies older RPG and save files into this folder on first use without overwriting files already there. RPG profile writes use a temporary file and backup during replacement. The kingdom profile persists across maps and loaded saves.

The RPG profile uses 64-bit counters, so its practical maximum is the 64-bit numeric limit even though the suffix formatter itself has no fixed suffix list. The game's original hero levels and resources retain their own engine limits. Older offline snapshots with creature-roster fields remain readable, but those fields are ignored and are omitted from new snapshots.
