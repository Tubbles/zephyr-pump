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

## [serial] [console] [interactive] [miniterm] 2026-06-20 — typing into the shell needs a RW terminal

`console.sh` is read-only (just `stty` + `cat`), fine for monitoring but you
cannot type into it. To send shell commands interactively (e.g. `wifi cred add`,
which we deliberately ran by hand so the passphrase stayed off-host-tooling), use
an interactive terminal. On this host only `pyserial-miniterm` is installed:
`pyserial-miniterm /dev/ttyACM0 115200`, quit with Ctrl-]. The Zephyr shell
echoes typed characters back over the wire, so keep local echo off (miniterm's
default) to avoid doubled input. Only one reader can own the CDC-ACM node at a
time, so quit miniterm before any host-side capture/poke, and vice versa.

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
  simple-boot. Kept apart from the OTA app on purpose (until it landed; see the
  "WiFi landed in the app proper" entry below).

## [wifi] [app] [directxip] [autoconnect] [verified] 2026-06-21 — WiFi landed in the app proper

WiFi is now built into the committed app, not a separate sample. What it took and
what it verified:

- Config (`app/prj.conf`): `CONFIG_WIFI` plus the networking/IPv4/DHCP/TCP set
  from the verified `samples/net/wifi/shell`, and `CONFIG_WIFI_CREDENTIALS` with
  the SETTINGS/NVS backend so a credential provisioned once over the shell
  persists and the board reconnects unattended. `CONFIG_ESP32_WIFI_STA_AUTO_DHCPV4=y`
  starts DHCP on association. Gated behind `CONFIG_APP_WIFI`.
- Manifest: `mbedtls` + `tf-psa-crypto` are now COMMITTED in `west.yml` (they
  were transient before). The WiFi driver selects MBEDTLS + PSA_CRYPTO; without
  both modules the build dies at mbedtls's `add_subdirectory(tf-psa-crypto)`.
- Entropy: dropped the sample's `CONFIG_TEST_RANDOM_GENERATOR` (a portability
  fallback for boards with no hardware RNG). `CONFIG_NETWORKING` selects
  `ENTROPY_GENERATOR`, and the board enables `trng0`, so `ENTROPY_ESP32_RNG` is
  default-y and the real hardware TRNG feeds the crypto. Confirmed `trng0
  (READY)` in `device list`.
- Auto-connect (`app/src/wifi.c`): a SYS_INIT registers a WiFi event handler and
  schedules a delayable work item that issues `NET_REQUEST_WIFI_CONNECT_STORED`,
  retrying every 500 ms (up to ~10 s) until the interface and supplicant are
  ready. Nothing else in the build triggers a boot-time association, so a
  post-boot `State: COMPLETED` is attributable to this code.

On-hardware verification of the COMBINED image. This is new: the earlier wifi
sections verified the standalone sample built as simple-boot, whereas this is
WiFi + MCUboot DirectXIP together. After `west flash` + reset, with the
credential already in flash from the earlier session (it survives a reflash):

- `device list`: `wifi (READY)` and `trng0 (READY)` alongside `ledc0` / `leds`,
  so WiFi coexists with the motor/speaker/LED peripherals.
- `wifi status`: `State: COMPLETED`, WPA2-PSK, WIFI 6 (802.11ax), 2.4 GHz, RSSI
  -42 (the powered RF switch, matching the antenna [rssi] figures).
- `net iface`: `DHCPv4 state: bound`, leased `192.168.1.27`, gateway
  192.168.1.1, so the full path (association + DHCP) runs from boot with no
  console interaction. `wifi scan` returned 15 APs.

Footprint: the app grew from ~146 KB to ~770 KB signed, well inside the 1792 KB
slot. Pristine build is clean, no warnings.

Provisioning hygiene unchanged: the user types `wifi cred add` at the console so
the passphrase stays out of tooling, and the assistant never runs `wifi cred
list` (plaintext).

## [mdns] [hostname] [dhcp] [fqdn] [verified] 2026-06-21 — name resolution: mDNS, and the router registers the DHCP hostname

The board is headless, so it needs a *name*, not just the DHCP-leased IP. Added
mDNS, and in the process found the router resolves it over plain DNS too.

mDNS (`app/prj.conf`): `CONFIG_MDNS_RESPONDER=y` plus `CONFIG_NET_HOSTNAME_ENABLE=y`
+ `CONFIG_NET_HOSTNAME="pump"` (the responder *depends on* a hostname and answers
`<hostname>.local`). It auto-starts via SYS_INIT, so no app code. It pulls in
IGMP + the sockets service, ~67 KB (app ~770 -> ~835 KB signed, still well
inside the 1792 KB slot). Skipped `MDNS_RESPONDER_PROBE` (experimental, drags in
the connection manager + resolvers) and `NET_HOSTNAME_UNIQUE` (one board, plain
`pump.local` reads better than a MAC-suffixed name).

Verified on hardware from a host running avahi: `getent hosts pump.local` ->
`192.168.1.27`, `ping pump.local` 2/2. mDNS is peer-to-peer multicast, so this
works with no router cooperation.

Bonus finding: setting a real hostname also makes the DHCP client advertise it
(option 12), and on this network the router now registers the board in its own
DNS: `pump.localdomain` -> 192.168.1.27 (forward AND reverse PTR), and bare
`pump` resolves via the `localdomain` search suffix. So there are now three ways
to reach it: `pump.local` (mDNS), `pump` / `pump.localdomain` (router DNS), and
the raw IP. (Earlier, with the default hostname `zephyr`, the router showed
NXDOMAIN; between the checks the board also re-did DHCP from a fresh boot, so the
cause, a generic-name filter vs a stale lease, is unconfirmed and now moot.)

[fqdn] No DHCP Client FQDN (option 81, RFC 4702) is involved. Zephyr v4.4.1's
DHCPv4 client has no FQDN support at all: the option table in
`subsys/net/lib/dhcpv4/dhcpv4_internal.h` has HOST_NAME=12 and DOMAIN_NAME=15
but no 81, and nothing under `subsys/net/` references it. The router registered
the board from the plain Host Name option (12) alone, so option 81 was not
needed here. Sending option 81 would mean patching the client; it is not a
Kconfig switch.

Refinement (same day): the hostname now carries the last two MAC bytes as hex so
multiple boards get distinct names. `CONFIG_NET_HOSTNAME_DYNAMIC=y` plus a small
`apply_hostname()` in `app/src/wifi.c` (set once, when the interface MAC is up,
before associating) turns the base `pump` into e.g. `pump6820` for MAC ...68:20.
The base prefix stays `CONFIG_NET_HOSTNAME`; the code reads the wlan0 link
address and appends `addr[len-2]`/`addr[len-1]`. Note the raw bytes are 0x68
0x20 and 0x20 is a space (invalid in a hostname), so they are rendered as hex,
the same scheme as Zephyr's `NET_HOSTNAME_UNIQUE` (which appends all six bytes;
we wanted only two, hence the manual set rather than that option). Verified after
reflash: `pump6820.local` (mDNS) and, once the router re-registered,
`pump6820` / `pump6820.localdomain` plus reverse PTR all track the new value,
and the old `pump` record aged out.

## [antenna] [wifi] [rf-switch] [gpio3] [gpio14] 2026-06-21 — the RF switch must be POWERED, even for the onboard antenna

The point of the sigmdel intro (point 8) is the opposite of "leave it alone":
the RF switch must be **powered** (GPIO3 low) for good RF on EITHER antenna, not
just the external one. The XIAO ESP32-C6 has an onboard ceramic antenna plus a
U.FL connector, both routed through an RF-switch IC on GPIO3 (power) and GPIO14
(select). With the switch unpowered the radio still works on a leakage path, so
it *looks* fine, which is the trap. The article, verbatim:

> "Even when it is not powered, there is enough leakage through the RF switch for
> the wireless signal to get to the microcontroller so that Wi-Fi, Bluetooth and
> Zigbee communication is possible if not optimal."
> "Be aware that the performance of the antenna will be degraded as a consequence."

Correction to an earlier conclusion in this session: our verified WiFi (the
sections above) ran with the switch UNPOWERED, so it was the degraded leakage
path, "possible if not optimal", not the onboard antenna at full performance.
"WiFi associates and pings" did not prove the antenna was driven properly. This
is exactly the confusion the article calls out.

Why unpowered by default in Zephyr:

- The board DTS carries an `rf_switch` node (`xiao_esp32c6_hpcore.dts`):
  `enable-gpios = <&gpio0 3 GPIO_ACTIVE_LOW>` (GPIO3 low powers the switch),
  `select-gpios = <&gpio0 14 GPIO_ACTIVE_HIGH>` (GPIO14 high = external). The
  hook in `board.c` ALWAYS powers the switch when it runs; the `#ifdef` only
  picks the antenna.
- But the hook only runs when `CONFIG_XIAO_ESP32C6_EXT_ANTENNA=y`: that symbol is
  the only thing that `select`s `BOARD_LATE_INIT_HOOK`, and the board
  `CMakeLists.txt` only compiles `board.c` when it is set. Default build → hook
  never runs → GPIO3/GPIO14 at reset state → switch unpowered → degraded onboard.

The upstream gap (the real problem, not a curiosity): because the CMake gate and
the Kconfig `select` hang off the same symbol, the `#else` (onboard) branch in
`board.c` can never compile. So there is NO upstream config for "switch powered +
onboard antenna": the only way to power the switch is `EXT_ANTENNA=y`, which also
forces the external U.FL antenna. To run the onboard antenna at full performance
you must drive the pins yourself: GPIO3 active (power) + GPIO14 inactive
(onboard), reusing the board's `rf_switch` DT node.

Implemented in `app/src/antenna.c` (`CONFIG_APP_ANTENNA`, on in `prj.conf`): a
SYS_INIT at APPLICATION level configures `enable_gpios` (GPIO3) OUTPUT_ACTIVE
(active-low, so driven low = switch powered) and `select_gpios` (GPIO14)
OUTPUT_INACTIVE (active-high, so driven low = onboard antenna). It runs at every
boot regardless of whether the radio is used; the cost is two GPIO writes plus
the switch IC's quiescent current, so there is no reason to defer it until WiFi
lands. Done eagerly so the antenna is always correct once the radio is enabled.

Separately, the ~6% RSSI figure in the article is the external-rod-vs-onboard
delta with the switch powered in both cases. That is a smaller, different axis
than powered-vs-unpowered, and not our concern (we use the onboard antenna).

## [antenna] [wifi] [rssi] [measured] 2026-06-21 — powering the switch is worth ~18 dB, not 6%

Measured the actual RSSI gain on hardware, and it is large: powering the RF
switch lifts the onboard antenna by roughly **17 to 20 dB** on reliably-seen APs
(up to ~23 dB on weak ones). That dwarfs the article's ~6% internal-vs-external
figure: the powered-vs-unpowered axis is the one that matters, not the antenna
choice. An unpowered RF switch sits in its high-isolation off-state, so only
leakage gets through, which is exactly that much loss.

Method (a clean within-boot A/B/C at a fixed position, no credentials needed):
built the upstream `samples/net/wifi/shell` with `CONFIG_GPIO_SHELL=y`, then over
the console drove the antenna pins live between batches of `wifi scan` (12 scans
per phase, RSSI averaged per BSSID so per-beacon noise washes out and each AP is
its own control):

- A: pins untouched (switch unpowered, the real default)
- B: `gpio set gpio0 3 0` + `gpio set gpio0 14 0` (switch powered + onboard,
  i.e. exactly what `antenna.c` does)
- C: `gpio set gpio0 3 1` (switch unpowered again, onboard select held)

Mean RSSI (dBm), same boot, same spot:

| AP (BSSID)              | A unpwr | B powered | C unpwr | B-A   |
| ----------------------- | ------- | --------- | ------- | ----- |
| Monkey_Mesh (..2B:8E:0A)| -63.2   | -43.0     | -63.1   | +20.2 |
| Blizzards   (..24:A7:76)| -69.0   | -50.2     | -69.1   | +18.8 |
| OWNIT       (..DF:3A:A3)| -76.4   | -59.2     | -77.7   | +17.2 |
| Kara_24GHz  (..64:46:E1)| -95.4   | -72.5     | -95.1   | +22.9 |
| Monkey_Mesh (..2C:0D:12)| -92.3   | -75.3     | -92.3   | +17.0 |

The C column is the proof: driving GPIO3 back high returns every AP to within
~1 dB of its A baseline, so the swing is the switch power and not drift or someone
walking past. With the switch powered, ~15 extra weak APs (-88 to -96 dBm) rose
above the detection floor that were invisible unpowered: independent
corroboration of the gain.

On-hardware confirmation of the app path: after reflashing the pump app, the boot
log shows `<inf> antenna: RF switch powered, onboard antenna selected` ahead of
main's banner, with no error, so `antenna_init` (SYS_INIT APPLICATION) runs and
the GPIO configures succeed. Driving GPIO3/GPIO14 does not disturb the USB-JTAG
console (those are not USB or strapping pins).

Repro gotcha for next time: the measurement scripts live in `tmp/` (not
committed). The wifi sample was flashed transiently; the board was reflashed back
to the pump app afterward. (Update: WiFi has since landed in the app and the
`mbedtls` + `tf-psa-crypto` manifest entries are now committed, no longer
transient. See the "WiFi landed in the app proper" entry above.)

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
