# XIAO ESP32-C6 Pump Controller — Build Plan

## What this is

`zephyr-pump` drives a pump from a Seeed XIAO ESP32-C6. The motion layer is an
A4988 stepper driver (a peristaltic pump head is turned by a stepper), exposed
today as the ported `motor` shell command group; a LEDC-PWM speaker tone rides
along as a simple status beeper. The board, driver, motor and power are loose
components on the bench; the pin assignment below reflects the chosen wiring (see
"Pin map").

## Hardware

| Item      | Part                            | Key facts                                                                                                  |
| --------- | ------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| MCU board | Seeed XIAO ESP32-C6 (ESP32-C6)  | RISC-V, 3.3 V I/O, WiFi 6 + BLE + 802.15.4, **has a SAR ADC**, hardware I²C/SPI/UART, LEDC PWM on any GPIO |
| Driver    | A4988 module (Hailege / Rungee) | Chopper current driver, VMOT 8–35 V, ~1 A bare / ~1.5 A with heatsink                                      |
| Motor     | 42SHD0217-24B                   | NEMA 17 bipolar, **1.5 A/phase**, 2.2 Ω, ~3.3 V rated                                                      |
| Power     | 3S LiPo                         | ~9–12.6 V, sits inside the A4988's VMOT range                                                              |

## What the ESP32-C6 changes vs the old FE310 design

The previous target (SiFive HiFive1 / FE310) shaped much of the original plan.
The XIAO ESP32-C6 lifts most of those constraints:

- **It has an ADC.** The FE310 had none, which forced every analog reading (pack
  voltage, temperature) onto an external I²C device. On the C6 the SAR ADC can
  read a divided battery voltage directly, so the external ADS1115 is now
  optional. I²C temperature sensors are still the easy path for temperature.
- **PWM is unrestricted.** The FE310 PWM had an 8-bit instance, a "channel 0 is
  just the period" caveat, and pins with no PWM mux at all (the speaker had to be
  a software square wave). The C6's LEDC routes a PWM channel to any GPIO, so both
  STEP and the speaker are plain hardware PWM (two LEDC channels on separate
  timers).
- **It is wireless.** WiFi/BLE/Thread/Zigbee are on-chip, so remote pump control
  or telemetry is possible later with no extra hardware.
- **Different form factor.** The XIAO is a 14-pin module, not an Arduino-shield
  host, so the EDU Shield and its Arduino-header pin map are gone. Wiring goes
  directly to the XIAO header pads.

## Pin map (XIAO ESP32-C6)

The XIAO exposes a standard 14-pin header, D0–D10 plus power. Zephyr's board maps
them to GPIOs as below; the default board devicetree already claims some for
I²C0, SPI2 and UART0:

| XIAO pin | GPIO   | Default peripheral | ADC? | In this project           |
| -------- | ------ | ------------------ | ---- | ------------------------- |
| D0       | GPIO0  | free               | yes  | free (ADC: battery sense) |
| D1       | GPIO1  | free               | yes  | free (ADC)                |
| D2       | GPIO2  | free               | yes  | free (ADC)                |
| D3       | GPIO21 | free               | no   | **speaker** (LEDC ch1)    |
| D4       | GPIO22 | I²C0 SDA           | no   | free for sensors          |
| D5       | GPIO23 | I²C0 SCL           | no   | free for sensors          |
| D6       | GPIO16 | UART0 TX           | no   | free (spare UART)         |
| D7       | GPIO17 | UART0 RX           | no   | free (spare UART)         |
| D8       | GPIO19 | SPI2 SCLK          | no   | **EN** (active-low)       |
| D9       | GPIO20 | SPI2 MISO          | no   | **DIR**                   |
| D10      | GPIO18 | SPI2 MOSI          | no   | **STEP** (LEDC ch0)       |

The control lines repurpose the SPI2 pins (D8–D10), which the pump does not use,
plus D3 for the speaker. That keeps the only ADC-capable header pins (D0–D2) free
for battery sensing and I²C0 (D4/D5) free for sensors. None of the D0–D10 header
pins is an ESP32-C6 strapping, flash or USB pin (those are GPIO4/5/8/9/15,
GPIO24–30 and GPIO12/13, all broken out off-header), so the choice is
unconstrained by boot safety. The overlay disables `spi2` to release D8–D10;
re-pin by editing `app/app.overlay`, nothing in `app/src` is pin-specific.

Console and shell run over the USB-Serial/JTAG port (the USB-C connector), so the
header UART0 (D6/D7) is a spare UART, not the console.

## Stepper driver wiring (A4988)

The driver, motor and power wiring are independent of the MCU and carry over from
the original plan.

### Control lines: XIAO → A4988

| A4988 pin | XIAO pin     | Role         | Notes                                          |
| --------- | ------------ | ------------ | ---------------------------------------------- |
| **STEP**  | D10 / GPIO18 | LEDC ch0     | Hardware PWM step train, smooth and CPU-free   |
| **DIR**   | D9 / GPIO20  | Plain output | Just a level, no timing                        |
| **EN**    | D8 / GPIO19  | Plain output | Active-low; add a pull-up so it boots disabled |

**STEP via PWM:** the step rate is the PWM frequency; the driver emits a 50 %
duty square wave (the A4988 needs only a ~1 µs minimum high time). LEDC covers a
wide frequency range with fine resolution, so no rescaling tricks are needed.

**EN:** kept because you are on battery. Driving EN inactive between moves cuts
holding current and heat. The motor still runs fine if EN is just grounded, but
you lose that power saving.

### A4988 module — required support connections

| A4988 pin         | Connect to        | Notes                                                       |
| ----------------- | ----------------- | ----------------------------------------------------------- |
| VMOT              | 3S LiPo +         | **100 µF** electrolytic across VMOT/GND, close to the board |
| GND (motor)       | LiPo −            | Common ground with logic                                    |
| VDD               | XIAO 3V3          | Logic supply                                                |
| GND (logic)       | XIAO GND          |                                                             |
| **RESET + SLEEP** | tie together      | **Mandatory**, bridge these or the driver won't output      |
| MS1 / MS2 / MS3   | set microstepping | Open = full step; pull high per table for 1/2…1/16          |
| STEP / DIR / EN   | see table above   | 3.3 V logic is accepted by the A4988                        |

> Classic gotcha: if the motor hums but won't move, you forgot the **RESET↔SLEEP** bridge.

### A4988 → Motor (42SHD0217-24B)

Bipolar, 4 wires = two coils. Wire one coil to **1A/1B** and the other to **2A/2B**.

- Identify coil pairs: the two wires of one coil read continuity / low resistance
  (~2.2 Ω) to each other and open to the other pair. Verify with a meter, don't
  assume.
- If the motor runs rough or vibrates without turning, one coil pair is split
  across the two channels, so swap one wire pair.
- Direction wrong? Flip DIR in software or swap one coil's two wires.

### Current limit (do this before first run)

- Target **≤ 1.5 A/phase**. Set via the trim pot → Vref.
- `Vref = I_max × 8 × R_sense`. **Check R_sense on your module** (often 0.1 Ω or
  0.05 Ω), it changes the math. Start low and raise it.
- Add the **heatsink** if pushing toward 1.5 A; airflow helps.

## Power notes

- 3S LiPo on VMOT is ideal; current limiting holds coil current constant as the
  pack sags.
- **Never** connect or disconnect the motor while VMOT is live, it can kill the
  A4988.
- Ignore the motor's "3.3 V" rating for supply purposes. That is only the DC coil
  voltage used in the current calc; the chopper handles the rest.

## Battery + temperature monitoring (optional)

The C6's ADC removes the hard dependency on an external ADC:

- **Voltage:** a resistor divider from the pack into an ADC-capable header pin
  (D0–D2 are ADC1 channels) reads pack voltage directly. Size the divider so the
  full-charge voltage (up to 12.6 V) lands inside the ADC's input range. An I²C
  ADS1115 is still an option if you want a dedicated, higher-resolution channel.
- **Temperature:** an I²C digital sensor (TMP102 / TMP117) on D4/D5. No analog
  thermistor needed.

> LiPo care: add a low-voltage alarm/cutoff at ~3.0–3.2 V per cell. Never run the pack flat.

## Software reality check

Use **Zephyr**: LEDC PWM for STEP, GPIO for DIR/EN, the ADC and I²C subsystems
for sensors. STEP is hardware PWM (LEDC channel 0), so the step train runs with
no CPU involvement. The XIAO ESP32-C6 is also Arduino-core supported if you ever
want that, but this project is Zephyr.

## Motor control (implemented)

The STEP/DIR/EN peripherals are wired up in `app/app.overlay` and driven from
`app/src/motor.c`, gated by `CONFIG_APP_MOTOR`. The overlay:

- brings up `ledc0` with two channels (channel 0 on GPIO18 for STEP, channel 1
  on GPIO21 for the speaker) on separate timers, via a `ledc0_default` pinctrl
  group;
- disables `spi2` to free its pins (GPIO18/19/20) for STEP/DIR/EN;
- carries the `zephyr,user` node with named PWMs (`pwm-names = "step", "speaker"`),
  STEP defaulting to 1 kHz with the rate set at runtime, plus `dir-gpios`
  (GPIO20) and `en-gpios` (GPIO19, `GPIO_ACTIVE_LOW` so logical 1 means "driver
  enabled").

At boot the driver parks EN inactive (motor free, no holding current) and STEP at
0 % duty (no pulses). It registers a `motor` shell command group:

```
motor enable             # EN active (energize, holding torque on)
motor disable            # EN inactive (coast)
motor dir <0|1>          # set the DIR line level
motor run <hz>           # continuous step train at <hz>
motor steps <count> <hz> # step <count> times at <hz>, then stop (~±1 step)
motor stop               # stop the step train
motor status             # last commanded enabled / dir / rate
```

STEP runs as a 50 % square wave (the A4988 needs only a ~1 µs minimum high time).
`run`/`steps` do not auto-enable the driver; they warn when EN is inactive since
the motor won't move. `steps` schedules the stop on the system workqueue, so the
pulse count is timer-bounded and accurate to about ±1 step.

## Speaker tone (LEDC PWM)

The speaker (D3 / GPIO21) is driven by a hardware LEDC PWM channel (channel 1):
the tone command sets the channel to a 50 % square wave at the requested
frequency and the peripheral generates it with no CPU work. A one-shot work item
silences the channel (0 % duty) after the requested duration. Lives in
`app/src/speaker.c`, gated by `CONFIG_APP_SPEAKER`. It registers a shell command:

```
tone <centi-hz> <ms>   # e.g. tone 44000 500  -> 440.00 Hz for half a second
```

Frequency is in centi-hertz (hundredths of a Hz) so the integer argument can
express sub-hertz precision. The pin gives the signal only: a piezo buzzer can
take it directly, but a low-impedance coil speaker draws more current than a GPIO
can source, so put a small transistor/MOSFET (or a tiny amp) between the pin and
a coil speaker.

## WiFi (implemented)

The on-chip 2.4 GHz radio runs in station mode and auto-connects at boot to a
network whose credential is stored in flash. Enabled by `CONFIG_APP_WIFI` plus
the WiFi/networking stack in `app/prj.conf`; the connect logic lives in
`app/src/wifi.c`.

- **Provision once, reconnect forever.** Add a network over the shell:
  `wifi cred add -s <ssid> -k 1 -p <passphrase>` (key type 1 = WPA2-PSK). It
  persists in the `storage` flash partition (settings/NVS backend), survives a
  reset and a reflash (`west flash` leaves `storage` intact), and the board
  rejoins it on every boot with no console interaction.
- **Auto-connect.** At boot `wifi.c` issues `NET_REQUEST_WIFI_CONNECT_STORED`,
  retrying until the interface and supplicant are ready; association and DHCP
  then run asynchronously. The `wifi` shell group stays available for manual
  control (`wifi status`, `wifi scan`, `wifi connect`, ...).
- **Antenna.** WiFi rides on the powered RF switch from `app/src/antenna.c`;
  without it the radio runs ~17-20 dB down (see docs/LOG.md [antenna]).
- **Footprint.** ~770 KB signed (vs ~146 KB without WiFi), inside the 1792 KB
  slot. Pulls in mbedtls + tf-psa-crypto (now committed in `west.yml`).

> Security: there is no at-rest encryption of the stored credential, and
> `wifi cred list` prints the passphrase in plaintext. Provision it yourself at
> the console; do not paste it into tooling.

## Quick pin summary

```
STEP    -> D10 / GPIO18 (LEDC ch0, hardware PWM)
DIR     -> D9  / GPIO20 (plain GPIO)
EN      -> D8  / GPIO19 (plain GPIO, active-low; pull up so it boots disabled)
speaker -> D3  / GPIO21 (LEDC ch1, hardware PWM)
ADC     -> D0/D1/D2 free for battery sense; I2C0 -> D4/D5 (TMP102/117, ADS1115)
Console/flash -> USB-C (USB-Serial/JTAG), not the header UART
spi2 is disabled in app/app.overlay to free D8-D10; re-pin there if needed.
```

## Still to decide

- Pump head and how the stepper couples to it (microstepping, steps per mL).
- Whether battery / temperature monitoring is needed, and ADC-divider vs I²C ADS1115.
- WiFi is enabled and auto-connects (see "WiFi"); what to run over it (a remote
  control protocol, a telemetry sink) is still open.
