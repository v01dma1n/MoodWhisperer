// lgl_vfd_studio.cpp — bit-bang implementation. See lgl_vfd_studio.h for the
// protocol description and source of the command bytes.

#include "lgl_vfd_studio.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include <algorithm>

namespace {
    // Half-period delay for the bit-banged clock. The reference Arduino
    // library relies on digitalWrite() call overhead alone for timing; we
    // add an explicit delay since IDF's gpio_set_level() is much faster and
    // the glass's real maximum clock rate isn't documented. 2 us per edge
    // gives roughly a 250 kHz bit clock, comfortably slow for this class of
    // shift-register VFD controller.
    constexpr int CLOCK_HALF_PERIOD_US = 2;

    // Gap the reference library inserts between CS toggles on multi-byte
    // commands (digit count, brightness).
    constexpr int COMMAND_SETTLE_US = 5;
}

LglVfdStudio::LglVfdStudio(int cs_gpio, int clk_gpio, int sdi_gpio, int numDigits)
    : _cs(cs_gpio), _clk(clk_gpio), _sdi(sdi_gpio),
      _numDigits(std::clamp(numDigits, 1, LglVfdConst::MAX_DIGITS)) {}

void LglVfdStudio::begin() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << _cs) | (1ULL << _clk) | (1ULL << _sdi);
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);

    gpio_set_level(static_cast<gpio_num_t>(_cs), 1);
    gpio_set_level(static_cast<gpio_num_t>(_clk), 0);
    gpio_set_level(static_cast<gpio_num_t>(_sdi), 0);

    beginCommand();
    bitBangByte(LglVfdConst::CMD_SET_DIGIT_COUNT);
    esp_rom_delay_us(COMMAND_SETTLE_US);
    bitBangByte(digitCountByte());
    endCommand();

    setBrightness(0xFF);
}

void LglVfdStudio::bitBangByte(uint8_t data) {
    for (int i = 0; i < 8; ++i) {
        gpio_set_level(static_cast<gpio_num_t>(_clk), 0);
        gpio_set_level(static_cast<gpio_num_t>(_sdi), data & 0x01);
        data >>= 1;
        esp_rom_delay_us(CLOCK_HALF_PERIOD_US);
        gpio_set_level(static_cast<gpio_num_t>(_clk), 1);
        esp_rom_delay_us(CLOCK_HALF_PERIOD_US);
    }
}

void LglVfdStudio::beginCommand() {
    gpio_set_level(static_cast<gpio_num_t>(_cs), 0);
}

void LglVfdStudio::endCommand() {
    gpio_set_level(static_cast<gpio_num_t>(_cs), 1);
    esp_rom_delay_us(COMMAND_SETTLE_US);
}

uint8_t LglVfdStudio::digitCountByte() const {
    return static_cast<uint8_t>(_numDigits - 1);
}

void LglVfdStudio::writeChar(uint8_t position, char c) {
    beginCommand();
    bitBangByte(LglVfdConst::CMD_WRITE_BASE + position);
    bitBangByte(static_cast<uint8_t>(c));
    endCommand();

    beginCommand();
    bitBangByte(LglVfdConst::CMD_LATCH);
    endCommand();
}

void LglVfdStudio::writeString(uint8_t position, const char* str) {
    if (!str) return;

    beginCommand();
    bitBangByte(LglVfdConst::CMD_WRITE_BASE + position);
    while (*str) bitBangByte(static_cast<uint8_t>(*str++));
    endCommand();

    beginCommand();
    bitBangByte(LglVfdConst::CMD_LATCH);
    endCommand();
}

void LglVfdStudio::setBrightness(uint8_t brightness) {
    beginCommand();
    bitBangByte(LglVfdConst::CMD_SET_BRIGHTNESS);
    esp_rom_delay_us(COMMAND_SETTLE_US);
    bitBangByte(brightness);
    endCommand();
}

void LglVfdStudio::clear() {
    char blanks[LglVfdConst::MAX_DIGITS + 1];
    int n = std::min(_numDigits, LglVfdConst::MAX_DIGITS);
    for (int i = 0; i < n; ++i) blanks[i] = ' ';
    blanks[n] = '\0';
    writeString(0, blanks);
}
