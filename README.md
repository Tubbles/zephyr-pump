# zephyr-hifive1

Self-contained Zephyr build environment for the SiFive HiFive1 Rev B
(FE310-G002). The git repo plus the Dockerfile fully describe the environment:
nothing is installed on your machine except Podman.

- `west.yml` pins the Zephyr revision and clones only Zephyr, no modules (the
  in-tree FE310 needs none); add an `import` allowlist when a feature needs a
  module.
- `Dockerfile` bakes only the tools (west, Zephyr's Python build deps, the
  RISC-V Zephyr SDK, and Segger's J-Link pack for flashing) into an image. The
  Zephyr source stays on the host.
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
./dev.sh make update                                 # fetch the workspace (once)
./dev.sh make build                                  # incremental build
./dev.sh make pristine                                # clean build
./dev.sh make menuconfig                              # Kconfig editor
./dev.sh make boards                                  # list boards
./dev.sh make BOARD=hifive1 build                     # override the board
./dev.sh bash                                         # shell in the env
./dev.sh west build -b hifive1_revb app -d build       # raw west, no Makefile
```

`make help` lists the recipes. `make clean` is just an rm and works on the host
too (no `dev.sh` needed).

The first invocation builds the image (installs west + Zephyr's Python deps and
downloads the SDK), so it takes a while and needs network. Then run
`./dev.sh make update` once to fetch the Zephyr source into the repo (also slow,
also network). Later runs reuse both and are fast; the workspace and `build/`
persist between runs, so incremental builds work (use `pristine` only when you
want a clean rebuild). Output ends up in `build/zephyr/` (`zephyr.elf`,
`zephyr.bin`, `zephyr.hex`), owned by you.

## Change the Zephyr version

Edit `revision:` in `west.yml`, then rebuild the image and refresh the
workspace:

```
podman image rm zephyr-hifive1:v4.4.1
./dev.sh make update
```

Both the image's baked tools/deps and the workspace are derived from `west.yml`,
so the repo alone determines the environment.

## Flashing

`west flash` drives the board's onboard Segger J-Link OB and works inside the
container: the image bakes JLinkExe and `dev.sh` mounts `/dev/bus/usb` so it can
reach the probe.

```
./dev.sh make build
./dev.sh west flash       # flashes build/zephyr/zephyr.elf via the J-Link
```

On a normal desktop no host setup is needed (logind's `uaccess` ACL already
grants your user the device, and `--userns=keep-id` carries that uid into the
container). On a headless box, install Segger's `99-jlink.rules` on the host.

Alternatively, drag-and-drop needs nothing in the container: the J-Link OB also
shows up as a USB mass-storage drive, so copying `build/zephyr/zephyr.hex` onto
it from the host flashes the board.

## Console output

The example prints over the board's UART, exposed by the J-Link OB as a USB
serial port (typically `/dev/ttyACM0`) at 115200 baud. `console.sh` is a
read-only monitor (runs on the host, auto-detects the device):

```
./console.sh
```
