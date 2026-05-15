# MKS TinyBee V1.0 — карта GPIO, силовая часть и прошивка сеялки

**Версия:** 1.0  
**Дата:** 2026-05-15  
**Плата:** MakerBase MKS TinyBee V1.0 (ESP32-WROOM-32U, 8 MB Flash)  
**Цель:** собственная прошивка (ESP-IDF) для машины автовысева семян в кассеты

---

## Содержание

1. [Кратко о плате](#1-кратко-о-плате)
2. [Архитектура выводов (критично)](#2-архитектура-выводов-критично)
3. [I2S → сдвиговый регистр (24 бита)](#3-i2s--сдвиговый-регистр-24-бита)
4. [Прямые GPIO ESP32](#4-прямые-gpio-esp32)
5. [Разъёмы EXP1 / EXP2 / SD](#5-разъёмы-exp1--exp2--sd)
6. [Силовая часть: как управлять](#6-силовая-часть-как-управлять)
7. [Раскладка под сеялку (рекомендация)](#7-раскладка-под-сеялку-рекомендация)
8. [Карта конфигов и исходников](#8-карта-конфигов-и-исходников)
9. [Разработка на ESP-IDF](#9-разработка-на-esp-idf)
10. [Скелет `board_pins.h`](#10-скелет-board_pinsh)
11. [Ограничения и безопасность](#11-ограничения-и-безопасность)
12. [Ссылки](#12-ссылки)

---

## 1. Кратко о плате

| Параметр | Значение |
|----------|----------|
| MCU | ESP32-WROOM-32U, 240 MHz |
| Flash | 8 MB |
| Питание | DC **12–24 V** |
| Оси | 5 осей, 6 моторов (dual Z параллельно) |
| Драйверы | STEP/DIR, DIP microstep |
| Wi‑Fi | Да (веб в экосистеме Marlin/MKS) |
| Прошивка «из коробки» | Marlin 2.0.x |

**Важно для сеялки:** плата изначально — контроллер 3D-принтера. Marlin «из коробки» не подходит для логики сетки кассеты и дозатора; нужна **своя прошивка на ESP-IDF**, с драйвером I2S-расширителя.

---

## 2. Архитектура выводов (критично)

На TinyBee **нет прямых GPIO** на STEP/DIR/ENABLE, нагреватели и вентиляторы.

Почти вся силовая логика идёт через:

```text
ESP32 I2S (GPIO 25, 26, 27)
    → 3 × 74HC595 (24 бита, MAX_EXPANDER_BITS = 24)
        → драйверы шаговиков (STEP / DIR / ENABLE)
        → MOSFET H-BED, H-E0, H-E1
        → MOSFET FAN1, FAN2
        → BEEPER (EXP1)
```

**Без инициализации I2S-стрима шаговики и силовые выходы не работают.**

### Линии I2S (не переназначать)

| Сигнал | GPIO |
|--------|------|
| I2S_BCK | **25** |
| I2S_WS | **26** |
| I2S_DATA | **27** |

### Два номера одного и того же бита

| Система | Обозначение | Формула |
|---------|-------------|---------|
| Marlin | `IO128` … `IO151` | Бит расширителя = `IO & 0x7F` |
| grblHAL | `I2SO(n)` | Бит `n` = Marlin `IO(128+n)` |
| Marlin HAL | `IS_I2S_EXPANDER_PIN(IO)` | `IO >= 128` (бит 7 установлен) |

Пример: `I2SO(17)` = Marlin `IO145` = **H-E0**.

---

## 3. I2S → сдвиговый регистр (24 бита)

Полная таблица (источники: Marlin `pins_MKS_TINYBEE.h`, grblHAL `mks_tinybee_1_0_map.h`).

| Бит `I2SO(n)` | Marlin `IO` | Разъём / функция | Тип |
|---------------|-------------|------------------|-----|
| 0 | 128 | X **ENABLE** | Шаговик |
| 1 | 129 | X **STEP** | Шаговик |
| 2 | 130 | X **DIR** | Шаговик |
| 3 | 131 | Y **ENABLE** | Шаговик |
| 4 | 132 | Y **STEP** | Шаговик |
| 5 | 133 | Y **DIR** | Шаговик |
| 6 | 134 | Z **ENABLE** | Шаговик |
| 7 | 135 | Z **STEP** | Шаговик |
| 8 | 136 | Z **DIR** | Шаговик |
| 9 | 137 | E0 **ENABLE** | Шаговик (4-я ось) |
| 10 | 138 | E0 **STEP** | Шаговик |
| 11 | 139 | E0 **DIR** | Шаговик |
| 12 | 140 | E1 **ENABLE** | Шаговик (5-я ось / dual Z) |
| 13 | 141 | E1 **STEP** | Шаговик |
| 14 | 142 | E1 **DIR** | Шаговик |
| 15 | 143 | *(не описан в Marlin — резерв)* | — |
| 16 | 144 | **H-BED** (стол) | Силовой MOSFET |
| 17 | 145 | **H-E0** (хотенд 0) | Силовой MOSFET |
| 18 | 146 | **H-E1** (хотенд 1) | Силовой MOSFET |
| 19 | 147 | **FAN1** | PWM MOSFET |
| 20 | 148 | **FAN2** | PWM MOSFET |
| 21 | 149 | **BEEPER** | Цифровой / звук |
| 22–23 | — | Резерв в цепочке | — |

### Результат сверки распайки

Распайка сверена с официальным README / PinMap MakerBase, схемой V1.0_003, Marlin `pins_MKS_TINYBEE.h` и grblHAL `mks_tinybee_1_0_map.h`.

- I2S: `BCK=25`, `WS=26`, `DATA=27`.
- Шаговики: `IO128..IO142` соответствуют `I2SO(0..14)`.
- `IO143 / I2SO(15)` в Marlin/README не назначен; на схеме это свободный `Q143`.
- Силовые выходы: `H-BED=IO144`, `H-E0=IO145`, `H-E1=IO146`, `FAN1=IO147`, `FAN2=IO148`, `BEEPER=IO149`.
- `IO150/IO151` на схеме есть как `OUT-Q150/OUT-Q151`, но в README/Marlin не выведены как штатные функции; оставить как резерв.
- Endstops, MT_DET, ADC, EXP1, EXP2, SD и UART2 совпадают с официальным README и Marlin.

### Полярность ENABLE

В Marlin для TinyBee: `X_ENABLE_ON 0` (и аналогично Y/Z/E) → драйвер шаговика **включён при LOW** на линии ENABLE.

---

## 4. Прямые GPIO ESP32

| GPIO | Назначение на плате | Вход/выход | Примечание |
|------|---------------------|------------|------------|
| **33** | X_STOP | Вход | Концевик X, homing |
| **32** | Y_STOP | Вход | Концевик Y |
| **22** | Z_STOP | Вход | Концевик Z |
| **35** | MT_DET | Только вход | Датчик материала / цифровой датчик |
| **36** | TH1 | ADC (вход) | Аналоговый датчик (оптика семени и т.п.) |
| **34** | TH2 **или** SD_DET | ADC / вход | **Джампер** на плате: термистор vs SD |
| **39** | TB | ADC (вход) | Второй аналоговый канал |
| **2** | SERVO / 3D Touch | Выход | LEDC PWM — серво, соленоид |
| **1** | UART0 TX | — | USB-serial (прошивка/лог) |
| **3** | UART0 RX | — | USB-serial |
| **25, 26, 27** | I2S | Выход | Зарезервированы под расширитель |

### ADC (Marlin)

- `TEMP_0` → GPIO **36**
- `TEMP_1` → GPIO **34** (нужен джампер R6/R7 для TH2)
- `TEMP_BED` → GPIO **39**
- Опорное напряжение в Marlin: `ADC_REFERENCE_VOLTAGE 2.565` V

### GPIO только input (нельзя использовать как выход)

**34, 35, 36, 39** — на ESP32 только вход; для ESP-IDF не вызывать `gpio_set_direction(..., GPIO_MODE_OUTPUT)`.

---

## 5. Разъёмы EXP1 / EXP2 / SD

Если **дисплей и SD не используются**, линии EXP можно занять под кнопки, UART, датчики (с осторожностью на GPIO 0).

### EXP1

| EXP1 | GPIO / I2SO | Назначение |
|------|-------------|------------|
| 1 | I2SO(21) / IO149 | BEEPER |
| 2 | 13 | BTN_ENC |
| 3 | 21 | LCD_EN |
| 4 | 4 | LCD_RS |
| 5 | 0 | LCD_D4 |
| 6 | 16 | LCD_D5 |
| 7 | 15 | LCD_D6 |
| 8 | 17 | LCD_D7 |

### EXP2

| EXP2 | GPIO | Назначение |
|------|------|------------|
| 1 | 19 | SPI MISO |
| 2 | 18 | SPI SCK |
| 3 | 14 | BTN_EN1 |
| 4 | 5 | SD_CS |
| 5 | 12 | BTN_EN2 |
| 6 | 23 | SPI MOSI |
| 7 | 34 | SD_DET (по умолчанию) |
| 8 | — | RESET |

### UART2 (README MKS)

| Сигнал | GPIO |
|--------|------|
| TXD2 | 17 |
| RXD2 | 16 |

Совпадает с линиями LCD_D7 / LCD_D5 — конфликт при использовании дисплея.

### SPI / TF (по умолчанию ESP32)

| Сигнал | GPIO |
|--------|------|
| CS | 5 |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| DET | 34 (джампер с TH2) |

---

## 6. Силовая часть: как управлять

### Блок-схема питания

```text
12–24 V IN → MKS TinyBee
    ├─ DC-DC → ESP32 (3.3 V логика)
    ├─ Разъёмы драйверов X/Y/Z/E (STEP, DIR, EN ← I2SO 0..14)
    ├─ H-BED, H-E0, H-E1 ← I2SO 16..18 (MOSFET, рассчитаны на ТЭН)
    ├─ FAN1, FAN2 ← I2SO 19..20 (MOSFET + программный PWM)
    └─ 2 × «power output» XH2.54-2p — уточнить по SCH.pdf
```

### Шаговые двигатели

| Действие | Как |
|----------|-----|
| Инициализация | `i2s_out_init()` (grblHAL) или `i2s_init()` (Marlin) |
| Установка бита | `i2s_out_write(bit, level)` + commit/push в буфер |
| **Запрещено** | `gpio_set_level(145, …)` — такого GPIO нет |

Импульсы STEP в Marlin генерируются **внутри I2S DMA-потока** (задача `stepperTask`), не «вручную» с задержками `delay_us` на GPIO.

### «Нагреватели» H-BED / H-E0 / H-E1 (I2SO 16–18)

| Канал | I2SO | Marlin IO | Типичная нагрузка в принтере |
|-------|------|-----------|------------------------------|
| Стол | 16 | 144 | Heated bed |
| Hotend 0 | 17 | 145 | HE0 |
| Hotend 1 | 18 | 146 | HE1 |

**Для сеялки:** реле вакуума, клапан, вибромотор (через реле), НЕ индуктивная нагрузка напрямую без защиты.

| Режим | API |
|-------|-----|
| Вкл/выкл | `i2s_out_write(16..18, 1/0)` + обновление буфера |
| PWM | В Marlin — программный PWM в `i2s_push_sample()` (~4 µs на тик); для вибро можно использовать **FAN** (19/20) |

### Вентиляторы FAN1 / FAN2 (I2SO 19–20)

| Канал | I2SO | Marlin IO |
|-------|------|-----------|
| FAN1 | 19 | 147 |
| FAN2 | 20 | 148 |

Подходят для **PWM**: вибропитатель, регулируемый вакуум (если MOSFET рассчитан на нагрузку).

В Marlin HAL (`set_pwm_duty` для `pin > 127`):

- частота задаётся через `pwm_cycle_ticks = 1_000_000 / f / 4` (тик 4 µs);
- duty — числом тиков HIGH в цикле.

### Серво GPIO 2

Прямой **LEDC PWM** ESP32 — механика иглы, микро-серво, щуп (оригинально `SERVO0_PIN 2`).

### Концевики

- Разъёмы **X_STOP, Y_STOP, Z_STOP** → GPIO 33, 32, 22.
- На ревизии платы **V1.0_003** добавлена защита Schottky на концевиках.
- Типично: NC к GND, срабатывание = **LOW** (проверить на своей механике).

### Аналоговые входы

Были под NTC 100k; для сеялки:

- оптодатчик «семя есть»;
- датчик вакуума (0–3.3 V через делитель).

Использовать `adc1` на каналах GPIO 36, 39, 34 (с учётом джампера).

---

## 7. Раскладка под сеялку (рекомендация)

Типовой цикл: **Home → ячейка (X,Y) → Z → захват семени → проверка → укладка → следующая ячейка**.

### Предлагаемое назначение

| Функция машины | Канал платы |
|----------------|-------------|
| Ось X портала | I2SO **0–2** |
| Ось Y | I2SO **3–5** |
| Ось Z (игла / сопло) | I2SO **6–8** |
| Вибропитатель (шаговый) или 4-я ось | I2SO **9–11** (E0) |
| Реле вакуума | I2SO **17** (H-E0) или **19** (FAN1) |
| Клапан сброса вакуума | I2SO **20** (FAN2) или **18** (H-E1) |
| Home X / Y / Z | GPIO **33 / 32 / 22** |
| Датчик семени (цифровой) | GPIO **35** (MT_DET) |
| Датчик семени (аналог) | GPIO **36** (ADC) |
| Серво / механика иглы | GPIO **2** |
| Звуковая индикация | I2SO **21** (beeper) |
| Отладка / прошивка | UART0 (USB), Wi‑Fi ESP32 |
| Резерв 5-я ось | I2SO **12–14** (E1) |

### Варианты механики (кратко)

| Схема | Оси | Комментарий |
|-------|-----|-------------|
| Портал X/Y + Z | 3–4 | Универсально, разные кассеты |
| Стол движется, головка фиксирована | 1–2 | Проще, хуже на большой площади |
| Барабан + подача кассеты | 1–2 | Быстро в ряд, сложнее сетка |

### Параметры кассеты (задать в прошивке)

- `rows`, `cols`
- `pitch_x_mm`, `pitch_y_mm`
- `origin_x_mm`, `origin_y_mm`
- допуск позиции над лункой (например ±0.5–1 mm)

---

## 8. Карта конфигов и исходников

| Файл | Репозиторий | Назначение |
|------|-------------|------------|
| `pins_MKS_TINYBEE.h` | [Marlin 2.1.2.1](https://github.com/MarlinFirmware/Marlin/blob/2.1.2.1/Marlin/src/pins/esp32/pins_MKS_TINYBEE.h) | Номера IO, I2S, ADC, EXP |
| `Conditionals_adv.h` | Marlin `HAL/ESP32/inc/` | `MAX_EXPANDER_BITS = 24` (3× HC595) |
| `i2s.cpp`, `fastio.h`, `HAL.cpp` | Marlin `HAL/ESP32/` | I2S stream, `WRITE(IO>=128)`, PWM на expander |
| `Configuration.h` | [MKS-TinyBee/firmware/mks tinybee marlin/](https://github.com/makerbase-mks/MKS-TinyBee/tree/main/firmware/mks%20tinybee%20marlin) | Готовый конфиг Marlin |
| `mks_tinybee_1_0_map.h` | [grblHAL/ESP32](https://github.com/grblHAL/ESP32/blob/master/main/boards/mks_tinybee_1_0_map.h) | **Рекомендуемая база для ESP-IDF** |
| `i2s_out.h`, `i2s_out.c` | grblHAL/ESP32 | Драйвер I2S + сдвиговый регистр |
| `use_i2s_out.h` | grblHAL/ESP32 | Макросы `I2SO(n)`, `DIGITAL_OUT` |
| `README.md` | [MKS-TinyBee](https://github.com/makerbase-mks/MKS-TinyBee) | Pinmap в таблице, картинки |
| `MKS TinyBee V1.0_003 PIN.pdf` | MKS hardware | Разъёмы (официальная схема) |
| `MKS TinyBee V1.0_003 SCH.pdf` | MKS hardware | Принципиальная схема, MOSFET |
| `MKS TinyBee V1.0 User Manual.pdf` | MKS repo | Руководство пользователя |

### Соответствие grblHAL ↔ Marlin (силовые / вспомогательные)

| grblHAL | I2SO | Назначение на плате |
|---------|------|---------------------|
| AUXOUTPUT0 | GPIO 2 | Spindle PWM / серво |
| AUXOUTPUT1 | 18 | HE1 |
| AUXOUTPUT2 | 17 | HE0 |
| AUXOUTPUT3 | 19 | FAN1 / flood |
| AUXOUTPUT4 | 20 | FAN2 / mist |
| AUXINPUT1 | GPIO 35 | MT_DET |
| AUXINPUT0 | GPIO 39 | TB (ADC) |
| AUXINPUT2 | GPIO 36 | TH1 (ADC) |
| AUXINPUT3 | GPIO 34 | TH2 / cycle (ADC) |

---

## 9. Разработка на ESP-IDF

### Рекомендуемый путь

1. **Не портировать Marlin целиком** — слишком тяжёлый стек для сеялки.
2. Взять **`i2s_out` + `mks_tinybee_1_0_map.h`** из [grblHAL/ESP32](https://github.com/grblHAL/ESP32).
3. Проверить на grblHAL с `BOARD_MKS_TINYBEE_V1`: вращение осей, щелчок реле на HE/FAN.
4. В чистом ESP-IDF v5.x — FSM сеялки поверх `i2s_out`.

### Минимальный стек модулей

```text
board_init()          — GPIO концевиков, ADC, UART0
i2s_out_init()        — I2S + 24-битный сдвиговый регистр
board_io              — обёртка i2s_out_write / PWM / ENABLE
motion                — planner + step generation (grblHAL или свой)
sower_fsm             — сетка кассеты, вакуум, retry, лог
wifi_web (опционально)— настройка сетки, калибровка
```

### Псевдокод силового выхода

```c
#include "i2s_out.h"

void board_set_i2so(uint8_t bit, int level)
{
    i2s_out_write(bit, level);
    i2s_out_push_sample(1);  /* или i2s_out_commit — по API grblHAL */
}

void vacuum_on(void)  { board_set_i2so(17, 1); }  /* H-E0 */
void vacuum_off(void) { board_set_i2so(17, 0); }
```

### Псевдокод FSM сеялки

```text
IDLE
  → HOME_ALL
  → FOR each cell (row, col):
       MOVE_TO_CELL
       PICK_SEED          /* вакуум ON, вибро, Z */
       VERIFY_SENSOR      /* GPIO35 или ADC36 */
       IF fail: RETRY (N times) ELSE SKIP + log
       PLACE_SEED         /* Z down, vacuum OFF */
  → DONE
```

### Альтернатива: grblHAL как основа

- Собрать grblHAL с картой `MKS_TINYBEE_V1`.
- Добавить custom M-code / plugin для цикла высева.
- Плюс: готовое движение; минус: привязка к экосистеме g-code.

---

## 10. Скелет `board_pins.h`

```c
#pragma once

/* --- I2S shift register (do not remap) --- */
#define TB_I2S_BCK    25
#define TB_I2S_WS     26
#define TB_I2S_DATA   27

/* --- Limits (active low typical) --- */
#define TB_PIN_X_END  33
#define TB_PIN_Y_END  32
#define TB_PIN_Z_END  22

/* --- Digital / analog inputs --- */
#define TB_PIN_MT_DET      35
#define TB_PIN_ADC_SEED    36   /* TH1 */
#define TB_PIN_ADC_AUX     39   /* TB */
#define TB_PIN_ADC_TH2     34   /* jumper: TH2 vs SD_DET */

/* --- Direct output --- */
#define TB_PIN_SERVO       2

/* --- I2SO: steppers (EN, STEP, DIR per axis) --- */
#define TB_I2SO_X_EN    0
#define TB_I2SO_X_STEP  1
#define TB_I2SO_X_DIR   2
#define TB_I2SO_Y_EN    3
#define TB_I2SO_Y_STEP  4
#define TB_I2SO_Y_DIR   5
#define TB_I2SO_Z_EN    6
#define TB_I2SO_Z_STEP  7
#define TB_I2SO_Z_DIR   8
#define TB_I2SO_E0_EN   9
#define TB_I2SO_E0_STEP 10
#define TB_I2SO_E0_DIR  11
#define TB_I2SO_E1_EN   12
#define TB_I2SO_E1_STEP 13
#define TB_I2SO_E1_DIR  14

/* --- I2SO: power outputs (seed sower) --- */
#define TB_I2SO_HE_BED  16
#define TB_I2SO_HE0      17   /* vacuum relay */
#define TB_I2SO_HE1      18   /* valve / spare */
#define TB_I2SO_FAN1     19   /* PWM — vibro */
#define TB_I2SO_FAN2     20   /* PWM — spare */
#define TB_I2SO_BEEPER   21

/* Marlin logical pin = 128 + I2SO bit */
#define TB_MARLIN_IO(bit)  (128 + (bit))
```

---

## 11. Ограничения и безопасность

### Электрика

| Правило | Причина |
|---------|---------|
| E-stop разрывает **силовую** цепь 24 V или ENABLE драйверов | GPIO не заменяет аппаратный stop |
| Реле/клапаны — с **flyback-диодом** | I2SO → MOSFET → индуктивная нагрузка |
| Не нагружать HE выходы «вслепую» | Рассчитаны на ТЭН; смотреть ток в SCH.pdf |
| Общий GND платы и внешних реле | Иначе ложные срабатывания |

### GPIO

| GPIO | Риск |
|------|------|
| **0** | Strapping BOOT — не использовать как кнопку без схемы |
| **6–11** | На некоторых модулях связаны с flash (WROOM-32U обычно OK) |
| **34–39** | Только вход |
| **25–27** | Обязательны для I2S |

### Прошивка

| Ошибка | Последствие |
|--------|-------------|
| `gpio_set_level(145, 1)` | Не работает — нет такого пина |
| Нет `i2s_out_init()` | Все I2SO «мёртвые» |
| STEP через `delay_us` на GPIO | На TinyBee STEP только через I2S |
| Wi‑Fi + критичный timing без тестов | Возможны джиттеры; выносить motion в отдельную задачу/ядро |

### Чек-лист перед первым включением

- [ ] Питание 12–24 V, полярность
- [ ] Драйверы: ток, microstep, ENABLE polarity
- [ ] Концевики: NC/NO, логика LOW/HIGH
- [ ] Джампер GPIO34: TH2 vs SD_DET
- [ ] I2S инициализирован, тест «щёлк» реле на HE0
- [ ] Homing всех осей до рабочего цикла
- [ ] E-stop проверен аппаратно

---

## 12. Ссылки

### Официальные (MKS)

- Репозиторий: https://github.com/makerbase-mks/MKS-TinyBee  
- Official README / PinMap: https://github.com/makerbase-mks/MKS-TinyBee/blob/main/README.md  
- Hardware V1.0_003 PDFs: https://github.com/makerbase-mks/MKS-TinyBee/tree/main/hardware/MKS%20TinyBee%20V1.0_003  
- PIN.pdf: https://github.com/makerbase-mks/MKS-TinyBee/raw/main/hardware/MKS%20TinyBee%20V1.0_003/MKS%20TinyBee%20V1.0_003%20PIN.pdf  

### Marlin

- `pins_MKS_TINYBEE.h`: https://github.com/MarlinFirmware/Marlin/blob/2.1.2.1/Marlin/src/pins/esp32/pins_MKS_TINYBEE.h  
- `i2s.cpp`: https://github.com/MarlinFirmware/Marlin/blob/2.1.2.1/Marlin/src/HAL/ESP32/i2s.cpp  

### grblHAL (база для ESP-IDF)

- `mks_tinybee_1_0_map.h`: https://github.com/grblHAL/ESP32/blob/master/main/boards/mks_tinybee_1_0_map.h  
- `i2s_out.h`: https://github.com/grblHAL/ESP32/blob/master/main/i2s_out.h  

### Локальные артефакты (этот каталог)

- Дамп flash (если есть): `dumps/esp32_ttyUSB1_flash_8mb_*.bin`

---

## История документа

| Версия | Дата | Изменения |
|--------|------|-----------|
| 1.0 | 2026-05-15 | Первая сборка: pinmap, силовая часть, сеялка, ESP-IDF |

---

*Документ не является частью MQTT/NodeConfig hydro2.0. Для интеграции с экосистемой Hydro — отдельный проект узла и спецификация.*
