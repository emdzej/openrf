#!/usr/bin/env python3
"""Dump view.py's 8-bit index buffers for the reference scenes to out/view_py/<scene>.raw, then
run the C renderer test (build/render_test, tests/render_test.c) which compares against them.

Usage: python3 tools/render_cmp.py [--no-run] [--fuzz N]
--fuzz N adds N random scenes (all maps, all modes, random positions; fixed seed), listed in
out/view_py/fuzz.txt which the C test also reads.
Scenes must match `scenes[]` in tests/render_test.c.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import view as V            # noqa: E402
import models as M          # noqa: E402
from car import Car         # noqa: E402

TOWN = (1808, 1456)       # cell (56,45) centre: the "No Gas Up" compound (matches out/view/RFMAP051_town_*.png)
L1 = 'cd/WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM'
L51 = 'cd/WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM'
SCENES = [
    ('RFMAP001_bunker', L1, 'bunker', None),
    ('RFMAP001_drive', L1, 'drive', None),
    ('RFMAP051_town_bunker', L51, 'bunker', TOWN),
    ('RFMAP051_town_drive', L51, 'drive', TOWN),
    ('RFMAP051_town_heli', L51, 'heli', TOWN),
    ('RFMAP051_town_lift', L51, 'lift', TOWN),
    ('RFMAP051_town_intro', L51, 'intro', TOWN),
]


def fuzz_scenes(n):
    import glob
    import random
    rnd = random.Random(1234)
    maps = sorted(os.path.relpath(p, ROOT) for p in glob.glob(os.path.join(ROOT, 'cd/WORLDS/*/LEVEL*/*.RFM')))
    out = []
    for k in range(n):
        m = rnd.choice(maps)
        mode = rnd.choice(sorted(V.MODES))
        out.append(('fuzz_%03d' % k, m, mode, (rnd.randrange(0, 4096), rnd.randrange(0, 4096))))
    return out


def main():
    scenes = list(SCENES)
    fz = []
    if '--fuzz' in sys.argv:
        fz = fuzz_scenes(int(sys.argv[sys.argv.index('--fuzz') + 1]))
    pe, models = M.load()
    car = Car(open(os.path.join(ROOT, 'cd/ART/ART.CAR'), 'rb').read())
    trans = V.load_trans(car)
    out = os.path.join(ROOT, 'out/view_py')
    os.makedirs(out, exist_ok=True)
    worlds = {}
    for name, path, mode, at in scenes + fz:
        if path not in worlds:
            worlds[path] = V.World(pe, os.path.join(ROOT, path))
        world = worlds[path]
        pitch, height, zoff = V.MODES[mode]
        v = V.View(320, 152, pitch=pitch, height=height)
        if at:
            tx, ty = at[0] << 16, at[1] << 16
        else:
            cx, cy = world.pads[0][0]
            tx, ty = (cx * 32 + 16) << 16, (cy * 32 + 16) << 16
        v.look_at(tx, ty, 0, zoff)
        ren = V.Renderer(pe, models, car, trans, world)
        ren.render(v)
        open(os.path.join(out, name + '.raw'), 'wb').write(bytes(ren.r.fb))
        if not name.startswith('fuzz'):
            print('%-24s cels=%d models=%d' % (name, ren.r.cels, len(ren.queue)))
    with open(os.path.join(out, 'fuzz.txt'), 'w') as f:
        for name, path, mode, at in fz:
            f.write('%s %s %s %d %d\n' % (name, path[3:], mode, at[0], at[1]))
    if '--no-run' not in sys.argv:
        exe = os.path.join(ROOT, 'build/render_test')
        sys.exit(subprocess.call([exe], cwd=ROOT))


if __name__ == '__main__':
    main()
