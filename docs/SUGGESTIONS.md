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

## In-app wireless OTA (WiFi/SMP) instead of esptool staging

The A/B demo staged slot 1 with `esptool write_flash 0x1e0000 ...` from the
host. A real OTA has the running app receive the image over a radio and write
slot 1 itself via the `flash_img` API + `boot_request_upgrade()`, or an MCUmgr
SMP server. WiFi (SMP-over-UDP) is the intended transport, BLE the fallback.
The bootloader/slot side is now in place; this is the remaining piece.

## Enable image signing (currently BOOT_SIGNATURE_TYPE_NONE)

The board defaults signing off, so images carry an MCUboot header + SHA-256 but
no cryptographic signature: anyone who can write a slot can install firmware.
Generating an Ed25519 key with `imgtool` and setting
`SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519` + a key file would make the update path
authentic. Skipped for the bring-up; needed before trusting OTA.

## Make the headless device discoverable on the network

Now that the app auto-connects and DHCP-leases an address, that address is only
visible over the console (`net iface`), which defeats the point of a headless
WiFi device. A stable hostname plus mDNS (`CONFIG_NET_HOSTNAME_ENABLE` +
`CONFIG_MDNS_RESPONDER`) would let a host reach it by name without first reading
the IP off the serial port. This becomes a prerequisite the moment the radio is
used for control, telemetry, or the WiFi/SMP OTA path above.

## A reliable boot-capture helper for the native USB-Serial/JTAG

`console.sh` works for steady-state monitoring, but the C6's USB-Serial/JTAG
re-enumerates on every reboot, so a plain `cat` loses its file descriptor
mid-boot and misses the banner. Capturing a boot reliably needed an esptool
reset (it drives the USB-JTAG reset), a reconnecting reader that reopens the
port as it reappears, and `stty ... clocal -crtscts`. Worth packaging as a
small `bootlog.sh` helper next to `console.sh`.
