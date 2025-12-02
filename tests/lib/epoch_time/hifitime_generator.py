#!/usr/bin/env python3

import time

import hifitime

if __name__ == "__main__":
    epoch_gps = hifitime.Epoch.init_from_gpst_seconds(0)
    start_1900 = hifitime.Epoch.init_from_gregorian_utc(1900, 1, 1, 0, 0, 0, 0)
    start_1970 = hifitime.Epoch.init_from_gregorian_utc(1970, 1, 1, 0, 0, 0, 0)
    start_2017 = hifitime.Epoch.init_from_gregorian_utc(2017, 1, 1, 0, 0, 0, 0)
    start_2020 = hifitime.Epoch.init_from_gregorian_utc(2020, 1, 1, 0, 0, 0, 0)
    random_time = hifitime.Epoch.init_from_gregorian_utc(2024, 7, 2, 12, 43, 1, 0)
    now = hifitime.Epoch.init_from_unix_seconds(int(time.time()))

    print(f"{start_1900}")
    print(f"\tUNIX = {start_1900.to_unix_seconds()}")

    print(f"{epoch_gps} (Leap = {epoch_gps.leap_seconds(True)}):")
    print(f"\tUNIX = {epoch_gps.to_unix_seconds()}")

    print(f"{start_2017} (Leap = {start_2017.leap_seconds(True)}):")
    print(f"\tUNIX = {start_2017.to_unix_seconds()}")
    print(f"\t GPS = {start_2017.to_gpst_seconds()}")

    print(f"{start_2020} (Leap = {start_2020.leap_seconds(True)}):")
    print(f"\tUNIX = {start_2020.to_unix_seconds()}")
    print(f"\t GPS = {start_2020.to_gpst_seconds()}")

    print(f"{random_time} (Leap = {random_time.leap_seconds(True)}):")
    print(f"\tUNIX = {random_time.to_unix_seconds()}")
    print(f"\t GPS = {random_time.to_gpst_seconds()}")

    print(f"{now}")
    print(f"\tUNIX = {now.to_unix_seconds()}")
    print(f"\t GPS = {now.to_gpst_seconds()}")
