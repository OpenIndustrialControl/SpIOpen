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
#include "dma_irq.h"
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

/* ----- FreeRTOS error hooks (output to USB serial) ----- */
void vAssertCalled(const char *file, int line)
{
    printf("\r\n*** ASSERT %s:%d ***\r\n", file, line);
    fflush(stdout);
    portDISABLE_INTERRUPTS();
    for (;;)
        tight_loop_contents();
}

void vApplicationMallocFailedHook(void)
{
    printf("\r\n*** MALLOC FAILED ***\r\n");
    fflush(stdout);
    portDISABLE_INTERRUPTS();
    for (;;)
        tight_loop_contents();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("\r\n*** STACK OVERFLOW task %s ***\r\n", pcTaskName != NULL ? pcTaskName : "?");
    fflush(stdout);
    portDISABLE_INTERRUPTS();
    for (;;)
        tight_loop_contents();
}

/* Dev test: PDO1 node 1 = COB-ID 0x181 (function 3, node 1) */
#define DEVTEST_FUNCTION_CODE  3u
#define DEVTEST_NODE_ID       1u

/** Drain stdin then block until one character is received (avoids bypass from buffered data). */
static void wait_key(void)
{
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
        ;
    (void) getchar();
}

int main(void)
{
    stdio_init_all();
    /* Brief delay so host can open the USB serial port before first message */
    sleep_ms(1500);

    printf("\r\nSpIOpen slave (Phase 1)\r\n");
    printf("Press any key to init...\r\n");
    wait_key();

    printf("init: frame_pool...\r\n");
    frame_pool_init();
    printf("init: frame_pool ok\r\n");

    printf("init: bus_queues...\r\n");
    bus_queues_init();
    printf("init: bus_queues ok\r\n");

    printf("init: dma_irq dispatcher...\r\n");
    spiopen_dma_irq_dispatcher_init();
    printf("init: dma_irq ok\r\n");

    printf("init: chainbus_tx_spi...\r\n");
    chainbus_tx_spi_init();
    printf("init: chainbus_tx_spi ok\r\n");

    printf("init: bus_rx_pio...\r\n");
    bus_rx_pio_init();
    printf("init: bus_rx_pio ok\r\n");

    printf("init: chainbus_rx...\r\n");
    chainbus_rx_init();
    printf("init: chainbus_rx ok\r\n");

    printf("init: dropbus_rx...\r\n");
    dropbus_rx_init();
    printf("init: dropbus_rx ok\r\n");

    printf("PIO+DMA+queues ready. Press any key to start tasks.\r\n");
    wait_key();

    printf("create: ttl task...\r\n");
    // xTaskCreate(ttl_forward_task, "ttl", TTL_TASK_STACK_SIZE, NULL, TTL_TASK_PRIORITY, NULL);  /* disabled for serial isolation */
    printf("create: ttl task ok (skipped)\r\n");

    printf("create: app task...\r\n");
    xTaskCreate(app_task, "app", APP_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY, NULL);
    printf("create: app task ok\r\n");

    printf("starting scheduler...\r\n");
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
    printf("app task started\r\n");
    spiopen_frame_desc_t desc;
    (void)pvParameters;
    for (;;) {
        /* Process USB serial one byte at a time (non-blocking). */
        int c = getchar_timeout_us(0);
        if (c >= '0' && c <= '9') {
            uint8_t *buf = frame_pool_get();
            if (buf == NULL) {
                printf("send failed: no buffer\r\n");
            } else {
                uint8_t ttl = (uint8_t)(c - '0');
                uint8_t payload_byte = (uint8_t)c;
                size_t frame_len = spiopen_frame_build_std(buf, (size_t)SPIOPEN_FRAME_BUF_SIZE, ttl, DEVTEST_FUNCTION_CODE, DEVTEST_NODE_ID, &payload_byte, 1);
                if (frame_len == 0) {
                    printf("send failed: frame build failed\r\n");
                    frame_pool_put(buf);
                } else if (send_to_chainbus_tx(buf, (uint8_t)frame_len, 0) != pdTRUE) {
                    printf("send failed: tx busy or timeout\r\n");
                    frame_pool_put(buf);
                } else {
                    printf("send ok: ");
                    print_hex_payload(buf, (uint8_t)frame_len);
                }
            }
        }
        if (receive_from_dropbus_rx(&desc, pdMS_TO_TICKS(10)) == pdTRUE) {
            print_hex_payload(desc.buf, desc.len);
            frame_pool_put(desc.buf);
        }
    }
}
