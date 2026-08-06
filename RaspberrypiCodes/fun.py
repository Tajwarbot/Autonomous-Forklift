import cv2
from pyzbar.pyzbar import decode
import numpy as np
import serial
import serial.tools.list_ports
import time
import subprocess
import shutil
from pathlib import Path


# ================= SERIAL SETTINGS =================

BAUD_RATE = 9600
SEND_INTERVAL = 0.2


# ================= SOUND SETTINGS =================

# Put qr_success.wav inside the Raspberry Pi user's home folder
SOUND_FILE = Path.home() / "qr_success.wav"

# The same QR must disappear for this long before it can
# produce another success sound.
QR_RESET_DELAY = 1.0

# Try these sound players in order
AUDIO_PLAYER = None

for player in ["paplay", "pw-play", "aplay"]:
    if shutil.which(player):
        AUDIO_PLAYER = player
        break


sound_process = None


def play_success_sound():
    """
    Plays the QR success sound without blocking the main program.
    """

    global sound_process

    if not SOUND_FILE.exists():
        print(f"Sound file not found: {SOUND_FILE}")
        return

    if AUDIO_PLAYER is None:
        print("No compatible audio player found.")
        print("Install paplay, pw-play, or aplay.")
        return

    # Do not start another sound while the previous sound is playing
    if sound_process is not None and sound_process.poll() is None:
        return

    try:
        sound_process = subprocess.Popen(
            [AUDIO_PLAYER, str(SOUND_FILE)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )

    except Exception as error:
        print("Sound playback error:", error)


# ================= ARDUINO CONNECTION =================

def find_arduino():

    ports = serial.tools.list_ports.comports()

    for port in ports:

        name = (
            str(port.description)
            + str(port.device)
            + str(port.manufacturer)
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
    arduino.close()
    raise Exception("Camera not detected")


print("QR → Arduino system started")
print("Audio player:", AUDIO_PLAYER)
print("Success sound:", SOUND_FILE)


# ================= PROGRAM VARIABLES =================

current_direction = None

last_send = 0.0

# Used to prevent continuous sound while one QR remains visible
last_announced_direction = None
last_valid_qr_time = 0.0


# ================= MAIN LOOP =================

try:

    while True:

        ret, frame = camera.read()

        if not ret:
            continue


        qr_codes = decode(frame)

        valid_qr_visible = False


        for qr in qr_codes:

            try:
                data = qr.data.decode("utf-8").strip().lower()

            except UnicodeDecodeError:
                data = ""


            # Accept only l or r
            if data in ("l", "r"):

                valid_qr_visible = True
                current_direction = data
                last_valid_qr_time = time.monotonic()


                # Play sound only when:
                # 1. A new valid QR appears, or
                # 2. The direction changes from l to r or r to l
                if data != last_announced_direction:

                    if data == "l":
                        print("QR detected successfully: LEFT")

                    elif data == "r":
                        print("QR detected successfully: RIGHT")


                    play_success_sound()

                    last_announced_direction = data


            # ================= DRAW QR OUTLINE =================

            if qr.polygon:

                points = np.array(
                    [
                        (point.x, point.y)
                        for point in qr.polygon
                    ],
                    np.int32
                )

                points = points.reshape((-1, 1, 2))

                cv2.polylines(
                    frame,
                    [points],
                    True,
                    (0, 255, 0),
                    2
                )


            # Show decoded data above QR code
            cv2.putText(
                frame,
                data,
                (
                    qr.rect.left,
                    max(qr.rect.top - 10, 20)
                ),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 255, 255),
                2
            )


        # Reset the sound lock after the QR disappears
        if (
            not valid_qr_visible
            and last_announced_direction is not None
            and time.monotonic() - last_valid_qr_time >= QR_RESET_DELAY
        ):
            last_announced_direction = None


        # ================= CONTINUOUS SERIAL SEND =================

        if current_direction is not None:

            current_time = time.monotonic()

            if current_time - last_send >= SEND_INTERVAL:

                command = current_direction + "\n"

                try:
                    arduino.write(command.encode("utf-8"))

                    print("Sending:", current_direction)

                except serial.SerialException as error:
                    print("Arduino serial error:", error)
                    break

                last_send = current_time


        # ================= DISPLAY =================

        cv2.imshow(
            "QR Scanner",
            frame
        )


        if cv2.waitKey(1) & 0xFF == ord("q"):
            break


except KeyboardInterrupt:
    print("\nProgram stopped.")


finally:

    camera.release()

    if arduino.is_open:
        arduino.close()

    cv2.destroyAllWindows()

    print("Camera and Arduino connection closed.")
