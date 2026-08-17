import serial
import sys
import time

PORT = "COM5"
BAUD = 115200
DURATION = 6  # seconds to capture after reset

s = serial.Serial(PORT, BAUD, timeout=0.2)

# Reset the ESP32 to NORMAL boot (IO0 stays HIGH while reset is released)
# RTS controls EN, DTR controls IO0
s.setDTR(False)  # IO0 = HIGH (normal boot)
s.setRTS(True)   # EN = LOW, hold in reset
time.sleep(0.1)
s.setRTS(False)  # release reset -> normal boot
time.sleep(0.1)

print(f"--- ESP32-CAM serial capture (COM5 @ {BAUD}, {DURATION}s) ---")
start = time.time()
while time.time() - start < DURATION:
    data = s.read(max(1, s.in_waiting))
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
print("\n--- capture end ---")
s.close()