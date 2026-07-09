#Distributed Multi-Channel PWM Air Cleaner (AC) Network
##Core Architecture: ESP32-S3 | Framework: PlatformIO (Arduino Core v2.0.14)
##Industrial Protocols: Modbus RTU (RS-485 Half-Duplex) & Routerless ESP-NOW Star Topology
#1. System Architecture & Topology
This repository contains the complete, enterprise-ready C++ firmware for an industrial distributed network of multi-channel PWM Computer-Fan Air Cleaners (ACs). The architecture utilizes a routerless wireless Star Topology to optimize data throughput and eliminate deployment reliance on local facility IT infrastructure.       
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
##Key Subsystems:
Server AC (Master): Acts as the central system bridge. It targets a remote cloud REST API over a dedicated 2.4 GHz uplink provided by a cellular Cradlepoint router. Concurrently, it exposes an upstream hardware RS-485 interface operating as a Modbus RTU Server/Slave to an Attune IoT Bridge, while driving up to 8 local PWM fan channels and compiling downstream synchronization data.Client ACs (Slaves): Dedicated hardware edge nodes executing high-speed localized differential pressure reading, PM particle counting, and individual fan tracking. They depend on bi-directional ESP-NOW transactions to synchronize with the Server's instructions.Dynamic RF Cohabitation: To eliminate 2.4 GHz radio layer packet collisions between the local ESP-NOW topology and the cellular backhaul uplink, the Server scans the environment at boot, isolates the exact operational Wi-Fi channel utilized by the Cradlepoint router, and locks the local ESP-NOW layer to that frequency.Loss-of-Connectivity Fail-Safe: If the Server fails to resolve its cloud endpoint for 10 consecutive connection strikes or 10 continuous minutes, an emergency fail-safe is asserted. The Server immediately overrides its operating capacity to 75%, sending an high-priority ESP-NOW broadcast forcing all paired Clients to transition their fan arrays to a 75% fallback threshold until connectivity restores.2. Hardware Interface & Pinout Map (Section 7.4)The firmware enforces the following precise physical GPIO mapping on the ESP32-S3:Pin MacroESP32-S3 GPIODescriptionFAN1_PWM_GENGPIO 4Fan Channel 1 PWM Output (25 kHz)FAN2_PWM_GENGPIO 5Fan Channel 2 PWM Output (25 kHz)FAN3_PWM_GENGPIO 6Fan Channel 3 PWM Output (25 kHz)FAN4_PWM_GENGPIO 7Fan Channel 4 PWM Output (25 kHz)FAN5_PWM_GENGPIO 15Fan Channel 5 PWM Output (25 kHz)FAN6_PWM_GENGPIO 16Fan Channel 6 PWM Output (25 kHz)FAN7_PWM_GENGPIO 2Fan Channel 7 PWM Output (25 kHz)FAN8_PWM_GENGPIO 1Fan Channel 8 PWM Output (25 kHz)MPX_ADDR0GPIO 1274HC4051 Multiplexer Address Bit A (LSB)MPX_ADDR1GPIO 1374HC4051 Multiplexer Address Bit BMPX_ADDR2GPIO 1474HC4051 Multiplexer Address Bit C (MSB)MPX_READGPIO 11Common Tachometer Pulse Input (Interrupt Edge)RS485_TXGPIO 17Hardware UART1 TX to MAX3485RS485_RXGPIO 18Hardware UART1 RX from MAX3485RS232_REDEGPIO 8MAX3485 Direction Line (HIGH = TX, LOW = RX)PMS5003_TXGPIO 43Plantower PMS5003 Sensor TX (ESP32 UART2 RX)PMS5003_RXGPIO 44Plantower PMS5003 Sensor RX (ESP32 UART2 TX)PMS5003_SETGPIO 9Plantower Low Power/Passive Control LinePMS5003_RSTGPIO 10Plantower Hardware Sensor ResetI2C_SDAGPIO 48Sensirion SDP810 I2C Data LineI2C_SCLGPIO 47Sensirion SDP810 I2C Clock LineQUIET_DOWN_PINGPIO 21Interactive Button (Internal Pullup, Active LOW)3. Industrial Modbus Mapping SchemaThe upstream Attune Gateway interfaces with system variables using 100-register blocked arrays. Block 0 holds Server AC local operational parameters and diagnostics. Blocks 1 through 19 represent mirrored cache tables representing Client AC 1 through Client AC 19.Common Block Layout (Offsets +0 to +15)Offset   Type     Access   Parameter Name                   Scaling/Formatting
─────────────────────────────────────────────────────────────────────────────────────────────
+0       uint16   R/W      Target Fan %                     None (0 - 100%)
+1       uint16   R/W      Ramp Time (s)                    None (Seconds)
+2       int16    R        Differential Pressure            ×10 (Divide by 10 for raw Pa)
+3       uint16   R        PM1.0 Mass Density               ×10 (Divide by 10 for raw µg/m³)
+4       uint16   R        PM2.5 Mass Density               ×10 (Divide by 10 for raw µg/m³)
+5       uint16   R        PM10 Mass Density                ×10 (Divide by 10 for raw µg/m³)
+6       uint16   R        Particle Count (0.3–1.0 µm)      None (Particles / cm³)
+7       uint16   R        Actual Fan Speed %               None (Normalized 0 - 100%)
+8       uint16   R        Active Local Target %            None (Normalized 0 - 100%)
+9       uint16   R        Status Bitfield                  Binary Packed (See breakdown)
+10      uint16   R        Seconds Since Last Telemetry     None (Seconds elapsed)
+11      uint16   R        Remaining Manual Override (min)  None (Counts down from 120)
+12      uint16   R        Firmware Version Major           None
+13      uint16   R        Firmware Version Minor           None
+14      uint16   R        Hardware Revision                None
+15      uint16   R        Protocol Version                 None
Status Bitfield Mappings (Offset +9)┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
│15 │14 │13 │12 │11 │10 │ 9 │ 8 │ 7 │ 6 │ 5 │ 4 │ 3 │ 2 │ 1 │ 0 │
└───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
                                          │   │   │   │   │   │
                                          │   │   │   │   │   └── Fail-Safe Active
                                          │   │   │   │   └────── Pressure Sensor Fault
                                          │   │   │   └────────── Particle Sensor Fault
                                          │   │   └────────────── Node Offline Indicator
                                          │   └────────────────── Fan Stall Alarm
                                          └────────────────────── Manual Control Active
Block 0 Diagnostic Extensions (Server Only)Offset   Type     Access   Parameter Name                   Scaling/Formatting
─────────────────────────────────────────────────────────────────────────────────────────────
+20      uint16   R/W      API Poll Interval (s)            None (Default 60s)
+21      uint16   R        Zone ID                          None
+22      uint16   R        Modbus Slave ID                  None (Default 1)
+23      uint16   R        Network ID                       None
+24      uint16   R        Authentication Failure Count     Counter
+25      uint16   R        Connectivity Failure Count       Counter
+26      uint16   R        Consecutive Poll Failures        Counter
+27      uint16   R        Consecutive Successful Polls     Counter
+28      uint16   R        OTA Status                       Enum / Flags
+29      uint16   R        Event Log Count                  Counter
4. Operation & Interface Directives4.1 USB-C Plaintext CLI (Server Only)Connect to the Server AC's native USB-C port via a standard terminal emulator (baud rate 115200). The interface supports the following syntax strings for runtime modification of the NVS whitelist:list: Outputs all currently whitelisted Client MAC addresses and indices.add XX:XX:XX:XX:XX:XX: Adds a target Client's MAC address directly into NVS.remove XX:XX:XX:XX:XX:XX: Deletes a target client from the persistent database structure.4.2 Local Hardware Button Timing States (QUIET_DOWN_PIN)The interactive button handler filters user input based on strict duration and iteration thresholds:Short Press (50ms to 1000ms): Suspends automatic operation and shifts the local device into Local Manual Override Mode. Each consecutive short press increments the local target fan speed by a fixed 25% block step, cycling indefinitely:$$\text{Auto Mode} \rightarrow 0\% \rightarrow 25\% \rightarrow 50\% \rightarrow 75\% \rightarrow 100\% \rightarrow 0\%$$An automated 120-minute countdown timer executes upon override entry; when it hits zero, the node automatically clears manual mode and returns to network synchronization.Quintuple Press (5 clicks within 10 seconds): Forces an immediate clearing of the local NVS memory table and resets the hardware.5. Firmware Directory TreePlaintextfirmware-root/
├── include/
│   ├── CommonDefs.h              # Pin definitions, versions, and role definitions
│   ├── DataModels.h              # Struct packaging, message unions, and bitfields
│   └── Config.h                  # NVS namespaces and calibration configurations
├── src/
│   ├── main.cpp                  # Main entry point and FreeRTOS orchestration engine
│   ├── drivers/
│   │   ├── FanPWMController.h    # 25 kHz PWM driver, linear ramping, and kickstarts
│   │   ├── FanPWMController.cpp
│   │   ├── TachMultiplexer.h     # 74HC4051 capture task and RPM normalizer
│   │   ├── TachMultiplexer.cpp
│   │   ├── SDP810_Pressure.h     # I2C driver for Sensirion differential pressure
│   │   ├── SDP810_Pressure.cpp
│   │   ├── PMS5003_Particle.h    # UART parsing driver for Plantower PM sensor
│   │   └── PMS5003_Particle.cpp
│   ├── storage/
│   │   ├── StorageManager.h      # NVS operational wrapper for client whitelists
│   │   └── StorageManager.cpp
│   ├── network/
│   │   ├── ModbusServer.h        # RS-485 memory-map abstraction interface
│   │   ├── ModbusServer.cpp
│   │   ├── EspNowEngine.h        # Wi-Fi alignment and Star topology coordinator
│   │   ├── EspNowEngine.cpp
│   │   ├── CloudSync.h           # REST Client engine and fail-safe state machines
│   │   └── CloudSync.cpp
│   └── ui/
│       ├── ButtonInterface.h     # Debounced UI tracker and multi-click decoder
│       └── ButtonInterface.cpp
└── platformio.ini                # Repeatable PlatformIO build compilation parameters
6. Build & Deployment WorkflowPrerequisitesInstall Visual Studio Code and the official PlatformIO IDE Extension.Connect the ESP32-S3 hardware via its native USB-C connector.Target Environment SelectionOpen your terminal inside the root project directory and execute compilation using the environment targets declared inside platformio.ini:Compilation and Deployment for the Server (Master Nodes):Bash# Compile the Server firmware image
pio run -e esp32s3_master

# Flash the image and launch the terminal monitor interface
pio run -t upload -t monitor -e esp32s3_master
Compilation and Deployment for the Client (Slave Nodes):Bash# Compile the Client firmware image
pio run -e esp32s3_slave

# Flash the image and launch the terminal monitor interface
pio run -t upload -t monitor -e esp32s3_slave
Exception DecodingIf a core fault triggers a panic state, the integrated esp32_exception_decoder configuration flag within platformio.ini automatically parses the resulting serial backtrace to output explicit C++ class file names and source code line numbers.
