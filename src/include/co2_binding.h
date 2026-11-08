#ifndef SRC_INCLUDE_CO2_BINDING_H_
#define SRC_INCLUDE_CO2_BINDING_H_

/* Custom configuration attributes in CO2 Measurement (0x040D). */
#define CO2_BIND_ATTR_ENABLED          0xF100
#define CO2_BIND_ATTR_ON_THRESHOLD     0xF101
#define CO2_BIND_ATTR_OFF_THRESHOLD    0xF102
#define CO2_BIND_ATTR_REPEAT_COUNT     0xF103
#define CO2_BIND_ATTR_REPEAT_INTERVAL  0xF104

void co2_binding_init(void);
void co2_binding_on_sample(int32_t ppm);
void co2_binding_config_written(void);

#endif
