#ifndef MODBUS_TCP_MODULE_H
#define MODBUS_TCP_MODULE_H

#include <ModbusMaster.h>

#include "utils.h"

#define MAX_MODBUSTCP_NAME 32
#define MAX_REGISTERS      16

#define MODBUSTCP_SENSOR_NAME "MODBUSTCP"

typedef struct
{
   uint16_t transactionId;
   uint16_t numBytes;
   uint8_t  slaveAddress;
   uint8_t  transactionType;
   uint16_t startRegister;
   uint16_t numRegisters;
} ModBusRequest;

typedef struct
{
   uint16_t transactionId;
   uint16_t numBytes;
   uint8_t  slaveAddress;
   uint8_t  transactionType;
   uint8_t  dataBytes;
   uint8_t  numRegisters;
   uint16_t registers[ MAX_REGISTERS + 1 ];
} ModBusResponse;


class ModbusTCP : public ModbusMaster
{
public:
   ModbusTCP();
   ~ModbusTCP();
   void  initialise();
   bool  isOk();
   uint8_t  readCoils(uint16_t, uint16_t);
   uint8_t  readDiscreteInputs(uint16_t, uint16_t);
   uint8_t  readHoldingRegisters(uint16_t, uint16_t);
   uint8_t  readInputRegisters(uint16_t, uint8_t);

private:
   typedef struct {
      char        m_name[ MAX_MODBUSTCP_NAME + 1 ];   // Friendly name
      IPAddress   m_tcpServerAddress;
      uint16_t    m_tcpServerPort;                    // TCP server port
      uint16_t    m_requestDelay;
      uint8_t     m_id;
      bool        m_isValid;
   } PrivateSensor;

   bool     getData( const ModBusRequest &request );
   uint8_t  getData( uint8_t transactionType,uint16_t u16ReadAddress,uint16_t u16ReadQty );

   PrivateSensor  m_sensor;
   uint8_t        m_transactionId;
};

#endif
