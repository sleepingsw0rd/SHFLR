# SHFLR 0.2.3 factory additions

The factory bank now contains 63 presets. The first 33 names, indices and
arrangements are unchanged; the 30 additions below occupy indices 33–62.
Indices in this document are zero-based, as used by the standalone renderer.

These patterns assume a break starts on its downbeat, with a kick near beat one
and a snare near beat two. They do not detect drum types. If the break has a
different layout, move the coloured boundaries to the desired hit. A sixteenth
is one source cell, an eighth is two cells, and one beat is four cells.

The roll presets retrigger the beginning of a hit, rather than repeating a
whole beat. Most leave the first source bar intact, then play the variation so
the change works as an answer or fill. The accelerating versions play the first
eighth of the hit followed by two sixteenth retriggers. No swing, pitch,
reverse, or audio stretching is added.

## Six-beat phrases

Six beats means source cells 0–23: one and a half bars from the beginning of a
two-bar break. The phrase repeats continuously until the selected output ends;
the final repeat is cut at that boundary. These are six-beat cycles over 4/4,
not triplet patterns.

| Index | Factory name | Input / output bars | Pattern |
|---:|---|---|---|
| 33 | Two bars - First six beats | 2 / 4 | Repeat the first six beats; cut the final repeat after four beats. |
| 34 | Two bars - First six beats long | 2 / 8 | Longer six-beat rotation; cut the final repeat after two beats. |
| 35 | Two bars - Six beat turn | 2 / 4 | Six beats straight, then exchange their middle and final two-beat phrases. |
| 36 | Two bars - Six beat kick return | 2 / 4 | Alternate six beats straight with a six-beat phrase opening on four kick sixteenths. |
| 37 | Two bars - Six beat snare return | 2 / 4 | Alternate six beats straight with four beat-two snare sixteenths. |
| 38 | Two bars - Six beat stop | 2 / 4 | First six beats, then two beats of silence; repeat. |

## Kick, snare and combined rolls

| Index | Factory name | Input / output bars | Pattern |
|---:|---|---|---|
| 39 | One bar - Kick eighth roll | 1 / 2 | Straight bar, then two kick eighths on beat one. |
| 40 | One bar - Kick sixteenth roll | 1 / 2 | Straight bar, then four kick sixteenths on beat one. |
| 41 | One bar - Kick double roll | 1 / 2 | Straight bar, then kick sixteenths on beats one and three. |
| 42 | One bar - Kick accelerating | 1 / 2 | Straight bar, then an eighth-to-sixteenth kick pickup. |
| 43 | Two bars - Kick eighth answer | 2 / 2 | First bar straight; two opening kick eighths answer with the rest of source bar two. |
| 44 | Two bars - Kick sixteenth chase | 2 / 2 | First bar straight; alternating kick rolls and the second bar's beat-two phrase. |
| 45 | One bar - Snare eighth roll | 1 / 2 | Straight bar, then two snare eighths on beat two. |
| 46 | One bar - Snare sixteenth roll | 1 / 2 | Straight bar, then four snare sixteenths on beat two. |
| 47 | One bar - Snare double roll | 1 / 2 | Straight bar, then beat-two snare rolls on beats two and four. |
| 48 | One bar - Snare accelerating | 1 / 2 | Straight bar, then an eighth-to-sixteenth snare roll. |
| 49 | Two bars - Snare eighth answer | 2 / 2 | First bar straight; source bar two keeps its kick and tail around two beat-two snare eighths. |
| 50 | Two bars - Snare sixteenth chase | 2 / 2 | First bar straight; source bar two answers with snare sixteenths on beats two and four. |
| 51 | One bar - Jungle kick snare rolls | 1 / 2 | Straight bar, then kick sixteenths on beat one and snare sixteenths on beats two and four. |
| 52 | One bar - Hip hop double tap | 1 / 2 | Kick eighths in the first bar, snare eighths in the second. |
| 53 | Two bars - DnB roll exchange | 2 / 2 | First bar straight; kick and snare sixteenths lead into source bar two's final half. |
| 54 | Two bars - Jungle roll crescendo | 2 / 2 | First bar straight; kick/snare eighth-to-sixteenth bursts build into full sixteenth rolls. |

## Gaps and dropouts

Rests occupy time in the output order without consuming a source colour. The
five supported durations are 1/16, 1/8, one beat, two beats and one bar. These
factories use all five sizes. They keep real source phrases around the gaps so
the dropouts work as rhythmic variations rather than silent-only presets.

| Index | Factory name | Input / output bars | Pattern |
|---:|---|---|---|
| 55 | One bar - Hip hop kick gaps | 1 / 1 | Two sixteenth kick hits separated by sixteenth gaps, then the rest of the bar. |
| 56 | One bar - Hip hop snare gaps | 1 / 1 | Two sixteenth snare hits with gaps on beat two; retain the kick and tail. |
| 57 | One bar - Jungle stop start | 1 / 2 | Straight bar, then gated beat-two snare hits and a silent final beat. |
| 58 | One bar - DnB gated eighths | 1 / 2 | Eighth-note kick/snare gaps, followed by a second bar with a gated closing snare. |
| 59 | Two bars - Jungle drop and roll | 2 / 2 | First bar straight; silent beats alternate with four beat-two snare sixteenths. |
| 60 | Two bars - DnB half bar dropout | 2 / 2 | Play the first six source beats, then leave the final half-bar silent. |
| 61 | Two bars - Hip hop cutaway | 2 / 2 | Preserve both source bars except the second bar's beat-two phrase, replaced by a beat of silence. |
| 62 | Two bars - Jungle bar silence | 2 / 4 | First source bar, silent bar, second source bar, first source bar. |

## Randomisation with rests

- **Random slices** changes only the source partition. Every audible colour
  referenced by the order survives; all rest tokens and lengths stay unchanged.
- **Shuffle order** keeps each existing phrase/rest occurrence and its length.
- **New sequence** chooses from assigned source colours and only the rest
  lengths already present in the order. It never adds silence to a pattern
  without rests. If source colours exist it starts with an audible phrase;
  deliberately all-rest patterns can still generate from their existing rests.

## Verification

Library tests use literal source-cell maps to verify six-beat repetition,
kick/snare eighth and sixteenth onsets, acceleration, gated hits, beat dropouts
and a full-bar rest. Structural checks cover all 63 factories: valid contiguous
colours, grid-aligned full source partitions, no hidden order entries, complete
output coverage, audible content and distinct source mappings. Seeded checks
exercise source/sequence randomisation with every supported rest length and
with all-rest orders. The original 33 factory positions remain stable.

The local core suite passed after implementation. Audio render and packaged
plugin verification are recorded separately by the main build workflow; no
Cubase or audible playback was used for bank development.
