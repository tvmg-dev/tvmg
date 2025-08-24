#include <SD.h>
#include <FS.h>
#include <ModbusMaster.h>

#include <time.h>

#include "utils.h"
#include "hwconfig.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "ModbusTCP.h"
#include "HeatMeter.h"
#include "UserIO.h"
#include "Measurement.h"
#include "Config.h"
#include "Storage.h"
#include "Networking.h"
#include "WebServer.h"
#include "LGHeatPump.h"

// ---------------------------------------------------------------------

ModbusMaster      *modbusMaster = nullptr;
TemperatureModule *tempModule = nullptr;
PowerModule       *powerModule = nullptr;
ModbusTCP         *modbusTCP = nullptr;
Storage           *storageModule = nullptr;
HeatMeterModule   *heatMeterModule = nullptr;
UserIO            *userIO = nullptr;
Measurement       *measurement = nullptr;
Config            *config = nullptr;
Networking        *networking = nullptr;
LGHeatPump        *lgThermaV = nullptr;

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

// ---------------------------------------------------------------------
// Reboot handling code, if we have N with no network then we consider WiFi has
// failed and drop to AP mode which will remain active until reboot.

#define MAX_FAILED_WIFI_ATTEMPTS 3

void newConfiguration( void )
{
   networking = new Networking;
   networking->startAccessPoint();

   PW_WARN( "Need to configure via SSID : %s",networking->getSSID().c_str() );
   PW_WARN( "Use %s/manager",networking->getMDNSName().c_str() );
   PW_WARN( "Or %s/manager",networking->getIPAddress().c_str() );

   if ( userIO )
   {
      char line[ MAX_OLED_COLUMNS ];

      userIO->clear();

      if ( !config->isFactoryReset() )
      {
         if ( config->isFastReset() )
         {
            snprintf( line,MAX_OLED_COLUMNS,"Fast Reset Error" );
         }
         else
         {
            int32_t  numNoWifi;

            config->getPersistentInt( k_noNetworkCounter,&numNoWifi,0 );
            snprintf( line,MAX_OLED_COLUMNS,"No WiFi count %d",numNoWifi );
         }
      }
      else
      {
         snprintf( line,MAX_OLED_COLUMNS,"From Reset..." );
      }

      userIO->updateLine( 0,line );

      snprintf( line,MAX_OLED_COLUMNS,"SSID %s",networking->getSSID().c_str() );
      userIO->updateLine( 2,line );
      snprintf( line,MAX_OLED_COLUMNS,"Use %s",networking->getMDNSName().c_str() );
      userIO->updateLine( 3,line );
      snprintf( line,MAX_OLED_COLUMNS,"Use %s",networking->getIPAddress().c_str() );
      userIO->updateLine( 4,line );
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
// button 1 is for debug emails, button 2 is for toggling OLED cycling
// or refreshing current display

int  touchThreshold = 32;
bool wasButton1Pressed = false;
bool wasButton2Pressed = false;
bool userIOHoldScreen = false;   // if true then don't cycle screens

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

// Have an instance of Measurement::Sample here to avoid potential stack depth
// issue, obviously consumes ram..

Measurement::Sample  s_sample;

void  handleTouch1()
{
   PW_MSG( "Button-1 was pressed" );

   PW_DEBUG( "Touch 1 value %d",touch1Value );

   wasButton1Pressed = false;

   userIO->clear();
   userIO->updateLine( 1, "BT-1 pressed" );

   String   msgString;
   char     message[ 128 ];

   s_sample = measurement->getLastSample();
   snprintf( message,sizeof(message),"Button samplen\n"
                    "IP : %s [%s]\n"
                    "Free Bytes : %u\n"
                    "Time signature %u\n\n",
                    networking->getLocalMDNSName().c_str(),
                    networking->getIPAddress().c_str(),
                    ESP.getFreeHeap(),
                    s_sample.m_sampleTime );

   msgString = message;

   TemperatureModule::takeMutex();
   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      if ( s_sample.m_tempSensors[ i ] )
      {
         const TempSensor  *sensor = s_sample.m_tempSensors[ i ];

         snprintf( message,sizeof(message),"%30s,%.1f\n",sensor->m_name,sensor->m_temp );

         msgString += message;
      }
   }
   TemperatureModule::releaseMutex();

   int i = 0;
   const PowerSensor *sensor;
   while( ( sensor = s_sample.m_powerSensors[ i++ ] ) )
   {
      snprintf( message,sizeof(message),"%30s,%.1f\n",sensor->m_name,sensor->m_power,sensor->m_energy );
      msgString += message;
   }
   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Btn Press",msgString.c_str() );

   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Current Data","Sample Data",storageModule->getCurrentFileName() );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug Log","Debug log",DEBUG_LOG );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"HP Modbus","Modbus Data",LGMODBUS_LOG );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"LG Event Log","Event log",LGSTATUS_LOG,true );
}

void  handleTouch2()
{
   PW_MSG( "Button-2 was pressed" );

   wasButton2Pressed = false;
   if ( userIOHoldScreen )
   {
      userIOHoldScreen = false;
   }
   else
   {
      userIOHoldScreen = true;
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
         PW_MSG( "Creating new modbus master with h/w serial" );
         modbusMaster = new ModbusMaster;
         HardwareSerial *serial = new HardwareSerial( hwConfig->ModBusSerial );

         PW_MSG( "Starting MODBUS port %u",hwConfig->ModBusSerial );
         PW_DEBUG( "   Baudrate %u, Rx pin [%u], Tx pin [%u]",hwConfig->ModBusBaudRate,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );

         // If enabled setup the MAX3485 device, need to set the device enable high for transmit to slaves
         // and low for receive.  The ModbusMaster has callbacks to facilitate that.
         // The waveshare LCD doesn't have enable/disable for the bus and uses the logic level of the
         // transmit pin to enable tx or rx mode of the SP3485EN using bias resistors to pull AB signals
         // to vcc/gnd if in rx mode.

         if ( hwConfig->ModBus485EnGPIO != -1 )
         {
            pinMode( hwConfig->ModBus485EnGPIO,OUTPUT );
            modbusMaster->preTransmission( modbusPreTransmission );
            modbusMaster->postTransmission( modbusPostTransmission );
         }

         serial->begin( hwConfig->ModBusBaudRate,hwConfig->ModBusSerialFormat,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );
         modbusMaster->begin( 1, *serial );
      }
   }
}

// ---------------------------------------------------------------------
// Setup serial port

void setupSerial()
{
#if !PW_LCD
   Serial.begin( 115200,SERIAL_8N1 );

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

//   Serial.setDebugOutput(true); from chip-debug-report.cpp

   Serial.begin( 115200,SERIAL_8N1 );

   delay( 500 );

   PW_DEBUG( "pins Ok %d serial enable %d",setPinsOk,isBootSerialEnabled );

#else
   // should have CDC USB active

   Serial.begin( 115200 );
   delay( 500 );
#endif
}

// ---------------------------------------------------------------------
// Create/initialise all modules prior to main loop

void setup( void )
{
   char line[ MAX_OLED_COLUMNS ];

   setupSerial();

   // Initialise our configuration, this will create SPIFFS if neeeded but not
   // the registry

   config = Config::instance( true );

   selectHardware();

   // prepare the display for output

   userIO = new UserIO();
   userIO->initialise();

   // Bump the reboot count

   int32_t  rebootCount;
   (void) config->getPersistentInt( k_rebootCounter,&rebootCount,0 );
   rebootCount++;
   config->setPersistentInt( k_rebootCounter,rebootCount );

   // Get the reboot reason

   RebootType rebootReason;
   String     rebootStr = config->getRebootReason( &rebootReason );

   // Brief display of reboot reason

   userIO->updateLine( 0,"Reboot Reason" );
   snprintf( line,MAX_OLED_COLUMNS,"Code : %d",rebootReason );
   userIO->updateLine( 1,line );
   snprintf( line,MAX_OLED_COLUMNS,"%s",config->getAppRebootReason( rebootReason ).c_str() );
   userIO->updateLine( 3,line );

   // If we fast booted then drop to AP mode again

   if ( config->isFastReset() )
   {
      newConfiguration();
   }

   delay( 2000 );
   userIO->clear();

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
   // otherwise we can move on as normal

   if ( config->isFactoryReset() )
   {
      newConfiguration();
   }

   userIO->updateLine( 0,"Starting Networking..." );
   userIO->updateLine( 1,"SSID :-" );
   userIO->updateLine( 2,GET_REGISTRY_STRING( WIFI_SSID ) );

   // set reboot reason to no-wifi so if we fail here we detect it

   config->setPersistentInt( k_rebootType,BOOT_NO_WIFI );

   networking = new Networking;
   networking->initialise();

   userIO->setNetworking( networking );

   if ( !networking->isConnected() )
   {
      int32_t  failedReboots;

      config->getPersistentInt( k_noNetworkCounter,&failedReboots,0 );
      failedReboots++;

      config->setPersistentInt( k_noNetworkCounter,failedReboots );

      snprintf( line,MAX_OLED_COLUMNS," Failure %u",failedReboots );
      userIO->updateLine( 4,line );

      // If we've had X failures to acquire WiFi, then revert to AP mode
      // and new configuration attempt

      if ( failedReboots >= MAX_FAILED_WIFI_ATTEMPTS )
      {
         config->setPersistentInt( k_rebootType,BOOT_NO_WIFI );
         delay( 2000 );

         newConfiguration();
      }

      userIO->updateLine( 5," Rebooting in 10s" );
      delay( 10000 );
      ESP.restart();
   }
   else
   {
      config->setPersistentInt( k_noNetworkCounter,0 );
   }

   // show network status

   userIO->show( UserIO::NETWORK_STATUS );

   // If we don't have NTP, then we reboot here if we have
   // a configuration - ping an email too.  If no configuration then
   // we assume that a new config will be loaded....

   if ( !networking->didAcquireNTP() )
   {
      userIO->updateLine( 5,"Reboot in 5s" );

      char msg[ 128 ];
      snprintf( msg,128,"Failed to aquire NTP - rebooting",VERSION_STR  );

      networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                  "Heat Pump Monitoring - Startup NTP fault",msg );

      config->setPersistentInt( k_rebootType,BOOT_NO_NTP );

      delay( 5000 );
      ESP.restart();
   }

   // set we got to setup, i.e. past WiFi & NTP

   config->setPersistentInt( k_rebootType,BOOT_IN_SETUP );

   PW_MSG( "Version: %s",VERSION_STR );
   PW_MSG( "Arduino Board: %s", ARDUINO_BOARD );
   PW_MSG( "Arduino Variant: %s", ARDUINO_VARIANT );
   PW_MSG( "Arduino Version: %s", ESP_ARDUINO_VERSION_STR);

   // Instantiate the storage module, and initialise it.  If the SD card
   // is not operational the storage module will not save data but at least
   // the system will continue to operate.

   storageModule = new Storage();
   storageModule->initialise();

   // did we boot with button down pressed, if so hold - allows webserver
   // to be used to re-configure the unit

   if ( hwConfig->TouchButton1 != -1 )
   {
      touch_value_t  touchVal = touchRead( hwConfig->TouchButton1 );
      if ( touchVal < touchThreshold )
      {
         while( 1 )
         {
            userIO->show( UserIO::NETWORK_STATUS );
            userIO->updateLine( 3,"  !! BOOT HOLD !!",false );
            delay( 5000 );
         }
      }
  }

   // Instantiate the temperature collecting module

   tempModule = new TemperatureModule;
   tempModule->initialise();

   // get modbus if available

   configureModBus();
   userIO->setModBus( modbusMaster );

   // Instantiate the power collecting module

   powerModule = new PowerModule( modbusMaster );
   powerModule->initialise();

   // Instantiate the heat pump collecting module if active and we have
   // a valid modbus

   if ( isSensorRequired( LGHEATPUMP_SENSOR_NAME ) && modbusMaster )
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

   userIO->setLGHeatPump( lgThermaV );

   // Instantiate the HeatMeterModule, userIO also needs HM module for update

   heatMeterModule = new HeatMeterModule( tempModule );
   heatMeterModule->initialise();
   userIO->setHeatMeter( heatMeterModule );

   // Instantiate the measurement module, but don't initialise it just yet,
   // userIO needs access to data

   measurement = new Measurement( tempModule,powerModule,lgThermaV,heatMeterModule,storageModule );
   userIO->setMeasurement( measurement );

   // let's tell storage we have networking available

   storageModule->setNetworking( networking );

   delay( 1000 );

   // Can now initialise the measurement module

   PW_MSG( "Initialising measurement prior to loop" );

   measurement->initialise();

   // intialise touch for boards if active
   // Touch ISR will be activated when reading is lower than the touchThreshold

   if ( hwConfig->TouchButton1 != -1 )
   {
      touchAttachInterrupt( hwConfig->TouchButton1,gotTouch1Event,touchThreshold );
   }

   if ( hwConfig->TouchButton2 != -1 )
   {
      touchAttachInterrupt( hwConfig->TouchButton2,gotTouch2Event,touchThreshold );
   }

   // Send emails, attachments if available

   String emailMsg( "Initial boot up completed\nVersion : " VERSION_STR "\n\n" );
   emailMsg += networking->getIPAddress();
   emailMsg += "\n\n";

   emailMsg += "Reboot count : ";
   emailMsg += String( rebootCount,DEC );
   emailMsg += "\n";

   emailMsg += "Last reboot reason : ";

   emailMsg += rebootStr;

   PW_MSG( "%s",emailMsg.c_str() );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Startup",emailMsg );

   // Send register scan logs, modbus log and lg registers read so far, removing after sending

   if ( SD.exists( LGREGISTER_SCAN_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Registers","Modbus regs",LGREGISTER_SCAN_LOG ) )
      {
         SD.remove( LGREGISTER_SCAN_LOG);
      }
   }

   if ( SD.exists( LGMODBUS_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Log","Modbus Logs",LGMODBUS_LOG ) )
      {
         SD.remove( LGMODBUS_LOG );
      }
   }

   if ( SD.exists( LGREGISTERS_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "HP Modbus Registers","Modbus Registers",LGREGISTERS_LOG ) )
      {
         if ( lgThermaV && lgThermaV->isLogging() )
         {
            SD.remove( LGREGISTERS_LOG );
            PW_DEBUG( "Removed %s as logging LG",LGREGISTERS_LOG );
         }
      }
   }

   // Send the LG event log if available

   if ( config->getSPIFFS()->exists( LGSTATUS_LOG ) )
   {
      networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "LG Event Log","Event Log",LGSTATUS_LOG,true );
   }

   if ( SD.exists ( DEBUG_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                        "Debug log","Debug Logs",DEBUG_LOG ) )
      {
         if ( GET_REGISTRY_INT( KEEP_DEBUG_LOG ) != 1 )
         {
            SD.remove( DEBUG_LOG );
         }
      }
   }

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

   // Can release the sensor JSON data now as we're setup
   releaseSensorJSON();

   // And we now set the reboot marker as hopefully next is a power cycle

   config->setPersistentInt( k_rebootType,POWER_CYCLE );
}

// ---------------------------------------------------------------------
// Loop

#define LOOP_PERIOD_MS     5000

void loop(void)
{
   static uint32_t targetMillis = 0,deltaMillis,currentMillis;
   static uint32_t networkLost = 0;
   static bool     didDailyUpdate = false;

   bool  restartRequired = false;

   START_TIMING( "Main Loop" );

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }

   // Take the networking mutex, it's a recursive mutex so if we take
   // again in this task, e.g. to send an email then no problem.

   if ( Networking::takeNewMutex( NETWORK_ALLOWED_BUSY_MS ) != 1 )
   {
      PW_ERROR( "Timeout on network mutex, rebooting..." );
      config->setPersistentInt( k_rebootType,LOOP_MUTEX );
      restartRequired = true;
   }

   if ( networking->hasUpdated() )
   {
      // OTA update has occurred, if so then we go for a hard reset
      // which takes ~ 250ms for the watchdog to kick in, so we delay
      // initially to let the webserver service the GET response, and
      // then delay after the reset which will cycle the chip

      config->setPersistentInt( k_rebootType,SERVER_OTA_UPDATE );

      delay( 2500 );
      hwReset();

      delay( 5000 );
      PW_ERROR( "HW Reset Failed" );
   }

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
         restartRequired = true;
      }
   }
   else if ( networking && !networking->isConnected() )
   {
      PW_WARN( "Lost network" );
      networkLost = millis();
   }

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

   // take measurement, if we performed a daily update in sample then
   // set local daily update flag and reset LG event log if LG present.
   // The storage didDailyUpdate() will be true for the update hour,
   // so we reset the local daily update flag when storate returns false

   START_TIMING( "takeSample" );
   measurement->takeSample();
   if ( storageModule )
   {
      if ( !didDailyUpdate && storageModule->didDailyUpdate() )
      {
         PW_DEBUG( "DailyUpdate : true" );
         didDailyUpdate = true;
         if ( lgThermaV )
         {
            lgThermaV->resetEventLog();
         }
      }
      else if ( didDailyUpdate && !storageModule->didDailyUpdate() )
      {
         PW_DEBUG( "DailyUpdate : false" );
         didDailyUpdate = false;
      }
   }
   END_TIMING;

   START_TIMING( "UserIO Update" );
   userIO->update();
   END_TIMING;

   START_TIMING( "UserIO Show Screen" );
   if ( userIOHoldScreen )
   {
      userIO->refresh();
   }
   else
   {
      userIO->showNext();
   }
   END_TIMING;

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

   Networking::releaseNewMutex();

   PW_DEBUG( "Loop Delay %u",deltaMillis );

   delay( deltaMillis );

   // Assert if we're testing fast reboot
   if ( testFastReboot )
   {
      assert( 0 );
   }
}
