# Vision-Guided Autonomous Forklift

This repository contains a mechatronics project for a **vision-guided autonomous material handling robot** (student prototype) that follows a guided path, classifies product routing via QR codes, and delivers to specified stations.

## Project Overview

The autonomous forklift system combines:
- **Hardware Architecture**: Raspberry Pi + Arduino Mega dual-processor control system
- **Navigation System**: Infrared line-sensor based path following with 6-sensor array
- **Vision Processing**: Camera-based QR code detection using OpenCV and ZBar
- **Actuation**: PWM-controlled DC motors (BTS7960 drivers) and servo-driven fork-lifter
- **Safety Mechanisms**: State-locking to prevent duplicate station triggers

**System Architecture**:

```
┌─────────────────────────────────────────────────────┐
│    Raspberry Pi (High-Level Vision & Logic)          │
│  - QR Code Detection (OpenCV + ZBar)                 │
│  - Route Decision Processing                         │
│  - Serial Command Transmission (9600 baud)           │
└──────────────────┬──────────────────────────────────┘
                   │ Serial Port (/dev/ttyS0)
┌──────────────────┴──────────────────────────────────┐
│   Arduino Mega (Real-Time Motor & Sensor Control)    │
│  - Line Following (6x IR Sensors on Analog A0-A5)    │
│  - Motor Control (2x BTS7960 Drivers)                │
│  - Servo Actuation (Fork Lift, Pin 5)                │
│  - Debug Output (USB Serial @ 115200 baud)           │
└─────────────────────────────────────────────────────┘
```

## Repository Structure

```
Autonomous-Forklift/
├── Codes/
│   ├── ArduinoMega.ino
│   │   Core firmware for real-time motor and sensor control
│   │
│   ├── ArduinoMega with safety codes.ino
│   │   Enhanced version with station re-trigger protection
│   │
│   └── RaspberryPi
│       Python script for QR code detection and serial communication
│
├── Proposal/
│   └── autonomous_material_handling_robot_proposal_fixed.pdf
│       Complete project documentation and system specifications
│
├── CAD/
│   └── GrabCAD/
│       Mechanical and electronics reference models
│
└── README.md (this file)
```

## Code Guidelines

### Arduino Mega: Real-Time Control Layer

#### ArduinoMega.ino - Core Firmware

**Purpose**: Primary firmware for real-time control of motors, sensors, and servo actuation.

**Key Functions**:

| Function | Purpose |
|----------|---------|
| `setup()` | Initialize GPIO pins, load EEPROM calibration, establish serial connections (USB debug at 115200 baud, Raspberry Pi at 9600 baud) |
| `line_follow()` | Main control loop that reads IR sensors, computes robot position relative to line, and adjusts motor speeds accordingly |
| `motor(int a, int b)` | PWM motor control via BTS7960 drivers; accepts values from -255 to +255 (negative = reverse, zero = stop) |
| `reading()` | Converts 6 analog IR sensor readings into binary pattern; computes position offset for steering corrections |
| `cal()` | Calibration routine executed on startup; sweeps motors across track while recording minimum and maximum sensor values to EEPROM |
| `lift_box()` / `drop_box()` | Servo commands to raise (0 degrees) or lower (180 degrees) the fork mechanism |
| `brake()` | Applies emergency reverse thrust then coasts to stop for safe deceleration |
| `serial_read_pi()` | Non-blocking serial receive function that decodes 'l' (left) or 'r' (right) commands from Raspberry Pi |

**Pin Configuration**:

```
Motor Control (BTS7960 Drivers):
  Pin 8   (PWM) = Left Motor Backward
  Pin 9   (PWM) = Left Motor Forward
  Pin 11  (PWM) = Right Motor Backward
  Pin 12  (PWM) = Right Motor Forward

Servo Actuator:
  Pin 5   (PWM) = Fork Lift (0 degrees = up, 180 degrees = down)

Sensors:
  Analog A0-A5 = 6x IR Line Sensors

Control Input:
  Pin 3 = Calibration/Run Button (INPUT_PULLUP to GND)

Serial Connections:
  USB = Debug output (Serial, 115200 baud)
  Pin 18 (TX1) = Transmit to Raspberry Pi
  Pin 19 (RX1) = Receive from Raspberry Pi (9600 baud)
```

**Sensor Fusion Algorithm**:

The robot converts 6 analog sensor readings into a binary 6-bit pattern:

```
Sensor pattern: 0b001100 (example)
                  ^^^^^^
                  ||||||_ Leftmost sensor (bit 0)
                  |||||__ ...
                  ||||___ ...
                  |||____ Center-left
                  ||_____ Center-right
                  |______ Rightmost sensor (bit 5)

Position tracking:
  - pos = 0: Robot centered on line
  - pos = +4: Robot drifting right (increase left motor speed)
  - pos = -4: Robot drifting left (increase right motor speed)
```

**State Machine**:

The robot operates in distinct states:

```
flag variable (line following direction):
  's' = Straight line ahead
  'l' = Detected left-side line branch
  'r' = Detected right-side line branch

cross variable (junction status):
  's' = No junction detection
  'l' / 'r' = Awaiting QR code decision at intersection

box_loaded: Boolean flag
  false = Empty (proceed to pickup station)
  true = Carrying box (proceed to drop station)

returning: Boolean flag
  false = Initial outbound route
  true = Return journey (route reversed)
```

**Operational Sequence**:

1. **Initialization**: Button press count determines mode
   - Press once: Enter calibration mode
   - Press twice: Enter autonomous line-following mode

2. **Line Following**: Continuous loop reading sensors and adjusting motor output

3. **Junction Detection**: When 6 sensors detect full black area (sum == 6)
   - Motors stop
   - Arduino waits for serial command from Raspberry Pi
   - Receives 'l' or 'r', sets turning direction

4. **Station Detection**: When sensors detect white area (sum == 0) while centered
   - 350ms timeout to confirm station arrival
   - Lifts box (if empty) or drops box (if loaded)
   - 5-second settle pause
   - Performs U-turn and resumes line following

5. **Calibration Mode**: Motor sweeps track while recording sensor bounds
   - Values stored to EEPROM (survives power cycles)
   - Must be run on new track or after sensor maintenance

**Tunable Parameters**:

```cpp
// Timing (milliseconds)
#define node_delay 200        // Pause at junction before moving
#define u_turn_delay 350      // Detection threshold for white station
#define u_turn_pause 5000     // Settle time after box pickup/drop
#define brake_time 50         // Deceleration duration
#define turn_brake 80         // Brake pause during line recovery after turn

// Motor speed multipliers (adjust for track conditions)
float spl = 8;                // Left motor output multiplier
float spr = 8;                // Right motor output multiplier
```

**Tuning Guidelines**:

| Symptom | Cause | Adjustment |
|---------|-------|------------|
| Too fast on curves | Overshoot and line loss | Decrease `spl`/`spr` to 6-7 |
| Too slow overall | Insufficient motor power | Increase `spl`/`spr` to 10-12 |
| Misses junctions | Timeout too short | Increase `u_turn_delay` |
| Jerky motion at turns | Brake duration too short | Increase `turn_brake` to 100-150 |
| Drifts in turns | Motor imbalance | Adjust `spl` and `spr` ratio independently |

---

#### ArduinoMega with safety codes.ino - Enhanced Safety Version

**Improvements Over Core Version**:

The safety-enhanced variant adds protection against duplicate station processing:

**Key Addition**: Station Re-Trigger Lock

```cpp
bool station_locked = false;           // Flag: station currently processing
uint32_t station_line_since = 0;       // Timestamp of normal line detection
#define station_unlock_time 250        // ms required on normal line to unlock
```

**How It Works**:

1. When station action begins (box pickup/drop), `station_locked` is set to true
2. Robot performs U-turn and returns to line following
3. If sensors briefly detect white again (sensor noise or wide station), station will NOT re-trigger
4. Once robot follows normal black line continuously for 250ms, `station_locked` is reset
5. Robot is then ready for the next station encounter

**When to Use Safety Version**:

- Production environments where missed deliveries cause problems
- Stations with wide white areas that could cause sensor bounce-back
- Tracks with reflective surfaces or variable lighting

---

### Raspberry Pi: High-Level Vision Processing

#### RaspberryPi - QR Code Scanner

**Purpose**: Real-time QR code detection and serial command transmission to Arduino.

**System Requirements**:

```
Hardware:
  - Raspberry Pi 3/4/5
  - USB camera or CSI ribbon camera
  - Serial connection to Arduino Mega (UART or USB adapter)

Software:
  - Python 3.7+
  - opencv-python: Video capture and frame display
  - pyzbar: ZBar QR decoder library
  - pyserial: Serial communication
  - numpy: Array operations

Installation:
  sudo apt update
  sudo apt install python3-pip
  pip install opencv-python pyzbar pyserial numpy
```

**Program Flow**:

```
Initialize:
  1. Open serial port (/dev/ttyS0 @ 9600 baud)
  2. Initialize camera (index 0)
  3. Set anti-spam timer (0.3 second minimum between sends)

Main Loop:
  1. Capture frame from camera
  2. Decode all QR codes in frame using ZBar
  3. For each detected code:
     - Extract text data (should be 'l' or 'r')
     - Draw bounding rectangle around QR code
     - Overlay decoded text on frame
     - If data is 'l' or 'r' and not spam: transmit to Arduino
  4. Display annotated video feed
  5. Check for 'q' key to exit

Cleanup:
  - Release camera
  - Close video window
  - Close serial port
```

**Code Organization**:

| Section | Role |
|---------|------|
| Serial Configuration | Define port and baud rate (adjustable for different systems) |
| Camera Initialization | Create video capture object from camera device |
| Main Loop | Continuous frame capture and processing |
| QR Detection | Use pyzbar to decode barcodes in frame |
| Serial Send Logic | Validate data, apply anti-spam filter, transmit character |
| Display Output | Draw QR boxes and text, show frame in window |

**Anti-Spam Mechanism**:

```python
last_sent = None              # Previously transmitted command
last_send_time = 0            # Timestamp of last send
SEND_INTERVAL = 0.3           # Minimum seconds between sends

Logic:
  if (command != last_sent) OR (time_since_last_send > SEND_INTERVAL):
    send to Arduino
    update timestamp
  else:
    skip (prevent repeated 'r' sends within 0.3 seconds)
```

**Expected Output**:

```
Starting QR Code Scanner with ZBar... Press 'q' to quit.
Data found: r
Sent to Arduino: r
Data found: r
  (skipped - within SEND_INTERVAL)
Data found: l
Sent to Arduino: l
```

**Serial Port Troubleshooting**:

| System | Port to Try |
|--------|------------|
| Raspberry Pi UART | `/dev/ttyS0` (hardware UART) or `/dev/ttyAMA0` (GPIO 14/15) |
| USB Serial Adapter | `/dev/ttyUSB0` or `/dev/ttyUSB1` |
| Arduino Serial Emulation | `/dev/ttyACM0` |

**Verify Connection**:

```bash
# List available serial ports
ls -la /dev/tty*

# Test communication (should show ARDUINO READY)
cat /dev/ttyS0
```

**Common Issues**:

| Issue | Cause | Solution |
|-------|-------|----------|
| Camera not found | No camera device attached | `ls /dev/video*` to verify |
| QR not decoding | Poor image quality | Improve lighting, adjust camera focus, ensure stable QR size 5-15cm |
| Serial timeout | Wrong port or baud rate | Verify `SEND_INTERVAL` setting and port configuration |
| Permission denied | Serial port access | Run with `sudo` or add user to `dialout` group: `sudo usermod -a -G dialout $USER` |
| Repeated same command | Anti-spam too aggressive | Reduce `SEND_INTERVAL` to 0.1-0.2 seconds |

---

## System Architecture Details

### Guided Navigation Subsystem

**Objective**: Follow black line on white floor with autonomous turning at junctions.

**Sensor Array**: 6 infrared line sensors arranged horizontally
- Analog readings (0-1023) converted to binary (black = 0, white = 1)
- Calibration determines threshold for each sensor
- Fusion creates 6-bit pattern for position estimation

**Algorithm**:

```
Straight Line (2-4 sensors active):
  - Smooth proportional motor adjustment
  - Example: sensor pattern 0b001100 (center) -> equal motor speeds
  - Example: sensor pattern 0b000110 (right-shifted) -> reduce right motor

Junction Detection (all 6 sensors active):
  - Full black area indicates multi-way intersection
  - Stop motors, wait for QR command
  - Apply appropriate turning corrections

Station Detection (no sensors active):
  - White area with centered approach (pos near 0)
  - Trigger pickup/drop action after 350ms confirmation
  - Prevents false triggers from shadows or isolated white patches
```

### Vision Decision Layer

**Objective**: Provide real-time routing decisions at junctions using QR codes.

**Input**: QR code data (single character: 'l' or 'r')

**Processing**:

```
At each junction:
  1. Arduino detects full-black intersection
  2. Raspberry Pi captures video frame
  3. ZBar library locates and decodes QR code
  4. Extracted data 'l' or 'r' sent to Arduino via serial
  5. Arduino applies turning logic based on state:
     - Outbound route: turn as specified by QR
     - Return route: turn opposite direction (coming back)
```

**Advantages Over Color Sensing**:

- Unambiguous: 'l' and 'r' are clear instructions
- Robust: Works in varied lighting (compared to color detection)
- Flexible: Can encode additional information if needed
- No learning curve: QR is globally recognized standard

### Motor Control Subsystem

**Hardware**: Two independent 12V DC motors via BTS7960 H-bridge drivers

**Control Method**: PWM (Pulse Width Modulation)

```
BTS7960 Pin Assignment:
  RPWM (Forward):  Arduino pin 9 (left), pin 12 (right)
  LPWM (Backward): Arduino pin 8 (left), pin 11 (right)

PWM Values:
  0    = Motors stopped
  1-127 = Reduced speed
  128-255 = Full speed
  Negative in code = automatic reverse direction mapping
```

**Speed Control Logic**:

```cpp
// Motor speeds vary based on line position
if (sensor == 0b001100) {
  motor(10 * spl, 10 * spr);    // Centered: straight ahead
}
else if (sensor == 0b000100) {
  motor(10 * spl, 9 * spr);     // Slightly right: slow right motor
}
else if (sensor == 0b000011) {
  motor(10 * spl, 0 * spr);     // Far right: only left motor forward
}
// ... 12 total patterns for smooth steering
```

### Servo Actuation Subsystem

**Hardware**: Standard servo motor on Pin 5, 180-degree rotation range

**Fork Positions**:

```
Servo Angle 0°   = Fork fully raised (pickup position)
Servo Angle 180° = Fork fully lowered (drop position)

Action Sequence:
  1. Approach station, centered stop
  2. If empty (box_loaded = false): servo to 0°, wait 1 second (lift)
  3. If loaded (box_loaded = true): servo to 180°, wait 1 second (drop)
  4. Additional 5-second pause for box to settle
  5. Resume line following
```

---

## Operating Instructions

### Calibration Procedure

**Objective**: Record sensor response thresholds for current lighting conditions.

**Steps**:

1. Place robot on black track at any location
2. Power on Arduino Mega
3. Press calibration button ONCE
4. Robot will rotate clockwise in place for 5 seconds
5. Watch Serial Monitor for progress
6. Robot stops automatically when complete
7. Sensor thresholds stored in EEPROM

**Expected Output**:

```
1023 512 256    (max, mid, min for sensor 0)
1024 511 255    (max, mid, min for sensor 1)
...
```

### Autonomous Operation

**Prerequisites**:
- Calibration completed
- Track laid out with black lines on white floor
- QR codes placed at all junctions (containing 'l' or 'r')
- Raspberry Pi script running with camera connected

**Steps**:

1. Power on Raspberry Pi first (wait for boot, ~30 seconds)
2. Start Raspberry Pi script: `python RaspberryPi`
3. Power on Arduino Mega
4. Verify both show "READY" in respective terminals
5. Press Arduino button TWICE to start autonomous mode
6. Robot will proceed with mission:
   - Follow black line to first station
   - Lift box
   - Follow line to first junction
   - Wait for QR code decision
   - Execute turn and continue
   - Arrive at drop station
   - Lower box
   - Return on same route (reversed)
   - Repeat

**Monitoring**:

- Arduino USB terminal shows real-time state and debug messages
- Raspberry Pi terminal displays QR code detections
- Use `DEBUG_SERIAL` flag (1 = verbose, 0 = silent) to control Arduino output verbosity

---

## Expected Robot Behavior

| Scenario | Expected Response | Duration |
|----------|-------------------|----------|
| Powering on | Servo moves to down position, confirms READY | 2 seconds |
| Button press (calibration) | Rotates in place, records sensor values | 5 seconds |
| Robot on black line | Smooth forward motion with micro-corrections | Continuous |
| Drifting off line | Gradual speed adjustment to steer back | 0.5-1.0 seconds |
| Approaching station | Deceleration, gradual stop | 2-3 seconds |
| At pickup station | Box lifted, 5-second pause, U-turn left | 7 seconds |
| At drop station | Box lowered, 5-second pause, U-turn right | 7 seconds |
| At junction without QR | Motors stop, waiting (blocking) | Until QR arrives |
| At junction with 'r' QR | Turn right after processing | 2-3 seconds |
| Serial connection lost | Last command continues; no new decisions | Continuous |

---

## Mechanical Components (CAD Reference)

The `CAD/GrabCAD/` directory contains reference models for:

**Electronics**:
- Raspberry Pi 3/4/5 with GPIO header layout
- Arduino Mega 2560 with pin assignments
- BTS7960 H-bridge motor driver modules
- Standard servo motor dimensions
- 12V DC motor specifications

**Mechanics**:
- Wheel assemblies (diameter, axle diameter, mounting holes)
- Motor coupling and gearbox arrangements
- Fork-lifter linkage and servo attachment points
- IR sensor mounting brackets
- Frame and chassis structure

**Use Case**: Sizing components, checking clearances, planning wire routing and structural integration.

---

## Proposal Document Reference

The `Proposal/autonomous_material_handling_robot_proposal_fixed.pdf` provides comprehensive project documentation:

**Sections**:

1. Abstract and Objectives
   - Mission statement and success criteria
   - Target performance metrics

2. System Architecture Overview
   - Block diagrams showing all subsystems
   - Data flow and control sequences
   - Timing requirements

3. Hardware Bill of Materials
   - Complete component list with specifications
   - Supplier information and costs
   - Power budget and voltage requirements

4. Software Architecture
   - Module interaction diagrams
   - Serial protocol specification
   - State machine definitions

5. Risk Analysis and Mitigation
   - Identified failure modes (sensor noise, motor slip, QR decode errors)
   - Proposed solutions and contingencies
   - Testing recommendations

6. Expected Outcomes
   - Success metrics and validation tests
   - Performance benchmarks

7. Future Enhancement Scope
   - Multi-color product classification
   - Advanced obstacle detection and avoidance
   - Machine learning for improved path planning
   - Wireless remote monitoring

**Recommendation**: Review the Proposal first for system-level context before diving into code implementation.

---

## Troubleshooting Guide

### Electrical Issues

**Robot does not power on**:
- Verify battery voltage (12V nominal)
- Check power connections to motor drivers and Arduino
- Test with multimeter: Arduino should show 5V on VCC

**Arduino not communicating with Raspberry Pi**:
- Verify UART pins (18, 19) connected and GND linked
- Confirm voltage divider on RX line (3.3V logic on Pi, 5V on Arduino)
- Test: Connect to Arduino serial monitor, type 'l' or 'r', should see echo
- Test: Run `cat /dev/ttyS0` on Pi, check incoming data

**No USB debug output**:
- Verify USB cable connected to Arduino
- Select correct COM port in Arduino IDE
- Check baud rate (115200)
- Try different USB port on computer

### Sensor and Navigation Issues

**Robot spins in circles**:
- Run calibration procedure again (sensors may have drifted)
- Check for debris on sensor lenses; clean with soft cloth
- Verify track contrast (black line must be dark, floor must be light)
- Adjust lighting to eliminate shadows

**Robot overshoots turns**:
- Increase `turn_brake` value (80 -> 100-150)
- Reduce `spl` and `spr` values (8 -> 6)
- Check wheel traction; ensure wheels roll freely

**Robot misses junctions**:
- Increase `u_turn_delay` (350 -> 500-600)
- Verify track geometry: black area at junction must be solid
- Check for gaps in black line near junction

**Robot stops mid-route without reaching station**:
- Verify floor is reasonably level (slopes cause drift)
- Check motor cables for loose connections
- Monitor current draw; verify power supply can handle load
- Reduce speed multipliers and try again

### Vision and QR Code Issues

**Camera not detected**:
- Run `ls /dev/video*` to verify camera device exists
- Try different camera index (0, 1, 2) in `cap = cv2.VideoCapture(N)`
- Ensure USB camera has power; check device manager

**QR codes not detected**:
- Ensure QR code contains only 'l' or 'r' (single character)
- Verify QR size: too small (<3cm) or too large (>20cm) won't decode
- Improve lighting; position camera 5-15cm from code
- Test with standard QR code generator online
- Run with debug output to see frame display

**Serial commands not reaching Arduino**:
- Verify serial port setting in code (`/dev/ttyS0` vs `/dev/ttyUSB0`)
- Run: `stty -F /dev/ttyS0 speed` to confirm 9600 baud
- Check permission: `ls -la /dev/ttyS0` (should be readable/writable)
- Test manually: `echo "l" > /dev/ttyS0`

**Repeated commands being sent**:
- Increase `SEND_INTERVAL` (0.3 -> 0.5 seconds)
- Verify debounce logic in Raspberry Pi script
- Check for QR code in camera feed for extended time

### Performance Tuning

**Too slow**:
- Increase `spl` and `spr` (8 -> 10, then 12 if needed)
- Reduce `turn_brake` (80 -> 50) for quicker recovery
- Verify motor power supply voltage (should be 12V)

**Too fast or jerky**:
- Decrease `spl` and `spr` (8 -> 6, then 4 if needed)
- Increase `brake_time` and `turn_brake` for smoother transitions
- Smooth out tuning by using fractional multipliers (e.g., 7.5)

**Inconsistent behavior**:
- Ensure consistent track lighting and surface (reflectance)
- Calibrate sensors with track in normal operating position
- Monitor battery voltage; performance drops as battery drains
- Check for loose wiring causing intermittent contact

---

## Development and Testing Checklist

Before deployment:

- [ ] Mechanical assembly complete and tested (wheels roll freely, servo moves)
- [ ] Electrical connections verified (all 5V and 12V lines correct)
- [ ] Arduino code uploaded without errors
- [ ] Calibration procedure successful; EEPROM populated
- [ ] Test track set up with black line and white floor
- [ ] QR codes generated and positioned at junctions
- [ ] Raspberry Pi script tested with camera feed visible
- [ ] Serial communication tested between Pi and Arduino
- [ ] Robot follows straight line smoothly
- [ ] Robot detects and stops at station (white area)
- [ ] Robot performs U-turn correctly
- [ ] Robot turns left/right at junctions based on QR code
- [ ] Full pickup-classify-drop cycle completed successfully
- [ ] Return journey retraces path correctly
- [ ] Multiple mission cycles run without errors
- [ ] Motor speeds and timing adjusted for consistent performance

---

## Version History

**Current Version**: Safety-Enhanced (August 2026)

**Previous Features**:
- Core line-following and motor control
- Basic QR code junction routing
- Servo-based box pickup and drop

**Latest Improvements**:
- Station re-trigger protection (ArduinoMega with safety codes.ino)
- Enhanced debug output and state visibility
- Robust junction timeout handling
- Improved serialization protocol documentation

---

## References and Resources

- **Arduino Mega 2560 Datasheet**: https://store.arduino.cc/products/arduino-mega-2560-rev3
- **BTS7960 Motor Driver Guide**: H-bridge PWM control documentation
- **ZBar QR Decoder**: http://zbar.sourceforge.net/
- **OpenCV Documentation**: https://docs.opencv.org/
- **Raspberry Pi GPIO Reference**: https://www.raspberrypi.org/documentation/

---

**Project Status**: Active Development  
**Last Updated**: August 19, 2026  
**Contact**: Tajwarbot (GitHub)

For technical questions, refer to inline code comments and the Proposal PDF for architectural context.
