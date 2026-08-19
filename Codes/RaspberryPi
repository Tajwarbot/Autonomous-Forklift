import cv2
from pyzbar.pyzbar import decode
import numpy as np
import serial
import time

# ================== SERIAL CONFIG ==================
# Change this if needed: /dev/ttyUSB0, /dev/ttyACM0, /dev/serial0
SERIAL_PORT = "/dev/ttyS0"
BAUD_RATE = 9600

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
time.sleep(2)  # allow microcontroller reset after opening serial

# ================== CAMERA ==================
cap = cv2.VideoCapture(0)

print("Starting QR Code Scanner with ZBar... Press 'q' to quit.")

# Optional anti-spam (don't send same command too rapidly)
last_sent = None
last_send_time = 0
SEND_INTERVAL = 0.3  # seconds

while True:
    ret, img = cap.read()

    if not ret or img is None:
        if cv2.waitKey(100) == ord("q"):
            break
        continue

    detected_codes = decode(img)

    for code in detected_codes:
        # Decode QR text
        data = code.data.decode("utf-8").strip()
        print("Data found:", data)

        # Draw QR polygon
        pts = np.array([point for point in code.polygon], dtype=np.int32)
        pts = pts.reshape((-1, 1, 2))
        cv2.polylines(img, [pts], isClosed=True, color=(255, 0, 0), thickness=2)

        # Draw decoded text
        x, y, w, h = code.rect
        text_y = y - 10 if (y - 10) > 20 else y + 30
        cv2.putText(
            img,
            data,
            (x, text_y),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (255, 250, 120),
            2
        )

        # ================== SERIAL SEND ==================
        # Your QR contains only "l" or "r"
        cmd = data.lower()
        if cmd in ("l", "r"):
            now = time.time()
            if cmd != last_sent or (now - last_send_time) > SEND_INTERVAL:
                ser.write(cmd.encode("utf-8"))  # send exactly one character
                ser.flush()
                print("Sent to Arduino:", cmd)
                last_sent = cmd
                last_send_time = now

    cv2.imshow("ZBar Code Detector", img)

    if cv2.waitKey(1) == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
ser.close()
