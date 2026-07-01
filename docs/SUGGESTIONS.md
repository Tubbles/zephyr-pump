# Suggestions

Ideas noticed while porting from the HiFive1/FE310 to the XIAO ESP32-C6. None
were requested; they are candidate improvements, not commitments.

## Battery monitoring via the onboard SAR ADC

`DESIGN.md` notes this: the FE310 had no ADC, so the original plan put pack
voltage on an external I²C ADS1115. The ESP32-C6 has a SAR ADC on the lower
header pins (D0–D2 / GPIO0–GPIO2), which the current pin assignment keeps free
for exactly this. A resistor divider into one of those reads pack voltage
directly and removes a part from the BOM. The ADS1115 stays an option if higher
resolution or a dedicated channel is wanted.

## Wireless pump control / telemetry

The ESP32-C6 has WiFi 6, BLE and 802.15.4 (Thread/Zigbee) on-chip. The pump could
expose remote start/stop/rate or report status and battery state over a radio
with no extra hardware. Out of scope for the initial bring-up.

## Host-side tests for the motor math, emul for future sensors

The period/duty arithmetic in `app/src/motor.c` (`start_train`, the `steps`
duration calc) is pure logic and could be unit-tested on `native_sim` with ztest,
independent of hardware. If I²C sensors are added later, Zephyr's `emul`
framework can model them in software so driver and app logic are testable in CI
without the physical parts.

## DirectXIP with revert + in-app confirm for safe rollback

The MCUboot foundation uses plain DirectXIP (`SB_CONFIG_MCUBOOT_MODE_DIRECT_XIP`
in `app/sysbuild.conf`): it boots the higher-version slot but has no
test/confirm/revert. Switching to `..._DIRECT_XIP_WITH_REVERT` plus an in-app
`boot_write_img_confirmed()` (from `<zephyr/dfu/mcuboot.h>`) once the app is
healthy would auto-roll-back an update that fails to confirm. Left out of the
A/B proof to keep the first hardware test minimal.

## OTA self-update (implemented)

Done, but as an HTTP(S) pull rather than the SMP server first sketched here: the
running app fetches a signed image from GitHub Pages and writes the inactive slot
via the `flash_img` API, then reboots into it under DirectXIP. See docs/OTA.md and
docs/LOG.md [ota]. The remaining hardening is the next two items.

## Enable image signing (the primary OTA trust control)

Signing is off today (`BOOT_SIGNATURE_TYPE_NONE`), so images carry an MCUboot
header + SHA-256, which is integrity only, not authenticity: anyone who can write a
slot, or serve the configured URL, can install firmware. Generate an Ed25519 key
with `imgtool`, set `SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519` + the key, and embed
the public key in the bootloader. This is the right layer for a configurable
endpoint: trust lives in your signature, not the host, so the image is
authenticated wherever `update url` points. Needed before trusting OTA in the
field.

## Transport authentication for the OTA fetch (secondary to signing)

The OTA client runs TLS with `TLS_PEER_VERIFY_NONE` (encrypt-only): the channel is
encrypted but the server is not authenticated. This is deliberately secondary to
signing. Once the image is signed, a substituted image is rejected regardless of
who served it or what cert they had, so authenticity does not depend on the
transport. If transport authentication is still wanted on top, it must be
endpoint-specific, not baked in: `update url` makes the host configurable, so the
firmware cannot assume GitHub/Fastly or any one CA. The host-neutral way is to
provision the expected server key with the URL (e.g. `update url <url>
<spki-sha256>`) and pin the public key (its SPKI hash), which survives cert
renewals that reuse the key and needs no clock. Pinning a specific CA or leaf in
firmware is the wrong layer: it couples a host-neutral device to one host's
rotation schedule.

## A reliable boot-capture helper for the native USB-Serial/JTAG

`console.sh` works for steady-state monitoring, but the C6's USB-Serial/JTAG
re-enumerates on every reboot, so a plain `cat` loses its file descriptor
mid-boot and misses the banner. Capturing a boot reliably needed an esptool
reset (it drives the USB-JTAG reset), a reconnecting reader that reopens the
port as it reappears, and `stty ... clocal -crtscts`. Worth packaging as a
small `bootlog.sh` helper next to `console.sh`.
