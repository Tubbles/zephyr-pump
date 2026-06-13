#!/usr/bin/env bash
#
# Format the repo's own Markdown with Prettier. This is a PLAIN recipe: it knows
# nothing about Podman and assumes Prettier is already on PATH, which it is
# inside the build container (the Dockerfile bakes it). Run it through dev.sh so
# it executes in that container, exactly like the Makefile targets:
#
#   ./dev.sh ./format.sh             # format all tracked *.md
#   ./dev.sh ./format.sh DESIGN.md   # format only the files you name
#   ./dev.sh make format             # same thing via the Makefile
#
# The point is keeping the dense pinout tables in DESIGN.md lined up: Prettier
# pads every table cell to its column width. proseWrap is preserve (see
# .prettierrc), so a run only realigns tables and other structure, never the
# hand-wrapped prose.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

# Default to the repo's own tracked Markdown, resolved with git so the gitignored
# zephyr/ checkout (thousands of .md files) is never touched; pass explicit paths
# to override.
if [ "$#" -gt 0 ]; then
  targets=("$@")
else
  mapfile -t targets < <(git ls-files '*.md')
fi

if [ "${#targets[@]}" -eq 0 ]; then
  echo "error: no Markdown files to format" >&2
  exit 1
fi

exec prettier --write "${targets[@]}"
