#include <SD.h>
#include <FS.h>

#include "Storage.h"
#include "Networking.h"
#include "Config.h"
#include "UserIO.h"

#include "TemperatureModule.h"
#include "PowerModule.h"

#define WRITE_TEST_FILE "/test.dat"

Storage::Storage()
       : m_currentFileName(),
         m_networking( nullptr ),
         m_lastSentHour( 23 ),
         m_dailyUpdate( false ),
         m_storageOk( false )
{
   PW_DEBUG( "Storage::Storage()" );
   PW_MSG( "Storage Module Startup" );

   m_currentFileName[ 0 ] = 0;
}

Storage::~Storage()
{
   PW_DEBUG( "Storage::~Storage()" );
}

void printDirectory( File dir, int numTabs )
{

   while (true)
   {
      File entry = dir.openNextFile();
      if (!entry) {
         if (numTabs == 0)
         {
            PW_DEBUG( "Done" );
         }
         return;
      }

      String line;

      for (uint8_t i = 0; i < numTabs; i++)
      {
         line += "  ";
      }

      line += entry.name();

      if ( entry.isDirectory() )
      {
         PW_DEBUG( line.c_str() );
         printDirectory(entry, numTabs + 1);
      }
      else
      {
         line += "      ";
         line += String( entry.size(), DEC );
         PW_DEBUG( line.c_str() );
      }

      entry.close();
   }
}

void Storage::initialise( void )
{
   PW_DEBUG( "Storage::initialise" );

   if ( GET_REGISTRY_INT( BOARD_TYPE ) == TEMPERATURE_BOARD )
   {
      PW_MSG( "TBoard - not detecting SD card" );
      m_storageOk = true;
      return;
   }

   SD.begin();
   if( SD.cardType() == CARD_NONE )
   {
      PW_WARN( "Failed to initialise storage" );
   }
   else
   {
      uint32_t totalMiB, usedMiB, freeMiB;
      totalMiB = SD.totalBytes() / (1024 * 1024);
      usedMiB = SD.usedBytes() / (1024 * 1024);
      freeMiB = totalMiB - usedMiB;

      PW_MSG( "%u MiB available, %u MiB used", freeMiB,usedMiB );

      // Now perform a quick write test to see if ok, only if we can
      // write to a file and delete it do we consider SD card ok

      File file = SD.open( WRITE_TEST_FILE,FILE_WRITE );
      if ( file )
      {
         PW_DEBUG( "Opened %s ok",WRITE_TEST_FILE );
         if( !file.print( "test" ) )
         {
            PW_DEBUG( "Failed to write to %s",WRITE_TEST_FILE );
         }
         else
         {
            file.close();
            if ( SD.remove( WRITE_TEST_FILE ) )
            {
               m_storageOk = true;
               PW_MSG( "SD Card ok" );
            }
         }
      }

      if ( ! m_storageOk )
      {
         PW_WARN( "SD card has failed !" );
      }
#if 0
   File root = SD.open( "/" );
   printDirectory( root,0 );
   root.close();
#endif
   }
}

void  Storage::setNetworking( Networking *network )
{
   m_networking = network;

   PW_DEBUG( "Storage::setNetworking" );

   // Send an email if SD card is not OK

   if ( ! m_storageOk && GET_REGISTRY_INT( BOARD_TYPE ) == MASTER_BOARD )
   {
      char subject[ 128 ];
      snprintf( subject,128,"HP Monitoring : %s - SD Card Init Fault",m_networking->getLocalMDNSName().c_str() );

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"No message" );
   }
}

void  Storage::saveSampleToBackingStore( const Measurement::Sample &sample )
{
   // we won't store if the card isn't ok

   if ( !m_storageOk )
   {
      return;
   }

   // If we're a tboard then exit

   if ( GET_REGISTRY_INT( BOARD_TYPE ) == TEMPERATURE_BOARD )
   {
      return;
   }

   bool   isNewFile = false;
   bool   currentFileExists = false;
   struct tm timeInfo;
   char   fileName[ MAX_FILENAME + 1 ];

   localtime_r( &sample.m_sampleTime,&timeInfo );
   strftime( fileName,MAX_FILENAME,"/%Y%m%d.dat",&timeInfo );

   // If the filename is new, then we send out the existing file.

   if ( ! SD.exists( fileName ) )
   {
      isNewFile = true;
      if ( SD.exists( m_currentFileName ) )
      {
         currentFileExists = true;
      }
   }

   if ( isNewFile )
   {
      PW_MSG( "Will be creating %s",fileName );

      // As this is a new file, let's send previous file onwards

      if ( m_networking && strlen( m_currentFileName ) && currentFileExists )
      {
         char subject[ 128 ];

         snprintf( subject,128,"HP Monitoring : %s - Daily Readings",m_networking->getLocalMDNSName().c_str() );

         m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"Today's Final Results",m_currentFileName,false );

         if ( GET_REGISTRY_INT( SEND_DAILY_DEBUG ) == 1 && SD.exists( "/debug.log" ) )
         {
            m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"Debug log","/debug.log",false );
         }

         if ( GET_REGISTRY_INT( KEEP_DEBUG_LOG ) != 1 && SD.exists( "/debug.log" ) )
         {
            SD.remove( "/debug.log" );
         }
      }

      // set new current filename

      strcpy( m_currentFileName,fileName );
   }

   // set current filename if not already set
   if ( ! strlen( m_currentFileName ) )
   {
      strcpy( m_currentFileName,fileName );
   }

   File  file = SD.open( fileName,FILE_APPEND );

   if( !file )
   {
      PW_WARN( "Failed to open %s",fileName );
      m_storageOk = false;
   }
   else
   {
      char  line[ 128 ];
      String  hdrString = "Date,Time";
      String  dataString;

      strftime( line,32,"%Y%m%d,%H:%M:%S",&timeInfo );
      dataString = line;

      // Output header line if a new file

      for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
      {
         const TempSensor  *sensor;
         sensor = sample.m_tempSensors[ i ];

         if ( !sensor )
         {
            break;
         }

         if ( isNewFile )
         {
            snprintf( line,128,",%s",sensor->m_name );
            hdrString += line;
         }
         snprintf( line,128,",%.1f",sensor->m_temp );
         dataString += line;
      }

      for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
      {
         const PowerSensor  *sensor;
         sensor = sample.m_powerSensors[ i ];

         if ( !sensor )
         {
            break;
         }

         if ( isNewFile )
         {
            snprintf( line,128,",%s (power),%s (energy)",sensor->m_name,sensor->m_name );
            hdrString += line;
         }
         snprintf( line,128,",%.1f,%.1f",sensor->m_power,sensor->m_energy );
         dataString += line;
      }

      if ( isNewFile )
      {
         PW_DEBUG( hdrString.c_str() );
         m_storageOk = file.println( hdrString.c_str() );
      }

      PW_MSG( "store %s",dataString.c_str() );
      m_storageOk = file.println( dataString.c_str() );
      file.close();
   }

   if ( ! m_storageOk && m_networking )
   {
      char subject[ 128 ];
      snprintf( subject,128,"HP Monitoring : %s - Storage Failure",m_networking->getLocalMDNSName().c_str() );

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"Preventing further writes" );

      PW_ERROR( "Storage failure" );
   }
}

void  Storage::storeSample( const Measurement::Sample &sample )
{
   char     line[ 128 ];
   String   thermometerStr, powerStr;

   // First send data to emon

   int i = 0;
   while ( sample.m_tempSensors[ i ] )
   {
      const TempSensor  *sensor;
      sensor = &sample.m_actualTemps[ i ];

      if ( sensor->m_temp > TEMPERATURE_INVALID && sensor->m_emonFeedId != 0 && m_networking )
      {
         snprintf( line,128,"%-30s : %4.1f\n",sensor->m_name,sensor->m_temp );
         thermometerStr += line;

         m_networking->sendToEmonCMS( sensor->m_emonFeedId,sensor->m_temp );
      }

      i++;
   }

   i = 0;
   while ( sample.m_powerSensors[ i ] )
   {
      const PowerSensor  *sensor;
      sensor = &sample.m_actualPowers[ i ];

      if ( sensor->m_power > POWER_INVALID && sensor->m_emonFeedId != 0 && m_networking )
      {
         snprintf( line,128,"%-30s : Power [%5.1f W] Energy [%5.1f kWhr]\n",sensor->m_name,sensor->m_power, sensor->m_energy / 1000.0 );
         powerStr += line;

         m_networking->sendToEmonCMS( sensor->m_emonFeedId,sensor->m_power );
      }
      i++;
   }

   struct tm timeInfo;

   // perform daily update mail if needed

   localtime_r( &sample.m_sampleTime,&timeInfo );

   // if the dailyUpdate has been sent and the time is no longer in the
   // 5pm hour, then reset the update flag for next time

   if ( m_dailyUpdate && timeInfo.tm_hour != 17 )
   {
      PW_DEBUG( "Resetting daily update flag" );
      m_dailyUpdate = false;
   }
   else if ( timeInfo.tm_hour == 17 && !m_dailyUpdate && m_networking )
   {
      PW_MSG( "Sending daily update" );

      m_dailyUpdate = true;
      String updateStr;

      char subject[ 128 ], line[ 128 ];

      snprintf( subject,128,"Daily Update : %s [%s]",m_networking->getLocalMDNSName().c_str(),m_networking->getIPAddress().c_str() );
      snprintf( line,128,"Version : %s\n\n",VERSION_STR );

      updateStr += line;
      updateStr += thermometerStr;
      updateStr += powerStr;
      updateStr += "\n\n";

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,updateStr );
   }

   // save sample to store
   saveSampleToBackingStore( sample );
}

char  *Storage::getCurrentFileName()
{
   return m_currentFileName;
}

void  Storage::getStatus( char *line )
{
   if ( GET_REGISTRY_INT( BOARD_TYPE ) == TEMPERATURE_BOARD )
   {
      strncpy( line,"TBoard - no SD",MAX_OLED_COLUMNS );
   }
   else if ( m_storageOk )
   {
      uint32_t totalMiB, usedMiB, freeMiB;
      totalMiB = SD.totalBytes() / (1024 * 1024);
      usedMiB = SD.usedBytes() / (1024 * 1024);

      snprintf( line,MAX_OLED_COLUMNS,"SD Used %u of %u MiB",usedMiB,totalMiB );
   }
   else
   {
      strncpy( line,"SD Card Fault",MAX_OLED_COLUMNS );
   }
}
