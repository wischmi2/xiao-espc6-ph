#include "ph_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ph_sensor.h"
#include "temp_sensor.h"

static SemaphoreHandle_t s_mutex;
static bool s_led_disabled;

static esp_err_t lock(void)
{
    if (s_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static esp_err_t ensure_sensors(void)
{
    ph_sensor_init();
    if (!s_led_disabled)
    {
        esp_err_t err = ph_sensor_disable_led();
        if (err == ESP_OK)
        {
            s_led_disabled = true;
        }
    }
    temp_sensor_init();
    return ESP_OK;
}

static esp_err_t apply_temp_compensation(float *temp_c)
{
    float temp = 25.0f;
    if (temp_sensor_read_celsius(&temp) != ESP_OK)
    {
        temp = 25.0f;
    }
    *temp_c = temp;
    return ph_sensor_set_temp(temp);
}

void ph_ops_init(void)
{
    if (s_mutex == NULL)
    {
        s_mutex = xSemaphoreCreateMutex();
    }
}

esp_err_t ph_ops_read(ph_reading_t *out, bool sleep_after)
{
    if (out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (lock() != ESP_OK)
    {
        return ESP_FAIL;
    }

    ensure_sensors();
    apply_temp_compensation(&out->temp_c);
    out->ph_valid = (ph_sensor_read(&out->ph) == ESP_OK);
    if (ph_sensor_cal_points(&out->cal_points) != ESP_OK)
    {
        out->cal_points = 0;
    }

    if (sleep_after)
    {
        ph_sensor_sleep();
        ph_sensor_deinit();
    }

    unlock();
    return ESP_OK;
}

esp_err_t ph_ops_cal_point(const char *point, float buffer_ph)
{
    if (point == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (lock() != ESP_OK)
    {
        return ESP_FAIL;
    }

    ensure_sensors();
    float temp_c = 25.0f;
    apply_temp_compensation(&temp_c);
    esp_err_t err = ph_sensor_calibrate(point, buffer_ph);
    unlock();
    return err;
}

esp_err_t ph_ops_cal_clear(void)
{
    if (lock() != ESP_OK)
    {
        return ESP_FAIL;
    }

    ensure_sensors();
    esp_err_t err = ph_sensor_cal_clear();
    unlock();
    return err;
}

esp_err_t ph_ops_cal_status(char *cal_out, size_t cal_len, char *slope_out, size_t slope_len)
{
    if (cal_out == NULL || cal_len == 0 || slope_out == NULL || slope_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (lock() != ESP_OK)
    {
        return ESP_FAIL;
    }

    ensure_sensors();
    esp_err_t err = ph_sensor_query("Cal,?", 300, cal_out, cal_len);
    if (err == ESP_OK)
    {
        err = ph_sensor_query("Slope,?", 300, slope_out, slope_len);
    }
    unlock();
    return err;
}
