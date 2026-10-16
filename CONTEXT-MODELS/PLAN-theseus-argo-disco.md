# PLAN: keeping the words steady from design to build (theseus, argo, disco)

*A plan, not a build. Written on the recipe branch; the first instrument it names exists and has
run twice (below). Everything after that is a proposal for the keepers to accept, change or drop.*

## The question

Bryant's garden has **theseus**, a continuity check: when the model under an agent changes, or a skill
does, does the ship that arrives still steer the same? It measures and never upgrades.

This project asks the same question of **words**. A design says *stand-in*, *composition*, *sim mod*.
Does what gets built still mean those things, under those names? Or does the code drift to *stub*,
*config*, *flag*, so the next reader of the design cannot find it in the build, and the next reader of
the build cannot find why in the design?

## The three parts

| part | whose | what it is | state |
|---|---|---|---|
| theseus | Bryant's garden | continuity probes for an agent's identity across model and skill changes | exists (Bryant's, v1.2) |
| argo | the Tavern | an agent kind on the lab's shared network; one each for Corwin and Bryant | exists |
| disco | this project, new | an app folder beside argo: probes for a **taxonomy's** continuity across the design-to-build turn, and, later, proposals to heal what drifted | first instrument built |

*disco* is Corwin's name for it (2026-10-16). What it stands for, if anything, is his to say.

## What exists now: the turn probe

`disco/turnprobe.py` (in the lab's project folder; it reads this repository, it does not live in it)
takes `TAXONOMY.tsv` and sorts every term by where it appears:

- **CARRIED**: in the design and in the build.
- **DESIGN-ONLY**: the design says it; the build never does. Either the build calls it something else
  (a heal candidate) or the idea was not built.
- **BUILD-ONLY**: the build says it; the design never explains it.
- **ABSENT**: neither.

**Run 0** (before RECIPE.org was written): 19 carried, 8 design-only, 4 build-only, 9 absent. It found
two isotopes no one had listed, both by measurement: the game's **Power Glove** sits under Alfred's
**glove**, and the game's memory **flags** under the developer's guide's flags. Both are now rows of
`TAXONOMY.tsv`. It also found its own fault: it treated `_` as part of a word, so `mods_census` did not
count as *census*. **Run 1**, with that fixed, and the two new rows: 21 carried, 6 design-only, 5
build-only, 10 absent.

## What comes next, in order, each with what would stop it

1. **Run the probe at every turn.** Before a design is built, and after. The difference between the
   two runs is the turn's drift, as data. *Stops if:* two runs on the same tree differ (the probe
   would be measuring itself).
2. **Weigh the hits.** A word in a heading or a catalog row weighs more than one in passing. Borrow
   C4's pressure for this (a rock becomes a boulder at four citations, a mountain at nine) rather than
   inventing a scale. *Stops if:* the weights change a verdict a person reading the files disagrees
   with, on more than one term in nine (a picked bound, said so).
3. **Propose heals, never apply them.** For each DESIGN-ONLY term, the build's nearest term (by where
   the design term's sentences point: the file named beside it). Write the proposal as a row a
   keeper accepts or refuses. *Stops if:* a proposal would rename something in the game's own code
   (`fixed` status): those words are not ours.
4. **Self-healing, the slow way.** A heal accepted twice for the same pair of terms becomes a bridge
   in `TAXONOMY.tsv` (`~` or `=`), so the probe counts both words as one term from then on. That is
   all "self-healing" should mean here: a remembered decision, never a silent rename.
5. **Give it to theseus.** Theseus's prose lane already scores expected terms in an agent's answers.
   A taxonomy is a list of expected terms. A bespoke theseus probe could ask an agent, cold, to
   explain this project's build, and score whether it uses the project's words or its own. That is
   the persona delta, measured on vocabulary. *Stops if:* Bryant does not want his instrument
   extended; then disco runs it beside theseus, not inside it.
6. **An argo for disco.** A Tavern agent that runs the probe when a branch is pushed and posts the
   turn's drift to the room. Only after 1 to 4 have run on real turns more than once.

## How we will know it was worth it

If, after the next three design-to-build turns in this project, the DESIGN-ONLY and BUILD-ONLY counts
fall and stay down while CARRIED rises, the words are holding. If they do not move, the probe is a
thermometer nobody reads, and it should be retired, with its runs kept.
