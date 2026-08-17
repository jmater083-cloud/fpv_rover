import serial

for port in ["COM5", "COM8"]:
    try:
        s = serial.Serial(port, 115200, timeout=0.5)
        print(f"{port} opened OK")
        s.close()
    except Exception as e:
        print(f"{port} error: {e}")