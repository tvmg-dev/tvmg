#if defined(TVMG_WAVESHARE_LCDB)
//
// RGB panel needs to be refilled constantly, so the ESP32S3 SoC has a DMA
// engine that can read from RAM and output on GPIO pins connected to the panel.
// This can be the SRAM or PSRAM.  At 800x480x2 bytes then the SRAM, which is
// limited to ~320KiB cannot possibly be used in this manner.
//
// So PSRAM has to be used.  Unfortunately the Expressif guys have set the
// Arduino library build such that LWiP buffers will use PSRAM in preference to
// SRAM.  This means that there is contention between the DMA feeding the RGB
// panel and networking.  Generally speaking the WiFi radio can connect, but
// network connections are next to impossible under certain timings of the
// panel.
//
// The panels have 'porch' periods. The front porch is the time after DMA of a
// line (or frame for vertical) before the sync pulse.  The back porch is the
// time after the sync pulse and before the DMA restarts.
//  see
//  https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/peripherals/lcd.html#_CPPv420esp_lcd_rgb_timing_t
//
// By extending the porches and sync pulses then we can provide more 'dead' time
// from the RGB DMA perspective, allowing other clients to use PSRAM uncontended
// in these periods.
//
// The timings here worked and in the main tvmg project allowed networking to
// be performed, albeit more slowly than without the panel.  Unfortunately the
// project also uses flash for OTA download, saving/retrieving LG data etc.  And
// this uses the SPI bus that is used by PSRAM - so again we have contention.
// This effectively means we cannot use a large RGB panel in the tvmg project
// even with the extended timings (that may be out of spec. in any event).
//
// This is an example where we use SRAM only to feed the RGB panel.  The
// concept being that we would divide the panel into rows of (say) 16 pixels.
// And have a single line for the first row that we could fill independently -
// to show 'LED' status indicators for example.  All other rows would be black -
// so using the panel as a crude indicator.  The ESP panel driver has a 'bounce
// buffer' solution where 2 buffers are used, one is filled in a callback whilst
// the other is being DMA'd to the panel.
//
// The current code is hardly perfect but works.  There are however 2 problems
// with this approach:
//
//    1 - Memory usage.
//        Need 2 bounce buffers and a line for the top row.
//        A 16 pixel bounce buffer is 25.6KB, so 51.2 KB required.
//        Top row of pixels is another 1.6 KB, so a total of 52.8 KB of SRAM
//        used
//    2 - CPU load.
//        The ESP panel driver issues the bounce callback, and we have to
//        memcpy/ memset the region.  So CPU intensive. The timing is such that
//        we would get a callback every 16 lines.  With reduced clock and new
//        porches, a line takes 67.7us that means a callback every 1083us. If we
//        fail to meet the deadline then the display presumably would jitter
//        etc. We could reduce the callback rate at the expense of larger
//        buffers.
//
// Given the other realtime aspects of the tvmg project then this really isn't a
// feasible approach.  It could work if we could simply provide a pointer to the
// memory region in the callback, little CPU overhead and more constrained
// memory usage.  But the ESP panel driver doesn't support that directly.
//
// What we can do is however is assume the 2 bounce buffers toggle, and that we
// fill one of them only once (black).  The other we fill on the first row from
// our source data, on the 3rd row we fill with black, so now only black rows
// are displayed - no need for any memcpy/memset for those rows.
//
// The other issue discovered is that this panel has an issue with blue bit 8.
// If the bit is set, so blue component goes from 0x7F to 0x80 for example then
// the first 8 bits of a solid colour containing that bit will not be correct.
// It may be a panel issue, pick up on the ESP's GPIO for bit 8 due to I2C clock
// or something. In any event, by avoiding blue bit 8 all other colours seem ok.

#include <Arduino.h>
#include <Wire.h>               // For CH422G I2C expander
#include <esp_lcd_panel_ops.h>  // Panel operations
#include <esp_lcd_panel_rgb.h>  // RGB panel driver

#include "LCDDisplay.h"

#include "src/config/Config.h"
#include "src/core/utils.h"

// ---------------------------------------------------------------------------
// LED status bar layout configuration
// ---------------------------------------------------------------------------
#define LED_COUNT 8          // number of indicators (must match enum)
#define LED_WIDTH 32         // pixel width of each LED block
#define LED_GAP 40           // pixel gap between blocks
#define LED_FIRST_PIXEL 128  // x‑coordinate of first LED

// structure keeping runtime state of an LED
struct LedState
{
   bool isRunningHeartBeat;     // currently running a isRunningHeartBeat sequence
   uint32_t onMillis;           // isRunningHeartBeat on period (ms)
   uint32_t offMillis;          // isRunningHeartBeat off period (ms)
   uint32_t lastChangedMillis;  // millis() when we last toggled
   bool isActive;               // current visible state
   uint16_t colour565;          // current RGB565 colour for the LED
};

static LedState leds[LED_COUNT];

// Initial colours 

#define COLOUR_LED1  0xFF2A24  // Muted red
#define COLOUR_LED2  0x2A657F  // Muted blue
#define COLOUR_LED3  0x5EC874  // Muted green
#define COLOUR_LED4  0xFF552A  // Orange
#define COLOUR_LED5  0xF1CA0E  // Yellow
#define COLOUR_LED6  0x7F7F7F  // Grey
#define COLOUR_LED7  0xFF2A7F  // Vibrant pink
#define COLOUR_LED8  0x2AAA55  // Deeper green

// default colours (RGB888) assigned at compile time
static const uint32_t initColours888[LED_COUNT] = {
    COLOUR_LED1,
    COLOUR_LED2,
    COLOUR_LED3,
    COLOUR_LED4,
    COLOUR_LED5,
    COLOUR_LED6,
    COLOUR_LED7,
    COLOUR_LED8};

// Panel resolution and color depth
#define LCD_H_RES 800
#define LCD_V_RES 480
#define LCD_BITS_PER_PIXEL 16

// Panel timing options

#if defined(EXTENDED_BLANKING)
// Adjusted timings with increased porches for more refill time
//    pixel clock is 62.5ns
//    time per line is (HRES + H-porches + H-sync) x clock = 58.0 us
//    time per frame is (VRES + V-porches + V-sync) x line ms = 40.02ms
//    refresh rate is therefore 1/40.02 ms ~ 25 Hz
//    DMA time is (HRES + VRES) x clock = 24ms

#define LCD_PCLK_HZ 16000000
#define HSYNC_PULSE_WIDTH 40
#define HSYNC_BACK_PORCH 40
#define HSYNC_FRONT_PORCH 48
#define VSYNC_PULSE_WIDTH 3
#define VSYNC_BACK_PORCH 150  // Increased
#define VSYNC_FRONT_PORCH 57  // Increased significantly
#else
// time per line is 67.7 us
// time per frame is 48.92 ms
// refresh rate a lowly 20.4 Hz
// time for bounce callbacks, 16 lines, is 1082 us and a vertical delay of
// 20ms, minimal processing in bounce callback

#define LCD_PCLK_HZ 12000000
#define HSYNC_PULSE_WIDTH 4
#define HSYNC_BACK_PORCH 4
#define HSYNC_FRONT_PORCH 4
#define VSYNC_PULSE_WIDTH 3
#define VSYNC_BACK_PORCH 120
#define VSYNC_FRONT_PORCH 120
#endif

// GPIO pins from Waveshare ESP32-S3-Touch-LCD-4.3B
#define LCD_PCLK 7
#define LCD_HSYNC 46
#define LCD_VSYNC 3
#define LCD_DE 5

// Data pins (16-bit, in bus order: D0=LSB to D15=MSB; based on board wiring for RGB565)
static const int lcd_data_pins[16] =
    {
        14, 38, 18, 17, 10,     // Blue
        39, 0, 45, 48, 47, 21,  // Green
        1, 2, 42, 41, 40        // Red
};

// CH422G I2C addresses and bits (from Waveshare docs/examples)
#define CH422G_SYS_ADDR 0x20  // System config
#define CH422G_IO_ADDR 0x38   // IO output register
#define TP_RST     0x02       // Bit 1: Touch reset (not used here)
#define LCD_BL     0x04       // Bit 2: Backlight
#define LCD_RST    0x08       // Bit 3: LCD reset

#define I2C_SDA   8
#define I2C_CLK   9
#define TP_INT    4

// Bounce buffer config (16 lines in SRAM; 2 buffers allocated by driver)
#define BOUNCE_LINES 16
#define BOUNCE_SIZE_PX (BOUNCE_LINES * LCD_H_RES)
#define BOUNCE_SIZE_BYTES (BOUNCE_SIZE_PX * 2)

// Position of status row (leds)
#define STATUS_ROW 16

// Our single line representing states
static uint16_t statusLine[LCD_H_RES] __attribute__((aligned(64)));

// Panel handle
esp_lcd_panel_handle_t panel_handle = NULL;

static uint32_t bounceCallbacks = 0;

// Global state
static void* activeBuffer = NULL;
static void* passiveBuffer = NULL;
static bool isActiveBufBlack = false;

// For screen saver, kick in after 5 minutes
#define  SCREENSAVER_MS             (5 * 60 * 1000)
static bool screenSaverActive = false;

/**
 * @brief RGB LCD VSYNC event callback prototype
 *
 * @param[in] panel LCD panel handle, returned from `esp_lcd_new_rgb_panel`
 * @param[in] edata Panel event data, fed by driver
 * @param[in] user_ctx User data, passed from
 * `esp_lcd_rgb_panel_register_event_callbacks()`
 * @return Whether a high priority task has been waken up by this function
 */

static uint32_t vsyncs = 0;
static uint32_t maxTimeInBounce = 0;

static bool IRAM_ATTR vsyncCallback(esp_lcd_panel_handle_t panel,
                                    const esp_lcd_rgb_panel_event_data_t* edata,
                                    void* user_ctx)
{
   vsyncs++;
   return false;
}

uint64_t lastBounce = 0;
uint32_t longCallbacks = 0;
uint32_t bounceTime = 0;

/**
 * @brief Prototype for function to re-fill a bounce buffer, rather than copying
 * from the frame buffer
 *
 * @param[in] panel LCD panel handle, returned from `esp_lcd_new_rgb_panel`
 * @param[in] bounce_buf Bounce buffer to write data into
 * @param[in] pos_px How many pixels already were sent to the display in this
 * frame, in other words, at what pixel the routine should start putting data
 * into bounce_buf
 * @param[in] len_bytes Length, in bytes, of the bounce buffer. Routine should
 * fill this length fully.
 * @param[in] user_ctx Opaque pointer that was passed from
 * `esp_lcd_rgb_panel_register_event_callbacks()`
 * @return Whether a high priority task has been waken up by this function
 */

#if defined(FLUSH_CACHE)
extern "C" void Cache_WriteBack_Addr(uint32_t addr, uint32_t size);
#endif

static bool IRAM_ATTR bounceCallback(esp_lcd_panel_handle_t panel,
                                     void* bounce_buf, int pos_px,
                                     int len_bytes, void* user_ctx)
{
   uint64_t now = esp_timer_get_time();
   uint32_t diff = now - lastBounce;

   // Just some debug to see how many callback intervals exceed 3ms, we expect a
   // long delay after the last line and the next start line - around 16ms for
   // the least bandwidth intensive

   if (diff > 3000)
   {
      longCallbacks++;
   }
   else
   {
      bounceTime += diff;
   }

   lastBounce = now;

   // We're assuming 2 bounce buffers in use here, so we collect their addresses
   // and for the passive buffer, i.e. not the 1st row we fill in black only
   // once.

   if (activeBuffer == NULL)
   {
      activeBuffer = bounce_buf;
   }
   else if (passiveBuffer == NULL && bounce_buf != activeBuffer)
   {
      passiveBuffer = bounce_buf;
      memset(passiveBuffer, 0, BOUNCE_SIZE_BYTES);  // Black forever
   }

   uint32_t row = (pos_px / BOUNCE_SIZE_PX);

   // If its the active buffer then on the first row we fill it from our
   // 'status' line, otherwise we fill it black for the rest of the frame.

   if (bounce_buf == activeBuffer)
   {
      if (row == STATUS_ROW)
      {
         // status row fill
         if (isActiveBufBlack)
         {
            size_t line_bytes = LCD_H_RES * 2;
            memcpy(bounce_buf, statusLine, line_bytes);

            uint8_t* dst = (uint8_t*)bounce_buf + line_bytes;
            for (int i = 1; i < BOUNCE_LINES; i++)
            {
               memcpy(dst, bounce_buf, line_bytes);
               dst += line_bytes;
            }
            isActiveBufBlack = false;
         }
      }
      else if (!isActiveBufBlack)
      {
         memset(bounce_buf, 0, len_bytes);  // set bounce buffer black
         isActiveBufBlack = true;
      }
   }

   bounceCallbacks++;

   uint32_t callbackTime = (uint32_t)(esp_timer_get_time() - now);
   if (callbackTime > maxTimeInBounce)
   {
      maxTimeInBounce = callbackTime;
   }

   return false;
}

void controlBacklight( bool on )
{
   uint8_t ioVal = LCD_RST | LCD_BL;

//   esp_lcd_panel_disp_on_off(panel_handle, on);

   if ( !on )
   {
      ioVal &= ~LCD_BL;
   }

   ioVal = (on ? 0x0E : 0x0A);

   Wire.beginTransmission(CH422G_IO_ADDR);
   Wire.write(ioVal);
   Wire.endTransmission();
}

void setupWS()
{
   String initStr("Initializing LCD.  Free Heap : ");
   initStr += String(ESP.getFreeHeap());

   PW_MSG(initStr.c_str());

   // Init I2C

   Wire.setClock(400000);
   Wire.begin(I2C_SDA, I2C_CLK);

   // Configure CH422G: Set IO mode (all outputs)
   Wire.beginTransmission(CH422G_SYS_ADDR);
   Wire.write(0x01); // Mode Register, enable output
   Wire.endTransmission();

   // Reset LCD: Low then high
   uint8_t io_val = 0x0F & ~LCD_RST;  // Set reset low
   Wire.beginTransmission(CH422G_IO_ADDR);
   Wire.write(io_val);
   Wire.endTransmission();
   delay(100);
   io_val |= LCD_RST;  // Reset high
   Wire.beginTransmission(CH422G_IO_ADDR);
   Wire.write(io_val);
   Wire.endTransmission();
   delay(100);

   // Turn on backlight
   controlBacklight( true );

   // RGB panel config
   esp_lcd_rgb_panel_config_t panel_config = {};
   panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
   panel_config.timings.pclk_hz = LCD_PCLK_HZ;
   panel_config.timings.h_res = LCD_H_RES;
   panel_config.timings.v_res = LCD_V_RES;
   panel_config.timings.hsync_pulse_width = HSYNC_PULSE_WIDTH;
   panel_config.timings.hsync_back_porch = HSYNC_BACK_PORCH;
   panel_config.timings.hsync_front_porch = HSYNC_FRONT_PORCH;
   panel_config.timings.vsync_pulse_width = VSYNC_PULSE_WIDTH;
   panel_config.timings.vsync_back_porch = VSYNC_BACK_PORCH;
   panel_config.timings.vsync_front_porch = VSYNC_FRONT_PORCH;
   panel_config.timings.flags.hsync_idle_low = 0;
   panel_config.timings.flags.vsync_idle_low = 0;
   panel_config.timings.flags.de_idle_high = 0;
   panel_config.timings.flags.pclk_active_neg = 0;
   panel_config.timings.flags.pclk_idle_high = 0;

   panel_config.data_width = LCD_BITS_PER_PIXEL;
   panel_config.bits_per_pixel = LCD_BITS_PER_PIXEL;
   panel_config.psram_trans_align = 64;  // No PSRAM, but set for alignment
   panel_config.sram_trans_align = 64;
   panel_config.num_fbs = 0;  // No frame buffers !
   panel_config.bounce_buffer_size_px = BOUNCE_SIZE_PX;
   panel_config.flags.no_fb = true;  // Key fix: Bounce-only, no frame buffer
   panel_config.flags.bb_invalidate_cache = true;
   panel_config.flags.fb_in_psram = false;
   panel_config.flags.double_fb = false;

   panel_config.pclk_gpio_num = LCD_PCLK;
   panel_config.vsync_gpio_num = LCD_VSYNC;
   panel_config.hsync_gpio_num = LCD_HSYNC;
   panel_config.de_gpio_num = LCD_DE;

   // Data pins array
   memcpy(panel_config.data_gpio_nums, lcd_data_pins, sizeof(lcd_data_pins));

   // Create RGB panel

   if (esp_lcd_new_rgb_panel(&panel_config, &panel_handle) != ESP_OK)
   {
      PW_ERROR("Failed to create new LCD panel");
      goto exit;
   }

   // Init panel

   if (esp_lcd_panel_reset(panel_handle) != ESP_OK)
   {
      PW_ERROR("Failed to reset LCD");
      goto exit;
   }

   if (esp_lcd_panel_init(panel_handle) != ESP_OK)
   {
      PW_ERROR("Failed to initialise LCD");
      goto exit;
   }

   // Register refill & vsync callbacks, in its own scope to avoid cross-init errror
   {
      esp_lcd_rgb_panel_event_callbacks_t callbacks =
          {
              .on_vsync = vsyncCallback,
              .on_bounce_empty = bounceCallback};

      if (esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &callbacks, NULL) != ESP_OK)
      {
         PW_ERROR("Failed to register LCD callbacks");
         goto exit;
      }
   }

   // Turn on display

   if (esp_lcd_panel_disp_on_off(panel_handle, true) != ESP_OK)
   {
      PW_ERROR("Failed to turn LCD on");
      goto exit;
   }

   initStr = "LCD initialized. Free heap: " + String(ESP.getFreeHeap());
   PW_MSG(initStr.c_str());
exit:
}

// ---------------------------------------------------------------------------
// LED status bar support
// ---------------------------------------------------------------------------
// convert 24‑bit RGB to RGB565; constexpr so it can be used in initialisers
static constexpr uint16_t convertToRGB565(uint32_t rgb888)
{
   return (uint16_t)((((rgb888) & 0xF80000) >> 8) | (((rgb888) & 0x00FC00) >> 5) |
                     (((rgb888) & 0x0000F8) >> 3));
}

// lazily initialise the LED table; called by every public function
static void ledsInit(void)
{
   static bool ledsInitialised = false;
   if (ledsInitialised)
   {
      return;
   }
   for (int i = 0; i < LED_COUNT; i++)
   {
      leds[i].isRunningHeartBeat = false;
      leds[i].onMillis = 0;
      leds[i].offMillis = 0;
      leds[i].lastChangedMillis = 0;
      leds[i].isActive = false;
      leds[i].colour565 = convertToRGB565(initColours888[i]);
   }
   ledsInitialised = true;
}

// regenerate the global statusLine array from the current LED states
static void regenStatusLine(void)
{
   // blank everything first (black)
   memset(statusLine, 0, sizeof(statusLine));

   // each LED uses configured width and gap
   for (int idx = 0; idx < LED_COUNT; idx++)
   {
      if (!leds[idx].isActive)
      {
         continue;
      }
      int x0 = LED_FIRST_PIXEL + idx * (LED_WIDTH + LED_GAP);
      if (x0 >= LCD_H_RES)
      {
         continue;
      }
      int x1 = x0 + LED_WIDTH;
      if (x1 > LCD_H_RES)
      {
         x1 = LCD_H_RES;
      }
      for (int x = x0; x < x1; x++)
      {
         statusLine[x] = leds[idx].colour565;
      }
   }
}

// periodic timer - checks for heartbeat changes on any led that's in that mode

static void heatbeatTimerCallback(void* arg)
{
   (void)arg;

   uint32_t now = millis();
   bool changed = false;

   // Handle screen saver mode changes

   int screenSaver = GET_REGISTRY_INT( USERIO_SCREENSAVER );

   if ( screenSaver == 1 )
   {
      if ( !screenSaverActive )
      {
         controlBacklight( false );
         screenSaverActive = true;
      }
      return;
   }
   else if ( screenSaver == 0 && screenSaverActive )
   {
      controlBacklight( true );
      screenSaverActive = false;
      return;
   }
   if ( screenSaver == -1 && now >= SCREENSAVER_MS )
   {
      SET_REGISTRY( USERIO_SCREENSAVER,1 );
      controlBacklight( false );
      screenSaverActive = true;
      return;
   }

   // If we're here then we need to perform any indicator heartbeats 

   for (int i = 0; i < LED_COUNT; i++)
   {
      if (!leds[i].isRunningHeartBeat)
      {
         continue;
      }
      uint32_t period = leds[i].isActive ? leds[i].onMillis : leds[i].offMillis;
      if (period < 200)  // enforce minimum
      {
         period = 200;
      }

      if ((uint32_t)(now - leds[i].lastChangedMillis) >= period)
      {
         leds[i].isActive = !leds[i].isActive;
         leds[i].lastChangedMillis = now;
         changed = true;
      }
   }

   if (changed)
   {
      regenStatusLine();
   }
}

static void startHeatbeatTimer(void)
{
   static esp_timer_handle_t heartbeatTimer = nullptr;

   if (heartbeatTimer)
   {
      return;
   }

   ledsInit();

   const esp_timer_create_args_t args =
       {.callback = &heatbeatTimerCallback,
        .name = "led_hb"};

   if ( esp_timer_create(&args, &heartbeatTimer) == ESP_OK )
   {
      /* fire every 100ms; nothing magic about the interval, it just needs to be
         short enough to honour the 200ms minimum isRunningHeartBeat period. */
      esp_timer_start_periodic(heartbeatTimer, 100 * 1000);
   }
}

// public API -------------------------------------------------------------

static LcdDisplay* s_lcdInstance = nullptr;

LcdDisplay::LcdDisplay() : Display()
{
   s_lcdInstance = this;
}

LcdDisplay::~LcdDisplay()
{
}

LcdDisplay* LcdDisplay::getInstance()
{
   if (!s_lcdInstance)
   {
      s_lcdInstance = new LcdDisplay();
   }
   return s_lcdInstance;
}

void LcdDisplay::initialise()
{
   PW_MSG("initialise LCD display");
   setupWS();
   startHeatbeatTimer();
}

void  LcdDisplay::updateLine( uint8_t lineNum,const char *line,bool isForLog )
{
   (void) lineNum;

   if ( isForLog )
   {
      PW_DEBUG( line );
   }
}

/**
 * @brief set or clear an LED immediately
 *
 * Calling this will cancel any isRunningHeartBeat that was running on the same LED.
 */
void LcdDisplay::setLed(int led, bool on)
{
   if (led < 0 || led >= LED_COUNT)
   {
      return;
   }

   leds[led].isRunningHeartBeat = false;
   leds[led].isActive = on;
   regenStatusLine();
}

/**
 * @brief start a isRunningHeartBeat animation on the given LED
 *
 * @param led led to control
 * @param onMillis how long the LED stays on each cycle (>=200)
 * @param offMillis how long the LED stays off each cycle (>=200)
 *
 * The LED will start in the "on" state when this call is made.
 */
void LcdDisplay::startLedHeartbeat(int led, uint32_t onMillis, uint32_t offMillis)
{
   if (led < 0 || led >= LED_COUNT)
   {
      return;
   }
   if (onMillis < 200)
   {
      onMillis = 200;
   }
   if (offMillis < 200)
   {
      offMillis = 200;
   }

   leds[led].isRunningHeartBeat = true;
   leds[led].onMillis = onMillis;
   leds[led].offMillis = offMillis;
   leds[led].isActive = true;
   leds[led].lastChangedMillis = millis();

   regenStatusLine();
}

/**
 * @brief change the colour used when an LED is illuminated
 *
 * @param led index of LED (use the LedId enum)
 * @param rgb888 24‑bit colour value (0xRRGGBB)
 */
void LcdDisplay::setLedColour(int led, uint32_t rgb888)
{
   if (led < 0 || led >= LED_COUNT)
   {
      return;
   }

   uint16_t colour565 = convertToRGB565(rgb888);
   if (colour565 != leds[led].colour565)
   {
      leds[led].colour565 = colour565;
      regenStatusLine();
   }
}

void loopWS()
{
   static uint32_t last_update = 0;
   static uint32_t lastBounce = 0;
   static uint32_t lastVsyncs = 0;
   static uint32_t lastLong = 0;
   static uint32_t lastBounceTime = 0;
   static uint32_t count = 0;

   static uint8_t red = 8;
   static uint8_t green = 4;

   if (millis() - last_update > 5000)
   {
      count++;

      {
         startHeatbeatTimer();

#if 0
         startLedHeartbeat(LED_STATUS, 400, 600);
         startLedHeartbeat(LED_AP, 4000, 2000);
         startLedHeartbeat(LED_OTA, 4000, 2000);
         startLedHeartbeat(LED_SENSOR1, 4000, 2000);
         startLedHeartbeat(LED_SENSOR2, 4000, 2000);
         startLedHeartbeat(LED_SENSOR3, 4000, 2000);
         startLedHeartbeat(LED_SENSOR4, 4000, 2000);
         startLedHeartbeat(LED_SENSOR5, 4000, 2000);
#endif
      }

#if defined(WALKING_BLUE_BITTEST)
      Serial.printf("0x%02x 0x%02x\n", red, green);
      for (int i = 0; i < LED_COUNT; i++)
      {
         uint32_t colour = (red << 16) | (green << 8) | (1 << i);
         uint16_t r565c = convertToRGB565(colour);
         Serial.printf("0x%06x - 0x%04x : ", colour, r565c);
         setLedColour(i, colour);
      }
      Serial.printf("\n");

      red = red * 2;
      green = green * 2;

      if (!red)
      {
         red = 8;
      }
      if (!green)
      {
         green = 4;
      }
#endif

      last_update = millis();

      Serial.printf("Bounce callbacks : %d %d\n", bounceCallbacks,
                    bounceCallbacks - lastBounce);
      Serial.printf("vsyncs %d %d\n", vsyncs, vsyncs - lastVsyncs);
      Serial.printf("long %d %d\n", longCallbacks, longCallbacks - lastLong);
      Serial.printf("bounce %d %d\n", bounceTime, bounceTime - lastBounceTime);
      Serial.printf("max time %d\n", maxTimeInBounce);

      lastVsyncs = vsyncs;
      lastBounce = bounceCallbacks;
      lastLong = longCallbacks;
      lastBounceTime = bounceTime;

      delay(100);  // short delay, doesn't need to be short really
   }
}
#endif
