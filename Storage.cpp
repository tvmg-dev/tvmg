#include "Storage.h"
#include "Networking.h"
#include "Config.h"

#include <SD.h>
#include <FS.h>

#include <EMailSender.h>

#define WRITE_TEST_FILE "/test.dat"

bool Storage::m_sdCardOk = false;

Storage::Storage()
       : m_currentFileName(),
         m_networking( nullptr ),
         m_lastSentHour( 23 ),
         m_dailyUpdate( false )
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
               m_sdCardOk = true;
               PW_MSG( "SD Card ok" );
            }
         }
      }

      if ( ! m_sdCardOk )
      {
         PW_WARN( "SD card has failed !" );
      }
      else if ( SD.exists( "/debug.log" ) )
      {
         if ( ! SD.remove( "/debug.log" ) )
         {
            PW_WARN( "Failed to remove log file, likely SD error" );
         }
      }
   }
}

bool  Storage::isSDCardOk( void )
{
   return m_sdCardOk;
}

void  Storage::setNetworking( Networking *network )
{
   m_networking = network;

   PW_DEBUG( "Storage::setNetworking" );

   // Send an email if SD card is not OK

   if ( ! m_sdCardOk )
   {
      char subject[ 128 ];

      snprintf( subject,128,"SD Card Init Fault [%s]",m_networking->getIPAddress().c_str() );

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"No message" );
   }
}

void  Storage::storeSample( const Measurement::Sample &sample )
{
   struct tm timeInfo;
   char   fileName[ MAX_FILENAME + 1 ];

   // Before we try and store, let's send to emoncms & a daily update

   localtime_r( &sample.m_sampleTime,&timeInfo );
   strftime( fileName,MAX_FILENAME,"/%Y%m%d.dat",&timeInfo );

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

      char updateMsg[ 512 ];

      snprintf( updateMsg,512,"Version [%s]\n"
                              "IP Address : %s\n\n"
                              "Heat Pump : Flow [ %.1f ] Return [ %.1f ] Power [ %.1f ] W, Energy [ %.1f ] kWhr\n"
                              "Heating : Flow [ %.1f ] Return [ %.1f ]\n"
                              "Immersion :  Power [ %.1f ] W, Energy [ %.1f ] kWhr\n"
                              "Outside : [ %.1f] \n\n",
                              VERSION_STR,
                              m_networking->getIPAddress().c_str(),
                              sample.m_flowHP,sample.m_returnHP,sample.m_powerHP,sample.m_energyHP / 1000,
                              sample.m_flowHeating,sample.m_returnHeating,
                              sample.m_powerImmersion,sample.m_energyImmersion / 1000,
                              sample.m_outside );

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),
               "Heat Pump Monitoring - Daily Update",updateMsg );
   }

   uint32_t hpFlowFeedId = 476741;
   uint32_t hpReturnFeedId = 476740;
   uint32_t heatingFlowFeedId = 476738;
   uint32_t heatingReturnFeedId = 476739;
   uint32_t outsideFeedId = 476990;
   uint32_t hpPowerFeedId = 476995;
   uint32_t immersionPowerFeedId = 476997;

   if ( m_networking )
   {
      if ( sample.m_flowHP > TEMPERATURE_INVALID )
      {
         m_networking->sendToEmonCMS( hpFlowFeedId,sample.m_flowHP );
      }

      if ( sample.m_returnHP > TEMPERATURE_INVALID )
      {
         m_networking->sendToEmonCMS( hpReturnFeedId,sample.m_returnHP );
      }

      if ( sample.m_flowHeating > TEMPERATURE_INVALID )
      {
         m_networking->sendToEmonCMS( heatingFlowFeedId,sample.m_flowHeating );
      }

      if ( sample.m_returnHeating > TEMPERATURE_INVALID )
      {
         m_networking->sendToEmonCMS( heatingReturnFeedId,sample.m_returnHeating );
      }

      if ( sample.m_outside > TEMPERATURE_INVALID )
      {
         m_networking->sendToEmonCMS( outsideFeedId,sample.m_outside );
      }

      if ( sample.m_powerHP > POWER_INVALID )
      {
         m_networking->sendToEmonCMS( hpPowerFeedId,sample.m_powerHP );
      }

      if ( sample.m_powerImmersion > POWER_INVALID )
      {
         m_networking->sendToEmonCMS( immersionPowerFeedId,sample.m_powerImmersion );
      }
   }

   // we won't store if the card isn't ok

   if ( !m_sdCardOk )
   {
      return;
   }

   // If the filename is new, then we send out the existing file.  If we
   // fail to write to the file then the SD card status is set false to
   // prevent further writes - we'll send an email in that case too.

   if ( !SD.exists( fileName ) )
   {
      PW_MSG( "Will be creating %s",fileName );

      // As this is a new file, let's send previous file onwards ...

      if ( m_networking && strlen( m_currentFileName ) && SD.exists( m_currentFileName ) )
      {
         m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),"Daily Readings","Today's Final Results",m_currentFileName );
      }
   }

   File file = SD.open( fileName,FILE_APPEND );
   if( !file )
   {
      PW_WARN( "Failed to open %s",fileName );
      m_sdCardOk = false;
   }
   else
   {
      char  message[ 256 ];
      char  timeStr[ 32 ];

      strcpy( m_currentFileName,fileName );

      strftime( timeStr,32,"%Y%m%d,%H:%M:%S",&timeInfo );
      sprintf( message,"%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.0f",
                  timeStr,
                  sample.m_flowHP,sample.m_returnHP,sample.m_flowHeating,sample.m_returnHeating,sample.m_outside,
                  sample.m_powerHP,sample.m_powerImmersion,sample.m_energyHP,sample.m_energyImmersion );
      m_sdCardOk = file.println( message );
      file.close();
   }

   if ( ! m_sdCardOk && m_networking )
   {
      char subject[ 128 ];

      snprintf( subject,128,"SD Card Failure [%s]",m_networking->getIPAddress().c_str() );

      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,"Preventing further writes" );
   }
}

char  *Storage::getCurrentFileName()
{
   return m_currentFileName;
}
