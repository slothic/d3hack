#!/usr/bin/env python3
"""d3hack-custom: assemble a release zip from the CURRENT build and sources.

Replaces build_release_zip.sh, which is stale: it expects src/deploy/{exefs,romfs}
(neither exists) and generates a 1.4 KB README unrelated to the hand-written one the
last several releases shipped.

    python src/scripts/pack_release.py v3.5 --readme README-v3.5.txt

The README is NOT generated. Releases carry a hand-edited one; by default it is lifted
from the previous release zip so you can diff and edit it, but pass --readme to supply
the finished file.

What goes in, and why:

  exefs/subsdk9, exefs/main.npdm
      src/build/work.nso and work.npdm -- whatever `cmake --build` last produced.
      ALWAYS verify this matches the binary you actually tested:
          md5sum src/build/work.nso
          md5sum "$APPDATA/Ryujinx/mods/contents/01001B300B9BE000/d3hack/exefs/subsdk9"
      A release built from a tree that was edited after the test run is the classic way
      to ship something nobody has ever run.

  romfs/d3gui/
      src/data/imgui.bin + src/data/strings_*.toml. Note deploy.sh copies exefs ONLY, so
      the locally deployed romfs is usually older than src/data. The zip ships src/data.

  sdcard/config/d3hack-nx/config.toml
      The LIVE emulator config, not src/examples/.../config.toml. The example file is the
      commented one; the mod rewrites config.toml on every boot into a sorted,
      comment-free form, so shipping the commented file just means the comments vanish on
      first launch. Check it for anything personal or any probe left switched on before
      shipping -- this script prints every `= true` it finds.
"""
import argparse
import glob
import hashlib
import io
import os
import shutil
import sys
import tempfile
import zipfile

TITLE_ID = "01001B300B9BE000"
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(REPO, "src")

APPDATA = os.environ.get("APPDATA", "")
LIVE_CONFIG = os.path.join(APPDATA, "Ryujinx", "sdcard", "config", "d3hack-nx", "config.toml")
DEPLOYED_SUBSDK = os.path.join(APPDATA, "Ryujinx", "mods", "contents",
                               TITLE_ID.lower(), "d3hack", "exefs", "subsdk9")


def md5(path):
    with open(path, "rb") as fh:
        return hashlib.md5(fh.read()).hexdigest()


def newest_zip():
    zips = sorted(glob.glob(os.path.join(REPO, "d3hack-v*-mod.zip")))
    return zips[-1] if zips else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version", help='release version, e.g. "v3.5"')
    ap.add_argument("--readme", help="finished README.txt (default: carried from the previous zip)")
    ap.add_argument("--config", default=LIVE_CONFIG, help="config.toml to ship")
    ap.add_argument("--out", help="output zip (default: <repo>/d3hack-<version>-mod.zip)")
    args = ap.parse_args()

    out = args.out or os.path.join(REPO, "d3hack-%s-mod.zip" % args.version)

    nso = os.path.join(SRC, "build", "work.nso")
    npdm = os.path.join(SRC, "build", "work.npdm")
    for required in (nso, npdm, args.config):
        if not os.path.isfile(required):
            sys.exit("missing: %s" % required)

    # The check that matters: is this the binary that was tested?
    print("subsdk9 (src/build/work.nso) md5 %s" % md5(nso))
    if os.path.isfile(DEPLOYED_SUBSDK):
        same = md5(DEPLOYED_SUBSDK) == md5(nso)
        print("deployed/tested subsdk9  md5 %s  %s"
              % (md5(DEPLOYED_SUBSDK), "MATCH" if same else "*** MISMATCH -- rebuild or redeploy ***"))
    else:
        print("deployed subsdk9 not found; cannot confirm this is what was tested")

    stage = tempfile.mkdtemp(prefix="d3hack-release-")
    try:
        exefs = os.path.join(stage, TITLE_ID, "d3hack", "exefs")
        romfs = os.path.join(stage, TITLE_ID, "d3hack", "romfs", "d3gui")
        cfgdir = os.path.join(stage, "sdcard", "config", "d3hack-nx")
        riftdir = os.path.join(cfgdir, "rift_data")
        for d in (exefs, romfs, riftdir):
            os.makedirs(d)

        shutil.copy2(nso, os.path.join(exefs, "subsdk9"))
        shutil.copy2(npdm, os.path.join(exefs, "main.npdm"))

        shutil.copy2(os.path.join(SRC, "data", "imgui.bin"), os.path.join(romfs, "imgui.bin"))
        for f in glob.glob(os.path.join(SRC, "data", "strings_*.toml")):
            shutil.copy2(f, os.path.join(romfs, os.path.basename(f)))

        shutil.copy2(args.config, os.path.join(cfgdir, "config.toml"))
        for f in glob.glob(os.path.join(SRC, "examples", "config", "d3hack-nx", "rift_data", "*")):
            shutil.copy2(f, os.path.join(riftdir, os.path.basename(f)))

        if args.readme:
            shutil.copy2(args.readme, os.path.join(stage, "README.txt"))
        else:
            prev = newest_zip()
            if not prev:
                sys.exit("no previous zip to carry a README from; pass --readme")
            print("README carried from %s -- edit it for this release"
                  % os.path.basename(prev))
            with zipfile.ZipFile(prev) as pz:
                io.open(os.path.join(stage, "README.txt"), "wb").write(pz.read("README.txt"))

        if os.path.exists(out):
            os.remove(out)
        count = 0
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for root, _dirs, files in os.walk(stage):
                for fn in sorted(files):
                    full = os.path.join(root, fn)
                    z.write(full, os.path.relpath(full, stage).replace("\\", "/"))
                    count += 1
    finally:
        shutil.rmtree(stage, ignore_errors=True)

    enabled = [l.strip() for l in io.open(args.config, encoding="utf-8") if l.strip().endswith("= true")]
    print("\nshipped config has %d settings enabled:" % len(enabled))
    for line in enabled:
        print("    %s" % line)

    print("\nwrote %s  (%d files, %d bytes)" % (out, count, os.path.getsize(out)))


if __name__ == "__main__":
    main()
