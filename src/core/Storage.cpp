#include <SD.h>
#include <FS.h>

#include "Storage.h"
#include "src/network/Networking.h"
#include "src/config/Config.h"

#include "src/sensors/TemperatureModule.h"
#include "src/sensors/PowerModule.h"
#include "src/sensors/ShellyPM.h"

#define WRITE_TEST_FILE "/test.dat"

// Delete files that are 40 days old as determined by their filenames

#define  DELETE_OLDER_THAN_SECONDS     (40 * 24 * 60 * 60)

Storage::Storage()
       : m_currentFileName(),
         m_networking( nullptr ),
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

void Storage::initialise( void )
{
   PW_DEBUG( "Storage::initialise" );

   if ( ! boardHasSDCard() )
   {
      PW_MSG( "Not detecting SD card" );
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
      else
      {
         removeOldSamples();
      }
   }
}

void Storage::removeOldSamples()
{
   // clean up root directory

   time_t currentTime;
   time( &currentTime );

   File root = SD.open( "/" );

   while (true)
   {
      File entry = root.openNextFile();
      if (!entry)
      {
         break;
      }

      if ( !entry.isDirectory() )
      {
         String fileName = entry.name();
         entry.close();

         // Shouldn't have filenames > 12 (8.3), but a bug introduced one so
         // ignore - try and delete, buf fails

         if ( fileName.length() > 12 )
         {
            PW_DEBUG( "Excessive filename length : %d",fileName.length() );
            SD.remove( fileName );
            continue;
         }

         // Get current time and then convert to a tm struct based on the
         // filename which is in yyyymmdd.dat format.  So any other files
         // will not get auto-cleaned here !

         struct tm timeInfo;
         localtime_r( &currentTime,&timeInfo );

         char year[ 5 ],month[ 3 ],day[ 3 ];

         year[ 0 ] = 0;
         month[ 0 ] = 0;
         day[ 0 ] = 0;

         uint16_t iyear,imonth,iday;

         sscanf( fileName.c_str(),"%4s%2s%2s",year,month,day );

         iday = atoi( day );
         imonth = atoi( month );
         iyear = atoi( year );

         // Basic sanity test
         if ( iyear < 2023 || iday > 31 || imonth > 12 )
         {
            PW_DEBUG( "Ignoring %s",fileName );
            continue;
         }

         timeInfo.tm_year = iyear - 1900;
         timeInfo.tm_mon = imonth - 1;
         timeInfo.tm_mday = iday;

         time_t   fileTime = mktime( &timeInfo );
         time_t   timeDiff = currentTime - fileTime;

         if ( timeDiff < 0 || timeDiff > DELETE_OLDER_THAN_SECONDS )
         {
            String fullPath = "/" + fileName;
            PW_MSG( "Deleting %s",fullPath.c_str() );
            SD.remove( fullPath.c_str() );
         }
         else
         {
            PW_DEBUG( "SD : %s",fileName.c_str() );
         }
      }
   }

   root.close();
}

void  Storage::setNetworking( Networking *network )
{
   m_networking = network;

   PW_DEBUG( "Storage::setNetworking" );

   // Send an email if SD card is not OK

   if ( boardHasSDCard() && ! m_storageOk )
   {
      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Monitoring Storage Failure","SD Card Initialisation Fault" );
   }
}

void  Storage::storeSample( const Measurement::Sample &sample,bool isNewFile )
{
   // we won't store if the card isn't ok,or no card at all

   if ( !boardHasSDCard() || !m_storageOk )
   {
      return;
   }

   bool   currentFileExists = false;
   struct tm timeInfo;
   static bool haveSkippedFirstSample = false;
   char   fileName[ MAX_FILENAME + 1 ];

   localtime_r( &sample.m_sampleTime,&timeInfo );
   strftime( fileName,MAX_FILENAME,"/%Y%m%d.dat",&timeInfo );

   // If we're closing the file then send old file, debug log & delete older files

   if ( isNewFile )
   {
      PW_MSG( "Will be creating %s",fileName );

      // As this is a new file, let's send previous file onwards

      if ( m_networking && strlen( m_currentFileName ) && SD.exists( m_currentFileName ) )
      {
         char subject[ 128 ];

         m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Daily Data","Today's Final Results",m_currentFileName,true );
      }

      // set new current filename

      strcpy( m_currentFileName,fileName );

      // Let's remove old files if present

      removeOldSamples();
   }

   // set current filename if not already set
   if ( ! strlen( m_currentFileName ) )
   {
      strcpy( m_currentFileName,fileName );
   }

   File  file = SD.open( m_currentFileName,FILE_APPEND );

   if( !file )
   {
      PW_WARN( "Failed to open %s",m_currentFileName );
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

      for ( int i = 0; i < sample.m_tempSensors.size(); i++ )
      {
         const TempSensor  &sensor = sample.m_tempSensors[ i ];

         if ( isNewFile )
         {
            snprintf( line,sizeof(line),",%s",getSensorName( THERM,sensor.m_id ).c_str() );
            hdrString += line;
         }
         snprintf( line,sizeof(line),",%.1f",sensor.m_temp );
         dataString += line;
      }

      for ( int i = 0; i < sample.m_powerSensors.size(); i++ )
      {
         const PowerSensor &sensor = sample.m_powerSensors[ i ];

         if ( isNewFile )
         {
            const char *name = getSensorName( POWER,sensor.m_id ).c_str();
            snprintf( line,sizeof(line),",%s (power),%s (energy)",name,name );
            hdrString += line;
         }
         snprintf( line,sizeof(line),",%.1f,%.1f",sensor.m_power,sensor.m_energy );
         dataString += line;
      }

      for ( int i = 0; i < sample.m_shellyPowerSensors.size(); i++ )
      {
         const ShellyPowerSensor &sensor = sample.m_shellyPowerSensors[ i ];

         if ( isNewFile )
         {
            const char *name = getSensorName( SHELLYPM,sensor.m_id ).c_str();
            snprintf( line,sizeof(line),",%s (power),%s (energy)",name,name );
            hdrString += line;
         }
         snprintf( line,sizeof(line),",%.1f,%.1f",sensor.m_power,sensor.m_energy );
         dataString += line;
      }

      for ( int i = 0; i < sample.m_lgRegisters.size(); i++ )
      {
         const LGHeatPump::LGRegister &lgReg = sample.m_lgRegisters[ i ];

         if ( isNewFile )
         {
            snprintf( line,sizeof(line),",%s",getSensorName( HEATPUMP,lgReg.m_id ).c_str() );
            hdrString += line;
         }
         snprintf( line,sizeof(line),",%.1f",lgReg.m_value );
         dataString += line;
      }

      if ( isNewFile )
      {
         PW_DEBUG( hdrString.c_str() );
         m_storageOk = file.println( hdrString.c_str() );
      }

      // We ignore the first sample following a reboot as it is most likely
      // incomplete, would be better to use sensor's valid flag

      if ( !haveSkippedFirstSample )
      {
         haveSkippedFirstSample = true;
         PW_MSG( "skip first sample" );
      }
      else
      {
         PW_MSG( "store %s",dataString.c_str() );
         m_storageOk = file.println( dataString.c_str() );
      }
      file.close();
   }

   if ( ! m_storageOk && m_networking )
   {
      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Monitoring Storage Failure","Preventing further writes" );

      PW_ERROR( "Storage failure" );
   }
}

char  *Storage::getCurrentFileName()
{
   return m_currentFileName;
}

void  Storage::getStatus( char *line,int lineSize )
{
   if ( ! boardHasSDCard() )
   {
      strncpy( line,"No Fitted SD",lineSize );
   }
   else if ( m_storageOk )
   {
      uint32_t totalMiB, usedMiB, freeMiB;
      totalMiB = SD.totalBytes() / (1024 * 1024);
      usedMiB = SD.usedBytes() / (1024 * 1024);

      snprintf( line,lineSize,"SD: %u MiB free",totalMiB - usedMiB );
   }
   else
   {
      strncpy( line,"SD Card Fault",lineSize );
   }
}

