#include <string.h>

#include "esp_log.h"
#include "ph_ops.h"
#include "ph_rpc.h"
#include <golioth/client.h>
#include <golioth/rpc.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#define TAG "ph_rpc"

static bool decode_optional_buffer_ph(zcbor_state_t *params, double *buffer_ph, double default_value)
{
    double value = 0.0;
    if (zcbor_float_decode(params, &value))
    {
        *buffer_ph = value;
        return true;
    }
    *buffer_ph = default_value;
    return true;
}

static enum golioth_rpc_status rpc_encode_reading(zcbor_state_t *detail, const ph_reading_t *reading)
{
    bool ok = zcbor_tstr_put_lit(detail, "ph")
              && zcbor_float64_put(detail, reading->ph)
              && zcbor_tstr_put_lit(detail, "temp_c")
              && zcbor_float64_put(detail, reading->temp_c)
              && zcbor_tstr_put_lit(detail, "cal_points")
              && zcbor_int32_put(detail, reading->cal_points)
              && zcbor_tstr_put_lit(detail, "ph_valid")
              && zcbor_int32_put(detail, reading->ph_valid ? 1 : 0);
    return ok ? GOLIOTH_RPC_OK : GOLIOTH_RPC_RESOURCE_EXHAUSTED;
}

static enum golioth_rpc_status on_ph_read(zcbor_state_t *request_params_array,
                                          zcbor_state_t *response_detail_map,
                                          void *callback_arg)
{
    ph_reading_t reading = {0};
    if (ph_ops_read(&reading, false) != ESP_OK)
    {
        return GOLIOTH_RPC_RESOURCE_EXHAUSTED;
    }
    return rpc_encode_reading(response_detail_map, &reading);
}

static enum golioth_rpc_status on_cal_status(zcbor_state_t *request_params_array,
                                             zcbor_state_t *response_detail_map,
                                             void *callback_arg)
{
    char cal[16] = {0};
    char slope[32] = {0};
    if (ph_ops_cal_status(cal, sizeof(cal), slope, sizeof(slope)) != ESP_OK)
    {
        return GOLIOTH_RPC_RESOURCE_EXHAUSTED;
    }

    bool ok = zcbor_tstr_put_lit(response_detail_map, "cal")
              && zcbor_tstr_put_term(response_detail_map, cal, strlen(cal))
              && zcbor_tstr_put_lit(response_detail_map, "slope")
              && zcbor_tstr_put_term(response_detail_map, slope, strlen(slope));
    return ok ? GOLIOTH_RPC_OK : GOLIOTH_RPC_RESOURCE_EXHAUSTED;
}

static enum golioth_rpc_status on_cal_point(zcbor_state_t *request_params_array,
                                            zcbor_state_t *response_detail_map,
                                            const char *point,
                                            double default_buffer_ph)
{
    double buffer_ph = default_buffer_ph;
    if (!decode_optional_buffer_ph(request_params_array, &buffer_ph, default_buffer_ph))
    {
        return GOLIOTH_RPC_INVALID_ARGUMENT;
    }

    if (ph_ops_cal_point(point, (float) buffer_ph) != ESP_OK)
    {
        return GOLIOTH_RPC_RESOURCE_EXHAUSTED;
    }

    bool ok = zcbor_tstr_put_lit(response_detail_map, "status")
              && zcbor_tstr_put_lit(response_detail_map, "ok")
              && zcbor_tstr_put_lit(response_detail_map, "point")
              && zcbor_tstr_put_term(response_detail_map, point, strlen(point))
              && zcbor_tstr_put_lit(response_detail_map, "buffer_ph")
              && zcbor_float64_put(response_detail_map, buffer_ph);
    return ok ? GOLIOTH_RPC_OK : GOLIOTH_RPC_RESOURCE_EXHAUSTED;
}

static enum golioth_rpc_status on_cal_mid(zcbor_state_t *request_params_array,
                                          zcbor_state_t *response_detail_map,
                                          void *callback_arg)
{
    return on_cal_point(request_params_array, response_detail_map, "mid", 7.0);
}

static enum golioth_rpc_status on_cal_low(zcbor_state_t *request_params_array,
                                          zcbor_state_t *response_detail_map,
                                          void *callback_arg)
{
    return on_cal_point(request_params_array, response_detail_map, "low", 4.0);
}

static enum golioth_rpc_status on_cal_high(zcbor_state_t *request_params_array,
                                           zcbor_state_t *response_detail_map,
                                           void *callback_arg)
{
    return on_cal_point(request_params_array, response_detail_map, "high", 10.0);
}

static enum golioth_rpc_status on_cal_clear(zcbor_state_t *request_params_array,
                                            zcbor_state_t *response_detail_map,
                                            void *callback_arg)
{
    if (ph_ops_cal_clear() != ESP_OK)
    {
        return GOLIOTH_RPC_RESOURCE_EXHAUSTED;
    }

    bool ok = zcbor_tstr_put_lit(response_detail_map, "status")
              && zcbor_tstr_put_lit(response_detail_map, "ok");
    return ok ? GOLIOTH_RPC_OK : GOLIOTH_RPC_RESOURCE_EXHAUSTED;
}

static void register_method(struct golioth_rpc *rpc, const char *method, golioth_rpc_cb_fn callback)
{
    enum golioth_status status = golioth_rpc_register(rpc, method, callback, NULL);
    if (status != GOLIOTH_OK)
    {
        ESP_LOGE(TAG, "Failed to register RPC method %s: %s", method, golioth_status_to_str(status));
    }
}

void ph_rpc_register(struct golioth_rpc *rpc)
{
    if (rpc == NULL)
    {
        return;
    }

    register_method(rpc, "ph_read", on_ph_read);
    register_method(rpc, "cal_status", on_cal_status);
    register_method(rpc, "cal_mid", on_cal_mid);
    register_method(rpc, "cal_low", on_cal_low);
    register_method(rpc, "cal_high", on_cal_high);
    register_method(rpc, "cal_clear", on_cal_clear);
    ESP_LOGI(TAG, "pH calibration RPC methods registered");
}
