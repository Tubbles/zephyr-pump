#!/usr/bin/env bash
#
# Thin wrapper: run any command inside the pinned Zephyr build environment.
# Prepend this script to whatever you want to run; everything after it runs
# verbatim in the container. It knows nothing about the Makefile or any recipe.
#
# Examples (from the repo root):
#   ./dev.sh make update                                 # fetch the workspace (once)
#   ./dev.sh make build                                  # via the Makefile
#   ./dev.sh west build -b xiao_esp32c6/esp32c6/hpcore app -d build   # raw west
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
IMAGE="${ZEPHYR_IMAGE:-zephyr-pump:v4.4.1}"

# Always (re)build so a changed Dockerfile takes effect without manual cache
# busting. Layer caching makes a no-op build near-instant -- only a changed layer
# and the ones after it re-run -- so this is cheap on every invocation. The first
# build is the slow, network-bound one (clones Zephyr, downloads the SDK).
# Output goes to stderr so the wrapped command's stdout stays clean.
podman build -t "$IMAGE" "$REPO_DIR" >&2

tty_flags=()
[ -t 0 ] && [ -t 1 ] && tty_flags=(-it)

# --init runs a tiny init as PID 1 so Ctrl+C is forwarded and child processes (a
# long `git fetch`, a hung build) are reaped. Without it SIGINT leaves the
# container alive and the terminal wedged. --userns=keep-id maps the container
# process back to your host uid, so artifacts (and the fetched workspace) come
# out owned by you. HOME=/tmp gives CMake/west a writable cache dir. The repo is
# mounted at its host path (so in-container and host paths match) as the west
# topdir, and ZEPHYR_BASE points at the zephyr checkout the workspace fetched.
#
# --security-opt label=disable runs the container unconfined by SELinux type
# enforcement (as spc_t). On a Fedora / SELinux-enforcing host this is what lets
# `west flash` open the board's serial node: the XIAO's USB-Serial/JTAG node is
# labeled usb_device_t, which a confined container_t is not allowed to open
# unless the host's container_use_devices boolean is set. Disabling label
# confinement keeps that fix in the repo rather than as host state. It also makes
# the repo mount's former `:z` relabel unnecessary (an unconfined container reads
# the repo whatever its SELinux label), so the bind mounts below carry no `:z`.
#
# The board's serial port(s) are passed in with --device so `west flash`
# (esptool) can reach the XIAO over USB-Serial/JTAG. Every /dev/ttyACM*/ttyUSB*
# that exists right now is bound, since the kernel renumbers the node on every
# replug. Harmless when no board is attached: there is just nothing to flash.
#
# Two host paths grant rw on the node, and we cover both:
#   1. Desktop seat: logind's uaccess ACL grants your seat uid rw directly on the
#      node. --userns=keep-id maps the container process back to that uid, so the
#      per-uid ACL applies inside the container too.
#   2. No seat ACL (headless, or a desktop without one): the node is only
#      group-accessible (root:dialout rw-rw----) and you reach it via dialout
#      group membership. --userns=keep-id maps your uid but NOT your supplementary
#      groups, so dialout is dropped (the node shows as nobody:nogroup, the
#      namespace overflow id) and the open fails with EACCES. --group-add
#      keep-groups keeps the host's real supplementary GIDs in the process, so the
#      kernel still matches dialout on its DAC check and the open succeeds.
#      Requires your host user to be in the dialout group.
# (SELinux is the other half, handled by label=disable above.)
serial_devices=()
for node in /dev/ttyACM* /dev/ttyUSB*; do
  [ -e "$node" ] && serial_devices+=(--device "$node")
done

exec podman run --rm --init "${tty_flags[@]}" \
  --userns=keep-id \
  --group-add keep-groups \
  --security-opt label=disable \
  -e HOME=/tmp \
  -e ZEPHYR_BASE="$REPO_DIR/zephyr" \
  -v "$REPO_DIR":"$REPO_DIR" \
  "${serial_devices[@]}" \
  -w "$REPO_DIR" \
  "$IMAGE" \
  "$@"
