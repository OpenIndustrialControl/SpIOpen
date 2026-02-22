/**
 * SpIOpen slave firmware – Phase 2 CANopenNode over SpIOpen, RGB LED on RxPDO1.
 *
 * Pins: see docs/DevelopmentPlan.md. Dropbus RX -> SpIOpen driver -> CANopenNode.
 * RxPDO1 (COB-ID 0x201) carries 3 bytes (R,G,B) mapped to OD 0x6200; LED on GPIO 16,17,25.
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
#include "spiopen_can_driver.h"
#include "led_rgb_pwm.h"
#include "canopen_slave.h"

#define TTL_TASK_STACK_SIZE  (configMINIMAL_STACK_SIZE * 2)
#define TTL_TASK_PRIORITY   (tskIDLE_PRIORITY + 3)

static void ttl_forward_task(void *pvParameters);

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

/** Drain stdin then block until one character is received (gives time to connect serial for debug). */
static void wait_for_serial_ready(void)
{
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
        ;
    printf("Press any key to start...\r\n");
    fflush(stdout);
    (void)getchar();
}

int main(void)
{
    stdio_init_all();
    sleep_ms(1500);

    printf("\r\nSpIOpen slave (Phase 2 CANopenNode + RGB RxPDO1)\r\n");
    wait_for_serial_ready();

    frame_pool_init();
    bus_queues_init();
    spiopen_dma_irq_dispatcher_init();
    chainbus_tx_spi_init();
    bus_rx_pio_init();
    chainbus_rx_init();
    dropbus_rx_init();

    spiopen_can_driver_init();
    led_rgb_pwm_init();

    if (canopen_init() != 0) {
        for (;;) tight_loop_contents();
    }

    xTaskCreate(ttl_forward_task, "ttl", TTL_TASK_STACK_SIZE, NULL, TTL_TASK_PRIORITY, NULL);

    printf("scheduler start\r\n");
    fflush(stdout);
    vTaskStartScheduler();
    return 0;
}

static void ttl_forward_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    (void)pvParameters;
    for (;;) {
        if (receive_from_chainbus_rx(&desc, portMAX_DELAY) != pdTRUE)
            continue;
        /* TTL is at buf[SPIOPEN_FRAME_CONTENT_OFFSET]. */
        if (desc.buf[SPIOPEN_FRAME_CONTENT_OFFSET] == 0) {
            frame_pool_put(desc.buf);
            continue;
        }
        desc.buf[SPIOPEN_FRAME_CONTENT_OFFSET]--;
        (void)send_to_chainbus_tx(desc.buf, desc.len, portMAX_DELAY);
    }
}
