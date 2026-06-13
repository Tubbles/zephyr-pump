# HiFive1 Rev B + EDU Shield + A4988 Stepper — Build Plan

## Hardware

| Item | Part | Key facts |
|---|---|---|
| MCU board | SiFive HiFive1 Rev B (FE310-G002) | **No ADC**, 3.3 V I/O only, hardware I²C/SPI/UART/PWM |
| Shield | Electrokit EDU Shield for Arduino | Mostly analog sensors (unusable) + 2× QWIIC I²C (usable) |
| Driver | A4988 module (Hailege / Rungee) | Chopper current driver, VMOT 8–35 V, ~1 A bare / ~1.5 A with heatsink |
| Motor | 42SHD0217-24B | NEMA 17 bipolar, **1.5 A/phase**, 2.2 Ω, ~3.3 V rated |
| Power | 3S LiPo | ~9–12.6 V; sits inside A4988's VMOT range |

## The one constraint that drives everything

The FE310-G002 has **no analog-to-digital converter**. `analogRead()` does not exist, the
AREF pin is not connected, and the A0–A5 header positions are not usable as inputs (not even
as digital pins, unlike a real Uno). Anything analog must go through an external **I²C** device.

---

## Peripheral matrix — Electrokit EDU Shield on HiFive1 Rev B

| Shield feature | Header pin | FE310 GPIO | Status | Notes |
|---|---|---|---|---|
| Analog light sensor | A0 | — | ❌ Dead | No ADC |
| Potentiometer | A1 | — | ❌ Dead | No ADC — use the encoder instead |
| External analog GPIO | A2 | — | ❌ Dead | No ADC |
| Analog temp sensor | A3 | — | ❌ Dead | No ADC — use an I²C temp sensor instead |
| QWIIC I²C ×2 | D18/D19 | GPIO12/13 | ✅ Works | **Your way in** for sensors |
| UART connector | D0/D1 | GPIO16/17 | ✅ Works | Also the USB debug console |
| Speaker (PWM tone) | D2 | GPIO18 | ✅ Works | |
| Rotary encoder + button | D3/D4/D5 | GPIO19/20/21 | ✅ Works | Best knob substitute for the dead pot |
| WS2812B RGB LED | D6 | GPIO22 | ⚠️ Maybe | Needs tight bit-bang timing; no RISC-V lib ready |
| Button 1 | D7 | — | ❌ Dead | Pin not connected on HiFive |
| Button 2 | D8 | — | ❌ Dead | Pin not connected on HiFive |
| LED 1 | D9 | GPIO1 | ✅ Works | |
| LED 2 | D10 | GPIO2 | ✅ Works | |
| LED 3 | D11 | GPIO3 | ⚠️ Cond. | SPI MOSI — plain output only if SPI unused |
| Servo | D12 | GPIO4 | ⚠️ Cond. | SPI MISO — conflicts if SPI active |
| Button 3 | D13 | GPIO5 | ⚠️ Cond. | SPI SCK — no effect while SPI active |

**Net result:** all four analog sensors and two of three buttons are gone. Everything you
actually need survives: speaker, encoder, LEDs, UART console, and both QWIIC I²C ports.

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
intact. GPIO12/13 are reserved for the QWIIC I²C bus, leaving GPIO9/10/11 free.

| A4988 pin | HiFive GPIO | Header | Role | Why this pin |
|---|---|---|---|---|
| **STEP** | GPIO10 | J4 / D16 | PWM2_0 | 16-bit PWM channel 0 sets the period → smooth, CPU-free step train |
| **DIR** | GPIO9 | J4 / D15 | Plain output | Just a level; no timing |
| **EN** | GPIO11 | J4 / D17 | Plain output | Active-low; drive it actively (don't trust the float) |

> If J4 is blocked by the seated shield, solder the three jumpers to the J4 pads **before**
> mounting the shield. STEP must stay on a 16-bit PWM channel (GPIO10).

**STEP via PWM:** step rate ≈ f_scaled / pwmcmp0, where f_scaled = 16 MHz / 2^pwmscale.
16-bit (pwmcmp0 up to 65535) gives near-continuous speed/accel resolution. The shared 8-bit
PWM0 would quantize speed in ~4–8 % jumps and force constant rescaling — avoid it; the PWM2
channels are already 16-bit.

**EN:** kept because you're on battery — driving EN high between moves cuts holding current
and heat. The motor still runs fine if EN is just grounded, but you lose that power saving.

### A4988 module — required support connections

| A4988 pin | Connect to | Notes |
|---|---|---|
| VMOT | 3S LiPo + | **100 µF** electrolytic across VMOT/GND, close to the board |
| GND (motor) | LiPo − | Common ground with logic |
| VDD | HiFive 3.3 V | Logic supply |
| GND (logic) | HiFive GND | |
| **RESET + SLEEP** | tie together | **Mandatory** — bridge these or the driver won't output |
| MS1 / MS2 / MS3 | set microstepping | Open = full step; pull high per table for 1/2…1/16 |
| STEP / DIR / EN | see table above | 3.3 V logic is accepted by the A4988 |

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
or **Zephyr**. PWM = configure pwmcfg / pwmscale / pwmcmp0 on PWM2; GPIO for DIR/EN; I²C
peripheral for the sensors. No `analogWrite()` / `analogRead()`.

## Quick pin summary

```
STEP  -> GPIO10  (J4, PWM2_0, 16-bit)
DIR   -> GPIO9   (J4, plain GPIO)
EN    -> GPIO11  (J4, plain GPIO)
I2C   -> GPIO12/13 (QWIIC: ADS1115 voltage + TMP102/117 temp)
Shield: speaker D2, encoder D3-D5, LEDs D9/D10, UART D0/D1 — all free
```
