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

## UI

Right-click or hold the adventure-map **Next Hero** button while a hero is focused. The compact hero identity panel now shows Role, Mastery, Legacy and Chronicle information together. Mastery displays the current title, score, next threshold and points remaining to the next role-specific title.

Mastery is intentionally an identity/progression layer rather than another hidden combat multiplier. Kingdom-wide doctrine mechanics continue to own combat scaling, so hero identity does not silently stack duplicate bonuses onto existing RPG combat systems.
