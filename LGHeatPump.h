#ifndef LG_HEATPUMP_MODULE_H
#define LG_HEATPUMP_MODULE_H

#include <map>

#include "utils.h"

#define MAX_HPREG_NAME  20
#define MAX_REGISTERS   50

// Define the registers available

#define  MB_COIL        0x10000
#define  MB_DISCRETE    0x20000
#define  MB_HOLDING     0x30000
#define  MB_INPUTR      0x40000
#define  MB_CALCULATED  0x80000

#define  HEATING_ENABLED   (MB_COIL | 0x0001)
#define  DHW_ENABLED       (MB_COIL | 0x0002)
#define  SILENT_ENABLED    (MB_COIL | 0x0003)

#define  WATER_FLOW_STATUS       (MB_DISCRETE | 0x0001)
#define  WATER_PUMP_STATUS       (MB_DISCRETE | 0x0002)
#define  EXT_WATER_PUMP_STATUS   (MB_DISCRETE | 0x0003)
#define  COMPRESSOR_STATUS       (MB_DISCRETE | 0x0004)
#define  DEFROST_STATUS          (MB_DISCRETE | 0x0005)
#define  DHW_HEATING             (MB_DISCRETE | 0x0006)
#define  LEGIONELLA_STATUS       (MB_DISCRETE | 0x0007)
#define  SILENT_STATUS           (MB_DISCRETE | 0x0008)
#define  BOOST_WATER             (MB_DISCRETE | 0x000D)

#define  OPERATING_MODE    (MB_HOLDING | 0x0001 )
#define  CONTROL_METHOD    (MB_HOLDING | 0x0002 )
#define  TARGET_TEMP       (MB_HOLDING | 0x0003 )
#define  WC_OFFSET_TEMP    (MB_HOLDING | 0x0005 )

#define  ERROR_CODE        (MB_INPUTR | 0x0001 )
#define  UNIT_CYCLE        (MB_INPUTR | 0x0002 )
#define  INLET_TEMP        (MB_INPUTR | 0x0003 )
#define  OUTLET_TEMP       (MB_INPUTR | 0x0004 )
#define  DHW_TEMP          (MB_INPUTR | 0x0006 )
#define  ROOM_TEMP         (MB_INPUTR | 0x0008 )
#define  FLOW_RATE         (MB_INPUTR | 0x0009 )
#define  OUTSIDE_TEMP      (MB_INPUTR | 0x000D )
#define  PIPE_IN_TEMP      (MB_INPUTR | 0x0011 )
#define  SUCTION_TEMP      (MB_INPUTR | 0x0013 )
#define  DISCHARGE_TEMP    (MB_INPUTR | 0x0014 )
#define  HEX_TEMP          (MB_INPUTR | 0x0015 )
#define  HIGH_PRESSURE     (MB_INPUTR | 0x0017 )
#define  LOW_PRESSURE      (MB_INPUTR | 0x0018 )
#define  COMPRESSOR_HZ     (MB_INPUTR | 0x0019 )

#define  HEATING_POWER     (MB_CALCULATED | 0x0001 )
#define  LOW_PRESS_TEMP    (MB_CALCULATED | 0x0002 )
#define  HIGH_PRESS_TEMP   (MB_CALCULATED | 0x0003 )
#define  COP               (MB_CALCULATED | 0x0004 )

class ModbusMaster;

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

class LGHeatPump
{
public:
   LGHeatPump( ModbusMaster *master );
   ~LGHeatPump();
   void  initialise();
   bool  isAvailable();
   LGRegister *readNextSensor( uint8_t index );
   void  getModbusStats( uint32_t *requests,uint32_t *failures );
   void  setCurrentKW( float_t kw );

private:
   void  getLGData();
   bool  getContiguousRange( ModbusType type,uint8_t *start,uint8_t *end );
   bool  getModbusData( ModbusType type,uint8_t start,uint8_t end );
   void  dumpData();
   bool  getStatus( uint32_t parameter,bool *state );
   bool  getValue( uint32_t parameter,float_t *value );
   bool  setValue( uint32_t parameter,float_t value );
   float_t  convertR32PressureToTemp( float_t pressure );

   LGRegister     *m_registers;
   uint8_t        m_numRegisters;
   ModbusMaster   *m_modbusRTU;
   uint32_t       m_modbusRequests;
   uint32_t       m_modbusFailures;
   int32_t        m_millisLastAquisition;       // milliseconds since last acquisition
   float_t        m_currentKW;                  // last reported

   std::map<uint32_t,uint8_t> m_registerMap;    // map of register address to m_registers[] index
};

#endif
