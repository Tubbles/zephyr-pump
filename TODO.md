# To Do

## Power the RF switch when WiFi lands in the app

The XIAO ESP32-C6 RF switch is unpowered in a default Zephyr build, so the
onboard antenna runs on a degraded leakage path (see the `[antenna]` entry in
docs/LOG.md). The upstream board only powers the switch via
`CONFIG_XIAO_ESP32C6_EXT_ANTENNA=y`, which also forces the external U.FL
antenna, so that knob is not what we want. When WiFi is enabled in the app, add
an app-side init (gated by its own Kconfig) that reuses the `rf_switch`
devicetree node to drive GPIO3 active (power the switch) and GPIO14 inactive
(keep the onboard antenna). No-op until the radio is actually used.

## User written inbox
