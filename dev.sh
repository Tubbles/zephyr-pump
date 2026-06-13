#!/usr/bin/env bash
#
# Thin wrapper: run any command inside the pinned Zephyr build environment.
# Prepend this script to whatever you want to run; everything after it runs
# verbatim in the container. It knows nothing about the Makefile or any recipe.
#
# Examples (from the repo root):
#   ./dev.sh make update                                 # fetch the workspace (once)
#   ./dev.sh make build                                  # via the Makefile
#   ./dev.sh west build -b hifive1_revb app -d build      # raw west
#   ./dev.sh west boards
#   ./dev.sh bash
#
# The repo IS the west workspace topdir: it is mounted at the SAME path inside
# the container as on the host (so paths in errors, compile_commands.json, etc.
# match), and that is the working directory. `make update` fetches zephyr/,
# modules/, ... into it (all gitignored). The image carries only tools, so
# ZEPHYR_BASE points at the zephyr checkout inside the mounted repo. Build
# output lands in ./build.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${ZEPHYR_IMAGE:-zephyr-hifive1:v4.4.1}"

# Build the image on first use (or after you delete it / bump west.yml).
if ! podman image inspect "$IMAGE" >/dev/null 2>&1; then
  echo ">> building $IMAGE (clones Zephyr + downloads the SDK; takes a while) ..." >&2
  podman build -t "$IMAGE" "$REPO_DIR"
fi

tty_flags=()
[ -t 0 ] && [ -t 1 ] && tty_flags=(-it)

# --init: run a tiny init as PID 1 so Ctrl+C is forwarded and child processes
# (a long `git fetch`, a hung build) are reaped -- without it SIGINT leaves the
# container alive and the terminal wedged. --userns=keep-id + :z: rootless-
# podman/SELinux handling so artifacts come out owned by you. HOME=/tmp gives
# CMake/west a writable cache dir. The repo is mounted at its host path (so
# in-/out-of-container paths match) as the west topdir, and ZEPHYR_BASE points
# at the zephyr checkout the workspace fetched.
#
# /dev/bus/usb is mounted so `west flash` can reach the board's onboard J-Link
# over USB (the image bakes JLinkExe). The whole bus tree is mounted, not a fixed
# node, because the kernel renumbers the device on every replug. --userns=keep-id
# maps the container process back to your host uid, which logind's uaccess ACL on
# the device node already grants -- so no extra privilege or udev rule is needed.
# Harmless when no board is attached; the directory just has nothing to flash.
exec podman run --rm --init "${tty_flags[@]}" \
  --userns=keep-id \
  -e HOME=/tmp \
  -e ZEPHYR_BASE="$REPO_DIR/zephyr" \
  -v "$REPO_DIR":"$REPO_DIR":z \
  -v /dev/bus/usb:/dev/bus/usb \
  -w "$REPO_DIR" \
  "$IMAGE" \
  "$@"
