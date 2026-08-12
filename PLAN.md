# Rapid AC — план развития (архив чата, обновлён 2026-08-12)

## Контекст

YTF Remote IR (TYWE3S/ESP8266, плата `esp01_1m`) прошит кастомным ESPHome-компонентом
`rapid_ac` — Wi-Fi ИК-пульт кондиционера Rapid (клон Goodweather).

- Локально: `D:\MY Trash\programing\rapid_ac\`
  - `components/rapid_ac/{__init__.py, climate.py, rapid_ac.h, rapid_ac.cpp}`
  - `esphome.yaml`, `secrets.yaml(.example)`, `README.md`, `PLAN.md`
- Сервер HA: `/config/esphome/ir-remote-bluster.yaml`,
  `/config/esphome/components/rapid_ac/`
- ESPHome 2026.7.4 (локально не установлен — компиляция только на сервере)

## Протокол (верифицировано по захватам)

- Кадр: 6 реальных байт → 12 wire `[~Ri, Ri]`, LSB-first, 38 кГц.
- Тайминги (мкс): HDR `6200/7480`, бит `560`, 1 → space `1640`, 0 → space `550`,
  футер `560/7480` + финальный mark `560`, зазор ~`10125`.
- Поля: `R0=00`, `R1=(Light<<0 | Turbo<<3)`, `R2=Command`, `R3=битфилд`,
  `R4=(mode_code<<5)|(temp−16)`, `R5=55` (pad). mode: AUTO=0 COOL=1 DRY=2 FAN=3 HEAT=4.

| R2 | Команда |
|----|---------|
| 0x00 | Power |
| 0x01 | Mode |
| 0x02 / 0x03 | Temp+ / Temp− |
| 0x04 | Swing |
| 0x05 | Fan speed |
| 0x06 | Timer |
| 0x07 | AirFlow |
| 0x08 | Hold |
| 0x09 | Sleep |
| 0x0A | Turbo |
| 0x0B | Light |

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

Реализовано: power, mode, temp±, swing (cmd 0x04, маска `{OFF, VERTICAL}`,
`last_swing_`, `R3^0x0C` во всех кадрах при включённой шторке).

Недочёты:
- `send_frame_()` собирает R3 константами по режиму → шлёт fan вслепую
  (COOL=High, остальное=Low), игнорируя желаемую скорость.
- Не реализованы: fan speed, sleep, turbo, light, timer, airflow, hold.
- README описан не полностью (нет R1/R3-таблиц и новых команд).
- Проект вне git → нет версионирования/откатов.

## Открытые решения

1. **Дефолт вентилятора**: Auto (0b00) или сохранить текущее (COOL=High, иначе Low)?
2. **Git**: инициализировать репозиторий в `rapid_ac` для поэтапных правок с откатом?
3. **Метод съёмки**: как конвертируются исходники из лога (raw → Pronto)?
4. **Кнопки**: есть ли на пульте AirFlow / Hold?

## Этап 0 — первичные действия

- [ ] Синк `rapid_ac.{h,cpp}` на сервер, пересборка, проверка swing вживую.
- [ ] (рекомендую) `git init` + первый коммит «swing», для возможности отката.

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
| 6 | Timer | вкл, +×2, выкл = 4 | `R2=06` (меняются ли поля?) |
| 7 | AirFlow / Hold | по 2 (если есть) | `R2=07/08` (bit4 R3) |

Формат передачи: raw-строка из лога (`Received Raw: ...`) по одной на кадр →
`write_raw.py` (блок `RAW`) → `final_decode2.py` → таблица.

## Этап 2 — Fan speed (R2=0x05)

- `rapid_ac.h`: fan-маска в конструктор (`CLIMATE_FAN_AUTO/LOW/MEDIUM/HIGH`),
  поле `last_fan_`.
- `rapid_ac.cpp`:
  - `transmit_state()`: смена fan при power on → `command = 0x05`.
  - `send_frame_()`: R3 собирать битфилдом
    `(sleep)|(power<<1)|(swing<<2)|(1<<4)|(fan<<5)`.
- Проверка на сервере; сравнить отправленные карты с захватами.

## Этап 3 — Sleep / Turbo / Light

- R1 битфилд: `(light<<0)|(turbo<<3)`; R3 bit0 = sleep.
- Команды `09 / 0A / 0B`, toggle-логика, `last_sleep_`, `last_turbo_`, `last_light_`.

## Этап 4 — Timer (R2=0x06)

- По Goodweather — команда без изменения полей. Доработать, если захваты покажут иначе.

## Этап 5 — HEAT / контроль

- После fan-реализации подтвердить: `0x7A / 0x1A` ≈ fan Low/Auto, режим ни при чём.
- Перепроверить метку «heat24»: по R4=`0x8F` это HEAT **31°C**.

## Этап 6 — документация

- `README.md`: побитовые таблицы R1/R3, все команды, fan/swing/sleep/turbo/light,
  дефолт вентилятора.
- Обновлять `PLAN.md` по мере завершения этапов.

## Файлы

`rapid_ac.h`, `rapid_ac.cpp`, `README.md`, `PLAN.md`, (git), серверные конфиги.