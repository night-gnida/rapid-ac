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
| R0 | 0x00 | fixed (timer hours `0xA0\|h` — see Timer, not exposed yet) |
| R1 | bitfield | Light=bit0, Turbo=bit3 |
| R2 | 0x00..0x0B | command (00=power, 01=mode, 02=temp+, 03=temp−, 04=swing, 05=fan, 06=timer, 09=sleep, 0A=turbo, 0B=light) |
| R3 | bitfield | see below |
| R4 | `((mode_code<<1)<<4) \| (temp−16)` | mode_code: AUTO=0 COOL=1 DRY=2 FAN=3 HEAT=4 |
| R5 | 0x55 | fixed → wire `AA 55` |

R1 bitfield (LSB):

| Bit | Field | Values |
|-----|-------|--------|
| 0 | Light (дисплей) | 1=display off |
| 3 | Turbo | 1=on |

R3 bitfield (LSB): `Sleep | Power | Swing(2) | AirFlow | Fan(2)` — bits:

| Bit(s) | Field | Values |
|--------|-------|--------|
| 0 | Sleep | 1=on |
| 1 | Power | 1=on, 0=off |
| 2-3 | Swing | 0b10=off, 0b01=on |
| 4 | AirFlow | 1 (fixed in captures) |
| 5-6 | Fan | 0b00=Auto, 0b01=High, 0b10=Med, 0b11=Low |
| 7 | reserved | 0 |

So `R1 = (light?0x01:0) | (turbo?0x08:0)` and
`R3 = (sleep?0x01:0) | (power?0x02:0) | (swing_on?0x04:0x08) | 0x10 | (fan_bits<<5)`.

### Home Assistant mapping

| Rapid feature | HA control | Frame |
|---------------|------------|-------|
| Power / Mode / Temp | climate standard | 0x00/0x01/0x02/0x03 |
| Swing (шторка) | swing mode `VERTICAL` | 0x04, bits 2-3 |
| Fan speed | fan mode `AUTO/LOW/MEDIUM/HIGH` | 0x05, bits 5-6 |
| Sleep | preset `Sleep` | 0x09, R3 bit0 |
| Turbo | preset `Boost` | 0x0A, R1 bit3 |
| Light (дисплей) | custom preset `light` | 0x0B, R1 bit0 |

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
