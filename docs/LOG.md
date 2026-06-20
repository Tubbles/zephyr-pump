# Engineering log

Learnings and decisions from the OTA bring-up that the git history and the code
do not capture: gotchas, verified facts, dead ends, and why choices were made.
The commits say *what* changed; this says *what we found out* and *what we
decided*.

Grep the `[bracketed]` keywords in the headers to jump around, e.g.
`grep -iE '\[serial\]|\[wifi\]' docs/LOG.md`. Dates mark when something was
learned.

---

## [serial] [usb-jtag] [console] [capture] 2026-06-20 — capturing a boot needs a reconnecting reader

The XIAO console is the ESP32-C6 *native* USB-Serial/JTAG, not an external
USB-UART chip. So the USB device drops and re-enumerates on every reboot, and
any open `/dev/ttyACM0` file descriptor goes stale mid-boot. Plain `cat` (and
`console.sh`) therefore cannot capture a boot: they silently return nothing.
This wasted a lot of time looking like a "board is stuck / not booting" problem
when the board was fine.

Recipe that actually works:

1. Reset with esptool (it drives the USB-JTAG reset correctly):
   `./dev.sh esptool --chip esp32c6 --port /dev/ttyACM0 --before default_reset --after hard_reset flash_id`
2. A reconnecting reader that reopens the port each time it reappears:
   `while ...; do [ -r /dev/ttyACM0 ] && { stty ...; timeout 2 cat /dev/ttyACM0 >> log; }; done`
3. stty MUST include `clocal -crtscts`. Omitting `clocal` (ignore modem-control
   lines) caused several baffling empty captures even when the board was alive.

## [serial] [shell] [poke] 2026-06-20 — big apps look "dead" unless you poke the shell

Small app (our pump): startup `LOG_INF` lines print *after* USB enumerates, so a
capture catches them. Large app (WiFi sample): the banner prints *before* USB
enumerates, then the app sits silently at the shell prompt, so a passive capture
looks empty even though everything is fine. The aliveness test is to SEND a
command (`help`, `wifi status`) and read the reply, not to listen for
spontaneous output.

## [serial] [access] [dialout] [setfacl] 2026-06-20 — getting rw on /dev/ttyACM0

The node is `root:dialout rw-rw----` with no logind `uaccess` ACL on this host,
and the user was not in `dialout`, so flashing and console both failed with
EACCES. Two fixes:

- Immediate, no re-login: `sudo setfacl -m u:$USER:rw /dev/ttyACM0`. Survives
  flashing (esptool's auto-reset does not re-enumerate the *device*, only on a
  full reboot); lost on physical unplug.
- Permanent: `sudo usermod -aG dialout $USER`, but needs a logout/login AND a
  restart of the tooling/session so the new `podman` inherits the group.

Why both work with the container: `dev.sh` uses `--userns=keep-id` (maps your
uid in, so a per-uid ACL applies inside) and `--group-add keep-groups` (carries
supplementary groups in, so dialout applies inside).

## [workspace] [build-dir] [stale-cache] [hifive1] 2026-06-20 — build/ was a foreign leftover

The first sysbuild failed with `Not a file: /home/tubbles/dev/zephyr-hifive1/
.../pristine.cmake`. The pre-existing `build/` was carried over from the
`zephyr-hifive1` predecessor project: its `CMakeCache.txt` pointed at that other
repo's paths. This repo was created by copying zephyr-hifive1, so watch for
other carried-over artifacts. Fix was just `rm -rf build` (it is gitignored and
regenerated).

## [manifest] [modules] [dependencies] [mbedtls] [tf-psa-crypto] 2026-06-20 — each feature drags in west modules

Enabling a feature means adding its module(s) to the `west.yml` name-allowlist
and re-running `make update`. The chains found:

- MCUboot needs the `mcuboot` module (committed; sysbuild uses it).
- WiFi needs `mbedtls` AND `tf-psa-crypto`. The WiFi driver `select`s `MBEDTLS`
  + `PSA_CRYPTO`; recent mbedtls split TF-PSA-Crypto into its own repo, and
  mbedtls's `CMakeLists.txt` does `add_subdirectory(tf-psa-crypto)` which fails
  ("TF-PSA-Crypto target tfpsacrypto does not exist") until that module is also
  fetched.

Decision: `mbedtls` + `tf-psa-crypto` were added **transiently** for the WiFi
verification and NOT committed, to keep the committed manifest minimal (WiFi is
not in the committed app yet). They get committed when WiFi actually lands in
the app.

## [directxip] [mcuboot] [esp32c6] [false-alarm] 2026-06-20 — "DirectXIP unsupported" is a red herring

`bootloader/mcuboot/boot/espressif/.../mcuboot_config.h` literally says
DirectXIP is "CURRENTLY UNSUPPORTED!". Ignore it for Zephyr: that is the
standalone ESP-IDF-style port. Zephyr sysbuild builds the *generic* `boot/zephyr`
port (`share/sysbuild/images/bootloader/CMakeLists.txt` SOURCE_DIR `.../boot/
zephyr/`), which supports DirectXIP. Confirmed by building AND booting it: it is
not just a build-time accident.

## [directxip] [swap] [default] [no-copy] 2026-06-20 — ESP defaults to a COPYING swap

Turning on MCUboot alone gives `swap-using-move` on Espressif parts (the sysbuild
choice default for `SOC_FAMILY_ESPRESSIF_ESP32`). That is a copying swap, the
opposite of what we wanted. DirectXIP (no copy; fits the C6 because it runs XIP
from flash through the MMU, so "switching slots" is an MMU remap) must be
selected explicitly. Runtime behavior verified: boots the higher-version slot;
invalidating slot 1 falls back to slot 0 (proving slot 0 is never touched).

## [flash] [slots] [offsets] [staging] 2026-06-20 — flashing individual slots

Offsets in use (4 MB layout): `mcuboot` @ `0x0`, `slot0` (image-0) @ `0x20000`,
`slot1` (image-1) @ `0x1e0000`. Note slot1 sits at a *higher* address than slot0.

- `west flash` under sysbuild flashes ALL three images (mcuboot + slot0 + slot1
  variant) as three separate esptool runs, each with its own reset. A `tail` of
  the output only shows the last one; read the full log to see all.
- Stage an update into slot 1 only (no touch to slot 0 / mcuboot):
  `esptool write_flash 0x1e0000 build/app_slot1_variant/zephyr/zephyr.signed.bin`
- Invalidate slot 1 (force fallback to slot 0):
  `esptool erase_region 0x1e0000 0x10000`

## [wifi] [#82874] [verified] 2026-06-20 — WiFi works with usb_serial + queue size 16

#82874 claimed WiFi on the C6 was broken while `usb_serial` was enabled and with
`CONFIG_NET_MGMT_EVENT_QUEUE_SIZE=16`. On this board at v4.4.1, the unmodified
upstream `samples/net/wifi/shell` (console on `usb_serial`, queue size 16):
`wifi status` reports the driver up (State: INACTIVE) and `wifi scan` returns
real 2.4 GHz APs. So the core of #82874 is refuted / fixed for v4.4.1.

The `wifi connect` association + DHCP path (where #82874's "stuck at SCANNING"
lived) is now verified too, and so is #86258; see the two sections directly
below. Both bug reports are closed on hardware.

## [wifi] [#82874] [connect] [dhcp] [verified] 2026-06-20 — full association + DHCP works

The connect path #82874 called broken works end to end on this board at v4.4.1,
console on `usb_serial`, `NET_MGMT_EVENT_QUEUE_SIZE=16`. After a cold reboot the
link comes up from a credential persisted in flash (`wifi cred auto_connect`):
`wifi status` reaches `State: COMPLETED` (WPA2-PSK, 2.4 GHz, WIFI 6 / 802.11ax),
`net_dhcpv4` auto-leases an address, and `net ping` to the gateway returns 3/3.
No "stuck at SCANNING". The DHCP exchange itself is a full DISCOVER/OFFER/REQUEST/
ACK round-trip, so it proves real bidirectional traffic, not just a lease.

## [wifi] [#86258] [watchdog] [verified] 2026-06-20 — CONFIG_WATCHDOG does not break WiFi

#86258 claimed `CONFIG_WATCHDOG=y` broke WiFi (the board enables `wdt0` by
default). Rebuilt the wifi shell sample with `CONFIG_WATCHDOG=y` (which pulls in
`CONFIG_WDT_ESP32=y` and binds `wdt0`). `device list` shows `watchdog@60008048
(READY)` and `wifi (READY)` at the same time, and the link still associates and
gets a DHCP lease. So #86258 does not reproduce on v4.4.1.

## [wifi] [credentials] [persistence] [security] 2026-06-20 — storing WiFi creds, and the plaintext gotcha

`CONFIG_WIFI_CREDENTIALS` persists networks so you connect once and reconnect
without re-typing. Findings on the C6:

- Backend: only `WIFI_CREDENTIALS_BACKEND_SETTINGS` (settings/NVS in the
  `storage` partition @ 0x3b0000) is usable here. The `BACKEND_PSA` protected-
  storage option `depends on BUILD_WITH_TFM`, and TF-M is ARM TrustZone only, so
  it is unavailable on this RISC-V part.
- `WIFI_CREDENTIALS_MAX_ENTRIES` defaults to 2 (multiple networks supported,
  bump as needed). `wifi cred add` stores; `wifi cred auto_connect` connects
  from flash with no passphrase at the prompt.
- The credential survives both a reset AND a reflash: `west flash` only writes
  the app region (0x0..~0xc0000), leaving the `storage` partition intact, so a
  rebuilt image still auto-connects.
- SECURITY GOTCHA: `wifi cred list` prints the stored passphrase in PLAINTEXT.
  There is no at-rest protection on this build (the settings/NVS copy is
  plaintext flash, readable via `esptool read_flash`); real at-rest encryption
  would need ESP flash encryption (eFuse), deliberately avoided. Workflow used:
  the user types the credential into the console themselves so the password never
  enters the assistant's context, and the assistant never runs `wifi cred list`.

## [wifi] [2.4ghz] [footprint] 2026-06-20 — WiFi facts

- The C6 is 2.4 GHz only (WiFi 6 on 2.4 GHz). 5 GHz-only networks never appear
  in a scan.
- The WiFi shell image is ~764 KB vs our app's ~146 KB; it pulls in mbedtls +
  tf-psa-crypto + the full IP/TLS stack.
- Verified separately from our app (the isolated upstream sample), built as
  simple-boot. Kept apart from the OTA app on purpose.

## [security] [signing] [efuse] [irreversible] 2026-06-20 — nothing irreversible was done

Signing is deliberately left at `BOOT_SIGNATURE_TYPE_NONE` (header + SHA-256, no
crypto signature). No eFuse work (Secure Boot v2, flash encryption) was touched:
those burn one-time fuses and can permanently brick the board. When that work
starts, `CONFIG_ESP32_EFUSE_VIRTUAL` can rehearse the whole Secure Boot / flash
encryption flow in emulated fuses (stored in the `sys` partition) with zero
permanent changes first.

## [hardware] [chip] [esptool] 2026-06-20 — board and tool facts

esptool reported the part as ESP32-C6FH4 (QFN32) rev v0.2, 4 MB embedded flash,
single core + LP core, 160 MHz, USB-Serial/JTAG. esptool v5.3.0 in the image;
its reset is "via RTS pin".

## [decisions] 2026-06-20 — key decisions and why

- DirectXIP (no-copy A/B) over swap modes: fits the C6's XIP-from-flash MMU; no
  per-boot copy or scratch wear.
- Version banner via `app/VERSION`: makes the running image observable in the
  console AND drives the imgtool sign version that DirectXIP selects on, one file
  feeds both.
- A/B differentiators for the demo: LED rate (for the eye, no console needed) +
  the version banner (for the log). The LED/version bump was transient demo
  scaffolding, reverted; only the foundation + banner are committed.
- WiFi verified via the upstream sample, transient mbedtls/tf-psa-crypto, not by
  enabling WiFi in the app: keeps the verification isolated and the manifest
  minimal.
- Password hygiene for the connect test: the user enters the WiFi credential at
  the console themselves and it persists in flash; the assistant drives every
  test via `wifi cred auto_connect` and never runs `wifi cred list` (plaintext).
- Commit directly to `main`, no feature branches (user preference for this repo).
