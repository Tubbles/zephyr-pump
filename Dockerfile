# Tools-only Zephyr build environment for the SiFive HiFive1 Rev B.
#
# This image bakes only the TOOLS: west, Zephyr's Python build deps, the
# RISC-V Zephyr SDK, and Prettier (Markdown formatter). It deliberately contains
# NO Zephyr workspace -- the source
# checkout (zephyr/, modules/, ...) lives on the host alongside your project and
# is fetched with `./dev.sh make update`. That keeps source on the host (owned
# by you) and only programs in the image. west.yml pins the Zephyr revision;
# this file pins the OS + SDK. Rebuild the image to change tool/SDK versions.
#
# Debian 13 (trixie), the latest stable release: system python3 is 3.13, well
# above Zephyr's minimum. The -slim variant stays small; build deps are added
# explicitly below.
FROM debian:trixie-slim
ENV DEBIAN_FRONTEND=noninteractive

# Host build deps (Zephyr getting-started, Debian/Ubuntu list) + git (west clones over
# git) + ca-certificates (TLS for clones and the SDK download).
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
        python3-dev python3-pip python3-setuptools python3-wheel \
        xz-utils file make gcc libsdl2-dev libmagic1 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# No venv: a disposable container owns its whole Python environment, so install
# west system-wide. --break-system-packages overrides Debian's PEP 668
# "externally managed" guard, which only exists to protect a real host's python.
# Pin west explicitly: the rest of the toolchain is pinned (Zephyr revision, SDK,
# J-Link), so the orchestrator that drives them should be too.
RUN pip install --break-system-packages --no-cache-dir west==1.5.0

# --- Harvest the pinned tools, then throw the source away ------------------
# We need Zephyr's revision-matched Python deps and the SDK, but NOT a baked
# workspace. So materialize a throwaway workspace from the pinned manifest,
# install the deps + SDK (which land in system site-packages and /opt, outside
# the temp dir), then delete the source. Copy ONLY west.yml first so this whole
# layer stays cached unless the manifest changes. --ignore-venv-check because
# there is no venv; the pip args after `--` reach pip itself.
COPY west.yml /tmp/west.yml
RUN mkdir -p /tmp/ws/.manifest \
 && cp /tmp/west.yml /tmp/ws/.manifest/west.yml \
 && git -C /tmp/ws/.manifest init -q \
 && git -C /tmp/ws/.manifest add west.yml \
 && git -C /tmp/ws/.manifest -c user.email=build@local -c user.name=build commit -qm pin \
 && cd /tmp/ws \
 && west init -l .manifest \
 && west update --narrow -o=--depth=1 \
 && west packages pip --install --ignore-venv-check -- --break-system-packages \
 && west sdk install --install-dir /opt/zephyr-sdk --toolchains riscv64-zephyr-elf \
 && cd / \
 && rm -rf /tmp/ws /tmp/west.yml /root/.cache
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

# --- J-Link tools for in-container flashing --------------------------------
# The HiFive1 Rev B's onboard Segger J-Link OB is driven by west's `jlink`
# runner, which shells out to JLinkExe. Fetch the pinned J-Link Software pack
# straight from Segger and extract its self-contained tree (JLinkExe + the
# libjlinkarm shared libs, which resolve via $ORIGIN rpath) onto PATH; libusb-1.0
# is JLinkExe's USB backend. The download POST programmatically accepts Segger's
# EULA (accept_license_agreement=accepted) -- building this image accepts it.
# Pinned to V950 to match the repo's pin-everything policy; bump JLINK_VERSION to
# change it. Flashing also needs the host USB device passed in, which dev.sh does.
ARG JLINK_VERSION=V950
RUN apt-get update \
 && apt-get install -y --no-install-recommends libusb-1.0-0 \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir -p /opt/segger/jlink \
 && wget -q --post-data='accept_license_agreement=accepted&non_emb_ctr=confirmed' \
        "https://www.segger.com/downloads/jlink/JLink_Linux_${JLINK_VERSION}_x86_64.tgz" \
        -O /tmp/jlink.tgz \
 && tar xzf /tmp/jlink.tgz --strip-components=1 -C /opt/segger/jlink \
 && rm /tmp/jlink.tgz
ENV PATH="/opt/segger/jlink:${PATH}"

# --- Formatters ------------------------------------------------------------
# `./dev.sh make format` (and `./dev.sh ./format.sh`) run Prettier on the repo's
# Markdown (keeping the dense pinout tables in DESIGN.md aligned) and clang-format
# on the C in app/. Prettier is a Node tool, so bake Node + a pinned global
# Prettier; clang-format comes from Debian, so its version tracks the pinned OS.
# Both then run in the same container as the build, keeping the host free of
# either. Bump PRETTIER_VERSION to change Prettier.
ARG PRETTIER_VERSION=3.4.2
RUN apt-get update \
 && apt-get install -y --no-install-recommends nodejs npm clang-format \
 && rm -rf /var/lib/apt/lists/* \
 && npm install -g "prettier@${PRETTIER_VERSION}" \
 && npm cache clean --force
