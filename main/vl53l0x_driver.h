#pragma once
#include "driver/i2c.h"
#include <cstdint>

// Minimal VL53L0X Time-of-Flight ranging driver for ESP-IDF.
// Ported from the Pololu Arduino library (MIT).  Only what MoodWhisperer needs:
//   init → startContinuous → readRangeMm in a polling loop.

class Vl53l0xDriver {
public:
    explicit Vl53l0xDriver(i2c_port_t port, int sda, int scl, uint8_t addr = 0x29);

    bool     init();              // install I2C + bring up the sensor
    void     startContinuous();   // back-to-back measurements, no inter-measurement period
    int      readRangeMm();       // blocks until next sample ready; -1 on timeout/error

    // Glass crosstalk calibration.  Call after init(), before startContinuous().
    // Fires `samples` single-shot measurements and returns the averaged peak signal
    // rate (Q9.7 MCPS) — the glass reflection the sensor sees with no target present.
    // Store the returned value in NVS and pass it to applyXtalkCompensation() on
    // every subsequent boot.
    uint16_t performXtalkCalibration(int samples = 50);

    // Write `rateMcps` to CROSSTALK_COMPENSATION_PEAK_RATE_MCPS (0x20).
    // A non-zero value enables hardware subtraction; zero disables it.
    // Call after init(), before startContinuous().
    void     applyXtalkCompensation(uint16_t rateMcps);

private:
    i2c_port_t _port;
    int        _sda, _scl;
    uint8_t    _addr;
    uint8_t    _stopVariable = 0;  // saved during init, restored before each burst

    void     writeReg(uint8_t reg, uint8_t val);
    void     writeReg16(uint8_t reg, uint16_t val);
    void     writeReg32(uint8_t reg, uint32_t val);
    uint8_t  readReg(uint8_t reg);
    uint16_t readReg16(uint8_t reg);
    void     readMulti(uint8_t reg, uint8_t* buf, uint8_t len);
    void     writeMulti(uint8_t reg, const uint8_t* buf, uint8_t len);

    bool getSpadInfo(uint8_t* count, bool* aperture);
    bool performRefCalibration(uint8_t vhvInitByte);
    void loadTuningSettings();
};
