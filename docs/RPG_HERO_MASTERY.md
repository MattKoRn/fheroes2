# RPG Hero Mastery

Hero Mastery extends the persistent RPG identity layer without adding another save file or profile-version field.

## How mastery is derived

A hero's mastery score is reconstructed from data already persisted by the RPG mod:

- Hero Renown contributes directly to mastery.
- Each recorded battle victory contributes 250 mastery.
- Each recorded enemy castle capture contributes 1,000 mastery.
- Each recorded Elite Rival victory contributes 3,000 mastery.

The calculation uses saturation-safe 64-bit arithmetic. Existing saves gain mastery automatically because the system reads the existing Hero Renown and Hero Chronicle ledgers.

## Mastery thresholds

| Tier | Required mastery |
| --- | ---: |
| I | 0 |
| II | 2,500 |
| III | 10,000 |
| IV | 40,000 |
| V | 150,000 |

The strongest doctrine hall supplies the hero's current calling. If no doctrine ranks have been purchased, the hero follows the generic Adventurer title track. Respeccing or changing the kingdom doctrine emphasis can therefore change the calling while the hero's earned mastery score remains derived from their persistent accomplishments.

## Mastery milestone rewards

Hero Mastery now feeds successful expedition deeds back into the existing kingdom progression loop. When a battle victory or enemy castle capture pushes a hero across a new Mastery tier, the kingdom receives a one-time RPG XP reward:

| Newly reached tier | RPG XP reward |
| --- | ---: |
| II | 1,000 |
| III | 2,500 |
| IV | 6,000 |
| V | 12,500 |

These rewards do not add a new currency, skill screen, save field, or hidden combat modifier. The XP goes through the normal RPG experience path, so it contributes to kingdom levels and therefore to the existing Guild Points that the player spends on doctrine choices.

Reward eligibility is derived at the moment a qualifying deed is recorded by comparing the hero's Mastery score immediately before and after that deed. A single exceptional deed can legitimately cross more than one threshold and earns every newly crossed reward. Heroes already beyond a threshold do not repeatedly earn it, and old saves do not receive a retroactive lump sum simply for loading.

The compact Mastery readouts show the next threshold and the RPG XP attached to it. At Tier V they state that all Mastery rewards have been earned.

## Role title tracks

| Calling | I | II | III | IV | V |
| --- | --- | --- | --- | --- | --- |
| Adventurer | Wanderer | Pathfinder | Trailblazer | Hero | Living Legend |
| Vanguard | Line Recruit | Shieldbearer | Vanguard | War Captain | Iron Legend |
| Reaver | Skirmisher | Ravager | Reaver | Bloodlord | Doom Herald |
| Strategist | Scout | Tactician | Strategist | Battle Sage | Fatewright |
| Sentinel | Guard | Shieldbearer | Sentinel | High Sentinel | Living Fortress |
| Arcanist | Apprentice | Spellbinder | Magus | Archmage | Arcane Sovereign |
| Warden | Watcher | Runeguard | Warden | High Warden | Eternal Aegis |
| Marshal | Officer | Commander | Marshal | High Marshal | Crown General |
| Paragon | Aspirant | Champion | Paragon | Exemplar | Living Myth |

## Campaign Accolades

Heroes also earn persistent Campaign Accolades derived dynamically from their recorded battle victories, castle captures, elite rival triumphs, and personal renown:

- **Nemesis of Elite Rivals**: 5+ Elite Rival victories
- **Vanquisher of the Elite**: 1+ Elite Rival victory
- **Conqueror of Realms**: 10+ Castle captures
- **Master Siege Commander**: 5+ Castle captures
- **Castle Breaker**: 1+ Castle capture
- **Grand Centurion**: 50+ Battle victories
- **Veteran of Twenty Battles**: 20+ Battle victories
- **Seasoned Campaigner**: 5+ Battle victories
- **Paragon of Renown**: 50,000+ Renown
- **Hero of the Realm**: 10,000+ Renown
- **Initiate of the Expedition**: Standard starting honorific

## Campaign Momentum

Campaign Momentum is a streak-style identity track reconstructed from the same persistent Hero Chronicle, so it requires no new save field and works immediately with existing saves. It measures sustained expedition success rather than claiming to be an undefeated or consecutive battle-win counter; the Chronicle stores aggregate accomplishments, not a complete chronological win/loss history.

Momentum scoring is deliberately simple and visible:

- Each recorded battle victory contributes **1 momentum**.
- Each recorded enemy castle capture contributes **3 momentum**.
- Each recorded Elite Rival victory contributes **6 additional momentum**.

Elite victories are already included in battle-victory totals; their additional six points intentionally reflect the greater achievement. All arithmetic uses the same saturation-safe 64-bit helpers as the rest of the RPG progression layer.

| Momentum | Streak label |
| ---: | --- |
| 0 | Quiet |
| 5 | Gathering |
| 15 | Hot |
| 40 | Dominant |
| 100 | Relentless |
| 250 | Legendary Run |

The Momentum line shows the current score, current label, next threshold, and remaining momentum. It is an identity/progression display only and does not add a hidden combat multiplier.

## UI & Field Ledger

- **Adventure Map Next Hero Button**: Right-click or hold the **Next Hero** button while a hero is focused. The compact hero inspection panel shows Role, Mastery, the next Mastery XP reward, Campaign Momentum, Legacy, Chronicle deeds, and earned Accolade together. Every word is fully unabbreviated, and the card is formatted compactly so it never overflows screen bounds.
- **Royal Guild Field Ledger**: Press **F** inside the Royal Guild menu to open the **RPG Field Ledger**. The ledger provides a complete expedition roster of all active heroes in the realm, their current Calling, Mastery Standing, Campaign Momentum, Accolades, and military triumphs.

Mastery milestone XP deliberately feeds the existing kingdom-level doctrine progression rather than adding hero-only combat scaling. Kingdom-wide doctrine mechanics continue to own combat modifiers, so hero identity does not silently stack duplicate bonuses onto existing RPG combat systems.