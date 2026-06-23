# OTA updates

The board updates itself by pulling a prebuilt, signed image from a static
endpoint and writing it into the spare flash slot. There is no build on the
device (an MCU cannot compile Zephyr); GitHub Actions builds the image and
publishes it to GitHub Pages, and the board downloads it on command.

## Flow

1. CI (`.github/workflows/firmware.yml`) builds both DirectXIP slot variants on
   every push to `main`, stamps a monotonic version, and publishes to Pages:
   - `…/version` plain text, e.g. `1.0.42`
   - `…/firmware-slot0.bin`, `…/firmware-slot1.bin` the signed images
2. On the board, the `update` shell command resolves the Pages host (DNS over
   the DHCP-provided server), opens a TLS socket, GETs the variant for its
   **inactive** slot, and streams it into that slot via `flash_img`.
3. It reboots. MCUboot (DirectXIP) boots whichever slot holds the higher image
   version, which is the freshly written one.

## Why two variants, and why a version bump is mandatory

Under DirectXIP an image only runs from the slot it was built for (its IROM/DROM
flash offsets are baked into the header, see docs/LOG.md `[directxip]`), so CI
publishes one binary per slot and the board fetches the one matching the slot it
is **not** running from. The board knows its own slot from its compiled-in
`CONFIG_FLASH_LOAD_OFFSET`.

DirectXIP boots the slot with the higher version, so a published build must
out-version the running one or the board will stage it but never switch. CI
stamps `app/VERSION`'s `PATCHLEVEL` from the workflow run number (build-time
only, not committed), giving a monotonic `1.0.<run>`.

## Shell commands

```
update url [<url>]   # show or set the base URL (persisted in settings/NVS)
update check         # print running vs published version (no flash)
update now           # download the inactive-slot image, flash it, reboot
update status        # running slot, version, target variant, base URL
```

The base URL defaults to `CONFIG_APP_UPDATE_URL`
(`https://tubbles.github.io/zephyr-pump`) and persists in the `update/url`
settings key, so it survives a reboot/reflash and can be repointed (e.g. at a
LAN HTTP server for testing) without rebuilding.

## Security posture (v1: minimal, by choice)

This first cut trades authenticity for getting the pipeline working, matching the
unsigned-image decision:

- **TLS is encrypt-only.** Peer certificate verification is OFF
  (`TLS_PEER_VERIFY_NONE`), so no CA is embedded (and none can rotate out from
  under a headless board). The channel is encrypted but the server is not
  authenticated.
- **The image is unsigned** (`BOOT_SIGNATURE_TYPE_NONE`): MCUboot checks only a
  SHA-256 integrity hash, not a signature.

Net trust model: anyone who can serve that URL (or MITM it) can install firmware.
Acceptable here because the binaries are public and the device controls nothing
dangerous, but the hardening path is real and tracked: enable Ed25519 image
signing (so MCUboot rejects unsigned/tampered images) and then turn TLS peer
verification back on (pin a CA or the GitHub Pages leaf). See
docs/SUGGESTIONS.md.

A corrupt or truncated download cannot brick the board: MCUboot validates the
image hash before boot and falls back to the other slot if it fails (verified,
docs/LOG.md `[directxip]`). The `update` command also refuses to write anything
but an HTTP 200 body, so a 404 page never lands in a slot.

## Config

Device side lives in `app/src/update.c`, gated by `CONFIG_APP_UPDATE`. The
networking/TLS/flash Kconfig is in `app/prj.conf` (HTTP client, TLS sockets with
the ECDHE+AES-GCM ciphersuites GitHub/Fastly negotiate, `IMG_MANAGER` +
`STREAM_FLASH` for the slot write). DNS is the resolver added earlier
(docs/LOG.md `[dns]`).
