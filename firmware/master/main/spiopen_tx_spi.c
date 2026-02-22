/**
 * SpIOpen master – drop bus TX via SPI2 master (MOSI + CLK @ 10 MHz).
 * Task blocks on SpIOpen TX queue; sends preamble 0xAA then frame buffer.
 */
#include "spiopen_tx_spi.h"
#include "spiopen_queues.h"
#include "frame_pool.h"
#include "spiopen_protocol.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

#define SPIOPEN_TX_SPI_CLK_HZ   (1 * 1000 * 1000) // 1 MHz
#define TX_TASK_STACK_SIZE      (configMINIMAL_STACK_SIZE * 2)
#define TX_TASK_PRIORITY        (tskIDLE_PRIORITY + 2)

static spi_device_handle_t s_spi_dev;

static void spiopen_tx_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    (void)pvParameters;

    for (;;) {
        if (receive_from_spiopen_tx(&desc, portMAX_DELAY) != pdTRUE)
            continue;

        uint8_t preamble = SPIOPEN_PREAMBLE;
        spi_transaction_t t_preamble = {
            .length = 8,
            .tx_buffer = &preamble,
        };
        spi_device_polling_transmit(s_spi_dev, &t_preamble);

        spi_transaction_t t_frame = {
            .length = (size_t)desc.len * 8,
            .tx_buffer = desc.buf,
        };
        spi_device_polling_transmit(s_spi_dev, &t_frame);

        frame_pool_put(desc.buf);
    }
}

void spiopen_tx_spi_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPIOPEN_DROP_TX_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = SPIOPEN_DROP_TX_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPIOPEN_FRAME_BUF_SIZE + 4,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPIOPEN_TX_SPI_CLK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi_dev));

    xTaskCreate(spiopen_tx_task, "spiopen_tx", TX_TASK_STACK_SIZE, NULL, TX_TASK_PRIORITY, NULL);
}
