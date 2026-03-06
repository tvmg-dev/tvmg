/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef LCD_INDICATOR_H
#define LCD_INDICATOR_H

#if defined(TVMG_WAVESHARE_LCDB)

#include "src/userio/Indicator.h"

class LCDIndicator : public Indicator
{
public:
   LCDIndicator( IndicatorType type, uint32_t id );
   ~LCDIndicator() override;

protected:
   void doOn() override;
   void doOff() override;
   void doHeartbeat( uint32_t onMillis, uint32_t offMillis ) override;

private:
   int8_t m_led;
};

#endif // TVMG_WAVESHARE_LCDB
#endif // LCD_INDICATOR_H
