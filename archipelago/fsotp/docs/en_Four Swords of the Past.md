# Four Swords of the Past

## What is this game?

Four Swords of the Past is a PC port of *The Legend of Zelda: A Link to the
Past* (built on the zelda3 C reimplementation) for up to four players in
drop-in/drop-out co-op, on one PC, over the LAN or online. You need your own
copy of the original game (the US version) to extract its graphics and sound.

## Where is the options page?

This world isn't part of Archipelago, so archipelago.gg has no options page
for it. Start from the example YAML that comes with the game
(`archipelago/Four Swords of the Past.yaml`), or use *Generate Template
Options* in the Archipelago Launcher and take `Four Swords of the Past.yaml`
from `Players/Templates`.

## What is randomized?

An Archipelago game is an open start: no intro and no escape from Hyrule
Castle, as if Zelda had just been brought to the sanctuary. Link starts with
nothing, at his house, and can go anywhere from the first step.

There are 106 locations:

- 100 chests: every chest in the game except those holding small keys, big
  keys, maps and compasses, which stay in their dungeons so dungeons play as
  before.
- Link's uncle, who waits in the secret passage by the castle and hands over
  two items instead of his sword and shield.
- The Master Sword pedestal, the smiths (hand over a sword to be tempered),
  the fairy in the Pyramid (throw in a sword) and the fairy at the Waterfall
  of Wishing (throw in a shield). You get back what you handed over or threw
  in.

Their items go into the multiworld: the four swords, three shields, two
gloves, two mails and three boomerangs (all progressive: each one you get is
the next level), the Bow, Hookshot, Hammer, Fire Rod, Ice Rod, Lamp, Cane of
Somaria, Cane of Byrna, Cape, Moon Pearl, a bottle, six pieces of heart, a
heart container, and rupees, bombs and arrows. The two extra Lamps of the
original game are 300 rupees each instead.

Items found outside chests stay where they always are: the Pegasus Boots,
the Book of Mudora, Zora's Flippers, the Magic Mirror, the shovel and flute,
the medallions, and the pendants and crystals.

## What is the goal?

Beat Ganon. Ganon's Tower opens once you have all seven crystals.

## How does the logic work?

The logic is the same as the game's own item randomizer, without glitches:

- A dungeon counts as reachable only with everything needed to finish it,
  and every dungeon boss needs a sword.
- The medallion tablets and the barrier to Agahnim's tower need the Master
  Sword level (the second sword), or the Cape for the barrier.
- The items that don't move are in logic once you can get them: the Boots
  after the Eastern Palace, then the Book, the Flippers with a glove, the
  Mirror on Death Mountain, and so on.
- The Lamp is always somewhere in your own game that is open from the start.
- The chests of the castle basement and sewers (whose keys come from the
  guards of the skipped escape) and a few houses the logic doesn't know only
  ever hold filler.

## What does an item from another world look like?

Every location in an Archipelago game hands out an "Archipelago item": a
placeholder that stands for whatever the multiworld put there. The game
tells the server, and the real item arrives from the server a moment later,
whether it is yours or the server sends it to another player's game. Your
own items always come from the server, also the ones found in your own
world, so a game that was offline catches up as soon as it connects again.

## Can I play in co-op?

Yes. The whole co-op team is one slot in the multiworld: one YAML, one slot
name. The team already shares one inventory, so every item the server sends
is for everyone, and a location any Link checks is checked for the team.

## Is there Death Link?

Yes, as the `death_link` option (the game's menu can also turn it on or off
for itself). When it is on, your deaths are sent to the others who have it
on, and theirs come to you. In co-op a death is the whole team being down
(the game over: not when a fairy in a bottle brings the last Link back), and
a death that comes in takes down every Link; as in the original game, a fairy
in a bottle brings the last one back.
