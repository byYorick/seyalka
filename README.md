# Sower TinyBee

Проект прошивки сеялки автовысева на базе MakerBase MKS TinyBee V1.0.

## Сборка из корня

```bash
cd /home/georgiy/esp/solo/seyalka
source ./export_idf.sh
idf.py build
```

Исходники прошивки лежат в `firmware/`, но ESP-IDF проект настроен так, чтобы собираться из корня репозитория.

## Bring-up режимы

По умолчанию прошивка стартует с выключенными `HE/FAN` выходами.
После `board_init()` запускается `io_task`: он обновляет входы каждые `10 ms` и raw ADC вакуума каждые `20 ms`.

Для визуального теста светодиодов `H-BED`, `H-E0`, `H-E1`, `FAN1`, `FAN2` включить:

```bash
idf.py menuconfig
```

Далее: `Sower bring-up options` -> `Run TinyBee HE/FAN output LED test on boot`.

Этот режим использовать только без подключенных нагревателей, клапанов, насосов и других силовых нагрузок.

Для проверки входов и raw ADC вакуума включить:

```bash
idf.py menuconfig
```

Далее: `Sower bring-up options` -> `Print TinyBee input and vacuum ADC monitor`.

После прошивки открыть:

```bash
idf.py monitor
```

В логе будут `stable/raw` состояния входов и `vacuum_adc raw`.

## Документация

- `docs/SOWER_PROJECT_ARCHITECTURE.md`
- `docs/SOWER_CODE_ARCHITECTURE_RULES.md`
- `docs/SOWER_FIRMWARE_IMPLEMENTATION_PLAN.md`
- `docs/MKS_TINYBEE_V1_PINMAP_AND_CUSTOM_FW.md`
