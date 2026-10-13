#!/usr/bin/env python3
"""Packs archipelago/fsotp into fsotp.apworld, which Archipelago loads from
its custom_worlds folder.

    python3 archipelago/build_apworld.py [OUT]   (default: archipelago/fsotp.apworld)

Does what Archipelago's own "Build APWorlds" launcher component does, without
needing an Archipelago install: a zip with everything under one fsotp/
folder, and the manifest with the container version fields added (without
"compatible_version", Archipelago 0.6.x logs the apworld as invalid and 0.7
refuses it). The files go in sorted with a fixed date, so the same sources
always make the same file.
"""
import fnmatch
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'fsotp')
NAME = 'fsotp'
# APWorldContainer.get_manifest in worlds/Files.py (Archipelago 0.6.4 and later).
CONTAINER_VERSION = 7
# Never packed: caches, editor droppings, and anything that could be game data.
IGNORE = ['__pycache__', '*.pyc', '*.pyo', '.DS_Store', '__MACOSX', '.git*', '.apignore',
          '*.sfc', '*.smc', '*.dat', '*.ppm']


def ignored(name):
    return any(fnmatch.fnmatch(name, pattern) for pattern in IGNORE)


def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, NAME + '.apworld'))
    if not out.endswith(os.sep + NAME + '.apworld'):
        # Archipelago imports the world by the file's name.
        raise SystemExit(f'the file must be called {NAME}.apworld')
    with open(os.path.join(SRC, 'archipelago.json'), encoding='utf-8') as f:
        manifest = json.load(f)
    manifest.update(version=CONTAINER_VERSION, compatible_version=CONTAINER_VERSION)

    files = []
    for root, dirs, names in os.walk(SRC):
        dirs[:] = sorted(d for d in dirs if not ignored(d))
        for name in names:
            path = os.path.join(root, name)
            rel = os.path.relpath(path, SRC).replace(os.sep, '/')
            if not ignored(name) and rel != 'archipelago.json':
                files.append((rel, path))
    files.sort()

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        def add(rel, data):
            info = zipfile.ZipInfo(f'{NAME}/{rel}', date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, data)
        for rel, path in files:
            with open(path, 'rb') as f:
                add(rel, f.read())
        add('archipelago.json', json.dumps(manifest, indent=2) + '\n')
    print(f'{out}: {len(files) + 1} files, world {manifest["world_version"]}')


if __name__ == '__main__':
    main()
