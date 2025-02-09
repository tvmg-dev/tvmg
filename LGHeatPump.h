#ifndef LG_HEATPUMP_MODULE_H
#define LG_HEATPUMP_MODULE_H

#include <map>
#include <time.h>

#include "utils.h"

#define MAX_HPREG_NAME        20
#define MAX_HP_REGISTERS      50
#define MAX_LGSOFTWARE_LENGTH 32

#define  LGREGISTER_SCAN_LOG  "/registers.txt"
#define  LGMODBUS_LOG         "/lgmodbus.txt"
#define  LGSTATUS_LOG         "/lgstatus.txt"
#define  LGREGISTERS_LOG      "/lgreg.txt"

#define  LGHEATPUMP_SENSOR_NAME  "LGHEATPUMP"

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
#define  SET_ROOM_TEMP     (MB_HOLDING | 0x0004 )
#define  WC_OFFSET_TEMP    (MB_HOLDING | 0x0005 )
#define  DHW_TARGET_TEMP   (MB_HOLDING | 0x0009 )

#define  ERROR_CODE        (MB_INPUTR | 0x0001 )
#define  HEATING_MODE      (MB_INPUTR | 0x0002 )
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
#define  COMPRESSION_RATIO (MB_CALCULATED | 0x0005 )


class ModbusMaster;
class UserIO;

enum ModbusType {
   INVALID,
   COIL = 1,
   DISCRETE,
   HOLDING,
   INPUTR,
   CALCULATED = 8
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

struct LGStatus {
   LGStatus();

   time_t   m_time;
   uint16_t m_updates;

   int16_t  m_error;

   int16_t  m_inlet;
   int16_t  m_outlet;
   int16_t  m_oat;
   int16_t  m_room;
   int16_t  m_dhw;
   int16_t  m_heatingTarget;
   int16_t  m_wcOffset;
   int16_t  m_dhwTarget;

   int16_t  m_heatingMode;
   bool     m_extWaterPumpOn;
   bool     m_isCompressorOn;
   bool     m_isHeating;
   bool     m_isDHW;
   bool     m_isLegionella;
   bool     m_isImmersion;
   bool     m_isSilent;
   bool     m_isDefrost;
};

typedef struct LGStatus LGStatus;

class LGHeatPump
{
public:
   LGHeatPump( ModbusMaster *master );
   ~LGHeatPump();
   void  initialise();
   bool  isAvailable();
   bool  isLogging();
   void  resetEventLog();
   LGRegister *readNextSensor( uint8_t index );
   void  setCurrentKW( float_t kw );
   void  updateUserIO( UserIO *userIO );

private:
   void  getLGData();
   bool  getContiguousRange( ModbusType type,uint8_t *start,uint8_t *end );
   bool  getModbusData( ModbusType type,uint8_t start,uint8_t end );
   void  dumpData();
   bool  getStatus( uint32_t parameter,bool *state );
   bool  getValue( uint32_t parameter,float_t *value );
   int16_t  getRawValue( uint32_t parameter );
   bool  setValue( uint32_t parameter,float_t value );
   float_t  convertR32PressureToTemp( float_t pressure );
   bool  valueChanged( uint32_t parameter );
   void  updateStatus();

   LGRegister     *m_registers;
   LGStatus       m_currentStatus;
   uint8_t        m_numRegisters;
   uint8_t        m_series;
   ModbusMaster   *m_modbus;
   uint16_t       m_modbusAddress;
   char           m_softwareVersion[ MAX_LGSOFTWARE_LENGTH ];
   int32_t        m_millisLastAquisition;       // milliseconds since last acquisition
   float_t        m_currentKW;                  // last reported
   uint16_t       m_flowRateWhenNotHeating;     // if non-zero then report this flow rate when heating but no compressor
                                                // (pump setting in heating for LG)

   std::map<uint32_t,uint8_t> m_registerMap;    // map of register address to m_registers[] index
   bool           m_logRegisters;               // whether writing registers to a file
};

#endif
