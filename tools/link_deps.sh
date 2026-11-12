#!/bin/sh
# Link the already-fetched third-party sources into this checkout (worktrees share one copy).
#   tools/link_deps.sh [/home/claude/dq/third_party]
SRC="${1:-/home/claude/dq/third_party}"
cd "$(dirname "$0")/../third_party" || exit 1
for d in SDL2 imgui miniaudio enet quickjs stb glm miniz json; do
  [ -e "$d" ] || ln -s "$SRC/$d" "$d"
done
echo "third_party linked from $SRC"
