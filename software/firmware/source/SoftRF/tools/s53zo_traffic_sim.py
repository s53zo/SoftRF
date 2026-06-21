#!/usr/bin/env python3
"""
S53ZO bench traffic simulator for SoftRF T-Beam.

It sends ownship GNSS NMEA once per second and injects one synthetic target
through the S53ZO $PSRFT command. Targets approach one at a time from different
bearings, each for 30 seconds by default.
"""

import argparse
import datetime as dt
import math
import os
import select
import sys
import termios
import time


DEFAULT_PORT = "/dev/cu.wchusbserial5B212287231"
DEFAULT_BAUD = 38400
DEFAULT_LAT = 46.0569
DEFAULT_LON = 14.5058
DEFAULT_ALT_M = 500
DEFAULT_BEARINGS = (0, 45, 90, 135, 180, 225, 270, 315)

BAUD_CONSTANTS = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
}


def nmea(sentence):
    checksum = 0
    for value in sentence.encode("ascii"):
        checksum ^= value
    return f"${sentence}*{checksum:02X}\r\n"


def nmea_degrees(value, is_lat):
    hemisphere = "N" if is_lat else "E"
    if value < 0:
        hemisphere = "S" if is_lat else "W"
        value = -value

    degrees = int(value)
    minutes = (value - degrees) * 60.0
    if is_lat:
        return f"{degrees:02d}{minutes:07.4f}", hemisphere
    return f"{degrees:03d}{minutes:07.4f}", hemisphere


def ownship_sentences(now, lat, lon, altitude_m, satellites):
    lat_value, lat_hemi = nmea_degrees(lat, True)
    lon_value, lon_hemi = nmea_degrees(lon, False)
    time_field = now.strftime("%H%M%S")
    date_field = now.strftime("%d%m%y")

    rmc = nmea(
        "GPRMC,"
        f"{time_field}.00,A,{lat_value},{lat_hemi},{lon_value},{lon_hemi},"
        f"0.0,0.0,{date_field},,,A"
    )
    gga = nmea(
        "GPGGA,"
        f"{time_field}.00,{lat_value},{lat_hemi},{lon_value},{lon_hemi},"
        f"1,{satellites:02d},0.9,{altitude_m:.1f},M,46.9,M,,"
    )
    return rmc, gga


def configure_serial(fd, baud):
    if baud not in BAUD_CONSTANTS:
        raise ValueError(f"unsupported baud {baud}; supported: {sorted(BAUD_CONSTANTS)}")

    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(
        termios.IGNBRK
        | termios.BRKINT
        | termios.PARMRK
        | termios.ISTRIP
        | termios.INLCR
        | termios.IGNCR
        | termios.ICRNL
        | termios.IXON
    )
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.CSIZE | termios.PARENB)
    attrs[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] &= ~(
        termios.ECHO
        | termios.ECHONL
        | termios.ICANON
        | termios.ISIG
        | termios.IEXTEN
    )
    attrs[4] = BAUD_CONSTANTS[baud]
    attrs[5] = BAUD_CONSTANTS[baud]
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)


def distance_for_step(step, steps, start_m, end_m):
    if steps <= 1:
        return end_m
    fraction = step / float(steps - 1)
    return int(round(start_m + (end_m - start_m) * fraction))


def drain_serial(fd):
    lines = []
    while True:
        ready, _, _ = select.select([fd], [], [], 0)
        if fd not in ready:
            break
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        text = chunk.decode("ascii", errors="replace")
        lines.extend(text.splitlines())
    return lines


def send_line(fd, line, dry_run):
    if dry_run:
        return
    os.write(fd, line.encode("ascii"))


def simulate(args):
    fd = None
    if not args.dry_run:
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        configure_serial(fd, args.baud)

    bearings = [int(value.strip()) % 360 for value in args.bearings.split(",") if value.strip()]
    if not bearings:
        bearings = list(DEFAULT_BEARINGS)

    try:
        cycle = 0
        while True:
            for bearing in bearings:
                label = f"target {cycle + 1}, bearing {bearing:03d} deg"
                print(f"\n--- {label} ---", flush=True)

                for step in range(args.duration):
                    now = dt.datetime.now(dt.timezone.utc)
                    distance_m = distance_for_step(
                        step, args.duration, args.start_distance, args.end_distance
                    )

                    rmc, gga = ownship_sentences(
                        now, args.lat, args.lon, args.altitude, args.satellites
                    )
                    psrft = nmea(f"PSRFT,1,{distance_m},{bearing},0")

                    send_line(fd, rmc, args.dry_run)
                    send_line(fd, gga, args.dry_run)
                    send_line(fd, psrft, args.dry_run)

                    color = "blue"
                    if distance_m <= 500:
                        color = "red"
                    elif distance_m <= 1500:
                        color = "amber"

                    print(
                        f"{now.strftime('%H:%M:%S')}Z "
                        f"ownship sent, target {distance_m:5d} m "
                        f"bearing {bearing:03d} alt +0 m -> {color}",
                        flush=True,
                    )

                    if fd is not None and args.show_rx:
                        time.sleep(0.05)
                        for line in drain_serial(fd):
                            if "PSRFT" in line or "PFLAA" in line or "PFLAU" in line:
                                print(f"RX {line}", flush=True)

                    time.sleep(1.0)

                cycle += 1
                if args.once and cycle >= 1:
                    return
    finally:
        if fd is not None:
            os.close(fd)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Simulate one approaching aircraft at a time for S53ZO SoftRF LED/OLED tests."
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help=f"serial port, default {DEFAULT_PORT}")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"serial baud, default {DEFAULT_BAUD}")
    parser.add_argument("--lat", type=float, default=DEFAULT_LAT, help="ownship latitude")
    parser.add_argument("--lon", type=float, default=DEFAULT_LON, help="ownship longitude")
    parser.add_argument("--altitude", type=float, default=DEFAULT_ALT_M, help="ownship altitude in meters")
    parser.add_argument("--satellites", type=int, default=9, help="satellites reported in GPGGA")
    parser.add_argument("--duration", type=int, default=30, help="seconds per target/bearing")
    parser.add_argument("--start-distance", type=int, default=9000, help="approach start distance in meters")
    parser.add_argument("--end-distance", type=int, default=250, help="approach end distance in meters")
    parser.add_argument(
        "--bearings",
        default=",".join(str(value) for value in DEFAULT_BEARINGS),
        help="comma-separated bearings in degrees",
    )
    parser.add_argument("--show-rx", action="store_true", help="print selected SoftRF responses")
    parser.add_argument("--dry-run", action="store_true", help="print actions without opening serial")
    parser.add_argument("--once", action="store_true", help="run only one target sequence")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        simulate(parse_args())
    except KeyboardInterrupt:
        print("\nstopped", file=sys.stderr)
