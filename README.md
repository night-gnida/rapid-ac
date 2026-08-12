# Rapid AC — ESPHome climate component for the YTF Remote IR (Rapid) air conditioner

Wi-Fi IR remote for a Rapid AC. Protocol fully reverse-engineered (Goodweather/Midea-style clone),
custom ESPHome `climate` platform implemented from scratch.

## Hardware

- **YTF Remote IR** board with **TYWE3S (ESP8266EX)**.
- IR receiver (IRM-3638) on **GPIO5**, IR LED on **GPIO14**.
- Flashing: USB-UART 3.3 V (CP2102/CH340/FT232) or Flipper Zero as UART bridge.
  Short **GPIO0 to GND** during power-up for boot mode, flash address **0x0**, board `esp01_1m`.

## Wiring (GPIO0 → boot / flash)

```
USB-UART         TYWE3S
  TX  ----------- RX
  RX  ----------- TX
  GND ----------- GND
  3.3V ---------- 3V3      (only if powered from UART adapter)
  GPIO0 pin ---   GND      (momentarily, during boot for flashing)
```

## Install

1. Install ESPHome (`pip install esphome`).
2. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your WiFi/API/OTA credentials.
3. Build & flash over UART:

   ```powershell
   esphome run esphome.yaml
   ```

   In the wizard choose "UPLOAD via USB cable". After flashing, subsequent updates go over WiFi (OTA).

## Component structure

```
components/rapid_ac/
  __init__.py   namespace + class binding (extends climate_ir.ClimateIR)
  climate.py    config schema registration
  rapid_ac.h    header
  rapid_ac.cpp  frame builder + IR transmit
esphome.yaml
secrets.yaml
```

## Protocol (reverse-engineered)

Carrier 38 kHz. Timing (µs): header mark/space **6200/7480**, bit mark **560**,
space-1 **1640**, space-0 **550**, footer mark **560**, footer space **7480**,
final mark **560**, end gap ~**10125**.

Wire frame = 12 bytes = 6 real bytes in pairs `[~Ri, Ri]` (LSB-first per byte):

| Real | Byte | Meaning |
|------|------|---------|
| R0 | 0x00 | fixed |
| R1 | 0x00 | fixed (Light=bit0, Turbo=bit3, not exposed yet) |
| R2 | 0x00..0x05 | command (00=power, 01=mode, 02=temp+, 03=temp−, 04=swing, 05=fan) |
| R3 | bitfield | see below |
| R4 | `((mode_code<<1)<<4) \| (temp−16)` | mode_code: AUTO=0 COOL=1 DRY=2 FAN=3 HEAT=4 |
| R5 | 0x55 | fixed → wire `AA 55` |

R3 bitfield (LSB): `Swing(2) | AirFlow(1) | Fan(2)` — Power at bit1, bits:

| Bit(s) | Field | Values |
|--------|-------|--------|
| 0 | reserved | 0 |
| 1 | Power | 1=on, 0=off |
| 2-3 | Swing | 0b10=off, 0b01=on |
| 4 | AirFlow | 1 (fixed in captures) |
| 5-6 | Fan | 0b00=Auto, 0b01=High, 0b10=Med, 0b11=Low |
| 7 | reserved | 0 |

So `R3 = (power?0x02:0) | (swing_on?0x04:0x08) | 0x10 | (fan_bits<<5)`.

### Swing (шторка, R2 = 0x04)

Swing uses `R2 = 0x04`. Swinging swaps bits 2-3 (`0x08`↔`0x04`). DRY has no swing.
When swing is enabled, every frame (mode/temp/power/fan) carries the swing bits,
matching the original remote.

### Fan speed (R2 = 0x05)

Fan speed is a full-state command `R2 = 0x05` that only changes the Fan bits in R3
(`R4` stays as the current mode/temp). Mapping:

| HA fan_mode | R3 bits 5-6 | R3 (power on, swing off) |
|-------------|-------------|--------------------------|
| AUTO  | 0b00 | 0x1A |
| HIGH  | 0b01 | 0x3A |
| MEDIUM| 0b10 | 0x5A |
| LOW   | 0b11 | 0x7A |

Confirmed by capture cycle Auto→High→Med→Low in FAN 25°C, and the same frames with
swing on (`0x16/0x36/0x56/0x76`).

Sample frames (wire):

```
cool 24°C : FF 00 FF 00 FD 02 C5 3A D7 28 AA 55
heat 24°C : FF 00 FF 00 FD 02 85 7A 77 88 AA 55
fan  24°C : FF 00 FF 00 FC 03 85 7A 97 68 AA 55
auto 25°C : FF 00 FF 00 FE 01 85 7A F6 09 AA 55
dry  25°C : FF 00 FF 00 FE 01 85 7A B6 49 AA 55
power off: FF 00 FF 00 FF 00 87 78 B6 49 AA 55
```

## Verification

- On boot, check ESPHome logs for `remote_receiver` decoded frames from the original remote.
  If they match the formula above, transmission timing is correct.
- Send a COOL 24 °C via Home Assistant and confirm the AC responds.
- The `ESP_LOGV` line in `rapid_ac.cpp` prints the emitted wire frame per command.
