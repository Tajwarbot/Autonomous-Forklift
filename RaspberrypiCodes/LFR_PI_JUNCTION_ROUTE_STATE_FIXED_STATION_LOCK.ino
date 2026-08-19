#include <EEPROM.h>
#include <Servo.h>

// ================== SERIAL PORTS ==================
// Serial  = USB to PC, debug output only (Serial Monitor @ 115200)
// Serial1 = Raspberry Pi link  @ 9600
//           Mega RX1 = pin 19  <-- Pi GPIO14 / TXD (header pin 8)
//           Mega TX1 = pin 18  --> divider --> Pi GPIO15 / RXD (header pin 10)
//           GND to GND is mandatory
#define DEBUG_SERIAL 1  // 1 = print every byte from the Pi, 0 = silent

// ================== MOTOR DRIVER PINS (2x BTS7960) ==================
#define left_RPWM 9    // left forward
#define left_LPWM 8    // left backward
#define right_RPWM 12  // right forward
#define right_LPWM 11  // right backward

// ================== BUTTON ==================
#define sw 3  // one diagonal leg -> D3, other -> GND (INPUT_PULLUP)

// ================== TIMING / TUNING ==================
#define node_delay 200
#define stop_timer 100
#define u_turn_delay 350
#define u_turn_pause 5000  // ms to sit still on white before rotating
#define brake_time 50
#define turn_brake 80
#define station_unlock_time 250  // ms of stable line required before another station can trigger

float spl = 8;
float spr = 8;

// ================== STATE VARIABLES ==================
char side = 's';
bool route_received = false;
char flag = 's';

bool box_loaded = false;
bool returning = false;

// Prevent the same physical pickup/drop station from being processed twice.
// It is locked immediately when a station action starts and unlocked only
// after the robot has been back on a normal black line for a short time.
bool station_locked = false;
uint32_t station_line_since = 0;

#define servo_pin 5
Servo forklift;
int pos;
char cross = 's';
uint32_t m1, m2;

// ================== SENSOR VARIABLES ==================
int s[6];
int mid[6], maximum[6], minimum[6];
int base[6] = { 1, 2, 4, 8, 16, 32 };
int sensor;
int sum = 0;

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);  // USB debug
  Serial1.begin(9600);   // Raspberry Pi

  pinMode(left_RPWM, OUTPUT);
  pinMode(left_LPWM, OUTPUT);
  pinMode(right_RPWM, OUTPUT);
  pinMode(right_LPWM, OUTPUT);
  pinMode(sw, INPUT_PULLUP);

  forklift.attach(servo_pin);

  // Start with fork fully DOWN
  forklift.write(180);
  delay(1000);

  for (int i = 0; i < 6; i++) {
    mid[i] = EEPROM.read(i) * 4;
    maximum[i] = EEPROM.read(i + 6) * 4;
    minimum[i] = EEPROM.read(i + 12) * 4;
    Serial.println(String(maximum[i]) + " " + String(mid[i]) + " " + String(minimum[i]));
  }

  motor(0, 0);

  Serial.println(F("ARDUINO READY - waiting for 'l' or 'r' on Serial1"));
  Serial1.println(F("ARDUINO READY"));  // so the Pi can confirm the link
}

// ================== MAIN LOOP ==================
// Press count:  1 = calibrate,  2 = run
void loop() {
  serial_read_pi();  // drain + echo bytes even before the run starts
  int r = button_read();
  if (r == 1) cal();
  else if (r == 2) line_follow();
}

// ================== BUTTON (press counter) ==================
int button_read() {
  int cl = 0;
p:
  int t = 0;
  if (digitalRead(sw) == 0) {
    while (digitalRead(sw) == 0) {
      serial_read_pi();
      delay(1);
      t++;
    }
    if (t > 15) {
      t = 0;
      cl++;
      while (digitalRead(sw) == HIGH) {
        serial_read_pi();
        delay(1);
        t++;
        if (t > 1000) return cl;
      }
      goto p;
    }
  }
  return cl;
}

// ================== RASPBERRY PI SERIAL COMMAND ==================
void serial_read_pi() {
  while (Serial1.available() > 0) {
    char command = Serial1.read();

#if DEBUG_SERIAL
    Serial.print(F("RX '"));
    Serial.print(command);       // printable character
    Serial.print(F("' dec="));
    Serial.print((int)command);  // 108='l' 114='r' 76='L' 82='R' 13=CR 10=LF
    Serial.print(F(" left="));
    Serial.println(Serial1.available());
#endif

    if (command == 'L') command = 'l';
    if (command == 'R') command = 'r';

    if ((command == 'l' || command == 'r') && returning == false) {
      side = command;
      route_received = true;

#if DEBUG_SERIAL
      Serial.print(F("  -> ACCEPT  side="));
      Serial.print(side);
      Serial.print(F("  route_received=1  returning="));
      Serial.println(returning);
#endif
      Serial1.print(F("ACK "));  // tell the Pi the command landed
      Serial1.println(side);
    }
#if DEBUG_SERIAL
    else {
      Serial.print(F("  -> IGNORE  (returning="));
      Serial.print(returning);
      Serial.println(F(")"));
    }
#endif
  }
}

void serial_delay(unsigned long t) {
  unsigned long start = millis();
  while (millis() - start < t) {
    serial_read_pi();
    delay(1);
  }
}

// ================== SENSOR READ (fusion) ==================
void reading() {
  serial_read_pi();

  sensor = 0;
  sum = 0;
  for (int i = 0; i < 6; i++) {
    s[i] = analogRead(i);
    (s[i] < mid[i]) ? s[i] = 0 : s[i] = 1;
    sensor += s[i] * base[i];
    sum += s[i];
  }
}

// ================== MOTOR (BTS7960) ==================
void motor(int a, int b) {
  // ---- left channel ----
  if (a >= 0) {
    if (a > 255) a = 255;
    analogWrite(left_RPWM, a);
    analogWrite(left_LPWM, 0);
  } else {
    a = -a;
    if (a > 255) a = 255;
    analogWrite(left_RPWM, 0);
    analogWrite(left_LPWM, a);
  }
  // ---- right channel ----
  if (b >= 0) {
    if (b > 255) b = 255;
    analogWrite(right_RPWM, b);
    analogWrite(right_LPWM, 0);
  } else {
    b = -b;
    if (b > 255) b = 255;
    analogWrite(right_RPWM, 0);
    analogWrite(right_LPWM, b);
  }
}

// ================== BRAKE ==================
void brake() {
  motor(-9 * spl, -9 * spr);
  serial_delay(brake_time);
  motor(0, 0);
  delay(60);
}

// ================== FORKLIFT SERVO CONTROL ==================
void lift_box() {
#if DEBUG_SERIAL
  Serial.println(F("ACTION: lift box"));
#endif
  forklift.write(0);
  delay(1000);
}

void drop_box() {
#if DEBUG_SERIAL
  Serial.println(F("ACTION: drop box"));
#endif
  forklift.write(180);
  delay(1000);
}

void reverse_before_return() {
  motor(-10 * spl, -10 * spr);
  delay(300);
  motor(0, 0);
  delay(200);
}

char get_junction_turn_direction() {
  if (route_received == false) return 's';
  if (returning == false) return side;
  if (side == 'l') return 'r';
  if (side == 'r') return 'l';
  return 's';
}

// ================== FIXED STATION U-TURN ==================
// QR l/r is ONLY for the route junction.
// Preserve the tested station behavior:
//   outbound pickup station -> LEFT U-turn
//   return/drop station      -> RIGHT U-turn
char get_station_turn_direction() {
  return returning ? 'r' : 'l';
}

char wait_for_junction_turn_direction() {
  char dir = get_junction_turn_direction();

#if DEBUG_SERIAL
  bool warned = false;
#endif

  while (dir != 'l' && dir != 'r') {
#if DEBUG_SERIAL
    if (!warned) {
      Serial.println(F("JUNCTION WAIT: no QR route received yet - motors stopped"));
      warned = true;
    }
#endif
    motor(0, 0);
    serial_read_pi();
    delay(1);
    dir = get_junction_turn_direction();
  }

#if DEBUG_SERIAL
  Serial.print(F("JUNCTION TURN dir="));
  Serial.print(dir);
  Serial.print(F("  (side="));
  Serial.print(side);
  Serial.print(F(" returning="));
  Serial.print(returning);
  Serial.println(F(")"));
#endif

  return dir;
}

// ================== LINE FOLLOW ==================
void line_follow() {
  while (1) {
a:
    reading();

    // ================= STATION RE-TRIGGER PROTECTION =================
    // After pickup/drop, do NOT allow another sum==0 reading to be treated
    // as a new station until the robot has genuinely left the white station
    // and followed a normal black line continuously for a short period.
    if (station_locked) {
      bool normal_line = (sum == 1 || sum == 2) &&
                         (s[1] + s[2] + s[3] + s[4] > 0);

      if (normal_line) {
        if (station_line_since == 0) station_line_since = millis();

        if (millis() - station_line_since >= station_unlock_time) {
          station_locked = false;
          station_line_since = 0;
#if DEBUG_SERIAL
          Serial.println(F("STATION LOCK RELEASED: normal line confirmed"));
#endif
        }
      } else {
        station_line_since = 0;
      }
    }
    // =================================================================

    if (sum == 0) {
      if (flag != 's') {
        brake();
        (flag == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        while (s[2] == 0 && s[3] == 0) reading();
        (flag == 'r') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        serial_delay(turn_brake);
        flag = 's';
        cross = 's';
        pos = 0;
      } else if (station_locked == false && pos > -3 && pos < 3) {
        m2 = millis();
        while (sum == 0) {
          reading();
          if (millis() - m2 > u_turn_delay) {
            // Lock this physical station BEFORE doing anything else.
            // Even if the sensors continue to see white after the U-turn,
            // the pickup/drop action cannot run a second time.
            station_locked = true;
            station_line_since = 0;

#if DEBUG_SERIAL
            Serial.println(F("STATION LOCKED: processing this station once"));
#endif

            brake();
            motor(0, 0);

            // ================= WHITE STATION ACTION =================
            if (box_loaded == false) {
              // PICKUP STATION.
              // If we have just returned from a drop station, the old QR route
              // must be forgotten HERE (not at the return junction), because
              // it was still needed to navigate back to the pickup station.
              if (returning == true) {
                returning = false;
                route_received = false;
                side = 's';
                cross = 's';
                flag = 's';

#if DEBUG_SERIAL
                Serial.println(F("NEW CYCLE: old QR route cleared; waiting for new l/r"));
#endif
              }

              lift_box();
              box_loaded = true;
            } else {
              // DROP STATION. Keep side/route_received unchanged because the
              // same route is required in reverse at the junction on the way home.
              drop_box();
              box_loaded = false;
              returning = true;
              reverse_before_return();
            }
            // ========================================================

            // Intentional pause: let the lifted/dropped box settle.
            serial_delay(u_turn_pause);

            // Station turn is fixed and does NOT depend on QR.
            char station_turn = get_station_turn_direction();

#if DEBUG_SERIAL
            Serial.print(F("STATION U-TURN dir="));
            Serial.print(station_turn);
            Serial.print(F("  QR="));
            Serial.print(side);
            Serial.print(F("  returning="));
            Serial.println(returning);
#endif

            (station_turn == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
            while (s[2] == 0 && s[3] == 0) reading();
            (station_turn == 'r') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
            serial_delay(turn_brake);
            pos = 0;
            break;
          }
        }
      }
      else if (station_locked) {
        // We are still in/near the same white station after its U-turn.
        // Do nothing here: keep the most recent motor command so the robot
        // can finish leaving/reacquiring the line, but NEVER process the
        // station action again.
        serial_read_pi();
      }
    }

    else if (sum == 1 || sum == 2) {  // straight line
      if (cross != 's') {
        brake();
        (cross == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        while (s[4] + s[3] + s[2] + s[1] > 0) reading();
        while (s[2] == 0 && s[3] == 0) reading();
        (cross == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        serial_delay(turn_brake);
        cross = 's';
        flag = 's';
        pos = 0;
      }
      if (sensor == 0b001100) {
        if (pos != 0) {
          (pos > 0) ? motor(-10 * spl, 10 * spr) : motor(10 * spl, -10 * spr);
          (pos > 0) ? delay(pos * 5) : delay(-pos * 5);
          pos = 0;
        }
        motor(10 * spl, 10 * spr);
      }
      // right side portion
      else if (sensor == 0b000100)
        motor(10 * spl, 9 * spr);
      else if (sensor == 0b000110) {
        if (pos < 1) pos = 1;
        motor(10 * spl, 6 * spr);
      } else if (sensor == 0b000010) {
        if (pos < 2) pos = 2;
        motor(10 * spl, 3 * spr);
      } else if (sensor == 0b000011) {
        if (pos < 3) pos = 3;
        motor(10 * spl, 0 * spr);
      } else if (sensor == 0b000001) {
        if (pos < 4) pos = 4;
        motor(10 * spl, -3 * spr);
      }
      // left side portion
      else if (sensor == 0b001000)
        motor(9 * spl, 10 * spr);
      else if (sensor == 0b011000) {
        if (pos > -1) pos = -1;
        motor(6 * spl, 10 * spr);
      } else if (sensor == 0b010000) {
        if (pos > -2) pos = -2;
        motor(3 * spl, 10 * spr);
      } else if (sensor == 0b110000) {
        if (pos > -3) pos = -3;
        motor(0 * spl, 10 * spr);
      } else if (sensor == 0b100000) {
        if (pos > -4) pos = -4;
        motor(-3 * spl, 10 * spr);
      }
    }

    else if (sum == 3 || sum == 4 || sum == 5) {
      if (s[5] == 1 && s[0] == 0 && s[2] + s[3] > 0) {
        flag = 'l';
        while (s[5] == 1 && s[0] == 0) reading();
        if (s[0] == 0) {
          serial_delay(node_delay);
          reading();
          if (sum != 0)
            cross = wait_for_junction_turn_direction();
        }
      }

      else if (s[0] == 1 && s[5] == 0 && s[2] + s[3] > 0) {
        flag = 'r';
        while (s[5] == 0 && s[0] == 1) reading();
        if (s[5] == 0) {
          serial_delay(node_delay);
          reading();
          if (sum != 0)
            cross = wait_for_junction_turn_direction();
        }
      }
      m1 = millis();
    }

    else if (sum == 6) {
      // Full-black junction. The old code stopped after stop_timer and then
      // waited for sum to change while the motors were OFF. On a wide black
      // junction that can never happen, so the robot can freeze permanently.
      char junction_dir = wait_for_junction_turn_direction();
      flag = junction_dir;

#if DEBUG_SERIAL
      Serial.print(F("FULL JUNCTION: moving through, dir="));
      Serial.println(junction_dir);
#endif

      // Keep moving forward until the outer sensors start leaving the solid
      // junction. The timeout prevents this loop from trapping the robot if
      // the junction is unusually wide or a sensor stays active.
      m2 = millis();
      while ((s[5] == 1 || s[0] == 1) && (millis() - m2 < 650)) {
        motor(7 * spl, 7 * spr);
        reading();
      }

      serial_delay(node_delay);
      reading();

      // Reuse the direction already chosen above. Do not ask for the route a
      // second time at the same junction.
      if (sum != 0) cross = junction_dir;
      m1 = millis();
    }
    if (millis() - m1 > 500) flag = 's';
  }
}

// ================== CALIBRATION ==================
void cal() {
  for (int i = 0; i < 6; i++) {
    maximum[i] = 0;
    minimum[i] = 1023;
  }
  motor(120, -120);
  for (int j = 0; j < 5000; j++) {
    for (int i = 0; i < 6; i++) {
      s[i] = analogRead(i);
      maximum[i] = max(maximum[i], s[i]);
      minimum[i] = min(minimum[i], s[i]);
    }
  }
  motor(0, 0);
  for (int i = 0; i < 6; i++) {
    mid[i] = minimum[i] + (maximum[i] - minimum[i]) * 0.4;
    EEPROM.write(i, mid[i] / 4);
    delay(10);
    EEPROM.write(i + 6, maximum[i] / 4);
    delay(10);
    EEPROM.write(i + 12, minimum[i] / 4);
    delay(10);
  }
}

// ================== DEBUG HELPERS ==================
void analog_reading() {
  while (1) {
    for (int i = 5; i >= 0; i--) {
      s[i] = analogRead(i);
      Serial.print(String(s[i]) + " ");
    }
    Serial.println();
  }
}

void digital_reading() {
  while (1) {
    reading();
    for (int i = 5; i >= 0; i--)
      Serial.print(String(s[i]) + " ");
    Serial.println(sensor);
  }
}