#include "grblhal_i2s_out.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rom/gpio.h"
#include "rom/lldesc.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2s_struct.h"

static const char *TAG = "grblhal_i2s_out";

#define I2S_SAMPLE_SIZE_BYTES 4U
#define I2S_DMA_BUFFER_COUNT 5U
#define I2S_DMA_BUFFER_LEN 2000U
#define I2S_DMA_SAMPLE_COUNT (I2S_DMA_BUFFER_LEN / I2S_SAMPLE_SIZE_BYTES)
#define I2S_USEC_PER_SAMPLE 4U
#define BIT_U32(bit) (1UL << (bit))

typedef struct {
    uint32_t **buffers;
    lldesc_t **descriptors;
    QueueHandle_t queue;
} i2s_dma_context_t;

static atomic_uint_least32_t s_port_state = ATOMIC_VAR_INIT(0);
static portMUX_TYPE s_i2s_lock = portMUX_INITIALIZER_UNLOCKED;
static i2s_dma_context_t s_dma;
static intr_handle_t s_i2s_intr;
static uint8_t s_ws_gpio = 255;
static uint8_t s_bck_gpio = 255;
static uint8_t s_data_gpio = 255;
static bool s_initialized;

static void gpio_matrix_out_checked(uint8_t gpio, uint32_t signal_idx)
{
    if (gpio == 255) {
        return;
    }

    gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
    gpio_matrix_out(gpio, signal_idx, false, false);
}

static void i2s_attach_gpio(void)
{
    gpio_matrix_out_checked(s_data_gpio, I2S0O_DATA_OUT23_IDX);
    gpio_matrix_out_checked(s_bck_gpio, I2S0O_BCK_OUT_IDX);
    gpio_matrix_out_checked(s_ws_gpio, I2S0O_WS_OUT_IDX);
}

static void i2s_reset_fifo_unlocked(void)
{
    I2S0.conf.rx_fifo_reset = 1;
    I2S0.conf.rx_fifo_reset = 0;
    I2S0.conf.tx_fifo_reset = 1;
    I2S0.conf.tx_fifo_reset = 0;
}

static void i2s_clear_dma_descriptor(lldesc_t *descriptor, uint32_t value)
{
    uint32_t *buffer = (uint32_t *)descriptor->buf;

    for (size_t i = 0; i < I2S_DMA_SAMPLE_COUNT; ++i) {
        buffer[i] = value;
    }

    descriptor->owner = 1;
    descriptor->eof = 1;
    descriptor->length = I2S_DMA_BUFFER_LEN;
    descriptor->size = I2S_DMA_BUFFER_LEN;
}

static void i2s_prepare_dma_ring(uint32_t value)
{
    for (size_t i = 0; i < I2S_DMA_BUFFER_COUNT; ++i) {
        s_dma.descriptors[i]->owner = 1;
        s_dma.descriptors[i]->eof = 1;
        s_dma.descriptors[i]->sosf = 0;
        s_dma.descriptors[i]->length = I2S_DMA_BUFFER_LEN;
        s_dma.descriptors[i]->size = I2S_DMA_BUFFER_LEN;
        s_dma.descriptors[i]->buf = (uint8_t *)s_dma.buffers[i];
        s_dma.descriptors[i]->offset = 0;
        s_dma.descriptors[i]->qe.stqe_next =
            (lldesc_t *)((i + 1U < I2S_DMA_BUFFER_COUNT) ? s_dma.descriptors[i + 1U] : s_dma.descriptors[0]);
        i2s_clear_dma_descriptor(s_dma.descriptors[i], value);
    }
}

static void IRAM_ATTR i2s_intr_handler(void *arg)
{
    (void)arg;
    BaseType_t high_priority_task_awoken = pdFALSE;

    if (I2S0.int_st.out_eof) {
        lldesc_t *finished = (lldesc_t *)I2S0.out_eof_des_addr;
        if (xQueueIsQueueFullFromISR(s_dma.queue)) {
            lldesc_t *dropped = NULL;
            xQueueReceiveFromISR(s_dma.queue, &dropped, &high_priority_task_awoken);
        }
        xQueueSendFromISR(s_dma.queue, &finished, &high_priority_task_awoken);
    }

    I2S0.int_clr.val = I2S0.int_st.val;

    if (high_priority_task_awoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void i2s_dma_task(void *arg)
{
    (void)arg;
    lldesc_t *descriptor = NULL;

    while (true) {
        if (xQueueReceive(s_dma.queue, &descriptor, portMAX_DELAY) == pdTRUE && descriptor) {
            /* В passthrough-режиме данные выходов идут через conf_single_data, DMA нужен для непрерывного WS/BCK. */
            i2s_clear_dma_descriptor(descriptor, 0);
        }
    }
}

static esp_err_t allocate_dma(void)
{
    s_dma.buffers = heap_caps_calloc(I2S_DMA_BUFFER_COUNT, sizeof(uint32_t *), MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_dma.buffers, ESP_ERR_NO_MEM, TAG, "dma buffer table allocation failed");

    s_dma.descriptors = heap_caps_calloc(I2S_DMA_BUFFER_COUNT, sizeof(lldesc_t *), MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_dma.descriptors, ESP_ERR_NO_MEM, TAG, "dma descriptor table allocation failed");

    for (size_t i = 0; i < I2S_DMA_BUFFER_COUNT; ++i) {
        s_dma.buffers[i] = heap_caps_calloc(1, I2S_DMA_BUFFER_LEN, MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(s_dma.buffers[i], ESP_ERR_NO_MEM, TAG, "dma buffer allocation failed");

        s_dma.descriptors[i] = heap_caps_calloc(1, sizeof(lldesc_t), MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(s_dma.descriptors[i], ESP_ERR_NO_MEM, TAG, "dma descriptor allocation failed");
    }

    s_dma.queue = xQueueCreate(I2S_DMA_BUFFER_COUNT, sizeof(lldesc_t *));
    ESP_RETURN_ON_FALSE(s_dma.queue, ESP_ERR_NO_MEM, TAG, "dma queue allocation failed");
    return ESP_OK;
}

static esp_err_t start_i2s(uint32_t init_value)
{
    portENTER_CRITICAL(&s_i2s_lock);

    i2s_prepare_dma_ring(0);
    I2S0.out_link.addr = (uint32_t)s_dma.descriptors[0];
    I2S0.out_link.stop = 1;
    I2S0.conf.tx_start = 0;
    I2S0.int_clr.val = I2S0.int_st.val;

    i2s_reset_fifo_unlocked();

    I2S0.conf.tx_reset = 1;
    I2S0.conf.tx_reset = 0;
    I2S0.conf.rx_reset = 1;
    I2S0.conf.rx_reset = 0;

    I2S0.lc_conf.in_rst = 1;
    I2S0.lc_conf.in_rst = 0;
    I2S0.lc_conf.out_rst = 1;
    I2S0.lc_conf.out_rst = 0;

    I2S0.lc_conf.check_owner = 0;
    I2S0.lc_conf.out_loop_test = 0;
    I2S0.lc_conf.out_auto_wrback = 0;
    I2S0.lc_conf.out_data_burst_en = 0;
    I2S0.lc_conf.outdscr_burst_en = 0;
    I2S0.lc_conf.out_no_restart_clr = 0;
    I2S0.lc_conf.indscr_burst_en = 0;
    I2S0.lc_conf.out_eof_mode = 1;

    I2S0.conf2.lcd_en = 0;
    I2S0.conf2.camera_en = 0;
    I2S0.pdm_conf.pcm2pdm_conv_en = 0;
    I2S0.pdm_conf.pdm2pcm_conv_en = 0;

    I2S0.fifo_conf.dscr_en = 0;
    I2S0.conf_chan.tx_chan_mod = 3;
    I2S0.conf_single_data = init_value;
    I2S0.fifo_conf.tx_fifo_mod = 3;
    I2S0.fifo_conf.rx_fifo_mod = 3;
    I2S0.sample_rate_conf.tx_bits_mod = 32;
    I2S0.sample_rate_conf.rx_bits_mod = 32;
    I2S0.conf.tx_mono = 0;
    I2S0.conf_chan.rx_chan_mod = 1;
    I2S0.conf.rx_mono = 0;
    I2S0.conf.tx_start = 0;
    I2S0.conf.rx_start = 0;
    I2S0.conf.tx_msb_right = 1;
    I2S0.conf.tx_right_first = 0;
    I2S0.conf.tx_slave_mod = 0;
    I2S0.fifo_conf.tx_fifo_mod_force_en = 1;
    I2S0.pdm_conf.rx_pdm_en = 0;
    I2S0.pdm_conf.tx_pdm_en = 0;
    I2S0.conf.tx_short_sync = 0;
    I2S0.conf.rx_short_sync = 0;
    I2S0.conf.tx_msb_shift = 0;
    I2S0.conf.rx_msb_shift = 0;

    I2S0.clkm_conf.clka_en = 0;
    I2S0.clkm_conf.clkm_div_num = 5;
    I2S0.clkm_conf.clkm_div_b = 0;
    I2S0.clkm_conf.clkm_div_a = 0;
    I2S0.sample_rate_conf.tx_bck_div_num = 2;
    I2S0.sample_rate_conf.rx_bck_div_num = 2;

    I2S0.int_ena.out_eof = 1;
    I2S0.int_ena.out_dscr_err = 0;
    I2S0.int_ena.out_total_eof = 0;
    I2S0.int_ena.out_done = 0;

    i2s_attach_gpio();
    I2S0.fifo_conf.dscr_en = 1;
    I2S0.int_clr.val = 0xFFFFFFFF;
    I2S0.out_link.start = 1;
    I2S0.conf.tx_start = 1;

    portEXIT_CRITICAL(&s_i2s_lock);

    esp_rom_delay_us(20);
    return ESP_OK;
}

esp_err_t grblhal_i2s_out_init(const grblhal_i2s_out_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is null");

    if (s_initialized) {
        return ESP_OK;
    }

    s_ws_gpio = config->ws_gpio;
    s_bck_gpio = config->bck_gpio;
    s_data_gpio = config->data_gpio;
    atomic_store(&s_port_state, config->init_value);

    periph_module_reset(PERIPH_I2S0_MODULE);
    periph_module_enable(PERIPH_I2S0_MODULE);

    ESP_RETURN_ON_ERROR(allocate_dma(), TAG, "dma allocation failed");
    ESP_RETURN_ON_ERROR(esp_intr_alloc(ETS_I2S0_INTR_SOURCE, 0, i2s_intr_handler, NULL, &s_i2s_intr),
                        TAG,
                        "i2s interrupt allocation failed");

    BaseType_t task_ok = xTaskCreatePinnedToCore(i2s_dma_task, "grblhal_i2s", 4096, NULL, 6, NULL, 1);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "i2s task allocation failed");

    s_initialized = true;
    return start_i2s(config->init_value);
}

esp_err_t grblhal_i2s_out_write(uint8_t bit, bool value)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "i2s out is not initialized");
    ESP_RETURN_ON_FALSE(bit < GRBLHAL_I2S_OUT_NUM_BITS, ESP_ERR_INVALID_ARG, TAG, "i2s bit is out of range");

    if (value) {
        atomic_fetch_or(&s_port_state, BIT_U32(bit));
    } else {
        atomic_fetch_and(&s_port_state, ~BIT_U32(bit));
    }

    I2S0.conf_single_data = atomic_load(&s_port_state);
    return ESP_OK;
}

bool grblhal_i2s_out_state(uint8_t bit)
{
    if (bit >= GRBLHAL_I2S_OUT_NUM_BITS) {
        return false;
    }

    return (atomic_load(&s_port_state) & BIT_U32(bit)) != 0;
}

void grblhal_i2s_out_delay(void)
{
    esp_rom_delay_us(I2S_USEC_PER_SAMPLE * 2U);
}
