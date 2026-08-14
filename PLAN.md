# Rapid AC — план развития (архив чата, обновлён 2026-08-13)

## Контекст

YTF Remote IR (TYWE3S/ESP8266, плата `esp01_1m`) прошит кастомным ESPHome-компонентом
`rapid_ac` — Wi-Fi ИК-пульт кондиционера Rapid (клон Goodweather).

- GitHub (public): `https://github.com/night-gnida/rapid-ac`
- Сервер HA: `/config/esphome/ir-remote-bluster.yaml` (external_components → GitHub)
- ESPHome 2026.7.4 (локально не установлен — компиляция только на сервере)
- IR приёмник IRM-3638 — **active-low** (mark = отрицательный, space = положительный)

## Протокол (верифицировано по захватам)

- Кадр: 6 реальных байт → 12 wire `[~Ri, Ri]`, LSB-first, 38 кГц.
- Тайминги (мкс): HDR `6200/7480`, бит `560`, 1 → space `1640`, 0 → space `550`,
  футер `560/7480` + финальный mark `560`, зазор ~`10125`.
- Поля: `R0=00 (0xA0|h для таймера)`, `R1=(Light<<0 | Turbo<<3)`, `R2=Command`,
  `R3=битфилд`, `R4=(mode_code<<5)|(temp−16)`, `R5=55` (pad).
  mode: AUTO=0 COOL=1 DRY=2 FAN=3 HEAT=4.

| R2 | Команда |
|----|---------|
| 0x00 | Power |
| 0x01 | Mode |
| 0x02 / 0x03 | Temp+ / Temp− |
| 0x04 | Swing |
| 0x05 | Fan speed |
| 0x06 | unknown (Timer is 0x0D, live capture) |
| 0x07 | AirFlow |
| 0x08 | Hold |
| 0x09 | Sleep |
| 0x0A | Turbo |
| 0x0B | Light |

R1 битфилд: `(Turbo<<3) | (Light<<4)`.
> **Уточнено живыми тестами 2026-08-14:**
> - Light = **бит 0** (кадры cmd `0x0B`: `R1=01↔00`). Промежуточный «фикс на бит 4»
>   (`f127b53`) был ошибкой, откачен в `H2` (см. коммиты ниже).
> - Turbo = бит 3 подтверждён (`R1=0x08`).
> - R1 **бит 4** (`0x10`) встречается в таймерных кадрах (cmd `0x0D`) — таймерный
>   флаг, НЕ Light.
> - Timer = команда **`0x0D`** (не `0x06`, как предполагалось по Goodweather):
>   живой захват «Timer+ 4ч» = cmd `0x0D`, `R0=0xA4` (формат `0xA0|h` подтверждён).

R3 (bitfield): `bit0 Sleep`, `bit1 Power`, `bits2-3 Swing (10=off, 01=on)`,
`bit4 AirFlow`, `bits5-6 Fan (00=Auto, 01=High, 10=Med, 11=Low)`.

> **Подтверждено:** различия R3 `0x3A / 0x7A / 0x1A` — это скорость вентилятора,
> НЕ база режима (DRY/FAN/AUTO=0x7A→Low, COOL=0x3A→High, HEAT=0x1A→Auto).
> «Несоответствие HEAT 0x7A vs 0x1A» закрыто — не нужно патчить.

Swing: `R2=04`; базы `AUTO/HEAT=7A`, `COOL/FAN=3A`; ON = `R3 ^ 0x0C` (биты 2-3);
R4/R5 как у текущего режима/температуры. Решение: свинг-бит ставить во все кадры
при включённой шторке (как оригинальный пульт). DRY — шторки нет.

Отличия от оригинального Goodweather (IRremoteESP8266): pad `55` (GW `D5`),
полярность space инверсна (long=1 у нас, long=0 у GW), swing только on/off
(GW: fast/slow/off).

## Текущее состояние кода

Реализовано (все запушено на master):
- power, mode, temp±, swing (cmd `0x04`, `last_swing_`)
- fan speed (cmd `0x05`, `last_fan_`, R3-битфилд, дефолт Auto)
- sleep (preset `Sleep`, bit0 R3), turbo (preset `Boost`, bit3 R1), light (custom `light`, bit0 R1)
- **приём ИК от пульта → синхронизация HA** (`on_receive`, `receiver_id`)
- **полярность-независимый декодер** (`abs()`-матчинг, фикс инверсии IRM-3638)
- **hvac_action** — mode→action маппинг (`f127b53`)

Git-коммиты:
- `af93392` — initial (swing)
- `3f2e9a7` — fan speed (cmd `0x05`, R3-битфилд)
- `6bd506a` — sleep/turbo/light (пресеты)
- `2dc501f` — приём ИК (`on_receive`, `receiver_id`)
- `ae38864` — диагностика `ESP_LOGD` в `on_receive`
- `e385eea` — дамп первых raw-значений
- `20fc0a0` — **fix: полярность-независимый декодер** (abs-матчинг)
- `463e418` — docs: обновление PLAN.md
- `f127b53` — hvac_action из mode + (ошибочный) перенос Light на бит4
- `H2` — **revert: Light = R1 бит0** (живые кадры cmd `0x0B`), бит4 = таймерный флаг

Недочёты:
- Timer отложен (cmd `0x0D`, часы в `R0=0xA0|h`, R1 бит4 — флаг).
- **Приём ИК проверен живьём 2026-08-14**: 23/23 кадра `accepted`, 0 `reject`
  (все режимы, temp, fan, swing, turbo, sleep, power). В том же логе найден и
  исправлен Light=бит4.

## Открытые решения

1. ~~**Дефолт вентилятора**~~ → **Решено: Auto**. R3 пересобран в битфилд.
2. ~~**Git**~~ → репозиторий создан: `night-gnida/rapid-ac`.
3. ~~**Метод съёмки**~~ → Pronto-строки из лога `remote.pronto` в ESPHome.
4. **Кнопки**: AirFlow / Hold — на пульте отсутствуют (по уточнению пользователя).
5. ~~**SP-EUC01 (розетка-энергомонитор)**~~ → **Отложено**. План готов: 4 замера
   мощности (выкл / FAN / COOL компрессор / COOL idle) → template sensor
   «компрессор работает» → уведомление «IR не дошёл» → Energy Dashboard.
6. **Датчик комнатной t°** → **Решено: аппаратный** (тип датчика и свободный GPIO
   уточняются; заняты GPIO5 приём, GPIO14 передатчик, GPIO0 boot).
7. **hvac_action** → в плане: маппинг mode→action в компоненте (см. Этап 8).

## Этап 0 — первичные действия

- [x] Git init + первый коммит, remote `night-gnida/rapid-ac` (public).
- [x] Тег `v0.2` (sleep/turbo/light).
- [x] Серверный конфиг переключён на `github://night-gnida/rapid-ac`.
- [x] `receiver_id: ir_rx`, `transmitter_id: ir_tx` в конфиге.

## Этап 1 — съёмка режимов с пульта (пользователь)

База для всех: кондиционер включён, **COOL 24°C**, шторка OFF, всё остальное OFF.
Одно нажатие = один кадр, пауза ~2 с. Метка у каждого кадра + состояние дисплея.

| № | Кнопка | Кадры | Ожидаемый признак |
|---|--------|-------|-------------------|
| 1 ⭐ | FAN-цикл | Auto→Low→Med→High = 4 | `R2=05`, биты 5-6 R3 (`00/01/10/11`) |
| 2 | Swing вкл/выкл | 2 | `R2=04`, базы, `^0C` (свежая сверка) |
| 3 | Sleep | 2 | `R2=09`, bit0 R3 |
| 4 | Turbo | 2 | `R2=0A`, bit3 R1 |
| 5 | Light | 2 | `R2=0B`, bit0 R1 |
| 6 | Timer | вкл, +×2, выкл = 4 | `R2=0D`, `R0=0xA0+h`, R1 бит4 (частично снято живьём) |
| 7 | AirFlow / Hold | по 2 (если есть) | `R2=07/08` (bit4 R3) |

Формат передачи: raw-строка из лога (`Received Raw: ...`) по одной на кадр →
`write_raw.py` (блок `RAW`) → `final_decode2.py` → таблица.

## Этап 2 — Fan speed (R2=0x05) ✅

- [x] `rapid_ac.h`: fan-маска в конструктор (`CLIMATE_FAN_AUTO/LOW/MEDIUM/HIGH`),
  поле `last_fan_`.
- [x] `rapid_ac.cpp`:
  - `transmit_state()`: смена fan при power on → `command = 0x05`.
  - `send_frame_()`: R3 собирать битфилдом
    `(sleep)|(power<<1)|(swing<<2)|(1<<4)|(fan<<5)`.
- [ ] Проверка на сервере; сравнить отправленные карты с захватами.

## Этап 3 — Sleep / Turbo / Light ✅

- [x] R1 битфилд: `(Light<<0)|(Turbo<<3)`; R3 bit0 = sleep.
  (Live-тесты: Light=бит0, Turbo=бит3, бит4=таймерный флаг.)
- [x] Команды `09 / 0A / 0B`, toggle-логика, `last_sleep_`, `last_turbo_`, `last_light_`.
- [x] Пресеты HA: `Sleep`, `Boost`, custom `light`.

## Этап 4 — Timer (cmd `0x0D`)

- **Отложен.** Живой захват 2026-08-14: команда **`0x0D`** (не `0x06` — то было
  предположение по Goodweather), часы в `R0 = 0xA0 | h` (4ч=`A4` подтверждено),
  R1 бит4 = таймерный флаг. Реализация через `number`/`switch` — попробуем позже.

## Этап 5 — HEAT / контроль

- После fan-реализации подтвердить: `0x7A / 0x1A` ≈ fan Low/Auto, режим ни при чём.
- Перепроверить метку «heat24»: по R4=`0x8F` это HEAT **31°C**.

## Этап 7 — Приём ИК (синхронизация с пультом)

- [x] `on_receive` декодер в `rapid_ac.cpp` (хедер/биты/футер/парность).
- [x] Парсинг power/mode/temp/fan/swing + presets sleep/turbo/light.
- [x] Синхронизация `last_*` + `publish_state()`.
- [x] `climate.py`: schema → `climate_ir_with_receiver_schema`.
- [x] Серверный конфиг: `id: ir_rx`/`ir_tx` + `receiver_id: ir_rx`.
- [x] **Fix полярности** (`20fc0a0`): IRM-3638 active-low → marks отрицательные →
  `expect_mark` всегда падал. Заменён на `abs()`-матчинг со ручным сдвигом индекса.
- [x] **Живая проверка пройдена 2026-08-14**: 23/23 `accepted`, 0 `reject`.
  Подтверждены: все 5 режимов + off, temp 20–26, fan Auto/High/Low, swing,
  Turbo (`R1=0x08`), Sleep (`R3 bit0`), power. Попутно найдено: Light = R1 бит4
  (был бит 0) — исправлено в `f127b53`.

## Этап 6 — документация

- `README.md`: побитовые таблицы R1/R3, все команды, fan/swing/sleep/turbo/light,
  дефолт вентилятора.
- Обновлять `PLAN.md` по мере завершения этапов.

## Этап 8 — пост-тестовые улучшения (план 2026-08-14)

Порядок после живого теста приёма ИК:

1. **YAML-батч**: `dump: all` → `raw` (снять нагрузку/предупреждение
   "remote_receiver took a long time"), добавить `captive_portal`,
   `wifi_signal`, `uptime`.
2. ~~**hvac_action**~~ ✅ `f127b53`: mode→action (COOL→COOLING, HEAT→HEATING,
   DRY→DRYING, FAN→FAN, OFF→OFF, AUTO/HEAT_COOL→IDLE). Зеркалит выбранный
   режим, реальный компрессор IR не видит.
3. **Аппаратный датчик t°**: sensor-платформа + `sensor:` в climate →
   `current_temperature` в HA/HomeKit. ClimateIR использует датчик только
   для отображения (display-only), для управления — автоматизации HA.
   Ждём тип датчика и свободный GPIO.
4. **Кнопка GPIO13** → `climate.toggle` (только если физическая кнопка на
   плате есть — бинарный сенсор замечен в серверном конфиге).
5. **DRY/FAN_ONLY в HomeKit**: template switch в HA — обход ограничения
   HeaterCooler (только Off/Heat/Cool/Auto).
6. **Timer** (cmd `0x0D`): метод `set_timer(hours)` + number-сущность.
7. **Автоматизации HA** (по желанию): расписание, сцены, геофенсинг.
8. **SP-EUC01** (отложено, см. Открытые решения п.5).

## Файлы

`rapid_ac.h`, `rapid_ac.cpp`, `README.md`, `PLAN.md`, (git), серверные конфиги.