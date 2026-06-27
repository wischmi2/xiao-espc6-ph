set(esp_idf_common "../submodules/golioth-firmware-sdk/examples/esp_idf/common")
set(project_common "../common")

set(includes
    "${esp_idf_common}"
    "."
)

set(srcs
    "app_main.c"
    "ph_sensor.c"
    "ph_ops.c"
    "ph_shell.c"
    "ph_rpc.c"
    "temp_sensor.c"
    "${project_common}/shell.c"
    "${esp_idf_common}/wifi.c"
    "${project_common}/nvs.c"
    "${esp_idf_common}/sample_credentials.c"
)

set(deps
    "golioth_sdk"
    "console"
    "spi_flash"
    "nvs_flash"
    "json"
    "driver"
    "esp_hw_support"
    "esp_wifi"
    "esp_timer"
    "esp_adc"
)

idf_component_register(
    INCLUDE_DIRS "${includes}"
    SRCS "${srcs}"
    PRIV_REQUIRES "${deps}"
)
