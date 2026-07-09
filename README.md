# Distributed Multi-Channel PWM Air Cleaner (AC) Network

### Core Architecture: ESP32-S3 | Framework: PlatformIO (Arduino Core v2.0.14)
### Industrial Protocols: Modbus RTU (RS-485 Half-Duplex) & Routerless ESP-NOW Star Topology

---

## 1. System Architecture & Topology

This repository contains the complete, enterprise-ready C++ firmware for an industrial distributed network of multi-channel PWM Computer-Fan Air Cleaners (ACs). The architecture utilizes a **routerless wireless Star Topology** to optimize data throughput and eliminate deployment reliance on local facility IT infrastructure.

```
       [ Upstream Attune IoT Gateway ] 
                     │
             (Modbus RTU over RS-485)
                     │
                     ▼
          ┌───────────────────────┐         (2.4 GHz Wi-Fi)
          │   Server AC (Master)  │ ══════════════════════════► [ Cradlepoint Cellular Router ]
          └───────────────────────┘                                          │
            │           │        │                                           ▼
       (ESP-NOW)    (ESP-NOW)  (ESP-NOW)                                 [ Cloud ]
            │           │        │                                    (REST JSON API)
            ▼           ▼        ▼
       ┌────────┐   ┌────────┐   ┌────────┐
       │Client 1│   │Client 2│   │Client N│  ... up to 19 Clients
       └────────┘   └────────┘   └────────┘
```

### Key Subsystems
* **Server AC (Master):** Acts as the central system bridge. It targets a remote cloud REST API over a dedicated 2.4 GHz uplink provided by a cellular Cradlepoint router. Concurrently, it exposes an upstream hardware RS-485 interface operating as a Modbus RTU Server/Slave to an Attune IoT Bridge, while driving up to 8 local PWM fan channels and compiling downstream synchronization data.
* **Client ACs (Slaves):** Dedicated hardware edge nodes executing high-speed localized differential pressure reading, PM particle counting, and individual fan tracking. They depend on bi-directional ESP-NOW transactions to synchronize with the Server's instructions.
* **Dynamic RF Cohabitation:** To eliminate 2.4 GHz radio layer packet collisions between the local ESP-NOW topology and the cellular backhaul uplink, the Server scans the environment at boot, isolates the exact operational Wi-Fi channel utilized by the Cradlepoint router, and locks the local ESP-NOW layer to that frequency.
* **Loss-of-Connectivity Fail-Safe:** If the Server fails to resolve its cloud endpoint for 10 consecutive connection strikes or 10 continuous minutes, an emergency fail-safe is asserted. The Server immediately overrides its operating capacity to **75%**, sending a high-priority ESP-NOW broadcast forcing all paired Clients to transition their fan arrays to a 75% fallback threshold until connectivity restores.

---

## 2. Hardware Interface & Pinout Map (Section 7.4)

The firmware enforces the following precise physical GPIO mapping on the ESP32-S3:

| Pin Macro | ESP32-S3 GPIO | Description |
| :--- | :--- | :--- |
| **`FAN1_PWM_GEN`** | GPIO 4 | Fan Channel 1 PWM Output (25 kHz) |
| **`FAN2_PWM_GEN`** | GPIO 5 | Fan Channel 2 PWM Output (25 kHz) |
| **`FAN3_PWM_GEN`** | GPIO 6 | Fan Channel 3 PWM Output (25 kHz) |
| **`FAN4_PWM_GEN`** | GPIO 7 | Fan Channel 4 PWM Output (25 kHz) |
| **`FAN5_PWM_GEN`** | GPIO 15 | Fan Channel 5 PWM Output (25 kHz) |
| **`FAN6_PWM_GEN`** | GPIO 16 | Fan Channel 6 PWM Output (25 kHz) |
| **`FAN7_PWM_GEN`** | GPIO 2 | Fan Channel 7 PWM Output (25 kHz) |
| **`FAN8_PWM_GEN`** | GPIO 1 | Fan Channel 8 PWM Output (25 kHz) |
| **`MPX_ADDR0`** | GPIO 12 | 74HC4051 Multiplexer Address Bit A (LSB) |
| **`MPX_ADDR1`** | GPIO 13 | 74HC4051 Multiplexer Address Bit B |
| **`MPX_ADDR2`** | GPIO 14 | 74HC4051 Multiplexer Address Bit C (MSB) |
| **`MPX_READ`** | GPIO 11 | Common Tachometer Pulse Input (Interrupt Edge) |
| **`RS485_TX`** | GPIO 17 | Hardware UART1 TX to MAX3485 |
| **`RS485_RX`** | GPIO 18 | Hardware UART1 RX from MAX3485 |
| **`RS232_REDE`** | GPIO 8 | MAX3485 Direction Line (HIGH = TX, LOW = RX) |
| **`PMS5003_TX`** | GPIO 43 | Plantower PMS5003 Sensor TX (ESP32 UART2 RX) |
| **`PMS5003_RX`** | GPIO 44 | Plantower PMS5003 Sensor RX (ESP32 UART2 TX) |
| **`PMS5003_SET`**| GPIO 9 | Plantower Low Power/Passive Control Line |
| **`PMS5003_RST`**| GPIO 10 | Plantower Hardware Sensor Reset |
| **`I2C_SDA`** | GPIO 48 | Sensirion SDP810 I2C Data Line |
| **`I2C_SCL`** | GPIO 47 | Sensirion SDP810 I2C Clock Line |
| **`QUIET_DOWN_PIN`**| GPIO 21 | Interactive Button (Internal Pullup, Active LOW) |

---

## 3. Industrial Modbus Mapping Schema

The upstream Attune Gateway interfaces with system variables using 100-register blocked arrays. **Block 0** holds Server AC local operational parameters and diagnostics. **Blocks 1 through 19** represent mirrored cache tables representing Client AC 1 through Client AC 19.

### Common Block Layout (Offsets +0 to +15)

| Offset | Type | Access | Parameter Name | Scaling / Formatting |
| :--- | :--- | :--- | :--- | :--- |
| **+0** | uint16 | R/W | Target Fan % | None (0 - 100%) |
| **+1** | uint16 | R/W | Ramp Time (s) | None (Seconds) |
| **+2** | int16 | R | Differential Pressure | ×10 (Divide by 10 for raw Pa) |
| **+3** | uint16 | R | PM1.0 Mass Density | ×10 (Divide by 10 for raw µg/m³) |
| **+4** | uint16 | R | PM2.5 Mass Density | ×10 (Divide by 10 for raw µg/m³) |
| **+5** | uint16 | R | PM10 Mass Density | ×10 (Divide by 10 for raw µg/m³) |
| **+6** | uint16 | R | Particle Count (0.3–1.0 µm) | None (Particles / cm³) |
| **+7** | uint16 | R | Actual Fan Speed % | None (Normalized 0 - 100%) |
| **+8** | uint16 | R | Active Local Target % | None (Normalized 0 - 100%) |
| **+9** | uint16 | R | Status Bitfield | Binary Packed (See breakdown below) |
| **+10** | uint16 | R | Seconds Since Last Telemetry | None (Seconds elapsed) |
| **+11** | uint16 | R | Remaining Manual Override (min) | None (Counts down from 120) |
| **+12** | uint16 | R | Firmware Version Major | None |
| **+13** | uint16 | R | Firmware Version Minor | None |
| **+14
