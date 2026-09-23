# Homm2RPG

Homm2RPG adds a persistent, kingdom-wide RPG profile to fheroes2. Press **F9** on the adventure map to open the menu. The eight tabs contain five upgrades each. Each tab shows three upgrades at a time; use the mouse wheel over the list or its up and down arrows to reach the others. Every level awards five upgrade points. Level 2 requires 250,000 RPG XP, and each later level requires another 100,000 XP. RPG levels and upgrade ranks have no fixed cap; saved numbers use 64-bit storage. Resistance is limited so armies cannot become invulnerable. No upgrade changes movement or game speed.

## Tabs and upgrades

| Tab | Upgrades |
| --- | --- |
| War | Might: army damage; Guard: physical resistance; Marksman: ranged damage; Duelist: melee damage; Underdog: damage while outnumbered |
| Magic | Sorcery: spell damage; Pyromancy: fire; Cryomancy: cold; Stormcraft: lightning; Cataclysm: wide-area spells |
| Wards | Spell Ward: all spell resistance; Fire Ward; Cold Ward; Storm Ward; Chaos Ward: matching spell resistance |
| Growth | Wisdom: all RPG XP; Meditation: offline XP; Veteran: battle XP; Explorer: adventure XP; Mentor: hero-earned XP |
| Battles | Monster Hunter: neutral battle XP; Hero Slayer: enemy hero battle XP; Siege Master: castle battle XP; Defender: defensive battle XP; Survivor: lost battle XP |
| Spoils | Scavenger: resource pickup XP; Treasure Hunter: chest XP; Relic Hunter: artifact XP; Prospector: mine XP; Castellan: castle visit XP |
| Sites | Pilgrim: shrine XP; Scholar: skill-site XP; Inspiration: morale and luck XP; Recruiter: dwelling XP; Storykeeper: event XP |
| Travel | Wayfarer: teleport XP; Mariner: sea encounter XP; Cartographer: map discovery XP; Merchant: trade-site XP; Generalist: other adventure XP |

Left-click an upgrade to buy its next rank. Hold right-click on an upgrade for a creature-style info popup with its trigger, current and next effects, point cost, and available points. **Auto-buy ROI** compares each available rank's next effect with its cost. It weights XP upgrades by how often the profile has gained XP from that source, and battle and adventure upgrades by recorded activity in their category. It spends points when XP arrives.

## XP and economy

RPG XP comes from hero XP awards, battles, and adventure actions. The menu shows total XP and XP needed for the next level with algorithmically generated number suffixes; the suffix formatter has no fixed suffix list. Battle XP scales with battle experience, outcome, and the opposing profile's level. Adventure XP scales with your RPG level and is awarded once per map tile for each profile, so revisiting a castle or resource site does not repeatedly grant XP. Opposing kingdoms receive temporary randomized RPG profiles based on your level and upgrade ranks when a map starts. Neutral monsters receive a separate temporary randomized profile from the same data. These profiles affect RPG combat bonuses and resistance and are discarded when the map ends. Enemy and neutral army sizes remain those supplied by the map and the game's normal rules.

Offline progress grants RPG XP for the full elapsed interval, with no duration cap. Saved kingdom income determines the XP rate; gold uses the increased offline weighting. Offline progress does not change normal wood, ore, gold, or other game resources, and it does not recruit creatures. The game's regular economy and recruitment still apply during play. Creatures and garrisons do not transfer between maps; each new map supplies its normal starting armies. The offline summary popup reports RPG XP earned.

## Saves

On Windows, RPG data, offline state, and standard game saves live in **Documents\\Homm2RPG**. `rpg_profile.dat` contains the RPG profile and the adventure tiles already rewarded. `offline_progress.dat` contains the offline timestamp and state. Normal game save files are in the same folder. The game copies older RPG and save files into this folder on first use without overwriting files already there. RPG profile writes use a temporary file and backup during replacement. The kingdom profile persists across maps and loaded saves.

The RPG profile uses 64-bit counters, so its practical maximum is the 64-bit numeric limit even though the suffix formatter itself has no fixed suffix list. The game's original hero levels and resources retain their own engine limits. Older offline snapshots with creature-roster fields remain readable, but those fields are ignored and are omitted from new snapshots.
