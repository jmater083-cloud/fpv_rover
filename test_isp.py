import serial
import time

s = serial.Serial("COM5", 115200, timeout=2)
time.sleep(0.5)

# Enter programming mode
s.write(b"P")
time.sleep(0.3)
resp = s.read(64)
print("Enter programming:", repr(resp))

# Read signature
s.write(b"S")
time.sleep(0.3)
sig = s.read(3)
print("Signature:", sig.hex(), "->", end=" ")
if sig == b"\x1e\x95\x0f":
    print("ATmega328P (UNO) - CORRECT!")
elif sig == b"\x1e\x95\x14":
    print("ATmega328PB")
elif sig == b"\x1e\x95\x0a":
    print("ATmega328")
elif sig == b"\x1e\x95\x02":
    print("ATmega328P (old)")
else:
    print("Unknown / no response")

# Read fuses
s.write(b"F")
time.sleep(0.3)
fuses = s.read(3)
print("Fuses (low, high, ext):", fuses.hex())

# Leave programming mode
s.write(b"L")
time.sleep(0.3)
resp = s.read(64)
print("Leave programming:", repr(resp))

s.close()