# Autonomous Forklift - Quick Reference

## System Overview

Vision-guided autonomous material handling robot with dual-processor architecture:
- **Raspberry Pi 4B**: QR code detection and routing decisions
- **Arduino Mega 2560**: Real-time motor control and sensor reading

---

## Hardware Architecture

**Power Distribution**:
- 3S LiPo Battery (12V) → Buck Converter #1 (5.15V) → Raspberry Pi
- 3S LiPo Battery (12V) → Buck Converter #2 (5.0V) → Logic & Servo
- Motors: Direct 12V supply via BTS7960 drivers

**Key Connections**:
- Raspberry Pi ↔ Arduino: Serial UART (GPIO 14/15 ↔ Pins 18/19, 9600 baud)
- Motor drivers: 2x BTS7960, 4 DC motors (2 per side)
- Servo: Pin 5 (5V PWM)
- IR Sensors: Analog pins A0-A5

---

## Process Flow

**Mission Sequence**:
1. Power ON → Self-check
2. Calibrate IR sensors
3. Follow black line to pickup station
4. Scan QR code (get route direction: L or R)
5. Pickup box (servo up)
6. Navigate to delivery station following QR-guided route
7. Drop box (servo down)
8. Return to pickup station
9. Repeat

---

## Code Deployment

### Arduino Mega 2560

**File**: `Codes/ArduinoMega with safety codes.ino`

**Upload via**:
- Arduino IDE → Board: "Arduino Mega 2560" → Sketch → Upload
- Baud rate: 115200 (USB debug)

**Primary Functions**:
- `line_follow()`: Main control loop - IR sensor reading and motor speed adjustment
- `motor(int a, int b)`: PWM output to BTS7960 drivers (-255 to +255)
- `lift_box()` / `drop_box()`: Servo control (0° up, 180° down)
- `cal()`: Sensor calibration on startup
- `serial_read_pi()`: Receive 'l'/'r' commands from Raspberry Pi

**Debug Output**: USB Serial @ 115200 baud shows real-time state

---

### Raspberry Pi 4B

**File**: `Codes/RaspberryPi`

**Dependencies**:
```bash
pip install opencv-python pyzbar pyserial numpy
```

**Run**:
```bash
python Codes/RaspberryPi
```

**Function**:
- Captures camera video
- Detects QR codes using ZBar
- Extracts direction data ('l' or 'r')
- Sends commands to Arduino via serial (/dev/ttyS0 @ 9600 baud)

**Expected Output**:
```
Data found: r
Sent to Arduino: r
```

---

## Pin Configuration

| Component | Arduino Pin | Details |
|-----------|-------------|---------|
| Left Motor Forward | 9 (PWM) | BTS7960 RPWM |
| Left Motor Backward | 8 (PWM) | BTS7960 LPWM |
| Right Motor Forward | 12 (PWM) | BTS7960 RPWM |
| Right Motor Backward | 11 (PWM) | BTS7960 LPWM |
| Servo (Fork Lift) | 5 (PWM) | Standard servo (0°-180°) |
| IR Sensors | A0-A5 | 6-channel line sensor array |
| Calibration Button | Pin 3 | INPUT_PULLUP to GND |
| Serial (Pi Link) | 18 (TX1) / 19 (RX1) | 9600 baud |

---

## Operating Instructions

**Calibration** (run once on new track):
1. Power on Arduino
2. Press button **once**
3. Robot rotates while recording sensor min/max values to EEPROM
4. Completes in ~5 seconds

**Autonomous Run**:
1. Start Raspberry Pi script: `python Codes/RaspberryPi`
2. Press Arduino button **twice** to enter line-following mode
3. Place robot on black line
4. Position QR codes at junctions
5. Robot proceeds: pickup → navigate → QR route selection → delivery → return

---

## Quick Tuning

In `ArduinoMega with safety codes.ino`:

```cpp
float spl = 8;  // Left motor speed (adjust for balance)
float spr = 8;  // Right motor speed

#define u_turn_delay 350      // Time to confirm station arrival
#define u_turn_pause 5000     // Settle time after pickup/drop
#define station_unlock_time 250  // Time on normal line to unlock station
```

**If robot overshoots**: Decrease spl/spr (6-7)  
**If robot is too slow**: Increase spl/spr (10-12)  
**If robot re-triggers station**: Increase station_unlock_time (300-400)

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Robot circles | Recalibrate sensors (press button 1x) |
| QR not detected | Improve lighting, check QR size (5-15cm), verify /dev/ttyS0 |
| Serial fails | Verify 9600 baud, check GND connection, test: `cat /dev/ttyS0` |
| Motor imbalance | Adjust spl/spr ratio independently |
| Station re-triggers | Enable safety version, increase station_unlock_time |

---

## Documentation

- **Full technical details**: See README.md
- **Project proposal**: Proposal/autonomous_material_handling_robot_proposal_fixed.pdf
- **CAD models**: CAD/GrabCAD/ (reference components)

---

**Status**: Active Development | **Updated**: August 2026
