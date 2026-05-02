# CAT Node 01 — Environmental & Energy Monitor

An Arduino-based IoT node that measures **temperature**, **humidity**, **AC current draw**, **power consumption**, and **estimated CO₂ emissions** in real time. Data is streamed as JSON over Bluetooth and buffered to EEPROM when connectivity is lost.

Deployed at **CAT Parenthèse** (Tunisia), using the ANME national grid emission factor of **0.614 kg CO₂/kWh**.

---

## Features

- 🌡️ **DHT11** temperature & humidity sensing
- ⚡ **ACS712** hall-effect AC current measurement (RMS over 500 samples)
- 🌍 **CO₂ estimation** based on Tunisia's grid emission factor (ANME)
- 📡 **Bluetooth streaming** of structured JSON payloads every 5 seconds
- 💾 **EEPROM offline buffer** — stores readings when BT is disconnected, auto-flushes on boot
- ✅ **Data quality flags** per sensor field in every payload
- 🔧 Serial & BT commands for diagnostics

---

## Hardware

| Component | Pin | Notes |
|---|---|---|
| DHT11 | D2 | Temperature & humidity |
| ACS712 (20B) | A0 | ±20 A range, 100 mV/A sensitivity |
| HC-05 / HC-06 BT | D10 (RX), D11 (TX) | SoftwareSerial @ 9600 baud |
| Arduino Uno | — | ATmega328P |

> To use a different ACS712 variant, update `ACS712_SENSITIVITY` in the firmware:
> - 05B → `185.0` mV/A
> - 20B → `100.0` mV/A *(default)*
> - 30B → `66.0` mV/A

---

## Wiring Diagram

```
DHT11 DATA  ──► D2
ACS712 OUT  ──► A0
HC-05 TXD   ──► D10 (Arduino RX)
HC-05 RXD   ──► D11 (Arduino TX)
```

All modules share Arduino 5 V and GND rails.

---

## JSON Payload

Published every **5 seconds** over Bluetooth (and to Serial):

```json
{
  "device_id": "CAT_NODE_01",
  "site_id":   "CAT_PARENTHESE",
  "ts_ms":     12345,
  "version":   "2.1",
  "sensors": {
    "temperature_C":  "24.5",
    "humidity_pct":   "58.0",
    "current_mA":     "312.4",
    "power_W":        "71.9",
    "voltage_V":      230,
    "co2_g_per_h":    "44.13",
    "co2_factor_src": "ANME_TN_0.614kgCO2_per_kWh"
  },
  "quality": {
    "all_valid":     true,
    "temp_valid":    true,
    "hum_valid":     true,
    "current_valid": true,
    "power_valid":   true
  }
}
```

### Special events

| Payload | Meaning |
|---|---|
| `{"event":"boot","buffered":false}` | Clean boot, no buffered data |
| `{"event":"buffer_flush_start"}` | About to replay buffered records |
| `{"buffered":true,"index":N,...}` | A replayed offline record |
| `{"event":"buffer_flush_end"}` | Replay complete |

---

## EEPROM Buffer

The node stores each reading to EEPROM so data is not lost if Bluetooth is unavailable. On the next boot, all buffered records are flushed over BT automatically.

- **Record size:** 10 bytes
- **Max records:** `(EEPROM size − 2) / 10` ≈ **100 records** on a Uno (1 KB EEPROM)
- Storage wraps around (circular buffer) once full

---

## Serial / BT Commands

| Command | Action |
|---|---|
| `d` | Print all EEPROM records to Serial (CSV) |
| `c` | Clear the EEPROM record counter |
| `s` | Print current buffered record count |

Commands work over both the USB Serial port and the Bluetooth link.

---

## Build & Flash (PlatformIO)

```ini
[env:uno]
platform  = atmelavr
board     = uno
framework = arduino
upload_port          = COM8
upload_speed         = 115200
upload_protocol      = arduino
upload_resetmethod   = arduino
lib_deps =
    adafruit/DHT sensor library@^1.4.7
    adafruit/Adafruit Unified Sensor@^1.1.14
    bblanchon/ArduinoJson@^6.21.5
monitor_speed = 9600
```

```bash
# Install PlatformIO CLI, then:
pio run --target upload
pio device monitor
```

Alternatively open the project in **VS Code** with the PlatformIO extension and click *Upload*.

---

## Configuration Reference

All tuneable constants are at the top of `main.cpp`:

| Constant | Default | Description |
|---|---|---|
| `DHTPIN` | `2` | DHT11 data pin |
| `ACS712_PIN` | `A0` | Analog input for ACS712 |
| `ACS712_SENSITIVITY` | `100.0` | mV/A for your module variant |
| `ACS712_VREF` | `2500.0` | mV at zero current (Vcc/2) |
| `VRMS_ASSUMED` | `230.0` | Mains RMS voltage (V) |
| `ADC_SAMPLES` | `500` | Samples per RMS current reading |
| `CO2_KG_PER_KWH` | `0.614` | Tunisia grid emission factor |
| `BT_RX / BT_TX` | `10 / 11` | SoftwareSerial pins for HC-05 |

---

## Project Structure

```
.
├── src/
│   └── main.cpp        # All firmware logic
├── platformio.ini      # PlatformIO build config
└── README.md
```

---

## License

MIT — see `LICENSE` for details.
