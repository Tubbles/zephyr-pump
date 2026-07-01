# To Do

## OTA hardening (before trusting it in the field)

OTA self-update works end to end (docs/LOG.md [ota]), but is unauthenticated by
design for now:

- Enable Ed25519 image signing (BOOT_SIGNATURE_TYPE_NONE today): the primary,
  endpoint-independent control. Trust is in your signature, not the host, so it
  holds wherever update url points.
- Transport auth is secondary and endpoint-specific (update url is configurable,
  so no baked-in CA): if wanted, provision a pinned server public key with the
  URL. See docs/SUGGESTIONS.md.
- Consider DIRECT_XIP_WITH_REVERT + in-app confirm so a bad update auto-rolls-back.

## User written inbox
