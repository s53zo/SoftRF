/*
 * LEDHelper.cpp
 * Copyright (C) 2016-2026 Linar Yusupov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../system/SoC.h"

#include <TimeLib.h>

#include "LED.h"
#include "Battery.h"
#include "../TrafficHelper.h"

extern uint32_t tx_packets_counter, rx_packets_counter;
static uint32_t prev_tx_packets_counter = 0;
static uint32_t prev_rx_packets_counter = 0;

#define isTimeToToggle() (millis() - status_LED_TimeMarker > 300)
static int status_LED = SOC_UNUSED_PIN;
static unsigned long status_LED_TimeMarker = 0;

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

#if !defined(EXCLUDE_LED_RING)
static bool LED_ring_present()
{
  return SOC_GPIO_PIN_LED != SOC_UNUSED_PIN;
}

static bool LED_pointer_enabled()
{
  return LED_ring_present() && settings->pointer != LED_OFF;
}

#if defined(SOFTRF_TBEAM_LED_RING_ADDON)
#define ADDON_WAIT_INTERVAL_MS   400
#define ADDON_ERROR_INTERVAL_MS  500

static unsigned long addon_LED_TimeMarker = 0;

static uint16_t LED_ringPixelCount()
{
  uint16_t pixel_count = uni_numPixels();
  return pixel_count < RING_LED_NUM ? pixel_count : RING_LED_NUM;
}

static void LED_ringClear_noflush()
{
  for (uint16_t i = 0; i < LED_ringPixelCount(); i++) {
    uni_setPixelColor(i, LED_COLOR_BLACK);
  }
}

static void LED_ringFill_noflush(color_t c)
{
  for (uint16_t i = 0; i < LED_ringPixelCount(); i++) {
    uni_setPixelColor(i, c);
  }
}

static void LED_ringShow()
{
  SoC->swSer_enableRx(false);
  uni_show();
  SoC->swSer_enableRx(true);
}

static bool LED_addon_error_active()
{
  float voltage = Battery_voltage();
  return voltage > BATTERY_THRESHOLD_INVALID && voltage < Battery_threshold();
}

static color_t LED_addon_target_color(int distance)
{
  if (distance >= 0 && distance <= LED_DISTANCE_CLOSE) {
    return uni_Color(16, 0, 0);
  }
  if (distance > LED_DISTANCE_CLOSE && distance <= LED_DISTANCE_NEAR) {
    return uni_Color(14, 9, 0);
  }
  return uni_Color(0, 0, 14);
}

static void LED_addon_wait_fix_noflush()
{
  uint16_t pixel_count = LED_ringPixelCount();

  LED_ringClear_noflush();

  if (pixel_count == 0) {
    return;
  }

  uint16_t led_num = (millis() / ADDON_WAIT_INTERVAL_MS) % pixel_count;
  uni_setPixelColor(led_num, uni_Color(7, 0, 0));
}

static void LED_addon_error_flash_noflush()
{
  bool flash_on = ((millis() / ADDON_ERROR_INTERVAL_MS) % 2) == 0;
  LED_ringFill_noflush(flash_on ? uni_Color(10, 0, 0) : LED_COLOR_BLACK);
}

static void LED_addon_idle_noflush()
{
  if (LED_addon_error_active()) {
    LED_addon_error_flash_noflush();
  } else if (!isValidFix()) {
    LED_addon_wait_fix_noflush();
  } else {
    LED_ringClear_noflush();
  }
}

static int LED_addon_nearest_target()
{
  int nearest = -1;
  float nearest_distance = LED_DISTANCE_FAR;
  time_t now_ts = now();

  for (int i = 0; i < MAX_TRACKING_OBJECTS; i++) {
    if (Container[i].addr &&
        (now_ts - Container[i].timestamp) <= LED_EXPIRATION_TIME &&
        Container[i].distance < nearest_distance) {
      nearest = i;
      nearest_distance = Container[i].distance;
    }
  }

  return nearest;
}

static void LED_addon_display_nearest_target_noflush()
{
  LED_ringClear_noflush();

  int target = LED_addon_nearest_target();
  if (target < 0) {
    return;
  }

  int bearing = (int) Container[target].bearing;
  int distance = (int) Container[target].distance;

  if (distance >= LED_DISTANCE_FAR) {
    return;
  }

  if (settings->pointer != DIRECTION_NORTH_UP) {
    bearing = (360 + bearing - (int) ThisAircraft.course) % 360;
  }

  int led_num = ((bearing + LED_ROTATE_ANGLE + SECTOR_PER_LED / 2) % 360) /
                SECTOR_PER_LED;

  uni_setPixelColor(led_num, LED_addon_target_color(distance));
}
#endif /* SOFTRF_TBEAM_LED_RING_ADDON */

#if defined(SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST)
static void LED_orderSelfTest()
{
  for (uint16_t i = 0; i < uni_numPixels(); i++) {
    uni_setPixelColor(i, LED_COLOR_BLACK);
  }
  uni_show();

  for (uint16_t i = 0; i < uni_numPixels(); i++) {
    color_t c = i < RING_LED_NUM ? LED_COLOR_MI_YELLOW : LED_COLOR_MI_GREEN;
    uni_setPixelColor(i, c);
    uni_show();
    delay(150);
    uni_setPixelColor(i, LED_COLOR_BLACK);
  }
  uni_show();
}
#endif /* SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST */

#if defined(SOFTRF_TBEAM_LED_RING_ADDON) && !defined(SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST)
static color_t LED_wheel(uint8_t pos)
{
  const uint8_t level = 16;

  pos = 255 - pos;
  if (pos < 85) {
    return uni_Color(level - pos * level / 85, 0, pos * level / 85);
  }
  if (pos < 170) {
    pos -= 85;
    return uni_Color(0, pos * level / 85, level - pos * level / 85);
  }

  pos -= 170;
  return uni_Color(pos * level / 85, level - pos * level / 85, 0);
}

static void LED_startupRainbow()
{
  const uint16_t pixel_count = LED_ringPixelCount();

  if (pixel_count == 0) {
    return;
  }

  for (uint8_t frame = 0; frame < 100; frame++) {
    for (uint16_t i = 0; i < pixel_count; i++) {
      uni_setPixelColor(i, LED_wheel(((i * 256 / pixel_count) + frame * 3) & 0xFF));
    }
    uni_show();
    delay(40);
  }

  for (uint16_t i = 0; i < pixel_count; i++) {
    uni_setPixelColor(i, LED_COLOR_BLACK);
  }
  uni_show();
}

static void LED_startupTopMarker()
{
  const uint16_t pixel_count = LED_ringPixelCount();

  if (pixel_count <= ZERO_BEARING_LED_NUM) {
    return;
  }

  for (uint8_t i = 0; i < 5; i++) {
    LED_ringClear_noflush();
    uni_setPixelColor(ZERO_BEARING_LED_NUM, uni_Color(0, 48, 0));
    uni_show();
    delay(160);

    LED_ringClear_noflush();
    uni_show();
    delay(160);
  }
}
#endif /* SOFTRF_TBEAM_LED_RING_ADDON && !SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST */
#endif /* EXCLUDE_LED_RING */

void LED_setup() {
#if !defined(EXCLUDE_LED_RING)
  if (LED_pointer_enabled()
#if defined(SOFTRF_TBEAM_LED_RING_ADDON)
      || LED_ring_present()
#endif /* SOFTRF_TBEAM_LED_RING_ADDON */
      ) {
    uni_begin();
    uni_show(); // Initialize all pixels to 'off'
#if defined(SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST)
    LED_orderSelfTest();
#elif defined(SOFTRF_TBEAM_LED_RING_ADDON)
    LED_startupRainbow();
    LED_startupTopMarker();
#endif /* SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST */
  }
#endif /* EXCLUDE_LED_RING */

  status_LED = SOC_GPIO_PIN_STATUS;

  if (status_LED != SOC_UNUSED_PIN) {
    pinMode(status_LED, OUTPUT);
    /* Indicate positive power supply */
    digitalWrite(status_LED, LED_STATE_ON);
  }
}

#if !defined(EXCLUDE_LED_RING)
// Fill the dots one after the other with a color
static void colorWipe(color_t c, uint8_t wait) {
  for (uint16_t i = 0; i < uni_numPixels(); i++) {
    uni_setPixelColor(i, c);
    uni_show();
    delay(wait);
  }
}

//Theatre-style crawling lights.
static void theaterChase(color_t c, uint8_t wait) {
  for (int j = 0; j < 10; j++) { //do 10 cycles of chasing
    for (int q = 0; q < 3; q++) {
      for (int i = 0; i < uni_numPixels(); i = i + 3) {
        uni_setPixelColor(i + q, c);  //turn every third pixel on
      }
      uni_show();

      delay(wait);

      for (int i = 0; i < uni_numPixels(); i = i + 3) {
        uni_setPixelColor(i + q, LED_COLOR_BLACK);      //turn every third pixel off
      }
    }
  }
}
#endif /* EXCLUDE_LED_RING */

void LED_test() {
#if !defined(EXCLUDE_LED_RING)
  if (LED_pointer_enabled()) {
    // Some example procedures showing how to display to the pixels:
    colorWipe(uni_Color(255, 0, 0), 50); // Red
    colorWipe(uni_Color(0, 255, 0), 50); // Green
    colorWipe(uni_Color(0, 0, 255), 50); // Blue
    // Send a theater pixel chase in...
    theaterChase(uni_Color(127, 127, 127), 50); // White
    theaterChase(uni_Color(127, 0, 0), 50); // Red
    theaterChase(uni_Color(0, 0, 127), 50); // Blue

    //  rainbow(20);
    //  rainbowCycle(20);
    //  theaterChaseRainbow(50);
    colorWipe(uni_Color(0, 0, 0), 50); // clear
  }
#endif /* EXCLUDE_LED_RING */
}

#if !defined(EXCLUDE_LED_RING)
static void LED_Clear_noflush() {
#if defined(SOFTRF_TBEAM_LED_RING_ADDON)
    LED_addon_idle_noflush();
#else
    for (uint16_t i = 0; i < RING_LED_NUM; i++) {
      uni_setPixelColor(i, LED_COLOR_BACKLIT);
    }

    if (rx_packets_counter > prev_rx_packets_counter) {
      uni_setPixelColor(LED_STATUS_RX, LED_COLOR_MI_GREEN);
      prev_rx_packets_counter = rx_packets_counter;

      if (settings->mode == SOFTRF_MODE_WATCHOUT) {
        for (uint16_t i = 0; i < RING_LED_NUM; i++) {
          uni_setPixelColor(i, LED_COLOR_RED);
        }
      } else if (settings->mode == SOFTRF_MODE_BRIDGE) {
        for (uint16_t i = 0; i < RING_LED_NUM; i++) {
          uni_setPixelColor(i, LED_COLOR_MI_RED);
        }
      }

    }  else {
      uni_setPixelColor(LED_STATUS_RX, LED_COLOR_BLACK);
    }

    if (tx_packets_counter > prev_tx_packets_counter) {
      uni_setPixelColor(LED_STATUS_TX, LED_COLOR_MI_GREEN);
      prev_tx_packets_counter = tx_packets_counter;
    } else {
      uni_setPixelColor(LED_STATUS_TX, LED_COLOR_BLACK);
    }

    uni_setPixelColor(LED_STATUS_POWER,
      Battery_voltage() > Battery_threshold() ? LED_COLOR_MI_GREEN : LED_COLOR_MI_RED);
    uni_setPixelColor(LED_STATUS_SAT,
      isValidFix() ? LED_COLOR_MI_GREEN : LED_COLOR_MI_RED);
#endif /* SOFTRF_TBEAM_LED_RING_ADDON */
}
#endif /* EXCLUDE_LED_RING */

void LED_Clear() {
#if !defined(EXCLUDE_LED_RING)
  if (LED_pointer_enabled()
#if defined(SOFTRF_TBEAM_LED_RING_ADDON)
      || LED_ring_present()
#endif /* SOFTRF_TBEAM_LED_RING_ADDON */
      ) {
    LED_Clear_noflush();

#if defined(SOFTRF_TBEAM_LED_RING_ADDON)
    LED_ringShow();
#else
    SoC->swSer_enableRx(false);
    uni_show();
    SoC->swSer_enableRx(true);
#endif /* SOFTRF_TBEAM_LED_RING_ADDON */
  }
#endif /* EXCLUDE_LED_RING */
}

void LED_DisplayTraffic() {
#if !defined(EXCLUDE_LED_RING)
#if defined(SOFTRF_TBEAM_LED_RING_ADDON)
  if (LED_ring_present()) {
    if (LED_addon_error_active()) {
      LED_addon_error_flash_noflush();
    } else if (!isValidFix()) {
      LED_addon_wait_fix_noflush();
    } else {
      LED_addon_display_nearest_target_noflush();
    }

    LED_ringShow();
  }
#else
  int bearing, distance;
  int led_num;
  color_t color;

  if (LED_pointer_enabled()) {
    LED_Clear_noflush();

    for (int i=0; i < MAX_TRACKING_OBJECTS; i++) {

      if (Container[i].addr && (now() - Container[i].timestamp) <= LED_EXPIRATION_TIME) {

        bearing  = (int) Container[i].bearing;
        distance = (int) Container[i].distance;

        if (settings->pointer == DIRECTION_TRACK_UP) {
          bearing = (360 + bearing - (int)ThisAircraft.course) % 360;
        }

        led_num = ((bearing + LED_ROTATE_ANGLE + SECTOR_PER_LED/2) % 360) / SECTOR_PER_LED;
//      Serial.print(bearing);
//      Serial.print(" , ");
//      Serial.println(led_num);
//      Serial.println(distance);
        if (distance < LED_DISTANCE_FAR) {
          if (distance >= 0 && distance <= LED_DISTANCE_CLOSE) {
            color =  LED_COLOR_RED;
          } else if (distance > LED_DISTANCE_CLOSE && distance <= LED_DISTANCE_NEAR) {
            color =  LED_COLOR_YELLOW;
          } else if (distance > LED_DISTANCE_NEAR && distance <= LED_DISTANCE_FAR) {
            color =  LED_COLOR_BLUE;
          }
          uni_setPixelColor(led_num, color);
        }
      }
    }

    SoC->swSer_enableRx(false);
    uni_show();
    SoC->swSer_enableRx(true);

  }
#endif /* SOFTRF_TBEAM_LED_RING_ADDON */
#endif /* EXCLUDE_LED_RING */
}

void LED_loop() {
#if !defined(EXCLUDE_LED_RING) && defined(SOFTRF_TBEAM_LED_RING_ADDON)
  if (LED_ring_present() && (LED_addon_error_active() || !isValidFix())) {
    unsigned long interval = LED_addon_error_active() ?
                             ADDON_ERROR_INTERVAL_MS : ADDON_WAIT_INTERVAL_MS;

    if (millis() - addon_LED_TimeMarker > interval) {
      LED_addon_idle_noflush();
      LED_ringShow();
      addon_LED_TimeMarker = millis();
    }
  }
#endif /* !EXCLUDE_LED_RING && SOFTRF_TBEAM_LED_RING_ADDON */

  if (status_LED != SOC_UNUSED_PIN) {
    if (Battery_voltage() > Battery_threshold() ) {
      /* Indicate positive power supply */
      if (digitalRead(status_LED) != LED_STATE_ON) {
        digitalWrite(status_LED, LED_STATE_ON);
      }
    } else {
      if (isTimeToToggle()) {
        digitalWrite(status_LED, !digitalRead(status_LED) ? HIGH : LOW);  // toggle state
        status_LED_TimeMarker = millis();
      }
    }
  }
}
