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

**First-time EZO setup:** the EZO ships in UART mode. Run `ph mode i2c 99` at the `esp32>` shell (EZO RX→D6, TX→D7), then wire I2C on D4/D5. Calibration persists across mode changes.

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

By default the device **stays awake** after each boot (10 s shell grace period, then one measurement/stream cycle, then serial shell). When you are done with setup or calibration, enable battery mode:

```
settings set power/deep-sleep 1
reset
```

## Expected behavior

- **Provisioning:** stays awake with serial shell until WiFi and Golioth credentials are set; run `reset` after provisioning
- **Default operation:** stays awake — 10 s shell grace period after boot, one measurement/stream cycle, then serial shell (no deep sleep)
- **Battery mode:** `settings set power/deep-sleep 1` then `reset` — wakes every **60 s**, measures, streams, deep sleeps
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
| Boot grace period (seconds) | 10 | Shell stays open after wake before measuring |
| I2C SDA GPIO | 22 | D4 |
| I2C SCL GPIO | 23 | D5 |
| EZO pH I2C address | 0x63 | Decimal 99 |
| DS18B20 data GPIO | 2 | D2 |
| Battery sense GPIO | 0 | A0 / D0 |
| Low battery cutoff (mV) | 3000 | Skip WiFi below this voltage |

## Calibration

Calibration is stored on the EZO board. See [PLAN.md](PLAN.md) for buffer procedures.

### Serial shell (bench mode)

With the device awake (default), use the `ph` command at the `esp32>` prompt:

```
ph read
ph cal status
ph cal mid 7.00
ph cal low 4.00
ph cal high 10.00
ph cal clear
```

Run `ph read` repeatedly while the probe stabilizes in buffer solution, then issue the cal command.

### Golioth RPC (bench mode)

When WiFi is connected in bench mode, these RPC methods are available:

| Method | Params | Action |
|---|---|---|
| `ph_read` | none | Return pH, temp, cal_points |
| `cal_status` | none | Return EZO `Cal,?` and `Slope,?` |
| `cal_mid` | `[7.00]` | Temp compensate, then `Cal,mid,7.00` |
| `cal_low` | `[4.00]` | `Cal,low,4.00` |
| `cal_high` | `[10.00]` | `Cal,high,10.00` |
| `cal_clear` | none | `Cal,clear` |

RPC is active only in bench mode (default). Battery mode (`power/deep-sleep 1`) streams readings but does not keep RPC online.

### I2C troubleshooting

At the `esp32>` prompt run:

```
ph debug
```

This prints the configured SDA/SCL/address, scans the I2C bus, and sends the EZO `i` (info) command with detailed error codes.

| Symptom | Likely cause |
|---|---|
| Scan finds nothing, `write=ESP_ERR_TIMEOUT` | No power, wrong pins, SDA/SCL swapped, or EZO still in **UART mode** |
| Scan finds `0x63` but probe fails | EZO asleep is OK (first command wakes it); check baud/mode if status 255 |
| Scan finds a different address | Update address with `I2C,<addr>` over UART once, or change `PH_SENSOR_I2C_ADDR` in menuconfig |
| Isolated carrier board | Leave **OFF** pin unconnected or pulled **high**; low = board powered off |

**UART mode switch (one-time):** EZO ships in UART mode. At the `esp32>` prompt, wire EZO **RX→D6**, **TX→D7**, run `ph mode i2c 99`, then reconnect **SDA→D4**, **SCL→D5**. Do not type `I2C,99` at the shell — that goes to the ESP32, not the EZO.

## Project layout

```
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── app_main.c
│   ├── ph_sensor.c
│   ├── ph_ops.c
│   ├── ph_shell.c
│   ├── ph_rpc.c
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
