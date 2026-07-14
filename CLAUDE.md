# MoodWhisperer — agent instructions

## Project
ESP-IDF firmware for a Sony HT-CT550W VFD (PT6315 controller) turned into
an NTP clock with mood-driven quotes. See README.md for architecture.

## Hardware
- ESP32-WROOM dev board, wired to the VFD via VSPI (SCK=18, MOSI=23, CS=5).
- Serial port on the host: /dev/ttyUSB0 (adjust if different).

## Build / flash / monitor loop

Always run from repo root. IDF is already sourced in the user's shell.

idf.py build
idf.py -p /dev/ttyUSB0 flash
timeout 45 idf.py -p /dev/ttyUSB0 monitor 2>&1 | tee /tmp/vfdw_monitor.log

`idf.py monitor` needs an interactive TTY. From an agent shell use the
capture script instead:

python tools/serial_capture.py /dev/ttyUSB0 45 /tmp/vfdw_monitor.log
python tools/serial_capture.py /dev/ttyUSB0 45 /tmp/vfdw_monitor.log --no-reset

The default form pulses EN (hard reset) and captures the boot. Use
--no-reset to eavesdrop on a boot already in progress — two resets
within 10 s are a double reset and force AP mode, so back-to-back
captures with reset will land you in the portal.

Then read `/tmp/vfdw_monitor.log` and look for:
- Compile errors — fix and rebuild.
- `Guru Meditation Error` — parse the `---` decoded backtrace lines, match
  against source files under `components/` and `main/`.
- `***ERROR*** A stack overflow in task <name>` — bump that task's stack
  where it's created (search the codebase for the task name).
- `abort()` / `assert failed` — look at the source line in the trace.

If the board doesn't boot cleanly or NVS seems corrupt:

idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash

This wipes WiFi credentials — the device will come up in AP mode
(`mood-whisperer`, open) and need portal reconfiguration.

## Conventions
- Edit `sdkconfig.defaults`, never `sdkconfig` directly. After editing
  defaults, run `idf.py reconfigure` before the next build.
- The engine is in `components/esp32_ntp_clock/`. The PT6315 driver is in
  `components/esp32_ntp_clock_drivers/`. The application is in `main/`.
- Base class names (BaseConfig, BaseAccessPointManager, BaseNtpClockApp,
  IBaseClock, IDisplayDriver) are chosen to mirror ESP32NTPClock on
  Arduino. Preserve them when refactoring.
- Comments and logs match the existing casual, specific voice. No emoji,
  no marketing words, no "comprehensive solution" phrasing.

## Known issues / in flight
- DIG1 top-row annunciator bit mapping is placeholder (marked TODO in
  sony_vfd_pt6315.cpp). Needs bench bit-walk.
- NTP timeout with no DS1307 fitted drops the FSM into AP_MODE even when
  WiFi is up (clock_fsm_manager.cpp, NTP_SYNC case). Bites bench boards
  without the RTC; consider RUNNING_NORMAL with unsynced time + background
  NTP retry instead.
- WiFiConnect() issues esp_wifi_connect() twice per attempt (STA_START
  handler + retry loop in components/esp32_wifi/src/wifi_connector.cpp).
  Harmless in testing but worth deduplicating.
- Home-network context (2026-07-14): the irhome RT-AC3200 refuses 802.11
  auth (status 1, Broadcom IE) to ALL new 2.4 GHz clients — suspected nvram
  exhaustion, masked by its nightly 4 AM reboot. Bench work uses the irlab
  AP (NETGEAR, 2.4 GHz only, AP mode on the main LAN). reason=202 against
  irhome is the router, not this firmware.

## Resolved (kept for crash-signature reference)
- Core 1 idle-task panic (PC=0, InstrFetchProhibited): PS.EXCM=1, A1/A3 in
  IPC task memory, backtrace in idle-task frames (red herring from stale
  Xtensa window-chain saves). Fixed by CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=4096
  and CONFIG_ESP_IPC_TASK_STACK_SIZE=4096. Validated 2026-07-14 across many
  portal-save/STA-connect cycles.
- Portal saves silently persisted loglevel=ERROR and dropped brightness/mood
  inputs: form shuttle buffers were smaller than the MAX_PREF_STRING_LEN
  write that applyFormBody() performs; the overflow zeroed cJSON global_hooks
  and s_logLevelBuffer in .data. All str_pref buffers must be
  MAX_PREF_STRING_LEN bytes.
- Sticky AP mode (one failed boot forced AP forever): the double-reset flag
  now clears after a 10 s window or on entering AP mode, not only on
  RUNNING_NORMAL.

## Do not
- Don't propose speculative "maybe this helps" changes. Make one change
  per build cycle so we can attribute cause.
- Don't touch `.git/` or rewrite history.
- Don't run `idf.py fullclean` unless a build is genuinely broken —
  full rebuilds cost 2+ minutes.
EOF

