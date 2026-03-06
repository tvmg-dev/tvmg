/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef INDICATOR_H
#define INDICATOR_H

#include <cstdint>
#include <map>
#include <memory>

#define SYSTEM_SETUP_ID 0
#define SYSTEM_AP_ID    1

class Indicator
{
public:
   enum IndicatorType { THERM, POWER, HEATMETER, HEATPUMP, NETWORK, SYSTEM };

   // factory/accessors
   static void initialise();
   static Indicator *getIndicator( IndicatorType type, uint32_t id );

   // RAII helper: turns indicator on while object alive
   class Scoped
   {
   public:
      explicit Scoped( Indicator *ind )
         : m_ind( ind )
      {
         if ( m_ind )
         {
            m_ind->on();
         }
      }

      ~Scoped()
      {
         if ( m_ind )
         {
            m_ind->off();
         }
      }

      // non‑copyable but movable
      Scoped( const Scoped & ) = delete;
      Scoped &operator=( const Scoped & ) = delete;
      Scoped( Scoped && ) = default;
      Scoped &operator=( Scoped && ) = default;

   private:
      Indicator *m_ind;
   };

   // operations that operate on the current instance
   void on();
   void off();
   void heartbeat( uint32_t onMillis, uint32_t offMillis );

   virtual ~Indicator();

protected:
   Indicator( IndicatorType type, uint32_t id );

   // hooks for the concrete implementation
   virtual void doOn() = 0;
   virtual void doOff() = 0;
   virtual void doHeartbeat( uint32_t onMillis, uint32_t offMillis ) = 0;

   IndicatorType type() const { return m_type; }
   uint32_t      id()   const { return m_id; }

private:
   static uint32_t generateKey( IndicatorType type, uint32_t id );

   static std::map<uint32_t,std::unique_ptr<Indicator>> s_map;

   IndicatorType m_type;
   uint32_t      m_id;
};

#endif // INDICATOR_H
