import cv2
from pyzbar.pyzbar import decode
import numpy as np
import serial
import serial.tools.list_ports
import time


# ================= SERIAL SETTINGS =================

BAUD_RATE = 9600


def find_arduino():

    ports = serial.tools.list_ports.comports()

    for port in ports:

        name = (
            port.description +
            port.device +
            str(port.manufacturer)
        ).lower()

        if (
            "arduino" in name
            or "ch340" in name
            or "usb" in name
            or "ttyacm" in name
        ):
            return port.device

    return None



arduino_port = find_arduino()


if arduino_port is None:
    raise Exception("Arduino Mega not found")


print("Arduino found:", arduino_port)


arduino = serial.Serial(
    arduino_port,
    BAUD_RATE,
    timeout=1
)


time.sleep(2)

arduino.reset_input_buffer()



# ================= CAMERA =================

camera = cv2.VideoCapture(0)


if not camera.isOpened():
    raise Exception("Camera not detected")



print("QR → Arduino system started")



current_direction = None


SEND_INTERVAL = 0.2

last_send = 0



while True:


    ret, frame = camera.read()


    if not ret:
        continue



    qr_codes = decode(frame)



    for qr in qr_codes:


        data = qr.data.decode(
            "utf-8"
        ).strip().lower()



        # Accept only l or r

        if data == "l":

            current_direction = "l"

            print("QR detected: LEFT")


        elif data == "r":

            current_direction = "r"

            print("QR detected: RIGHT")



        points = np.array(
            [
                (point.x, point.y)
                for point in qr.polygon
            ],
            np.int32
        )


        points = points.reshape(
            (-1,1,2)
        )


        cv2.polylines(
            frame,
            [points],
            True,
            (0,255,0),
            2
        )


        cv2.putText(
            frame,
            data,
            (
                qr.rect.left,
                qr.rect.top - 10
            ),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0,255,255),
            2
        )



    # ================= CONTINUOUS SEND =================


    if current_direction is not None:


        if time.time() - last_send >= SEND_INTERVAL:


            command = current_direction + "\n"


            arduino.write(
                command.encode()
            )


            print(
                "Sending:",
                current_direction
            )


            last_send = time.time()




    cv2.imshow(
        "QR Scanner",
        frame
    )


    if cv2.waitKey(1) & 0xff == ord("q"):
        break




camera.release()

arduino.close()

cv2.destroyAllWindows()
