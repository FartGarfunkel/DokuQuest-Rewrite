#!/usr/bin/env python3
"""Minecraft 1.20's terrain (its noise router: density functions, noises, the Overworld's noise settings) from the client
jar into data/worldgen_base/ (git-ignored: Mojang's data, read by gen/density.cpp for terrain version 14):
  python3 tools/port/worldgen_base.py [client.jar] [data-dir]"""
import pathlib, sys, zipfile

JAR = pathlib.Path.home() / '.local/share/PrismLauncher/libraries/com/mojang/minecraft/1.20.1/minecraft-1.20.1-client.jar'
KEEP = ('data/minecraft/worldgen/density_function/', 'data/minecraft/worldgen/noise/', 'data/minecraft/worldgen/noise_settings/overworld.json')


def main(jar=JAR, d='data'):
    out = pathlib.Path(d) / 'worldgen_base'
    out.mkdir(parents=True, exist_ok=True)
    n = 0
    with zipfile.ZipFile(jar) as z:
        for name in z.namelist():
            if not name.endswith('.json') or not name.startswith(KEEP): continue
            rel = name[len('data/minecraft/worldgen/'):]
            f = out / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(z.read(name)); n += 1
    (out / '.gitignore').write_text('*\n!.gitignore\n')
    print('worldgen_base: %d files into %s' % (n, out))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else JAR, sys.argv[2] if len(sys.argv) > 2 else 'data')
