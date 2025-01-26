#include <SD.h>
#include <FS.h>

#include "Storage.h"
#include "Networking.h"
#include "Config.h"
#include "UserIO.h"

#include "TemperatureModule.h"
#include "PowerModule.h"

#define WRITE_TEST_FILE "/test.dat"

#define INVALID_UPDATE_HOUR  25

// Delete files that are 40 days old as determined by their filenames

#define  DELETE_OLDER_THAN_SECONDS     (40 * 24 * 60 * 60)

Storage::Storage()
       : m_currentFileName(),
         m_networking( nullptr ),
         m_dailyUpdated( false ),
         m_dailyUpdateHour( INVALID_UPDATE_HOUR ),
         m_dailyModbusSent( 0 ),
         m_dailyModbusFailed( 0 ),
         m_dailyEmonSent( 0 ),
         m_dailyEmonFailed( 0 ),
         m_storageOk( false )
{
   PW_DEBUG( "Storage::Storage()" );
   PW_MSG( "Storage Module Startup" );

   int updateHour = GET_REGISTRY_INT( DAILY_EMAIL_HOUR );
   if ( updateHour >=0 && updateHour <= 23 )
   {
      m_dailyUpdateHour = updateHour;
      PW_DEBUG( "Setting update hour to %u",m_dailyUpdateHour );
   }

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
      char subject[ 128 ];
      snprintf( subject,128,"HP Monitoring : %s - SD Card Init Fault",m_networking->getLocalMDNSName().c_str() );

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"No message" );
   }
}

void  Storage::saveSampleToBackingStore( const Measurement::Sample &sample )
{
   // we won't store if the card isn't ok,or no card at all

   if ( !boardHasSDCard() || !m_storageOk )
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

         m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Daily Data","Today's Final Results",m_currentFileName,false );

         if ( SD.exists( DEBUG_LOG ) )
         {
            if ( GET_REGISTRY_INT( SEND_DAILY_DEBUG ) == 1 )
            {
               m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Debug Log","Debug log",DEBUG_LOG,false );
            }

            if ( GET_REGISTRY_INT( KEEP_DEBUG_LOG ) != 1 )
            {
               SD.remove( DEBUG_LOG );
            }
         }
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

      TemperatureModule::takeMutex();
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
      TemperatureModule::releaseMutex();

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

      for ( int i = 0; i < MAX_HP_REGISTERS; i++ )
      {
         const LGRegister  *lgReg;
         lgReg = sample.m_lgRegisters[ i ];

         if ( !lgReg )
         {
            break;
         }

         if ( isNewFile )
         {
            snprintf( line,128,",%s",lgReg->m_name );
            hdrString += line;
         }
         snprintf( line,128,",%.1f",lgReg->m_value );
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

void  Storage::updateEmon( const Measurement::Sample &sample )
{
   char     line[ 128 ];
   String   thermometerStr, powerStr,lgStr;

   // Can't update if no network

   if ( ! m_networking )
   {
      return;
   }

   // send any temperatures, power, heat pump and heat meter data

   int i = 0;
   const TempSensor  *tsensor;

   TemperatureModule::takeMutex();
   while ( (tsensor = sample.m_tempSensors[ i++ ] ) )
   {
      if ( tsensor->m_temp > TEMPERATURE_INVALID && tsensor->m_emonFeedId != 0 )
      {
         m_networking->sendToEmonCMS( tsensor->m_emonFeedId,tsensor->m_temp );
      }
   }
   TemperatureModule::releaseMutex();

   i = 0;
   const PowerSensor *sensor;
   while( ( sensor = sample.m_powerSensors[ i++ ] ) )
   {
      if ( sensor->m_power > POWER_INVALID && sensor->m_emonFeedId != 0 )
      {
         m_networking->sendToEmonCMS( sensor->m_emonFeedId,sensor->m_power );
      }
   }

   i = 0;
   const LGRegister *lgReg;
   while( ( lgReg = sample.m_lgRegisters[ i++ ] ) )
   {
      if ( lgReg->m_emonFeedId != 0 && m_networking )
      {
         m_networking->sendToEmonCMS( lgReg->m_emonFeedId,lgReg->m_value );
      }
   }

   i = 0;
   const HeatMeterSensor *hmSensor;
   while( ( hmSensor = sample.m_heatMeterSensors[ i++ ] ) )
   {
      if ( hmSensor->m_emonPowerId && hmSensor->m_emonFlowId )
      {
         float_t flowRate, power;

         if ( hmSensor->m_power == HM_POWER_ERROR )
         {
            flowRate = 0;
            power = -1;
         }
         else
         {
            flowRate = hmSensor->m_flowRate;
            power = hmSensor->m_power;
         }

         m_networking->sendToEmonCMS( hmSensor->m_emonFlowId,flowRate );
         m_networking->sendToEmonCMS( hmSensor->m_emonPowerId,power );
      }
   }
}

void  Storage::storeSample( const Measurement::Sample &sample )
{
   // First send data to emon

   updateEmon( sample );

   // save sample to storage if we have it

   saveSampleToBackingStore( sample );

   // perform daily update mails if needed

   if ( m_dailyUpdateHour == INVALID_UPDATE_HOUR )
   {
      return;
   }

   struct tm timeInfo;
   localtime_r( &sample.m_sampleTime,&timeInfo );

   // if the dailyUpdate has been sent and the time is no longer in the
   // hour, then reset the update flag for next time

   if ( m_dailyUpdated && timeInfo.tm_hour != m_dailyUpdateHour )
   {
      PW_DEBUG( "Resetting daily update flag" );
      m_dailyUpdated = false;
   }
   else if ( timeInfo.tm_hour == m_dailyUpdateHour && !m_dailyUpdated && m_networking )
   {
      char     line[ 128 ];
      String   thermometerStr,powerStr,lgStr,commsStr;

      PW_MSG( "Sending daily update" );

      int i = 0;
      TemperatureModule::takeMutex();
      while ( sample.m_tempSensors[ i ] )
      {
         const TempSensor  *sensor = sample.m_tempSensors[ i ];

         PW_DEBUG( "TS %p %s %f %d",sensor,sensor->m_name,sensor->m_temp,sensor->m_emonFeedId );
         if ( sensor->m_temp > TEMPERATURE_INVALID && sensor->m_emonFeedId != 0 )
         {
            snprintf( line,sizeof(line),"%-30s : %4.1f\n",sensor->m_name,sensor->m_temp );
            thermometerStr += line;
         }

         i++;
      }
      TemperatureModule::releaseMutex();

      i = 0;
      const PowerSensor *sensor;
      while( ( sensor = sample.m_powerSensors[ i++ ] ) )
      {
         if ( sensor->m_power > POWER_INVALID && sensor->m_emonFeedId != 0 )
         {
            snprintf( line,sizeof(line),"%-30s : Power [%5.1f W] Energy [%5.1f kWhr]\n",sensor->m_name,sensor->m_power, sensor->m_energy / 1000.0 );
            powerStr += line;
         }
      }

      // modbus, then emon

      uint32_t   sends,fails;
      float_t    percentSent = 100;

      getModbusStats( &sends,&fails );

      sends -= m_dailyModbusSent;
      fails -= m_dailyModbusFailed;

      m_dailyModbusSent += sends;
      m_dailyModbusFailed += fails;

      if ( sends )
      {
         if ( fails )
         {
            percentSent = (100.0 * sends) / (fails + sends);
         }

         snprintf( line,sizeof(line),"\nModbus Sent: %u, Failed: %u - (%.1f %% Ok)\n",sends,fails,percentSent );
         commsStr += line;
      }

      Networking::Status state = m_networking->getStatus();

      state.emonSent -= m_dailyEmonSent;
      state.emonFails -= m_dailyEmonFailed;

      m_dailyEmonSent += state.emonSent;
      m_dailyEmonFailed += state.emonFails;

      if ( state.emonSent )
      {
         percentSent = 100;
         if ( state.emonFails )
         {
            percentSent = (100.0 * state.emonSent) / (state.emonFails + state.emonSent);
         }

         snprintf( line,sizeof(line),"EmonCMS Sent: %u, Failed: %u - (%.1f %% Ok)\n",state.emonSent,state.emonFails,percentSent );

         commsStr += line;
      }

      m_dailyUpdated = true;
      String updateStr;

      char subject[ 64 ];

      snprintf( subject,sizeof(subject),"Daily Update : %s [%s]",m_networking->getLocalMDNSName().c_str(),m_networking->getIPAddress().c_str() );
      snprintf( line,sizeof(line),"Version : %s\n\n",VERSION_STR );

      updateStr += line;
      updateStr += thermometerStr;
      updateStr += powerStr;
      updateStr += commsStr;
      updateStr += "\n\n";

      // Send LG data if we have it, otherwise simple email
      if ( Config::instance()->getSPIFFS()->exists ( LGSTATUS_LOG ) )
      {
         if ( m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,updateStr,LGSTATUS_LOG,true ) )
         {
            Config::instance()->getSPIFFS()->remove( LGSTATUS_LOG );
         }
      }
      else
      {
         m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,updateStr );
      }
   }
}

char  *Storage::getCurrentFileName()
{
   return m_currentFileName;
}

void  Storage::getStatus( char *line )
{
   if ( ! boardHasSDCard() )
   {
      strncpy( line,"No Fitted SD",MAX_OLED_COLUMNS );
   }
   else if ( m_storageOk )
   {
      uint32_t totalMiB, usedMiB, freeMiB;
      totalMiB = SD.totalBytes() / (1024 * 1024);
      usedMiB = SD.usedBytes() / (1024 * 1024);

      snprintf( line,MAX_OLED_COLUMNS,"SD: %u MiB free",totalMiB - usedMiB );
   }
   else
   {
      strncpy( line,"SD Card Fault",MAX_OLED_COLUMNS );
   }
}
