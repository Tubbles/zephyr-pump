# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A self-contained Zephyr RTOS build environment for the SiFive HiFive1 Rev B
(FE310-G002, RISC-V). The repo + Dockerfile fully describe the toolchain;
nothing is installed on the host except Podman. `app/` is the only thing you
edit.

## Build and run

Everything runs inside the pinned container via `dev.sh`, which prepends the
Zephyr environment to any command. The `Makefile` holds plain Zephyr recipes
and knows nothing about Podman; `dev.sh` knows nothing about the Makefile.
Compose them:

```
./dev.sh make update                            # fetch workspace into the repo (once)
./dev.sh make build                            # incremental build
./dev.sh make pristine                          # clean (-p always) build
./dev.sh make menuconfig                         # Kconfig editor
./dev.sh make boards                             # list boards
./dev.sh make BOARD=hifive1 build                # override board (default hifive1_revb)
./dev.sh bash                                    # interactive shell in the env
./dev.sh west build -b hifive1_revb app -d build  # raw west, bypassing the Makefile
make clean                                       # just an rm; runs on the host, no dev.sh
./console.sh                                     # read-only UART monitor (host, 115200 baud)
```

First `dev.sh` invocation builds the image (installs west + Zephyr's Python deps,
downloads the SDK): slow and needs network. Then `./dev.sh make update` fetches
the Zephyr source into the repo (also slow, also network) -- required once
before the first build, and again after any `west.yml` revision bump. Later runs
reuse both. The workspace and `build/` persist on the host between runs (both
git-ignored), so incremental builds work; use `pristine` for a clean rebuild.
Output lands in `build/zephyr/` (`zephyr.elf`, `.bin`, `.hex`), owned by you.

There is no test suite or linter in this repo. Verification is building
cleanly and (optionally) flashing + watching `console.sh` output.

## Architecture

Source lives on the host; only tools live in the image. The repo alone
determines the entire environment:

- `west.yml` pins the Zephyr revision and imports Zephyr's manifest with a
  `name-allowlist` so only the modules this board needs are fetched (empty for
  the in-tree FE310 / HiFive1; widen it when a feature needs a module). The repo
  root is the west topdir (Zephyr "T2 / star topology" application); `make
  update` copies this file into a generated, gitignored `.manifest/` git repo
  and runs `west init -l .manifest`, so the checkouts land in the repo (all
  gitignored).
- `Dockerfile` bakes only TOOLS: west (installed system-wide, no venv -- a
  disposable container needs none), Zephyr's revision-matched Python deps, and
  the RISC-V SDK. It harvests the deps + SDK from a throwaway workspace built
  from `west.yml` (copied in first for layer caching) and then deletes that
  source, so no workspace is baked in.
- `dev.sh` bind-mounts the repo as the workspace topdir at its **host path**
  (`$PWD:$PWD:z`, so in-container paths match the host) and runs there with
  `--userns=keep-id` so artifacts (and the workspace) come out owned by you,
  `HOME=/tmp` for a writable cache, and `ZEPHYR_BASE=$repo/zephyr` pointing at
  the checkout (the image has no Zephyr of its own).

This split is deliberate: baking the workspace as root while running as your uid
caused git "dubious ownership" failures that broke west's manifest import. With
the source host-side and owned by you, that whole class of problem is gone.

## Changing versions

- Zephyr version: edit `revision:` in `west.yml`, rebuild the image
  (`podman image rm zephyr-hifive1:v4.4.1` then any `./dev.sh` command), then
  `./dev.sh make update` to refresh the workspace to the new revision.
- OS / SDK: edit the `Dockerfile`, then rebuild the image.

The image tag is pinned to the Zephyr version in `dev.sh`
(`ZEPHYR_IMAGE`, default `zephyr-hifive1:v4.4.1`) and in the README's rebuild
instructions; keep all three in sync when bumping the Zephyr revision.

## Flashing

`west flash` here drives the onboard Segger J-Link OB, needing Segger's
proprietary J-Link pack and USB access. The intended split is: build in the
container, flash from the host against `build/zephyr/zephyr.elf`. In-container
flashing (USB passthrough + J-Link pack in a downstream layer) is deliberately
left out of the base setup.
