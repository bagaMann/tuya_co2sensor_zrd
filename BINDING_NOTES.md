# CO2 On/Off binding extension — first test build

Base: `slacky1965/tuya_co2sensor_zrd`. UART, sensor measurements, Tuya datapoints, and the original flash layout remain unchanged.

- Endpoint 1 has **On/Off 0x0006 Client/Output**, for binding to an On/Off Server/Input on a relay, plug or group. There is **no OnOff server state** or Z2M automation state.
- Custom writable configuration attributes in **CO2 Measurement 0x040D**: `F100` enable (bool, default false); `F101` ON threshold ppm (uint16, 1000); `F102` OFF threshold ppm (uint16, 800); `F103` total transmissions (uint8, 3); `F104` repeat interval ms (uint16, 1500).
- Commands are triggered only when hysteresis state changes; no continuous On/Off transmissions. Initial state is unknown until a valid reading outside the hysteresis band. The 1–5 repeats are sent by a nonblocking SDK timer. Settings are retained via Telink NV. Write invalid combinations are reverted; read back after changing thresholds.
- Converter: `zigbee2mqtt/tuya_co2sensor_binding.mjs`. This is for the **custom firmware**. Keep the original Tuya converter in `zigbee2mqtt/convertors/` only when preparing a stock-device first OTA.
- Debian: install the original Telink TC32 toolchain at `/opt/tc32/bin`; then `make clean && make`. Outputs include `bin/tuya_co2sensor_zrd_V1.0.05.bin`, `bin/tuya_co2sensor_zrd_last_version.bin`, standard `.zigbee` OTA and Tuya-header `.zigbee` OTA. The existing `make flash` still writes the app at **0x8000**.
- OTA: uses **unchanged** `zigbee_ota.py` from `slacky1965/ts0201_tz3000_zed` and generates both OTA headers in one `make`. **Caution:** this CO2 firmware retains `BOOT_LOADER_MODE=1` and the app address 0x8000 for compatibility with existing working devices. Creating a Tuya-header file does not yet establish that a *first* OTA from factory Tuya can safely use that header; verify bootloader behavior before attempting a stock-device migration. Test normal OTA on an already-modified device first.
- After installing: re-interview the sensor if Z2M has cached endpoint clusters, bind its **Output genOnOff** on endpoint 1, set thresholds in the converter, then test with a lamp/noncritical relay.
