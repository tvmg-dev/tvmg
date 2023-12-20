#include <time.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

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

#if PW_WIFI == 1
DeviceAddress heatpumpFlowThermometer = { 0x28,0x48,0xFB,0x81,0xE3,0x71,0x3C,0x06 };
#else
DeviceAddress heatpumpFlowThermometer = { 0x28,0x30,0x21,0x94,0x97,0x0D,0x03,0x10 };
#endif

DeviceAddress heatpumpReturnThermometer = { 0x28,0x26,0x11,0x94,0x97,0x0A,0x03,0x13 };
DeviceAddress heatingFlowThermometer = { 0x28,0x9A,0x11,0x94,0x97,0x02,0x03,0x1F };
DeviceAddress heatingReturnThermometer = { 0x28,0xD4,0x39,0x94,0x97,0x03,0x03,0x7B };
DeviceAddress outsideThermometer = { 0x28,0x4E,0X5F,0x94,0x97,0x03,0x03,0x88 };

#define  HEATPUMP_FLOW_THERM_CAL    0.12
#define  HEATPUMP_RETURN_THERM_CAL  0.38
#define  HEATING_FLOW_THERM_CAL     0.25
#define  HEATING_RETURN_THERM_CAL   0.12
#define  OUTSIDE_THERM_CAL          0.0

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

void setup(void)
{
   // start serial port

   Serial.begin( 115200 );

   delay( 1000 );

   Serial.println( "start" );

   // Initialise our configuration

   config = Config::instance();

   // Instantiate the storage module, and initialise it.  If the SD card
   // is not operational the storage module will not save data but at least
   // the system will continue to operate.

   storageModule = new Storage();
   storageModule->initialise();

   // Instantiate the temperature collecting module

   tempModule = new TemperatureModule;
   tempModule->registerSensor( HEATPUMP_FLOW_THERM,heatpumpFlowThermometer,"Heat Pump Flow",HEATPUMP_FLOW_THERM_CAL );
   tempModule->registerSensor( HEATPUMP_RETURN_THERM,heatpumpReturnThermometer,"Heat Pump Return",HEATPUMP_RETURN_THERM_CAL );
   tempModule->registerSensor( HEATING_FLOW_THERM,heatingFlowThermometer,"Heating Flow",HEATING_FLOW_THERM_CAL );
   tempModule->registerSensor( HEATING_RETURN_THERM,heatingReturnThermometer,"Heating Return",HEATING_RETURN_THERM_CAL );
   tempModule->registerSensor( OUTSIDE_THERM,outsideThermometer,"Outside",OUTSIDE_THERM_CAL );

   tempModule->initialise();

   // Instantiate the power collecting module

   powerModule = new PowerModule;
   powerModule->registerSensor( HEATPUMP_POWER,MODBUS_HEATPUMP_ADDR,"Heatpump" );
   powerModule->registerSensor( IMMERSION_POWER,MODBUS_IMMERSION_ADDR,"Immersion" );

   powerModule->initialise();

   // Instantiate the measurement module, but don't initialise it just yet

   measurement = new Measurement( tempModule,powerModule,storageModule );

   // prepare the OLED display

   userIO = new UserIO( measurement );
   userIO->initialise();

   char line[ MAX_OLED_COLUMNS + 1 ];
   char ipAddr[ 20 ];

   strncpy( line,"Initial boot delay...",MAX_OLED_COLUMNS );
   userIO->updateLine( 0,line );

   delay( GET_REGISTRY_INT( BOOT_DELAY ) );

   // now networking...

   strncpy( line,"Starting Networking...",MAX_OLED_COLUMNS );
   userIO->updateLine( 1,line );

   networking = new Networking;

   networking->initialise();
   if ( !networking->isConnected() )
   {
      userIO->updateLine( 1,"WiFi not connected",false );
   }
   else
   {
      networking->getIPAddress( ipAddr );
      snprintf( line,MAX_OLED_COLUMNS,"IP %s",ipAddr );
      userIO->updateLine( 1,line,false );

      if ( networking->didAcquireNTP() )
      {
         struct tm   timeInfo;

         getLocalTime( &timeInfo );

         strftime( line,MAX_OLED_COLUMNS,"%d/%m/%y : %H:%M:%S",&timeInfo );
         userIO->updateLine( 2,line );
      }
      else
      {
         strncpy( line,"No NTP !!",MAX_OLED_COLUMNS );
         userIO->updateLine( 2,line );
      }
   }

   // If we don't have NTP, then we reboot here - ping an email too.

   if ( !networking->didAcquireNTP() )
   {
      strncpy( line,"Reboot in 5s",MAX_OLED_COLUMNS );
      userIO->updateLine( 5,line );

      char msg[ 128 ];
      snprintf( msg,128,"Failed to aquire NTP - rebooting",VERSION_STR  );

      networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
                  "Heat Pump Monitoring - Startup NTP fault",msg );

      delay( 5000 );
      ESP.restart();
   }

   // let's tell storage we have networking available

   storageModule->setNetworking( networking );

   // Display status info before starting

   delay( 5000 );
   userIO->clear();

   PW_MSG( "Boot Summary :-" );

   if ( networking->isConnected() )
   {

      snprintf( line,MAX_OLED_COLUMNS,"IP %s",ipAddr );
   }
   else
   {
      strcpy( line,"No Network" );
   }
   userIO->updateLine( 0,line );

   if ( networking->didAcquireNTP() )
   {
      struct tm   timeInfo;

      getLocalTime( &timeInfo );

      strftime( line,MAX_OLED_COLUMNS,"%d/%m/%y : %H:%M:%S",&timeInfo );
   }
   else
   {
      strcpy( line,"NTP : Inactive" );
   }
   userIO->updateLine( 1,line );

   if ( storageModule->isSDCardOk() )
   {
      strcpy( line,"SD Card Ok" );
   }
   else
   {
      strcpy( line,"No SD Card" );
   }
   userIO->updateLine( 2,line );
   userIO->updateLine( 5,"Boot complete..." );

   delay( 5000 );

   // Can now initialise the measurement module

   PW_MSG( "Initialising measurement prior to loop" );

   measurement->initialise();

   // intialise touch
   // Touch ISR will be activated when reading is lower than the threshold

   touchAttachInterrupt( TOUCH_BUTTON_1,gotTouchEvent,threshold );
   touchInterruptSetThresholdDirection( testingLower );

   char initialMsg[ 128 ];

   snprintf( initialMsg,128,"Initial boot up completed - [%s]\nIP : [%s]\nStarting monitoring...\n\n\Good luck !",VERSION_STR,ipAddr  );

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
      char  ipAddr[ 20 ];

      networking->getIPAddress( ipAddr );

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
                       ipAddr,
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
