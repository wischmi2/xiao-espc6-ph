#pragma once

#include <stdbool.h>

#include "esp_err.h"

void temp_sensor_init(void);
esp_err_t temp_sensor_read_celsius(float *temp_c);
