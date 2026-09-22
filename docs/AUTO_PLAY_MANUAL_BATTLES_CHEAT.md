# Auto-play with manual battles cheat

This fork exposes the adventure-map AI auto-control feature in normal builds and keeps tactical battles under manual control.

## Command

While an adventure map is active, press **F8**.

- Confirm **Enable** to hand your adventure-map turns to the AI.
- Enabling the cheat ends the current human turn so the AI can take over immediately.
- Whenever your auto-play player enters a battle, the battle screen opens and your troops are controlled manually.
- Neutral or normal AI opponents remain AI-controlled in battle. If Auto Play-Test puts both map players under auto-control, both player sides are manual in their battle.
- Press **F8** again to queue a return to normal adventure-map control. The return is committed when the AI finishes the current turn.
- The command is ignored while a battle is already open.

The default hotkey is configurable through the normal fheroes2 hotkey file. Its entry is named **auto-play with manual battles**.

Auto Play-Test uses the same manual-battle path automatically, so its adventure map stays automated but its battles stop for player input.

## Offline progress and persistent resources

This fork also keeps one exact persistent human-player resource wallet across new maps and loaded games. Auto Play-Test is excluded so test runs cannot overwrite the real wallet.

Offline state is stored in the fheroes2 config directory as:

`offline_progress.dat`

The file stores the last-seen Unix timestamp, exact resource counts, the last known daily income, and per-resource fractional carry. Offline time is not capped. Rewards use the saved daily income prorated by real elapsed time, with fractional carry retained so repeated short offline sessions do not lose progress.

When a map starts or a save is loaded, the saved wallet replaces that map/save's resource counts, accrued rewards are applied, and a popup reports the exact time away in days, hours, minutes, and seconds plus the rewards.
