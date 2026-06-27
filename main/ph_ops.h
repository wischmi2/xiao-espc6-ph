#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct
{
    float ph;
    float temp_c;
    int cal_points;
    bool ph_valid;
} ph_reading_t;

void ph_ops_init(void);

esp_err_t ph_ops_read(ph_reading_t *out, bool sleep_after);
esp_err_t ph_ops_cal_point(const char *point, float buffer_ph);
esp_err_t ph_ops_cal_clear(void);
esp_err_t ph_ops_cal_status(char *cal_out, size_t cal_len, char *slope_out, size_t slope_len);
