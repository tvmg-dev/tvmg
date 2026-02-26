#include <SD.h>
#include <FS.h>
#include <ModbusMaster.h>

#include <time.h>

#include "src/core/utils.h"
#include "src/core/Measurement.h"
#include "src/core/Storage.h"

#include "src/config/Config.h"
#include "src/config/hwconfig.h"

#include "src/sensors/TemperatureModule.h"
#include "src/sensors/PowerModule.h"
#include "src/sensors/HeatMeter.h"
#include "src/sensors/ShellyPM.h"
#include "src/sensors/LGHeatPump.h"
#include "src/sensors/LGHeatPumpSim.h"

#include "src/userio/DummyDisplay.h"
#include "src/userio/OledDisplay.h"
#include "src/userio/LCDDisplay.h"
#include "src/userio/Indicator.h"

#include "src/network/Networking.h"
#include "src/network/WebServer.h"

#include "src/network/ModbusTCP.h"

// ---------------------------------------------------------------------

HardwareSerial    *hwSerial = nullptr;
ModbusMaster      *modbusMaster = nullptr;
TemperatureModule *tempModule = nullptr;
PowerModule       *powerModule = nullptr;
ShellyPowerModule *shellyPowerModule = nullptr;
ModbusTCP         *modbusTCP = nullptr;
Storage           *storageModule = nullptr;
HeatMeterModule   *heatMeterModule = nullptr;
Display           *display = nullptr;
Measurement       *measurement = nullptr;
Config            *config = nullptr;
Networking        *networking = nullptr;
LGHeatPump        *lgThermaV = nullptr;
LGHeatPumpSimulator  *lgSimulator = nullptr;
Indicator         *systemIndicator = nullptr;

// Amount of time we can have the network mutex held before the loop()
// can proceed.  If this is exceeded then will reboot.

#define  NETWORK_ALLOWED_BUSY_MS (90 * 1000)

// Amount of time we tolerate no WiFi connection before rebooting

#define  NETWORK_ALLOWED_DISCONNECTED_MS (90 * 1000)

// We'll malloc into this buffer for heap size debugging

char  *testMallocBuffer = nullptr;

// For testing fast reboot handling

bool  testFastReboot = false;

// To get modbus stats :(

void  getModbusStats( uint32_t *requests,uint32_t *fails )
{
   if ( requests && fails )
   {
      if ( modbusMaster )
      {
         modbusMaster->getTransactionCounts( requests,fails );
      }
      else
      {
         *requests = 0;
         *fails = 0;
      }
   }
}

// Networking state callback function
 void networkingInfoCallback( Networking::NetworkingInfo info,const String &str )
{
   char line[ MAX_DISPLAY_COLUMNS + 1 ];

   PW_DEBUG( "NWC: %d : %s", info,str.c_str() );

   if ( !display )
   {
      return;
   }

   switch( info )
   {
      case Networking::ACQUIRING_NTP:
         display->updateLine( 3,"Acquire NTP" );
         break;
      case Networking::OTA_FAILED:
         display->clear();
         display->updateLine( 1,"Updating :" );
         display->updateLine( 3,"FAILED !" );
         delay( 2000 );
         display->show( Display::NETWORK_STATUS );
         break;
      case Networking::OTA_STARTED:
         display->show( Display::OTA_UPDATE );
         display->clear();
         display->updateLine( 1,"Updating :" );
         snprintf( line,MAX_DISPLAY_COLUMNS," %s",str.c_str() );
         display->updateLine( 2,line );
         break;
      case Networking::OTA_PROGRESS:
         {
            static int progress = -1;
            int newProgress = str.toInt();
            if ( newProgress != progress  )
            {
               snprintf( line,MAX_DISPLAY_COLUMNS,"%s %%",str.c_str() );
               display->updateLine( 5,line,false );
               progress = newProgress;
            }
         }  
         break;
      case Networking::OTA_COMPLETE:
         display->updateLine( 5,"Completed" );
         delay( 2000 );
         display->show( Display::NETWORK_STATUS );
         break;
   }
}

// --------------------------------------------------------------------------
// Reboot handling code, if we have no network then we consider WiFi has
// failed and drop to AP mode which will remain active until reboot.

#define MAX_FAILED_WIFI_ATTEMPTS 3

void newConfiguration( void )
{
   Indicator *newConfigIndicator = Indicator::getIndicator( Indicator::SYSTEM,SYSTEM_AP_ID );
   Indicator::Scoped guard( newConfigIndicator );

   networking = new Networking( networkingInfoCallback );
   networking->startAccessPoint();

   PW_WARN( "Need to configure via SSID : %s",networking->getSSID().c_str() );
   PW_WARN( "Use %s/manager",networking->getMDNSName().c_str() );
   PW_WARN( "Or %s/manager",networking->getIPAddress().c_str() );

   if ( display )
   {
      char line[ MAX_DISPLAY_COLUMNS ];

      display->clear();

      if ( !config->isFactoryReset() )
      {
         if ( config->wasFastReboot() )
         {
            snprintf( line,MAX_DISPLAY_COLUMNS,"Fast Reboot Error" );
         }
         else
         {
            int32_t  numNoWifi;

            config->getPersistentInt( k_noNetworkCounter,&numNoWifi,0 );
            snprintf( line,MAX_DISPLAY_COLUMNS,"No WiFi count %d",numNoWifi );
         }
      }
      else
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"From Reset..." );
      }

      display->updateLine( 0,line );

      snprintf( line,MAX_DISPLAY_COLUMNS,"SSID %s",networking->getSSID().c_str() );
      display->updateLine( 2,line );
      snprintf( line,MAX_DISPLAY_COLUMNS,"Use %s",networking->getMDNSName().c_str() );
      display->updateLine( 3,line );
      snprintf( line,MAX_DISPLAY_COLUMNS,"Use %s",networking->getIPAddress().c_str() );
      display->updateLine( 4,line );
   }

   // Clear the no WiFi counter, so we can try and reboot again if possible

   config->setPersistentInt( k_noNetworkCounter,0 );

   while( 1 )
   {
      delay( 60 * 1000 );
      PW_DEBUG( "Waiting for configuration..." );
   }
}

// ---------------------------------------------------------------------
// Handle button presses
// button 1 is for debug emails, button 2 is unused

int  touchThreshold = 32;
bool wasButton1Pressed = false;
bool wasButton2Pressed = false;

int  touch1Value = 0;

void IRAM_ATTR gotTouch1Event()
{
  wasButton1Pressed = true;
  touch1Value = touchRead( hwConfig->TouchButton1 );
}

void IRAM_ATTR gotTouch2Event()
{
  wasButton2Pressed = true;
}

void  handleTouch1()
{
   PW_MSG( "Button-1 was pressed" );

   PW_DEBUG( "Touch 1 value %d",touch1Value );

   wasButton1Pressed = false;

   display->clear();
   display->updateLine( 1, "BT-1 pressed" );

   String   msgString;
   const Measurement::Sample sample = measurement->getLastSample();
   char     message[ 128 ];

   snprintf( message,sizeof(message),"Button samplen\n"
                    "IP : %s [%s]\n"
                    "Free Bytes : %u\n"
                    "Time signature %u\n\n",
                    networking->getLocalMDNSName().c_str(),
                    networking->getIPAddress().c_str(),
                    ESP.getFreeHeap(),
                    sample.m_sampleTime );

   msgString = message;

   for ( int i = 0; i < sample.m_tempSensors.size(); i++ )
   {
      const TempSensor &sensor = sample.m_tempSensors[ i ];

      snprintf( message,sizeof(message),"%30s,%.1f\n",getSensorName( THERM,sensor.m_id ).c_str(),sensor.m_temp );
      msgString += message;
   }

   int i = 0;
   for ( int i = 0; i < sample.m_powerSensors.size(); i++ )
   {
      const PowerSensor &sensor = sample.m_powerSensors[ i ];

      snprintf( message,sizeof(message),"%30s,%.1f\n",getSensorName( POWER,sensor.m_id ).c_str(),sensor.m_power,sensor.m_energy );
      msgString += message;
   }

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Btn Press",msgString );

   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Current Data","Sample Data",storageModule->getCurrentFileName(),true );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug Log","Debug log",DEBUG_LOG,true );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"HP Modbus","Modbus Data",LGMODBUS_LOG,true );
   networking->sendEmailWithFileAsBody( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"LG Event Log",LGSTATUS_LOG_HTML );
}

void  handleTouch2()
{
   PW_MSG( "Button-2 was pressed" );
}

void  processButtons()
{
   // process button presses

   if ( wasButton1Pressed )
   {
      START_TIMING( "Handle Touch1" );
      handleTouch1();
      wasButton2Pressed = false;
      END_TIMING;
   }

   if ( wasButton2Pressed )
   {
      START_TIMING( "Handle Touch2" );
      handleTouch2();
      wasButton1Pressed = false;
      END_TIMING;
   }
}

// ---------------------------------------------------------------------
// Configure modbus

void modbusPreTransmission()
{
   digitalWrite( hwConfig->ModBus485EnGPIO,1 );
}

void modbusPostTransmission()
{
   digitalWrite( hwConfig->ModBus485EnGPIO,0 );
}

void  configureModBus()
{
   PW_DEBUG( "Checking for MODBUSTCP" );

   // modbus has a few types here - and the selected type depends on the following
   // order preference (determined by configured sensors
   //
   //    1. Modbus TCP to act as modbus master
   //    2. RS485 transceiver to act as a slave to emulate devices
   //    3. RS485 transceiver to act as modbus master

   if ( isSensorRequired( MODBUSTCP_SENSOR_NAME ) > 0 )
   {
      modbusTCP = new ModbusTCP();
      modbusTCP->initialise();

      if ( !modbusTCP->isOk() )
      {
         PW_WARN( "ModbusTCP is NOK !" );
         modbusTCP = nullptr;
      }

      modbusMaster = modbusTCP;
   }
   else
   {
      // May need a modbus instance
      // If we have configure a modbus UART then if that UART is 0 then
      // also need to confirm that it is not being used for serial debug

      bool  isModBusAvailable = false;
      if ( hwConfig->ModBusSerial == 0 )
      {
         if ( !isBootSerialEnabled )
         {
            isModBusAvailable = true;
         }
      }
      else if ( hwConfig->ModBusSerial != -1 )
      {
         isModBusAvailable = true;
      }

      if ( !isModBusAvailable )
      {
         PW_DEBUG( "No modbus available" );
      }
      else
      {
         PW_MSG( "Creating new modbus with h/w serial" );
         hwSerial = new HardwareSerial( hwConfig->ModBusSerial );

         PW_MSG( "Starting serial port %u",hwConfig->ModBusSerial );
         PW_DEBUG( "   Baudrate %u, Rx pin [%u], Tx pin [%u]",hwConfig->ModBusBaudRate,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );

         hwSerial->begin( hwConfig->ModBusBaudRate,hwConfig->ModBusSerialFormat,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );

         // Are we simulating the LG, i.e. acting as a slave, if so we can't
         // be a master.

         if ( isSensorRequired( LGHEATPUMPSIM_SENSOR_NAME ) )
         {
            PW_MSG( "LG Sim required, so modbus master not allowed" );
         }
         else
         {
            PW_MSG( "Creating modbus Master" );
            modbusMaster = new ModbusMaster;
            // If enabled setup the MAX3485 device, need to set the device enable high for transmit to slaves
            // and low for receive.  The ModbusMaster has callbacks to facilitate that.

            if ( hwConfig->ModBus485EnGPIO != -1 )
            {
               pinMode( hwConfig->ModBus485EnGPIO,OUTPUT );
               modbusMaster->preTransmission( modbusPreTransmission );
               modbusMaster->postTransmission( modbusPostTransmission );

               // Pull the enable low to set to listening mode

               modbusPostTransmission();
            }

            modbusMaster->begin( 1, *hwSerial );
         }
      }
   }
}

// ---------------------------------------------------------------------
// Setup serial port

void setupSerial()
{
#ifdef TVMG_ESP32
   // The monitor board has a switch to disable serial as the port is used for
   // modbus.  This SERIAL_DISABLE_GPIO is the MTDO strapping pin of the ESP32
   // wroom device, which determines whether serial output is enabled on boot.
   // If boot serial isn't enabled then we configure alternate pins to ensure
   // if anything attempts to use serial then it won't affect modbus for the
   // monitor board.

   pinMode( SERIAL_DISABLE_GPIO,INPUT_PULLUP );
   int val = digitalRead( SERIAL_DISABLE_GPIO );

   bool setPinsOk = true;
   if ( !val )
   {
      isBootSerialEnabled = false;
      setPinsOk = Serial.setPins( ALTERNATE_UART0_RX_GPIO,ALTERNATE_UART0_TX_GPIO );
   }

//   Serial.setDebugOutput(true); // from chip-debug-report.cpp

   Serial.begin( 115200,SERIAL_8N1 );

   delay( 500 );

   PW_DEBUG( "pins Ok %d serial enable %d",setPinsOk,isBootSerialEnabled );

#else    // ESP32S3's
#if defined(TVMG_WAVESHARE_LCDB) || defined(TVMG_WAVESHARE_RELAY)
   #if ARDUINO_USB_CDC_ON_BOOT == 0
      #error "Should have CDC enabled"
   #endif

   uint32_t serialStart = millis();

   Serial.begin( 115200 );

   while( !Serial && (millis() - serialStart < 500) );
#elif defined(TVMG_ESP32S3) && defined(TVMG_RS485)

#if ARDUINO_USB_CDC_ON_BOOT == 1
   #error "Should have CDC disabled"
#endif
   Serial.begin( 115200 );
   delay( 500 );

#else
   #error "Unknown serial setup"
#endif

#endif
}

// ---------------------------------------------------------------------
// Display the reboot reason and bump the reboot counter.  If we've had
// too many fast reboots then we'll drop into new configuration mode as
// that shouldn't happen.
//
// Fast reboot is considered a runtime less than 10 minutes, and if we
// have 10 such reboots then that is criteria for AP mode.

String handleBootReason()
{
   char line[ MAX_DISPLAY_COLUMNS ];

   // Bump the reboot count

   int32_t  rebootCount;
   (void) config->getPersistentInt( k_rebootCounter,&rebootCount,0 );
   rebootCount++;
   config->setPersistentInt( k_rebootCounter,rebootCount );

   // Get the reboot reason

   RebootType rebootReason;
   String     rebootStr = config->getRebootReason( &rebootReason );

   // Brief display of reboot reason

   display->updateLine( 0,"Reboot Reason" );
   snprintf( line,MAX_DISPLAY_COLUMNS,"Code : %d",rebootReason );
   display->updateLine( 1,line );
   snprintf( line,MAX_DISPLAY_COLUMNS,"%s",config->getAppRebootReason( rebootReason ).c_str() );
   display->updateLine( 3,line );

   delay( 2000 );
   display->clear();

   // If we fast booted then drop to AP mode again

   if ( config->wasFastReboot() )
   {
      newConfiguration();
   }

   return rebootStr;
}

// ---------------------------------------------------------------------
// Start networking
// We try to connect to WiFi and we wait for 60s (default, may be configured
// via WIFI_CONNECT_TIMEOUT).  If failed to connect then we reboot, with
// the reboot reason set to BOOT_NO_WIFI.
//
// After 3 failed attempts to connect then drop to AP mode to setup a new
// WiFi network.
//
// If we connected to WiFi but failed to acquire NTP then we reboot with
// the code set to BOOT_NO_NTP.  We may send an email to that effect.
//
// So we only exit this function effectively if we're connected to WiFi
// and have valid NTP set.  The networking module will also have started:
//
//    - webserver
//    - email
//    - emonCMS sending task
//    - async UDP

void startNetworking()
{
   char line[ MAX_DISPLAY_COLUMNS ];
   char *ssid = GET_REGISTRY_STRING( WIFI_SSID );

   display->updateLine( 0,"Starting Networking..." );
   display->updateLine( 1,"SSID :" );
   display->updateLine( 2,ssid );

   // If no WIFI_SSID then we just jump straight to new config
   if ( ssid && !strcmp( ssid,"unknown" ) )
   {
      config->setPersistentInt( k_rebootType,BOOT_NO_WIFI );
      delay( 500 );

      newConfiguration();
   }

   // set reboot reason to no-wifi so if we fail here we detect it

   config->setPersistentInt( k_rebootType,BOOT_NO_WIFI );

   networking = new Networking( networkingInfoCallback );
   display->setNetworking( networking );

   networking->initialise();

   if ( !networking->isConnected() )
   {
      int32_t  failedReboots;

      config->getPersistentInt( k_noNetworkCounter,&failedReboots,0 );
      failedReboots++;

      config->setPersistentInt( k_noNetworkCounter,failedReboots );

      snprintf( line,MAX_DISPLAY_COLUMNS," Failure %u",failedReboots );
      display->updateLine( 4,line );

      // If we've had X failures to acquire WiFi, then revert to AP mode
      // and new configuration attempt

      if ( failedReboots >= MAX_FAILED_WIFI_ATTEMPTS )
      {
         config->setPersistentInt( k_rebootType,BOOT_NO_WIFI );
         delay( 2000 );

         newConfiguration();
      }

      display->updateLine( 5," Rebooting in 10s" );
      delay( 10000 );
      ESP.restart();
   }
   else
   {
      config->setPersistentInt( k_noNetworkCounter,0 );
   }

   // show network status

   display->show( Display::NETWORK_STATUS );

   // If we don't have NTP, then we reboot here if we have
   // a configuration - ping an email too.  If no configuration then
   // we assume that a new config will be loaded....

   if ( !networking->didAcquireNTP() )
   {
      display->updateLine( 5,"Reboot in 5s" );

      char msg[ 128 ];
      snprintf( msg,128,"Failed to aquire NTP - rebooting"  );

      networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                  "Heat Pump Monitoring - Startup NTP fault",msg );

      config->setPersistentInt( k_rebootType,BOOT_NO_NTP );

      delay( 5000 );
      ESP.restart();
   }
}

// ---------------------------------------------------------------------
// did we boot with button down pressed, if so hold - allows webserver
// to be used to re-configure the unit.  We don't exit this method.

void checkBootHold()
{
   if ( hwConfig->TouchButton1 != -1 )
   {
      touch_value_t  touchVal = touchRead( hwConfig->TouchButton1 );
      if ( touchVal < touchThreshold )
      {
         while( 1 )
         {
            display->show( Display::NETWORK_STATUS );
            display->updateLine( 3,"  !! BOOT HOLD !!",false );
            delay( 5000 );
         }
      }
  }
}

// ---------------------------------------------------------------------
// Initialise measurement
//
// This sets up any configured sensors, thermocouples, modbus, power
// meters, heat meters, heat pump & the measurement module itself

void  initialiseMeasurement()
{
   // Instantiate the temperature collecting module

   tempModule = new TemperatureModule;
   tempModule->initialise();

   // get modbus if available

   configureModBus();
   display->setModBus( modbusMaster );

   // Instantiate the power collecting module

   powerModule = new PowerModule( modbusMaster );
   powerModule->initialise();

   // Instantiate the Shelly power collecting module

   shellyPowerModule = new ShellyPowerModule();
   shellyPowerModule->initialise();

   // Instantiate the heat pump collecting module if active and we have
   // a valid modbus only if we're not acting as an LG simulator

   if ( isSensorRequired( LGHEATPUMPSIM_SENSOR_NAME ) && hwSerial )
   {
      lgSimulator = new LGHeatPumpSimulator( hwSerial );

      lgSimulator->initialise();
      if ( !lgSimulator )
      {
         delete lgSimulator;
         lgSimulator = nullptr;
      }
   }
   else if ( isSensorRequired( LGHEATPUMP_SENSOR_NAME ) && modbusMaster )
   {
      lgThermaV = new LGHeatPump( modbusMaster );

      if ( lgThermaV->isAvailable() )
      {
         lgThermaV->initialise();
      }
      else
      {
         delete lgThermaV;
         lgThermaV = nullptr;
      }
   }

   // User IO needs HP collection stats, could be a null ptr but UserIO will
   // deal with it

   display->setLGHeatPump( lgThermaV );

   // Instantiate the HeatMeterModule, userIO also needs HM module for update

   heatMeterModule = new HeatMeterModule( tempModule );
   heatMeterModule->initialise();
   display->setHeatMeter( heatMeterModule );

   // Instantiate the measurement module, but don't initialise it just yet,
   // Display needs access to data

   measurement = new Measurement( tempModule,powerModule,shellyPowerModule,lgThermaV,heatMeterModule,storageModule,networking );
   display->setMeasurement( measurement );

   // let's tell storage we have networking available

   storageModule->setNetworking( networking );

   delay( 1000 );

   // Can now initialise the measurement module

   PW_MSG( "Initialising measurement prior to loop" );

   measurement->initialise();

   // Can release the sensor JSON data now as we're setup

   releaseSensorJSON();
}

// ---------------------------------------------------------------------
// intialise touch for boards if active
// Touch ISR will be activated when reading is lower than the touchThreshold

void setupTouch()
{
   if ( hwConfig->TouchButton1 != -1 )
   {
      touchAttachInterrupt( hwConfig->TouchButton1,gotTouch1Event,touchThreshold );
   }

   if ( hwConfig->TouchButton2 != -1 )
   {
      touchAttachInterrupt( hwConfig->TouchButton2,gotTouch2Event,touchThreshold );
   }
}

// ---------------------------------------------------------------------
// send any startup email if enabled

void handleBootEmail( const String &rebootStr )
{
   if ( GET_REGISTRY_INT( SEND_EMAILS ) != 1 )
   {
      PW_MSG( "Not sending boot email" );
      return;
   }

   // Send emails, attachments if available

   String emailMsg( "Initial boot up completed\nVersion : ");
   emailMsg += String( k_versionStr ) + "\n\n";
   emailMsg += networking->getIPAddress();
   emailMsg += "\n\n";

   int32_t  rebootCount;
   (void) config->getPersistentInt( k_rebootCounter,&rebootCount,0 );

   emailMsg += "Reboot count : ";
   emailMsg += String( rebootCount,DEC );
   emailMsg += "\n";

   emailMsg += "Last reboot reason : ";

   emailMsg += rebootStr;

   PW_MSG( "%s",emailMsg.c_str() );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Startup",emailMsg );
}

// ---------------------------------------------------------------------
// send any data logs

void handleDataLogs()
{
   // Send register scan logs, modbus log and lg registers read so far, removing after sending

   if ( SD.exists( LGREGISTER_SCAN_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Registers","Modbus regs",LGREGISTER_SCAN_LOG,true ) )
      {
         SD.remove( LGREGISTER_SCAN_LOG);
      }
   }

   if ( SD.exists( LGMODBUS_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Log","Modbus Logs",LGMODBUS_LOG,true ) )
      {
         SD.remove( LGMODBUS_LOG );
      }
   }

   if ( SD.exists( LGREGISTERS_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Registers","Modbus Registers",LGREGISTERS_LOG,true ) )
      {
         if ( lgThermaV && lgThermaV->isLogging() )
         {
            SD.remove( LGREGISTERS_LOG );
            PW_DEBUG( "Removed %s as logging LG",LGREGISTERS_LOG );
         }
      }
   }

   if ( SD.exists ( DEBUG_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "Debug log","Debug Logs",DEBUG_LOG,true ) )
      {
         if ( GET_REGISTRY_INT( KEEP_DEBUG_LOG ) != 1 )
         {
            SD.remove( DEBUG_LOG );
         }
      }
   }

   if ( tvmgFileSys.exists( LGSTATUS_LOG_HTML ) )
   {
      networking->sendEmailWithFileAsBody( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "LG Event Log",LGSTATUS_LOG_HTML );
   }
   if ( tvmgFileSys.exists( LGREGISTERS_LOG ) )
   {
      networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Registers","Modbus Registers",LGREGISTERS_LOG );
   }

}

// ---------------------------------------------------------------------
// For debug purposes

void  handleDebugTests()
{
   if ( !testMallocBuffer )
   {
      int size = GET_REGISTRY_INT( HEAP_TEST_SIZE );
      if ( size != -1 )
      {
         PW_MSG( "Test alloc %d KiB",size );
         size *= 1024;
         testMallocBuffer = static_cast<char *>(malloc( size ));
         if ( !testMallocBuffer )
         {
            PW_DEBUG( "failed to malloc" );
         }
      }
   }

   if ( !testFastReboot )
   {
      int shouldAssert = GET_REGISTRY_INT( ASSERT_FOR_FAST_BOOT );
      if ( shouldAssert > 0 )
      {
         PW_MSG( "Testing fast boot" );
         testFastReboot = true;
      }
   }
}

// ---------------------------------------------------------------------
// Conditionally override the ESP's shouldPrintChipDebugReport() so we get a report of
// the device info

#if 0
bool shouldPrintChipDebugReport(void)
{
  return true;
}
#endif

// ---------------------------------------------------------------------
// Create/initialise all modules prior to main loop

void setup( void )
{
   char line[ MAX_DISPLAY_COLUMNS ];

   setupSerial();

   // Initialise our configuration, this will create the filesystem if neeeded but not
   // the registry, then select the hardware.

   config = Config::instance( true );
   selectHardware();

   // prepare the display and indicators for output

#if defined(TVMG_OLED)   
   display = new OledDisplay;
#elif defined(TVMG_WAVESHARE_LCDB)
   display = new LcdDisplay;
#else
   display = new DummyDisplay;
#endif   

   display->initialise();
   
   Indicator::initialise();

   // Get system indicator and turn it on
   systemIndicator = Indicator::getIndicator( Indicator::SYSTEM,SYSTEM_SETUP_ID );
   Indicator::Scoped guard( systemIndicator );

   // handle reboot reason - display info & may drop to AP mode if too many fast boot cycles

   String rebootReason = handleBootReason();

   // Is registry available, if not then we need to enter configuration
   // mode, i.e. networking with AP only. The user must resolve issues
   // via the webserver hopefully.  We need to initialise to get registry
   // working here.

   config->initialise();
   if ( ! config->isRegistryAvailable() )
   {
      config->setPersistentInt( k_rebootType,BOOT_NO_CONFIG );
      newConfiguration();
   }

   // If this is a result of factory reset, then new configuration too,
   // otherwise we can move on as normal and start networking.

   if ( config->isFactoryReset() )
   {
      newConfiguration();
   }

   startNetworking();

   // set reboot type that we got to setup, i.e. past WiFi & NTP etc

   config->setPersistentInt( k_rebootType,BOOT_IN_SETUP );

   PW_MSG( "Version: %s",k_versionStr );
   PW_MSG( "Arduino Board: %s", ARDUINO_BOARD );
   PW_MSG( "Arduino Variant: %s", ARDUINO_VARIANT );
   PW_MSG( "Arduino Version: %s", ESP_ARDUINO_VERSION_STR);

   // Instantiate the storage module, and initialise it.  If the SD card
   // is not operational the storage module will not save data but at least
   // the system will continue to operate.

   storageModule = new Storage();
   storageModule->initialise();

   // check whether we need to stop the boot (key press) - this function
   // may not return.

   checkBootHold();

   // Now initialise all sensors etc, then touch sensors.

   initialiseMeasurement();
   setupTouch();

   // Perhaps send startup email

   handleBootEmail( rebootReason );

   // Any debug tests

   handleDebugTests();

   // And we now set the reboot marker as hopefully next is a power cycle

   config->setPersistentInt( k_rebootType,POWER_CYCLE );
}

// ---------------------------------------------------------------------
// handleAnyOTAUpdate
//
// If OTA update has occurred, if so then we go for a hard reset
// which takes ~ 250ms for the watchdog to kick in, so we delay
// initially to let the webserver service the GET response, and
// then delay after the reset which will cycle the chip

void handleAnyOTAUpdate()
{
   if ( networking->hasUpdated() )
   {
      config->setPersistentInt( k_rebootType,SERVER_OTA_UPDATE );

      delay( 1500 );
      hwReset();

      delay( 5000 );
      PW_ERROR( "HW Reset Failed" );
   }
}

// ---------------------------------------------------------------------
// checkNetworking
//
// If we're connected then all ok.  Otherwise if we've been disconnected
// for > 90s then need to restart - return false;

bool isNetworkOk()
{
   static uint32_t networkLost = 0;
   bool networkOk = true;

   // Have we lost network connection ?  Check if connection dropped for
   // too long...

   if ( networkLost )
   {
      if ( networking->isConnected() )
      {
         PW_MSG( "Regained network" );
         networkLost = 0;
      }
      else if ( millis() - networkLost > NETWORK_ALLOWED_DISCONNECTED_MS )
      {
         PW_ERROR( "Lost network, need to reboot" );
         config->setPersistentInt( k_rebootType,LOST_WIFI );
         networkOk = false;
      }
   }
   else if ( networking && !networking->isConnected() )
   {
      PW_WARN( "Lost network" );
      networkLost = millis();
   }

   return networkOk;
}

// ---------------------------------------------------------------------
// Loop

#define LOOP_PERIOD_MS     5000

bool  loopTestRequired = false;

void  loopTest()
{
#if 0

   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Current Data","Sample Data","/20251225.dat" );
//   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Sensor Data","Sensors","/sensors.dat",true );
   handleTouch1();
#endif

   handleDataLogs();
}

void loop(void)
{
   static uint32_t targetMillis = 0,deltaMillis,currentMillis;
   static bool     didDailyUpdate = false;

   bool  restartRequired = false;

#if defined(TVMG_RGBLED)
   static uint8_t  count = 0;
   uint8_t         brightness = 16;
   if ( count++ % 2 )
   {
      brightness = 0;
   }
   rgbLedWrite( 48,0,0,brightness );
#endif

   START_TIMING( "Main Loop" );

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }

   // Take the networking mutex, it's a recursive mutex so if we take
   // again in this task, e.g. to send an email then no problem.

   if ( Networking::takeNetworkMutex( NETWORK_ALLOWED_BUSY_MS ) != 1 )
   {
      PW_ERROR( "Timeout on network mutex, rebooting..." );
      config->setPersistentInt( k_rebootType,LOOP_MUTEX );
      restartRequired = true;
   }

   // Check if we've updated, we'll reset if OTA has occurred
   handleAnyOTAUpdate();

   // Check for reboot requested, doing this in the loop task to
   // allow the webserver to respond
   if ( isRebootRequired() )
   {
      reboot();
   }

   // Check network is alive
   restartRequired |= !isNetworkOk();

   // If we've been up for 24 days then reboot - just to sure we
   // don't have millis() (32 bits) causing issues.

   if ( currentMillis > ( 24 * 24 * 3600 * 1000) )
   {
      PW_MSG( "24 day reboot %d",currentMillis );
      config->setPersistentInt( k_rebootType,APP_24D_RESET );
      restartRequired = true;
   }

   if ( restartRequired )
   {
      PW_MSG( "Rebooting..." );

      ESP.restart();
      while( 1 )
      {
         delay( 500 );
      }
   }

   // Handle any button presses

   processButtons();

   // take measurement, if we performed a daily update in sample then
   // set local daily update flag and reset LG event log if LG present.
   // The storage didDailyUpdate() will be true for the update hour,
   // so we reset the local daily update flag when storate returns false

   START_TIMING( "takeSample" );
   measurement->takeSample();
   if ( !didDailyUpdate && measurement->didDailyUpdate() )
   {
      PW_DEBUG( "DailyUpdate : true" );
      didDailyUpdate = true;
      if ( lgThermaV )
      {
         lgThermaV->resetEventLog();
      }
   }
   else if ( didDailyUpdate && !measurement->didDailyUpdate() )
   {
      PW_DEBUG( "DailyUpdate : false" );
      didDailyUpdate = false;
   }
   END_TIMING;

   // run any debugging test, setup by webserver for picking up in the loop

   if ( loopTestRequired )
   {
      loopTest();
      loopTestRequired = false;
   }

   // If we have an heat pump simulator then perform housekeeping
   if ( lgSimulator )
   {
      lgSimulator->heartbeat();
   }

   // our target MS is our original millis at entry of this loop, plus
   // our sampling delay

   targetMillis += LOOP_PERIOD_MS;
   currentMillis = millis();

   // We may need to skip a sample(s) if we've executed too long in this loop

   while ( currentMillis >= targetMillis )
   {
      targetMillis += LOOP_PERIOD_MS;
   }

   deltaMillis = targetMillis - currentMillis;

   END_TIMING;

   // Now we release the mutex and delay for next cycle

   Networking::releaseNetworkMutex();

   PW_DEBUG( "Loop Delay %u",deltaMillis );

   delay( deltaMillis );

   // Assert if we're testing fast reboot
   if ( testFastReboot )
   {
      assert( 0 );
   }
}
