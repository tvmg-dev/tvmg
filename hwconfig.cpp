#include "utils.h"
#include "Config.h"

#include "hwconfig.h"

// For modbus, what are the transmit & receive pins - using h/w serial #2
// The RS485 - TTL module has its TX connected to ESP's RX, and obviously
// its RX to ESP's TX.  PZEM-16 takes ~ 30 ms to act on receipt of the
// register read request.  And we introduce a delay between modbus requests.
// Also require an enable/disable for the MAX3485 485 converter.
//
// We appear to need a 10k (?) pullup too on the RX GPIO.  Tried the
// internal pull up to no avail.

HardwareConfig MasterDevice =
{
   27,            // OneWireGPIO
   2,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   16,            // ModBusRxGPIO
   17,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   4,             // ModBus485EnGPIO
   32,            // OLEDClkGPIO
   33,            // OLEDDataGPIO
   T6,            // TouchButton1
   T5,            // TouchButton2
   -1             // PWM GPIO
};

HardwareConfig TemperatureNode =
{
   26,            // OneWireGPIO
   -1,            // ModBusSerial
   -1,            // ModBusBaudRate
   -1,            // ModBusSerialFormat
   -1,            // ModBusRxGPIO
   -1,            // ModBusTxGPIO
   -1,            // ModBusMsgDelay
   -1,            // ModBus485EnGPIO
   4,             // OLEDClkGPIO
   5,             // OLEDDataGPIO
   -1,            // TouchButton1
   -1,            // TouchButton2
   25             // PWM GPIO
};

HardwareConfig MonitorBoard =
{
   13,            // OneWireGPIO
   0,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   3,             // ModBusRxGPIO
   1,             // ModBusTxGPIO
   50,            // ModBusMsgDelay
   12,            // ModBus485EnGPIO
   4,            // OLEDClkGPIO
   5,            // OLEDDataGPIO
   T2,           // TouchButton1 (esp32 touch 2)
   -1,           // TouchButton2
   16            // PWM GPIO
};

HardwareConfig *hwConfig;

void  selectHardware()
{
   int boardType = GET_REGISTRY_INT( BOARD_TYPE );

   if ( boardType == MASTER_BOARD )
   {
      PW_MSG( "Master Device Detected" );
      hwConfig = &MasterDevice;
   }
   else if ( boardType == TEMPERATURE_BOARD )
   {
      PW_MSG( "Temperature Module Detected" );
      hwConfig = &TemperatureNode;
   }
   else
   {
      PW_MSG( "Monitor Board Detected" );
      hwConfig = &MonitorBoard;
   }
}
