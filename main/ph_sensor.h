#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

void ph_sensor_init(void);
void ph_sensor_deinit(void);

esp_err_t ph_sensor_set_temp(float temp_c);
esp_err_t ph_sensor_read(float *ph_out);
esp_err_t ph_sensor_cal_points(int *points_out);
esp_err_t ph_sensor_disable_led(void);
esp_err_t ph_sensor_sleep(void);
