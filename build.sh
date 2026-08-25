#!/bin/sh
# d3hack -- build inside a PERSISTENT devkitA64 container.
#
# Why this exists: `docker run --rm` starts a fresh container every time, so every build paid
# cold-filesystem-cache cost on the bind mount. Measured on a no-op build, that was most of it:
#
#     git describe, warm ....................    92 ms
#     gen_build_git.py, warm ................   230 ms
#     gen_build_git.py, cold (docker run) ...  9300 ms
#
# A container that stays alive keeps the page cache warm between builds. Combined with dropping
# CONFIGURE_DEPENDS (12.7 s of re-globbing per build) and `git describe --dirty` (a full
# working-tree stat), a no-op build went from 44 s to a couple of seconds.
#
#     ./build.sh              build
#     ./build.sh deploy       build, then deploy if the build SUCCEEDED
#     ./build.sh configure    re-run cmake configure (needed after ADDING/REMOVING a source file,
#                             because CONFIGURE_DEPENDS is off on purpose)
#     ./build.sh shell        interactive shell in the build container
#     ./build.sh stop         stop the container
#
# The container is disposable; stop it any time. Nothing is stored in it -- source and build
# output both live on the host mount.

set -e

NAME=d3hack-build
IMAGE=devkitpro/devkita64:latest
SRC="$(cd "$(dirname "$0")" && pwd)"
HOSTSRC="C:/D3 Server/switch/d3hack/src"

ensure_up() {
    if [ -n "$(docker ps -q -f name="^${NAME}$")" ]; then
        return
    fi
    if [ -n "$(docker ps -aq -f name="^${NAME}$")" ]; then
        docker start "$NAME" >/dev/null
        echo "build container restarted"
        return
    fi
    MSYS_NO_PATHCONV=1 docker run -d --name "$NAME" \
        -v "${HOSTSRC}:/work" -w /work "$IMAGE" \
        tail -f /dev/null >/dev/null
    echo "build container created"
}

in_container() {
    MSYS_NO_PATHCONV=1 docker exec -w /work "$NAME" bash -lc "$1"
}

case "${1:-build}" in
    stop)
        docker rm -f "$NAME" >/dev/null 2>&1 || true
        echo "build container removed"
        exit 0
        ;;
    shell)
        ensure_up
        MSYS_NO_PATHCONV=1 exec docker exec -it -w /work "$NAME" bash -l
        ;;
    configure)
        ensure_up
        in_container 'cmake --preset switch'
        exit 0
        ;;
esac

ensure_up

# Configure on first use, or after the build dir is wiped.
if ! in_container 'test -f build/build.ninja' >/dev/null 2>&1; then
    echo "no build dir -- configuring"
    in_container 'cmake --preset switch >/dev/null'
fi

# Capture the build's OWN exit status. A previous session piped the build through `tail` and
# then chained deploy with &&, so the shell saw tail's status, a failed build still deployed,
# and a STALE binary shipped while the build-stamp header had already regenerated.
set +e
in_container 'cmake --build --preset switch -j"$(nproc)"'
rc=$?
set -e

if [ $rc -ne 0 ]; then
    echo "BUILD FAILED (exit $rc) -- not deploying"
    exit $rc
fi

if [ "${1:-build}" = "deploy" ]; then
    "$SRC/deploy.sh"
    grep TIMESTAMP "$SRC/build/generated/d3hack_build_git.hpp" 2>/dev/null || true
fi
