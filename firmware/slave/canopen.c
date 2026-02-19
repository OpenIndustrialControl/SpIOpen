/**
 * CANopen init and mainline task for SpIOpen slave.
 * Init performs CO_new, CO_CANinit, CO_CANopenInit, CO_CANopenInitPDO, CO_CANsetNormalMode
 * and then creates the canopen_task.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "CANopen.h"
#include "spiopen_can_driver.h"
#include "frame_pool.h"
#include "bus_queues.h"
#include "led_rgb_pwm.h"
#include "OD.h"

#define CANOPEN_NODE_ID         1u
#define CANOPEN_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 6)
#define CANOPEN_TASK_PRIORITY   (tskIDLE_PRIORITY + 3)

static CO_t *co = NULL;

static void canopen_task(void *pvParameters);

/**
 * Initialize CANopen stack and create the CANopen task.
 */
int canopen_init(void)
{
    uint32_t heapUsed = 0;
    co = CO_new(NULL, &heapUsed);
    if (co == NULL) {
        printf("CO_new failed\r\n");
        fflush(stdout);
        return -1;
    }
    if (CO_CANinit(co, NULL, 0) != CO_ERROR_NO) {
        printf("CO_CANinit failed\r\n");
        fflush(stdout);
        return -1;
    }
    uint32_t errInfo = 0;
    if (CO_CANopenInit(co, NULL, NULL, OD, NULL,
            0, 1000, 1000, 500, 0, CANOPEN_NODE_ID, &errInfo) != CO_ERROR_NO) {
        printf("CO_CANopenInit failed errInfo=%lu\r\n", (unsigned long)errInfo);
        fflush(stdout);
        return -1;
    }
    if (CO_CANopenInitPDO(co, co->em, OD, CANOPEN_NODE_ID, &errInfo) != CO_ERROR_NO) {
        printf("CO_CANopenInitPDO failed errInfo=%lu\r\n", (unsigned long)errInfo);
        fflush(stdout);
        return -1;
    }
    CO_CANsetNormalMode(co->CANmodule);

    if (xTaskCreate(canopen_task, "canopen", CANOPEN_TASK_STACK_SIZE, NULL, CANOPEN_TASK_PRIORITY, NULL) != pdPASS) {
        printf("xTaskCreate(canopen) failed\r\n");
        fflush(stdout);
        return -1;
    }
    return 0;
}

/**
 * CANopen mainline task: receives SpIOpen frames from dropbus, feeds CANopenNode, processes
 * stack and RPDO, and drives the RGB LED from the OD.
 *
 * We block in receive_from_dropbus_rx() with a timeout derived from timerNext_us.
 * Wake on either a received frame or the timeout, then run CO_process / RPDO / LED.
 */
static void canopen_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    uint32_t last_us = 0;
    uint32_t timerNext_us = 1000000;
    (void)pvParameters;

    for (;;) {
        uint32_t timeout_ms = timerNext_us / 1000u;
        if (timeout_ms > 1u)
            timeout_ms -= 1u;
        if (timeout_ms < 1u)
            timeout_ms = 1u;
        if (timeout_ms > 1000u)
            timeout_ms = 1000u;

        if (receive_from_dropbus_rx(&desc, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            if (spiopen_can_driver_inject_rx(desc.buf, (size_t)desc.len) == 0)
                frame_pool_put(desc.buf);
        }
        if (co != NULL) {
            uint32_t now = (uint32_t)time_us_32();
            uint32_t diff = (last_us != 0) ? (now - last_us) : 1000;
            last_us = now;
            (void)CO_process(co, 0, diff, &timerNext_us);
#if ((CO_CONFIG_PDO) & CO_CONFIG_RPDO_ENABLE) != 0
            for (uint16_t i = 0; i < OD_CNT_RPDO && co->RPDO != NULL; i++)
                CO_RPDO_process(&co->RPDO[i], 0, &timerNext_us, 1, 0);
#endif
            led_rgb_set(OD_RAM.x6200_RGB.red, OD_RAM.x6200_RGB.green, OD_RAM.x6200_RGB.blue);
        }
    }
}
