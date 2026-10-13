# The README's 3D view screenshots (tools/readme_shots.sh runs this and
# makes docs/screenshots/3d-*.png). Not a test: tests/run_coop_tests.sh only
# runs tests/coop. Pick other spots with pos, or hold and run, before a shot.
widescreen 71
hd2d 1
load 2
run 60
# The original picture, then the same moment in 3D at the classic angle.
view3d 0
shot tests/out/readme_2d.ppm
view3d 1
# (A few frames drawn in 3D: the sprites' tags and the switch from 2D.)
drawrun 15
cam3d 1 tp 0 45
shot tests/out/readme_3d_tp.ppm
# Down the steps into the courtyard: the camera turned, then first person
# (the soldier stands in the world as a flat card that faces you).
hold 1 D
drawrun 90
hold 1 -
drawrun 5
cam3d 1 tp -35 40
drawrun 2
shot tests/out/readme_3d_turned.ppm
cam3d 1 fp 150 8
drawrun 2
shot tests/out/readme_3d_fp_out.ppm
# First person in the Sanctuary.
enter 12
drawrun 120
cam3d 1 fp 0 4
drawrun 2
shot tests/out/readme_3d_fp_sanctuary.ppm
# Two players on one computer: player 2 in first person gets a pane.
cam3d 1 tp 0 45
localjoin 2
tick 30
cam3d 2 fp 0 6
drawtick 1
tick 2
splitshot tests/out/readme_3d_split.ppm 1 2
