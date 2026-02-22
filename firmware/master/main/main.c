/**
 * SpIOpen CANopen master (ESP32-C3 Zero).
 * Init: frame_pool, queues, driver, TX/RX transport, CO_new, CO_CANinit,
 * CO_CANopenInit, CO_CANopenInitPDO, CO_GTWA_init, CO_CANsetNormalMode.
 * Tasks: CANopen (RX inject + CO_process + CO_GTWA_process), USB input → CO_GTWA_write.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "frame_pool.h"
#include "spiopen_queues.h"
#include "spiopen_can_driver.h"
#include "spiopen_tx_spi.h"
#include "spiopen_rx_slave.h"
#include "canopen_od/OD.h"

/* Include CANopenNode with exact name to avoid shadowing on case-insensitive FS */
#include "CANopen.h"

#define MASTER_NODE_ID           127u
#define CANOPEN_TASK_STACK_SIZE  (configMINIMAL_STACK_SIZE * 6)
#define CANOPEN_TASK_PRIORITY    (tskIDLE_PRIORITY + 3)
#define GATEWAY_LINE_BUF_SIZE    128u

static CO_t *s_co = NULL;
static const char *TAG = "main";

static size_t gtwa_read_callback(void *object, const char *buf, size_t count, uint8_t *connectionOK)
{
    (void)object;
    if (connectionOK != NULL)
        *connectionOK = 1;
    if (count == 0)
        return 0;
    for (size_t i = 0; i < count; i++)
        putchar((unsigned char)buf[i]);
    return count;
}

static void canopen_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    uint32_t last_us = 0;
    uint32_t timerNext_us = 1000000u;
    (void)pvParameters;

    for (;;) {
        uint32_t timeout_ms = timerNext_us / 1000u;
        if (timeout_ms > 1u)
            timeout_ms -= 1u;
        if (timeout_ms < 1u)
            timeout_ms = 1u;
        if (timeout_ms > 1000u)
            timeout_ms = 1000u;

        if (receive_from_spiopen_rx(&desc, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            /* 0 = success (driver frees buf); non-zero = failure, we must return buffer to pool. */
            if (spiopen_can_driver_inject_rx(desc.buf, (size_t)desc.len) != 0)
                frame_pool_put(desc.buf);
        }

        if (s_co != NULL) {
            uint32_t now = (uint32_t)esp_timer_get_time();
            uint32_t diff = (last_us != 0u) ? (now - last_us) : 1000u;
            last_us = now;

            (void)CO_process(s_co, 1, diff, &timerNext_us);

#if (((CO_CONFIG_GTW) & CO_CONFIG_GTW_ASCII) != 0)
            if (s_co->gtwa != NULL)
                CO_GTWA_process(s_co->gtwa, 1, diff, &timerNext_us);
#endif
        }
    }
}

static void gateway_input_task(void *pvParameters)
{
    char line[GATEWAY_LINE_BUF_SIZE];
    size_t idx = 0;
    (void)pvParameters;

    for (;;) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (idx < sizeof(line) - 1u) {
            line[idx++] = (char)(unsigned char)c;
            if (c == '\n' || c == '\r') {
                line[idx] = '\0';
                if (s_co != NULL && s_co->gtwa != NULL) {
                    size_t n = (size_t)idx;
                    if (n > 0 && CO_GTWA_write_getSpace(s_co->gtwa) >= n)
                        CO_GTWA_write(s_co->gtwa, line, n);
                }
                idx = 0;
            }
        } else {
            idx = 0;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SpIOpen CANopen master starting");

    frame_pool_init();
    spiopen_queues_init();
    spiopen_can_driver_init();
    spiopen_tx_spi_init();
    spiopen_rx_slave_init();

    uint32_t heapUsed = 0;
    s_co = CO_new(NULL, &heapUsed);
    if (s_co == NULL) {
        ESP_LOGE(TAG, "CO_new failed");
        return;
    }

    if (CO_CANinit(s_co, NULL, 0) != CO_ERROR_NO) {
        ESP_LOGE(TAG, "CO_CANinit failed");
        return;
    }

    uint32_t errInfo = 0;
    if (CO_CANopenInit(s_co, NULL, NULL, OD, NULL,
            0, 1000, 1000, 500, 0, (uint8_t)MASTER_NODE_ID, &errInfo) != CO_ERROR_NO) {
        ESP_LOGE(TAG, "CO_CANopenInit failed errInfo=%lu", (unsigned long)errInfo);
        return;
    }

    if (CO_CANopenInitPDO(s_co, s_co->em, OD, (uint8_t)MASTER_NODE_ID, &errInfo) != CO_ERROR_NO) {
        ESP_LOGE(TAG, "CO_CANopenInitPDO failed errInfo=%lu", (unsigned long)errInfo);
        return;
    }

#if (((CO_CONFIG_GTW) & CO_CONFIG_GTW_ASCII) != 0)
    CO_ReturnError_t gtwErr = CO_GTWA_init(s_co->gtwa,
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_SDO) != 0)
        s_co->SDOclient, 500, 0,
#endif
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_NMT) != 0)
        s_co->NMT,
#endif
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_LSS) != 0)
        s_co->LSSmaster,
#endif
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_PRINT_LEDS) != 0)
        s_co->LEDs,
#endif
        0);  /* dummy uint8_t */
    if (gtwErr != CO_ERROR_NO) {
        ESP_LOGE(TAG, "CO_GTWA_init failed");
        return;
    }
    CO_GTWA_initRead(s_co->gtwa, gtwa_read_callback, NULL);
#endif

    CO_CANsetNormalMode(s_co->CANmodule);

    if (xTaskCreate(canopen_task, "canopen", CANOPEN_TASK_STACK_SIZE, NULL, CANOPEN_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "canopen task create failed");
        return;
    }
    if (xTaskCreate(gateway_input_task, "gtw_in", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "gateway input task create failed");
        return;
    }

    ESP_LOGI(TAG, "Master running; use ASCII over USB (e.g. [1] 1 read 0x1017 0 u16)");
}
