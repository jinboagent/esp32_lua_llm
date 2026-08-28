import serial
import time
import sys

port = "COM12"
baud = 115200
cmd = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else "STATUS"

ser = serial.Serial(port, baud, timeout=5)
time.sleep(0.5)  # Wait for serial to settle

# Flush any pending data
ser.reset_input_buffer()

# Send command
ser.write((cmd + "\n").encode())
ser.flush()
print(f"> {cmd}")

# Read response (wait up to 5 seconds)
time.sleep(1)
while ser.in_waiting > 0:
    line = ser.readline().decode(errors='replace').strip()
    if line:
        print(f"< {line}")

# For SCAN START, also read a few seconds of scan output
if "SCAN START" in cmd:
    print("Waiting for scan data...")
    ser.timeout = 3
    for _ in range(5):
        line = ser.readline().decode(errors='replace').strip()
        if line:
            print(f"< {line}")

ser.close()
