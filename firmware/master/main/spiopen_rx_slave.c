/**
 * SpIOpen master – chain bus RX via I2S slave + DMA (high-speed).
 * No CS on chain; BCLK = chain CLK, DIN = chain MOSI. WS is driven locally
 * (e.g. GPIO tied low) for single-slot stream. DMA fills buffers; a task
 * runs a sliding-window parser to find two-byte preamble 0xAA 0xAA and extract frames.
 */
#include "spiopen_rx_slave.h"
#include "spiopen_queues.h"
#include "frame_pool.h"
#include "spiopen_protocol.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

#define RX_TASK_STACK_SIZE   (configMINIMAL_STACK_SIZE * 5)
#define RX_TASK_PRIORITY     (tskIDLE_PRIORITY + 2)
/** DMA buffer size in bytes (8-bit mono: one byte per sample). */
#define I2S_RX_DMA_BUF_BYTES 512
/** Max frame = header 4 + payload 64 + CRC 4. Carryover keeps unprocessed tail. */
#define CARRYOVER_MAX        (SPIOPEN_HEADER_LEN + SPIOPEN_MAX_PAYLOAD + SPIOPEN_CRC_BYTES)

static const char *TAG = "spiopen_rx";
static i2s_chan_handle_t s_rx_handle;

/**
 * Sliding-window parser over a contiguous stream.
 * \param stream  Full stream (carryover + new chunk)
 * \param total_len  Total bytes in stream
 * \return Number of bytes at the end to keep as carryover for next chunk (0..CARRYOVER_MAX).
 */
static size_t parse_stream(uint8_t *stream, size_t total_len)
{
    size_t consumed = 0;

    while (consumed + SPIOPEN_HEADER_LEN + SPIOPEN_CRC_BYTES <= total_len) {
        uint8_t *p = stream + consumed;
        size_t remaining = total_len - consumed;

        const uint8_t *q = (const uint8_t *)memchr(p, SPIOPEN_PREAMBLE, remaining);
        if (q == NULL)
            break;
        /* Require two consecutive 0xAA bytes (bit-slip resilience). */
        if (remaining < 2u || q[1] != SPIOPEN_PREAMBLE) {
            consumed += (size_t)(q - p) + 1u;
            continue;
        }
        consumed += (size_t)(q - p) + SPIOPEN_PREAMBLE_BYTES;
        p = stream + consumed;
        remaining = total_len - consumed;

        if (remaining < (size_t)SPIOPEN_HEADER_LEN)
            break;
        uint8_t dlc_encoded = p[SPIOPEN_HEADER_OFFSET_DLC];
        uint8_t dlc_raw;
        if (spiopen_dlc_decode(dlc_encoded, &dlc_raw) != 0) {
            continue;  /* next iteration: memchr finds next 0xAA and advances consumed */
        }
        uint8_t payload_len = spiopen_dlc_to_byte_count(dlc_raw);
        if (payload_len > SPIOPEN_MAX_PAYLOAD) {
            continue;
        }
        size_t frame_len = (size_t)SPIOPEN_HEADER_LEN + payload_len + SPIOPEN_CRC_BYTES;
        if (remaining < frame_len)
            break;
        if (SPIOPEN_FRAME_CONTENT_OFFSET + frame_len > SPIOPEN_FRAME_BUF_SIZE)
            continue;

        if (!spiopen_crc32_verify_frame(p, frame_len)) {
            continue;
        }

        uint8_t *fbuf = frame_pool_get();
        if (fbuf == NULL) {
            continue;
        }
        memcpy(fbuf + SPIOPEN_FRAME_CONTENT_OFFSET, p, frame_len);
        if (send_to_spiopen_rx(fbuf, (uint8_t)(SPIOPEN_PREAMBLE_BYTES + frame_len), 0) != pdTRUE)
            frame_pool_put(fbuf);
        consumed += frame_len;
    }

    size_t tail = total_len - consumed;
    if (tail > CARRYOVER_MAX)
        tail = CARRYOVER_MAX;
    return tail;
}

static void spiopen_rx_task(void *pvParameters)
{
    uint8_t *buf = heap_caps_malloc(CARRYOVER_MAX + I2S_RX_DMA_BUF_BYTES, MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        ESP_LOGE(TAG, "RX task: no memory for stream buf");
        vTaskDelete(NULL);
        return;
    }
    uint8_t *carryover = buf;
    uint8_t *chunk = buf + CARRYOVER_MAX;
    size_t carryover_len = 0;
    (void)pvParameters;

    for (;;) {
        size_t read_len = 0;
        esp_err_t err = i2s_channel_read(s_rx_handle, chunk, I2S_RX_DMA_BUF_BYTES, &read_len, portMAX_DELAY);
        if (err != ESP_OK || read_len == 0) {
            if (err != ESP_OK)
                ESP_LOGW(TAG, "i2s_channel_read err %d", (int)err);
            continue;
        }
        /* Build contiguous stream: [carryover | chunk] then parse. */
        memmove(buf + carryover_len, chunk, read_len);
        size_t total = carryover_len + read_len;
        size_t new_tail = parse_stream(buf, total);
        if (new_tail > 0)
            memmove(carryover, buf + total - new_tail, new_tail);
        carryover_len = new_tail;
    }
}

void spiopen_rx_slave_init(void)
{
    /* WS not provided by chain: drive it low so I2S sees a fixed level (single slot). */
    gpio_config_t ws_io = {
        .pin_bit_mask = (1ULL << SPIOPEN_CHAIN_RX_WS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ws_io);
    gpio_set_level(SPIOPEN_CHAIN_RX_WS_GPIO, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    chan_cfg.dma_desc_num = 2;
    chan_cfg.dma_frame_num = I2S_RX_DMA_BUF_BYTES;  /* 8-bit mono: 1 byte per frame */
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel RX failed %d", (int)err);
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 2000000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_8BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = SPIOPEN_CHAIN_RX_CLK_GPIO,
            .ws = SPIOPEN_CHAIN_RX_WS_GPIO,
            .dout = GPIO_NUM_NC,
            .din = SPIOPEN_CHAIN_RX_MOSI_GPIO,
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
    err = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed %d", (int)err);
        i2s_del_channel(s_rx_handle);
        return;
    }
    err = i2s_channel_enable(s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed %d", (int)err);
        i2s_del_channel(s_rx_handle);
        return;
    }

    xTaskCreate(spiopen_rx_task, "spiopen_rx", RX_TASK_STACK_SIZE, NULL, RX_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "chain RX: I2S slave + DMA, sliding-window parser");
}
