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

// ----------------------------------------------------------------------

// The following are the device addresses - we should read this from a file

#if PW_WIFI == 1
#define MODBUS_HEATPUMP_ADDR     11
#define MODBUS_IMMERSION_ADDR    12
#else
#define MODBUS_HEATPUMP_ADDR     0x1
#define MODBUS_IMMERSION_ADDR    0x5
#endif

TemperatureModule *tempModule = nullptr;
PowerModule       *powerModule = nullptr;
Storage           *storageModule = nullptr;
UserIO            *userIO = nullptr;
Measurement       *measurement = nullptr;
Config            *config = nullptr;
Networking        *networking = nullptr;

int threshold = 40;
bool testingLower = true;
bool wasButtonPressed = false;

// Initially testingLower which triggers when < threshold, i.e. 'key down'
// When we receive that then need to get next event when it goes above the
// threshold which we use to set buttin pressed.

void gotTouchEvent()
{

  if ( !testingLower )
  {
     wasButtonPressed = true;
  }

  touchInterruptSetThresholdDirection( !testingLower );
  testingLower = !testingLower;
}

void newConfiguration( void )
{
   networking = new Networking;
   networking->startAccessPoint();

   PW_WARN( "Need to configure via SSID : %s",networking->getSSID().c_str() );
   PW_WARN( "Use %s/manager",networking->getMDNSName().c_str() );
   PW_WARN( "Or %s/manager",networking->getIPAddress().c_str() );

   while( 1 )
   {
      delay( 60 * 1000 );
      PW_DEBUG( "Waiting for configuration..." );
   }
}

void setup( void )
{
   // start serial port

   Serial.begin( 115200 );

   delay( 1000 );

   // Initialise our configuration

   config = Config::instance();

   selectHardware();

   // Is registry available, if not then we need to enter configuration
   // mode, i.e. networking with AP only with SSID HeatPump-Monitor. The
   // user must download a suitable config.dat to the device.

   if ( ! config->isRegistryAvailable() )
   {
      newConfiguration();
   }

   // Must have a valid configuration at this stage

   // prepare the OLED display for output

   userIO = new UserIO();
   userIO->initialise();

   char line[ MAX_OLED_COLUMNS + 1 ];

   if ( config->isRegistryAvailable() )
   {
      userIO->updateLine( 0,"Starting Networking..." );
   }
   else
   {
      userIO->updateLine( 0,"Waiting for config" );
   }

   networking = new Networking;
   networking->initialise();

   // Instantiate the storage module, and initialise it.  If the SD card
   // is not operational the storage module will not save data but at least
   // the system will continue to operate.

   storageModule = new Storage();
   storageModule->initialise();

   if ( !networking->isConnected() )
   {
      userIO->updateLine( 0,"WiFi not connected" );
   }
   else
   {
      snprintf( line,MAX_OLED_COLUMNS,"IP %s",networking->getIPAddress().c_str() );
      userIO->updateLine( 0,line );

      snprintf( line,MAX_OLED_COLUMNS,"%s",networking->getLocalMDNSName().c_str() );
      userIO->updateLine( 1,line );

      if ( networking->didAcquireNTP() )
      {
         struct tm   timeInfo;

         getLocalTime( &timeInfo );

         strftime( line,MAX_OLED_COLUMNS,"%d/%m/%y : %H:%M:%S",&timeInfo );
         userIO->updateLine( 2,line );
      }
      else
      {
         userIO->updateLine( 2,"No NTP !!" );
      }
   }

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

   // Instantiate the temperature collecting module

   tempModule = new TemperatureModule;
   tempModule->initialise();

   // Instantiate the power collecting module

   powerModule = new PowerModule;
   powerModule->registerSensor( HEATPUMP_POWER,MODBUS_HEATPUMP_ADDR,"Heatpump" );
   powerModule->registerSensor( IMMERSION_POWER,MODBUS_IMMERSION_ADDR,"Immersion" );

   powerModule->initialise();

   // Instantiate the measurement module, but don't initialise it just yet

   measurement = new Measurement( tempModule,powerModule,storageModule );
   userIO->setMeasurement( measurement );

   // let's tell storage we have networking available

   storageModule->setNetworking( networking );

   // Display status info before starting

   if ( storageModule->isSDCardOk() )
   {
      strcpy( line,"SD Card Ok" );
   }
   else
   {
      strcpy( line,"No SD Card" );
   }
   userIO->updateLine( 3,line );
   userIO->updateLine( 5,"Boot complete..." );

   delay( 5000 );

   // Can now initialise the measurement module

   PW_MSG( "Initialising measurement prior to loop" );

   measurement->initialise();

   // intialise touch
   // Touch ISR will be activated when reading is lower than the threshold

   touchAttachInterrupt( hwConfig->TouchButton1,gotTouchEvent,threshold );
   touchInterruptSetThresholdDirection( testingLower );

   char initialMsg[ 128 ];

   snprintf( initialMsg,128,"Initial boot up completed - [%s]\nIP : [%s]\nStarting monitoring...\n\n\Good luck !",VERSION_STR,networking->getIPAddress().c_str()  );

   networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                  "Heat Pump Monitoring - Startup",initialMsg );
}

#define LOOP_PERIOD_MS  5000

void loop(void)
{
#if 0
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

//   measurement->dumpMeasurements();

   // was a button pressed ?
   if ( wasButtonPressed )
   {
      PW_WARN( "Button was pressed" );
      wasButtonPressed = false;

      char  message[ 512 ];

      Measurement::Sample  sample = measurement->getLastSample();
      sprintf( message,"Button sample\n\n"
                       "IP : %s\n\n"
                       "Time signature %u\n\n"
                       "Heat Pump : Flow [ %.1f ] Return [ %.1f ] DT [ %.1f ]\n"
                       "Heating   : Flow [ %.1f ] Return [ %.1f ] DT [ %.1f ]\n"
                       "Outside   : [ %.1f ]\n\n"
                       "Heat Pump : Current [ %.0f W ] Total [ %.0f WHr ]\n"
                       "Immersion : Current [ %.0f W ] Total [ %.0f WHr ]\n\n"
                       "Free Bytes : %u\n",
                       networking->getIPAddress().c_str(),
                       sample.m_sampleTime,
                       sample.m_flowHP,sample.m_returnHP,( sample.m_flowHP - sample.m_returnHP ),
                       sample.m_flowHeating,sample.m_returnHeating,( sample.m_flowHeating - sample.m_returnHeating ),
                       sample.m_outside,
                       sample.m_powerHP,sample.m_energyHP,
                       sample.m_powerImmersion,sample.m_energyImmersion,
                       ESP.getFreeHeap() );

      networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Current Data",message,storageModule->getCurrentFileName() );
      networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug Log",message,"/debug.log" );
      networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Btn Press",message );

      userIO->updateLine( 1, "BT pressed" );
      delay( 2000 );

   }

   currentMillis = millis();

   // We may need to skip a sample if we've executed too long in this loop

   while ( currentMillis >= targetMillis )
   {
      targetMillis += LOOP_PERIOD_MS;
   }

   deltaMillis = targetMillis - currentMillis;

   delay( deltaMillis );
#endif
   PW_MSG( "loop" );
   delay( 60 * 1000 );
}
