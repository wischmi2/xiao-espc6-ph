#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "ph_ops.h"
#include "ph_sensor.h"
#include "ph_shell.h"
#include "sdkconfig.h"
#include "shell.h"

static void print_ph_usage(void)
{
    printf("usage:\n"
           "  ph read\n"
           "  ph debug\n"
           "  ph mode i2c [99]   (UART on D6/D7, then rewire to I2C on D4/D5)\n"
           "  ph mode info       (UART device info on D6/D7)\n"
           "  ph cal status\n"
           "  ph cal mid [7.00]\n"
           "  ph cal low [4.00]\n"
           "  ph cal high [10.00]\n"
           "  ph cal clear\n");
}

static int cmd_ph_debug(void)
{
    uint8_t addrs[16] = {0};
    esp_err_t write_err = ESP_OK;
    esp_err_t read_err = ESP_OK;
    uint8_t status = 0;
    char info[32] = {0};

    printf("I2C config: SDA=GPIO%d SCL=GPIO%d addr=0x%02X freq=%d Hz\n",
           CONFIG_PH_SENSOR_I2C_SDA_GPIO,
           CONFIG_PH_SENSOR_I2C_SCL_GPIO,
           CONFIG_PH_SENSOR_I2C_ADDR,
           CONFIG_PH_SENSOR_I2C_FREQ_HZ);

    int found = ph_sensor_i2c_scan(addrs, sizeof(addrs));
    if (found == 0)
    {
        printf("I2C scan: no devices found\n");
        printf("Check: power/GND, SDA/SCL wiring, pull-ups, EZO still in UART mode\n");
        printf("Isolated carrier: leave OFF pin unconnected or pulled high\n");
    }
    else
    {
        printf("I2C scan: %d device(s):", found);
        for (int i = 0; i < found && i < (int) sizeof(addrs); i++)
        {
            printf(" 0x%02X", addrs[i]);
        }
        printf("\n");
        bool configured_found = false;
        for (int i = 0; i < found && i < (int) sizeof(addrs); i++)
        {
            if (addrs[i] == CONFIG_PH_SENSOR_I2C_ADDR)
            {
                configured_found = true;
                break;
            }
        }
        if (!configured_found)
        {
            printf("Warning: configured addr 0x%02X not in scan results\n",
                   CONFIG_PH_SENSOR_I2C_ADDR);
        }
    }

    esp_err_t err = ph_sensor_raw_command("i", 300, &status, info, sizeof(info), &write_err, &read_err);
    printf("EZO probe 'i': write=%s read=%s overall=%s status=%u payload='%s'\n",
           esp_err_to_name(write_err),
           esp_err_to_name(read_err),
           esp_err_to_name(err),
           status,
           info);

    if (write_err == ESP_ERR_TIMEOUT || read_err == ESP_ERR_TIMEOUT
        || write_err == ESP_ERR_INVALID_STATE)
    {
        printf("Hint: no I2C ACK — check wiring/mode/power, or run 'ph mode i2c 99' first\n");
    }
    if (err == ESP_OK && status == 1)
    {
        printf("EZO I2C communication OK\n");
        return 0;
    }

    if (status == 255)
    {
        printf("Hint: status 255 often means EZO is still in UART mode; send I2C,99 over UART once\n");
    }

    return err == ESP_OK ? 0 : 1;
}

static float parse_buffer_value(int argc, char **argv, int value_index, float default_value)
{
    if (argc <= value_index)
    {
        return default_value;
    }
    return strtof(argv[value_index], NULL);
}

static int cmd_ph(int argc, char **argv)
{
    if (argc < 2)
    {
        print_ph_usage();
        return 1;
    }

    if (0 == strcmp(argv[1], "debug"))
    {
        return cmd_ph_debug();
    }

    if (0 == strcmp(argv[1], "mode"))
    {
        if (argc < 3)
        {
            print_ph_usage();
            return 1;
        }

        if (0 == strcmp(argv[2], "info"))
        {
            char response[64] = {0};
            esp_err_t err = ph_sensor_uart_command("i", response, sizeof(response));
            if (err != ESP_OK)
            {
                printf("UART info failed: %s (wire EZO RX->D6, TX->D7)\n", esp_err_to_name(err));
                return 1;
            }
            printf("EZO UART info: %s\n", response);
            return 0;
        }

        if (0 != strcmp(argv[2], "i2c"))
        {
            print_ph_usage();
            return 1;
        }

        int addr = 99;
        if (argc >= 4)
        {
            addr = (int) strtol(argv[3], NULL, 10);
        }

        printf("Sending I2C,%d over UART (TX=GPIO%d/RX=GPIO%d @ 9600)...\n",
               addr,
               CONFIG_PH_EZO_UART_TX_GPIO,
               CONFIG_PH_EZO_UART_RX_GPIO);
        printf("Wire EZO RX -> D6, EZO TX -> D7, VCC/GND common, then run this command.\n");

        char response[64] = {0};
        esp_err_t err = ph_sensor_switch_uart_to_i2c(addr, response, sizeof(response));
        if (err != ESP_OK)
        {
            printf("UART mode switch failed: %s\n", esp_err_to_name(err));
            return 1;
        }

        printf("EZO replied: %s\n", response);
        if (strstr(response, "*OK") != NULL)
        {
            printf("Mode switch OK. Rewire SDA->D4, SCL->D5 and run 'ph debug'\n");
            return 0;
        }
        if (strstr(response, "*ER") != NULL)
        {
            printf("UART link works, but EZO returned *ER (command error).\n");
            printf("Often means already in I2C mode — rewire SDA->D4, SCL->D5 and run 'ph debug'.\n");
            printf("If I2C still fails, swap EZO RX/TX on D6/D7 and retry, or run 'ph mode info'.\n");
            return 0;
        }

        printf("Unexpected response. Rewire to I2C and run 'ph debug' anyway.\n");
        return 0;
    }

    if (0 == strcmp(argv[1], "read"))
    {
        ph_reading_t reading = {0};
        esp_err_t err = ph_ops_read(&reading, false);
        if (err != ESP_OK)
        {
            printf("pH read failed: %s\n", esp_err_to_name(err));
            return 1;
        }

        if (reading.ph_valid)
        {
            printf("ph=%.3f temp=%.1f C cal_points=%d\n",
                   reading.ph,
                   reading.temp_c,
                   reading.cal_points);
        }
        else
        {
            printf("pH read invalid temp=%.1f C cal_points=%d\n",
                   reading.temp_c,
                   reading.cal_points);
        }
        return 0;
    }

    if (argc < 3 || 0 != strcmp(argv[1], "cal"))
    {
        print_ph_usage();
        return 1;
    }

    if (0 == strcmp(argv[2], "status"))
    {
        char cal[16] = {0};
        char slope[32] = {0};
        esp_err_t err = ph_ops_cal_status(cal, sizeof(cal), slope, sizeof(slope));
        if (err != ESP_OK)
        {
            printf("Calibration status failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("cal=%s slope=%s\n", cal, slope);
        return 0;
    }

    if (0 == strcmp(argv[2], "clear"))
    {
        esp_err_t err = ph_ops_cal_clear();
        if (err != ESP_OK)
        {
            printf("Calibration clear failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("Calibration cleared\n");
        return 0;
    }

    const char *point = NULL;
    float default_value = 0.0f;
    if (0 == strcmp(argv[2], "mid"))
    {
        point = "mid";
        default_value = 7.0f;
    }
    else if (0 == strcmp(argv[2], "low"))
    {
        point = "low";
        default_value = 4.0f;
    }
    else if (0 == strcmp(argv[2], "high"))
    {
        point = "high";
        default_value = 10.0f;
    }
    else
    {
        print_ph_usage();
        return 1;
    }

    float buffer_ph = parse_buffer_value(argc, argv, 3, default_value);
    esp_err_t err = ph_ops_cal_point(point, buffer_ph);
    if (err != ESP_OK)
    {
        printf("Calibration failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("Cal,%s,%.2f OK\n", point, buffer_ph);
    return 0;
}

void ph_shell_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ph",
        .help = "pH sensor: read | debug | mode i2c [99] | cal ...",
        .hint = NULL,
        .func = cmd_ph,
    };
    shell_register_command(&cmd);
}
