import serial
import time
import sys

PORT = "COM5"
BAUD = 115200

try:
    s = serial.Serial(PORT, BAUD, timeout=0.2)
    print(f"Connected to {PORT} @ {BAUD}. Press reset on the ESP32 to see boot output. Ctrl+C to exit.")
except Exception as e:
    print(f"Could not open {PORT}: {e}")
    sys.exit(1)

try:
    while True:
        data = s.read(s.in_waiting or 1)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
except KeyboardInterrupt:
    print("\nSerial monitor closed.")
finally:
    s.close()