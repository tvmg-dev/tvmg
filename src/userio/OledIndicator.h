/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef OLED_INDICATOR_H
#define OLED_INDICATOR_H

#ifdef TVMG_OLED

#include "src/userio/Indicator.h"

class OledIndicator : public Indicator
{
public:
   OledIndicator( IndicatorType type, uint32_t id );
   ~OledIndicator() override;

protected:
   void doOn() override;
   void doOff() override;
   void doHeartbeat( uint32_t onMillis, uint32_t offMillis ) override;

private:
   void setIndicator( bool on );

   int8_t m_led;
};

#endif // TVMG_OLED
#endif // OLED_INDICATOR_H
