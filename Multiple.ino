#include <time.h>

#include "utils.h"
#include "hwconfig.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "UserIO.h"
#include "Measurement.h"
#include "Config.h"
#include "Storage.h"
#include "Networking.h"
#include "WebServer.h"

// ----------------------------------------------------------------------

TemperatureModule *tempModule = nullptr;
PowerModule       *powerModule = nullptr;
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


// Initially testingLower which triggers when < threshold, i.e. 'key down'
// When we receive that then need to get next event when it goes above the
// threshold which we use to set button pressed.

int threshold = 40;
bool testingLower = true;
bool wasButtonPressed = false;

void gotTouchEvent()
{
  if ( !testingLower )
  {
     wasButtonPressed = true;
  }

  touchInterruptSetThresholdDirection( !testingLower );
  testingLower = !testingLower;
}

void  handleTouch1()
{
   PW_MSG( "Button-1 was pressed" );

   wasButtonPressed = false;

   String   msgString;
   char     message[ 128 ];

   Measurement::Sample  sample = measurement->getLastSample();
   snprintf( message,128,"Button sample\n\n"
                    "IP : %s [%s]\n"
                    "Free Bytes : %u\n\n"
                    "Time signature %u\n",
                    networking->getLocalMDNSName().c_str(),
                    networking->getIPAddress().c_str(),
                    sample.m_sampleTime,
                    ESP.getFreeHeap() );

   msgString = message;

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      if ( sample.m_tempSensors[ i ] )
      {
         snprintf( message,128,"%30s,%.1f\n",sample.m_tempSensors[ i ]->m_name,sample.m_tempSensors[ i ]->m_temp );
         msgString += message;
      }
   }

   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      if ( sample.m_powerSensors[ i ] )
      {
         snprintf( message,128,"%30s,%.1f\n",sample.m_powerSensors[ i ]->m_name,sample.m_powerSensors[ i ]->m_power,sample.m_powerSensors[ i ]->m_energy );
         msgString += message;
      }
   }

   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Current Data","No content",storageModule->getCurrentFileName() );
   networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug Log","No content","/debug.log" );
   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Btn Press",msgString.c_str() );

   userIO->updateLine( 1, "BT pressed" );
   delay( 2000 );

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
   touchInterruptSetThresholdDirection( testingLower );

   char subject[ 128 ];
   char initialMsg[ 128 ];

   snprintf( subject,128,"HP Monitoring : %s - Startup",networking->getLocalMDNSName().c_str() );
   snprintf( initialMsg,128,"Initial boot up completed\nVersion : [%s]\nIP : [%s]\nStarting monitoring...\n\n",VERSION_STR,networking->getIPAddress().c_str()  );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,initialMsg );
}

#define LOOP_PERIOD_MS  5000

void loop(void)
{
#if 1
   // TODO, wrap millis !

   /* Design decisions needed to build on the basics.

   1. Measure periodically (the temperature module will not allow readings
      at greater than 4/minute)

   2. Service the display/input periodically, but not at necessarily
      at the same rate.

   3. Check network functionality, restarting if possible - state machine

   6. Reset energy used every day ?

   Have ~ 170 KiB available currently for dynamic storage.  If we
   stored every 30s, then 24 hours would require 2880 samples, at 50
   bytes/sample that's ~ 141 KiB.

   */

   static uint32_t targetMillis = 0,deltaMillis,currentMillis;

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }
   targetMillis += LOOP_PERIOD_MS;

   measurement->takeSample();

   userIO->update();
   userIO->showNext();

   // was a button pressed ?
   if ( wasButtonPressed )
   {
      handleTouch1();
   }

   currentMillis = millis();

   // We may need to skip a sample if we've executed too long in this loop

   while ( currentMillis >= targetMillis )
   {
      targetMillis += LOOP_PERIOD_MS;
   }

   deltaMillis = targetMillis - currentMillis;

   delay( deltaMillis );
#else
   PW_MSG( "loop running on core %d",xPortGetCoreID() );
   delay( 60 * 1000 );
   PW_MSG( "waiting..." );

#endif
}
