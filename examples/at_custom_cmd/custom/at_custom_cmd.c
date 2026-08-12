/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_at.h"
#include "driver/gpio.h"

static uint8_t at_test_cmd_test(uint8_t *cmd_name)
{
    uint8_t buffer[64] = {0};
    snprintf((char *)buffer, 64, "test command: <AT%s=?> is executed\r\n", cmd_name);
    esp_at_port_write_data(buffer, strlen((char *)buffer));

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_query_cmd_test(uint8_t *cmd_name)
{
    uint8_t buffer[64] = {0};
    snprintf((char *)buffer, 64, "query command: <AT%s?> is executed\r\n", cmd_name);
    esp_at_port_write_data(buffer, strlen((char *)buffer));

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_setup_cmd_test(uint8_t para_num)
{
    uint8_t index = 0;

    // get first parameter, and parse it into a digit
    int32_t digit = 0;
    if (esp_at_get_para_as_digit(index++, &digit) != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // get second parameter, and parse it into a string
    uint8_t *str = NULL;
    if (esp_at_get_para_as_str(index++, &str) != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // allocate a buffer and construct the data, then send the data to mcu via interface (uart/spi/sdio/socket)
    uint8_t *buffer = (uint8_t *)malloc(512);
    if (!buffer) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    int len = snprintf((char *)buffer, 512, "setup command: <AT%s=%d,\"%s\"> is executed\r\n",
                       esp_at_get_current_cmd_name(), digit, str);
    esp_at_port_write_data(buffer, len);

    // remember to free the buffer
    free(buffer);

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_exe_cmd_test(uint8_t *cmd_name)
{
    uint8_t buffer[64] = {0};
    snprintf((char *)buffer, 64, "execute command: <AT%s> is executed\r\n", cmd_name);
    esp_at_port_write_data(buffer, strlen((char *)buffer));

    return ESP_AT_RESULT_CODE_OK;
}

/**
 * @brief GPIO commands for ESP32 (WROOM-32) and ESP32-C3
 *
 * - AT+GPIOM=<pin>,<mode>[,<pull>]  configure pin direction
 *       mode: 0=input, 1=output;  pull: 0=none, 1=pull-up, 2=pull-down
 * - AT+GPIOW=<pin>,<level>          write output level (0/1)
 * - AT+GPIOR=<pin>[,<pull>]         read input level, pull optional
 * - AT+GPIO=?                       print usage and valid pins
 */
static bool at_gpio_is_valid(int32_t pin, bool for_output)
{
    if (pin < 0) {
        return false;
    }
    if (for_output) {
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            return false;
        }
    } else {
        if (!GPIO_IS_VALID_GPIO(pin)) {
            return false;
        }
    }
#ifdef CONFIG_IDF_TARGET_ESP32
    /* AT command port on WROOM-32: TX=GPIO1, RX=GPIO3 */
    if (pin == 1 || pin == 3) {
        return false;
    }
    /* GPIO6 ~ GPIO11 are connected to the internal SPI flash */
    if (pin >= 6 && pin <= 11) {
        return false;
    }
    /* Strapping pins (0/2/5/12/15) may prevent normal boot if forced.
     * Remove this block if you really need them. */
    if (pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15) {
        return false;
    }
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C3
    /* AT command port on ESP32-C3: TX=GPIO21, RX=GPIO20 */
    if (pin == 20 || pin == 21) {
        return false;
    }
    /* XTAL_32K_P/N (GPIO0/GPIO1) are used by the external 32 kHz crystal */
    if (pin == 0 || pin == 1) {
        return false;
    }
    /* Strapping pins (2/8/9) may prevent normal boot if forced.
     * Remove this block if you really need them. */
    if (pin == 2 || pin == 8 || pin == 9) {
        return false;
    }
#endif
    return true;
}

static void at_gpio_set_pull(gpio_num_t pin, int32_t pull)
{
    switch (pull) {
    case 1:
        gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
        break;
    case 2:
        gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
        break;
    default:
        gpio_set_pull_mode(pin, GPIO_FLOATING);
        break;
    }
}

static uint8_t at_test_cmd_gpio(uint8_t *cmd_name)
{
    uint8_t buffer[512] = {0};
    int len = snprintf((char *)buffer, sizeof(buffer),
        "GPIO commands:\r\n"
        "  AT+GPIOM=<pin>,<mode>[,<pull>]   mode: 0=input 1=output, pull: 0=none 1=up 2=down\r\n"
        "  AT+GPIOW=<pin>,<level>           write output level (0/1)\r\n"
        "  AT+GPIOR=<pin>[,<pull>]          read input level\r\n"
#ifdef CONFIG_IDF_TARGET_ESP32C3
        "valid in/out pins: 3,4,5,6,7,10,18,19\r\n"
        "reserved:          0,1,2,8,9,20,21 (AT uart / 32k xtal / strapping)\r\n"
#else
        "valid in/out pins: 4,13,14,16,17,18,19,21,22,23,25,26,27,32,33\r\n"
        "input-only pins:   34,35,36,39\r\n"
        "reserved:          0,1,2,3,5,6-11,12,15\r\n"
#endif
        );
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    esp_at_port_write_data(buffer, len);

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_setup_cmd_gpiom(uint8_t para_num)
{
    int32_t pin = 0;
    int32_t mode = 0;
    int32_t pull = 0;
    uint8_t index = 0;
    esp_at_para_parse_ret_t ret;

    ret = esp_at_get_para_as_digit(index++, &pin);
    if (ret != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    ret = esp_at_get_para_as_digit(index++, &mode);
    if (ret != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (index != para_num) {
        ret = esp_at_get_para_as_digit(index++, &pull);
        if (ret != ESP_AT_PARA_PARSE_RET_OK && ret != ESP_AT_PARA_PARSE_RET_OMITTED) {
            return ESP_AT_RESULT_CODE_ERROR;
        }
    }
    if (mode != 0 && mode != 1) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (pull < 0 || pull > 2) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (!at_gpio_is_valid(pin, mode == 1)) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    gpio_set_direction((gpio_num_t)pin, mode ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    at_gpio_set_pull((gpio_num_t)pin, pull);

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_setup_cmd_gpiow(uint8_t para_num)
{
    int32_t pin = 0;
    int32_t level = 0;
    uint8_t index = 0;

    if (esp_at_get_para_as_digit(index++, &pin) != ESP_AT_PARA_PARSE_RET_OK ||
        esp_at_get_para_as_digit(index++, &level) != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (level != 0 && level != 1) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (!at_gpio_is_valid(pin, true)) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, level);

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_setup_cmd_gpior(uint8_t para_num)
{
    int32_t pin = 0;
    int32_t pull = 0;
    bool has_pull = false;
    uint8_t index = 0;
    esp_at_para_parse_ret_t ret;
    uint8_t buffer[64] = {0};

    ret = esp_at_get_para_as_digit(index++, &pin);
    if (ret != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (index != para_num) {
        ret = esp_at_get_para_as_digit(index++, &pull);
        if (ret != ESP_AT_PARA_PARSE_RET_OK && ret != ESP_AT_PARA_PARSE_RET_OMITTED) {
            return ESP_AT_RESULT_CODE_ERROR;
        }
        has_pull = true;
    }
    if (pull < 0 || pull > 2) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    if (!at_gpio_is_valid(pin, false)) {
        return ESP_AT_RESULT_CODE_ERROR;
    }

    if (has_pull) {
        /* explicit pull means "read as input" */
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        at_gpio_set_pull((gpio_num_t)pin, pull);
    }

    int len = snprintf((char *)buffer, sizeof(buffer), "+GPIOR:%d,%d\r\n", (int)pin, gpio_get_level((gpio_num_t)pin));
    esp_at_port_write_data(buffer, len);

    return ESP_AT_RESULT_CODE_OK;
}

static const esp_at_cmd_t at_custom_cmd[] = {
    {"+TEST", at_test_cmd_test, at_query_cmd_test, at_setup_cmd_test, at_exe_cmd_test},
    {"+GPIOM", at_test_cmd_gpio, NULL, at_setup_cmd_gpiom, NULL},
    {"+GPIOW", at_test_cmd_gpio, NULL, at_setup_cmd_gpiow, NULL},
    {"+GPIOR", at_test_cmd_gpio, NULL, at_setup_cmd_gpior, NULL},
    /**
     * @brief You can define your own AT commands here.
     */
};

bool esp_at_custom_cmd_register(void)
{
    return esp_at_custom_cmd_array_register(at_custom_cmd, sizeof(at_custom_cmd) / sizeof(esp_at_cmd_t));
}

ESP_AT_CMD_SET_INIT_FN(esp_at_custom_cmd_register, 1);
