#include <time.h>

#include "utils.h"
#include "hwconfig.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "HeatPumpModule.h"
#include "UserIO.h"
#include "Measurement.h"
#include "Config.h"
#include "Storage.h"
#include "Networking.h"
#include "WebServer.h"

// ----------------------------------------------------------------------

TemperatureModule *tempModule = nullptr;
PowerModule       *powerModule = nullptr;
HeatPumpModule    *heatPumpModule = nullptr;
Storage           *storageModule = nullptr;
UserIO            *userIO = nullptr;
Measurement       *measurement = nullptr;
Config            *config = nullptr;
Networking        *networking = nullptr;

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


// Initially testingBtnLower which triggers when < threshold, i.e. 'key down'
// When we receive that then need to get next event when it goes above the
// threshold which we use to set button pressed.

int threshold = 40;
bool testingBtnLower = true;
bool wasButton1Pressed = false;
bool wasButton2Pressed = false;

void gotTouchEvent()
{
  if ( !testingBtnLower )
  {
     wasButton1Pressed = true;
  }

  touchInterruptSetThresholdDirection( !testingBtnLower );
  testingBtnLower = !testingBtnLower;
}

extern bool getHPData();

void  handleTouch1()
{
   PW_MSG( "Button-1 was pressed" );

   userIO->clear();
   userIO->updateLine( 1, "BT pressed" );

   wasButton1Pressed = false;

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

   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      if ( sample.m_powerSensors[ i ] )
      {
         const PowerSensor  *sensor;
         sensor = &sample.m_actualPowers[ i ];

         snprintf( message,128,"%30s,%.1f\n",sensor->m_name,sensor->m_power,sensor->m_energy );
         msgString += message;
      }
   }
   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Btn Press",msgString.c_str() );

   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Current Data","Sample Data",storageModule->getCurrentFileName() );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug Log","Debug log","/debug.log" );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"HP Modbus","Modbus Data","/hpmodbus.log" );
}

void setup( void )
{
   // start serial port

   Serial.begin( 115200 );

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

   // Instantiate the temperature collecting module

   tempModule = new TemperatureModule;
   tempModule->initialise();

   // Instantiate the power collecting module

   powerModule = new PowerModule;
   powerModule->initialise();

   // Instantiate the heat pump collecting module

   heatPumpModule = new HeatPumpModule();
   heatPumpModule->initialise();

   // Instantiate the measurement module, but don't initialise it just yet

   measurement = new Measurement( tempModule,powerModule,storageModule );
   userIO->setMeasurement( measurement );

   // let's tell storage we have networking available

   storageModule->setNetworking( networking );

   delay( 5000 );

   // Can now initialise the measurement module

   PW_MSG( "Initialising measurement prior to loop" );

   measurement->initialise();

   // intialise touch
   // Touch ISR will be activated when reading is lower than the threshold

   touchAttachInterrupt( hwConfig->TouchButton1,gotTouchEvent,threshold );
   touchInterruptSetThresholdDirection( testingBtnLower );

   char subject[ 128 ];
   char initialMsg[ 128 ];

   snprintf( subject,128,"HP Monitoring : %s - Startup",networking->getLocalMDNSName().c_str() );
   snprintf( initialMsg,128,"Initial boot up completed\nVersion : [%s]\nIP : [%s]\nStarting monitoring...\n\n",VERSION_STR,networking->getIPAddress().c_str()  );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,initialMsg );
}

#define LOOP_PERIOD_MS  5000

bool  simulatedBtn1Press = false;

uint32_t hpSamples = 0;
uint32_t hpErrors = 0;

void loop(void)
{
   static uint32_t targetMillis = 0,deltaMillis,currentMillis,lastHpMillis = 0;
   static uint32_t loops = 1;

   START_TIMING( "Main Loop" );

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }

   if ( !userIO->isFirmwareUpdateInProgress() )
   {
      // was button 1 pressed, or we may simulate it

      simulatedBtn1Press = false;
      if ( loops == 5 )
      {
         if ( GET_REGISTRY_INT( BTN_PRESS_ON_LOOP40 ) == 1 )
         {
            simulatedBtn1Press = true;
         }
         loops = 0;
      }
      else if ( loops > 0 )
      {
         loops++;
      }

      if ( wasButton1Pressed || simulatedBtn1Press )
      {
         START_TIMING( "Handle Touch1" );
         handleTouch1();
         END_TIMING;
      }

      START_TIMING( "takeSample" );
      measurement->takeSample();
      END_TIMING;

      START_TIMING( "UserIO Update" );
      userIO->update();
      END_TIMING;

      START_TIMING( "UserIO ShowNext" );
      userIO->showNext();
      END_TIMING;

      if ( GET_REGISTRY_INT( LG_MODBUS ) == 1 && (targetMillis - lastHpMillis) > 40000  )
      {
         START_TIMING( "LG Modbus" );
         hpSamples++;
         if ( ! getHPData() )
         {
            hpErrors++;
         }
         END_TIMING;

         char buff[ 64 ];
         sprintf( buff,"t: %u - e: %u",hpSamples,hpErrors );

         userIO->updateLine( 5,buff );
         lastHpMillis = targetMillis;
      }
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

   PW_DEBUG( "Loop Delay %u",deltaMillis );

   delay( deltaMillis );
}
