import serial
import time

s = serial.Serial("COM5", 115200, timeout=3)
time.sleep(0.5)

# Slower SPI timing
s.write(b"P")
time.sleep(2.0)  # long wait for programming mode entry
resp = s.read(64)
print("Enter programming:", repr(resp))

# Read signature byte 0 with long delays between each command
s.write(b"S")
time.sleep(1.0)
sig0 = s.read(1)
print(f"Signature[0]: {sig0.hex() if sig0 else 'none'}")

time.sleep(0.5)
s.write(b"S")
time.sleep(1.0)
sig1 = s.read(1)
print(f"Signature[1]: {sig1.hex() if sig1 else 'none'}")

time.sleep(0.5)
s.write(b"S")
time.sleep(1.0)
sig2 = s.read(1)
print(f"Signature[2]: {sig2.hex() if sig2 else 'none'}")

# Read fuses
s.write(b"F")
time.sleep(1.0)
fuses = s.read(3)
print(f"Fuses: {fuses.hex() if len(fuses)==3 else fuses.hex()}")

s.write(b"L")
time.sleep(0.3)
s.read(64)
s.close()