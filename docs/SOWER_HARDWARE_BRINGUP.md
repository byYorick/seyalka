# Hardware bring-up: MKS TinyBee + сеялка

Журнал проверки платы и механики. Заполнять по мере тестов на реальном железе.

См. также:

- `docs/SOWER_FIRMWARE_IMPLEMENTATION_PLAN.md` — этапы прошивки
- `docs/MKS_TINYBEE_V1_PINMAP_AND_CUSTOM_FW.md` — пины
- `firmware/README.md` — Kconfig bring-up опции

## Безопасность

- По умолчанию все bring-up флаги в Kconfig **выключены**.
- Не включать `SOWER_OUTPUT_LED_TEST` при подключенных нагревателях, клапанах, насосах.
- `SOWER_CONVEYOR_MOTION_TEST` — только при безопасной механике (малый ход, низкая скорость, без зажимов).
- E-stop — аппаратная цепь; прошивка не заменяет его.

## Kconfig (menuconfig → Sower bring-up options)

| Опция | Назначение |
|-------|------------|
| `SOWER_OUTPUT_LED_TEST` | Циклический тест I2SO → HE-BED, HE-E0, HE-E1, FAN1, FAN2 |
| `SOWER_IO_MONITOR` | Периодический лог stable/raw входов и raw ADC вакуума |
| `SOWER_CONVEYOR_MOTION_TEST` | X: ход вперёд/назад на заданные mm и mm/s |

```bash
cd /home/georgiy/esp/solo/seyalka
source ./export_idf.sh
idf.py menuconfig
idf.py build flash monitor
```

## Чеклист bring-up

| # | Проверка | Ожидание | Факт | Дата | Примечание |
|---|----------|----------|------|------|------------|
| 1 | I2SO static outputs (HE/FAN) | LED/мультиметр переключаются | | | `CONFIG_SOWER_OUTPUT_LED_TEST` |
| 2 | Входы (cassette, home, encoder) | stable/raw логичны при срабатывании | | | `CONFIG_SOWER_IO_MONITOR` |
| 3 | ADC вакуума GPIO36 | raw меняется при разрядке/подключении | | | |
| 4 | Stepper enable X | EN активирует драйвер | | | без STEP |
| 5 | STEP pulse train X | импульсы на STEP | | | осциллограф/логика |
| 6 | X движение без нагрузки | +/- N mm, возврат | | | `CONFIG_SOWER_CONVEYOR_MOTION_TEST` |
| 7 | Direction X | обратный ход в другую сторону | | | |
| 8 | Z homing | останов на home switch | | | после `motion_home_axis` |
| 9 | E0 homing | останов на home switch | | | |
| 10 | Лента до датчика кассеты | останов на sensor | | | |
| 11 | VAC/PRESS mutual exclusion | safety fault при одновременном вкл. | | | |
| 12 | Dry-run цикла | FSM без семян | | | |
| 13 | Один ряд с семенами | | | | |
| 14 | Полная кассета | | | | |

## Полярность входов (заполнить после IO monitor)

| Вход | GPIO | active level (stable=1 когда…) | Проверено |
|------|------|--------------------------------|-----------|
| cassette sensor | см. pinmap | | |
| dibbler home | | | |
| transfer home | | | |
| start/stop | | | |
| encoder A/B/btn | | | |

## Motion X (заполнить после conveyor test)

| Параметр | Значение в Kconfig | Наблюдение |
|----------|-------------------|------------|
| distance_mm | | |
| speed_mm_s | | |
| enable polarity | active low | OK / инвертировать |
| direction | | forward = positive dir level |

## Известные проблемы

_Пока пусто — добавлять по мере тестов._
