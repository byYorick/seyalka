# MKS TinyBee V1.0 documentation bundle

Локальная копия документации и pinmap-референсов для MakerBase MKS TinyBee V1.0.
Файлы скачаны 2026-05-15 из публичных upstream-источников.

## Официальные документы MakerBase

| Файл | Назначение | Источник |
|------|------------|----------|
| `README.upstream.md` | Официальный README, board parameters, PinMap, EXP/SD/UART2 | https://github.com/makerbase-mks/MKS-TinyBee/blob/main/README.md |
| `MKS_TinyBee_V1.0_User_Manual.pdf` | Руководство пользователя платы | https://github.com/makerbase-mks/MKS-TinyBee/blob/main/MKS%20TinyBee%20V1.0%20User%20Manual.pdf |
| `MKS_TINYBEE_V1.0_using_serial_screen_tutorial.pdf` | Подключение serial screen | https://github.com/makerbase-mks/MKS-TinyBee/blob/main/MKS%20TINYBEE%20V1.0%20using%20serial%20screen%20tutorial.pdf |

## Hardware V1.0_003

Каталог: `hardware/V1.0_003/`

| Файл | Назначение | Источник |
|------|------------|----------|
| `MKS_TinyBee_V1.0_003_PIN.pdf` | Распиновка разъемов | https://github.com/makerbase-mks/MKS-TinyBee/tree/main/hardware/MKS%20TinyBee%20V1.0_003 |
| `MKS_TinyBee_V1.0_003_SCH.pdf` | Принципиальная схема | https://github.com/makerbase-mks/MKS-TinyBee/tree/main/hardware/MKS%20TinyBee%20V1.0_003 |
| `MKS_TinyBee_V1.0_003_SIZE.pdf` | Размеры платы | https://github.com/makerbase-mks/MKS-TinyBee/tree/main/hardware/MKS%20TinyBee%20V1.0_003 |
| `MKS_TinyBee_V1.0_003_TOP.pdf` | Верхний слой / размещение | https://github.com/makerbase-mks/MKS-TinyBee/tree/main/hardware/MKS%20TinyBee%20V1.0_003 |
| `MKS_TinyBee_V1.0_003_BOTTOM.pdf` | Нижний слой / размещение | https://github.com/makerbase-mks/MKS-TinyBee/tree/main/hardware/MKS%20TinyBee%20V1.0_003 |

## Firmware references

Каталог: `firmware_refs/`

| Файл | Назначение | Источник |
|------|------------|----------|
| `Marlin_pins_MKS_TINYBEE.h` | Pinmap Marlin для TinyBee | https://github.com/MarlinFirmware/Marlin/blob/2.1.2.1/Marlin/src/pins/esp32/pins_MKS_TINYBEE.h |
| `Marlin_Conditionals_adv.h` | `MAX_EXPANDER_BITS = 24` для TinyBee | https://github.com/MarlinFirmware/Marlin/blob/2.1.2.1/Marlin/src/HAL/ESP32/inc/Conditionals_adv.h |
| `Marlin_i2s.cpp` | Реализация I2S stepper stream / expander в Marlin | https://github.com/MarlinFirmware/Marlin/blob/2.1.2.1/Marlin/src/HAL/ESP32/i2s.cpp |
| `grblHAL_mks_tinybee_1_0_map.h` | Pinmap grblHAL для TinyBee | https://github.com/grblHAL/ESP32/blob/master/main/boards/mks_tinybee_1_0_map.h |
| `grblHAL_use_i2s_out.h` | Макросы I2S digital I/O grblHAL | https://github.com/grblHAL/ESP32/blob/master/main/use_i2s_out.h |
| `grblHAL_i2s_out.h` | Заголовок драйвера I2S output grblHAL | https://github.com/grblHAL/ESP32/blob/master/main/i2s_out.h |
| `grblHAL_i2s_out.c` | Реализация драйвера I2S output grblHAL | https://github.com/grblHAL/ESP32/blob/master/main/i2s_out.c |

## Рабочий документ проекта

Сводная карта GPIO, силовой части и рекомендации под прошивку сеялки:

- `../MKS_TINYBEE_V1_PINMAP_AND_CUSTOM_FW.md`

