#!/usr/bin/env python3
"""Flash ESP32-CAM by toggling DTR/RTS for download mode, then running esptool.
Usage: python flash_board.py <COM_PORT> <BINARY_PATH>
"""
import serial
import time
import subprocess
import sys

if len(sys.argv) < 3:
    print("Usage: python flash_board.py <COM_PORT> <BINARY_PATH>")
    sys.exit(1)

port = sys.argv[1]
binary = sys.argv[2]
esptool_path = r"C:\Users\jimma\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe"

print(f"[*] Preparing {port} for download mode...")
s = serial.Serial(port, 115200, timeout=2)
# Step 1: Ensure default state
s.dtr = True
s.rts = True
time.sleep(0.05)

# Step 2: Hold GPIO0 (RTS) low, assert reset (DTR) low
s.rts = False   # GPIO0=0 -> FLASH/download mode
s.dtr = False   # RST=0   -> reset chip
time.sleep(0.15)

# Step 3: Release reset (DTR high) while keeping GPIO0 low (RTS low)
s.dtr = True    # RST=1   -> release reset, chip boots into ROM bootloader in download mode
time.sleep(0.15)

# Step 4: Release GPIO0 (RTS high) - chip stays in download mode
s.rts = True    # GPIO0=1 -> normal, still in download mode
time.sleep(0.5)  # Wait for ROM bootloader to be ready

s.close()
time.sleep(0.05)

# Step 5: Run esptool with --before no-reset (we already did the reset)
print(f"[*] Running esptool to flash {binary}...")
result = subprocess.run([
    esptool_path,
    "--before", "no-reset",
    "--after", "hard-reset",
    "--baud", "115200",
    "--port", port,
    "write_flash", "-z",
    "0x0",
    binary
], timeout=60)

if result.returncode == 0:
    print(f"[+] Flash SUCCESSFUL on {port}!")
else:
    print(f"[-] Flash FAILED on {port} (return code: {result.returncode})")
sys.exit(result.returncode)
