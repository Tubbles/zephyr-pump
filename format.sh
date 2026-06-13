#!/usr/bin/env bash
#
# Format the repo's own sources in place. This is a PLAIN recipe: it knows
# nothing about Podman and assumes Prettier and clang-format are on PATH, which
# they are inside the build container (the Dockerfile bakes both). Run it through
# dev.sh so it executes in that container, exactly like the Makefile targets:
#
#   ./dev.sh ./format.sh        # format everything
#   ./dev.sh make format        # same thing via the Makefile
#
# Prettier handles Markdown and keeps the dense pinout tables in DESIGN.md lined
# up (proseWrap is preserve, see .prettierrc, so prose is left alone).
# clang-format handles the C in app/ using the repo's .clang-format, which is
# Zephyr's own style so app code matches the tree it builds against.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

# File lists come from git so the gitignored zephyr/ checkout (thousands of
# upstream files) is never touched; only the repo's own sources match.
mapfile -t markdown < <(git ls-files '*.md')
mapfile -t sources < <(git ls-files '*.c' '*.h' '*.cpp' '*.hpp')

if [ "${#markdown[@]}" -gt 0 ]; then
  prettier --write "${markdown[@]}"
fi

if [ "${#sources[@]}" -gt 0 ]; then
  clang-format -i "${sources[@]}"
fi
