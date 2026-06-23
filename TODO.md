# To Do

## OTA hardening (before trusting it in the field)

OTA self-update works end to end (docs/LOG.md [ota]), but is unauthenticated by
design for now:

- Enable Ed25519 image signing (BOOT_SIGNATURE_TYPE_NONE today) so MCUboot
  rejects unsigned/tampered images.
- Then turn TLS peer verification back on (currently TLS_PEER_VERIFY_NONE): pin a
  CA or the GitHub Pages leaf. See docs/SUGGESTIONS.md.
- Consider DIRECT_XIP_WITH_REVERT + in-app confirm so a bad update auto-rolls-back.
- Not yet exercised: the reverse OTA direction (writing slot0 while running from
  slot1); same code path, worth one confirming cycle.

## User written inbox
