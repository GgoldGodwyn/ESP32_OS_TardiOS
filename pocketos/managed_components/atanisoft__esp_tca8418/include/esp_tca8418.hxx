/*
 * SPDX-FileCopyrightText: 2024 Mike Dunston (atanisoft)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 */

#pragma once

#include <driver/i2c_types.h>
#include <hal/gpio_types.h>
#include <stdint.h>

/// Utility class that interacts with the keypad driver TCA8418 IC.
class TCA8418
{
public:
    /// Constructor.
    ///
    /// @param scl GPIO pin to use as SCL.
    /// @param sda GPIO pin to use as SDA.
    /// @param notify_pin GPIO pin to use for notification (not used today).
    /// @param address I2C address of the TCA8418 IC.
    ///
    /// @note Using this constructore will call to initialize an I2C bus which can
    /// fail, if it fails it will cause an abort.
    TCA8418(gpio_num_t scl, gpio_num_t sda, gpio_num_t notify_pin = GPIO_NUM_NC,
            uint8_t address = 0x34);


    /// Constructor.
    ///
    /// @param bus_handle I2C bus handle to use, requires external initialization.
    /// @param notify_pin GPIO pin to use for notification (not used today).
    /// @param address I2C address of the TCA8418 IC.
    ///
    /// @note Using this constructore will require an externally initialized I2C bus.
    TCA8418(i2c_master_bus_handle_t bus_handle, gpio_num_t notify_pin = GPIO_NUM_NC,
            uint8_t address = 0x34);

    /// Attempts to detect and initializes the TCA8418 IC.
    ///
    /// @param rows Number of rows to configure the TCA8418 to use, default to maximum (8).
    /// @param columns Number of rows to configure the TCA8418 to use, default to maximum (10).
    ///
    /// @return true if the TCA8418 was detected and successfully initialized, false otherwise.
    bool hw_init(size_t rows = MAX_ROW_COUNT, size_t columns = MAX_COLUMN_COUNT);

    /// @return the number of pending keypress events that have yet to be read.
    size_t get_event_count();

    /// Returns the last keypress event value.
    ///
    /// @return key code.
    ///
    /// @note this value is based on a 10x8 grid for all columns and rows even if
    /// configured to be smaller than this size.
    ///
    /// Example grid showing the values as expected to be returned from this function:
    ///
    ///            COLUMNS (0 - 9)
    /// ROW 0: 0   1  2  3  4  5  6  7  8  9
    /// ROW 1: 10 11 12 13 14 15 16 17 18 19
    /// ROW 2: 20 21 22 23 24 25 26 27 28 29
    /// ...
    /// ROW 7: 70 71 72 73 74 75 76 77 78 79
    uint8_t get_key();

    /// Discards all remaining keypress events.
    void flush();

private:
    /// Log tag to use for all output logging.
    static constexpr const char *TAG = "tca8418";

    /// Maximum number of columns supported by the TCA8418 IC.
    static constexpr size_t MAX_COLUMN_COUNT = 10;

    /// Maximum number of rows supported by the TCA8418 IC.
    static constexpr size_t MAX_ROW_COUNT = 8;

    /// Maximum number of milliseconds to wait for completing an I2C transaction.
    static constexpr int I2C_MAX_TRANSFER_TIME_MS = 500;

    /// I2C address of the TCA8418 IC.
    const uint8_t address_;

    /// GPIO pin used for notification of keypress.
    const gpio_num_t notifyPin_;

    /// GPIO pin used for I2C SCL.
    const gpio_num_t sclPin_;

    /// GPIO pin used for I2C SDA.
    const gpio_num_t sdaPin_;

    /// I2C bus handle to use for I2C communication.
    i2c_master_bus_handle_t busHandle_;

    /// I2C device handle to use for I2C communication.
    i2c_master_dev_handle_t devHandle_;

    /// Reads a single register from the TCA8418 IC.
    ///
    /// @param reg Register to read the value of.
    ///
    /// @return register value.
    uint8_t readRegister(uint8_t reg);

    /// Writes a register value to the TCA8418 IC.
    ///
    /// @param reg Register to write a value to.
    /// @param value Value to write to the register.
    void writeRegister(uint8_t reg, uint8_t value);
};
