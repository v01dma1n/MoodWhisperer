import sys, time
import serial

port, seconds = sys.argv[1], float(sys.argv[2])
out = sys.argv[3]

no_reset = len(sys.argv) > 4 and sys.argv[4] == "--no-reset"

s = serial.Serial(None, 115200, timeout=1)
s.port = port
if no_reset:
    # Deassert DTR/RTS before opening so the CP210x doesn't pulse
    # EN/IO0 — we want to eavesdrop on the boot already in progress,
    # not restart it (a reset here re-arms the double-reset AP flag).
    s.dtr = False
    s.rts = False
s.open()
if not no_reset:
    # Hard-reset the board like esptool does: pulse EN via RTS with DTR high.
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)

deadline = time.time() + seconds
with open(out, "wb") as f:
    while time.time() < deadline:
        data = s.read(4096)
        if data:
            f.write(data)
            f.flush()
s.close()
