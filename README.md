# zephyr-pump

Self-contained Zephyr build environment for the Seeed XIAO ESP32-C6
(ESP32-C6, RISC-V). The git repo plus the Dockerfile fully describe the
environment: nothing is installed on your machine except Podman.

- `west.yml` pins the Zephyr revision and imports only the one module ESP32-C6
  needs (`hal_espressif`, for the Espressif HAL and bootloader); the rest of the
  SoC support is in-tree.
- `Dockerfile` bakes only the tools (west, Zephyr's Python build deps including
  esptool for flashing, the RISC-V Zephyr SDK, plus Prettier for Markdown and
  clang-format for C) into an image. The Zephyr source stays on the host.
- The repo itself is the west workspace: `./dev.sh make update` fetches the
  source (`zephyr/`, `modules/`, ...) into it, all gitignored. `app/` is the
  only thing you edit.

## Run

`dev.sh` is a thin environment wrapper: prepend it to any command and that
command runs in the pinned Zephyr environment. It knows nothing about the
Makefile. The repo is mounted as the workspace topdir and is the working
directory, so `app/` is built with relative paths and output lands in `build/`
on the host.

The `Makefile` holds plain recipes; compose them with `dev.sh`:

```
./dev.sh make update                                    # fetch the workspace + blobs (once)
./dev.sh make build                                     # incremental build
./dev.sh make pristine                                  # clean build
./dev.sh make menuconfig                                # Kconfig editor
./dev.sh make boards                                    # list boards
./dev.sh make format                                    # format Markdown + C (Prettier, clang-format)
./dev.sh make BOARD=esp32c6_devkitc/esp32c6/hpcore build               # override the board
./dev.sh bash                                           # shell in the env
./dev.sh west build -b xiao_esp32c6/esp32c6/hpcore app -d build  # raw west, no Makefile
```

`make help` lists the recipes. `make clean` is just an rm and works on the host
too (no `dev.sh` needed).

`dev.sh` rebuilds the image on every run; layer caching keeps that near-instant
after the first build, which is the slow, network-bound one (installs west +
Zephyr's Python deps, downloads the SDK). Then run `./dev.sh make update` once
to fetch the Zephyr source plus the Espressif binary blobs into the repo (also
slow, also network). Later runs reuse both and are fast; the workspace and
`build/` persist between runs, so incremental builds work (use `pristine` only
when you want a clean rebuild). Output ends up in `build/zephyr/` (`zephyr.elf`
and the flashable `zephyr.bin`), owned by you.

## Change the Zephyr version

Edit `revision:` in `west.yml`, then refresh the workspace:

```
./dev.sh make update
```

`dev.sh` rebuilds the image on every run (layer caching keeps it quick), so the
new revision is baked automatically; `make update` then refreshes the checkout.
Both the image's baked tools/deps and the workspace are derived from `west.yml`,
so the repo alone determines the environment.

## Flashing

The XIAO ESP32-C6 flashes over its native USB-Serial/JTAG port with esptool, no
hardware debug probe. `west flash` works inside the container: the image carries
esptool and `dev.sh` passes the board's `/dev/ttyACM*` serial node in.

```
./dev.sh make build
./dev.sh west flash       # flashes build/zephyr/ via esptool over USB-Serial/JTAG
```

esptool auto-detects the port; if you have more than one board attached, pin it
with `./dev.sh west flash --esp-device /dev/ttyACM0`. Add yourself to the
`dialout` group on the host (`sudo usermod -aG dialout $USER`, then re-login):
the serial node is `root:dialout rw-rw----`, and `dev.sh` carries your
membership into the container via `--group-add keep-groups`. On a desktop with a
logind `uaccess` ACL this group is technically redundant (the per-uid ACL plus
`--userns=keep-id` already grants access), but not every host has that ACL, so
the group path is the reliable one. If auto-reset into the download mode ever
fails, hold the BOOT button while tapping RESET, then flash.

## Console output

The example prints over the board's USB-Serial/JTAG port, which shows up as a
USB serial device (typically `/dev/ttyACM0`) at 115200 baud. `console.sh` is a
read-only monitor (runs on the host, auto-detects the device):

```
./console.sh
```
