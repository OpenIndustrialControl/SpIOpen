/**
 * CANopenNode driver target for SpIOpen transport (ESP32-C3 master, FreeRTOS).
 * Included from 301/CO_driver.h; provides types, lock macros, and master/gateway config.
 */
#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* CO_CANrxMsg_t and read macros from common SpIOpen–CANopenNode integration. */
#include "spiopen_canopen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) (x)
#define CO_SWAP_32(x) (x)
#define CO_SWAP_64(x) (x)
typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

/* Master and gateway: override defaults from CO_config.h (included before this). */
#undef CO_CONFIG_NMT
#define CO_CONFIG_NMT (CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT | CO_CONFIG_NMT_MASTER)
#undef CO_CONFIG_LSS
#define CO_CONFIG_LSS (CO_CONFIG_LSS_MASTER)
#undef CO_CONFIG_SDO_CLI
#define CO_CONFIG_SDO_CLI (CO_CONFIG_SDO_CLI_ENABLE)
#undef CO_CONFIG_GTW
#define CO_CONFIG_GTW (CO_CONFIG_GTW_ASCII | CO_CONFIG_GTW_ASCII_SDO | CO_CONFIG_GTW_ASCII_NMT | CO_CONFIG_GTW_ASCII_LSS | CO_CONFIG_GTW_ASCII_PRINT_HELP)
#ifndef CO_CONFIG_GTWA_COMM_BUF_SIZE
#define CO_CONFIG_GTWA_COMM_BUF_SIZE 200
#endif
#ifndef CO_CONFIG_GTW_BLOCK_DL_LOOP
#define CO_CONFIG_GTW_BLOCK_DL_LOOP 1
#endif
#undef CO_CONFIG_FIFO
#define CO_CONFIG_FIFO (CO_CONFIG_FIFO_ENABLE | CO_CONFIG_FIFO_ASCII_COMMANDS | CO_CONFIG_FIFO_ASCII_DATATYPES)

typedef struct {
    uint16_t ident;
    uint16_t mask;
    void *object;
    void (*CANrx_callback)(void *object, void *message);
} CO_CANrx_t;

typedef struct {
    uint32_t ident;
    uint8_t  DLC;
    uint8_t  data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} CO_CANtx_t;

typedef struct {
    void *CANptr;
    CO_CANrx_t *rxArray;
    uint16_t rxSize;
    CO_CANtx_t *txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile bool_t useCANrxFilters;
    volatile bool_t bufferInhibitFlag;
    volatile bool_t firstCANtxMessage;
    volatile uint16_t CANtxCount;
    uint32_t errOld;
} CO_CANmodule_t;

typedef struct {
    void *addr;
    size_t len;
    uint8_t subIndexOD;
    uint8_t attr;
    void *storageModule;
    uint16_t crc;
    size_t eepromAddrSignature;
    size_t eepromAddr;
    size_t offset;
    void *additionalParameters;
} CO_storage_entry_t;

#define CO_CONFIG_STORAGE (0)
#define CO_CONFIG_LEDS (0)

extern void CO_lock_can_send(void);
extern void CO_unlock_can_send(void);

#define CO_LOCK_CAN_SEND(CAN_MODULE)   CO_lock_can_send()
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) CO_unlock_can_send()
#define CO_LOCK_EMCY(CAN_MODULE)
#define CO_UNLOCK_EMCY(CAN_MODULE)
#define CO_LOCK_OD(CAN_MODULE)
#define CO_UNLOCK_OD(CAN_MODULE)

#define CO_MemoryBarrier() __sync_synchronize()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew) do { CO_MemoryBarrier(); (rxNew) = (void*)1L; } while(0)
#define CO_FLAG_CLEAR(rxNew) do { CO_MemoryBarrier(); (rxNew) = NULL; } while(0)

#ifdef __cplusplus
}
#endif

#endif /* CO_DRIVER_TARGET_H */
