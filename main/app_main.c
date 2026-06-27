#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "ph_ops.h"
#include "ph_rpc.h"
#include "ph_shell.h"
#include "sample_credentials.h"
#include "shell.h"
#include "wifi.h"

#include <golioth/client.h>
#include <golioth/rpc.h>
#include <golioth/stream.h>

#define TAG "ph_monitor"
#define STREAM_PATH "sensor/ph"
#define VBAT_DIVIDER_RATIO 2.0f

static SemaphoreHandle_t s_connected_sem;
static SemaphoreHandle_t s_stream_ack_sem;

static void on_client_event(struct golioth_client *client,
                            enum golioth_client_event event,
                            void *arg)
{
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    if (is_connected && s_connected_sem != NULL)
    {
        xSemaphoreGive(s_connected_sem);
    }
    GLTH_LOGI(TAG, "Golioth client %s", is_connected ? "connected" : "disconnected");
}

static void on_stream_ack(struct golioth_client *client,
                          enum golioth_status status,
                          const struct golioth_coap_rsp_code *coap_rsp_code,
                          const char *path,
                          void *arg)
{
    if (status != GOLIOTH_OK)
    {
        GLTH_LOGW(TAG, "Stream push to %s failed: %s", path, golioth_status_to_str(status));
    }
    else
    {
        GLTH_LOGI(TAG, "Stream push to %s acknowledged", path);
    }

    if (s_stream_ack_sem != NULL)
    {
        xSemaphoreGive(s_stream_ack_sem);
    }
}

static float read_battery_voltage(void)
{
    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &adc_handle) != ESP_OK)
    {
        return 0.0f;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    if (adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_cfg) != ESP_OK)
    {
        adc_oneshot_del_unit(adc_handle);
        return 0.0f;
    }

    int raw = 0;
    if (adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw) != ESP_OK)
    {
        adc_oneshot_del_unit(adc_handle);
        return 0.0f;
    }

    float battery_v = 0.0f;
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK)
    {
        int voltage_mv = 0;
        if (adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv) == ESP_OK)
        {
            battery_v = ((float) voltage_mv / 1000.0f) * VBAT_DIVIDER_RATIO;
        }
        adc_cali_delete_scheme_curve_fitting(cali_handle);
    }

    adc_oneshot_del_unit(adc_handle);
    return battery_v;
}

static void log_missing_credentials(void)
{
    if (0 == strcmp(nvs_read_wifi_ssid(), NVS_DEFAULT_STR))
    {
        GLTH_LOGW(TAG, "Missing setting: wifi/ssid");
    }
    if (0 == strcmp(nvs_read_wifi_password(), NVS_DEFAULT_STR))
    {
        GLTH_LOGW(TAG, "Missing setting: wifi/psk");
    }
    if (0 == strcmp(nvs_read_golioth_psk_id(), NVS_DEFAULT_STR))
    {
        GLTH_LOGW(TAG, "Missing setting: golioth/psk-id");
    }
    if (0 == strcmp(nvs_read_golioth_psk(), NVS_DEFAULT_STR))
    {
        GLTH_LOGW(TAG, "Missing setting: golioth/psk");
    }
}

static void provisioning_loop(void)
{
    ph_shell_register();
    shell_start();
    log_missing_credentials();
    GLTH_LOGW(TAG,
             "Use the shell settings commands to set missing credentials, "
             "then verify with: settings get <key>");

    while (!nvs_credentials_are_set())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    GLTH_LOGI(TAG,
             "Credentials configured. Device stays awake by default; set "
             "'settings set power/deep-sleep 1' when ready for battery mode, then reset.");
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static bool stream_ph_reading(struct golioth_client *client,
                              const ph_reading_t *reading,
                              float battery_v)
{
    char json[128];
    int len = 0;

    if (reading->ph_valid)
    {
        len = snprintf(json,
                       sizeof(json),
                       "{\"ph\":%.3f,\"temp_c\":%.1f,\"battery_v\":%.2f,"
                       "\"cal_points\":%d,\"heartbeat\":0}",
                       reading->ph,
                       reading->temp_c,
                       battery_v,
                       reading->cal_points);
    }
    else
    {
        len = snprintf(json,
                       sizeof(json),
                       "{\"temp_c\":%.1f,\"battery_v\":%.2f,\"cal_points\":%d,"
                       "\"heartbeat\":0}",
                       reading->temp_c,
                       battery_v,
                       reading->cal_points);
    }

    if (len <= 0 || len >= (int) sizeof(json))
    {
        GLTH_LOGE(TAG, "Failed to format stream JSON");
        return false;
    }

    enum golioth_status status = golioth_stream_set_async(client,
                                                          STREAM_PATH,
                                                          GOLIOTH_CONTENT_TYPE_JSON,
                                                          (const uint8_t *) json,
                                                          (size_t) len,
                                                          on_stream_ack,
                                                          NULL);
    if (status != GOLIOTH_OK)
    {
        GLTH_LOGW(TAG, "Failed to enqueue stream update: %s", golioth_status_to_str(status));
        return false;
    }

    return true;
}

static struct golioth_client *connect_golioth(void)
{
    wifi_init(nvs_read_wifi_ssid(), nvs_read_wifi_password());
    wifi_wait_for_connected();

    const struct golioth_client_config *config = golioth_sample_credentials_get();
    struct golioth_client *client = golioth_client_create(config);
    assert(client);

    s_connected_sem = xSemaphoreCreateBinary();
    s_stream_ack_sem = xSemaphoreCreateBinary();
    golioth_client_register_event_callback(client, on_client_event, NULL);

    GLTH_LOGI(TAG, "Waiting for connection to Golioth...");
    xSemaphoreTake(s_connected_sem, portMAX_DELAY);
    return client;
}

static void log_reading(const ph_reading_t *reading, float battery_v)
{
    GLTH_LOGI(TAG,
             "Reading: ph=%s%.3f temp=%.1f C battery=%.2f V cal_points=%d",
             reading->ph_valid ? "" : "invalid ",
             reading->ph_valid ? reading->ph : 0.0f,
             reading->temp_c,
             battery_v,
             reading->cal_points);
}

static bool take_and_stream_reading(struct golioth_client *client, bool sleep_after)
{
    ph_reading_t reading = {0};
    if (ph_ops_read(&reading, sleep_after) != ESP_OK)
    {
        GLTH_LOGW(TAG, "Sensor read failed");
        return false;
    }

    float battery_v = read_battery_voltage();
    log_reading(&reading, battery_v);

    if (battery_v > 0.1f && battery_v < (CONFIG_PH_BATTERY_LOW_V / 1000.0f))
    {
        GLTH_LOGW(TAG, "Battery below %.2f V; skipping stream",
                  CONFIG_PH_BATTERY_LOW_V / 1000.0f);
        return false;
    }

    if (client == NULL)
    {
        return true;
    }

    if (stream_ph_reading(client, &reading, battery_v))
    {
        xSemaphoreTake(s_stream_ack_sem, pdMS_TO_TICKS(CONFIG_PH_STREAM_ACK_TIMEOUT_MS));
    }
    return true;
}

static void run_measurement_cycle(void)
{
    struct golioth_client *client = connect_golioth();
    take_and_stream_reading(client, true);
    golioth_client_destroy(client);
    s_connected_sem = NULL;
    s_stream_ack_sem = NULL;
    esp_wifi_stop();
}

static void bench_mode_run(void)
{
    struct golioth_client *client = connect_golioth();
    struct golioth_rpc *rpc = golioth_rpc_init(client);
    assert(rpc);
    ph_rpc_register(rpc);

    take_and_stream_reading(client, false);

    GLTH_LOGI(TAG,
             "Bench mode: WiFi and Golioth RPC active. Use 'ph read' or RPC for calibration.");
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void enter_deep_sleep(void)
{
    GLTH_LOGI(TAG, "Entering deep sleep for %d s", CONFIG_PH_POLL_INTERVAL_S);
    esp_sleep_enable_timer_wakeup((uint64_t) CONFIG_PH_POLL_INTERVAL_S * 1000000ULL);
    esp_deep_sleep_start();
}

static void boot_grace_period(void)
{
    ph_shell_register();
    shell_start();

    if (CONFIG_PH_BOOT_GRACE_S <= 0)
    {
        return;
    }

    GLTH_LOGI(TAG,
             "Serial shell open for %d s before measurement (connect and type now)",
             CONFIG_PH_BOOT_GRACE_S);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_PH_BOOT_GRACE_S * 1000));
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    if (wake_cause == ESP_SLEEP_WAKEUP_TIMER)
    {
        GLTH_LOGI(TAG, "Wake from deep sleep timer");
    }
    else
    {
        GLTH_LOGI(TAG, "XIAO ESP-C6 pH Monitor starting");
    }

    nvs_init();
    ph_ops_init();

    if (!nvs_credentials_are_set())
    {
        provisioning_loop();
        return;
    }

    boot_grace_period();

    if (nvs_deep_sleep_enabled())
    {
        run_measurement_cycle();
        enter_deep_sleep();
    }

    bench_mode_run();
}
