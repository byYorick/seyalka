#include "tmc2209.h"

#include <math.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#if CONFIG_SOWER_TMC2209_ENABLE

static const char *TAG = "tmc2209";

/* Регистры TMC2209 (7-bit адрес в UART-пакете). */
#define TMC2209_REG_GCONF       0x00
#define TMC2209_REG_GSTAT       0x01
#define TMC2209_REG_IFCNT       0x02
#define TMC2209_REG_IHOLD_IRUN  0x10
#define TMC2209_REG_TPOWERDOWN  0x11
#define TMC2209_REG_CHOPCONF    0x6C
#define TMC2209_REG_PWMCONF     0x70

#define TMC2209_SYNC_BYTE       0x05
#define TMC2209_UART_TIMEOUT_MS 50

static SemaphoreHandle_t s_uart_lock;
static bool s_initialized;


static uart_port_t uart_port(void)
{
    return (uart_port_t)CONFIG_SOWER_TMC2209_UART_PORT;
}

static uint8_t tmc_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc >> 7) ^ (byte & 1)) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
            byte >>= 1;
        }
    }

    return crc;
}

static esp_err_t uart_write_frame(const uint8_t *frame, size_t len)
{
    const int written = uart_write_bytes(uart_port(), (const char *)frame, len);
    ESP_RETURN_ON_FALSE(written == (int)len, ESP_FAIL, TAG, "uart write failed");
    return uart_wait_tx_done(uart_port(), pdMS_TO_TICKS(TMC2209_UART_TIMEOUT_MS));
}

static esp_err_t uart_read_bytes(uint8_t *buffer, size_t len, uint32_t timeout_ms)
{
    const int read = uart_read_bytes(uart_port(), buffer, len, pdMS_TO_TICKS(timeout_ms));
    ESP_RETURN_ON_FALSE(read == (int)len, ESP_ERR_TIMEOUT, TAG, "uart read timeout");
    return ESP_OK;
}

static void build_write_frame(uint8_t addr, uint8_t reg, uint32_t value, uint8_t *frame)
{
    frame[0] = TMC2209_SYNC_BYTE;
    frame[1] = addr & 0x7F;
    frame[2] = reg | 0x80;
    frame[3] = (value >> 24) & 0xFF;
    frame[4] = (value >> 16) & 0xFF;
    frame[5] = (value >> 8) & 0xFF;
    frame[6] = value & 0xFF;
    frame[7] = tmc_crc8(frame, 7);
}

static void build_read_request(uint8_t addr, uint8_t reg, uint8_t *frame)
{
    memset(frame, 0, 8);
    frame[0] = TMC2209_SYNC_BYTE;
    frame[1] = addr & 0x7F;
    frame[2] = reg;
    frame[7] = tmc_crc8(frame, 7);
}

static esp_err_t write_register(tmc2209_addr_t addr, uint8_t reg, uint32_t value)
{
    uint8_t frame[8];
    build_write_frame((uint8_t)addr, reg, value, frame);
    return uart_write_frame(frame, sizeof(frame));
}

static esp_err_t read_register(tmc2209_addr_t addr, uint8_t reg, uint32_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t request[8];
    uint8_t delayed[8];
    uint8_t response[8];

    build_read_request((uint8_t)addr, reg, request);
    ESP_RETURN_ON_ERROR(uart_write_frame(request, sizeof(request)), TAG, "read request failed");

    /* TMC2209 отдаёт ответ с задержкой одного UART-кадра. */
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(uart_read_bytes(delayed, sizeof(delayed), TMC2209_UART_TIMEOUT_MS),
                        TAG,
                        "read delayed frame failed");
    ESP_RETURN_ON_ERROR(uart_read_bytes(response, sizeof(response), TMC2209_UART_TIMEOUT_MS),
                        TAG,
                        "read response failed");

    if (response[0] != TMC2209_SYNC_BYTE || response[1] != ((uint8_t)addr & 0x7F) || response[2] != reg) {
        ESP_LOGE(TAG,
                 "unexpected read header addr=%u reg=0x%02X got %02X %02X %02X",
                 addr,
                 reg,
                 response[0],
                 response[1],
                 response[2]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (tmc_crc8(response, 7) != response[7]) {
        ESP_LOGE(TAG, "read crc mismatch addr=%u reg=0x%02X", addr, reg);
        return ESP_FAIL;
    }

    *value = ((uint32_t)response[3] << 24) |
             ((uint32_t)response[4] << 16) |
             ((uint32_t)response[5] << 8) |
             (uint32_t)response[6];
    return ESP_OK;
}

static uint8_t current_ma_to_cs(uint16_t rms_ma, float rsense_ohm)
{
    const float irms = (float)rms_ma / 1000.0f;
    int cs = (int)lroundf(32.0f * irms * 1.41421356f * rsense_ohm / 0.325f - 1.0f);

    if (cs < 0) {
        cs = 0;
    }
    if (cs > 31) {
        cs = 31;
    }

    return (uint8_t)cs;
}

static uint8_t microsteps_to_mres(uint16_t microsteps)
{
    switch (microsteps) {
    case 256: return 0;
    case 128: return 1;
    case 64: return 2;
    case 32: return 3;
    case 16: return 4;
    case 8: return 5;
    case 4: return 6;
    case 2: return 7;
    case 1: return 8;
    default: return 4;
    }
}

static uint32_t build_gconf(bool stealthchop)
{
    uint32_t gconf = 0;
    gconf |= (1U << 6); /* pdn_disable: UART вместо PDN */
    gconf |= (1U << 7); /* mstep_reg_select */
    /* GCONF.en_spreadCycle (bit 2): 0 = StealthChop, 1 = spreadCycle */
    if (!stealthchop) {
        gconf |= (1U << 2);
    }
    return gconf;
}

static uint32_t build_ihold_irun(uint8_t irun_cs, uint8_t ihold_cs)
{
    return ((uint32_t)(irun_cs & 0x1F) << 8) | (ihold_cs & 0x1F);
}

static uint32_t build_chopconf(uint16_t microsteps)
{
    const uint8_t mres = microsteps_to_mres(microsteps);
    return ((uint32_t)mres << 24) | 0x10000053;
}

static uint32_t build_pwmconf(bool stealthchop)
{
    if (!stealthchop) {
        return 0;
    }

    uint32_t pwm = 0;
    pwm |= (1U << 0); /* pwm_autoscale */
    pwm |= (1U << 1); /* pwm_autograd */
    pwm |= (1U << 18); /* pwm_freq ~2/683 f_clk */
    return pwm;
}

static esp_err_t configure_axis_locked(tmc2209_addr_t addr, const tmc2209_axis_config_t *config)
{
    const uint8_t irun = current_ma_to_cs(config->run_current_ma, config->rsense_ohm);
    const uint16_t hold_ma = config->hold_current_ma > 0
                                 ? config->hold_current_ma
                                 : (uint16_t)(config->run_current_ma / 2);
    const uint8_t ihold = current_ma_to_cs(hold_ma, config->rsense_ohm);

    ESP_RETURN_ON_ERROR(write_register(addr, TMC2209_REG_GCONF, build_gconf(config->stealthchop)),
                        TAG,
                        "GCONF failed");
    ESP_RETURN_ON_ERROR(write_register(addr, TMC2209_REG_IHOLD_IRUN, build_ihold_irun(irun, ihold)),
                        TAG,
                        "IHOLD_IRUN failed");
    ESP_RETURN_ON_ERROR(write_register(addr, TMC2209_REG_TPOWERDOWN, 20),
                        TAG,
                        "TPOWERDOWN failed");
    ESP_RETURN_ON_ERROR(write_register(addr, TMC2209_REG_CHOPCONF, build_chopconf(config->microsteps)),
                        TAG,
                        "CHOPCONF failed");
    ESP_RETURN_ON_ERROR(write_register(addr, TMC2209_REG_PWMCONF, build_pwmconf(config->stealthchop)),
                        TAG,
                        "PWMCONF failed");

    ESP_LOGI(TAG,
             "axis %u configured: irun=%u (%.2f A rms), microsteps=%u, stealth=%d",
             addr,
             irun,
             (double)config->run_current_ma / 1000.0,
             config->microsteps,
             config->stealthchop);
    return ESP_OK;
}

#endif /* CONFIG_SOWER_TMC2209_ENABLE */

esp_err_t tmc2209_get_default_config(tmc2209_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_SOWER_TMC2209_ENABLE
    const float rsense = (float)CONFIG_SOWER_TMC2209_RSENSE_MILLIOHM / 1000.0f;
    const tmc2209_axis_config_t axis_cfg = {
        .run_current_ma = CONFIG_SOWER_TMC2209_RUN_CURRENT_MA,
        .hold_current_ma = CONFIG_SOWER_TMC2209_RUN_CURRENT_MA / 2,
        .microsteps = CONFIG_SOWER_TMC2209_MICROSTEPS,
        .stealthchop = CONFIG_SOWER_TMC2209_STEALTHCHOP,
        .rsense_ohm = rsense,
    };
#else
    const tmc2209_axis_config_t axis_cfg = {
        .run_current_ma = 1500,
        .hold_current_ma = 750,
        .microsteps = 16,
        .stealthchop = true,
        .rsense_ohm = 0.11f,
    };
#endif

    config->conveyor = axis_cfg;
    config->dibbler = axis_cfg;
    config->transfer = axis_cfg;
    return ESP_OK;
}

esp_err_t tmc2209_init(void)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    return ESP_OK;
#else
    if (s_initialized) {
        return ESP_OK;
    }

    s_uart_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_uart_lock, ESP_ERR_NO_MEM, TAG, "uart mutex allocation failed");

    const uart_config_t uart_cfg = {
        .baud_rate = CONFIG_SOWER_TMC2209_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(uart_port(), 256, 256, 0, NULL, 0),
                        TAG,
                        "uart install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(uart_port(), &uart_cfg), TAG, "uart param failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(uart_port(),
                                     CONFIG_SOWER_TMC2209_TX_GPIO,
                                     CONFIG_SOWER_TMC2209_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart pins failed");

    uart_flush(uart_port());
    s_initialized = true;

    ESP_LOGI(TAG,
             "UART%d init on TX=%d RX=%d baud=%d",
             CONFIG_SOWER_TMC2209_UART_PORT,
             CONFIG_SOWER_TMC2209_TX_GPIO,
             CONFIG_SOWER_TMC2209_RX_GPIO,
             CONFIG_SOWER_TMC2209_BAUD);
    return ESP_OK;
#endif
}

esp_err_t tmc2209_deinit(void)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    return ESP_OK;
#else
    if (!s_initialized) {
        return ESP_OK;
    }

    uart_driver_delete(uart_port());
    if (s_uart_lock) {
        vSemaphoreDelete(s_uart_lock);
        s_uart_lock = NULL;
    }
    s_initialized = false;
    return ESP_OK;
#endif
}

esp_err_t tmc2209_read_register(tmc2209_addr_t addr, uint8_t reg, uint32_t *value)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    (void)addr;
    (void)reg;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (xSemaphoreTake(s_uart_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = read_register(addr, reg, value);
    xSemaphoreGive(s_uart_lock);
    return err;
#endif
}

esp_err_t tmc2209_configure_axis(tmc2209_addr_t addr, const tmc2209_axis_config_t *config)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    (void)addr;
    (void)config;
    return ESP_OK;
#else
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->run_current_ma > 0, ESP_ERR_INVALID_ARG, TAG, "run current is zero");
    ESP_RETURN_ON_FALSE(config->rsense_ohm > 0.0f, ESP_ERR_INVALID_ARG, TAG, "rsense is zero");

    if (xSemaphoreTake(s_uart_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = configure_axis_locked(addr, config);
    xSemaphoreGive(s_uart_lock);
    return err;
#endif
}

esp_err_t tmc2209_configure_all(const tmc2209_config_t *config)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    (void)config;
    return ESP_OK;
#else
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is null");

    ESP_RETURN_ON_ERROR(tmc2209_configure_axis(TMC2209_ADDR_X, &config->conveyor),
                        TAG,
                        "X configure failed");
    ESP_RETURN_ON_ERROR(tmc2209_configure_axis(TMC2209_ADDR_Z, &config->dibbler),
                        TAG,
                        "Z configure failed");
    ESP_RETURN_ON_ERROR(tmc2209_configure_axis(TMC2209_ADDR_E0, &config->transfer),
                        TAG,
                        "E0 configure failed");
    return ESP_OK;
#endif
}

esp_err_t tmc2209_verify_axis(tmc2209_addr_t addr)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    (void)addr;
    return ESP_OK;
#else
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (xSemaphoreTake(s_uart_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t ifcnt_before = 0;
    uint32_t ifcnt_after = 0;
    uint32_t ihold_irun = 0;
    esp_err_t err = read_register(addr, TMC2209_REG_IFCNT, &ifcnt_before);
    if (err == ESP_OK) {
        err = write_register(addr, TMC2209_REG_TPOWERDOWN, 20);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        err = read_register(addr, TMC2209_REG_IFCNT, &ifcnt_after);
    }
    if (err == ESP_OK) {
        err = read_register(addr, TMC2209_REG_IHOLD_IRUN, &ihold_irun);
    }

    xSemaphoreGive(s_uart_lock);

    ESP_RETURN_ON_ERROR(err, TAG, "verify communication failed");

    const uint8_t before = ifcnt_before & 0xFF;
    const uint8_t after = ifcnt_after & 0xFF;
    if (after == before) {
        ESP_LOGE(TAG, "axis %u IFCNT did not increment (%u -> %u)", addr, before, after);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "axis %u uart ok, IFCNT %u->%u, IHOLD_IRUN=0x%08lx",
             addr,
             before,
             after,
             (unsigned long)ihold_irun);
    return ESP_OK;
#endif
}

esp_err_t tmc2209_verify_sower_axes(void)
{
#if !CONFIG_SOWER_TMC2209_ENABLE
    return ESP_OK;
#else
    ESP_RETURN_ON_ERROR(tmc2209_verify_axis(TMC2209_ADDR_X), TAG, "X verify failed");
    ESP_RETURN_ON_ERROR(tmc2209_verify_axis(TMC2209_ADDR_Z), TAG, "Z verify failed");
    ESP_RETURN_ON_ERROR(tmc2209_verify_axis(TMC2209_ADDR_E0), TAG, "E0 verify failed");
    return ESP_OK;
#endif
}
