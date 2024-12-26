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

// Amount of time we can have the webserver busy before we reboot.  This
// is to ensure that if editing we will reboot if not completed in this period.

#define  NETWORK_ALLOWED_BUSY_MS (120 * 1000)

// We'll malloc into this buffer for heap size debugging

char  *testMallocBuffer = nullptr;

// ---------------------------------------------------------------------
// Reboot handling code, if we have 3 reboots then we consider WiFi has
// failed and drop to AP mode which will remain active until reboot.

#define REBOOT_COUNTER_FILE      "/failedreboot.dat"
#define MAX_FAILED_WIFI_ATTEMPTS 3

uint32_t failedReboots = 0;

void  clearFailedRebootCount()
{
   fs::SPIFFSFS *spiffs = config->getSPIFFS();
   if ( spiffs->exists( REBOOT_COUNTER_FILE ) )
   {
      spiffs->remove( REBOOT_COUNTER_FILE );
   }
   failedReboots = 0;
}

uint32_t getFailedRebootCount()
{
   uint32_t current = 0;
   fs::SPIFFSFS *spiffs = config->getSPIFFS();
   File file = spiffs->open( REBOOT_COUNTER_FILE,FILE_READ );

   if ( file )
   {
      current = file.parseInt();
      file.close();
   }

   PW_DEBUG( "reboot count %d",current );
   return( current );
}

void  bumpFailedRebootCount( uint32_t count )
{
   count++;

   fs::SPIFFSFS *spiffs = config->getSPIFFS();
   File file = spiffs->open( REBOOT_COUNTER_FILE,FILE_WRITE );

   if ( file )
   {
      file.println( count );
      file.close();
   }

   PW_DEBUG( "New reboot count %d",count );
}

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

      snprintf( line,MAX_OLED_COLUMNS,"Failed %d reboots",failedReboots );
      userIO->updateLine( 0,line );

      snprintf( line,MAX_OLED_COLUMNS,"SSID %s",networking->getSSID().c_str() );
      userIO->updateLine( 2,line );
      snprintf( line,MAX_OLED_COLUMNS,"Use %s",networking->getMDNSName().c_str() );
      userIO->updateLine( 3,line );
      snprintf( line,MAX_OLED_COLUMNS,"Use %s",networking->getIPAddress().c_str() );
      userIO->updateLine( 4,line );
   }

   // Clear the reboot counter, so we can try and reboot again if possible

   clearFailedRebootCount();

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

void  handleTouch1()
{
   PW_MSG( "Button-1 was pressed" );

   PW_DEBUG( "Touch 1 value %d",touch1Value );

   wasButton1Pressed = false;

   userIO->clear();
   userIO->updateLine( 1, "BT-1 pressed" );

   String   msgString;
   char     message[ 128 ];

   Measurement::Sample  sample = measurement->getLastSample();
   snprintf( message,128,"Button sample\n\n"
                    "IP : %s [%s]\n"
                    "Free Bytes : %u\n"
                    "Time signature %u\n\n",
                    networking->getLocalMDNSName().c_str(),
                    networking->getIPAddress().c_str(),
                    ESP.getFreeHeap(),
                    sample.m_sampleTime );

   msgString = message;

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      if ( sample.m_tempSensors[ i ] )
      {
         const TempSensor  *sensor;
         sensor = &sample.m_actualTemps[ i ];

         snprintf( message,128,"%30s,%.1f\n",sensor->m_name,sensor->m_temp );
         msgString += message;
      }
   }

   int i = 0;
   const PowerSensor *sensor;
   while( ( sensor = sample.m_powerSensors[ i++ ] ) )
   {
      snprintf( message,128,"%30s,%.1f\n",sensor->m_name,sensor->m_power,sensor->m_energy );
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

         // setup the MAX3485 device, need to set the device enable high for transmit to slaves
         // and low for receive.  The ModbusMaster has callbacks to facilitate that.

         pinMode( hwConfig->ModBus485EnGPIO,OUTPUT );
         modbusMaster->preTransmission( modbusPreTransmission );
         modbusMaster->postTransmission( modbusPostTransmission );

         serial->begin( hwConfig->ModBusBaudRate,hwConfig->ModBusSerialFormat,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );
         modbusMaster->begin( 1, *serial );
      }
   }
}

// ---------------------------------------------------------------------
// Create/initialise all modules prior to main loop

void setup( void )
{
   // start serial port, if the GPIO controlling serial on boot behaviour is low,
   // i.e. no serial on boot, then we reconfigure uart0 (Serial) to have
   // alternate GPIO pins for other library use of Serial and we disable
   // our serial logging

   pinMode( SERIAL_DISABLE_GPIO,INPUT_PULLUP );
   int val = digitalRead( SERIAL_DISABLE_GPIO );

   bool setPinsOk = true;
   if ( !val )
   {
      isBootSerialEnabled = false;
      setPinsOk = Serial.setPins( ALTERNATE_UART0_RX_GPIO,ALTERNATE_UART0_TX_GPIO );
   }

   Serial.begin( 115200,SERIAL_8N1 );

   delay( 1000 );

   // Initialise our configuration

   config = Config::instance();

   // Is registry available, if not then we need to enter configuration
   // mode, i.e. networking with AP only with SSID HeatPump-Monitor. The
   // user must download a suitable config.dat to the device.

   if ( ! config->isRegistryAvailable() )
   {
      newConfiguration();
   }

   // If this is a result of factory reset, then new configuration too

   if ( config->isFactoryReset() )
   {
      config->clearFactoryReset();
      newConfiguration();
   }

   selectHardware();

   // Must have a valid configuration at this stage

   // prepare the OLED display for output

   userIO = new UserIO();
   userIO->initialise();

   userIO->updateLine( 0,"Starting Networking..." );
   userIO->updateLine( 1,"SSID :-" );
   userIO->updateLine( 2,GET_REGISTRY_STRING( WIFI_SSID ) );

   networking = new Networking;
   networking->initialise();

   userIO->setNetworking( networking );

   if ( !networking->isConnected() )
   {
      failedReboots = getFailedRebootCount();
      char     line[ MAX_OLED_COLUMNS ];

      bumpFailedRebootCount( failedReboots );
      failedReboots++;
      snprintf( line,MAX_OLED_COLUMNS," Failure %u",failedReboots );
      userIO->updateLine( 4,line );

      if ( failedReboots >= MAX_FAILED_WIFI_ATTEMPTS )
      {
         delay( 5000 );

         // If we've had X failures to acquire WiFi, then revert to AP mode
         // and new configuration attempt

         newConfiguration();
      }

      userIO->updateLine( 5," Rebooting in 10s" );
      delay( 10000 );
      ESP.restart();
   }
   else
   {
      clearFailedRebootCount();
   }

   PW_MSG( "Version: %s",VERSION_STR );

   // Instantiate the storage module, and initialise it.  If the SD card
   // is not operational the storage module will not save data but at least
   // the system will continue to operate.

   storageModule = new Storage();
   storageModule->initialise();

   PW_MSG( "pins Ok %d serial enable %d",setPinsOk,isBootSerialEnabled );

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

      delay( 5000 );
      ESP.restart();
   }

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

   delay( 2000 );

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

   char initialMsg[ 128 ];

   snprintf( initialMsg,128,"Initial boot up completed\nVersion : [%s]\nIP : [%s]\nStarting monitoring...\n\n",VERSION_STR,networking->getIPAddress().c_str()  );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Startup",initialMsg );

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
         size *= 1024;
         PW_DEBUG( "allocating %d",size );
         testMallocBuffer = static_cast<char *>(malloc( size ));
         if ( !testMallocBuffer )
         {
            PW_DEBUG( "failed to malloc" );
         }
      }
   }
}

// ---------------------------------------------------------------------
// Loop

#define LOOP_PERIOD_MS     5000
#define FASTLOOP_PERIOD_MS 500

void loop(void)
{
   static uint32_t targetMillis = 0,deltaMillis,currentMillis;
   static uint32_t networkStartBusyMillis = 0;
   static uint32_t loopMillis = LOOP_PERIOD_MS;

   START_TIMING( "Main Loop" );

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }

   if ( networking->isBusy() )
   {
      if ( !networkStartBusyMillis )
      {
         networkStartBusyMillis = millis();
         loopMillis = FASTLOOP_PERIOD_MS;
      }
      else if ( millis() - networkStartBusyMillis  > NETWORK_ALLOWED_BUSY_MS )
      {
         PW_ERROR( "Timeout on webserver busy, rebooting..." );
         ESP.restart();
      }
   }
   else
   {
      networkStartBusyMillis = 0;
      loopMillis = LOOP_PERIOD_MS;

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

      START_TIMING( "takeSample" );
      measurement->takeSample();
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
   }

   // our target MS is our original millis at entry of this loop, plus
   // our sampling delay

   targetMillis += loopMillis;
   currentMillis = millis();

   // We may need to skip a sample(s) if we've executed too long in this loop

   while ( currentMillis >= targetMillis )
   {
      targetMillis += loopMillis;
   }

   deltaMillis = targetMillis - currentMillis;

   END_TIMING;

   PW_DEBUG( "Loop Delay %u",deltaMillis );

   delay( deltaMillis );
}
