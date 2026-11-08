import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

// Custom writable configuration attributes within CO2 Measurement 0x040D.
// There is deliberately no genOnOff Server/state expose.
const attr = {enable: 0xF100, on: 0xF101, off: 0xF102, count: 0xF103, interval: 0xF104};

export default {
    zigbeeModel: Array.from({length: 16}, (_, i) =>
        `Tuya_CO2Sensor_r${(i + 1).toString(16).toUpperCase().padStart(2, '0')}`),
    model: 'Tuya_CO2Sensor_binding',
    vendor: 'Slacky-DIY',
    description: 'Tuya CO2 sensor with threshold-based direct Zigbee binding',
    extend: [
        m.co2(),
        m.temperature(),
        m.humidity(),
        m.binary({
            name: 'co2_control',
            cluster: 'msCO2',
            attribute: {ID: attr.enable, type: 0x10},
            valueOn: ['ON', 1], valueOff: ['OFF', 0],
            description: 'Enable or disable direct CO2 threshold control',
        }),
        m.numeric({
            name: 'co2_on_threshold', cluster: 'msCO2',
            attribute: {ID: attr.on, type: 0x21},
            unit: 'ppm', valueMin: 401, valueMax: 5000, valueStep: 1,
            description: 'Send On when CO2 reaches this threshold',
        }),
        m.numeric({
            name: 'co2_off_threshold', cluster: 'msCO2',
            attribute: {ID: attr.off, type: 0x21},
            unit: 'ppm', valueMin: 400, valueMax: 4999, valueStep: 1,
            description: 'Send Off when CO2 falls to this threshold',
        }),
        m.numeric({
            name: 'co2_repeat_count', cluster: 'msCO2',
            attribute: {ID: attr.count, type: 0x20},
            valueMin: 1, valueMax: 5, valueStep: 1,
            description: 'Total number of repeated On or Off transmissions',
        }),
        m.numeric({
            name: 'co2_repeat_interval', cluster: 'msCO2',
            attribute: {ID: attr.interval, type: 0x21},
            unit: 'ms', valueMin: 500, valueMax: 10000, valueStep: 100,
            description: 'Time between On or Off retransmissions',
        }),
    ],
    ota: true,
};
