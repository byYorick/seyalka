#include "board_tinybee.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "grblhal_i2s_out.h"

static const char *TAG = "board_tinybee";
static const uint32_t INPUT_DEBOUNCE_MS = 20;

static portMUX_TYPE s_board_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_outputs[BOARD_OUT_COUNT];
static uint16_t s_pwm_permille[BOARD_OUT_COUNT];
static board_input_snapshot_t s_inputs;
static bool s_inputs_initialized;
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_channel_t s_vacuum_adc_channel;
static bool s_vacuum_adc_ready;
static uint16_t s_vacuum_adc_raw;
static uint32_t s_vacuum_adc_updated_ms;
static bool s_vacuum_adc_valid;

static int input_to_gpio(board_input_t in)
{
    switch (in) {
    case BOARD_IN_CASSETTE_SENSOR: return TB_GPIO_CASSETTE_SENSOR;
    case BOARD_IN_DIBBLER_HOME: return TB_GPIO_DIBBLER_HOME;
    case BOARD_IN_TRANSFER_HOME: return TB_GPIO_TRANSFER_HOME;
    case BOARD_IN_START_STOP: return TB_GPIO_START_STOP;
    case BOARD_IN_ENCODER_A: return TB_GPIO_ENCODER_A;
    case BOARD_IN_ENCODER_B: return TB_GPIO_ENCODER_B;
    case BOARD_IN_ENCODER_BUTTON: return TB_GPIO_ENCODER_BUTTON;
    default: return -1;
    }
}

static esp_err_t configure_inputs(void)
{
    const uint64_t mask =
        (1ULL << TB_GPIO_CASSETTE_SENSOR) |
        (1ULL << TB_GPIO_DIBBLER_HOME) |
        (1ULL << TB_GPIO_TRANSFER_HOME) |
        (1ULL << TB_GPIO_START_STOP) |
        (1ULL << TB_GPIO_ENCODER_A) |
        (1ULL << TB_GPIO_ENCODER_B) |
        (1ULL << TB_GPIO_ENCODER_BUTTON);

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

static esp_err_t configure_vacuum_adc(void)
{
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;

    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(TB_GPIO_VACUUM_ADC, &unit, &channel),
                        TAG,
                        "vacuum adc gpio mapping failed");

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle), TAG, "vacuum adc unit init failed");

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, channel, &channel_cfg),
                        TAG,
                        "vacuum adc channel config failed");

    s_vacuum_adc_channel = channel;
    s_vacuum_adc_ready = true;
    ESP_LOGI(TAG, "vacuum ADC configured on GPIO%d, unit=%d, channel=%d",
             TB_GPIO_VACUUM_ADC,
             (int)unit,
             (int)channel);
    return ESP_OK;
}

static int output_to_i2so_bit(board_output_t out)
{
    switch (out) {
    case BOARD_OUT_VACUUM_PUMP: return TB_I2SO_HE_BED;
    case BOARD_OUT_VACUUM_VALVE: return TB_I2SO_HE0;
    case BOARD_OUT_PRESSURE_VALVE: return TB_I2SO_HE1;
    case BOARD_OUT_VIBRO_PWM: return TB_I2SO_FAN1;
    case BOARD_OUT_SPARE: return TB_I2SO_FAN2;
    case BOARD_OUT_BEEPER: return TB_I2SO_BEEPER;
    default: return -1;
    }
}

esp_err_t board_init(void)
{
    const grblhal_i2s_out_config_t i2s_out_cfg = {
        .ws_gpio = TB_I2S_WS_GPIO,
        .bck_gpio = TB_I2S_BCK_GPIO,
        .data_gpio = TB_I2S_DATA_GPIO,
        .init_value = 0,
    };

    ESP_RETURN_ON_ERROR(configure_inputs(), TAG, "input configuration failed");
    ESP_RETURN_ON_ERROR(configure_vacuum_adc(), TAG, "vacuum adc configuration failed");
    ESP_RETURN_ON_ERROR(grblhal_i2s_out_init(&i2s_out_cfg), TAG, "grblHAL i2s_out init failed");
    ESP_RETURN_ON_ERROR(board_safe_outputs_off(), TAG, "safe outputs failed");
    ESP_RETURN_ON_ERROR(board_update_inputs(), TAG, "initial input update failed");
    ESP_RETURN_ON_ERROR(board_update_vacuum_adc(), TAG, "initial vacuum adc update failed");

    ESP_LOGI(TAG, "TinyBee static outputs use grblHAL i2s_out passthrough backend");
    return ESP_OK;
}

esp_err_t board_safe_outputs_off(void)
{
    portENTER_CRITICAL(&s_board_state_lock);
    for (int i = 0; i < BOARD_OUT_COUNT; ++i) {
        s_outputs[i] = false;
        s_pwm_permille[i] = 0;
    }
    portEXIT_CRITICAL(&s_board_state_lock);

    for (int out = 0; out < BOARD_OUT_COUNT; ++out) {
        const int i2so_bit = output_to_i2so_bit((board_output_t)out);
        if (i2so_bit >= 0) {
            ESP_RETURN_ON_ERROR(grblhal_i2s_out_write((uint8_t)i2so_bit, false), TAG, "safe output write failed");
        }
    }
    grblhal_i2s_out_delay();
    return ESP_OK;
}

esp_err_t board_set_output(board_output_t out, bool on)
{
    if (out >= BOARD_OUT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_board_state_lock);
    s_outputs[out] = on;
    portEXIT_CRITICAL(&s_board_state_lock);

    const int i2so_bit = output_to_i2so_bit(out);
    if (i2so_bit >= 0) {
        ESP_RETURN_ON_ERROR(grblhal_i2s_out_write((uint8_t)i2so_bit, on), TAG, "output write failed");
        grblhal_i2s_out_delay();
    }

    ESP_LOGD(TAG, "output %d = %d", out, on);
    return ESP_OK;
}

esp_err_t board_set_pwm(board_output_t out, uint16_t duty_permille)
{
    if (out >= BOARD_OUT_COUNT || duty_permille > 1000) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_board_state_lock);
    s_pwm_permille[out] = duty_permille;
    portEXIT_CRITICAL(&s_board_state_lock);

    ESP_RETURN_ON_ERROR(board_set_output(out, duty_permille > 0), TAG, "pwm output write failed");
    ESP_LOGD(TAG, "pwm output %d = %u permille", out, duty_permille);
    return ESP_OK;
}

esp_err_t board_update_inputs(void)
{
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    for (int i = 0; i < BOARD_IN_COUNT; ++i) {
        const int gpio = input_to_gpio((board_input_t)i);
        if (gpio < 0) {
            continue;
        }

        const bool raw = gpio_get_level(gpio) != 0;
        portENTER_CRITICAL(&s_board_state_lock);
        if (!s_inputs_initialized) {
            s_inputs.raw[i] = raw;
            s_inputs.stable[i] = raw;
            s_inputs.last_change_ms[i] = now_ms;
            portEXIT_CRITICAL(&s_board_state_lock);
            continue;
        }

        if (raw != s_inputs.raw[i]) {
            s_inputs.raw[i] = raw;
            s_inputs.last_change_ms[i] = now_ms;
        }

        /* Stable меняется только после выдержки, чтобы короткие помехи не попадали в FSM. */
        if (raw != s_inputs.stable[i] && (now_ms - s_inputs.last_change_ms[i]) >= INPUT_DEBOUNCE_MS) {
            s_inputs.stable[i] = raw;
        }
        portEXIT_CRITICAL(&s_board_state_lock);
    }

    portENTER_CRITICAL(&s_board_state_lock);
    s_inputs.updated_ms = now_ms;
    s_inputs_initialized = true;
    portEXIT_CRITICAL(&s_board_state_lock);
    return ESP_OK;
}

bool board_get_input(board_input_t in)
{
    if (in >= BOARD_IN_COUNT) {
        return false;
    }

    (void)board_update_inputs();
    portENTER_CRITICAL(&s_board_state_lock);
    const bool value = s_inputs.stable[in];
    portEXIT_CRITICAL(&s_board_state_lock);
    return value;
}

bool board_get_input_raw(board_input_t in)
{
    if (in >= BOARD_IN_COUNT) {
        return false;
    }

    (void)board_update_inputs();
    portENTER_CRITICAL(&s_board_state_lock);
    const bool value = s_inputs.raw[in];
    portEXIT_CRITICAL(&s_board_state_lock);
    return value;
}

static esp_err_t read_vacuum_adc_direct(uint16_t *adc_raw)
{
    if (!adc_raw) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_vacuum_adc_ready || !s_adc_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, s_vacuum_adc_channel, &raw),
                        TAG,
                        "vacuum adc read failed");
    if (raw < 0 || raw > UINT16_MAX) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *adc_raw = (uint16_t)raw;
    return ESP_OK;
}

esp_err_t board_update_vacuum_adc(void)
{
    uint16_t raw = 0;
    const esp_err_t err = read_vacuum_adc_direct(&raw);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_board_state_lock);
        s_vacuum_adc_valid = false;
        portEXIT_CRITICAL(&s_board_state_lock);
        return err;
    }

    portENTER_CRITICAL(&s_board_state_lock);
    s_vacuum_adc_raw = raw;
    s_vacuum_adc_updated_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    s_vacuum_adc_valid = true;
    portEXIT_CRITICAL(&s_board_state_lock);
    return ESP_OK;
}

esp_err_t board_read_vacuum_adc(uint16_t *adc_raw)
{
    if (!adc_raw) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_board_state_lock);
    const bool adc_valid = s_vacuum_adc_valid;
    portEXIT_CRITICAL(&s_board_state_lock);

    if (!adc_valid) {
        ESP_RETURN_ON_ERROR(board_update_vacuum_adc(), TAG, "vacuum adc cache update failed");
    }

    portENTER_CRITICAL(&s_board_state_lock);
    *adc_raw = s_vacuum_adc_raw;
    portEXIT_CRITICAL(&s_board_state_lock);
    return ESP_OK;
}

esp_err_t board_get_status_snapshot(board_status_snapshot_t *snapshot)
{
    if (!snapshot) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(board_update_inputs(), TAG, "snapshot input update failed");

    portENTER_CRITICAL(&s_board_state_lock);
    snapshot->inputs = s_inputs;
    for (int i = 0; i < BOARD_OUT_COUNT; ++i) {
        snapshot->outputs[i] = s_outputs[i];
        snapshot->pwm_permille[i] = s_pwm_permille[i];
    }
    snapshot->vacuum_adc_raw = s_vacuum_adc_raw;
    snapshot->vacuum_adc_updated_ms = s_vacuum_adc_updated_ms;
    snapshot->vacuum_adc_valid = s_vacuum_adc_valid;
    portEXIT_CRITICAL(&s_board_state_lock);

    return ESP_OK;
}
