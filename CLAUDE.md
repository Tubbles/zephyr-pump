# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## What this is

A self-contained Zephyr RTOS build environment for the SiFive HiFive1 Rev B
(FE310-G002, RISC-V). The repo + Dockerfile fully describe the toolchain;
nothing is installed on the host except Podman. `app/` is the only thing you
edit. Make sure to read DESIGN.md to understand the project.

## Build and run

Everything runs inside the pinned container via `dev.sh`, which prepends the
Zephyr environment to any command. The `Makefile` holds plain Zephyr recipes and
knows nothing about Podman; `dev.sh` knows nothing about the Makefile. Compose
them:

```
./dev.sh make update                            # fetch workspace into the repo (once)
./dev.sh make build                            # incremental build
./dev.sh make pristine                          # clean (-p always) build
./dev.sh make menuconfig                         # Kconfig editor
./dev.sh make boards                             # list boards
./dev.sh make format                             # format Markdown + C (Prettier, clang-format)
./dev.sh make BOARD=hifive1 build                # override board (default hifive1_revb)
./dev.sh bash                                    # interactive shell in the env
./dev.sh west build -b hifive1_revb app -d build  # raw west, bypassing the Makefile
make clean                                       # just an rm; runs on the host, no dev.sh
./console.sh                                     # read-only UART monitor (host, 115200 baud)
```

`dev.sh` rebuilds the image on every run, but layer caching makes that
near-instant once built; the first build is the slow, network-bound one
(installs west + Zephyr's Python deps, downloads the SDK + J-Link pack). Then
`./dev.sh make update` fetches the Zephyr source into the repo (also slow, also
network) -- required once before the first build, and again after any `west.yml`
revision bump. Later runs reuse both. The workspace and `build/` persist on the
host between runs (both git-ignored), so incremental builds work; use `pristine`
for a clean rebuild. Output lands in `build/zephyr/` (`zephyr.elf`, `.bin`,
`.hex`), owned by you.

There is no test suite or linter in this repo. Verification is building cleanly
and (optionally) flashing + watching `console.sh` output.

## Architecture

Source lives on the host; only tools live in the image. The repo alone
determines the entire environment:

- `west.yml` pins the Zephyr revision and clones only Zephyr, no modules, for
  the in-tree FE310 / HiFive1: it sets `west-commands` to register the
  build/flash extensions and omits `import:` so west fetches no module repos
  (add an `import` allowlist when a feature needs a module; an empty allowlist
  imports everything, so don't use that to mean "none"). The repo root is the
  west topdir (Zephyr "T2 / star topology" application); `make update` copies
  this file into a generated, gitignored `.manifest/` git repo and runs
  `west
init -l .manifest`, so the checkouts land in the repo (all
  gitignored).
- `Dockerfile` bakes only TOOLS: west (installed system-wide, no venv -- a
  disposable container needs none), Zephyr's revision-matched Python deps, the
  RISC-V SDK, Segger's J-Link pack for flashing, and formatters (a pinned
  Prettier for Markdown, clang-format for C). It harvests the deps + SDK
  from a throwaway workspace built from `west.yml` (copied in first for layer
  caching) and then deletes that source, so no workspace is baked in. The J-Link
  pack is fetched straight from Segger at a pinned version; its download POST
  auto-accepts Segger's EULA, so building the image accepts it.
- `dev.sh` bind-mounts the repo as the workspace topdir at its **host path**
  (`$PWD:$PWD`, so in-container paths match the host) and runs there with
  `--userns=keep-id` so artifacts (and the workspace) come out owned by you,
  `HOME=/tmp` for a writable cache, and `ZEPHYR_BASE=$repo/zephyr` pointing at
  the checkout (the image has no Zephyr of its own). It also mounts
  `/dev/bus/usb` so `west flash` can reach the board's J-Link over USB, and runs
  with `--security-opt label=disable` so SELinux on a Fedora-style host does not
  block opening the probe's `usb_device_t` node (which also makes the mount's
  former `:z` relabel unnecessary).

This split is deliberate: baking the workspace as root while running as your uid
caused git "dubious ownership" failures that broke west's manifest import. With
the source host-side and owned by you, that whole class of problem is gone.

## Changing versions

`dev.sh` rebuilds on every run, so edits below take effect on the next
`./dev.sh` invocation -- layer caching only re-runs the changed layer and those
after it. (`podman image rm zephyr-hifive1:v4.4.1` forces a cache-free rebuild
if you ever want one.)

- Zephyr version: edit `revision:` in `west.yml`, then `./dev.sh make update` to
  refresh the workspace to the new revision.
- OS / SDK: edit the `Dockerfile`.
- J-Link version: edit `JLINK_VERSION` in the `Dockerfile` (e.g. `V950`). Segger
  serves a tarball per version at a stable URL.
- Prettier version: edit `PRETTIER_VERSION` in the `Dockerfile`.
- clang-format version: tracks the pinned OS (`Dockerfile` base image); bump it
  by bumping Debian. The `.clang-format` itself is vendored from Zephyr, so
  refresh it from the tree if its style changes on a revision bump.

The image tag is pinned to the Zephyr version in `dev.sh` (`ZEPHYR_IMAGE`,
default `zephyr-hifive1:v4.4.1`) and in the README's rebuild instructions; keep
all three in sync when bumping the Zephyr revision.

## Flashing

`west flash` drives the board's onboard Segger J-Link OB and works inside the
container: the image bakes JLinkExe (the `jlink` runner shells out to it) and
`dev.sh` mounts `/dev/bus/usb` so it can reach the probe.

```
./dev.sh make build       # build first (flash does not rebuild reliably on its own)
./dev.sh west flash       # flash build/zephyr/zephyr.elf via the onboard J-Link
```

Host permissions usually need nothing: on a normal desktop, logind's `uaccess`
ACL grants your seat user rw on the J-Link node, and `--userns=keep-id` maps the
container process back to that uid. On a headless box with no seat ACL, install
Segger's `99-jlink.rules` (shipped in the J-Link pack) on the **host** so your
user can open the device.

Drag-and-drop still works too and needs none of this: the J-Link OB also exposes
a USB mass-storage drive, so copying `build/zephyr/zephyr.hex` onto it from the
host flashes the board.
