#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_service_init(void);
esp_err_t i2c_service_add_device(uint16_t address,
                                 uint32_t scl_speed_hz,
                                 i2c_master_dev_handle_t *device);
esp_err_t i2c_service_probe(uint16_t address, int timeout_ms);
i2c_master_bus_handle_t i2c_service_bus_handle(void);

#ifdef __cplusplus
}
#endif
