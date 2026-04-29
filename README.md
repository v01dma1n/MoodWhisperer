# MoodWhisperer

An ESP-IDF firmware scaffold for a Sony HT-CT550W front-panel VFD turned
standalone NTP clock with a twist: it whispers mood-driven motivational
and demotivational quotes between time/date scenes.

This is the IDF port of the architecture used by
[GustavClock](https://github.com/v01dma1n/GustavClock) (Arduino / MAX6921)
built on top of
[ESP32NTPClock2](https://github.com/v01dma1n/ESP32NTPClock2) and
[ESP32NTPClockDrivers2](https://github.com/v01dma1n/ESP32NTPClockDrivers2).
Class names, extension points, and file layout mirror the originals, so
switching between the Arduino and IDF variants should feel familiar.

---

## Hardware

| Part              | Role                                           |
|-------------------|------------------------------------------------|
| Sony 1-883-796-11 | HT-CT550W/UC2 front panel (11 × 16-seg VFD)    |
| Princeton PT6315  | VFD controller (44-pin LQFP)                   |
| ESP32-WROOM       | MCU (38-pin HiLetgo dev board works)           |

CN803 wiring (electrical order — **ignore the mirrored silkscreen labels on
side A**):

| Pin | Signal   | ESP32                |
|-----|----------|----------------------|
| 1   | E3.3V    | 3V3                  |
| 3   | DGND     | GND                  |
| 4   | FL_CLK   | GPIO 18 (VSPI SCK)   |
| 5   | FL_CS    | GPIO 5  (VSPI SS)    |
| 6   | FL_DATA  | GPIO 23 (VSPI MOSI)  |
| 10  | SW4V     | 4 – 5 V supply       |
| 11  | DGND_FL  | GND                  |

Optional add-on hardware (all present on the target PCB):

| Part             | Role                                               |
|------------------|----------------------------------------------------|
| VL53L0X          | Time-of-flight distance sensor (I2C, addr 0x29)   |
| DS1307 (HW-111)  | Battery-backed RTC (I2C, addr 0x68)                |
| 2× WS2812 clone  | Mood / ambient LEDs ("MoodLeds")                   |

Other GPIO usage:

| GPIO | Role                                                       |
|------|------------------------------------------------------------|
| 0    | BOOT button — hold 3 s to enter AP / config mode           |
| 2    | Built-in blue LED — brief flash on every WiFi/IP event     |
| 21   | I2C SDA (shared: VL53L0X + DS1307)                         |
| 22   | I2C SCL (shared: VL53L0X + DS1307, 100 kHz)                |
| 26   | WS2812 data (2 LEDs, warm amber glow)                      |

SPI settings are 1 MHz, MODE 0, framing is LSB-first (the driver
pre-reverses each byte so IDF's MSB-first bus can emit PT6315 frames
correctly).

---

## Project layout

```
MoodWhisperer/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions/
│   └── partitions.csv
├── components/
│   ├── esp32_ntp_clock/            # git submodule → ESP32NTPClock2 (the "engine")
│   │   ├── include/
│   │   └── src/
│   └── esp32_ntp_clock_drivers/    # git submodule → ESP32NTPClockDrivers2
│       ├── include/                # + DispDriverPT6315 for the Sony board
│       └── src/
└── main/                           # the MoodWhisperer application
    ├── main.cpp
    ├── whisperer_app.{h,cpp}
    ├── whisperer_preferences.{h,cpp}
    ├── whisperer_access_point_manager.{h,cpp}
    ├── vfd_hardware_map.h
    └── version.h
```

---

## Architecture

### 1. Engine (`components/esp32_ntp_clock`)

Port of ESP32NTPClock2. Same class names, different backends:

| Arduino original             | IDF port                                            |
|------------------------------|-----------------------------------------------------|
| `Preferences`                | `nvs_flash` / `nvs_handle_t`                        |
| `ESPAsyncWebServer`          | `esp_http_server` + hand-rolled DNS hijack          |
| `SimpleFSM`                  | 5-state switch inside `ClockFsmManager`             |
| Arduino `SPI` + Arduino SPI  | `driver/spi_master`                                 |
| `HTTPClient`                 | `esp_http_client`                                   |
| `RTClib` (DS1307)            | dropped — the ESP32's internal RTC is sufficient    |
| `Serial.printf`              | `esp_log` macros behind a runtime level gate        |

Key interfaces, unchanged in shape from the Arduino project:

- `IBaseClock` — the contract an app fulfils (SSID, timezone, display, etc.)
- `IDisplayDriver` — one-frame-at-a-time hardware abstraction
- `IAnimation` — per-animation scratch buffer + `update()` / `isDone()`
- `BaseConfig` — generic credentials struct, designed to be subclassed
- `BasePreferences` — NVS-backed loader/saver; subclass to add fields
- `BaseAccessPointManager` — soft-AP + captive portal + form renderer;
  subclass and override `initializeFormFields()` to add rows
- `BaseNtpClockApp` — the "engine" an app inherits from

Everything else (animations, scene manager, FSM, boot manager, SNTP,
WiFi) is internal to the engine and used through those interfaces.

### 2. Drivers (`components/esp32_ntp_clock_drivers`)

Only one driver is relevant for MoodWhisperer — `DispDriverPT6315`. It
wraps a lower-level `SonyVfdPt6315` class (the IDF port of the Arduino
`SonyVFD` library) behind the `IDisplayDriver` interface. Unlike the
MAX6921, the PT6315 multiplexes internally, so there's no need for a
high-priority multiplexing task on core 1.

The 16-segment "Union Jack" glyph table (`sony_vfd_font.cpp`) is ported
verbatim from the Arduino `SonyVFDFont.h`.

### 3. Application (`main/`)

`WhispererApp` is a straight descendant of `BaseNtpClockApp`, one class
per concern just like GustavClock:

- `WhispererPreferences : BasePreferences` — adds display brightness,
  startup-anim toggle, and mood settings on top of `BaseConfig`.
- `WhispererAccessPointManager : BaseAccessPointManager` — adds the
  matching rows to the captive portal.
- `WhispererApp : BaseNtpClockApp` — owns the display driver, the
  display manager, the mood provider, the quote manager, the scene
  playlist, the VL53L0X distance sensor, the DS1307 RTC, and the
  MoodLeds instance. Houses the three quote-trigger mode state
  machines (Classic / Thermal / Geiger).
- `MoodLeds` — two WS2812 LEDs with seven internal states spanning
  simple fade-in/out (Classic), three-phase thermal overload ramp,
  and continuous sine-wave breathing with adaptive frequency (Geiger).

---

## Mood system

The user spec was "-1 = very bad, +1 = very good". That is implemented
as-written:

```cpp
class MoodProvider {
public:
    virtual ~MoodProvider() = default;
    virtual float currentMood() = 0;   // always in [-1.0, +1.0]
};
```

Two concrete sources ship in the engine:

- `RandomMoodProvider` — fresh random value on every call. Default for
  bringup when no real mood source exists yet.
- `FixedMoodProvider` — returns a user-chosen constant. Useful for
  demos, and as a manual override the captive portal exposes.

To wire a real mood source later (barometric pressure? cal entries?
button count? LLM call?), subclass `MoodProvider`, write `currentMood()`,
and hand it to `QuoteManager` in `WhispererApp::refreshMoodProvider()`.

`QuoteManager` keeps a table of `Quote{text, minMood, maxMood}` entries,
picks uniformly at random from those whose mood window overlaps the
current mood, and avoids repeating the last few picks. The default table
lives at the top of `quote_manager.cpp` — drop in your own quotes there,
or pass a custom table to the constructor from `WhispererApp`.

---

## Scene playlist

Defined in `whisperer_app.cpp::s_scenePlaylist`. Each entry:

| Field              | Meaning                                                    |
|--------------------|------------------------------------------------------------|
| `scene_name`       | label for logs                                             |
| `format_string`    | strftime() if it looks like one, else snprintf() template  |
| `animation_type`   | `STATIC_TEXT` / `SLOT_MACHINE` / `MATRIX` / `SCROLLING`    |
| `duration_ms`      | how long the scene stays up                                |
| `isLiveUpdate`     | re-render once per second (seconds ticker)                 |
| `anim_param_1/2`   | animation-specific timing in ms                            |
| `getDataValue()`   | returns a float that fills `%f` tokens                     |

The quote scene uses a small trick: its `format_string` points at a
mutable static buffer, and its `getDataValue()` callback refreshes that
buffer with the next quote when the scene starts. Because the buffer has
no `%` specifiers, `snprintf()` just copies it verbatim. No engine
changes required.

---

## Quote trigger modes

The distance sensor and mood LEDs cooperate to trigger and frame quotes.
The active mode is selected in the captive portal ("Quote Trigger Mode")
and persisted to NVS.

---

### Classic

The original behaviour.

- Idle: LEDs are off.
- Trigger: any distance reading that deviates from the slow-drifting
  ambient baseline by ≥ `DISTANCE_CHANGE_THRESHOLD_MM` (default 300 mm)
  fires a quote.
- LED sequence: **fade in** (2 s) → quote scrolls once the LED is fully
  lit → **fade out** (2 s) → scenes resume once the LED is dark.
- 30-second cooldown between triggers (`QUOTE_COOLDOWN_MS`).

---

### Thermal Overload

Presence-based: sustained proximity builds up "heat" until the clock
vents.

**Phase A — Warning** (person enters < 1 m zone)
: LEDs snap to 30% and hold. No quote yet.

**Phase B — Overload** (3 s continuous presence inside 1 m)
: LEDs ramp from 30% up to a 60%–100% sine throb (~1.5 Hz).

**Phase C — Vent** (triggered automatically once throb starts)
: Quote scrolls (LEDs continue throbbing throughout). When the
scroll ends, LEDs cut off instantly, display brightness drops to 0,
and a 5-second cooldown begins. Cooldown ends only after **both**
the 5 s have elapsed **and** the person has backed beyond 1 m.

If the person backs away during Phase A or B, the mode resets to
idle without venting.

---

### Geiger Proximity Pulse

Proximity-reactive ambient lighting with continuous breathing.

**Always on while in `RUNNING_NORMAL`:**  
LEDs breathe with a sine-wave cycle. The frequency is driven by
deviation from an adaptive ambient baseline:

| Deviation from ambient | LED pulse rate |
|------------------------|----------------|
| 0 mm (no movement)     | ~0.2 Hz (5 s cycle — calm) |
| ~200 mm                | ~2.5 Hz (nervous) |
| ≥ 400 mm               | 5 Hz (rapid Geiger-counter tick) |

The ambient baseline is a slow exponential moving average
(`0.98 × old + 0.02 × new` at 10 Hz) that tracks environmental
background — desk ceiling, wall, chair — so the mode works
correctly in confined spaces without manual calibration.

**Quote trigger:**  
Same ≥ 300 mm delta / 30-second cooldown as Classic.

**LED during quote:**  
LEDs snap to 100% and hold for the full scroll duration, then
`startGeiger()` resumes breathing at the current distance-derived
frequency.

---

## Weather

MoodWhisperer can pull current temperature (°F) and relative
humidity from [OpenWeatherMap](https://openweathermap.org/) and
display them as scenes in the rotation.

Configure in the portal:

- **OpenWeatherMap API Key** — free account at openweathermap.org,
  "Current Weather Data" tier is sufficient.
- **OWM City** — city name as the OWM API expects it, e.g. `New York,US`
  or `Warsaw,PL`. Spaces are fine; the firmware URL-encodes them.

Once both fields are set, the firmware fetches weather once on the first
RUNNING\_NORMAL loop tick, then every 10 minutes. Until the first
successful fetch the temperature and humidity scenes show `---`.

Scenes added to the playlist:

| Scene | Format   | Example   |
|-------|----------|-----------|
| Temp  | `%.1f F` | `72.3 F`  |
| Hum   | `%.0f PCT` | `65 PCT` |

The fetch blocks AppTask for up to 5 s (the HTTP timeout). DisplayTask
runs independently on Core 1 so the display continues updating during
the fetch.

---

## Captive portal and timezone handling

**Answer to "can we calculate time zone automatically, or must it be in
setup?":** yes, but not purely — automatic detection needs working WiFi,
and we need the captive portal *before* WiFi. The split:

1. First boot / no credentials stored → AP mode → user enters SSID/password,
   optionally picks a timezone from the dropdown. Defaults to UTC.
2. After WiFi connects successfully, `ClockFsmManager` calls
   `geoFetchPosixTz()` if the stored timezone is blank.
   `ip-api.com/json/?fields=timezone` returns an IANA name ("America/New_York"),
   which gets mapped to one of the POSIX strings in `tz_data.cpp`.
3. The suggestion is logged but not silently persisted — the user is
   expected to confirm it in the portal before save.

Bottom line: **auto-detection is a nice-to-have fill-in, not a substitute
for the setup step.** The portal always has the final say.

The portal is open (no password), rendered in a dark monospace theme
from a single HTTP response (no CDN, no external JS/CSS) at
`http://192.168.4.1/`. Every DNS query on the AP network gets
redirected to the AP's own IP so iOS / Android / macOS / Windows all
pop the captive portal sheet automatically.

Trigger AP mode at any time by holding the BOOT button (GPIO 0) for
3 seconds. The GPIO and hold duration are set in `vfd_hardware_map.h`
(`AP_TRIGGER_GPIO`) and `ClockFsmManager::requestApMode()`.

---

## Build

Requirements: ESP-IDF ≥ 5.1.

The `components/esp32_ntp_clock` and `components/esp32_ntp_clock_drivers`
directories are git submodules. Initialise them after cloning:

```bash
git clone --recurse-submodules https://github.com/v01dma1n/MoodWhisperer.git
# or, inside an existing clone:
git submodule update --init
```

Then build and flash:

```bash
. $IDF_PATH/export.sh
cd MoodWhisperer
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

First boot: the board comes up in AP mode (no credentials stored).
Connect your phone to the open `mood-whisperer` network, the captive
portal opens, fill in WiFi + timezone, tap save.

---

## Outstanding work (inherited from the Sony VFD integration)

Two bit-level unknowns carry over from the Arduino driver and need a
one-session bit-walk on the bench to lock down:

1. **DIG1 top-row annunciator bits.** The placeholders in
   `SonyVfdPt6315::annunciatorMap()` for `ANN_AAC_LPCM` ... `ANN_MSTR_HI_RES`
   are guesses. Walk bit by bit and update.
2. **Per-digit annunciator bit.** Assumed to be SG17 (bit 0 of byte 2).
   Verify with e.g. `ANN_NIGHT` and update if wrong.

Neither blocks clock operation — the time/date/quote scenes work without
any annunciators lit.

Other nice-to-haves:

- A real `MoodProvider` wired to a live signal — barometric pressure
  trend from a BME280 is a natural fit; the interface is one method.
- Refactor the quote scene hack (mutable buffer as `format_string`) into
  a first-class `CustomTextSource` scene type in the engine.
- An `IRtcDriver` interface in `BaseNtpClockApp` so the DS1307 init /
  NTP write-back boilerplate moves out of the app class.

---

## License

MIT (matches the upstream Arduino projects).
