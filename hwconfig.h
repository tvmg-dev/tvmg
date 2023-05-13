#ifndef HWCONFIG_H
#define HWCONFIG_H

// For temperature monitoring, what GPIO are we on ?

#define  ONE_WIRE_GPIO 27

// For modbus, what are the transmit & receive pins - using h/w serial #2
// The RS485 - TTL module has its TX connected to ESP's RX, and obviously
// its RX to ESP's TX.  PZEM-16 takes ~ 30 ms to act on receipt of the
// register read request.  And we introduce a delay between modbus requests.
// Also require an enable/disable for the MAX3485 485 converter.
//
// We appear to need a 4.7k (?) pullup too on the RX GPIO.  Tried the
// internal pull up to no avail.

#define MODBUS_SERIAL         2
#define MODBUS_BAUD_RATE      9600
#define MODBUS_SERIAL_FORMAT  SERIAL_8N1
#define MODBUS_RX_GPIO        16
#define MODBUS_TX_GPIO        17
#define MODBUS_MSG_DELAY      50
#define MODBUS_485EN_GPIO     4

// OLED display, needs I2C clk & data

#define OLED_I2C_CLK_GPIO     32
#define OLED_I2C_DATA_GPIO    33

// Touch

#define  TOUCH_BUTTON_1       T2
#define  TOUCH_BUTTON_2       T3

#endif
