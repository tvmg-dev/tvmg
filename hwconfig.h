#ifndef HWCONFIG_H
#define HWCONFIG_H

#include <stdint.h>

typedef struct {
   int8_t        OneWireGPIO;
   int8_t        ModBusSerial;
   int32_t       ModBusBaudRate;
   int32_t       ModBusSerialFormat;
   int8_t        ModBusRxGPIO;
   int8_t        ModBusTxGPIO;
   int8_t        ModBusMsgDelay;
   int8_t        ModBus485EnGPIO;
   int8_t        OLEDClkGPIO;
   int8_t        OLEDDataGPIO;
   int8_t        TouchButton1;
   int8_t        TouchButton2;
} HardwareConfig;

extern HardwareConfig *hwConfig;

extern void selectHardware();

#endif
