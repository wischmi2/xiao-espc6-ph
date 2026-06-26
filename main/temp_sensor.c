#include "temp_sensor.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG "temp_sensor"

#define DS18_CMD_CONVERT 0x44
#define DS18_CMD_READ_SCRATCH 0xBE

static int s_gpio;

static inline void ds18_drive_low(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_gpio, 0);
}

static inline void ds18_release(void)
{
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);
}

static bool ds18_reset(void)
{
    ds18_drive_low();
    esp_rom_delay_us(480);
    ds18_release();
    esp_rom_delay_us(70);
    bool presence = gpio_get_level(s_gpio) == 0;
    esp_rom_delay_us(410);
    return presence;
}

static void ds18_write_bit(int bit)
{
    ds18_drive_low();
    if (bit)
    {
        esp_rom_delay_us(6);
        ds18_release();
        esp_rom_delay_us(64);
    }
    else
    {
        esp_rom_delay_us(60);
        ds18_release();
        esp_rom_delay_us(10);
    }
}

static int ds18_read_bit(void)
{
    ds18_drive_low();
    esp_rom_delay_us(3);
    ds18_release();
    esp_rom_delay_us(10);
    int value = gpio_get_level(s_gpio);
    esp_rom_delay_us(50);
    return value;
}

static void ds18_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++)
    {
        ds18_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ds18_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++)
    {
        byte >>= 1;
        if (ds18_read_bit())
        {
            byte |= 0x80;
        }
    }
    return byte;
}

static esp_err_t ds18_read_celsius(float *temp_c)
{
    if (!ds18_reset())
    {
        ESP_LOGE(TAG, "DS18B20 not present on GPIO %d", s_gpio);
        return ESP_ERR_NOT_FOUND;
    }

    ds18_write_byte(0xCC);
    ds18_write_byte(DS18_CMD_CONVERT);
    vTaskDelay(pdMS_TO_TICKS(750));

    if (!ds18_reset())
    {
        return ESP_ERR_NOT_FOUND;
    }

    ds18_write_byte(0xCC);
    ds18_write_byte(DS18_CMD_READ_SCRATCH);

    uint8_t scratch[9];
    for (int i = 0; i < 9; i++)
    {
        scratch[i] = ds18_read_byte();
    }

    if (scratch[0] == 0xFF && scratch[1] == 0xFF)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    int16_t raw = (int16_t) ((scratch[1] << 8) | scratch[0]);
    *temp_c = (float) raw / 16.0f;
    return ESP_OK;
}

void temp_sensor_init(void)
{
    s_gpio = CONFIG_TEMP_SENSOR_GPIO;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << s_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "DS18B20 on GPIO %d", s_gpio);
}

esp_err_t temp_sensor_read_celsius(float *temp_c)
{
    if (temp_c == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ds18_read_celsius(temp_c);
}
