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
  climate.py    config schema registration (climate_ir_with_receiver_schema)
  rapid_ac.h    header
  rapid_ac.cpp  frame builder + IR transmit + IR receive decoder
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
| R0 | 0x00 | fixed (timer hours `0xA0\|h` — see Timer) |
| R1 | bitfield | Light=bit0, Turbo=bit3, timer flag=bit4 |
| R2 | 0x00..0x0B | command (see table below) |
| R3 | bitfield | Sleep, Power, Swing, AirFlow, Fan |
| R4 | `(mode_code << 5) \| (temp - 16)` | mode_code: AUTO=0 COOL=1 DRY=2 FAN=3 HEAT=4 |
| R5 | 0x55 | fixed → wire `AA 55` |

### Commands (R2)

| R2 | Command |
|----|---------|
| 0x00 | Power |
| 0x01 | Mode |
| 0x02 | Temp+ |
| 0x03 | Temp- |
| 0x04 | Swing (toggle) |
| 0x05 | Fan speed |
| 0x06 | unknown (assumed Timer from Goodweather; live capture says Timer = 0x0D) |
| 0x09 | Sleep |
| 0x0A | Turbo |
| 0x0B | Light |
| 0x0D | Timer |

### R1 bitfield (LSB)

| Bit | Field | Values |
|-----|-------|--------|
| 0 | Light (display) | 1=display off |
| 3 | Turbo | 1=on |
| 4 | timer flag | 1 in timer frames (cmd 0x0D) |

`R1 = (light ? 0x01 : 0) | (turbo ? 0x08 : 0)`

### R3 bitfield (LSB)

| Bit(s) | Field | Values |
|--------|-------|--------|
| 0 | Sleep | 1=on |
| 1 | Power | 1=on, 0=off |
| 2-3 | Swing | 0b01=on, 0b10=off |
| 4 | AirFlow | 1 (fixed) |
| 5-6 | Fan | 0b00=Auto, 0b01=High, 0b10=Med, 0b11=Low |
| 7 | reserved | 0 |

`R3 = (sleep ? 0x01 : 0) | (power ? 0x02 : 0) | (swing_on ? 0x04 : 0x08) | 0x10 | (fan_bits << 5)`

### Home Assistant mapping

| Rapid feature | HA control | Frame |
|---------------|------------|-------|
| Power / Mode / Temp | climate standard | 0x00 / 0x01 / 0x02 / 0x03 |
| Swing (louver) | swing mode `VERTICAL` | 0x04, R3 bits 2-3 |
| Fan speed | fan mode `AUTO / LOW / MEDIUM / HIGH` | 0x05, R3 bits 5-6 |
| Sleep | preset `Sleep` | 0x09, R3 bit0 |
| Turbo | preset `Boost` | 0x0A, R1 bit3 |
| Light (display) | custom preset `light` | 0x0B, R1 bit0 |

`hvac_action` mirrors the selected mode (COOL→cooling, HEAT→heating, DRY→drying,
FAN→fan, OFF→off, AUTO→idle). The real compressor state is not observable over IR —
pair with a power-monitoring plug for a true idle/running signal.

### Sample frames (wire)

Power on commands, COOL 24°C base, fan Auto, swing off:

```
cool 24°C power on : FF 00 FF 00 FF 00 E5 1A D7 28 AA 55
heat 24°C power on : FF 00 FF 00 FF 00 E5 1A 77 88 AA 55
fan  24°C power on : FF 00 FF 00 FF 00 E5 1A 97 68 AA 55
auto 25°C power on : FF 00 FF 00 FF 00 E5 1A F6 09 AA 55
dry  25°C power on : FF 00 FF 00 FF 00 E5 1A B6 49 AA 55
power off          : FF 00 FF 00 FF 00 E7 18 D7 28 AA 55
```

Fan speed change (cmd 0x05), COOL 24°C, power on:

```
fan High           : FF 00 FA 05 C5 3A D7 28 AA 55
fan Auto           : FF 00 FA 05 E5 1A D7 28 AA 55
fan Medium         : FF 00 FA 05 CD 2A D7 28 AA 55
fan Low            : FF 00 FA 05 85 7A D7 28 AA 55
```

Temp+ from original remote (cmd 0x02), COOL 24°C, fan Low:

```
temp+ (capture)    : FF 00 FD 02 85 7A D7 28 AA 55
```

### Differences from Goodweather (IRremoteESP8266)

| Feature | Rapid | Goodweather |
|---------|-------|-------------|
| Pad (R5) | 0x55 (wire `AA 55`) | 0xD5 (wire `2A D5`) |
| Space polarity | long space = 1 | long space = 0 |
| Swing | on / off only | fast / slow / off |
| AirFlow button | absent on remote | present |

## Verification

- On boot, check ESPHome logs for `remote_receiver` decoded frames from the original remote.
  If they match the formula above, transmission timing is correct.
- Send a COOL 24 °C via Home Assistant and confirm the AC responds.
- The `ESP_LOGV` line in `rapid_ac.cpp` prints the emitted wire frame per command.

## Receive (IR feedback from the original remote)

The component decodes IR frames from the original remote (via `remote_receiver`, GPIO5)
and publishes the resulting state to Home Assistant, so `climate.rapid_ac` stays in sync
when you press buttons on the physical remote.

Requires `receiver_id` in the climate config:

```yaml
climate:
  - platform: rapid_ac
    name: "Rapid AC"
    transmitter_id: remote_transmitter
    receiver_id: remote_receiver
```

Decoding is polarity-agnostic (`abs()`-matching with ±25% tolerance) because the IRM-3638
receiver is active-low: marks come as negative values, spaces as positive. The decoder matches
absolute values against expected timings regardless of sign.

Flow: header `6200/7480` → 96 data bits (mark `560`, space `>1000 µs` = 1 else 0) →
footer `560/7480` + final mark `560` → pair-complement check `[~Ri, Ri]`. Every valid
frame updates power/mode/temp/fan/swing and presets (sleep/turbo/light), then `publish_state()`.

### Testing IR receive

1. Clean Build → Install on server.
2. Press any button on the original remote.
3. In ESPHome logs look for:
   - `accepted: R1=.. R3=.. R4=.. power=.. mode=.. temp=.. fan=.. swing=..` — frame decoded
   - `reject: ...` — frame did not match protocol (check timings)
4. Verify `climate.rapid_ac` state updates in Home Assistant.

## Timer (R2=0x0D, not implemented)

Confirmed by live capture: timer+ 4h frame = cmd `0x0D`, `R0=0xA4` (`0xA0 | hours`,
so 1h=`A1`, 2h=`A2`...), R1 bit4 set (timer flag). Planned as `number`/`switch`
entity in Home Assistant. Not yet implemented.

## GitHub

Public repository: `https://github.com/night-gnida/rapid-ac`
