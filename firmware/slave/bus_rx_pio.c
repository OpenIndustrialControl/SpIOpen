/**
 * SpIOpen slave – shared PIO init for dropbus RX and chainbus RX.
 * Loads spiopen_bus_rx once; runs on SM 0 (dropbus pins 26,27) and SM 1 (chainbus pins 28,29).
 */
#include "bus_rx_pio.h"
#include "spiopen_protocol.h"
#include "hardware/gpio.h"
#include <stddef.h>

#include "bus_rx_pio.pio.h"

/* Pins per docs/DevelopmentPlan.md */
#define DROPBUS_CLK_GPIO   26
#define DROPBUS_MOSI_GPIO  27
#define CHAINBUS_CLK_GPIO  28
#define CHAINBUS_MOSI_GPIO 29

static PIO s_pio;
static uint s_offset;
static uint s_dropbus_sm;
static uint s_chainbus_sm;

static void init_sm(uint sm, uint clk_gpio, uint mosi_gpio)
{
    pio_sm_config c = spiopen_bus_rx_program_get_default_config(s_offset);
    sm_config_set_in_pins(&c, mosi_gpio);
    sm_config_set_set_pins(&c, clk_gpio, 1);
    sm_config_set_in_shift(&c, true, true, 8);
    sm_config_set_out_shift(&c, true, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c, 1.f);

    pio_sm_set_pindirs_with_mask(s_pio, sm, 0u, (1u << clk_gpio) | (1u << mosi_gpio));
    pio_gpio_init(s_pio, clk_gpio);
    pio_gpio_init(s_pio, mosi_gpio);
    pio_sm_init(s_pio, sm, s_offset, &c);
    pio_sm_put_blocking(s_pio, sm, (uint32_t)SPIOPEN_PREAMBLE);
    pio_sm_set_enabled(s_pio, sm, true);
}

void bus_rx_pio_init(void)
{
    s_pio = pio0;
    s_offset = pio_add_program(s_pio, &spiopen_bus_rx_program);

    s_dropbus_sm = pio_claim_unused_sm(s_pio, true);
    s_chainbus_sm = pio_claim_unused_sm(s_pio, true);
    init_sm(s_dropbus_sm, DROPBUS_CLK_GPIO, DROPBUS_MOSI_GPIO);
    init_sm(s_chainbus_sm, CHAINBUS_CLK_GPIO, CHAINBUS_MOSI_GPIO);
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
