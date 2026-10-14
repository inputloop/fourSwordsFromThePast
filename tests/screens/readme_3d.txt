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
# Outdoors: Link's house on its raised yard (the ground steps down at its
# ledges), then a Kakariko house with its fence, hedges and trees.
cam3d 1 tp 0 45
load 2
drawrun 30
enter 104
drawrun 200
hold 1 D
drawrun 150
hold 1 -
drawrun 30
clearsprites
cam3d 1 tp -40 35
drawrun 2
shot tests/out/readme_3d_house.ppm
load 2
drawrun 30
enter f2
drawrun 200
hold 1 D
drawrun 40
hold 1 -
drawrun 30
cam3d 1 tp -35 35
drawrun 2
shot tests/out/readme_3d_kakariko.ppm
# First person in the Sanctuary, a few steps in from the door (in the
# doorway its sides fill the view), swinging the sword.
cam3d 1 tp 0 45
load 2
drawrun 30
enter 12
drawrun 120
hold 1 U
drawrun 40
hold 1 -
drawrun 5
cam3d 1 fp 0 4
drawrun 2
shot tests/out/readme_3d_fp_sanctuary.ppm
hold 1 B
drawrun 6
shot tests/out/readme_3d_fp_sword.ppm
hold 1 -
drawrun 30
# Two players on one computer: player 2 in first person gets a pane.
cam3d 1 tp 0 45
localjoin 2
tick 30
cam3d 2 fp 0 6
drawtick 1
tick 2
splitshot tests/out/readme_3d_split.ppm 1 2
