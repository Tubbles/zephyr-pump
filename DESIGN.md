# HiFive1 Rev B + EDU Shield + A4988 Stepper — Build Plan

## Hardware

| Item      | Part                              | Key facts                                                             |
| --------- | --------------------------------- | --------------------------------------------------------------------- |
| MCU board | SiFive HiFive1 Rev B (FE310-G002) | **No ADC**, 3.3 V I/O only, hardware I²C/SPI/UART/PWM                 |
| Shield    | Electrokit EDU Shield for Arduino | Mostly analog sensors (unusable) + 2× QWIIC I²C (usable)              |
| Driver    | A4988 module (Hailege / Rungee)   | Chopper current driver, VMOT 8–35 V, ~1 A bare / ~1.5 A with heatsink |
| Motor     | 42SHD0217-24B                     | NEMA 17 bipolar, **1.5 A/phase**, 2.2 Ω, ~3.3 V rated                 |
| Power     | 3S LiPo                           | ~9–12.6 V; sits inside A4988's VMOT range                             |

## The one constraint that drives everything

The FE310-G002 has **no analog-to-digital converter**. `analogRead()` does not exist and the
AREF pin is not connected, so no pin can read an analog voltage. A0 isn't routed on this board
at all; A1–A5 are wired to GPIO9–13 and work fine as **digital** GPIO (the stepper plan below
reuses them), they just can't sense analog. Anything analog must go through an external **I²C**
device.

---

## Peripheral matrix — Electrokit EDU Shield on HiFive1 Rev B

| Shield feature          | Header pin | FE310 GPIO   | Status     | Notes                                                                             |
| ----------------------- | ---------- | ------------ | ---------- | --------------------------------------------------------------------------------- |
| Analog light sensor     | A0         | —            | ❌ Dead    | No ADC; A0 isn't routed at all                                                    |
| Potentiometer           | A1         | GPIO9        | ❌ Dead    | No ADC — use the encoder. Pin reused as digital (stepper DIR)                     |
| External analog GPIO    | A2         | GPIO10       | ❌ Dead    | No ADC. Pin reused as digital (stepper EN)                                        |
| Analog temp sensor      | A3         | GPIO11       | ❌ Dead    | No ADC — use an I²C temp sensor. Pin reused (stepper STEP)                        |
| QWIIC I²C ×2            | A4/A5      | GPIO12/13    | ✅ Works   | I²C0 SDA/SCL — **your way in** for sensors                                        |
| UART connector          | D0/D1      | GPIO16/17    | ✅ Works   | UART0; also the USB debug console                                                 |
| Speaker (PWM tone)      | D2         | GPIO18       | ✅ SW tone | Works, but GPIO18 has no PWM mux — tone via software toggling, not the pwm driver |
| Rotary encoder + button | D3/D4/D5   | GPIO19/20/21 | ⚠️ Cond.   | GPIO19/21 are the onboard green/blue LEDs — disable to reuse                      |
| WS2812B RGB LED         | D6         | GPIO22       | ⚠️ Maybe   | GPIO22 is the onboard red LED; tight bit-bang, no RISC-V lib                      |
| Button 1                | D7         | GPIO23       | ❌ Dead    | Mapped in DT but not routed on the Rev B header                                   |
| Button 2                | D8         | GPIO0        | ❌ Dead    | Mapped in DT but not routed on the Rev B header                                   |
| LED 1                   | D9         | GPIO1        | ✅ Works   |                                                                                   |
| LED 2                   | D10        | GPIO2        | ✅ Works   |                                                                                   |
| LED 3                   | D11        | GPIO3        | ⚠️ Cond.   | SPI1 MOSI — plain output only if SPI disabled                                     |
| Servo                   | D12        | GPIO4        | ⚠️ Cond.   | SPI1 MISO — conflicts if SPI active                                               |
| Button 3                | D13        | GPIO5        | ⚠️ Cond.   | SPI1 SCK — no effect while SPI active                                             |

**Net result:** all four analog sensors and two of three buttons are gone. Everything you
actually need survives: speaker, encoder, LEDs, UART console, and both QWIIC I²C ports.

---

## Pin naming reference (Uno ↔ Zephyr ↔ FE310)

Three naming systems describe the same physical header, which is the source of most
confusion. Always disambiguate by **FE310 GPIO number**, that is the only label the
Zephyr devicetree and the FE310 registers actually use.

- **Uno silk** is the printed label (D0–D13, A0–A5).
- **Uno alt #** is the Arduino habit of also numbering the analog pins as digital 14–19
  (A0=14 … A5=19). This is where the design's "D18/D19" for I²C comes from (A4/A5).
- **Zephyr DT** is the `ARDUINO_HEADER_R3_*` label in the board devicetree. Note Zephyr
  reuses **D14/D15 for the dedicated SDA/SCL pins** (= the A4/A5 nets), which collides
  with the Uno alt numbering where D14/D15 would be A0/A1. Same pin, different label.
- **IOF0 / IOF1** are the FE310's two pin-mux alternate functions. A pin is GPIO **or**
  one IOF at a time, selected per-peripheral by the devicetree `pinctrl`.

Sourced from `boards/sifive/hifive1/hifive1.dts` (the `arduino_header` map),
`hifive1-pinctrl.dtsi`, and `dts/riscv/sifive/riscv32-fe310.dtsi`.

| Uno silk | Uno alt # | Zephyr DT | FE310 GPIO | IOF0         | IOF1      | Notes                                   |
| -------- | --------- | --------- | ---------- | ------------ | --------- | --------------------------------------- |
| D0       | —         | D0        | GPIO16     | UART0 RX     | —         | console UART                            |
| D1       | —         | D1        | GPIO17     | UART0 TX     | —         | console UART                            |
| D2       | —         | D2        | GPIO18     | —            | —         | **no PWM/serial mux** — plain GPIO only |
| D3       | —         | D3        | GPIO19     | —            | PWM1 ch1  | onboard **green** LED (`led0`)          |
| D4       | —         | D4        | GPIO20     | —            | PWM1 ch0  |                                         |
| D5       | —         | D5        | GPIO21     | —            | PWM1 ch2  | onboard **blue** LED (`led1`)           |
| D6       | —         | D6        | GPIO22     | —            | PWM1 ch3  | onboard **red** LED (`led2`)            |
| D7       | —         | D7        | GPIO23     | —            | —         |                                         |
| D8       | —         | D8        | GPIO0      | —            | PWM0 ch0† |                                         |
| D9       | —         | D9        | GPIO1      | —            | PWM0 ch1  |                                         |
| D10      | —         | D10       | GPIO2      | SPI1 CS0     | PWM0 ch2  |                                         |
| D11      | —         | D11       | GPIO3      | SPI1 MOSI    | PWM0 ch3  |                                         |
| D12      | —         | D12       | GPIO4      | SPI1 MISO    | —         |                                         |
| D13      | —         | D13       | GPIO5      | SPI1 SCK     | —         |                                         |
| SDA      | —         | D14       | GPIO12     | **I2C0 SDA** | PWM2 ch2  | same net as A4                          |
| SCL      | —         | D15       | GPIO13     | **I2C0 SCL** | PWM2 ch3  | same net as A5                          |
| A0       | D14       | —         | —          | —            | —         | **not connected** on HiFive1            |
| A1       | D15       | A1        | GPIO9      | SPI1 CS2     | —         | no PWM function                         |
| A2       | D16       | A2        | GPIO10     | SPI1 CS3     | PWM2 ch0† |                                         |
| A3       | D17       | A3        | GPIO11     | —            | PWM2 ch1  | only usable hardware-PWM pin free on J4 |
| A4       | D18       | A4        | GPIO12     | **I2C0 SDA** | PWM2 ch2  | = D14 / SDA                             |
| A5       | D19       | A5        | GPIO13     | **I2C0 SCL** | PWM2 ch3  | = D15 / SCL                             |

**PWM instances:** PWM0 is 8-bit (`compare-width = <8>`), PWM1 and PWM2 are 16-bit.

**† Channel 0 caveat:** on every FE310 PWM, channel 0's compare register _is_ the
shared period register, so it cannot drive an output. The Zephyr `pwm_sifive` driver
returns `-ENOTSUP` for channel 0. Usable PWM outputs are **channels 1–3 only**. This is
why STEP must sit on PWM2 **ch1** (GPIO11), not ch0 (GPIO10).

**Default-DTS conflicts to resolve in an overlay:** as shipped, `spi1`+`spi2` are enabled
and claim GPIO2/3/4/5/9/10, while `pwm2` (ch1–3) and `i2c0` both claim GPIO12/13. Free
GPIO9/10 by disabling SPI, and trim `pwm2` to ch1 only so `i2c0` can own GPIO12/13.

**Header routing:** the map is the devicetree's `arduino-header-r3` binding — the SoC pin at
each position. A few positions aren't broken out on the Rev B board: **A0** (no GPIO) and the
**D7/D8** pins (GPIO23/GPIO0), so they're listed for completeness but aren't usable.

---

## Battery monitoring plan (via QWIIC, no ADC needed)

Both readings go over I²C through the shield's two QWIIC connectors — no soldering, 3.3 V logic.

- **Voltage:** QWIIC **ADS1115** (16-bit I²C ADC) + a **voltage divider** to scale the pack
  (up to 12.6 V) down into the ADC input range. Size the divider for your full-charge voltage
  with margin.
- **Temperature:** QWIIC digital temp sensor (**TMP102 / TMP117**, or a DS18B20 on 1-Wire).
  No analog thermistor.
- Both share the I²C bus (GPIO12/13); just give them distinct addresses.

> LiPo care: add a low-voltage alarm/cutoff at ~3.0–3.2 V per cell. Never run the pack flat.

---

## Stepper driver wiring plan

### Control lines: HiFive → A4988

Because you can solder directly to any pad, the clean choice is the **J4 6-pin header**
(GPIO9–13), which the Arduino-footprint shield does **not** occupy — so the whole shield stays
intact. GPIO12/13 are reserved for the QWIIC I²C bus, leaving GPIO9/10/11 for the driver. Note
GPIO9/10 carry SPI1 by default — disable SPI in the overlay (see the pin reference above) to use
them as plain GPIO.

| A4988 pin | HiFive GPIO | Header  | Role         | Why this pin                                                                                               |
| --------- | ----------- | ------- | ------------ | ---------------------------------------------------------------------------------------------------------- |
| **STEP**  | GPIO11      | J4 (A3) | PWM2 ch1     | Only free J4 pin with a usable 16-bit PWM output (ch0 just holds the period) → smooth, CPU-free step train |
| **DIR**   | GPIO9       | J4 (A1) | Plain output | Just a level, no timing. SPI1 CS2 by default — free it in the overlay                                      |
| **EN**    | GPIO10      | J4 (A2) | Plain output | Active-low; drive it actively. SPI1 CS3 by default — free it in the overlay                                |

> If J4 is blocked by the seated shield, solder the three jumpers to the J4 pads **before**
> mounting the shield. STEP must stay on a usable 16-bit PWM channel: GPIO11 = PWM2 ch1 (ch0 on
> GPIO10 only holds the period and can't drive a pin).

**STEP via PWM:** step rate ≈ f_scaled / pwmcmp0, where f_scaled = 16 MHz / 2^pwmscale and
pwmcmp0 is PWM2's shared period register. The STEP pin is **channel 1** (pwmcmp1 sets the pulse
width); channel 0 only ever holds the period and can't drive a pin. 16-bit (pwmcmp0 up to 65535) gives near-continuous speed/accel resolution. The shared 8-bit PWM0 would quantize speed
in ~4–8 % jumps and force constant rescaling — avoid it; the PWM2 channels are already 16-bit.

**EN:** kept because you're on battery — driving EN high between moves cuts holding current
and heat. The motor still runs fine if EN is just grounded, but you lose that power saving.

### A4988 module — required support connections

| A4988 pin         | Connect to        | Notes                                                       |
| ----------------- | ----------------- | ----------------------------------------------------------- |
| VMOT              | 3S LiPo +         | **100 µF** electrolytic across VMOT/GND, close to the board |
| GND (motor)       | LiPo −            | Common ground with logic                                    |
| VDD               | HiFive 3.3 V      | Logic supply                                                |
| GND (logic)       | HiFive GND        |                                                             |
| **RESET + SLEEP** | tie together      | **Mandatory** — bridge these or the driver won't output     |
| MS1 / MS2 / MS3   | set microstepping | Open = full step; pull high per table for 1/2…1/16          |
| STEP / DIR / EN   | see table above   | 3.3 V logic is accepted by the A4988                        |

> Classic gotcha: if the motor hums but won't move, you forgot the **RESET↔SLEEP** bridge.

### A4988 → Motor (42SHD0217-24B)

Bipolar, 4 wires = two coils. Wire one coil to **1A/1B** and the other to **2A/2B**.

- Identify coil pairs: the two wires of one coil read continuity / low resistance (~2.2 Ω)
  to each other and open to the other pair. Common Electrokit/OEM coloring is
  (A+ / A−) and (B+ / B−) — verify with a meter, don't assume.
- If the motor runs rough or vibrates without turning, one coil pair is split across the two
  channels — swap one wire pair.
- Direction wrong? Flip DIR in software or swap one coil's two wires.

### Current limit (do this before first run)

- Target **≤ 1.5 A/phase**. Set via the trim pot → Vref.
- `Vref = I_max × 8 × R_sense`. **Check R_sense on your module** (often 0.1 Ω or 0.05 Ω) —
  it changes the math. Start low and raise it.
- Add the **heatsink** if pushing toward 1.5 A; airflow helps.

---

## Power notes

- 3S LiPo on VMOT is ideal; current limiting holds coil current constant as the pack sags.
- **Never** connect/disconnect the motor while VMOT is live — it can kill the A4988.
- Ignore the motor's "3.3 V" rating for supply purposes — that's only the DC coil voltage used
  in the current calc; the chopper handles the rest.

## Software reality check

Arduino core isn't supported on this board. Use **Freedom Metal** (bare-metal register access)
or **Zephyr**. PWM = configure pwmcfg / pwmscale / pwmcmp0 (period) + pwmcmp1 (the STEP pulse)
on PWM2; GPIO for DIR/EN; I²C peripheral for the sensors. Under Zephyr the `pwm_sifive` driver
rejects channel 0, so STEP uses channel 1. No `analogWrite()` / `analogRead()`.

## Motor control (implemented)

The STEP/DIR/EN peripherals are wired up in `app/app.overlay` and driven from
`app/src/motor.c`, gated by `CONFIG_APP_MOTOR`. The overlay realizes the
overlay plan above:

- `spi1` and `spi2` are disabled. Both claim GPIO9 on this board (spi2 reuses
  spi1's cs2 pin group), and spi1 also claims GPIO10, so both must go to free
  DIR/EN as plain GPIO. The boot flash is on spi0, so this is safe.
- `pwm2` is trimmed to `pinctrl-0 = <&pwm2_1_default>` (channel 1 only), keeping
  STEP on GPIO11 and leaving GPIO12/13 free for I²C0.
- The `zephyr,user` node carries `pwms = <&pwm2 1 PWM_MSEC(1)>` (STEP, default
  1 kHz, rate set at runtime), `dir-gpios` (GPIO9), and `en-gpios` (GPIO10,
  `GPIO_ACTIVE_LOW` so logical 1 means "driver enabled"). The
  `sifive,pwm0` binding has `#pwm-cells = <2>` (channel, period) with no flags
  cell; the driver only supports normal polarity anyway.

At boot the driver parks EN inactive (motor free, no holding current) and STEP
at 0% duty (no pulses). It registers a `motor` shell command group:

```
motor enable             # EN active (energize, holding torque on)
motor disable            # EN inactive (coast)
motor dir <0|1>          # set the DIR line level
motor run <hz>           # continuous step train at <hz>
motor steps <count> <hz> # step <count> times at <hz>, then stop (~±1 step)
motor stop               # stop the step train
motor status             # last commanded enabled / dir / rate
```

STEP runs as a 50% square wave (the A4988 needs only a ~1 µs minimum high time).
`run`/`steps` do not auto-enable the driver; they warn when EN is inactive since
the motor won't move. `steps` schedules the stop on the system workqueue, so the
pulse count is timer-bounded and accurate to about ±1 step.

## Speaker tone (software)

D2 (GPIO18) has no PWM mux, so the speaker is driven as a square wave in
software: a repeating `k_timer` toggles the pin from its expiry handler, which
runs in the system timer interrupt. A one-shot `k_timer` ends the tone and parks
the pin low. Lives in `app/src/speaker.c`, gated by `CONFIG_APP_SPEAKER`, which
also raises `CONFIG_SYS_CLOCK_TICKS_PER_SEC` to 100 kHz (10 µs steps) so the
output frequency tracks the request to within ~1%. It registers a shell command:

```
tone <centi-hz> <ms>   # e.g. tone 44000 500  -> 440.00 Hz for half a second
```

Frequency is in centi-hertz (hundredths of a Hz) so the integer argument can
express sub-hertz precision.

## Quick pin summary

```
STEP  -> GPIO11  (J4, PWM2 ch1, 16-bit)
DIR   -> GPIO9   (J4, plain GPIO)
EN    -> GPIO10  (J4, plain GPIO)
I2C   -> GPIO12/13 (QWIIC: ADS1115 voltage + TMP102/117 temp)
Overlay: disable spi1/spi2 (frees GPIO9/10); trim pwm2 to ch1 (frees GPIO12/13 for I2C)
Shield via J4: UART D0/D1; speaker D2 needs software PWM; encoder D3-D5 shares the onboard RGB LED pins
```
