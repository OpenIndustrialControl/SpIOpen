/**
 * CANopenNode driver target for SpIOpen transport (RP2040 FreeRTOS).
 * Included from 301/CO_driver.h; provides types, lock macros, and rx message access.
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

/**
 * One receive filter / mailbox: when a received ident matches (ident ^ mask), the stack
 * calls CANrx_callback(object, message). Structure dictated by CANopenNode.
 */
typedef struct {
    uint16_t ident;   /* Filter identifier; match when (received_ident ^ ident) & mask == 0. */
    uint16_t mask;    /* Bits that must match (1 = compare, 0 = don't care). */
    void *object;     /* Passed to CANrx_callback as first argument. */
    void (*CANrx_callback)(void *object, void *message); /* Called when a matching frame is injected. */
} CO_CANrx_t;

/**
 * One transmit buffer: SDO reply, heartbeat, TPDO, EMCY, etc. The stack fills ident/DLC/data
 * and calls CO_CANsend; our driver builds a SpIOpen frame and sends on chainbus. Structure
 * dictated by CANopenNode.
 */
typedef struct {
    uint32_t ident;   /* CAN id + flags; we use low 11 bits as SpIOpen CID. */
    uint8_t  DLC;     /* Payload length in bytes (0–8). */
    uint8_t  data[8]; /* Payload to send. */
    volatile bool_t bufferFull;  /* True when driver could not send yet (e.g. no pool buffer). */
    volatile bool_t syncFlag;    /* True if sync PDO; driver may clear pending sync PDOs on SYNC. */
} CO_CANtx_t;

/**
 * The single "CAN controller" instance; CANopenNode allocates one and we back it with
 * SpIOpen (dropbus RX → inject, chainbus TX ← CO_CANsend). Structure dictated by CANopenNode.
 */
typedef struct {
    void *CANptr;     /* Driver-specific context; we leave unused. */
    CO_CANrx_t *rxArray;  /* Array of receive filters (RPDO, SDO, NMT, etc.). */
    uint16_t rxSize;      /* Number of entries in rxArray. */
    CO_CANtx_t *txArray;  /* Array of transmit buffers. */
    uint16_t txSize;      /* Number of entries in txArray. */
    uint16_t CANerrorStatus;  /* Bitmask of CO_CAN_ERR* flags. */
    volatile bool_t CANnormal; /* True when in normal mode (we set after CO_CANopenInit). */
    volatile bool_t useCANrxFilters; /* We set false; we do software match in inject_rx. */
    volatile bool_t bufferInhibitFlag; /* Set to inhibit sync PDO sends until next SYNC. */
    volatile bool_t firstCANtxMessage; /* True until first successful CO_CANsend. */
    volatile uint16_t CANtxCount;  /* Number of TX buffers currently full (backlog). */
    uint32_t errOld;  /* Previous error state for change detection. */
} CO_CANmodule_t;

/**
 * Descriptor for one Object Dictionary entry that can be stored to/restored from non-volatile
 * memory. Structure dictated by CANopenNode. We disable storage (CO_CONFIG_STORAGE 0), so this
 * is never used by our build; it is here to satisfy the stack's type expectations.
 */
typedef struct {
    void *addr;       /* Pointer to OD variable. */
    size_t len;       /* Length in bytes. */
    uint8_t subIndexOD;   /* OD subindex. */
    uint8_t attr;     /* OD attribute flags. */
    void *storageModule;  /* Storage backend handle. */
    uint16_t crc;     /* CRC for this entry. */
    size_t eepromAddrSignature;  /* EEPROM address of signature. */
    size_t eepromAddr;    /* EEPROM address of data. */
    size_t offset;    /* Offset within storage. */
    void *additionalParameters;  /* Extra args for storage driver. */
} CO_storage_entry_t;

/* Disable storage so we don't need CO_storage (example OD still has 1010/1011 entries). */
#define CO_CONFIG_STORAGE (0)
/* Disable LEDs (303) and LSS (305) so we don't need 303/ or 305/ sources; we use our own RGB LED (0x6200). */
#define CO_CONFIG_LEDS (0)
#define CO_CONFIG_LSS (0)

/** Locks use FreeRTOS mutex; CO_LOCK_* are implemented in our CO_driver.c (SpIOpen). */
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
