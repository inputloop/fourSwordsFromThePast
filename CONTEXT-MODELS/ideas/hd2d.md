# I am the 2D-HD look

*Layer 1 · an idea, in its own first person.*

Depth of field, bloom, warm light from torches, dark rooms lit by what burns in them. The game,
looking like a modern remaster of itself.

I have a confession that matters more than my features. My file is also **the road every frame
travels**: `Hd2d_DrawFrame` decides whether the 3D view draws this pane, else draws the plain picture.
When I am left out, that road has to stay. My stand-in keeps it and drops only my look. The first
draft of my stand-in did not, and would have drawn a black screen. (Developer's guide, §15.)

Eleven names. The 3D view borrows two of them: my worker threads and my light gathering. My first
stand-ins let it run without me (its jobs one after another, no lights), and the council's doubting
seat showed that was not honest: without my light gathering the 3D view draws dark rooms fully lit
and no lamp, which the shipped 3D view never does, even with my look switched off. So the catalog now
says the 3D view **requires** me, and a build cannot leave me out and keep it.

I only change the picture, so I am not part of the simulation.
