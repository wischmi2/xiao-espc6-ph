# XIAO ESP-C6 pH Monitor

Battery-powered pH monitoring with a [Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/), [Atlas Scientific EZO pH circuit](https://files.atlas-scientific.com/pH_EZO_Datasheet.pdf), and [Golioth](https://golioth.io) cloud streaming.

See [PLAN.md](PLAN.md) for the full hardware wiring diagram, power budget, calibration procedure, and Phase 2 RPC plan.

## Hardware

| Component | Notes |
|---|---|
| Seeed XIAO ESP32-C6 | Wi-Fi 6, 3.3V I/O, LiPo on BAT+ / BAT- |
| Atlas EZO pH circuit | I2C default address **99 (0x63)**; must be switched from UART to I2C once |
| DS18B20 | 1-Wire temp sensor on D2 for EZO temperature compensation |
| 3.7 V LiPo (protected) | Powers the board via BAT pads; USB-C charges via onboard SGM40567 |

### Wiring

| EZO pH | XIAO ESP32-C6 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | D4 (GPIO22) |
| SCL | D5 (GPIO23) |

| DS18B20 | XIAO ESP32-C6 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| DATA | D2 (GPIO2) + 4.7 kΩ pull-up to 3V3 |

Optional battery voltage sense: solder Seeed's **200k / 200k divider** mod and read **A0 / D0 (GPIO0)** in firmware.

**First-time EZO setup:** the EZO ships in UART mode. Send `I2C,99` once over UART on D6/D7 at 9600 baud, or use Atlas Desktop. Calibration persists across mode changes.

## Prerequisites

- [ESP-IDF v5.4.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/windows-setup.html)
- USB cable for the XIAO ESP32-C6
- Golioth account with a device provisioned (e.g. `esp32-c6-ph`)

## Clone and initialize

```powershell
git clone --recursive https://github.com/wischmi2/xiao-espc6-ph.git
cd xiao-espc6-ph
git submodule update --init --recursive
cd submodules/golioth-firmware-sdk
git checkout v0.22.0
git submodule update --init --recursive
```

## Build and flash

Open an **ESP-IDF** terminal:

```powershell
cd xiao-espc6-ph
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with your serial port. Hold **BOOT** while plugging in USB if the port is not detected.

## Golioth setup

### 1. Device credentials

In [Golioth Console](https://console.golioth.io), create a device and copy the **PSK-ID** and **PSK**.

### 2. Stream pipeline

Deploy [pipelines/json-to-lightdb.yml](pipelines/json-to-lightdb.yml) as a JSON pipeline in the console.

### 3. Provision over serial

After flashing, in the serial monitor:

```
settings set wifi/ssid <your-ssid>
settings set wifi/psk <your-wifi-password>
settings set golioth/psk-id <psk-id-from-console>
settings set golioth/psk <psk-from-console>
reset
```

## Expected behavior

- **Provisioning:** stays awake with serial shell until WiFi and Golioth credentials are set; run `reset` after provisioning
- **Normal operation:** wakes every **60 s** (configurable), reads pH + temperature + battery, streams to Golioth, then deep sleeps
- Sends `L,0` at boot to disable the EZO LED and `Sleep` after each reading
- Skips WiFi when battery is below **3.0 V** (configurable)
- Stream path: `sensor/ph`

Example payload:

```json
{"ph": 7.123, "temp_c": 23.5, "battery_v": 3.85, "cal_points": 2, "heartbeat": 0}
```

## Configuration

Run `idf.py menuconfig` and open **pH Monitor Configuration**:

| Option | Default | Description |
|---|---|---|
| Wake / poll interval (seconds) | 60 | Deep sleep period between readings |
| I2C SDA GPIO | 22 | D4 |
| I2C SCL GPIO | 23 | D5 |
| EZO pH I2C address | 0x63 | Decimal 99 |
| DS18B20 data GPIO | 2 | D2 |
| Battery sense GPIO | 0 | A0 / D0 |
| Low battery cutoff (mV) | 3000 | Skip WiFi below this voltage |

## Calibration

Calibration is stored on the EZO board. See [PLAN.md](PLAN.md) for buffer procedures and command reference. Phase 2 will add Golioth RPC methods for app-driven field calibration.

## Project layout

```
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── app_main.c
│   ├── ph_sensor.c
│   ├── temp_sensor.c
│   └── Kconfig.projbuild
├── common/
│   ├── shell.c
│   └── nvs.c
├── pipelines/
│   └── json-to-lightdb.yml
└── submodules/
    └── golioth-firmware-sdk/
```
