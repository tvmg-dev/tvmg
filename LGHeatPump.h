#ifndef LG_HEATPUMP_MODULE_H
#define LG_HEATPUMP_MODULE_H

#include <map>

#include "utils.h"

#define MAX_HPREG_NAME  20
#define MAX_REGISTERS   50

class ModbusMaster;

class LGHeatPump
{
public:
   LGHeatPump( ModbusMaster *master );
   ~LGHeatPump();
   void  initialise();
   bool  isAvailable();
   bool  readNextSensor( uint8_t index );
   void  getModbusStats( uint32_t *requests,uint32_t *failures );

private:
   enum ModbusType {
      INVALID,
      COIL,
      DISCRETE,
      HOLDING,
      INPUTR,
      CALCULATED,
   };

   typedef struct {
      char        m_name[ MAX_HPREG_NAME + 1 ];    // Friendly name
      ModbusType  m_type;                          // coil etc,
      uint16_t    m_address;                       // address on modbus
      uint32_t    m_emonFeedId;                    // for emon
      float_t     m_scalingFactor;                 // conversion factor
      int16_t     m_rawValue;                      // treat all as signed values
      float_t     m_value;                         // after scaling
      bool        m_isValid;
   } LGRegister;

   void  getLGData();
   bool  getContiguousRange( ModbusType type,uint8_t *start,uint8_t *end );
   bool  getModbusData( ModbusType type,uint8_t start,uint8_t end );
   bool  getStatus( uint32_t parameter,bool *state );
   bool  getValue( uint32_t parameter,float_t *value );

   bool           m_isValid;
   LGRegister     *m_registers;
   uint8_t        m_numRegisters;
   ModbusMaster   *m_modbusRTU;
   uint32_t       m_modbusRequests;
   uint32_t       m_modbusFailures;

   int32_t        m_millisLastAquisition;       // milliseconds since last acquisition

   std::map<uint32_t,uint8_t> m_registerMap;
};

#endif
