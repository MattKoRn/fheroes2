# Homm2RPG

Homm2RPG adds a persistent, kingdom-wide RPG profile to fheroes2. Press **F9** on the adventure map to open the menu. The RPG screen uses a Heroes II-style royal-guild presentation: gem-decorated window framing, embossed/inset brown-and-gold panels, pressed category plaques, framed rank crests, guild-themed section names, and compact BUY plaques. Every visible upgrade row shows its current combat effect and the exact next-rank effect before you spend a point. The normal good/evil interface treatment is preserved. Each tab contains five upgrades and shows three at a time; use the mouse wheel or scrollbar to reach the others. Every level awards five guild points. Level 2 requires 250,000 RPG XP, and each later level requires another 100,000 XP. RPG levels remain open-ended and saved counters use 64-bit storage. Individual upgrades that represent bounded mechanics stop accepting ranks once their mechanical cap is reached. Flat creature stats are deliberately limited: Arms Training and Armor Training cap at +8, while Veteran Core caps at +4 Attack and Defense. No upgrade changes movement or game speed.

## Tabs and upgrades

The upgrade tree is now built around direct RPG combat mechanics instead of resource payouts or XP multipliers.

| Tab | Upgrades |
| --- | --- |
| ARMY | **Arms Training:** +1 creature Attack per rank, max +8; **Armor Training:** +1 creature Defense per rank, max +8; **Veteran Core:** +1 Attack and Defense per rank, max +4 each; **Blood Drinker:** life steal, capped at 20%; **Reaper:** healing from slain HP, capped at 30% |
| OFFENSE | **Ferocity:** all creature damage; **Marksman:** ranged damage; **Brawler:** melee damage; **Executioner:** bonus damage against wounded stacks; **Opening Blow:** bonus damage against untouched stacks |
| TACTICS | **Giant Slayer:** bonus damage while outnumbered; **Overwhelm:** bonus damage while outnumbering the target; **Frenzy:** bonus damage below half starting HP; **Discipline:** bonus damage at full starting HP; **Armor Piercing:** ignores part of RPG physical damage reduction |
| DEFENSE | **Iron Skin:** all physical reduction; **Arrow Ward:** ranged reduction; **Melee Guard:** melee reduction; **Last Stand:** extra reduction below half starting HP; **Bulwark:** extra reduction while outnumbered. Combined RPG physical reduction is capped at 70% before Armor Piercing |
| MAGIC | **Sorcery:** all damaging spells; **Pyromancy:** Fireball/Fireblast; **Cryomancy:** Cold Ray/Cold Ring; **Stormcraft:** Lightning Bolt/Chain Lightning; **Cataclysm:** Elemental Storm/Armageddon |
| WARDS | **Spell Ward:** all spell reduction; **Fire Ward:** fire reduction; **Cold Ward:** cold reduction; **Storm Ward:** lightning reduction; **Chaos Ward:** Elemental Storm/Armageddon reduction. Combined RPG spell reduction is capped at 70% before Arcane Piercing |
| COMMAND | **Leadership:** up to +3 Morale; **Fortune:** up to +3 Luck; **Regeneration:** heals the wounded top creature at the start of its turn, capped at 15%; **Critical Training:** up to 20% critical chance; **Brutal Criticals:** increases critical bonus damage beyond the base +50% and requires Critical Training |
| MASTERY | **Evasion:** up to 15% chance to halve incoming creature-attack damage; **Arcane Piercing:** ignores up to 60% of RPG spell reduction; **Close Quarters:** recovers part of a ranged creature's normal melee penalty; **Unyielding:** extra reduction while untouched; **Ruthless:** bonus damage against targets below half starting HP |

**Life steal and Reaper healing never resurrect dead creatures.** They can only repair the currently surviving stack. Regeneration follows the same rule. Critical hits and Evasion use independent combat rolls. Towers do not receive the creature-specific RPG affixes.

Left-click the **BUY** plaque to purchase a rank. Hold right-click on an upgrade to see the exact current effect, next-rank effect, cost, prerequisites, and available guild points. High-impact primary stats, Morale/Luck, proc chances, and sustain effects use steeper costs than ordinary percentage upgrades. The **Steward** auto-buyer compares approximate expected combat value per point instead of treating +1 Attack and +1% chance as equivalent, and it skips capped or locked upgrades.

## XP and economy

RPG XP is now only the progression currency that earns levels and guild points. **Upgrade ranks do not multiply RPG XP and no upgrade pays resource bundles.** Hero XP awards, battles, and first-time adventure actions still feed the persistent RPG profile so ordinary play advances the combat tree.

The menu shows total RPG XP and XP needed for the next level with algorithmically generated number suffixes. Battle activity adds a modest RPG XP bonus that scales with battle experience, outcome, and the opposing profile's level. Normal hero experience still contributes RPG XP as well, so the battle-specific coefficient is intentionally smaller to avoid excessive double progression. Adventure XP scales with RPG level and is awarded once per map tile for each profile, preventing repeated farming of the same site.

Opposing kingdoms receive deterministic temporary RPG profiles derived only from combat ranks the local profile has actually purchased. Enemy players vary around the player's purchased ranks; neutral monsters use a weaker 60–90% copy and do not receive free rank-1 perks. This keeps reloads stable and prevents a fresh profile from facing hidden bonuses simply because its RPG level increased. Temporary profiles are discarded when the map ends.

Offline progress still grants RPG XP for the full elapsed interval with no duration cap. It does not grant normal game resources or recruit creatures. The game's normal economy, armies, and map rules remain in control of those systems.

## Saves

Profile format version 6 performs a one-time respec when loading older RPG profiles: points spent under the previous upgrade-price curve are refunded, old slot ranks are cleared, and Steward auto-buy is disabled so the refunded points are not immediately spent for you. The migrated profile is saved immediately.

On Windows, RPG data, offline state, and standard game saves live in **Documents\\Homm2RPG**. `rpg_profile.dat` contains the RPG profile and the adventure tiles already rewarded. `offline_progress.dat` contains the offline timestamp and state. Normal game save files are in the same folder. The game copies older RPG and save files into this folder on first use without overwriting files already there. RPG profile writes use a temporary file and backup during replacement. The kingdom profile persists across maps and loaded saves.

The RPG profile uses 64-bit counters, while individual bounded combat mechanics stop accepting ranks once another rank would no longer increase their effect. The suffix formatter itself has no fixed suffix list. The game's original hero levels and resources retain their own engine limits. Older offline snapshots with creature-roster fields remain readable, but those fields are ignored and are omitted from new snapshots.
