//This header defines physical pinouts, firmware/hardware versions, 
//system-wide constant limits, and the global enumeration of device roles.
#pragma once

#include <Arduino.h>

// =============================================================================
// Section 7.4: System Pinout Definition
// =============================================================================

// On-Board Multi-Channel Fan PWM Output Pins
#define FAN1_PWM_GEN      4
#define FAN2_PWM_GEN      5
#define FAN3_PWM_GEN      6
#define FAN4_PWM_GEN      7
#define FAN5_PWM_GEN      15
#define FAN6_PWM_GEN      16
#define FAN7_PWM_GEN      2
#define FAN8_PWM_GEN      1

// Tachometer 74HC4051 Multiplexer Control Interface
#define MPX_ADDR0         12   // MUX Selection Bit A (LSB)
#define MPX_ADDR1         13   // MUX Selection Bit B
#define MPX_ADDR2         14   // MUX Selection Bit C (MSB)
#define MPX_READ          11   // Common Tachometer Pulse Input (Interrupt Pin)

// Upstream Industrial Modbus RTU RS-485 (via MAX3485)
#define RS485_TX          17   // Hardware UART1 TX
#define RS485_RX          18   // Hardware UART1 RX
#define RS232_REDE        8    // Direction Control Pin (HIGH=TX, LOW=RX)

// Plantower PMS5003 Particle Sensor
#define PMS5003_TX        43   // Connected to ESP32 Hardware UART2 RX
#define PMS5003_RX        44   // Connected to ESP32 Hardware UART2 TX
#define PMS5003_SET       9    // Control line for low power mode
#define PMS5003_RST       10   // Hardware sensor reset line

// Sensirion SDP810-500PA Differential Pressure Sensor (I2C)
#define I2C_SDA           48
#define I2C_SCL           47
#define SDP810_I2C_ADDR   0x25 // Primary I2C target address

// User Input Interface
#define QUIET_DOWN_PIN    21   // Push-button input (Internal Pullup, Active LOW)

// =============================================================================
// Versioning & Protocol Metadata
// =============================================================================
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define HW_REVISION       1
#define PROTOCOL_VERSION  1

// =============================================================================
// Operational Parameters
// =============================================================================
#define NUM_FANS          8
#define MAX_CLIENTS       19
#define MODBUS_BLOCK_SZ   100

enum class DeviceRole : uint8_t {
    SERVER_MASTER = 0,
    CLIENT_SLAVE  = 1
};
