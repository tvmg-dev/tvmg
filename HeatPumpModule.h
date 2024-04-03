#ifndef HEAT_PUMP_MODULE_H
#define HEAT_PUMP_MODULE_H

#include <IPAddress.h>

#include "PowerModule.h"

#include "utils.h"

#define MAX_HEATPUMP_NAME  32
#define MAX_REGISTERS      15

// The ID should be matched in the sensors.dat file

#define HEAT_PUMP_ID    200

class WiFiClient;

typedef struct
{
   uint16_t transactionId;
   uint16_t protocol;
   uint16_t numBytes;
   uint8_t  slaveAddress;
   uint8_t  transactionType;
   uint16_t startRegister;
   uint16_t numRegisters;
} ModBusRequest;

typedef struct
{
   uint16_t transactionId;
   uint16_t protocol;
   uint16_t numBytes;
   uint8_t  slaveAddress;
   uint8_t  transactionType;
   uint8_t  dataBytes;
   uint8_t  numRegisters;
   uint16_t registers[ MAX_REGISTERS + 1 ];
} ModBusResponse;


typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_flowRate;    // FlowRate
   char    *m_name;        // name (don't store the name here to keep the structure size to minimum
} HeatPumpSensor;

class HeatPumpModule
{
public:
   HeatPumpModule();
   ~HeatPumpModule();
   void  initialise();
   HeatPumpSensor *readNextSensor( uint8_t index );
   bool  sampleHP();
   bool  getData( WiFiClient *host,ModBusRequest *request, ModBusResponse *response );
   bool  isAvailable();

private:
   typedef struct {
      HeatPumpSensor m_hpSensor;                   // sensor essentials
      char        m_name[ MAX_HEATPUMP_NAME + 1 ]; // Friendly name
      uint8_t     m_slaveAddress;                  // address on modbus
      IPAddress   m_tcpServerAddress;
      uint16_t    m_tcpServerPort;                 // TCP server port
      bool        m_simulateNonInputs;             // if device only has input registers
      uint8_t     m_numCoils;
      uint8_t     m_numDiscretes;
      uint8_t     m_numHolding;
      uint8_t     m_numInput;
      uint16_t    m_requestDelay;
      bool        m_isValid;
   } PrivateSensor;

   PrivateSensor  m_sensor;
};

#endif
