#include "ph_sensor.h"

#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG "ph_sensor"

#define EZO_RESPONSE_MAX 31
#define EZO_I2C_TIMEOUT_MS 1000
#define EZO_UART_NUM UART_NUM_1
#define EZO_UART_BAUD 9600

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_ezo_dev;
static bool s_initialized;

static esp_err_t ezo_transaction(const char *cmd,
                                 uint32_t delay_ms,
                                 char *response,
                                 size_t response_len)
{
    if (!s_initialized || response == NULL || response_len == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_master_transmit(s_ezo_dev,
                                          (const uint8_t *) cmd,
                                          strlen(cmd),
                                          EZO_I2C_TIMEOUT_MS);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C write failed for '%s': %s", cmd, esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    uint8_t buf[EZO_RESPONSE_MAX + 1] = {0};
    err = i2c_master_receive(s_ezo_dev, buf, sizeof(buf), EZO_I2C_TIMEOUT_MS);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C read failed for '%s': %s", cmd, esp_err_to_name(err));
        return err;
    }

    if (buf[0] == 254)
    {
        vTaskDelay(pdMS_TO_TICKS(300));
        err = i2c_master_receive(s_ezo_dev, buf, sizeof(buf), EZO_I2C_TIMEOUT_MS);
        if (err != ESP_OK)
        {
            return err;
        }
    }

    if (buf[0] == 2)
    {
        ESP_LOGE(TAG, "EZO syntax error for command '%s'", cmd);
        return ESP_ERR_INVALID_ARG;
    }
    if (buf[0] == 255)
    {
        ESP_LOGE(TAG, "EZO no data for command '%s'", cmd);
        return ESP_ERR_NOT_FOUND;
    }
    if (buf[0] != 1)
    {
        ESP_LOGE(TAG, "EZO unexpected status %u for command '%s'", buf[0], cmd);
        return ESP_FAIL;
    }

    size_t payload_len = strnlen((const char *) &buf[1], EZO_RESPONSE_MAX);
    if (payload_len >= response_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(response, &buf[1], payload_len);
    response[payload_len] = '\0';
    return ESP_OK;
}

void ph_sensor_init(void)
{
    if (s_initialized)
    {
        return;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = CONFIG_PH_SENSOR_I2C_PORT,
        .sda_io_num = CONFIG_PH_SENSOR_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_PH_SENSOR_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_PH_SENSOR_I2C_ADDR,
        .scl_speed_hz = CONFIG_PH_SENSOR_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_ezo_dev));

    s_initialized = true;
    ESP_LOGI(TAG, "EZO pH on I2C addr 0x%02X", CONFIG_PH_SENSOR_I2C_ADDR);
}

void ph_sensor_deinit(void)
{
    if (!s_initialized)
    {
        return;
    }

    i2c_master_bus_rm_device(s_ezo_dev);
    i2c_del_master_bus(s_i2c_bus);
    s_ezo_dev = NULL;
    s_i2c_bus = NULL;
    s_initialized = false;
}

bool ph_sensor_is_initialized(void)
{
    return s_initialized;
}

esp_err_t ph_sensor_query(const char *cmd, uint32_t delay_ms, char *response, size_t response_len)
{
    return ezo_transaction(cmd, delay_ms, response, response_len);
}

esp_err_t ph_sensor_calibrate(const char *point, float buffer_ph)
{
    char cmd[24];
    char response[16];
    snprintf(cmd, sizeof(cmd), "Cal,%s,%.2f", point, buffer_ph);
    return ezo_transaction(cmd, 900, response, sizeof(response));
}

esp_err_t ph_sensor_cal_clear(void)
{
    char response[16];
    return ezo_transaction("Cal,clear", 900, response, sizeof(response));
}

esp_err_t ph_sensor_disable_led(void)
{
    char response[16];
    return ezo_transaction("L,0", 300, response, sizeof(response));
}

esp_err_t ph_sensor_set_temp(float temp_c)
{
    char cmd[16];
    char response[16];
    snprintf(cmd, sizeof(cmd), "T,%.2f", temp_c);
    return ezo_transaction(cmd, 300, response, sizeof(response));
}

esp_err_t ph_sensor_read(float *ph_out)
{
    if (ph_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char response[16];
    esp_err_t err = ezo_transaction("R", 900, response, sizeof(response));
    if (err != ESP_OK)
    {
        return err;
    }

    char *end = NULL;
    float ph = strtof(response, &end);
    if (end == response)
    {
        ESP_LOGE(TAG, "Failed to parse pH from '%s'", response);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *ph_out = ph;
    return ESP_OK;
}

esp_err_t ph_sensor_cal_points(int *points_out)
{
    if (points_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char response[16];
    esp_err_t err = ezo_transaction("Cal,?", 300, response, sizeof(response));
    if (err != ESP_OK)
    {
        return err;
    }

    const char *prefix = "?CAL,";
    if (strncmp(response, prefix, strlen(prefix)) != 0)
    {
        ESP_LOGE(TAG, "Unexpected cal response: %s", response);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *points_out = (int) strtol(response + strlen(prefix), NULL, 10);
    return ESP_OK;
}

esp_err_t ph_sensor_sleep(void)
{
    char response[16];
    return ezo_transaction("Sleep", 300, response, sizeof(response));
}

int ph_sensor_i2c_scan(uint8_t *addrs, size_t max_addrs)
{
    ph_sensor_init();

    int count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++)
    {
        if (i2c_master_probe(s_i2c_bus, addr, 100) != ESP_OK)
        {
            continue;
        }

        if (addrs != NULL && (size_t) count < max_addrs)
        {
            addrs[count] = addr;
        }
        count++;
    }

    return count;
}

esp_err_t ph_sensor_raw_command(const char *cmd,
                                uint32_t delay_ms,
                                uint8_t *status_out,
                                char *response,
                                size_t response_len,
                                esp_err_t *write_err,
                                esp_err_t *read_err)
{
    if (cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ph_sensor_init();

    esp_err_t err = i2c_master_transmit(s_ezo_dev,
                                          (const uint8_t *) cmd,
                                          strlen(cmd),
                                          EZO_I2C_TIMEOUT_MS);
    if (write_err != NULL)
    {
        *write_err = err;
    }
    if (err != ESP_OK)
    {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    uint8_t buf[EZO_RESPONSE_MAX + 1] = {0};
    err = i2c_master_receive(s_ezo_dev, buf, sizeof(buf), EZO_I2C_TIMEOUT_MS);
    if (read_err != NULL)
    {
        *read_err = err;
    }
    if (err != ESP_OK)
    {
        return err;
    }

    if (status_out != NULL)
    {
        *status_out = buf[0];
    }

    if (response != NULL && response_len > 0)
    {
        size_t payload_len = strnlen((const char *) &buf[1], EZO_RESPONSE_MAX);
        if (payload_len >= response_len)
        {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(response, &buf[1], payload_len);
        response[payload_len] = '\0';
    }

    return ESP_OK;
}

esp_err_t ph_sensor_switch_uart_to_i2c(int i2c_addr_decimal, char *response, size_t response_len)
{
    if (i2c_addr_decimal < 1 || i2c_addr_decimal > 127)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char cmd[16];
    snprintf(cmd, sizeof(cmd), "I2C,%d", i2c_addr_decimal);
    return ph_sensor_uart_command(cmd, response, response_len);
}

esp_err_t ph_sensor_uart_command(const char *cmd, char *response, size_t response_len)
{
    if (cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = EZO_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(EZO_UART_NUM, 512, 512, 0, NULL, 0);
    if (err != ESP_OK)
    {
        return err;
    }

    err = uart_param_config(EZO_UART_NUM, &uart_config);
    if (err != ESP_OK)
    {
        uart_driver_delete(EZO_UART_NUM);
        return err;
    }

    err = uart_set_pin(EZO_UART_NUM,
                       CONFIG_PH_EZO_UART_TX_GPIO,
                       CONFIG_PH_EZO_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        uart_driver_delete(EZO_UART_NUM);
        return err;
    }

    uart_flush(EZO_UART_NUM);

    char frame[32];
    int frame_len = snprintf(frame, sizeof(frame), "%s\r", cmd);
    uart_write_bytes(EZO_UART_NUM, frame, frame_len);

    vTaskDelay(pdMS_TO_TICKS(1200));

    uint8_t buf[64] = {0};
    int read_len = uart_read_bytes(EZO_UART_NUM, buf, sizeof(buf) - 1, pdMS_TO_TICKS(500));
    uart_driver_delete(EZO_UART_NUM);

    if (read_len > 0)
    {
        if (response != NULL && response_len > 0)
        {
            size_t copy_len = (size_t) read_len;
            if (copy_len >= response_len)
            {
                copy_len = response_len - 1;
            }
            memcpy(response, buf, copy_len);
            response[copy_len] = '\0';
        }
        ESP_LOGI(TAG, "EZO UART '%s' -> %.*s", cmd, read_len, buf);
        return ESP_OK;
    }

    ESP_LOGW(TAG,
             "No UART response to '%s' (wire EZO RX->D6/GPIO%d, EZO TX->D7/GPIO%d)",
             cmd,
             CONFIG_PH_EZO_UART_TX_GPIO,
             CONFIG_PH_EZO_UART_RX_GPIO);
    return ESP_ERR_TIMEOUT;
}
