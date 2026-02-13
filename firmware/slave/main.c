/**
 * SpIOpen slave firmware – Phase 1 loopback on RP2040 XIAO (FreeRTOS).
 *
 * Pins: see docs/DevelopmentPlan.md (GPIO26/27 dropbus PIO, GPIO28/29 chainbus in PIO,
 * GPIO2/3 chainbus out SPI/PIO).
 *
 * USB serial: numeral '0'-'9' -> build fake PDO (CID 181, 1 byte payload, TTL = digit)
 *             and send on chainbus_tx; other bytes discarded.
 *             Dropbus RX frames -> output full payload as hex bytestring.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "spiopen_protocol.h"
#include "frame_pool.h"
#include "bus_queues.h"
#include "bus_rx_pio.h"
#include "chainbus_tx_spi.h"
#include "chainbus_rx.h"
#include "dropbus_rx.h"

/* Task stack and priority */
#define APP_TASK_STACK_SIZE   (configMINIMAL_STACK_SIZE * 3)
#define TTL_TASK_STACK_SIZE   (configMINIMAL_STACK_SIZE * 2)
#define APP_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)
#define TTL_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)

static void app_task(void *pvParameters);
static void ttl_forward_task(void *pvParameters);

/** Print buf[0..len-1] as hex bytestring to stdout, then CRLF. */
static void print_hex_payload(const uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\r\n");
}

/* Dev test: PDO1 node 1 = COB-ID 0x181 (function 3, node 1) */
#define DEVTEST_FUNCTION_CODE  3u
#define DEVTEST_NODE_ID       1u

int main(void)
{
    stdio_init_all();
    /* Brief delay so host can open the USB serial port before first message */
    sleep_ms(1500);

    printf("\r\nSpIOpen slave (Phase 1)\r\n");

    frame_pool_init();
    bus_queues_init();
    chainbus_tx_spi_init();
    bus_rx_pio_init();
    chainbus_rx_init();
    dropbus_rx_init();

    printf("PIO+DMA+queues ready. Type 0-9 to send PDO (TTL=digit), dropbus RX -> hex.\r\n\r\n");

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
        if (receive_from_chainbus_rx(&desc, portMAX_DELAY) != pdTRUE)
            continue;
        /* TTL is at buf[1] per Architecture */
        if (desc.buf[1] == 0) {
            frame_pool_put(desc.buf);
            continue;
        }
        desc.buf[1]--;
        (void)send_to_chainbus_tx(desc.buf, desc.len, portMAX_DELAY);
    }
}

static void app_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    (void)pvParameters;
    for (;;) {
        /* Process USB serial one byte at a time (non-blocking). */
        int c = getchar_timeout_us(0);
        if (c >= '0' && c <= '9') {
            uint8_t *buf = frame_pool_get();
            if (buf == NULL) {
                printf("tx: no buffer\r\n");
            } else {
                uint8_t ttl = (uint8_t)(c - '0');
                uint8_t payload_byte = (uint8_t)c;
                size_t frame_len = spiopen_frame_build_std(buf, (size_t)SPIOPEN_FRAME_BUF_SIZE, ttl, DEVTEST_FUNCTION_CODE, DEVTEST_NODE_ID, &payload_byte, 1);
                if (frame_len != 0 && send_to_chainbus_tx(buf, (uint8_t)frame_len, 0) == pdTRUE) {
                    printf("tx TTL=%u len=%u\r\n", (unsigned)ttl, (unsigned)frame_len);
                } else {
                    if (frame_len == 0)
                        printf("tx: build failed\r\n");
                    frame_pool_put(buf);
                }
            }
        }
        if (receive_from_dropbus_rx(&desc, pdMS_TO_TICKS(10)) == pdTRUE) {
            printf("dropbus %u: ", (unsigned)desc.len);
            print_hex_payload(desc.buf, desc.len);
            frame_pool_put(desc.buf);
        }
    }
}
