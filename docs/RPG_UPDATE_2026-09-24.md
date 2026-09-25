# RPG persistence and Windows build update — 24 September 2026

This update concentrates on the existing RPG progression loop. Offline snapshots supply
Renown and virtual reward calculations; a damaged current snapshot must not silently
change those calculations. The loader now rejects invalid version-13 snapshots and tries
the remaining valid temporary, primary, and backup candidates. Versions 1–12 keep their
historical normalization rules. The save format and progression balance are unchanged.

## Detailed changes

1. Reject repeated fields in a version-13 offline snapshot instead of letting the last value silently win.
2. Reject unknown fields in a version-13 snapshot so trailing or misspelled records cannot hide corruption.
3. Reject retired creature-roster records in version 13; those fields remain readable in the older formats that used them.
4. Reject negative saved resource amounts in current snapshots before they are normalized to zero.
5. Reject saved resource amounts above the engine's 32-bit resource limit before they are clamped.
6. Reject negative daily-income entries in current snapshots before they can affect offline production.
7. Reject daily-income entries above the 32-bit limit before they can distort production.
8. Reject negative fractional carry for any resource instead of silently clamping it.
9. Reject fractional carry of a full day or more; carry is a remainder in resource-seconds.
10. Reject an inactive homecoming streak that still has an award timestamp.
11. Reject an inactive homecoming streak that still has an award calendar day.
12. Reject an active streak with no positive award timestamp.
13. Reject an active streak whose award timestamp is later than the snapshot timestamp.
14. Reject an active streak with no positive saved local calendar day.
15. Reject negative pending-resume time instead of silently discarding that invalid interval.
16. Reject a not-yet-initialized contract that already contains progress.
17. Reject a not-yet-initialized contract that already contains a target.
18. Reject an active contract with an ID outside the five supported contract types.
19. Reject an active contract with a zero or impossibly large target.
20. Reject more than four stored treasure fragments, since five fragments complete a map.
21. Reject an out-of-range castle count in a current snapshot.
22. Reject an out-of-range town count in a current snapshot.
23. Reject an out-of-range hero count in a current snapshot.
24. Reject an out-of-range mine count in a current snapshot.
25. Reject an out-of-range artifact count in a current snapshot.
26. Reject state-efficiency values outside the designed 100–150% range.
27. Reject supply-rush carry outside its 0–99 range.
28. Preserve the legacy carry, efficiency, meter, and fragment normalization after current-format validation.
29. Reject a snapshot if the input stream ends because of a read error rather than clean end-of-file.
30. Calculate contract hours with division and a remainder, avoiding overflow near the largest supported offline interval.
31. Widen the treasure-map counter before adding one, avoiding 32-bit wrap at its maximum value.
32. Saturate the treasure-map event time offset before adding it to a long offline interval.
33. Compare consecutive local streak days by subtraction, avoiding an overflowing day-index increment.
34. Saturate offline RPG XP before converting it to a 64-bit integer.
35. Leave an unchanged current RPG profile untouched on map entry, preserving the older backup for recovery.
36. Continue saving new, migrated, and recovered RPG profiles immediately on map entry.
37. Keep a selected valid RPG profile backup during recovery even when the primary is valid but older.
38. Compile Morale-aware RPG hero-skill evaluation with MSVC by using an explicit integer clamp type.
39. Compile Luck-aware RPG hero-skill evaluation with the same explicit type.

The current `offline_progress.dat` layout and `rpg_profile.dat` layout are unchanged.
Existing valid saves continue to load; a damaged current snapshot falls back to another
valid candidate when one exists.
