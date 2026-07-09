Code for control and networking of air cleaners

include/ contains your register maps and configuration keys.

src/drivers/ abstracts the 25kHz PWM constraints, 74HC4051 sequential reads, and I2C/UART sensors.

src/network/ implements radio cohabitation, star topology syncs, and the half-duplex RS-485 map.

src/ui/ & storage/ dictate human intervention windows and non-volatile state recall.

src/main.cpp binds the system logic securely on FreeRTOS tasks.

firmware-root/

├── include/
│   ├── CommonDefs.h              # Shared macros, pinouts (Section 7.4), and firmware versions
│   ├── DataModels.h              # Modbus maps, bitfield structures, and ESP-NOW network payloads
│   └── Config.h                  # NVS keys, default timing windows, and calibration constants

├── src/
│   ├── main.cpp                  # Application entry point, FreeRTOS task coordination, and boot routing

│   ├── drivers/
│   │   ├── FanPWMController.h    # 25 kHz PWM generator, kick-start logic, and linear ramping implementation
│   │   ├── FanPWMController.cpp
│   │   ├── TachMultiplexer.h     # 74HC4051 sequential scanning, ISR handle, and RPM normalization
│   │   ├── TachMultiplexer.cpp
│   │   ├── SDP810_Pressure.h     # I2C driver for Sensirion differential pressure sensor
│   │   ├── SDP810_Pressure.cpp
│   │   ├── PMS5003_Particle.h    # UART driver for Plantower PM sensor parsing
│   │   └── PMS5003_Particle.cpp

│   ├── storage/
│   │   ├── StorageManager.h      # NVS wrapper for MAC provisioning arrays, runtime counters, and targets
│   │   └── StorageManager.cpp

│   ├── network/
│   │   ├── ModbusServer.h        # Half-duplex RS-485 Modbus RTU register mapping engine
│   │   ├── ModbusServer.cpp
│   │   ├── EspNowEngine.h        # Wi-Fi cohabitation alignment and peer-to-peer Star topology handling
│   │   ├── EspNowEngine.cpp
│   │   ├── CloudSync.h           # JSON REST API polling client, timeout counters, and fail-safe triggers
│   │   └── CloudSync.cpp

│   └── ui/
│       ├── ButtonInterface.h     # Debounced manual override state machine and pairing routine decoder
│       └── ButtonInterface.cpp

└── platformio.ini                # Build configurations, compiler flags, and framework dependency declarations
