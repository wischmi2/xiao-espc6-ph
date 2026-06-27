#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

void ph_sensor_init(void);
void ph_sensor_deinit(void);
bool ph_sensor_is_initialized(void);

esp_err_t ph_sensor_set_temp(float temp_c);
esp_err_t ph_sensor_read(float *ph_out);
esp_err_t ph_sensor_cal_points(int *points_out);
esp_err_t ph_sensor_disable_led(void);
esp_err_t ph_sensor_sleep(void);

esp_err_t ph_sensor_query(const char *cmd, uint32_t delay_ms, char *response, size_t response_len);
esp_err_t ph_sensor_calibrate(const char *point, float buffer_ph);
esp_err_t ph_sensor_cal_clear(void);

int ph_sensor_i2c_scan(uint8_t *addrs, size_t max_addrs);
esp_err_t ph_sensor_raw_command(const char *cmd,
                                uint32_t delay_ms,
                                uint8_t *status_out,
                                char *response,
                                size_t response_len,
                                esp_err_t *write_err,
                                esp_err_t *read_err);
esp_err_t ph_sensor_switch_uart_to_i2c(int i2c_addr_decimal,
                                       char *response,
                                       size_t response_len);
esp_err_t ph_sensor_uart_command(const char *cmd, char *response, size_t response_len);
