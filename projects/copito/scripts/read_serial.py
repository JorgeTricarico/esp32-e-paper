import serial
import time

try:
    ser = serial.Serial('COM3', 115200, timeout=2)
    ser.dtr = True
    ser.rts = False
    print("[+] Conectado a COM3. Esperando datos durante 3 segundos...")
    start = time.time()
    while time.time() - start < 3:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='replace').strip()
            if line:
                print(f"ESP32: {line}")
        time.sleep(0.05)
    ser.close()
except Exception as e:
    print(f"[!] Error serial: {e}")
