# To Do

## User written inbox

Have a look at https://www.sigmdel.ca/michel/ha/xiao/xiao_esp32c6_intro_en.html#antenna_1
specifically point 8.

## Done

WiFi bug-report verification completed on hardware at v4.4.1: zephyr#82874
(connect + DHCP with the `usb_serial` console at `NET_MGMT_EVENT_QUEUE_SIZE=16`)
and zephyr#86258 (`CONFIG_WATCHDOG=y` vs WiFi) both work and do not reproduce.
Details in docs/LOG.md. Follow-on ideas (in-app WiFi + wireless OTA, Ed25519
signing) live in docs/SUGGESTIONS.md.
