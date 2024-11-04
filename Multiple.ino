#include <SD.h>
#include <FS.h>

#include <time.h>

#include "utils.h"
#include "hwconfig.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "HeatPumpModule.h"
#include "HeatMeter.h"
#include "UserIO.h"
#include "Measurement.h"
#include "Config.h"
#include "Storage.h"
#include "Networking.h"
#include "WebServer.h"
#include "LGHeatPump.h"

// ---------------------------------------------------------------------

TemperatureModule *tempModule = nullptr;
PowerModule       *powerModule = nullptr;
HeatPumpModule    *heatPumpModule = nullptr;
Storage           *storageModule = nullptr;
HeatMeterModule   *heatMeterModule = nullptr;
UserIO            *userIO = nullptr;
Measurement       *measurement = nullptr;
Config            *config = nullptr;
Networking        *networking = nullptr;
LGHeatPump        *lgThermaV = nullptr;

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

int  touchThreshold = 35;
bool wasButton1Pressed = false;
bool wasButton2Pressed = false;
bool userIOHoldScreen = false;   // if true then don't cycle screens

void IRAM_ATTR gotTouch1Event()
{
  wasButton1Pressed = true;
}

void IRAM_ATTR gotTouch2Event()
{
  wasButton2Pressed = true;
}

void  handleTouch1()
{
   PW_MSG( "Button-1 was pressed" );

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
// Create/initialise all modules prior to main loop

void setup( void )
{
   // start serial port, if the GPIO controlling serial on boot behaviour is low,
   // i.e. no serial on boot then we can reconfigure uart0 (Serial) to have
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

      userIO->updateLine( 5," Rebooting in 5s" );
      delay( 5000 );
      ESP.restart();
   }
   else
   {
      clearFailedRebootCount();
   }

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

   // Give webserver access to userIO

   networking->getWebServer()->setUserIO( userIO );

   // did we boot with button down pressed ?

   if ( GET_REGISTRY_INT( BOARD_TYPE ) == MASTER_BOARD )
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

   // Instantiate the power collecting module

   powerModule = new PowerModule;
   powerModule->initialise();

   // Instantiate the heat pump collecting module if active and we have
   // a valid modbus

   if ( GET_REGISTRY_INT( LG_MODBUS ) > 0 && powerModule->getModbus() )
   {
      lgThermaV = new LGHeatPump( powerModule->getModbus() );
      lgThermaV->initialise();
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

   char subject[ 128 ];
   char initialMsg[ 128 ];

   snprintf( subject,128,"HP Monitoring : %s - Startup",networking->getLocalMDNSName().c_str() );
   snprintf( initialMsg,128,"Initial boot up completed\nVersion : [%s]\nIP : [%s]\nStarting monitoring...\n\n",VERSION_STR,networking->getIPAddress().c_str()  );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,initialMsg );

   if ( SD.exists ( LGREGISTERS_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"HP Modbus Registers","Modbus regs",LGREGISTERS_LOG ) )
      {
         SD.remove( LGREGISTERS_LOG);
      }
   }

   if ( SD.exists ( LGMODBUS_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"HP Modbus Log","Modbus Logs",LGMODBUS_LOG ) )
      {
         SD.remove( LGMODBUS_LOG );
      }
   }

   if ( SD.exists ( DEBUG_LOG ) )
   {
      if ( networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug log","Debug Logs",DEBUG_LOG ) )
      {
         if ( GET_REGISTRY_INT( KEEP_DEBUG_LOG ) != 1 )
         {
            SD.remove( DEBUG_LOG );
         }
      }
   }

   if ( config->getSPIFFS()->exists( LGSTATUS_LOG ) )
   {
      networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"LG Event Log","Event Log",LGSTATUS_LOG,true );
   }
}

// ---------------------------------------------------------------------
// Loop

#define LOOP_PERIOD_MS  5000

void loop(void)
{
   static uint32_t targetMillis = 0,deltaMillis,currentMillis;
   static uint32_t loops = 0;

   loops++;

   START_TIMING( "Main Loop" );

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }

   if ( !userIO->isFirmwareUpdateInProgress() )
   {
      touch_value_t  touchVal = touchRead( hwConfig->TouchButton1 );
      PW_MSG( "Touch value %d",touchVal );
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

   targetMillis += LOOP_PERIOD_MS;
   currentMillis = millis();

   // We may need to skip a sample(s) if we've executed too long in this loop

   while ( currentMillis >= targetMillis )
   {
      targetMillis += LOOP_PERIOD_MS;
   }

   deltaMillis = targetMillis - currentMillis;

   END_TIMING;

   PW_MSG( "Loop Delay %u",deltaMillis );

   delay( deltaMillis );
}
