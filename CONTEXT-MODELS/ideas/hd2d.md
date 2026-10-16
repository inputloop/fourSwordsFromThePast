# I am the 2D-HD look

*Layer 1 · an idea, in its own first person.*

Depth of field, bloom, warm light from torches, dark rooms lit by what burns in them. The game,
looking like a modern remaster of itself.

I have a confession that matters more than my features. My file is also **the road every frame
travels**: `Hd2d_DrawFrame` decides whether the 3D view draws this pane, else draws the plain picture.
When I am left out, that road has to stay. My stand-in keeps it and drops only my look. The first
draft of my stand-in did not, and would have drawn a black screen. (Developer's guide, §15.)

Eleven names. The 3D view borrows two of them: my worker threads and my light gathering. Without me
it runs its jobs one after another and gets no lights. Slower, plainer, correct.

I only change the picture, so I am not part of the simulation.
