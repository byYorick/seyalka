# Sower TinyBee firmware

ESP-IDF firmware for the seed sower based on MakerBase MKS TinyBee V1.0.

## Environment

```bash
cd /home/georgiy/esp/solo/seyalka
source ./export_idf.sh
idf.py build
idf.py flash monitor
```

The main ESP-IDF project is rooted at `/home/georgiy/esp/solo/seyalka`.
`export_idf.sh` pins the project to `/home/georgiy/esp/esp-idf`, currently ESP-IDF v5.5.3.

## Architecture

See:

- `../docs/SOWER_PROJECT_ARCHITECTURE.md`
- `../docs/SOWER_CODE_ARCHITECTURE_RULES.md`
- `../docs/MKS_TINYBEE_V1_PINMAP_AND_CUSTOM_FW.md`
- `../docs/SOWER_FIRMWARE_IMPLEMENTATION_PLAN.md`
- `../docs/SOWER_HARDWARE_BRINGUP.md`

## Bring-up (Kconfig)

Open **Component config → Sower bring-up options** (`idf.py menuconfig`).

| Option | Default | Purpose |
|--------|---------|---------|
| `SOWER_OUTPUT_LED_TEST` | off | Cycle I2SO outputs mapped to HE-BED, HE-E0, HE-E1, FAN1, FAN2 |
| `SOWER_IO_MONITOR` | off | Log debounced inputs and raw vacuum ADC periodically |
| `SOWER_IO_MONITOR_PERIOD_MS` | 1000 | Monitor period when IO monitor is enabled |
| `SOWER_CONVEYOR_MOTION_TEST` | off | Move X axis forward/back after boot |
| `SOWER_CONVEYOR_MOTION_TEST_DISTANCE_MM` | 10 | One leg distance for conveyor test |
| `SOWER_CONVEYOR_MOTION_TEST_SPEED_MM_S` | 5 | Conveyor test speed |

**Production/default boot:** all options off — outputs stay under safety control, no motion test.

### Typical bring-up sequence

1. Build and flash with default config; confirm no HE/FAN toggling.
2. Enable `SOWER_IO_MONITOR`, verify inputs and vacuum ADC in serial log; record polarity in `docs/SOWER_HARDWARE_BRINGUP.md`.
3. Enable `SOWER_CONVEYOR_MOTION_TEST` with small distance/speed; verify X STEP/DIR/EN without dangerous load.
4. Do not enable LED test and conveyor test together unless you understand shared I2SO timing.

### Serial log tags

- `app_main` — boot and bring-up task start
- `io_service` — input/ADC monitor lines
- `motion_adapter` — step moves and timeouts
- `board_tinybee` — board init
- `safety` — safe state and periodic checks

## Motion layer

All step/dir/enable signals go through `motion_adapter` (I2SO via `grblhal_i2s_out`). Application code must not call GPIO or I2SO directly.

Axes v1:

| API axis | TinyBee | Role |
|----------|---------|------|
| `MOTION_AXIS_CONVEYOR` | X | belt / cassette feed |
| `MOTION_AXIS_DIBBLER` | Z | dibbler tool |
| `MOTION_AXIS_TRANSFER` | E0 | transfer arm |

`motion_home_axis()` — homing Z (dibbler) and E0 (transfer) toward home switch (`BOARD_IN_*_HOME`, stable=1 = at home). Conveyor has no homing. Homing uses 25% of axis `max_speed_mm_s`, 2 mm chunks.

`cassette_wait_and_capture()` feeds the belt in 5 mm chunks until `BOARD_IN_CASSETTE_SENSOR` is active or timeout.

## TMC2209 (optional UART)

**Component config → Sower TMC2209 drivers →** enable `SOWER_TMC2209_ENABLE`.

| Setting | Default | Notes |
|---------|---------|-------|
| UART | UART2, TX=17, RX=16 | MKS TinyBee UART2 (conflicts with LCD D5/D7 if display used) |
| Run current | 1500 mA RMS | Project target; verify driver temperature |
| Microsteps | 16 | Must match DIP (MS1/MS2) when MS3 removed for UART |
| StealthChop | on | Quiet mode; spreadCycle if disabled |

**Hardware:** stock TinyBee often runs TMC2209 in standalone mode (potentiometer + DIP). UART needs MS3 jumper off and a shared UART bus to drivers (see MKS-TinyBee issue #6, `docs/SOWER_HARDWARE_BRINGUP.md`).

Configured axes: **X** (conveyor, addr 0), **Z** (dibbler, addr 2), **E0** (transfer, addr 3). Y (addr 1) is unused in v1.

On communication failure: `FAULT_TMC_COMM_FAILED`; with `SOWER_TMC2209_FAIL_ON_COMM_ERROR`, boot enters safe state.
