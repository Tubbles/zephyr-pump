# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## What this is

A self-contained Zephyr RTOS build environment for the Seeed XIAO ESP32-C6
(ESP32-C6, RISC-V). The repo + Dockerfile fully describe the toolchain; nothing
is installed on the host except Podman. `app/` holds a ported A4988 stepper +
LEDC-PWM speaker shell demo (the basis for the pump controller) and is the only
thing you edit. Make sure to read docs/DESIGN.md to understand the project.
docs/LOG.md is a grep-able log of bring-up learnings and decisions (boot-capture
gotchas, west module dependencies, DirectXIP and WiFi findings) that the commits
and code do not capture; skim it before debugging hardware or the build.

## Documentation and process

Project docs live in `docs/` (DESIGN.md, LOG.md, SUGGESTIONS.md); TODO.md stays
in the repo root. You have standing permission to create new documents and to
set up new processes whenever you notice one is missing, at any time, without
asking first. When you learn something worth keeping, spot a recurring task, or
find a gap a document would fill, write it down then and there: design notes and
references under `docs/`, outstanding work in TODO.md, your own ideas in
SUGGESTIONS.md. Prefer extending an existing doc over starting a parallel one,
and add a pointer from CLAUDE.md or the README when a newcomer should be able to
find the new doc.

Close out every work item by documenting it before you commit, the same way the
commit and push is part of finishing the work. Put the learnings, gotchas, dead
ends, verified facts, and the reasoning behind decisions in `docs/LOG.md`: the
git diff records what changed, the log records what you found out and why. Record
new ideas you noticed in `docs/SUGGESTIONS.md`. Delete finished items from `TODO.md`
once their record is in `docs/LOG.md`, and keep the unfinished ones. Update `CLAUDE.md`, the `README`, or your memory
when a convention, build step, or durable preference changed. The documentation
pass is part of the work, not an optional extra.

## Git workflow

Commit directly to `main`; this repo does not use feature branches. Committing
and then pushing to the remote is the standing close-out step for every change:
when a piece of work is done, commit it and push it without waiting to be asked.
Keep commits small and focused, and never force-push.

When you notice the user has added content to a tracked doc, most commonly the
`## User written inbox` section of `TODO.md`, commit and push it too, in its own
commit, so their additions reach the remote and are never left sitting
uncommitted on the host.

## Build and run

Everything runs inside the pinned container via `dev.sh`, which prepends the
Zephyr environment to any command. The `Makefile` holds plain Zephyr recipes and
knows nothing about Podman; `dev.sh` knows nothing about the Makefile. Compose
them:

```
./dev.sh make update                            # fetch workspace + blobs into the repo (once)
./dev.sh make build                            # incremental build
./dev.sh make pristine                          # clean (-p always) build
./dev.sh make menuconfig                         # Kconfig editor
./dev.sh make boards                             # list boards
./dev.sh make format                             # format Markdown + C (Prettier, clang-format)
./dev.sh make BOARD=esp32c6_devkitc/esp32c6/hpcore build        # override board (default xiao_esp32c6/esp32c6/hpcore)
./dev.sh bash                                    # interactive shell in the env
./dev.sh west build -b xiao_esp32c6/esp32c6/hpcore app -d build --sysbuild  # raw west, bypassing the Makefile
make clean                                       # just an rm; runs on the host, no dev.sh
./console.sh                                     # read-only serial monitor (host, 115200 baud)
```

`dev.sh` rebuilds the image on every run, but layer caching makes that
near-instant once built; the first build is the slow, network-bound one
(installs west + Zephyr's Python deps, downloads the SDK). Then
`./dev.sh make update` fetches the Zephyr source plus the Espressif binary blobs
into the repo (also slow, also network) -- required once before the first build,
and again after any `west.yml` revision bump. Later runs reuse both. The
workspace and `build/` persist on the host between runs (both git-ignored), so
incremental builds work; use `pristine` for a clean rebuild. Builds run under
sysbuild (MCUboot + the app), so output is split per image: the bootloader at
`build/mcuboot/zephyr/zephyr.bin`, the slot-0 app at
`build/app/zephyr/zephyr.signed.bin`, and the slot-1 update image at
`build/app_slot1_variant/zephyr/zephyr.signed.bin`, all owned by you.

There is no test suite or linter in this repo. Verification is building cleanly
and (optionally) flashing + watching `console.sh` output.

## Architecture

Source lives on the host; only tools live in the image. The repo alone
determines the entire environment:

- `west.yml` pins the Zephyr revision and imports Zephyr's manifest filtered to
  two modules: `hal_espressif` (the Espressif HAL, ESP simple-boot bootloader
  support, and WiFi/BT blobs) and `mcuboot` (the MCUboot secondary bootloader,
  built by sysbuild for OTA / A-B slot swapping). The rest of the SoC support is
  in-tree. The name-allowlist keeps west from fetching the other ~40 module
  repos; importing Zephyr's manifest also registers its build/flash extension
  commands. An empty allowlist
  imports everything, so don't use that to mean "none"; add another module by
  appending its name. The repo root is the west topdir (Zephyr "T2 / star
  topology" application); `make update` copies this file into a generated,
  gitignored `.manifest/` git repo and runs `west init -l .manifest`, so the
  checkouts land in the repo (all gitignored).
- `Dockerfile` bakes only TOOLS: west (installed system-wide, no venv -- a
  disposable container needs none), Zephyr's revision-matched Python deps (which
  include esptool, pulled in via hal_espressif's requirement files for
  flashing), the RISC-V SDK, and formatters (a pinned Prettier for Markdown,
  clang-format for C). It harvests the deps + SDK from a throwaway workspace
  built from `west.yml` (copied in first for layer caching) and then deletes
  that source, so no workspace is baked in. There is no separate flash-probe
  download: the XIAO flashes over its native USB-Serial/JTAG with esptool.
- `dev.sh` bind-mounts the repo as the workspace topdir at its **host path**
  (`$PWD:$PWD`, so in-container paths match the host) and runs there with
  `--userns=keep-id` so artifacts (and the workspace) come out owned by you,
  `HOME=/tmp` for a writable cache, and `ZEPHYR_BASE=$repo/zephyr` pointing at
  the checkout (the image has no Zephyr of its own). It passes each
  `/dev/ttyACM*//dev/ttyUSB*` that exists into the container with `--device` so
  `west flash` (esptool) can reach the board's USB-Serial/JTAG, and runs with
  `--security-opt label=disable` so SELinux on a Fedora-style host does not block
  opening the serial node's `usb_device_t` (which also makes the mount's former
  `:z` relabel unnecessary).

This split is deliberate: baking the workspace as root while running as your uid
caused git "dubious ownership" failures that broke west's manifest import. With
the source host-side and owned by you, that whole class of problem is gone.

## Changing versions

`dev.sh` rebuilds on every run, so edits below take effect on the next
`./dev.sh` invocation -- layer caching only re-runs the changed layer and those
after it. (`podman image rm zephyr-pump:v4.4.1` forces a cache-free rebuild
if you ever want one.)

- Zephyr version: edit `revision:` in `west.yml`, then `./dev.sh make update` to
  refresh the workspace to the new revision.
- OS / SDK: edit the `Dockerfile`.
- esptool version: not a separate knob -- it is pinned by the Zephyr revision
  via hal_espressif's requirement files, installed by `west packages pip`.
- Prettier version: edit `PRETTIER_VERSION` in the `Dockerfile`.
- clang-format version: tracks the pinned OS (`Dockerfile` base image); bump it
  by bumping Debian. The `.clang-format` itself is vendored from Zephyr, so
  refresh it from the tree if its style changes on a revision bump.

The image tag is pinned to the Zephyr version in `dev.sh` (`ZEPHYR_IMAGE`,
default `zephyr-pump:v4.4.1`) and in the README's rebuild instructions; keep
all three in sync when bumping the Zephyr revision.

## Flashing

`west flash` flashes the XIAO over its native USB-Serial/JTAG with esptool and
works inside the container: the image carries esptool (the `esp32` runner shells
out to it) and `dev.sh` passes the board's `/dev/ttyACM*` serial node in.

```
./dev.sh make build       # build first (flash does not rebuild reliably on its own)
./dev.sh west flash       # flash MCUboot + app via esptool (--esp-device /dev/ttyACM0 to pin the port)
```

Host permissions: the serial node is `root:dialout rw-rw----`, so add your user
to the `dialout` group on the **host**. `dev.sh` carries that membership into the
container with `--group-add keep-groups`, since `--userns=keep-id` maps your uid
but drops supplementary groups (without keep-groups the node shows as
`nobody:nogroup` inside the container and the open fails with EACCES). On a
desktop where logind sets a per-uid `uaccess` ACL on the node, that ACL plus
`--userns=keep-id` would suffice on its own, but not every host has it, so the
dialout group is the reliable path. esptool auto-resets the board into download
mode over USB-Serial/JTAG; if that ever fails, hold BOOT while tapping RESET,
then flash.
