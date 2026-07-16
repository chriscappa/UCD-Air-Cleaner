# Distributed Air Cleaner (AC) Mesh Network

This repository contains the production firmware for a self-forming, self-healing mesh network of PWM-controlled Fan Air Cleaners. 

The system leverages **ESP32-S3** microcontrollers running **painlessMesh** to dynamically route sensor telemetry and fan targets between nodes. The Server (Master) node acts as an internet gateway, downloading control schedules from a cloud API via a Cradlepoint router and distributing them downstream through the mesh using JSON messaging.

```
   [ Cradlepoint Router ]
            ▲
     (Wi-Fi Network)
            │
    [ Server AC Node ] <===================> [ PC Console / CLI ]
     (Mesh Root/Gateway)
            ▲
     (painlessMesh JSON)
      ┌─────┴─────┐
      ▼           ▼
[ Client 1 ] [ Client 2 ] <--- (Self-Heals / Relays automatically)

```

```

## System Features

* **True Mesh Topology (`painlessMesh`):** Nodes automatically find each other and dynamically route messages. If a node goes offline, the network self-heals and reroutes telemetry automatically.
* **Smart Control Arbitration:** Prioritizes physical manual buttons and local USB overrides over scheduled cloud targets.
* **Continuous Safety Checks:** Monitors fan tachometer feedback. If the actual speed stays 10% below the target for over 5 seconds, a hardware stall alarm is triggered.
* **JSON Serialization:** Uses `ArduinoJson` to cleanly broadcast structured sensor metrics (particle count, differential pressure) and downstream fan speeds.

---

## Hardware Architecture

* **MCU:** ESP32-S3 (DevKitC-1 configuration)
* **Particulate Sensor:** PMS5003 (UART)
* **Differential Pressure Sensor:** SDP810 (I2C)
* **Actuator:** PWM-controlled fan with tachometer feedback and hardware multiplexer

---

## Firmware Directory Structure

```text
├── src/
│   ├── main.cpp                 # Central orchestrator loop and state machine
│   ├── CommonDefs.h             # Shared system constants and structures
│   ├── DataModels.h             # Telemetry packet definitions
│   ├── network/
│   │   ├── MeshEngine.h         # Header-only painlessMesh wrapper (SSID, port, events)
│   │   ├── ModbusServer.h       # Modbus mapping for local server registers
│   │   └── CloudSync.h          # Handles Cradlepoint connection and Cloud API fetches
│   ├── drivers/
│   │   ├── FanPWMController.h   # Generates PWM fan speed steps with linear ramping
│   │   ├── TachMultiplexer.h    # Decodes fan RPM signals 
│   │   ├── SDP810_Pressure.h    # Differential pressure I2C driver
│   │   └── PMS5003_Particle.h   # Particulate matter UART driver
│   ├── storage/
│   │   └── StorageManager.h     # Controls non-volatile flash storage configurations
│   └── ui/
│       └── ButtonInterface.h    # Manages manual buttons and override timers
├── platformio.ini               # Build environments, CPU flags, and dependencies
└── README.md                    # This instruction manual

```

---

## Getting Started

### 1. Prerequisites

1. Download and install [VS Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension from the VS Code Extensions marketplace.

### 2. Network Configurations

Open `src/main.cpp` and update your Cradlepoint connection details near the top of the file:

```cpp
const char* ROUTER_SSID  = "YOUR_CRADLEPOINT_SSID";
const char* ROUTER_PASS  = "YOUR_CRADLEPOINT_PASSWORD";
const char* API_ENDPOINT = "[https://api.attune-iot.com/v1/ac/targets](https://api.attune-iot.com/v1/ac/targets)";

```

*The internal mesh network runs on its own dedicated credentials (`AC_MESH_NET` / `MeshSecurePassword123`) automatically.*

---

## Compiling & Installation

We use PlatformIO's build environments to compile either the **Master** (Server) firmware or the **Slave** (Client) firmware from the exact same codebase.

1. Connect your ESP32-S3 to your computer via USB-C.
2. Click the **PlatformIO (Ant Head) Icon** on the left sidebar.
3. Choose your target environment:

### For the Server (Master) AC:

* Expand `esp32s3_master`
* Click **General** -> **Upload**

### For the Client (Slave) ACs:

* Expand `esp32s3_slave`
* Click **General** -> **Upload**

---

## Local Verification & Testing

You can test the entire mesh network step-by-step using your computer and the built-in USB Command Line Interface (CLI).

### 1. Check Local Hardware (Server or Client)

Plug a node into your PC and open the PlatformIO Serial Monitor (plug icon on the bottom toolbar). Press the physical button once:

* You should see: `Local Manual Override Asserted. Speed Shifted to: 25%`
* The fan will ramp up. If the fan is disconnected or fails to spin, the console will print a stall alarm within 5 seconds.

### 2. Monitor Mesh Connections

Open the Serial Monitor on the Server AC. As Client ACs are powered on, the mesh will automatically link them. The Server console will output:

```text
New node joined the mesh! Assigned ID: 2390845112

```

### 3. CSV Data Logging (No Cloud Required)

To inspect raw performance data without connecting to Attune, the Server AC periodically dumps a clean, comma-separated stream to the Serial Monitor:

```text
--- LOCAL SYSTEM LOG (CSV) ---
Node,TargetSpeed,ActualSpeed,Pressure,PM2_5,StallAlarm
0 (Server),50,50,N/A,N/A,0
1 (Client),50,48,1.2,12.5,0
2 (Client),50,0,0.0,8.1,1
------------------------------

```

You can copy and paste this output directly into Excel or Google Sheets to analyze your network's physical performance.

```

```
