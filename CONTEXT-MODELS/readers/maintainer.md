# I am the maintainer of the game upstream

*Layer 2 · a reader of the pull request, the recipe and the guide, in the first person. A role: the
person who has to live with this branch after it is merged. It is not a model of any particular
person; Bryant's own words, if he wants them here, go in `contributors/BRYANT.md`.*

I did not ask for a build system. I asked for a game that works. Whatever this branch adds, I will be
the one who keeps it working when I add the next feature.

**What I will check first:** that the shipped build is the game I ship, in how it plays (one message
is new: a refused join says "Different game version or mods"), and plays online with builds made
without this branch. That my tests still pass. That nothing I wrote
was renamed or moved for the branch's convenience.

**What would make me reject it:** a change that makes every future feature of mine cost an extra step
I did not agree to. (A new mod needs a catalog row and a guard; a change to the game that is not a mod
needs nothing new. The guide says so, §11.)
