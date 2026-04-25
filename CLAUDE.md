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
- Core 1 idle-task panic (PC=0, InstrFetchProhibited) — TWO STACK FIXES APPLIED.
  Crash signature: PS.EXCM=1 (double exception), A1/A3 in IPC task memory,
  _xt_lowint1 + _frxt_int_enter in registers, backtrace landing in idle task
  frames (red herring from stale Xtensa window-chain saves).
  Fix 1: CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=4096 — IDLE1 had only 976 bytes
  free in AP mode; WiFi level-1 interrupts on Core 1 could overflow it. Fixed
  but did NOT eliminate the crash; crash still occurred after AP config save
  (WiFi STA connect path), meaning a second culprit existed.
  Fix 2: CONFIG_ESP_IPC_TASK_STACK_SIZE=4096 — ipc1 had only 1520 bytes free
  (2048 total); WiFi STA-connect IPC calls pushed it past the limit. The
  overflow corrupted adjacent memory and the next level-1 interrupt dispatched
  through a zeroed handler → PC=0. Fix raises headroom to 3580 bytes free.
  Pending validation: configure WiFi credentials through the portal and
  verify the device survives the STA-connect boot.
- DIG1 top-row annunciator bit mapping is placeholder (marked TODO in
  sony_vfd_pt6315.cpp). Needs bench bit-walk.

## Do not
- Don't propose speculative "maybe this helps" changes. Make one change
  per build cycle so we can attribute cause.
- Don't touch `.git/` or rewrite history.
- Don't run `idf.py fullclean` unless a build is genuinely broken —
  full rebuilds cost 2+ minutes.
EOF

