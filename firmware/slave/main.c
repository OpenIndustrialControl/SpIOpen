/**
 * SpIOpen slave firmware – Phase 1 loopback on RP2040 XIAO (FreeRTOS).
 *
 * Pins: see docs/DevelopmentPlan.md (GPIO26/27 drop PIO, GPIO28/29 chain in PIO,
 * GPIO2/3 chain out SPI/PIO).
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "spiopen_protocol.h"
#include "frame_pool.h"
#include "bus_queues.h"
#include "chain_tx_spi.h"
#include "drop_rx.h"

/* Task stack and priority */
#define APP_TASK_STACK_SIZE   (configMINIMAL_STACK_SIZE * 2)
#define TTL_TASK_STACK_SIZE   (configMINIMAL_STACK_SIZE * 2)
#define APP_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)
#define TTL_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)

static void app_task(void *pvParameters);
static void ttl_forward_task(void *pvParameters);

int main(void)
{
    stdio_init_all();

    (void)spiopen_crc32(NULL, 0); /* Phase 1: protocol stub reference */

    frame_pool_init();
    bus_queues_init();
    chain_tx_spi_init();
    drop_rx_init();

    xTaskCreate(ttl_forward_task, "ttl", TTL_TASK_STACK_SIZE, NULL, TTL_TASK_PRIORITY, NULL);
    xTaskCreate(app_task, "app", APP_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY, NULL);
    vTaskStartScheduler();

    /* Should not reach here */
    return 0;
}

static void ttl_forward_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    (void)pvParameters;
    for (;;) {
        if (receive_from_chain_rx(&desc, portMAX_DELAY) != pdTRUE)
            continue;
        /* TTL is at buf[1] per Architecture */
        if (desc.buf[1] == 0) {
            frame_pool_put(desc.buf);
            continue;
        }
        desc.buf[1]--;
        (void)send_to_chain_tx(desc.buf, desc.len, portMAX_DELAY);
    }
}

static void app_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    (void)pvParameters;
    for (;;) {
        if (receive_from_drop_rx(&desc, pdMS_TO_TICKS(1000)) != pdTRUE) {
            /* No frame; optional: build and send a test frame on chain_tx */
            uint8_t *buf = frame_pool_get();
            if (buf != NULL) {
                buf[0] = 0;
                buf[1] = 2; /* TTL */
                buf[2] = 0;
                if (send_to_chain_tx(buf, 3, 0) != pdTRUE)
                    frame_pool_put(buf);
            }
            continue;
        }
        /* Mock process: log and discard */
        (void)desc.len;
        frame_pool_put(desc.buf);
    }
}
