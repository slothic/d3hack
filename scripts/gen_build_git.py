#!/usr/bin/env python3

import os
import subprocess
import sys
from datetime import datetime


def _run(cmd):
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
        return out.decode("utf-8", errors="replace").strip()
    except Exception:
        return ""


def _ok(cmd):
    try:
        return subprocess.call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0
    except Exception:
        return False


def _c_escape(s):
    # Minimal C string literal escaping.
    return (
        s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def _should_skip(out_path):
    """d3hack-custom: leave the header alone when nothing has changed.

    This target runs on EVERY build, and rewriting the header (the timestamp always differs)
    invalidates build_stamp.cpp.obj, which forces a relink. Measured warm, that pair is the
    entire floor of a no-op build:

        compile build_stamp.cpp ....  9.7 s   (it pulls in the heavy common headers)
        link .......................  8.5 s

    If no source is newer than the existing header then the binary on disk IS current, and its
    stamp already says when that binary was built -- which is exactly what the stamp is for.
    Rewriting it would only make the timestamp lie about a build that did not happen.
    """
    if not os.path.exists(out_path):
        return False
    try:
        stamp = os.path.getmtime(out_path)
    except OSError:
        return False

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    watch = [os.path.join(root, d) for d in ("source", "include", "scripts", "cmake")]
    watch.append(os.path.join(root, "CMakeLists.txt"))
    # A commit or checkout must produce a new stamp even with no file edits.
    watch.append(os.path.join(root, ".git", "HEAD"))
    watch.append(os.path.join(root, ".git", "index"))

    for w in watch:
        if os.path.isfile(w):
            try:
                if os.path.getmtime(w) > stamp:
                    return False
            except OSError:
                return False
            continue
        for dirpath, _dirnames, filenames in os.walk(w):
            for fn in filenames:
                try:
                    if os.path.getmtime(os.path.join(dirpath, fn)) > stamp:
                        return False
                except OSError:
                    return False
    return True


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else None
    if out and _should_skip(out):
        return 0

    if len(sys.argv) != 2:
        print("usage: gen_build_git.py <out_header>", file=sys.stderr)
        return 2

    out_path = sys.argv[1]

    # d3hack-custom: NO --dirty here. It makes git stat the entire working tree, which
    # over a Docker bind mount cost ~9 s of every build. The dirty flag is cosmetic; the
    # BUILD TIMESTAMP below is the field that actually matters, because it is how you
    # confirm the binary you just built is the one that got deployed. Dirtiness is
    # derived cheaply from the index instead.
    git_describe = _run(["git", "describe", "--tags", "--always"])
    git_commit = _run(["git", "rev-parse", "--short", "HEAD"])
    # `git diff --quiet` exits 1 when there are unstaged changes and touches far less
    # than a full describe --dirty scan.
    dirty = 0 if _ok(["git", "diff", "--quiet"]) else 1
    build_timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")

    if not git_describe:
        git_describe = "unknown"
    if not git_commit:
        git_commit = "unknown"

    text = "\n".join(
        [
            "#pragma once",
            "",
            '#define D3HACK_GIT_DESCRIBE "%s"' % _c_escape(git_describe),
            '#define D3HACK_GIT_COMMIT "%s"' % _c_escape(git_commit),
            "#define D3HACK_GIT_DIRTY %d" % int(dirty),
            '#define D3HACK_BUILD_TIMESTAMP "%s"' % _c_escape(build_timestamp),
            "",
        ]
    )

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
