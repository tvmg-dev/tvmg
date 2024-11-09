#ifndef MODBUS_TCP_MODULE_H
#define MODBUS_TCP_MODULE_H

#include <ModbusMaster.h>

#include "utils.h"

#define MAX_MODBUSTCP_NAME 32
#define MAX_REGISTERS      16

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


class ModbusTCP : public ModbusMaster
{
public:
   ModbusTCP();
   ~ModbusTCP();
   void  initialise();
   bool  isOk();

private:
   typedef struct {
      char        m_name[ MAX_MODBUSTCP_NAME + 1 ];   // Friendly name
      IPAddress   m_tcpServerAddress;
      uint16_t    m_tcpServerPort;                    // TCP server port
      uint16_t    m_requestDelay;
      uint8_t     m_id;
      bool        m_isValid;
   } PrivateSensor;

   PrivateSensor  m_sensor;
};

#endif
