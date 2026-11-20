# SHFLR 0.2.2 factory additions

Twenty new arrangements extend the existing bank from 13 to 33 presets. The
original names, order and arrangements remain unchanged. These are grid-based
starting points for breaks aligned to the beginning of a bar; there is no drum
recognition. A snare/ghost/kick name describes the intended rhythm, so the result
depends on where the chosen source break puts its hits.

All durations below assume 4/4. Each beat contains four source cells. Each colour
still owns exactly one contiguous source region, with at most eight colours.
Every displayed order box is reached during the selected output loop. The
three-beat cycles repeat automatically and the final phrase is cut at the loop
boundary when necessary.

| Preset | Input / output bars | Arrangement |
| --- | --- | --- |
| One bar - First three beats | 1 / 2 | Repeat source beats 1–3 continuously; the last repetition ends after beat 2. |
| One bar - Three beat turnaround | 1 / 2 | Repeat the six-beat sequence 1, 2, 3, 1, 3, 2, cutting at the eight-beat output boundary. |
| One bar - Three beat tail | 1 / 2 | Play 1, 2, 3 twice, then source beat 4 twice. |
| Two bars - First three beats | 2 / 4 | Repeat only source beats 1–3 over sixteen output beats; the final repetition ends after beat 1. |
| Two bars - Three beat pendulum | 2 / 4 | Alternate the first three beats of source bar 1 and source bar 2. |
| Two bars - Three beat answer | 2 / 2 | Repeat source beats 1–3 twice, then source beat 8 and beat 4. |
| One bar - Jungle backbeat | 1 / 2 | First pass straight; second pass repeats the first half of beat 2 and reverses the halves of beat 4. |
| One bar - Jungle ghost steps | 1 / 2 | First pass straight; second pass exchanges the eighth-note halves of beats 3 and 4. |
| One bar - Jungle snare rush | 1 / 2 | First pass straight; second pass repeats the first and third sixteenths of the last beat. |
| One bar - Jungle tail switch | 1 / 2 | First pass straight; second pass brings the last beat forward, swaps its eighth-note halves and ends on beat 3. |
| One bar - DnB kick return | 1 / 2 | First pass straight; second pass doubles the first eighth, preserves beat 2, then answers with two repeats of the second eighth before beat 4. |
| One bar - DnB snare answer | 1 / 2 | First pass straight; second pass exchanges beats 2 and 4, with their eighth-note slices still independently available. |
| One bar - DnB two step | 1 / 2 | Keep beats 1, 2 and 4; double the first half of beat 3 on the first pass, then reverse its halves on the second. |
| Two bars - Jungle backbeat relay | 2 / 2 | Keep both bar downbeats and beat 3; borrow source beat 6 for the first bar's last beat, and source beat 2 for the second bar's second beat. |
| Two bars - Jungle ghost relay | 2 / 2 | Preserve the two-bar phrase except for reversed eighth-note halves of source beat 4. |
| Two bars - Jungle break exchange | 2 / 2 | Exchange source beats 2 and 6 while retaining the surrounding phrases. |
| Two bars - Jungle last beat rush | 2 / 2 | Keep the first seven beats; repeat the first and third sixteenths of the final beat. |
| Two bars - DnB kick conversation | 2 / 2 | Keep the two-bar phrase, replacing the second eighth of bar 2 with the first eighth of bar 1. |
| Two bars - DnB half beat chase | 2 / 2 | Keep beats 1–3 and 5–6; fill output beats 4 and 8 with repeated first halves of source beat 7. |
| Two bars - DnB turnaround | 2 / 2 | Play source beats 1–5, then 8, 7, 6 as a three-beat turnaround. |

The library tests check literal source-cell playback for the first-three-beat
patterns and representative jungle chops, as well as valid contiguous regions,
grid alignment, audible order entries and distinct output mappings. Listening
and host testing remain with the user.
