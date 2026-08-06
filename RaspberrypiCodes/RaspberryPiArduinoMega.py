import cv2
from pyzbar.pyzbar import decode
import numpy as np
import serial
import serial.tools.list_ports
import time

# CHANGE THIS after checking which port the Arduino Mega enumerates as
# Windows: e.g. "COM5"   |   Linux/Mac: e.g. "/dev/ttyACM0" or "/dev/ttyUSB0"
ARDUINO_PORT = "COM5"
BAUD_RATE = 9600  # must match Serial.begin() on the Mega

# ---------- SERIAL CONNECT ----------
def connect():
    s = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=2)
    time.sleep(2)  # allow Mega to reset after serial connection opens
    return s

client = connect()

# ---------- CAMERA ----------
cap = cv2.VideoCapture(0)
last_data = None

print("QR → Arduino Mega Serial system started")

while True:
    ret, frame = cap.read()
    if not ret:
        continue

    codes = decode(frame)
    for code in codes:
        data = code.data.decode("utf-8")

        # avoid spam
        if data != last_data:
            print("Sending:", data)
            try:
                client.write((data + "\n").encode())
                response = client.readline().decode().strip()
                print("Arduino:", response)
            except Exception as e:
                print("Reconnecting...", e)
                try:
                    client.close()
                except:
                    pass
                time.sleep(1)
                client = connect()
            last_data = data

        # draw QR box
        pts = np.array([p for p in code.polygon], np.int32)
        pts = pts.reshape((-1, 1, 2))
        cv2.polylines(frame, [pts], True, (0, 255, 0), 2)

        x, y, w, h = code.rect
        cv2.putText(frame, data, (x, y - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                    (0, 255, 255), 2)

    cv2.imshow("QR Scanner", frame)
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
client.close()
cv2.destroyAllWindows()
