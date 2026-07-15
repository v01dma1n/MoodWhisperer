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
    // Writing a non-zero Q9.7 value here both enables compensation and sets the rate.
    // Writing zero disables it.
    static constexpr uint8_t CROSSTALK_COMPENSATION_PEAK_RATE = 0x20;
    static constexpr uint8_t RESULT_PEAK_SIGNAL_RATE          = 0x1A; // Q9.7 MCPS, big-endian
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

// Probe every 7-bit address and log the ones that ACK. Called on init
// failure so the log shows whether the bus is dead (wiring/power) or the
// sensor answered at an unexpected address.
static void scanBus(i2c_port_t port) {
    LOGINF("I2C scan on port %d:", (int)port);
    int found = 0;
    for (uint8_t a = 0x08; a <= 0x77; ++a) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (a << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        if (i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(10)) == ESP_OK) {
            LOGINF("  device ACK at 0x%02X", a);
            ++found;
        }
        i2c_cmd_link_delete(cmd);
    }
    if (!found) {
        LOGINF("  no devices ACKed — bus is dead, check SDA/SCL wiring and "
               "sensor power");
    }
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
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_FAIL) {
        LOGERR("VL53L0X I2C install failed: %d", err);
        return false;
    }

    // Check model ID. Read it raw so a NACK (dead bus / missing sensor)
    // is distinguishable from a chip that answered with the wrong ID.
    uint8_t idReg = 0xC0;
    uint8_t model = 0;
    esp_err_t iderr = i2c_master_write_read_device(
        _port, _addr, &idReg, 1, &model, 1, pdMS_TO_TICKS(10));
    if (iderr != ESP_OK || model != 0xEE) {
        LOGERR("VL53L0X not found (addr 0x%02X, i2c err %d, model 0x%02X, "
               "want 0xEE)", _addr, iderr, model);
        scanBus(_port);
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
    _lastStatus     = readReg(Reg::RESULT_RANGE_STATUS);
    _lastSignalRate = readReg16(Reg::RESULT_PEAK_SIGNAL_RATE);
    uint16_t mm = readReg16(0x1E);
    writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);
    return mm;
}

uint16_t Vl53l0xDriver::performXtalkCalibration(int samples) {
    // Fire single-shot measurements with no target present (open air or
    // black absorber at ≥500 mm).  The averaged peak signal rate is the
    // glass-reflection crosstalk that hardware subtraction will remove.
    uint32_t sum   = 0;
    int      valid = 0;

    for (int i = 0; i < samples; ++i) {
        // Stop-variable preamble required before every single-shot trigger.
        writeReg(0x80, 0x01); writeReg(0xFF, 0x01); writeReg(0x00, 0x00);
        writeReg(0x91, _stopVariable);
        writeReg(0x00, 0x01); writeReg(0xFF, 0x00); writeReg(0x80, 0x00);
        writeReg(Reg::SYSRANGE_START, 0x01);  // single shot

        bool timedOut = false;
        int64_t t = esp_timer_get_time();
        while ((readReg(Reg::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
            if (esp_timer_get_time() - t > TIMEOUT_US) { timedOut = true; break; }
            vTaskDelay(1);
        }
        writeReg(Reg::SYSRANGE_START, 0x00);

        if (timedOut) {
            LOGERR("Xtalk calibration: sensor not responding — aborting");
            return 0;
        }
        sum += readReg16(Reg::RESULT_PEAK_SIGNAL_RATE);  // Q9.7 MCPS at 0x1A
        ++valid;
        writeReg(Reg::SYSTEM_INTERRUPT_CLEAR, 0x01);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint16_t rate = (valid > 0) ? (uint16_t)(sum / valid) : 0;
    LOGINF("Xtalk calibration: %d/%d valid samples, rate = %u (Q9.7 MCPS)",
           valid, samples, (unsigned)rate);
    return rate;
}

void Vl53l0xDriver::applyXtalkCompensation(uint16_t rateMcps) {
    // Register 0x20 is FixPoint 3.13 (ST API: FIXPOINT1616TOFIXPOINT313),
    // not Q9.7 like the RESULT_PEAK_SIGNAL_RATE we calibrate from. Convert
    // (<<6) and clamp: 3.13 tops out at ~8 MCPS — cover glass reflecting
    // more than that cannot be fully compensated in hardware and needs an
    // optical fix (light-barrier gasket between sensor and glass).
    uint32_t q313 = (uint32_t)rateMcps << 6;
    if (q313 > 0xFFFF) {
        LOGERR("Xtalk %u Q9.7 (%.1f MCPS) exceeds the ~8 MCPS compensation "
               "ceiling — clamping. Ranging through this glass will stay "
               "degraded until the optical path improves.",
               (unsigned)rateMcps, rateMcps / 128.0f);
        q313 = 0xFFFF;
    }
    writeReg16(Reg::CROSSTALK_COMPENSATION_PEAK_RATE, (uint16_t)q313);
    LOGINF("Xtalk compensation %s (%.1f MCPS, reg=0x%04X)",
           rateMcps ? "enabled" : "disabled",
           rateMcps / 128.0f, (unsigned)q313);
}
