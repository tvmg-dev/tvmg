#include <SD.h>

#include "src/core/utils.h"

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
   -1,            // TouchButton1
   -1,            // TouchButton2
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
   -1,            // TouchButton1
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
   -1,            // TouchButton1
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
   -1,            // TouchButton1 (esp32 touch 2)
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

HardwareConfig WaveshareRelay =
{
   -1,            // OneWireGPIO
   1,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   18,            // ModBusRxGPIO
   17,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   21,            // ModBus485EnGPIO
   -1,            // OLEDClkGPIO
   -1,            // OLEDDataGPIO
   -1,            // TouchButton1 (esp32 touch 2)
   -1,            // TouchButton2
   -1,            // PWM GPIO
   false          // has SD card
};

HardwareConfig ESP32S3Rs485 =
{
   10,            // OneWireGPIO
   1,             // ModBusSerial - use port 1 for real serial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   18,            // ModBusRxGPIO
   17,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   15,            // ModBus485EnGPIO
   -1,            // OLEDClkGPIO
   -1,            // OLEDDataGPIO
   -1,            // TouchButton1
   -1,            // TouchButton2
   4,             // PWM GPIO
   false          // has SD card
};

#define  TNODE_BOARD_FILE     "/tnode.hid"
#define  MASTER_BOARD_FILE    "/master.hid"
#define  EXTERNAL_BOARD_FILE  "/external.hid"
#define  MONITOR_BOARD_FILE   "/monitor.hid"
#define  WAVESHARE_LCD_FILE   "/lcd.hid"
#define  ESP32S3_FILE         "/esp32s3.hid"

HardwareConfig *hwConfig = nullptr;

void  selectHardware()
{
   PW_MSG( "Board selection..." );

#if defined(TVMG_WAVESHARE_LCDB)
   PW_MSG( "Waveshare LCD" );
   hwConfig = &WaveshareLCD;
#elif defined(TVMG_WAVESHARE_RELAY)
   PW_MSG( "Waveshare Relay" );
   hwConfig = &WaveshareRelay;
#elif defined(TVMG_ESP32S3) && defined(TVMG_RS485)
   PW_MSG( "ESP32S3 with RS485" );
   hwConfig = &ESP32S3Rs485;
#else
   // Must be an older ESP32 board - runtime detect based on file
   // in filesystem

   if ( !Config::instance() || ! tvmgFileSys )
   {
      PW_WARN( "No Config available" );
   }
   else if ( tvmgFileSys.exists( MASTER_BOARD_FILE ) )
   {
      PW_MSG( "Master Device" );
      hwConfig = &MasterDevice;
   }
   else if ( tvmgFileSys.exists( EXTERNAL_BOARD_FILE ) )
   {
      PW_MSG( "External Board" );
      hwConfig = &ExternalBoard;
   }
   else if ( tvmgFileSys.exists( MONITOR_BOARD_FILE ) )
   {
      PW_MSG( "Monitor Board" );
      hwConfig = &MonitorBoard;
   }
   else if ( tvmgFileSys.exists( TNODE_BOARD_FILE ) )
   {
      PW_MSG( "TNode" );
      hwConfig = &TemperatureNode;
   }
#endif

#ifdef TVMG_OLED
   if ( !hwConfig )
   {
      PW_MSG( "No board file - default to TNode" );
      hwConfig = &TemperatureNode;
   }
#else
   if ( !hwConfig )
   {
      PW_MSG( "No board file - default to ESP32S3" );
      hwConfig = &ESP32S3Rs485;
   }
#endif

   // If we have a modbus enable then bring it low as soon as possible.

   if ( hwConfig->ModBus485EnGPIO != -1 )
   {
      pinMode( hwConfig->ModBus485EnGPIO,OUTPUT );
      digitalWrite( hwConfig->ModBus485EnGPIO,0 );
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

// A fault has arisen such that we never entered the reboot detection logic that would
// ordinarily place the system in AP mode.  There must have been some very early
// fault that's causing continuous rebooting.
//
// So all we can do is simply stop continuously rebooting by looping indefinately

bool boardHalt()
{
#if defined(TVMG_WAVESHARE_RELAY)
   bool on = true;
   pinMode( 17,OUTPUT );
   while ( 1 )
   {
      digitalWrite( 17,HIGH );
      delay( 1000 );
      digitalWrite( 17,LOW );
      delay( 1000 );
   }
#else
   while( 1 )
   {
      delay( 5000 );
   }
#endif
}