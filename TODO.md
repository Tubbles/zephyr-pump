# To Do

## Verify the ESP32-C6 WiFi bug reports on hardware

Requested alongside the A/B proof of concept ("verify the claims and bug
reports"). The DirectXIP claims were verified on hardware (the v1.0.0/v2.0.0
A/B demo: DirectXIP boots the higher-version slot with no copy, and falls back
to slot 0 when slot 1 is invalidated). The WiFi-specific bug reports were not,
since they need a WiFi build, a flash, and runtime observation. Worth
confirming on this exact board at v4.4.1 (issue numbers are pointers from the
earlier research, verify them too):

- zephyrproject-rtos/zephyr#82874: WiFi reportedly fails while `usb_serial` is
  enabled, and `wifi connect` stuck at SCANNING until
  `CONFIG_NET_MGMT_EVENT_QUEUE_SIZE` was raised from 16 to 24+; also WiFi
  breaks when `uart1` is enabled. The XIAO console is on `usb_serial`, so this
  conflict is the one that matters here. Reported fixed before v4.4.1.
- zephyrproject-rtos/zephyr#86258: `CONFIG_WATCHDOG=y` reportedly broke WiFi;
  the board enables `wdt0` by default. Reported fixed before v4.4.1.

Test: enable WiFi STA plus a socket, build, flash, and confirm an association
and DHCP lease on this board with the console still on `usb_serial`.
