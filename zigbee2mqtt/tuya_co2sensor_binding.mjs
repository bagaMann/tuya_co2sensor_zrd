import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

// Only 5 explicit exposes; Zigbee2MQTT adds Linkquality automatically.
const attr = {
    enable: 0xF100,
    on: 0xF101,
    off: 0xF102,
    repeat: 0xF103,
};

export default {
    zigbeeModel: Array.from({length: 16}, (_, i) =>
        `Tuya_CO2Sensor_r${(i + 1).toString(16).toUpperCase().padStart(2, '0')}`),
    model: 'Tuya_CO2Sensor_binding',
    vendor: 'Slacky-DIY',
    description: 'Tuya CO2 sensor with threshold-based On/Off binding',
    extend: [
        m.co2(),
        m.binary({
            name: 'enabling_co2_control',
            cluster: 'msCO2',
            attribute: {ID: attr.enable, type: 0x10},
            valueOn: ['ON', 1], valueOff: ['OFF', 0],
            description: 'Enable or disable CO2 binding control',
        }),
        m.binary({
            name: 'enabling_repeat_command',
            cluster: 'msCO2',
            attribute: {ID: attr.repeat, type: 0x10},
            valueOn: ['ON', 1], valueOff: ['OFF', 0],
            description: 'Repeat last On/Off command every 60 seconds',
        }),
        m.numeric({
            name: 'co2_on_command', cluster: 'msCO2',
            attribute: {ID: attr.on, type: 0x21},
            unit: 'ppm', valueMin: 401, valueMax: 5000, valueStep: 1,
            description: 'CO2 level that sends On',
        }),
        m.numeric({
            name: 'co2_off_command', cluster: 'msCO2',
            attribute: {ID: attr.off, type: 0x21},
            unit: 'ppm', valueMin: 400, valueMax: 4999, valueStep: 1,
            description: 'CO2 level that sends Off',
        }),
    ],
    ota: true,
};
