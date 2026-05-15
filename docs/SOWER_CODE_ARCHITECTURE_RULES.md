# Архитектура кода, правила и паттерны прошивки сеялки

**Дата:** 2026-05-15  
**Проект:** сеялка автовысева на MKS TinyBee V1.0  
**База:** ESP-IDF + grblHAL/ESP32 для motion planner, step generation и I2S output  
**Цель документа:** задать границы модулей, правила зависимостей, ограничения TinyBee и паттерны реализации до начала написания кода

---

## 1. Базовые принципы

- Прикладная логика сеялки не управляет GPIO, STEP/DIR, I2S и PWM напрямую.
- Все движения выполняются через `motion_adapter`; ручные импульсы STEP запрещены.
- Все силовые выходы проходят через `board_tinybee` и `safety`.
- Главный технологический цикл реализуется как явная конечная state machine, без скрытых переходов внутри драйверов.
- Каждый модуль имеет один уровень ответственности: board, motion, machine subsystem, app FSM, UI, storage.
- Ошибки возвращаются как typed result/error code, а не как `bool` без причины.
- Все параметры процесса берутся из `recipe/config`, не из magic numbers внутри FSM.
- Safety имеет приоритет над UI, web, логированием и технологическим циклом.
- Все поясняющие комментарии в исходном коде пишутся на русском языке.

---

## 2. Предлагаемая структура проекта

```text
firmware/
  CMakeLists.txt
  sdkconfig.defaults
  main/
    app_main.c
    app_context.h

  components/
    board_tinybee/
      include/board_tinybee.h
      board_tinybee.c
      board_pins.h

    motion_adapter/
      include/motion_adapter.h
      motion_adapter.c

    machine/
      include/cassette_indexer.h
      include/dibbler.h
      include/transfer_arm.h
      include/vacuum_system.h
      include/pneumatics.h
      include/vibro_feeder.h
      cassette_indexer.c
      dibbler.c
      transfer_arm.c
      vacuum_system.c
      pneumatics.c
      vibro_feeder.c

    app_sower/
      include/sower_fsm.h
      include/sower_events.h
      include/fault_manager.h
      include/recipe.h
      sower_fsm.c
      row_process.c
      fault_manager.c
      recipe.c

    ui_display/
      include/ui_display.h
      ui_display.c

    ui_web/
      include/ui_web.h
      ui_web.c

    storage/
      include/config_store.h
      include/event_log.h
      config_store.c
      event_log.c

    safety/
      include/safety.h
      safety.c

  tests/
    host/
      test_recipe.c
      test_sower_fsm.c
      test_fault_policy.c
```

`docs/` остается для архитектуры, pinmap и эксплуатационных заметок. Upstream-файлы Marlin/grblHAL из `docs/mks_tinybee/firmware_refs/` являются reference, не рабочим исходным кодом проекта.

---

## 3. Слои и зависимости

Разрешенные зависимости:

```text
app_sower
  -> machine
  -> storage
  -> safety

machine
  -> motion_adapter
  -> board_tinybee
  -> safety

ui_display / ui_web
  -> app_sower public API
  -> storage
  -> safety read-only status

motion_adapter
  -> grblHAL/ESP32

board_tinybee
  -> ESP-IDF drivers
  -> grblHAL i2s_out if reused directly

safety
  -> board_tinybee
  -> motion_adapter
```

Запрещенные зависимости:

- `board_tinybee` не знает про рецепты, ряды кассеты, вакуумный цикл и UI.
- `motion_adapter` не знает про семена, кассеты, клапаны и дисплей.
- `machine` не вызывает web/display напрямую.
- `ui_display` и `ui_web` не дергают клапаны, моторы и GPIO напрямую.
- `app_sower` не содержит номеров GPIO/I2SO.
- `safety` не зависит от UI и не ждет UI для отключения выходов.

---

## 4. Board layer

`board_tinybee` - единственное место, где живут номера GPIO, I2SO и особенности MKS TinyBee.

### Обязанности

- Инициализация I2S expander, GPIO, ADC, SPI display pins.
- Безопасное начальное состояние выходов.
- Чтение дискретных входов: кассета, home лунок, home переноса, Start/Stop, encoder.
- Чтение ADC датчика вакуума.
- Управление силовыми выходами: вакуум, давление, насос, вибро, beeper.

### Минимальный API

```c
typedef enum {
    BOARD_OUT_VACUUM_PUMP,
    BOARD_OUT_VACUUM_VALVE,
    BOARD_OUT_PRESSURE_VALVE,
    BOARD_OUT_VIBRO_PWM,
    BOARD_OUT_SPARE,
    BOARD_OUT_BEEPER,
} board_output_t;

typedef enum {
    BOARD_IN_CASSETTE_SENSOR,
    BOARD_IN_DIBBLER_HOME,
    BOARD_IN_TRANSFER_HOME,
    BOARD_IN_START_STOP,
    BOARD_IN_ENCODER_A,
    BOARD_IN_ENCODER_B,
    BOARD_IN_ENCODER_BUTTON,
} board_input_t;

esp_err_t board_init(void);
esp_err_t board_safe_outputs_off(void);
esp_err_t board_set_output(board_output_t out, bool on);
esp_err_t board_set_pwm(board_output_t out, uint16_t duty_permille);
bool board_get_input(board_input_t in);
esp_err_t board_read_vacuum_adc(uint16_t *adc_raw);
```

### Правила

- `board_safe_outputs_off()` должен быть idempotent и вызываться в любом состоянии.
- ENABLE шаговых драйверов активен LOW; это скрывается внутри motion/grblHAL layer.
- Не использовать `gpio_set_level(145, ...)`: `IO144..IO149` - это I2S expander, не GPIO.
- GPIO `34/35/36/39` только input.
- GPIO `25/26/27` зарезервированы под I2S.
- GPIO `12` как энкодер B использовать только со схемой, не влияющей на boot strapping.

---

## 5. Motion adapter

`motion_adapter` скрывает grblHAL и представляет машине предметные команды движения.

### Оси v1

| Machine axis | TinyBee | Назначение |
|--------------|---------|------------|
| `MOTION_AXIS_CONVEYOR` | X | Лента, Leadshine 42CM08, ремень 1:8 |
| `MOTION_AXIS_DIBBLER` | Z | Система лунок |
| `MOTION_AXIS_TRANSFER` | E0 | Полукруг переноса игл |
| `MOTION_AXIS_RESERVED_Y` | Y | Резерв |
| `MOTION_AXIS_RESERVED_E1` | E1 | Резерв |

### Минимальный API

```c
typedef enum {
    MOTION_AXIS_CONVEYOR,
    MOTION_AXIS_DIBBLER,
    MOTION_AXIS_TRANSFER,
} motion_axis_t;

typedef struct {
    float max_speed_mm_s;
    float accel_mm_s2;
    float steps_per_mm;
} motion_axis_cfg_t;

esp_err_t motion_init(const recipe_t *recipe);
esp_err_t motion_enable_all(bool enable);
esp_err_t motion_stop_all(void);
esp_err_t motion_home_axis(motion_axis_t axis, uint32_t timeout_ms);
esp_err_t motion_move_rel_mm(motion_axis_t axis, float distance_mm, float speed_mm_s);
esp_err_t motion_move_abs_mm(motion_axis_t axis, float position_mm, float speed_mm_s);
bool motion_is_busy(void);
```

### Правила

- FSM не ждет движение busy-loop без timeout.
- Любое движение имеет лимит времени или контролируемое условие завершения.
- Конвейер не имеет home: в `WAIT_CASSETTE` он движется до датчика кассеты.
- После датчика кассеты позиция конвейера считается локальным нулем для текущей кассеты.
- Все conversion `mm <-> steps` живут в motion/config, не в FSM.
- TMC2209 на старте проекта настраивать на `1.5 A`; проверка нагрева обязательна в bring-up.

---

## 6. Machine subsystems

Machine subsystem - это маленький контроллер конкретного узла. Он не знает глобальную FSM, но умеет выполнять свою операцию и возвращать результат.

### `cassette_indexer`

Обязанности:

- Запуск подачи ленты.
- Остановка по датчику кассеты.
- Индексация к центру первого ряда.
- Переход на следующий ряд.

Паттерн:

```c
esp_err_t cassette_wait_and_capture(uint32_t timeout_ms);
esp_err_t cassette_move_to_first_row(const recipe_t *recipe);
esp_err_t cassette_advance_row(const recipe_t *recipe, uint16_t row_index);
```

### `dibbler`

Обязанности:

- Homing инструмента лунок.
- Рабочий ход на 8 лунок.
- Возврат в safe position.

Правило: если home не найден, дальнейший цикл запрещен.

### `transfer_arm`

Обязанности:

- Homing полуокружности переноса.
- Позиции `PICK`, `DROP`, `BLOW`, `SAFE`.
- Контроль timeout движения.

Правило: позиции transfer arm задаются в config, не в коде FSM.

### `vacuum_system`

Обязанности:

- Чтение и фильтрация ADC вакуума.
- Проверка порогов `pickup`, `hold`, `release`.
- Диагностика утечки/неудачного захвата.

Паттерн:

```c
typedef enum {
    VACUUM_OK,
    VACUUM_TIMEOUT,
    VACUUM_SENSOR_RANGE_ERROR,
    VACUUM_RELEASE_FAILED,
} vacuum_result_t;
```

### `pneumatics`

Обязанности:

- Управление клапаном вакуума и клапаном давления.
- Взаимоисключение вакуум/давление.
- Deadtime между переключениями.

Правило: состояние, где одновременно открыт вакуумный клапан и клапан давления, запрещено.

### `vibro_feeder`

Обязанности:

- PWM DC мотора вибробункера.
- Профиль `pre_pick`, `pick`, `post_pick`.

Правило: конкретный ток/модель мотора пока unknown; код должен позволять отключить вибро в config.

---

## 7. FSM pattern

FSM должна быть явной таблицей состояний и событий.

### Типы

```c
typedef enum {
    SOWER_STATE_BOOT,
    SOWER_STATE_SAFE_OUTPUTS_OFF,
    SOWER_STATE_LOAD_CONFIG,
    SOWER_STATE_INIT_BOARD,
    SOWER_STATE_HOME_TOOLS,
    SOWER_STATE_IDLE,
    SOWER_STATE_WAIT_CASSETTE,
    SOWER_STATE_RUN_CONVEYOR_FEED,
    SOWER_STATE_DETECT_CASSETTE_EDGE,
    SOWER_STATE_STOP_CONVEYOR,
    SOWER_STATE_INDEX_TO_FIRST_ROW_CENTER,
    SOWER_STATE_MAKE_HOLES,
    SOWER_STATE_PICK_SEEDS,
    SOWER_STATE_VERIFY_PICKUP,
    SOWER_STATE_TRANSFER_TO_DROP,
    SOWER_STATE_DROP_SEEDS,
    SOWER_STATE_BLOW_NEEDLES,
    SOWER_STATE_ADVANCE_TO_NEXT_ROW,
    SOWER_STATE_EJECT_CASSETTE,
    SOWER_STATE_FAULT,
    SOWER_STATE_ABORTED,
} sower_state_t;

typedef enum {
    SOWER_EVENT_TICK,
    SOWER_EVENT_START_STOP,
    SOWER_EVENT_CASSETTE_DETECTED,
    SOWER_EVENT_MOTION_DONE,
    SOWER_EVENT_TIMEOUT,
    SOWER_EVENT_FAULT,
    SOWER_EVENT_ESTOP,
} sower_event_t;
```

### Правила FSM

- Состояние имеет `on_enter`, `on_tick`, `on_exit`.
- `on_enter` запускает действие, `on_tick` проверяет завершение/timeout.
- Blocking delays внутри FSM запрещены; использовать deadline/timer.
- Любой state timeout явно задан в config или константе safety.
- Переходы логируются: old state, event, new state, result.
- Retry не размазан по состояниям; retry policy живет в `fault_manager`.
- `SOWER_EVENT_ESTOP` обрабатывается из любого состояния.
- Из `FAULT` нельзя автоматически продолжать, если fault marked critical.

---

## 8. Config, recipe, storage

### Правила

- Все технологические параметры сериализуемы.
- У config есть `version`.
- Загрузка config валидирует диапазоны и подставляет defaults.
- Runtime не пишет config постоянно; запись только по команде UI/web.
- Невалидный config переводит систему в service fault, но не запускает цикл.

### Минимальные типы

```c
typedef struct {
    uint16_t rows;
    uint8_t needles;
    float row_pitch_mm;
    float first_row_offset_from_sensor_mm;
    float conveyor_steps_per_mm;
    float conveyor_belt_reduction;
} recipe_geometry_t;

typedef struct {
    uint16_t pickup_threshold_adc;
    uint16_t hold_min_adc;
    uint16_t release_threshold_adc;
    uint32_t pickup_timeout_ms;
    uint8_t retry_count;
} vacuum_cfg_t;

typedef struct {
    recipe_geometry_t geometry;
    vacuum_cfg_t vacuum;
    /* motion, pneumatics, vibro, fault_policy */
    uint32_t version;
    uint32_t crc32;
} recipe_t;
```

---

## 9. Fault handling

Fault - это структурированное событие, а не строка.

```c
typedef enum {
    FAULT_NONE,
    FAULT_ESTOP,
    FAULT_CONFIG_INVALID,
    FAULT_HOME_DIBBLER_FAILED,
    FAULT_HOME_TRANSFER_FAILED,
    FAULT_CASSETTE_TIMEOUT,
    FAULT_CASSETTE_SENSOR_INVALID,
    FAULT_PICKUP_VACUUM_TIMEOUT,
    FAULT_VACUUM_LOST,
    FAULT_VACUUM_RELEASE_FAILED,
    FAULT_ADC_RANGE,
    FAULT_MOTION_TIMEOUT,
} fault_code_t;

typedef enum {
    FAULT_SEVERITY_INFO,
    FAULT_SEVERITY_WARN,
    FAULT_SEVERITY_RECOVERABLE,
    FAULT_SEVERITY_CRITICAL,
} fault_severity_t;
```

Правила:

- Critical fault always calls `safety_enter_safe_state()`.
- Recoverable fault may retry only through `fault_manager`.
- Every fault stores state, row index, ADC snapshot, motion axis if relevant.
- Fault text for UI is derived from code, not stored as primary logic.

---

## 10. Safety rules

- E-stop is hardware first. Firmware status input is optional and diagnostic only.
- Boot starts with all outputs off.
- On reset/reboot, valves and pump default to off.
- Vacuum and pressure valves are mutually exclusive.
- Motion cannot start unless tools are homed or explicitly in service/manual mode.
- Manual mode must show active warning on display and web.
- Watchdog enabled for main control task.
- ADC vacuum sensor range is checked; impossible values stop the cycle.
- Unknown valve/vibro hardware must default to disabled in config until electrically verified.

---

## 11. UI and command pattern

UI layers never perform actions directly. They send commands to `app_sower`.

```c
typedef enum {
    CMD_START,
    CMD_STOP,
    CMD_ACK_FAULT,
    CMD_HOME_TOOLS,
    CMD_JOG_CONVEYOR,
    CMD_TEST_VACUUM,
    CMD_TEST_PRESSURE,
    CMD_TEST_VIBRO,
    CMD_SAVE_CONFIG,
} app_command_type_t;
```

Rules:

- Commands validate current state before execution.
- Web and display share the same command API.
- Display is allowed to poll status snapshots.
- Status snapshot is read-only and copy-based; UI does not hold pointers to mutable FSM internals.

---

## 12. FreeRTOS task model

Recommended v1 tasks:

| Task | Priority | Role |
|------|----------|------|
| `control_task` | High | FSM tick, safety checks, command queue |
| `motion_task` | High/grblHAL owned | Motion planner and step generation |
| `io_task` | Medium | Debounce inputs, ADC sampling/filtering |
| `display_task` | Low/Medium | ILI9341 redraw and encoder UI |
| `web_task` | Low | Wi-Fi config/API |
| `log_task` | Low | Event log flush |

Rules:

- Only `control_task` mutates FSM state.
- ISRs push events to queues; no heavy logic in ISR.
- ADC filtering can run in `io_task`, FSM reads last validated sample.
- UI/web never block `control_task`.
- Shared status uses mutex or single-writer snapshot copy.

---

## 13. CPU and performance budget

ESP32-WROOM-32U на TinyBee достаточен для v1, если motion остается в grblHAL/I2S, а UI/web/storage не попадают в критический путь.

### Оценка нагрузки v1

| Subsystem | Ожидаемая нагрузка | Правило |
|-----------|--------------------|---------|
| grblHAL motion + I2S | Критичный realtime | Высший приоритет, не блокировать UI/web/storage |
| `control_task` FSM | Легкая, 50-100 Hz tick | Только state transitions, timeouts, command queue |
| `io_task` ADC/buttons | Легкая, 50-200 Hz | Фильтрация и debounce вне FSM |
| ILI9341 | Средняя при full redraw | Dirty rectangles, ограничить refresh |
| Web UI / Wi-Fi | Нерегулярная, потенциально шумная | Только config/status, без streaming и тяжелых страниц |
| Logging/storage | Нерегулярная | Ring buffer + async flush |

### Core affinity

- Wi-Fi обычно оставлять на системном ядре ESP-IDF.
- `control_task` и grblHAL/motion по возможности закрепить на другом ядре.
- `display_task`, `web_task`, `log_task` не должны иметь приоритет выше `control_task`.
- Если grblHAL задает собственные affinity/priority, не переопределять их без измерений.

### Display limits

- ILI9341 использовать без full-screen animation в production cycle.
- Обновлять только изменившиеся области экрана.
- Целевой refresh статуса: 5-10 Hz.
- Full-screen redraw разрешен в меню/idle/service, но не во время точного движения.
- SPI display transfer должен быть DMA/non-blocking where practical; длительный synchronous redraw не запускается из `control_task`.

### Web limits

- Web UI v1: конфигурация, статус, ручные сервисные команды.
- Не добавлять live-графики с высокой частотой, camera stream, websocket spam или большие JSON в production cycle.
- Изменение config через web применяет параметры только в safe states: `IDLE`, `FAULT`, `SERVICE`.
- Network failure не должен влиять на текущий цикл высева.

### Storage limits

- `control_task` не пишет flash напрямую.
- Журнал сначала пишется в RAM ring buffer.
- Flush в flash выполняет `log_task`, маленькими пачками и только вне критичных движений или с низким приоритетом.
- Config write только по явной команде оператора.

### Acceptance metrics

При bring-up добавить диагностический экран/лог:

- CPU idle/headroom по ядрам;
- stack high watermark для задач;
- максимальная задержка `control_task` tick;
- максимальная задержка реакции на Start/Stop;
- максимальная задержка остановки конвейера по датчику кассеты;
- счетчик пропусков display frames;
- счетчик Wi-Fi/storage блокировок, если появятся.

Минимальный критерий v1: при включенных display и web в production cycle должен оставаться запас CPU не меньше 30% на каждом ядре или должна быть явно отключаемая деградация UI/web.

### Если мощности не хватает

Порядок деградации:

1. Снизить refresh ILI9341 до 2-5 Hz и убрать full redraw.
2. Отключить web во время production cycle, оставить только status polling в idle.
3. Уменьшить частоту logging flush, писать только fault/row summary.
4. Перенести сложную графику/статистику в web idle-mode.
5. Если позже появится камера или компьютерное зрение, вынести это на отдельный SBC/MCU; ESP32 TinyBee не планировать под vision.

---

## 14. Logging and diagnostics

Log events:

- boot/reset reason;
- config loaded/invalid;
- state transitions;
- cassette detected timestamp;
- row start/finish;
- vacuum ADC at pickup/hold/release;
- retry attempts;
- faults;
- manual service commands.

Minimum event record:

```c
typedef struct {
    uint32_t ts_ms;
    uint16_t row;
    sower_state_t state;
    fault_code_t fault;
    uint16_t vacuum_adc;
    int32_t value;
} event_log_record_t;
```

---

## 15. Testing rules

Host tests:

- recipe validation ranges;
- FSM transition table;
- retry policy;
- fault severity mapping;
- vacuum threshold logic;
- cassette index calculations.

Hardware tests:

- I2S outputs map to expected TinyBee outputs.
- TMC2209 current setting and motor direction.
- cassette sensor debounce and polarity.
- ADC vacuum calibration.
- valve mutual exclusion.
- Start/Stop behavior.
- E-stop hardware cut.
- CPU headroom with motion + display + web enabled.
- worst-case display redraw does not delay motion/control deadlines.

Acceptance for first bring-up:

- Boot leaves all outputs off.
- Manual valve test cannot open vacuum and pressure simultaneously.
- Conveyor feed stops on cassette sensor.
- Timeout stops conveyor if cassette is not detected.
- Homing failure prevents production cycle.
- Production dry-run keeps `control_task` responsive while display and web are enabled.

---

## 16. Naming and coding style

- C modules use `snake_case`.
- Public module functions start with module prefix: `board_`, `motion_`, `cassette_`, `sower_`.
- Public types end with `_t`.
- Enum values use uppercase module prefix.
- Time variables include unit suffix: `_ms`, `_us`.
- Distance variables include unit suffix: `_mm`, `_steps`.
- ADC raw values include `_adc` or `_raw`.
- No global mutable state outside module-owned static context.
- No hidden hardware access in macros except pin constants.
- Комментарии в коде пишутся на русском языке.
- Комментарии объясняют неочевидные ограничения по времени, безопасности, железу и технологическому процессу.
- Комментарии не пересказывают очевидные присваивания и имена функций.
- Имена файлов, модулей, функций, типов и enum остаются на английском языке для совместимости с ESP-IDF, CMake, grblHAL и внешними библиотеками.

---

## 17. Hard prohibitions

- No direct GPIO access outside `board_tinybee`, except inside low-level display driver if needed.
- No direct grblHAL calls outside `motion_adapter`.
- No delays like `vTaskDelay()` inside safety-critical state transitions unless state remains serviceable and cancellable.
- No blocking network/storage calls in `control_task`.
- No display rendering, JSON generation or flash write in `control_task`.
- No magic I2SO numbers in application/machine modules.
- No production cycle when config is invalid.
- No production cycle when tools are not homed.
- No software-only E-stop design.
- No inductive load on TinyBee MOSFET outputs without flyback protection.
