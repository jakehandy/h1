#!/usr/bin/env python3
"""Renders the pictures for the README and the guide at jakehandy.com/h1.

For every instrument in the build it writes docs/img/<name>.png (a still, 480 x 480) and
docs/img/<name>.webp (a three-second loop of it being played, 240 x 240), plus a settings page
still. Everything comes from the host renderer, so the pictures are the real firmware.

    python3 tools/guide_media.py                         # builds build/h1host first
    python3 tools/guide_media.py --site ../jakehandynew  # and copies everything into the site

With --site it also copies dist/h1-simulator.html to the site's public/h1/simulator.html, wrapped
in a proper document (the built file is a fragment meant for an artifact host) so it renders as
UTF-8. The guide page itself (h1.html in the site repo) is hand-written: update its text by hand.
Needs clang, ffmpeg (with libwebp) and ImageMagick's convert.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "img")
HOST = os.path.join(ROOT, "build", "h1host")
FPS = 30


def instruments():
    """The instrument names, in PRESETS order, read from the core."""
    core = os.path.join(ROOT, "core")
    table = open(os.path.join(core, "synth.c")).read()
    order = re.search(r"insts\[NINST\]\s*=\s*\{([^}]*)\}", table).group(1)
    names = []
    for sym in re.findall(r"&(\w+)", order):
        for f in os.listdir(core):
            if not f.endswith(".c"):
                continue
            m = re.search(r"const InstDef " + sym + r"\s*=\s*\{\s*\"([^\"]+)\"", open(os.path.join(core, f)).read())
            if m:
                names.append(m.group(1))
                break
    return names


def run(outdir, words):
    subprocess.run([HOST, outdir] + words, check=True, stdout=subprocess.DEVNULL)


# a little phrase: (time in ms, key, down)
PHRASE = [(0, 5, 1), (380, 5, 0), (420, 9, 1), (440, 12, 1), (1300, 9, 0), (1300, 12, 0),
          (1450, 2, 1), (1700, 2, 0), (1750, 7, 1), (2000, 7, 0), (2050, 14, 1), (2050, 17, 1),
          (2800, 14, 0), (2800, 17, 0)]


def loop_script(i, frames_dir):
    words = ["inst", str(i), "run", "1300"]
    t, step = 0, 1000 // FPS
    events = sorted(PHRASE)
    n = 0
    for f in range(3 * FPS):
        while events and events[0][0] <= t:
            _, k, d = events.pop(0)
            words += ["key", str(k), str(d)]
        # sweep knob 2 up and back so the picture shows a modulation too
        if f % 6 == 0:
            words += ["enc", "4", "3" if f < 45 else "-3"]
        words += ["run", str(step), "shot", "f%03d" % n]
        n += 1
        t += step
    return words


def main():
    for tool in ("ffmpeg", "convert"):
        if not shutil.which(tool):
            sys.exit("needs " + tool)
    subprocess.run([os.path.join(ROOT, "tools", "build_host.sh")], check=True, stdout=subprocess.DEVNULL)
    os.makedirs(OUT, exist_ok=True)
    names = instruments()
    with tempfile.TemporaryDirectory() as tmp:
        for i, name in enumerate(names):
            slug = name.lower()
            still = os.path.join(tmp, slug)
            run(tmp, ["inst", str(i), "run", "2000", "key", "5", "1", "key", "9", "1", "key", "12", "1",
                      "run", "450", "shot", slug])
            subprocess.run(["convert", still + ".ppm", "-filter", "Lanczos", "-resize", "200%",
                            os.path.join(OUT, slug + ".png")], check=True)
            fdir = os.path.join(tmp, slug + "_f")
            os.makedirs(fdir)
            run(fdir, loop_script(i, fdir))
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS),
                            "-i", os.path.join(fdir, "f%03d.ppm"), "-c:v", "libwebp_anim", "-loop", "0",
                            "-lossless", "0", "-q:v", "65", "-compression_level", "6",
                            os.path.join(OUT, slug + ".webp")], check=True)
            print("wrote", slug)
        # a settings page (ARP) and the home screen with the knob strip pinned (EDIT)
        run(tmp, ["inst", "0", "run", "1500", "btn", "8", "1", "run", "50", "btn", "8", "0",
                  "enc", "3", "2", "run", "700", "shot", "page"])
        run(tmp, ["inst", "4", "run", "1500", "btn", "4", "1", "run", "50", "btn", "4", "0",
                  "key", "7", "1", "run", "600", "shot", "hud"])
        # step mode: hold SEQ to swap to STEP, REC for step-write, a few notes and a rest, then PLAY
        steps = ["inst", "6", "run", "1500", "btn", "9", "1", "run", "1200", "btn", "9", "0", "run", "300",
                 "btn", "6", "1", "run", "50", "btn", "6", "0", "run", "1300",
                 "btn", "11", "1", "run", "50", "btn", "11", "0", "run", "200"]
        for k in (5, 9, 12, -1, 7, 9, -1, 14):
            if k < 0:
                steps += ["btn", "4", "1", "run", "40", "btn", "4", "0", "run", "120"]
            else:
                steps += ["key", str(k), "1", "run", "90", "key", str(k), "0", "run", "120"]
        steps += ["btn", "11", "1", "run", "50", "btn", "11", "0", "btn", "10", "1", "run", "50", "btn", "10", "0",
                  "run", "1700", "shot", "step"]
        run(tmp, steps)
        for s in ("page", "hud", "step"):
            subprocess.run(["convert", os.path.join(tmp, s + ".ppm"), "-filter", "Lanczos", "-resize", "200%",
                            os.path.join(OUT, s + ".png")], check=True)
    print("instruments:", ", ".join(names))
    if "--site" in sys.argv:
        site_copy(sys.argv[sys.argv.index("--site") + 1])


SIM_HEAD = ('<!DOCTYPE html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            '<link rel="canonical" href="https://jakehandy.com/public/h1/simulator.html">\n')


def site_copy(site):
    dest = os.path.join(site, "public", "h1")
    if not os.path.isdir(os.path.join(site, "public")):
        sys.exit("no public/ in " + site)
    os.makedirs(dest, exist_ok=True)
    for f in glob.glob(os.path.join(OUT, "*")):
        shutil.copy(f, dest)
    sim = open(os.path.join(ROOT, "dist", "h1-simulator.html"), encoding="utf-8").read()
    if not sim.lstrip().lower().startswith("<!doctype"):
        sim = SIM_HEAD + sim + "\n</html>\n"
    open(os.path.join(dest, "simulator.html"), "w", encoding="utf-8").write(sim)
    print("copied pictures and the simulator to", dest)


if __name__ == "__main__":
    main()
