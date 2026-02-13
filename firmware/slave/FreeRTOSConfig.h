/**
 * FreeRTOS configuration for SpIOpen slave (RP2040 XIAO).
 * RP2040 port uses rp2040_config.h for port-specific options.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (125000000UL)
#define configSYSTICK_CLOCK_HZ                  (1000000UL)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    (8)
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                   ((size_t)(32 * 1024))
#define configMAX_TASK_NAME_LEN                 (16)
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TRACE_FACILITY                1
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_CO_ROUTINES                   0
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 2)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256

#define configNUMBER_OF_CORES                   1

#define configASSERT(x)                         do { if (!(x)) { portDISABLE_INTERRUPTS(); for (;;); } } while (0)
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTimerPendFunctionCall          1  /* required by RP2040 port for xEventGroupSetBitsFromISR */

#endif /* FREERTOS_CONFIG_H */
