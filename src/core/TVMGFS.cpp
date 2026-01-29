#include "TVMGFS.h"
#include "src/core/utils.h"

#if defined(TVMG_LITTLEFS)
   #include <LittleFS.h>
   #define TVMG_FS_INSTANCE LittleFS
   #define TVMG_FS_TYPE_NAME "LittleFS"
#elif defined(TVMG_SPIFFS)
   #include <SPIFFS.h>
   #define TVMG_FS_INSTANCE SPIFFS
   #define TVMG_FS_TYPE_NAME "SPIFFS"
#else
   #error "Must define either TVMG_LITTLEFS or TVMG_SPIFFS"
#endif

TVMGFileSystem tvmgFileSys;

TVMGFileSystem::TVMGFileSystem()
   : m_isMounted( false )
{
   PW_MSG( "TVMGFileSystem for %s",typeName() );
}

File TVMGFileSystem::open( const String& path,const char* mode )
{
   return TVMG_FS_INSTANCE.open( path,mode );
}

bool TVMGFileSystem::exists( const String& path )
{
   return TVMG_FS_INSTANCE.exists( path );
}

bool TVMGFileSystem::remove( const String& path )
{
   return TVMG_FS_INSTANCE.remove(path);
}

bool TVMGFileSystem::rename( const char* pathFrom, const char* pathTo )
{
   return TVMG_FS_INSTANCE.rename( pathFrom,pathTo );
}

bool TVMGFileSystem::mkdir( const String& path )
{
   return TVMG_FS_INSTANCE.mkdir(path);
}

bool TVMGFileSystem::rmdir( const String& path )
{
   return TVMG_FS_INSTANCE.rmdir(path);
}

bool TVMGFileSystem::begin( bool formatOnFail )
{
   PW_MSG( "Mounting %s %s", typeName(), (formatOnFail ? ",format on fail" : "" ) );

   // We call begin on the system singleton (TVMG_FS_INSTANCE)
   // This avoids the Guru Meditation crash caused by duplicate instances
   m_isMounted = TVMG_FS_INSTANCE.begin( formatOnFail );

   PW_MSG( "%s %s", typeName(), (m_isMounted ? "mounted" : "failed" ) );

   return m_isMounted;
}

fs::FS& TVMGFileSystem::getFS()
{
   return TVMG_FS_INSTANCE;
}

size_t TVMGFileSystem::totalBytes()
{
   size_t total = TVMG_FS_INSTANCE.totalBytes();

#if defined(TVMG_SPIFFS)
   total *= 0.75f;
#endif

   return total;
}

size_t TVMGFileSystem::usedBytes()
{
   return TVMG_FS_INSTANCE.usedBytes();
}

bool TVMGFileSystem::format()
{
   return TVMG_FS_INSTANCE.format();
}

const char* TVMGFileSystem::typeName() const
{
   return TVMG_FS_TYPE_NAME;
}

TVMGFileSystem::operator bool()
{
   bool isOk = m_isMounted && ( totalBytes() > 0 );

   if ( !isOk )
   {
      PW_WARN( "%s not OK", typeName() );
   }

   return isOk;
}
