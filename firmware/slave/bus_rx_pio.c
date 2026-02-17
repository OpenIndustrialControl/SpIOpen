/**
 * SpIOpen slave – shared PIO init for dropbus RX and chainbus RX.
 * Loads spiopen_bus_rx once; runs on two SMs with MOSI-first pin groups (pin 0 = MOSI, pin 1 = CLK).
 * Pins per docs/DevelopmentPlan.md: MOSI and CLK must be consecutive (CLK = MOSI + 1).
 */
#include "bus_rx_pio.h"
#include "spiopen_protocol.h"
#include "FreeRTOS.h"
#include "hardware/gpio.h"
#include "hardware/pio_instructions.h"
#include <stddef.h>

#include "bus_rx_pio.pio.h"

/* PIO group order: pin 0 = MOSI, pin 1 = CLK (consecutive GPIOs). */
#define DROPBUS_MOSI_GPIO  26
#define DROPBUS_CLK_GPIO   27
#define CHAINBUS_MOSI_GPIO 28
#define CHAINBUS_CLK_GPIO  29

#define NUM_PIO_SM         4u
#define PIO_GPIO_MAX       29u

static PIO s_pio;
static uint s_offset;
static uint s_dropbus_sm;
static uint s_chainbus_sm;

static void init_sm(uint sm, uint mosi_gpio, uint clk_gpio)
{
    configASSERT(sm < NUM_PIO_SM);
    configASSERT(mosi_gpio <= PIO_GPIO_MAX);
    configASSERT(clk_gpio <= PIO_GPIO_MAX);
    configASSERT(clk_gpio == mosi_gpio + 1u);  /* PIO group: pin 0 = MOSI, pin 1 = CLK; must be consecutive. */

    pio_sm_config c = spiopen_bus_rx_program_get_default_config(s_offset);
    /* IN_BASE = MOSI (pin 0); WAIT pin 1 = CLK. "in pins, 1" reads MOSI only; 8-bit push, no unpack in C. */
    sm_config_set_in_pins(&c, mosi_gpio);
    sm_config_set_jmp_pin(&c, mosi_gpio);
    /* shift_left=false (data in low 8 bits), autopush=true, push_threshold=8 => push to RX FIFO after each 8 bits. */
    sm_config_set_in_shift(&c, false, true, 8);
    sm_config_set_out_shift(&c, true, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c, 1.f);

    pio_sm_set_pindirs_with_mask(s_pio, sm, 0u, (1u << mosi_gpio) | (1u << clk_gpio));
    pio_gpio_init(s_pio, mosi_gpio);
    pio_gpio_init(s_pio, clk_gpio);
    pio_sm_init(s_pio, sm, s_offset, &c);
    pio_sm_put_blocking(s_pio, sm, (uint32_t)SPIOPEN_PREAMBLE);
    pio_sm_set_enabled(s_pio, sm, true);
}

void bus_rx_pio_init(void)
{
    configASSERT(pio0 != NULL);

    s_pio = pio0;
    s_offset = pio_add_program(s_pio, &spiopen_bus_rx_program);
    configASSERT(s_offset < 32u);  /* PIO instruction memory size */

    s_dropbus_sm = pio_claim_unused_sm(s_pio, true);
    configASSERT(s_dropbus_sm < NUM_PIO_SM);

    s_chainbus_sm = pio_claim_unused_sm(s_pio, true);
    configASSERT(s_chainbus_sm < NUM_PIO_SM);
    configASSERT(s_dropbus_sm != s_chainbus_sm);

    init_sm(s_dropbus_sm, DROPBUS_MOSI_GPIO, DROPBUS_CLK_GPIO);
    init_sm(s_chainbus_sm, CHAINBUS_MOSI_GPIO, CHAINBUS_CLK_GPIO);
}

void bus_rx_pio_get_dropbus(PIO *pio, uint *sm)
{
    if (pio != NULL)
        *pio = s_pio;
    if (sm != NULL)
        *sm = s_dropbus_sm;
}

void bus_rx_pio_get_chainbus(PIO *pio, uint *sm)
{
    if (pio != NULL)
        *pio = s_pio;
    if (sm != NULL)
        *sm = s_chainbus_sm;
}

/** Restart SM to program start (sync); clears shift state, then jumps to first instruction. Safe to call from IRQ. */
static void restart_sm_to_sync(uint sm)
{
    pio_sm_restart(s_pio, sm);
    pio_sm_exec(s_pio, sm, pio_encode_jmp(s_offset));
}

void bus_rx_pio_restart_dropbus(void)
{
    restart_sm_to_sync(s_dropbus_sm);
}

void bus_rx_pio_restart_chainbus(void)
{
    restart_sm_to_sync(s_chainbus_sm);
}
