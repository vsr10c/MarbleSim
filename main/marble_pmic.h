#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize AXP2101 PMIC PEK (Power Key) interrupt detection
 * 
 * Configures the onboard AXP2101 PMIC on the shared I2C bus at address 0x34
 * to capture PEK short press events.
 * 
 * @param bus_handle Shared I2C master bus handle from BSP
 * @return ESP_OK if PMIC was detected and configured, ESP_FAIL otherwise
 */
esp_err_t marble_pmic_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Non-blockingly poll AXP2101 PMIC for PEK short-press status
 * 
 * Checks the hardware interrupt status register and automatically clears the
 * write-1-to-clear flag if a short press occurred.
 * 
 * @return true if a power key short press was detected, false otherwise
 */
bool marble_pmic_poll_power_key(void);

#ifdef __cplusplus
}
#endif
