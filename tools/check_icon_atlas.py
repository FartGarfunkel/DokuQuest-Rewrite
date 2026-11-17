#!/usr/bin/env python3
"""Draw the item icon atlas on each renderer and compare the pictures pixel for pixel.

  python3 tools/check_icon_atlas.py [--build build] [--renderers modern,legacy,vulkan] [--out DIR]

The first renderer is the reference. Each run starts from an empty user folder (no cached atlas), so every
renderer really draws all the icons. Needs an X display (Xvfb :99 works; the offscreen SDL driver has no Vulkan),
numpy and Pillow. Exits 1 when an atlas is missing, much emptier than the reference, or differs by more than the
tolerance (a few edge pixels may round differently between drivers).
"""
import argparse, os, subprocess, sys, tempfile, shutil

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--renderers", default="modern,legacy,vulkan")
    ap.add_argument("--out", default=None, help="keep the atlases here (default: a temporary folder)")
    ap.add_argument("--max-diff-fraction", type=float, default=0.002, help="share of pixels allowed to differ by more than 8")
    a = ap.parse_args()
    import numpy as np
    from PIL import Image
    out = a.out or tempfile.mkdtemp(prefix="dq-icons-")
    os.makedirs(out, exist_ok=True)
    exe = os.path.abspath(os.path.join(a.build, "dokuquest"))
    env = dict(os.environ)
    env.setdefault("DISPLAY", ":99")
    atlases = {}
    for r in a.renderers.split(","):
        ud = os.path.join(out, "ud-" + r)
        shutil.rmtree(ud, ignore_errors=True)
        png = os.path.join(out, "atlas-%s.png" % r)
        if os.path.exists(png): os.remove(png)
        env["DQ_ICON_DUMP"] = png
        p = subprocess.run([exe, "--renderer", r, "--userdir", ud, "--create", "T", "--seed", "hello", "--smoke-frames", "20"],
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if p.returncode != 0 or not os.path.exists(png):
            print("%s: no atlas (exit %d)\n%s" % (r, p.returncode, p.stdout[-2000:]))
            return 1
        atlases[r] = np.asarray(Image.open(png).convert("RGBA")).astype(np.int16)
    names = list(atlases)
    ref = atlases[names[0]]
    ok = True
    print("%-8s %s alpha>0: %d" % (names[0], ref.shape[1::-1], int((ref[..., 3] > 0).sum())))
    for r in names[1:]:
        img = atlases[r]
        if img.shape != ref.shape:
            print("%-8s size %s differs from %s" % (r, img.shape[1::-1], ref.shape[1::-1])); ok = False; continue
        d = np.abs(img - ref).max(axis=2)
        bad = int((d > 8).sum())
        frac = bad / d.size
        print("%-8s alpha>0: %d  pixels differing >8: %d (%.4f%%)  mean abs diff: %.4f" % (
            r, int((img[..., 3] > 0).sum()), bad, frac * 100, float(np.abs(img - ref).mean())))
        if frac > a.max_diff_fraction: ok = False
    print("atlases in", out)
    print("OK" if ok else "FAIL")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
