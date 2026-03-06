/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef LCD_INDICATOR_H
#define LCD_INDICATOR_H

#if defined(TVMG_WAVESHARE_RELAY)

#include "Indicator.h"

class RelayIndicator : public Indicator
{
public:
   RelayIndicator( IndicatorType type, uint32_t id );
   ~RelayIndicator() override;

protected:
   void doOn() override;
   void doOff() override;
   void doHeartbeat( uint32_t onMillis, uint32_t offMillis ) override;

private:
   int8_t  m_led;
   int8_t  m_gpio;
};

#endif // TVMG_WAVESHARE_RELAY
#endif // LCD_INDICATOR_H
