# LilyGO T-Beam AXP2101 LED Ring Add-on

This option is for a custom SoftRF ESP32 / Prime MkII build on a LilyGO T-Beam AXP2101 v1.2 with an external WS2812/NeoPixel indicator chain.

It is experimental traffic awareness hardware, not certified FLARM or certified collision avoidance equipment.

## Build Flags

Enable the add-on only for the custom T-Beam build:

```text
-DSOFTRF_TBEAM_LED_RING_ADDON
```

This also enables the OLED text page and makes it the initial OLED page. The text page shows nearest-target details: target index/count, o'clock direction, distance, relative altitude, and aircraft type.

Optional one-boot LED order self-test:

```text
-DSOFTRF_TBEAM_LED_RING_ADDON -DSOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST
```

The add-on flag changes `SOC_GPIO_PIN_LED` to GPIO13 for classic ESP32 builds. Other builds are unchanged unless this flag is supplied.

For the Arduino CLI T-Beam board profile, use the `min_spiffs` partition override because the full SoftRF image is larger than the default OTA slot:

```text
--build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080
```

The custom firmware version string is suffixed as `1.9-S53ZO`.

## OLED Default Page

The T-Beam OLED is a small 128x64 display, approximately 25 mm by 15 mm. With `SOFTRF_TBEAM_LED_RING_ADDON` enabled, SoftRF starts on the compact text traffic page instead of the radio counter page. This keeps the display focused on nearest-target details that fit the small screen:

- target index/count
- o'clock direction
- distance
- relative altitude
- aircraft type

On the AXP2101 T-Beam, a short press on the PMU power button changes pages through the PMU interrupt path. In the S53ZO add-on build, the GPIO38 side button returns to the OLED text page when another page is visible; when the text page is already visible it selects the next target.

## Parts

- LilyGO T-Beam AXP2101 v1.2 running the ESP32 / Prime MkII SoftRF firmware.
- 8 WS2812/NeoPixel LEDs for the direction ring.
- 330 to 470 ohm resistor in series with the data line.
- 470 to 1000 uF capacitor across LED 5V and GND near the first LED.
- Stable 5V from the T-Beam USB-C 5V/VBUS rail, or an external 5V supply if the installation needs it.
- Optional 74AHCT125 or similar 3.3V-to-5V data level shifter.

## Wiring

```text
T-Beam GPIO13  -> 330-470 ohm resistor -> first WS2812 DIN
T-Beam 5V      -> LED 5V
T-Beam GND     -> LED GND
470-1000 uF    -> across LED 5V/GND near the first LED
```

Use a common ground. Do not power the LEDs from GPIO or 3V3. Keep brightness modest for night use and USB power.

GPIO13 is output-capable and is not one of the T-Beam LoRa, GNSS UART, PMU/OLED I2C, PMU IRQ, or button pins used by the normal AXP2101 Prime MkII path. In this source tree GPIO13 is also named as the T-Beam secondary I2C SDA pin, so do not use this add-on on hardware that needs that optional secondary I2C bus at the same time.

## LED Order

Build one chain with the 8 direction pixels only:

```text
LED 0-7   traffic direction ring
```

The SoftRF direction math maps logical LED 4 to zero bearing, meaning the physical ahead/top position. Mount the 8-pixel ring like this:

```text
          LED 4
     LED 3     LED 5
  LED 2           LED 6
     LED 1     LED 7
          LED 0
```

## LED Signalling

Normal add-on firmware uses the 8-pixel ring like this:

- boot: four-second low-brightness rainbow sweep
- top marker: LED 4 blinks five times in mid-bright green after the rainbow
- waiting for GNSS fix: cyan breathing ring with one orange running marker
- GNSS fix, no active target: all LEDs off
- active traffic: nearest target only, mapped by bearing onto the ring
- low battery: dim red flash

The order self-test build replaces the startup rainbow with an LED 0 through LED 7 order check.

## Bench Test

1. Build with `-DSOFTRF_TBEAM_LED_RING_ADDON -DSOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST`.
2. Power the LEDs from T-Beam 5V and GND.
3. Boot on the bench and confirm the self-test lights LED 0 through LED 7 in order at low brightness.
4. Confirm LED 4 is physically at the ahead/top position.
5. Rebuild without `SOFTRF_TBEAM_LED_RING_ADDON_SELF_TEST` for normal use.

## Serial Traffic Simulator

The S53ZO build accepts a bench-test traffic command over the USB serial port:

```text
$PSRFT,1,<distance_m>,<bearing_deg>,<altitude_diff_m>*checksum
```

For a longer LED/OLED test, run the host-side simulator script from the repository root:

```sh
python3 software/firmware/source/SoftRF/tools/s53zo_traffic_sim.py --show-rx
```

By default it uses `/dev/cu.wchusbserial5B212287231` at 38400 baud. It sends ownship GNSS NMEA once per second and injects one same-altitude target at a time for 30 seconds from bearings 0, 45, 90, 135, 180, 225, 270, and 315 degrees. Each target closes from 9000 m to 250 m, so the LED ring should progress through blue, amber, and red distance states.
