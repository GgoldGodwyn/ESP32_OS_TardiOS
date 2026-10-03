/*
 * SPDX-FileCopyrightText: 2024 Mike Dunston (atanisoft)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 */

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <algorithm>

#include "esp_tca8418.hxx"

// TCA8418 register constants
static constexpr uint8_t REG_INTERRUPT_STATUS = 0x02;
static constexpr uint8_t REG_KEY_LOCK_EVT_COUNT = 0x03;
static constexpr uint8_t REG_KEY_EVENT_A = 0x04;
static constexpr uint8_t REG_GPIO_INT_STAT1 = 0x11;
static constexpr uint8_t REG_GPIO_INT_STAT2 = 0x12;
static constexpr uint8_t REG_GPIO_INT_STAT3 = 0x13;
static constexpr uint8_t REG_GPIO_INT_EN1 = 0x1A;
static constexpr uint8_t REG_GPIO_INT_EN2 = 0x1B;
static constexpr uint8_t REG_GPIO_INT_EN3 = 0x1C;
static constexpr uint8_t REG_KEY_PRESS_GPIO1 = 0x1D;
static constexpr uint8_t REG_KEY_PRESS_GPIO2 = 0x1E;
static constexpr uint8_t REG_KEY_PRESS_GPIO3 = 0x1F;
static constexpr uint8_t REG_GPI_EM1 = 0x20;
static constexpr uint8_t REG_GPI_EM2 = 0x21;
static constexpr uint8_t REG_GPI_EM3 = 0x22;
static constexpr uint8_t REG_GPIO_DIRECTION_1 = 0x23;
static constexpr uint8_t REG_GPIO_DIRECTION_2 = 0x24;
static constexpr uint8_t REG_GPIO_DIRECTION_3 = 0x25;
static constexpr uint8_t REG_GPIO_INTERRUPT_LVL1 = 0x26;
static constexpr uint8_t REG_GPIO_INTERRUPT_LVL2 = 0x27;
static constexpr uint8_t REG_GPIO_INTERRUPT_LVL3 = 0x28;
static constexpr uint8_t REG_DEBOUNCE_DIS1 = 0x29;
static constexpr uint8_t REG_DEBOUNCE_DIS2 = 0x2A;
static constexpr uint8_t REG_DEBOUNCE_DIS3 = 0x2B;

TCA8418::TCA8418(gpio_num_t scl, gpio_num_t sda, gpio_num_t notify_pin, uint8_t address)
    : address_(address), notifyPin_(notify_pin), sclPin_(scl), sdaPin_(sda)
{
}

TCA8418::TCA8418(i2c_master_bus_handle_t bus_handle, gpio_num_t notify_pin, uint8_t address)
    : address_(address), notifyPin_(notify_pin), sclPin_(GPIO_NUM_NC), sdaPin_(GPIO_NUM_NC), busHandle_(bus_handle)
{   
}

bool TCA8418::hw_init(size_t rows, size_t columns)
{
    // Bit masks used for rows / columns
    const uint8_t ROW_COL_BIT_MASK[8] =
    {
        0b00000001, // one row / column
        0b00000011, // two rows / columns
        0b00000111, // three rows / columns
        0b00001111, // four rows / columns
        0b00011111, // five rows / columns
        0b00111111, // six rows / columns
        0b01111111, // seven rows / columns
        0b11111111, // eight rows / columns
    };
    if (sclPin_ != GPIO_NUM_NC && sdaPin_ != GPIO_NUM_NC)
    {
        const i2c_master_bus_config_t config =
        {
            .i2c_port = -1,
            .sda_io_num = sdaPin_,
            .scl_io_num = sclPin_,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
            {
                .enable_internal_pullup = true,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,4,0)
                .allow_pd = false
#endif
            }
        };
        ESP_LOGI(TAG, "Initializing I2C bus (scl:%d, sda:%d)", sclPin_, sdaPin_);
        ESP_ERROR_CHECK(i2c_new_master_bus(&config, &busHandle_));
    }

    const i2c_device_config_t dev_config =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_,
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags =
        {
            .disable_ack_check = 0
        }
    };
    ESP_LOGI(TAG, "Initializing device:%02x", address_);
    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(busHandle_, &dev_config, &devHandle_));

    ESP_LOGI(TAG, "Probing device:%02x", address_);
    esp_err_t res =
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_probe(busHandle_, address_, 10));
    if (res == ESP_OK)
    {
        ESP_LOGI(TAG, "Device %02x found! Configuring for %zu (cols), %zu (rows)",
            address_, columns, rows);
        // set default all GPIO pins to INPUT.
        writeRegister(REG_GPIO_DIRECTION_1, 0x00);
        writeRegister(REG_GPIO_DIRECTION_2, 0x00);
        writeRegister(REG_GPIO_DIRECTION_3, 0x00);

        // add all pins to key events.
        writeRegister(REG_GPI_EM1, 0xFF);
        writeRegister(REG_GPI_EM2, 0xFF);
        writeRegister(REG_GPI_EM3, 0xFF);

        // set all pins to FALLING interrupts.
        writeRegister(REG_GPIO_INTERRUPT_LVL1, 0x00);
        writeRegister(REG_GPIO_INTERRUPT_LVL2, 0x00);
        writeRegister(REG_GPIO_INTERRUPT_LVL3, 0x00);

        // add all pins to interrupts.
        writeRegister(REG_GPIO_INT_EN1, 0xFF);
        writeRegister(REG_GPIO_INT_EN2, 0xFF);
        writeRegister(REG_GPIO_INT_EN3, 0xFF);

        // configure rows and columns registers
        writeRegister(REG_KEY_PRESS_GPIO1, ROW_COL_BIT_MASK[std::min(rows, sizeof(ROW_COL_BIT_MASK)) - 1]);
        writeRegister(REG_KEY_PRESS_GPIO2, ROW_COL_BIT_MASK[std::min(columns, sizeof(ROW_COL_BIT_MASK)) - 1]);
        if (columns > 8)
        {
            writeRegister(REG_KEY_PRESS_GPIO3, ROW_COL_BIT_MASK[columns - 9]);
        }

        writeRegister(REG_DEBOUNCE_DIS1, 0x00);
        writeRegister(REG_DEBOUNCE_DIS2, 0x00);
        writeRegister(REG_DEBOUNCE_DIS3, 0x00);

        if (notifyPin_ != GPIO_NUM_NC)
        {
            gpio_config_t cfg =
            {
                .pin_bit_mask = static_cast<uint64_t>(1 << notifyPin_),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_NEGEDGE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
                .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE
#endif
            };
            ESP_ERROR_CHECK(gpio_config(&cfg));
        }
        return true;
    }
    else
    {
        ESP_LOGI(TAG, "Device %02x not found/responding!", address_);
    }
    return false;
}

size_t TCA8418::get_event_count()
{
    return (readRegister(REG_KEY_LOCK_EVT_COUNT) & 0x0F);
}

uint8_t TCA8418::get_key()
{
    return readRegister(REG_KEY_EVENT_A);
}

void TCA8418::flush()
{
    while (get_key() != 0)
    {
        ;
    }
    readRegister(REG_GPIO_INT_STAT1);
    readRegister(REG_GPIO_INT_STAT2);
    readRegister(REG_GPIO_INT_STAT3);
    writeRegister(REG_INTERRUPT_STATUS, 0x03);
}

uint8_t TCA8418::readRegister(uint8_t reg)
{
    uint8_t reg_number[1] =
    {
        reg
    };
    uint8_t receive_buf[1] =
    {
        0
    };
    ESP_LOGV(TAG, "Reading register %02x from dev %02x", reg, address_);
    ESP_ERROR_CHECK(i2c_master_transmit_receive(devHandle_, reg_number, 1,
        receive_buf, 1, I2C_MAX_TRANSFER_TIME_MS));
    return receive_buf[0];
}

void TCA8418::writeRegister(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] =
    {
        reg,
        value
    };
    ESP_LOGV(TAG, "Writing %02x to register %02x on dev %02x", value, reg, address_);
    ESP_ERROR_CHECK(
        i2c_master_transmit(devHandle_, buf, 2, I2C_MAX_TRANSFER_TIME_MS));
}
