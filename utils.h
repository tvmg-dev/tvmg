#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <stdarg.h>

#define MAX_FILENAME    32

#define DEBUG_LEVEL_ENABLED   1

enum LOGGING_LEVEL { MSG, DEBUG, WARNING, ERROR };

extern void msgLog( LOGGING_LEVEL level,const char *format,... );
#if DEBUG_LEVEL_ENABLED == 1
  #define PW_DEBUG(...)  msgLog( LOGGING_LEVEL::DEBUG, __VA_ARGS__ )
#else
  #define PW_DEBUG(...)  //
#endif

#define PW_MSG(...)     msgLog( LOGGING_LEVEL::MSG, __VA_ARGS__ )
#define PW_WARN(...)    msgLog( LOGGING_LEVEL::WARNING, __VA_ARGS__ )
#define PW_ERROR(...)   msgLog( LOGGING_LEVEL::ERROR, __VA_ARGS__ )

#endif
