# CO2 binding — v1.0.06

Base firmware: [slacky1965/tuya_co2sensor_zrd](https://github.com/slacky1965/tuya_co2sensor_zrd). Sensor UART and all existing measurements remain unchanged. On/Off 0x0006 remains Output/Client only.

- CO2 Measurement 0x040D custom writable attributes: 0xF100 control ON/OFF (default OFF), 0xF101 ON ppm (default 1000), 0xF102 OFF ppm (default 800), 0xF103 cyclic command repeat ON/OFF (default OFF). No OnOff server state.
- On threshold change: three fixed On/Off transmissions, first immediate, then twice at 1500 ms intervals. These parameters are no longer writable.
- Optional cyclic repeat uses the Slacky `DEFAULT_REPEAT_TIME 60` idea: resend the absolute last On or Off every 60 seconds while both automation and repeat are enabled. Timer stops when repeat or automation is disabled.
- Configuration save/restore follows Slacky's `app_config.c`: packed struct, additive checksum, `nv_flashWriteNew(1, NV_MODULE_APP, NV_ITEM_APP_USER_CFG, ...)` and `nv_flashReadNew`. A readback check follows saves; `DEBUG_SAVE` prints diagnostics on the existing UART debug path. Legacy 1.0.05 NV records are migrated to the new layout while keeping control enable and thresholds.
- Z2M converter: `zigbee2mqtt/tuya_co2sensor_binding.mjs`. Exposes only CO2, enabling CO2 control, enabling repeat command, CO2 on command, CO2 off command. Zigbee2MQTT supplies Linkquality automatically.
- Build with `make` on TC32 Linux; normal OTA file for current custom firmware has ID `6565-0393`. Program `.bin` remains an application image at address `0x8000`.
- Test: OTA, copy/reload converter, set thresholds, power-cycle sensor and verify readback after restart, exercise binding with repeat OFF then ON; inspect logs if NV write returns nonzero.
