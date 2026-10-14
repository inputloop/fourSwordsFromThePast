#!/bin/sh
# Makes the README's 3D view screenshots from your own ROM: runs
# tests/screens/readme_3d.txt headless, then scales the pictures 2x (nearest
# neighbor, so the pixels stay sharp) into docs/screenshots/:
#   3d-compare.png       the original picture and the 3D view side by side
#   3d-turned.png        the camera turned
#   3d-outdoors.png      the camera turned at Link's house and in Kakariko
#   3d-first-person.png  first person in the Sanctuary (swinging the sword) and outdoors
#   3d-split.png         split screen, player 2 in first person
# Needs the game built (make), zelda3_assets.dat and Python 3 with Pillow.
# Only these PNGs are committed (the screenshots are from the game's own
# graphics, as the other README pictures).
set -e
cd "$(dirname "$0")/.."
mkdir -p tests/out docs/screenshots
./zelda3 --mptest tests/screens/readme_3d.txt
python3 -I - <<'EOF'
from PIL import Image

def load(name):
    return Image.open('tests/out/%s.ppm' % name).convert('RGB')

# Side by side with a 4 pixel black gap (as hd2d-compare.png).
def pair(a, b):
    out = Image.new('RGB', (a.width + 4 + b.width, max(a.height, b.height)))
    out.paste(a, (0, 0))
    out.paste(b, (a.width + 4, 0))
    return out

def save(im, name):
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save('docs/screenshots/' + name)
    print('wrote docs/screenshots/' + name)

save(pair(load('readme_2d'), load('readme_3d_tp')), '3d-compare.png')
save(load('readme_3d_turned'), '3d-turned.png')
save(pair(load('readme_3d_house'), load('readme_3d_kakariko')), '3d-outdoors.png')
save(pair(load('readme_3d_fp_sword'), load('readme_3d_fp_out')), '3d-first-person.png')
save(load('readme_3d_split'), '3d-split.png')
EOF
