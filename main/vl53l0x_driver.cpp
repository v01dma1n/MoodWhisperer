#include "vl53l0x_driver.h"
#include "logging.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

// VL53L0X register addresses (abbreviated — only what we use).
namespace Reg {
    static constexpr uint8_t SYSRANGE_START              = 0x00;
    static constexpr uint8_t SYSTEM_SEQUENCE_CONFIG      = 0x01;
    static constexpr uint8_t SPAD_REF_EN_START_OFFSET    = 0x4F;
    static constexpr uint8_t SPAD_NUM_REQUESTED_REF      = 0x4E;
    static constexpr uint8_t GLOBAL_REF_EN_START_SELECT  = 0xB6;
    static constexpr uint8_t RESULT_INTERRUPT_STATUS     = 0x13;
    static constexpr uint8_t RESULT_RANGE_STATUS         = 0x14;
    static constexpr uint8_t CROSSTALK_COMPENSATION_EN   = 0x20;
    static constexpr uint8_t FINAL_RANGE_CONFIG_MIN_CNT  = 0x44;
    static constexpr uint8_t MSRC_CONFIG_CONTROL         = 0x60;
    static constexpr uint8_t GLOBAL_SPAD_ENABLES_REF_0   = 0xB0;
    static constexpr uint8_t SYSTEM_INTERRUPT_CLEAR      = 0x0B;
    static constexpr uint8_t OSC_CALIBRATE_VAL           = 0xF8;
}

// Timeout for any blocking wait (calibration, measurement ready).
static constexpr int64_t TIMEOUT_US = 500'000;  // 500 ms

Vl53l0xDriver::Vl53l0xDriver(i2c_port_t port, int sda, int scl, uint8_t addr)
    : _port(port), _sda(sda), _scl(scl), _addr(addr) {}

// --- I2C helpers -----------------------------------------------------------

void Vl53l0xDriver::writeReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_master_write_to_device(_port, _addr, buf, 2, pdMS_TO_TICKS(10));
}

void Vl53l0xDriver::writeReg16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)val};
    i2c_master_write_to_device(_port, _addr, buf, 3, pdMS_TO_TICKS(10));
}

void Vl53l0xDriver::writeReg32(uint8_t reg, uint32_t val) {
    uint8_t buf[5] = {reg,
        (uint8_t)(val >> 24), (uint8_t)(val >> 16),
        (uint8_t)(val >> 8),  (uint8_t)val};
    i2c_master_write_to_device(_port, _addr, buf, 5, pdMS_TO_TICKS(10));
}

uint8_t Vl53l0xDriver::readReg(uint8_t reg) {
    uint8_t val = 0;
    i2c_master_write_read_device(_port, _addr, &reg, 1, &val, 1, pdMS_TO_TICKS(10));
    return val;
}

uint16_t Vl53l0xDriver::readReg16(uint8_t reg) {
    uint8_t buf[2] = {};
    i2c_master_write_read_device(_port, _addr, &reg, 1, buf, 2, pdMS_TO_TICKS(10));
    return ((uint16_t)buf[0] << 8) | buf[1];
}

void Vl53l0xDriver::readMulti(uint8_t reg, uint8_t* dst, uint8_t len) {
    i2c_master_write_read_device(_port, _addr, &reg, 1, dst, len, pdMS_TO_TICKS(10));
}

void Vl53l0xDriver::writeMulti(uint8_t reg, const uint8_t* src, uint8_t len) {
    // Prepend register address into a local buffer (max expected is 7 bytes).
    uint8_t buf[16];
    buf[0] = reg;
    for (uint8_t i = 0; i < len && i < 15; ++i) buf[1 + i] = src[i];
    i2c_master_write_to_device(_port, _addr, buf, len + 1, pdMS_TO_TICKS(10));
}

// --- SPAD calibration data -------------------------------------------------

bool Vl53l0xDriver::getSpadInfo(uint8_t* count, bool* aperture) {
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, readReg(0x83) | 0x04);
    writeReg(0xFF, 0x07);
    writeReg(0x81, 0x01);
    writeReg(0x80, 0x01);
    writeReg(0x94, 0x6B);
    writeReg(0x83, 0x00);

    int64_t t = esp_timer_get_time();
    while (readReg(0x83) == 0x00) {
        if (esp_timer_get_time() - t > TIMEOUT_US) return false;
        vTaskDelay(1);
    }

    writeReg(0x83, 0x01);
    uint8_t tmp = readReg(0x92);
    *count    = tmp & 0x7F;
    *aperture = (tmp >> 7) & 0x01;

    writeReg(0x81, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, readReg(0x83) & ~0x04);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);
    return true;
}

// --- Single reference calibration step ------------------------------------

bool Vl53l0xDriver::performRefCalibration(uint8_t vhvInitByte) {
    writeReg(Reg::SYSRANGE_START, 0x01 | vhvInitByte);

    int64_t t = esp_timer_get_time();
    while ((readReg(Reg::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
        if (esp_timer_get_time() - t > TIMEOUT_US) return false;
        vTaskDelay(1);
    }
    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);
    writeReg(Reg::SYSRANGE_START, 0x00);
    return true;
}

// --- ST-mandated tuning settings -------------------------------------------

void Vl53l0xDriver::loadTuningSettings() {
    // Verbatim from the ST VL53L0X API / Pololu port.
    writeReg(0xFF, 0x01); writeReg(0x00, 0x00);
    writeReg(0xFF, 0x00); writeReg(0x09, 0x00);
    writeReg(0x10, 0x00); writeReg(0x11, 0x00);
    writeReg(0x24, 0x01); writeReg(0x25, 0xFF);
    writeReg(0x75, 0x00);
    writeReg(0xFF, 0x01); writeReg(0x4E, 0x2C);
    writeReg(0x48, 0x00); writeReg(0x30, 0x20);
    writeReg(0xFF, 0x00); writeReg(0x30, 0x09);
    writeReg(0x54, 0x00); writeReg(0x31, 0x04);
    writeReg(0x32, 0x03); writeReg(0x40, 0x83);
    writeReg(0x46, 0x25); writeReg(0x60, 0x00);
    writeReg(0x27, 0x00); writeReg(0x50, 0x06);
    writeReg(0x51, 0x00); writeReg(0x52, 0x96);
    writeReg(0x56, 0x08); writeReg(0x57, 0x30);
    writeReg(0x61, 0x00); writeReg(0x62, 0x00);
    writeReg(0x64, 0x00); writeReg(0x65, 0x00);
    writeReg(0x66, 0xA0);
    writeReg(0xFF, 0x01); writeReg(0x22, 0x32);
    writeReg(0x47, 0x14); writeReg(0x49, 0xFF);
    writeReg(0x4A, 0x00);
    writeReg(0xFF, 0x00); writeReg(0x7A, 0x0A);
    writeReg(0x7B, 0x00); writeReg(0x78, 0x21);
    writeReg(0xFF, 0x01); writeReg(0x23, 0x34);
    writeReg(0x42, 0x00); writeReg(0x44, 0xFF);
    writeReg(0x45, 0x26); writeReg(0x46, 0x05);
    writeReg(0x40, 0x40); writeReg(0x0E, 0x06);
    writeReg(0x20, 0x1A); writeReg(0x43, 0x40);
    writeReg(0xFF, 0x00); writeReg(0x34, 0x03);
    writeReg(0x35, 0x44);
    writeReg(0xFF, 0x01); writeReg(0x31, 0x04);
    writeReg(0x4B, 0x09); writeReg(0x4C, 0x05);
    writeReg(0x4D, 0x04);
    writeReg(0xFF, 0x00); writeReg(0x44, 0x00);
    writeReg(0x45, 0x20); writeReg(0x47, 0x08);
    writeReg(0x48, 0x28); writeReg(0x67, 0x00);
    writeReg(0x70, 0x04); writeReg(0x71, 0x01);
    writeReg(0x72, 0xFE); writeReg(0x76, 0x00);
    writeReg(0x77, 0x00);
    writeReg(0xFF, 0x01); writeReg(0x0D, 0x01);
    writeReg(0xFF, 0x00); writeReg(0x80, 0x01);
    writeReg(0x01, 0xF8);
    writeReg(0xFF, 0x01); writeReg(0x8E, 0x01);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00); writeReg(0x80, 0x00);
}

// --- Public API ------------------------------------------------------------

bool Vl53l0xDriver::init() {
    // Install I2C master (tolerate already-installed).
    i2c_config_t conf = {};
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = _sda;
    conf.scl_io_num       = _scl;
    conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;  // DS1307 on the same bus caps at 100 kHz
    i2c_param_config(_port, &conf);
    esp_err_t err = i2c_driver_install(_port, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOGERR("VL53L0X I2C install failed: %d", err);
        return false;
    }

    // Check model ID.
    if (readReg(0xC0) != 0xEE) {
        LOGERR("VL53L0X not found (model ID mismatch)");
        return false;
    }

    // Standard init sequence (from ST API via Pololu).
    writeReg(0x88, 0x00);
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    _stopVariable = readReg(0x91);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);

    // Disable MSRC and pre-range signal rate limit checks.
    writeReg(Reg::MSRC_CONFIG_CONTROL, readReg(Reg::MSRC_CONFIG_CONTROL) | 0x12);

    // Set final-range signal rate limit: 0.25 MCPS (fixed-point Q9.7 = 32).
    writeReg16(Reg::FINAL_RANGE_CONFIG_MIN_CNT, 32);

    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0xFF);

    // SPAD calibration.
    uint8_t spadCount;
    bool    spadAperture;
    if (!getSpadInfo(&spadCount, &spadAperture)) {
        LOGERR("VL53L0X getSpadInfo failed");
        return false;
    }

    uint8_t refSpadMap[6];
    readMulti(Reg::GLOBAL_SPAD_ENABLES_REF_0, refSpadMap, 6);

    writeReg(0xFF, 0x01);
    writeReg(Reg::SPAD_REF_EN_START_OFFSET, 0x00);
    writeReg(Reg::SPAD_NUM_REQUESTED_REF,   0x2C);
    writeReg(0xFF, 0x00);
    writeReg(Reg::GLOBAL_REF_EN_START_SELECT, 0xB4);

    uint8_t firstSpad = spadAperture ? 12 : 0;
    uint8_t spadsEnabled = 0;
    for (int i = 0; i < 48; ++i) {
        if (i < firstSpad || spadsEnabled == spadCount) {
            refSpadMap[i / 8] &= ~(1 << (i % 8));
        } else if ((refSpadMap[i / 8] >> (i % 8)) & 0x1) {
            ++spadsEnabled;
        }
    }
    writeMulti(Reg::GLOBAL_SPAD_ENABLES_REF_0, refSpadMap, 6);

    loadTuningSettings();

    writeReg(0x0A, 0x04);  // SYSTEM_INTERRUPT_CONFIG_GPIO = new sample ready
    writeReg(0x84, readReg(0x84) & ~0x10);  // GPIO_HV_MUX_ACTIVE_HIGH: active low
    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);

    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0xE8);

    // Timing budget: 33 ms (default).
    // (We use the default and skip the full budget recalculation.)

    // VHV + Phase calibration.
    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0x01);
    if (!performRefCalibration(0x40)) {
        LOGERR("VL53L0X VHV calibration failed");
        return false;
    }
    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0x02);
    if (!performRefCalibration(0x00)) {
        LOGERR("VL53L0X phase calibration failed");
        return false;
    }

    writeReg(Reg::SYSTEM_SEQUENCE_CONFIG, 0xE8);

    LOGINF("VL53L0X initialised (stop_variable=0x%02X)", _stopVariable);
    return true;
}

void Vl53l0xDriver::startContinuous() {
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    writeReg(0x91, _stopVariable);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);
    // Continuous ranging mode (no inter-measurement period).
    writeReg(Reg::SYSRANGE_START, 0x02);
}

int Vl53l0xDriver::readRangeMm() {
    int64_t t = esp_timer_get_time();
    while ((readReg(Reg::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
        if (esp_timer_get_time() - t > TIMEOUT_US) return -1;
        vTaskDelay(1);
    }
    // RESULT_RANGE_STATUS+10 = 0x1E gives range in mm (big-endian).
    uint16_t mm = readReg16(0x1E);
    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);
    return mm;
}
