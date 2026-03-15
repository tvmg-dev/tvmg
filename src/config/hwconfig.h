/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#include <Arduino.h>
#include <stdint.h>

#include <esp_system.h>

#define  SERIAL_DISABLE_GPIO  15
#define  ALTERNATE_UART0_RX_GPIO 32
#define  ALTERNATE_UART0_TX_GPIO 33

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
   int8_t        PWMGPIO;
   bool          hasSDCard;
} HardwareConfig;

extern HardwareConfig *hwConfig;

extern void selectHardware();

extern bool boardHasSDCard();

extern bool boardHalt();

// reboot-related types and persistent keys

enum RebootType {
   POWER_CYCLE = 0,
   BOOT_NO_CONFIG,
   BOOT_NO_WIFI,
   BOOT_NO_NTP,
   BOOT_IN_SETUP,
   LOST_WIFI,
   SERVER_REBOOT,
   SERVER_RESET,
   SERVER_OTA_UPDATE,
   LOOP_MUTEX,
   ESP32_PANIC,
   ESP32_WATCHDOG,
   APP_24D_RESET,
   UNKNOWN
};

inline constexpr  char k_rebootCounter[] = "rebootCount";
inline constexpr  char k_rebootType[] = "rebootType";
inline constexpr  char k_watchdogCause[] = "wdogReason";
inline constexpr  char k_noNetworkCounter[] = "noNetwork";

// hardware-state helpers

extern bool    isFactoryReset();
extern void    setFactoryReset();

extern String  getESPRebootReason( esp_reset_reason_t code );
extern String  getAppRebootReason( RebootType code );
extern String  getRebootReason( RebootType *type );
extern bool    wasFastReboot();
extern bool    didRebootNoWiFi();

extern void    hwReset();
extern void    reboot();
extern void    setRebootRequired();
extern bool    isRebootRequired();
extern void    checkEarlyRebootFailure();

extern bool    getPersistentInt( const String &key,int32_t *value,int32_t defValue = -1 );
extern void    setPersistentInt( const String &key,int32_t value );

#endif
