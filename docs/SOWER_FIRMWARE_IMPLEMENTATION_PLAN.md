# План реализации прошивки сеялки для ИИ-агента

**Дата:** 2026-05-15  
**Плата:** MakerBase MKS TinyBee V1.0  
**Проект:** автономная сеялка автовысева семян в кассеты  
**Текущий статус:** ESP-IDF skeleton собирается из корня; `grblhal_i2s_out` passthrough проверен на `HE/FAN`; bring-up режимы LED/IO/conveyor вынесены в Kconfig; `io_task` и `safety_task` работают; `motion_adapter` v1 (software step gen через I2SO, 3 оси, `motion_wait_idle`); FSM/machine/UI — черновики.

Этот документ является рабочим заданием для ИИ-агента, который будет продолжать реализацию прошивки. Перед изменениями обязательно прочитать:

- `docs/SOWER_PROJECT_ARCHITECTURE.md`
- `docs/SOWER_CODE_ARCHITECTURE_RULES.md`
- `docs/MKS_TINYBEE_V1_PINMAP_AND_CUSTOM_FW.md`
- `README.md`
- `firmware/README.md`

## 1. Главные ограничения

- Сборка выполняется из корня проекта:

```bash
cd /home/georgiy/esp/solo/seyalka
source ./export_idf.sh
idf.py build
```

- Нельзя обращаться к GPIO/I2SO из прикладной логики напрямую.
- Все номера GPIO/I2SO живут только в `board_tinybee`.
- Все движения идут только через `motion_adapter`.
- Все комментарии в проектном коде пишутся на русском языке.
- Имена файлов, функций, типов и enum остаются на английском языке.
- `E-stop` проектировать как аппаратную защиту; firmware может только диагностировать.
- `control_task` не должен писать flash, рисовать дисплей, делать web-запросы или блокироваться на долгие задержки.
- До реального теста с нагрузками отключать нагреватели, клапаны, насосы и внешние нагрузки; сначала проверять светодиоды/мультиметр.

## 2. Текущая база

Существующие рабочие части:

- Корневой ESP-IDF project wrapper: `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`.
- `firmware/components/grblhal_i2s_out` - I2S0 passthrough backend для 74HC595/I2SO.
- `firmware/components/board_tinybee` - базовые входы, выходы, safe outputs.
- `firmware/components/io_service` - регулярный `io_task` для debounce входов и обновления raw ADC вакуума.
- `firmware/main/Kconfig.projbuild` - bring-up флаги `CONFIG_SOWER_OUTPUT_LED_TEST`, `CONFIG_SOWER_IO_MONITOR`, `CONFIG_SOWER_CONVEYOR_MOTION_TEST`.
- `firmware/main/app_main.c` - опциональные bring-up задачи LED-test и conveyor motion test.
- `firmware/components/motion_adapter` - software step generator через I2SO для X/Z/E0, `motion_wait_idle()`.
- `firmware/components/safety` - fault latch, safe-state, ACK, periodic output conflict check, отключенный по умолчанию control watchdog.
- Заготовки компонентов: `machine`, `app_sower`, `storage`, `ui_display`, `ui_web`.

Проверенный на железе факт:

- `I2S0 -> 74HC595 -> IO144..IO148` работает через `grblhal_i2s_out`.
- LED-test по `H-BED`, `H-E0`, `H-E1`, `FAN1`, `FAN2` визуально работает.

## 3. Целевая архитектура задач

Минимальная модель задач v1:

| Task | Priority | Назначение |
|------|----------|------------|
| `control_task` | High | FSM, safety checks, command queue |
| `motion_task` | High | grblHAL/I2S step generation |
| `io_task` | Medium | debounce входов, ADC вакуума |
| `display_task` | Low/Medium | ILI9341 + encoder UI |
| `web_task` | Low | web config/status |
| `log_task` | Low | async event log flush |

Правила:

- Только `control_task` меняет состояние FSM.
- ISRs только кладут события в queue.
- UI/web работают через command API, без прямого управления железом.
- Storage пишет flash асинхронно и не из `control_task`.

## 4. Порядок реализации

### Этап 0. Убрать постоянный LED-test из production boot

**Статус:** выполнено 2026-05-15.

Цель: текущий тест выходов оставить доступным, но не запускать всегда.

Сделать:

- Добавить compile-time флаг или service mode для LED-test.
- Рекомендуемый вариант: `CONFIG_SOWER_OUTPUT_LED_TEST` через `Kconfig.projbuild` или временный `#define` в отдельном `bringup_config.h`.
- По умолчанию тест выключен.
- При выключенном тесте boot оставляет все outputs off.

Критерии готовности:

- `idf.py build` проходит.
- При default config LED-test не запускается.
- При включенном флаге LED-test работает как сейчас.

### Этап 1. Довести `board_tinybee`

**Статус:** базово выполнено 2026-05-15.

Сделано:

- `board_update_inputs()`;
- debounced/stable input state;
- raw input state;
- `board_status_snapshot_t`;
- `board_get_status_snapshot()`;
- raw ADC чтение вакуума через ESP-IDF `adc_oneshot` на `GPIO36`;
- `board_get_input()` возвращает stable/debounced состояние.
- `io_service` запускает `io_task`;
- `io_task` обновляет входы каждые `10 ms`;
- `io_task` обновляет raw ADC вакуума каждые `20 ms`;
- board snapshot копируется через короткий critical section.
- добавлен bring-up input/ADC monitor через `CONFIG_SOWER_IO_MONITOR`;
- README описывает проверку входов и raw ADC через `idf.py monitor`.

Осталось:

- добавить E-stop diagnostic input, когда будет назначен пин;
- добавить калибровку датчика вакуума после подключения железа.

Цель: board layer должен быть единственной точкой доступа к железу TinyBee.

Сделать:

- Оставить `grblhal_i2s_out` как backend для I2SO.
- Добавить read API для всех нужных входов:
  - cassette sensor;
  - dibbler home;
  - transfer home;
  - start/stop;
  - encoder A/B/button;
  - optional E-stop diagnostic input, если будет выделен пин.
- Добавить debounce/filter слой для дискретных входов.
- Добавить ADC backend для вакуума на `GPIO36`.
- Добавить простую калибровку raw ADC min/max.
- Добавить `board_get_status_snapshot()`.

Критерии готовности:

- Можно читать входы без прямого `gpio_get_level()` вне `board_tinybee`.
- Можно читать raw ADC вакуума.
- `board_safe_outputs_off()` выключает `HE/FAN/BEEPER` и не падает при повторном вызове.
- Host/hardware smoke test документирован в README или bring-up notes.

### Этап 2. Safety manager

**Статус:** базово выполнено 2026-05-15.

Сделано:

- `safety_enter_safe_state()` останавливает motion и выключает выходы;
- fault latch запрещает старт цикла до `safety_ack_fault()`;
- `safety_task` вызывает `safety_check_periodic()` каждые `50 ms`;
- `safety_check_periodic()` ловит одновременное включение `VACUUM_VALVE` и `PRESSURE_VALVE`;
- добавлен control watchdog API, по умолчанию выключен до появления `control_task`.

Осталось:

- подключить diagnostic E-stop input после назначения пина;
- включить watchdog из будущего `control_task`;
- связать прикладные `fault_code_t` с `safety_fault_t` в FSM.

Цель: безопасная остановка должна быть общей для всех узлов.

Сделать:

- `safety_enter_safe_state()`:
  - останавливает motion;
  - выключает outputs;
  - запрещает новый cycle до ack/reset.
- Добавить `safety_check_periodic()`.
- Добавить fault latch.
- Добавить запрет одновременного вакуума и давления.
- Добавить watchdog для control path.

Критерии готовности:

- Любой critical fault вызывает safe state.
- Из `FAULT` нельзя стартовать без `ACK_FAULT`.
- Safe state не зависит от UI/web.

### Этап 3. Motion adapter v1

**Статус:** в работе 2026-05-15.

Сделано:

- step/dir/enable для X/Z/E0 через I2SO (`grblhal_i2s_out`);
- `motion_init()`, `motion_enable_all()`, `motion_stop_all()`, `motion_move_rel_mm()`, `motion_move_abs_mm()`, `motion_is_busy()`, `motion_get_status()`;
- `motion_wait_idle(timeout_ms)` с остановкой при timeout;
- `motion_task` на отдельном ядре, software step generator для bring-up;
- bring-up тест конвейера через `CONFIG_SOWER_CONVEYOR_MOTION_TEST` в `app_main.c`.

Осталось:

- проверить X/Z/E0 на железе (enable polarity, direction, STEP, home switches);
- уточнить направление homing и active level концевиков в `SOWER_HARDWARE_BRINGUP.md`;
- timeout/ограничение длины хода внутри motion path (сейчас — chunked + `motion_wait_idle`);
- позже: grblHAL backend или TMC2209 (этап 4).

Дополнительно сделано в этапе 3:

- `motion_home_axis()` для Z/E0 (chunked search к home switch, backoff, сброс position);
- `cassette_wait_and_capture()` — подача чанками до `BOARD_IN_CASSETTE_SENSOR`;
- `cassette_move_to_first_row()` / `cassette_advance_row()` — ожидание завершения хода;
- `dibbler` / `transfer_arm` — `motion_wait_idle` после перемещений.

Цель: получить контролируемое движение одной оси, затем остальных.

Оси v1:

| Axis | TinyBee | Назначение |
|------|---------|------------|
| `MOTION_AXIS_CONVEYOR` | X | конвейер/лента |
| `MOTION_AXIS_DIBBLER` | Z | инструмент лунок |
| `MOTION_AXIS_TRANSFER` | E0 | перенос игл |
| `Y` | резерв | не использовать в v1 |
| `E1` | резерв | не использовать в v1 |

Сделать:

- Подключить step/dir/enable через I2SO.
- Добавить минимальный step generator или интегрировать grblHAL motion backend.
- Для начала реализовать только одну ось `CONVEYOR`.
- Реализовать:
  - `motion_init()`;
  - `motion_enable_all()`;
  - `motion_stop_all()`;
  - `motion_move_rel_mm()`;
  - `motion_home_axis()` для осей с home;
  - `motion_is_busy()`.
- Для конвейера home не нужен: подача идет до датчика кассеты.

Критерии готовности:

- Ось X делает контролируемое движение без нагрузки.
- Direction меняется ожидаемо.
- Enable polarity правильная.
- Timeout останавливает движение.
- `idf.py build` проходит после каждого шага.

### Этап 4. TMC2209

Цель: драйверы должны иметь контролируемую конфигурацию.

Сделать:

- Определить UART wiring TinyBee для TMC2209.
- Добавить компонент `tmc2209`.
- Настроить:
  - current `1.5 A`;
  - microsteps;
  - stealthChop/spreadCycle policy;
  - enable polarity;
  - direction inversion config.
- Добавить диагностику связи с драйвером.

Критерии готовности:

- Драйверы включаются и выключаются программно.
- Ток выставляется и подтверждается чтением регистра, если UART доступен.
- При ошибке связи система уходит в service fault.

### Этап 5. Machine subsystems

Цель: каждый механический узел имеет небольшой API и не знает глобальную FSM.

Реализовать:

- `cassette_indexer`
  - feed until cassette sensor;
  - stop on sensor;
  - move to first row center;
  - advance row.

- `dibbler`
  - home;
  - make holes;
  - return safe.

- `transfer_arm`
  - home;
  - move to `PICK`;
  - move to `DROP`;
  - move to `BLOW`;
  - move to `SAFE`.

- `vacuum_system`
  - ADC filter;
  - pickup threshold;
  - hold threshold;
  - release threshold;
  - vacuum lost detection.

- `pneumatics`
  - vacuum valve;
  - pressure valve;
  - deadtime;
  - mutual exclusion.

- `vibro_feeder`
  - enable/disable;
  - duty/profile;
  - config disable until hardware known.

Критерии готовности:

- Каждый subsystem имеет отдельный hardware/service test.
- Ни один subsystem не вызывает UI/web/storage напрямую.
- Ошибки возвращаются typed result/error code.

### Этап 6. Recipe/config/storage

Цель: все параметры машины должны быть конфигурируемыми.

Сделать:

- `recipe_t` с version и CRC.
- Defaults для одного типа кассеты.
- Geometry:
  - rows;
  - needles = 8;
  - row pitch;
  - first row offset from sensor;
  - conveyor steps/mm.
- Motion:
  - speed/acceleration per axis;
  - homing speed;
  - timeout.
- Vacuum:
  - pickup threshold;
  - hold threshold;
  - release threshold;
  - retry count.
- Pneumatics:
  - valve deadtime;
  - blow duration.
- Storage:
  - NVS load/save;
  - validation;
  - fallback defaults.

Критерии готовности:

- Invalid config не запускает production cycle.
- Config сохраняется только по команде UI/web.
- Default recipe загружается на чистой NVS.

### Этап 7. FSM production cycle

Цель: технологический цикл должен быть явной state machine без blocking delays.

Состояния v1:

- `BOOT`
- `SAFE_OUTPUTS_OFF`
- `LOAD_CONFIG`
- `INIT_BOARD`
- `HOME_TOOLS`
- `IDLE`
- `WAIT_CASSETTE`
- `INDEX_TO_FIRST_ROW_CENTER`
- `MAKE_HOLES`
- `PICK_SEEDS`
- `VERIFY_PICKUP`
- `TRANSFER_TO_DROP`
- `DROP_SEEDS`
- `BLOW_NEEDLES`
- `ADVANCE_TO_NEXT_ROW`
- `EJECT_CASSETTE`
- `FAULT`
- `ABORTED`

Сделать:

- `on_enter`, `on_tick`, `on_exit` для состояний.
- Command queue: start, stop, ack fault, home, service tests.
- Deadline/timeout на каждое действие.
- Row index tracking.
- Retry через `fault_manager`, не внутри случайных состояний.

Критерии готовности:

- Dry-run проходит без семян.
- Все переходы логируются.
- Stop command работает из каждого производственного состояния.
- Critical fault переводит в `FAULT` и safe outputs off.

### Этап 8. Fault manager

Цель: ошибки должны быть структурированными.

Минимальные fault codes:

- `FAULT_ESTOP`
- `FAULT_CONFIG_INVALID`
- `FAULT_HOME_DIBBLER_FAILED`
- `FAULT_HOME_TRANSFER_FAILED`
- `FAULT_CASSETTE_TIMEOUT`
- `FAULT_PICKUP_VACUUM_TIMEOUT`
- `FAULT_VACUUM_LOST`
- `FAULT_VACUUM_RELEASE_FAILED`
- `FAULT_ADC_RANGE`
- `FAULT_MOTION_TIMEOUT`

Сделать:

- severity;
- retry policy;
- row snapshot;
- ADC snapshot;
- motion axis snapshot;
- UI text mapping.

Критерии готовности:

- Critical fault всегда вызывает safety.
- Recoverable fault retry работает ограниченное число раз.
- UI получает понятное описание из fault code.

### Этап 9. UI display

Цель: локальное управление без web.

Сделать:

- ILI9341 SPI driver.
- Encoder navigation.
- Main status screen:
  - state;
  - row;
  - vacuum raw/filtered;
  - faults;
  - outputs summary.
- Service screen:
  - output tests;
  - input monitor;
  - ADC monitor;
  - jog axes.
- Config screen для базовых параметров.

Критерии готовности:

- Display не блокирует control task.
- Full redraw не выполняется во время точного движения.
- Encoder работает с debounce.

### Этап 10. Web config

Цель: настройка и диагностика через браузер.

Сделать:

- Wi-Fi AP или STA config.
- HTTP API:
  - status;
  - config get/set;
  - command;
  - event log.
- Минимальная web страница.
- Service commands разрешены только в safe/service state.

Критерии готовности:

- Web failure не влияет на production cycle.
- Config через web применяется только в safe states.
- Нет high-frequency websocket spam в production.

### Этап 11. Logging/diagnostics

Цель: после сбоя понять, что произошло.

Логировать:

- boot/reset reason;
- config loaded/invalid;
- state transitions;
- cassette detected timestamp;
- row start/finish;
- vacuum ADC pickup/hold/release;
- retry attempts;
- faults;
- service commands.

Сделать:

- RAM ring buffer.
- Async flush.
- Log snapshot API для UI/web.

Критерии готовности:

- `control_task` не пишет flash напрямую.
- Последние события видны на display/web.

## 5. Hardware bring-up порядок

1. I2SO static outputs: `HE/FAN/BEEPER`.
2. Inputs polarity: cassette/home/buttons/encoder.
3. ADC vacuum raw.
4. Stepper enable без движения.
5. Один STEP pulse train на X без нагрузки.
6. X движение с малой скоростью.
7. Z homing.
8. E0 homing.
9. Conveyor feed until cassette sensor.
10. Vacuum/pressure valve mutual exclusion.
11. Dry-run полного цикла без семян.
12. Один ряд с семенами.
13. Полная кассета.

## 6. Definition of Done для каждого PR/шага

Каждый шаг должен:

- собираться командой `idf.py build` из корня;
- не ломать safe boot;
- не добавлять прямой GPIO/I2SO доступ вне разрешенного слоя;
- иметь короткую запись в README/docs, если меняется bring-up процедура;
- сохранять русские комментарии в проектном коде;
- оставлять production outputs выключенными по умолчанию.

## 7. Что не делать без отдельного решения

- Не добавлять компьютерное зрение на ESP32.
- Не делать software-only E-stop.
- Не подключать клапаны/насосы/нагреватели без flyback protection и проверки схемы.
- Не переносить весь проект обратно в `firmware/` как root.
- Не смешивать UI/web команды с прямым управлением железом.
- Не реализовывать сложную графику дисплея до стабильного motion/safety.
- Не писать flash из `control_task`.

## 8. Ближайшее задание для следующего агента

Практические шаги (этап 3 + bring-up):

1. Включить `CONFIG_SOWER_IO_MONITOR`, проверить полярность входов и raw ADC вакуума на железе; записать в `docs/SOWER_HARDWARE_BRINGUP.md`.
2. Без нагрузки включить `CONFIG_SOWER_CONVEYOR_MOTION_TEST` (малая дистанция/скорость), проверить STEP/DIR/EN оси X; записать результат.
3. Проверить `motion_home_axis()` на Z/E0 и `cassette_wait_and_capture()` на железе; записать полярность в bring-up doc.
4. После стабильного motion — этап 4 (TMC2209 UART).
5. Добавить `control_task` и non-blocking FSM tick (этап 7).

Проверка:

```bash
cd /home/georgiy/esp/solo/seyalka
source ./export_idf.sh
idf.py build
idf.py menuconfig   # Component config → Sower bring-up options
idf.py flash monitor
```

Ожидаемый результат:

- default boot не мигает `HE/FAN` и не крутит конвейер;
- `io_task` стартует после `board_init`;
- при `CONFIG_SOWER_CONVEYOR_MOTION_TEST` ось X делает ход +/- N mm и возвращается;
- прямых GPIO-вызовов из app/machine слоев нет.
