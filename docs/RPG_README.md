# Homm2RPG

Homm2RPG adds a persistent, kingdom-wide RPG profile to fheroes2. Press **F9** on the adventure map to open the menu. The RPG screen uses a Heroes II-style royal-guild presentation: gem-decorated window framing, embossed/inset brown-and-gold panels, pressed category plaques, framed rank crests, guild-themed section names, and compact BUY plaques. Every visible upgrade row shows its current combat effect and the exact next-rank effect before you spend a point. The normal good/evil interface treatment is preserved. Each tab contains five upgrades and shows three at a time; use the mouse wheel or scrollbar to reach the others. Every level awards five guild points. Level 2 requires 250,000 RPG XP, and each later level requires another 100,000 XP. RPG levels and upgrade ranks have no fixed cap; saved numbers use 64-bit storage. Resistance is limited so armies cannot become invulnerable. No upgrade changes movement or game speed.

## Tabs and upgrades

The upgrade tree is now built around direct RPG combat mechanics instead of resource payouts or XP multipliers.

| Tab | Upgrades |
| --- | --- |
| ARMY | **Arms Training:** +1 creature Attack per rank; **Armor Training:** +1 creature Defense per rank; **Veteran Core:** +1 Attack and Defense per rank; **Blood Drinker:** attacks heal the surviving stack for a percentage of actual damage dealt; **Reaper:** kills heal based on the slain creatures' hit points |
| OFFENSE | **Ferocity:** all creature damage; **Marksman:** ranged damage; **Brawler:** melee damage; **Executioner:** bonus damage against wounded stacks; **Opening Blow:** bonus damage against untouched stacks |
| TACTICS | **Giant Slayer:** bonus damage while outnumbered; **Overwhelm:** bonus damage while outnumbering the target; **Frenzy:** bonus damage below half starting HP; **Discipline:** bonus damage at full starting HP; **Armor Piercing:** ignores part of RPG physical damage reduction |
| DEFENSE | **Iron Skin:** all physical reduction; **Arrow Ward:** ranged reduction; **Melee Guard:** melee reduction; **Last Stand:** extra reduction below half starting HP; **Bulwark:** extra reduction while outnumbered |
| MAGIC | **Sorcery:** all damaging spells; **Pyromancy:** Fireball/Fireblast; **Cryomancy:** Cold Ray/Cold Ring; **Stormcraft:** Lightning Bolt/Chain Lightning; **Cataclysm:** Elemental Storm/Armageddon |
| WARDS | **Spell Ward:** all spell reduction; **Fire Ward:** fire reduction; **Cold Ward:** cold reduction; **Storm Ward:** lightning reduction; **Chaos Ward:** Elemental Storm/Armageddon reduction |
| COMMAND | **Leadership:** up to +3 Morale; **Fortune:** up to +3 Luck; **Regeneration:** heals the wounded top creature at the start of its turn; **Critical Training:** chance for a critical attack; **Brutal Criticals:** increases critical bonus damage beyond the base +50% |
| MASTERY | **Evasion:** chance to halve incoming creature-attack damage; **Arcane Piercing:** ignores part of RPG spell resistance; **Close Quarters:** recovers part of a ranged creature's normal melee penalty; **Unyielding:** extra reduction while untouched; **Ruthless:** bonus damage against targets below half starting HP |

**Life steal and Reaper healing never resurrect dead creatures.** They can only repair the currently surviving stack. Regeneration follows the same rule. Critical hits and Evasion use independent combat rolls. Towers do not receive the creature-specific RPG affixes.

Left-click the **BUY** plaque to purchase a rank. Hold right-click on an upgrade to see the exact current effect, next-rank effect, scaling gained, cost, and available guild points. The **Steward** auto-buyer chooses the available next rank with the largest immediate mechanical gain per point and skips capped upgrades once another rank would add no effect.

## XP and economy

RPG XP is now only the progression currency that earns levels and guild points. **Upgrade ranks do not multiply RPG XP and no upgrade pays resource bundles.** Hero XP awards, battles, and first-time adventure actions still feed the persistent RPG profile so ordinary play advances the combat tree.

The menu shows total RPG XP and XP needed for the next level with algorithmically generated number suffixes. Battle XP scales with battle experience, outcome, and the opposing profile's level. Adventure XP scales with RPG level and is awarded once per map tile for each profile, preventing repeated farming of the same site.

Opposing kingdoms receive temporary randomized RPG profiles based on the local profile when a map starts, and neutral monsters receive a separate temporary profile. Their ranks use the same combat-affix system, so enemy stacks can also gain Attack/Defense, wards, conditional damage, regeneration, criticals, and similar effects. These temporary profiles are discarded when the map ends.

Offline progress still grants RPG XP for the full elapsed interval with no duration cap. It does not grant normal game resources or recruit creatures. The game's normal economy, armies, and map rules remain in control of those systems.

## Saves

On Windows, RPG data, offline state, and standard game saves live in **Documents\\Homm2RPG**. `rpg_profile.dat` contains the RPG profile and the adventure tiles already rewarded. `offline_progress.dat` contains the offline timestamp and state. Normal game save files are in the same folder. The game copies older RPG and save files into this folder on first use without overwriting files already there. RPG profile writes use a temporary file and backup during replacement. The kingdom profile persists across maps and loaded saves.

The RPG profile uses 64-bit counters, so its practical maximum is the 64-bit numeric limit even though the suffix formatter itself has no fixed suffix list. The game's original hero levels and resources retain their own engine limits. Older offline snapshots with creature-roster fields remain readable, but those fields are ignored and are omitted from new snapshots.
