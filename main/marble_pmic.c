#include "marble_pmic.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "marble_pmic";

#define AXP2101_I2C_ADDR       0x34
#define AXP2101_REG_CHIP_ID    0x03
#define AXP2101_REG_INTEN1     0x40
#define AXP2101_REG_INTEN2     0x41
#define AXP2101_REG_INTSTS1    0x48
#define AXP2101_REG_INTSTS2    0x49

// In AXP2101 INTSTS2:
// Bit 2: PEK short press status
// Bit 1: PEK positive edge (press release)
#define AXP2101_PEK_SHORT_PRESS_MASK 0x06

static i2c_master_dev_handle_t s_pmic_dev = NULL;

static esp_err_t pmic_read_reg(uint8_t reg, uint8_t *val) {
    if (!s_pmic_dev || !val) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(s_pmic_dev, &reg, 1, val, 1, 50);
}

static esp_err_t pmic_write_reg(uint8_t reg, uint8_t val) {
    if (!s_pmic_dev) return ESP_ERR_INVALID_ARG;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_pmic_dev, buf, 2, 50);
}

esp_err_t marble_pmic_init(i2c_master_bus_handle_t bus_handle) {
    if (!bus_handle) {
        ESP_LOGE(TAG, "Invalid I2C bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags.disable_ack_check = false
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_config, &s_pmic_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register AXP2101 device on I2C bus");
        return ret;
    }

    // Verify communication by reading chip ID or status
    uint8_t chip_id = 0;
    ret = pmic_read_reg(AXP2101_REG_CHIP_ID, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 probe failed (err=%s). Board may not have AXP2101 PMIC at 0x34.", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "AXP2101 PMIC detected (Chip ID: 0x%02X)", chip_id);

    // Enable PEK short press interrupt in INTEN2 (Reg 0x41)
    // Bit 2 = PEK short press IRQ, Bit 3 = PEK long press IRQ
    ret = pmic_write_reg(AXP2101_REG_INTEN2, 0x0E);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable PEK IRQ in INTEN2");
    }

    // Clear any existing pending IRQ flags
    uint8_t status = 0;
    if (pmic_read_reg(AXP2101_REG_INTSTS2, &status) == ESP_OK && status != 0) {
        pmic_write_reg(AXP2101_REG_INTSTS2, status);
    }
    if (pmic_read_reg(AXP2101_REG_INTSTS1, &status) == ESP_OK && status != 0) {
        pmic_write_reg(AXP2101_REG_INTSTS1, status);
    }

    ESP_LOGI(TAG, "AXP2101 PEK key detection active");
    return ESP_OK;
}

bool marble_pmic_poll_power_key(void) {
    if (!s_pmic_dev) {
        return false;
    }

    uint8_t status = 0;
    esp_err_t ret = pmic_read_reg(AXP2101_REG_INTSTS2, &status);
    if (ret == ESP_OK && (status & AXP2101_PEK_SHORT_PRESS_MASK)) {
        // Clear interrupt flag by writing 1 back to the active bits
        pmic_write_reg(AXP2101_REG_INTSTS2, status);
        ESP_LOGI(TAG, "AXP2101 Power key short press detected!");
        return true;
    }

    return false;
}
