#include <SD.h>

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
   -1,            // PWM GPIO
   true           // has SD card
};

// T-Nodes use the integrated ESP32/OLED module
// The don't support modbus transceiver module but may use modbus via
// a modbusTCP adapter, set in config.dat & sensors file

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
   T2,            // TouchButton1
   -1,            // TouchButton2
   25,            // PWM GPIO
   false          // has SD card
};

// External board is a Temperature Bord but with different
// address for One Wire, and no PWM support

HardwareConfig ExternalBoard =
{
   13,            // OneWireGPIO
   -1,            // ModBusSerial
   -1,            // ModBusBaudRate
   -1,            // ModBusSerialFormat
   -1,            // ModBusRxGPIO
   -1,            // ModBusTxGPIO
   -1,            // ModBusMsgDelay
   -1,            // ModBus485EnGPIO
   4,             // OLEDClkGPIO
   5,             // OLEDDataGPIO
   T2,            // TouchButton1
   -1,            // TouchButton2
   -1,            // PWM GPIO
   false          // has SD card
};


// Monitor uses the integrated ESP32/OLED module and also have modbus
// transceiver module available and PWM support.

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
   4,             // OLEDClkGPIO
   5,             // OLEDDataGPIO
   T2,            // TouchButton1 (esp32 touch 2)
   -1,            // TouchButton2
   16,            // PWM GPIO
   false          // has SD card
};

// The waveshare 43B LCD panel.  Uses hardware serial for modbus.  Only
// opto isoloated digital IO so no 1-wire support or PWM.  Touch will be
// via lvgl graphics buttons.

HardwareConfig WaveshareLCD =
{
   -1,            // OneWireGPIO
   1,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   43,            // ModBusRxGPIO
   44,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   -1,            // ModBus485EnGPIO
   -1,            // OLEDClkGPIO
   -1,            // OLEDDataGPIO
   -1,            // TouchButton1 (esp32 touch 2)
   -1,            // TouchButton2
   -1,            // PWM GPIO
   false          // has SD card
};

#define  TNODE_BOARD_FILE     "/tnode.hid"
#define  MASTER_BOARD_FILE    "/master.hid"
#define  EXTERNAL_BOARD_FILE  "/external.hid"
#define  MONITOR_BOARD_FILE   "/monitor.hid"
#define  WAVESHARE_LCD_FILE   "/lcd.hid"

HardwareConfig *hwConfig = nullptr;

void  selectHardware()
{
   fs::SPIFFSFS   *spiffs = nullptr;

   PW_MSG( "Board selection..." );
   if ( !Config::instance() )
   {
      PW_WARN( "No Config available" );

   }
   else if ( ! (spiffs = Config::instance()->getSPIFFS() ) )
   {
      PW_WARN( "No SPIFFS" );
   }
   else
   {
      spiffs = Config::instance()->getSPIFFS();

      if ( spiffs->exists( WAVESHARE_LCD_FILE ) )
      {
         PW_MSG( "Waveshare LCD" );
         hwConfig = &WaveshareLCD;
      }
      else if ( spiffs->exists( MASTER_BOARD_FILE ) )
      {
         PW_MSG( "Master Device" );
         hwConfig = &MasterDevice;
      }
      else if ( spiffs->exists( EXTERNAL_BOARD_FILE ) )
      {
         PW_MSG( "External Board" );
         hwConfig = &ExternalBoard;
      }
      else if ( spiffs->exists( MONITOR_BOARD_FILE ) )
      {
         PW_MSG( "Monitor Board" );
         hwConfig = &MonitorBoard;
      }
      if ( spiffs->exists( TNODE_BOARD_FILE ) )
      {
         PW_MSG( "TNode" );
         hwConfig = &TemperatureNode;
      }
   }

   if ( !hwConfig )
   {
      PW_MSG( "No board file - default to TNode" );
      hwConfig = &TemperatureNode;
   }
}

bool  boardHasSDCard()
{
   if ( hwConfig )
   {
      return hwConfig->hasSDCard;
   }

   return( false );
}
