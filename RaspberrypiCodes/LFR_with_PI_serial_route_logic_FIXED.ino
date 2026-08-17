#include <EEPROM.h>
#include <Servo.h>

// ================== MOTOR DRIVER PINS (2x BTS7960) ==================
// Left driver drives the 2 left motors, Right driver drives the 2 right motors.
// Each BTS7960 channel: drive ONE pwm pin, hold the other LOW to pick direction.
#define left_RPWM 9    // left forward
#define left_LPWM 8    // left backward
#define right_RPWM 12  // right forward
#define right_LPWM 11  // right backward

// ================== BUTTON ==================
#define sw 3  // one diagonal leg -> D3, other -> GND (INPUT_PULLUP)

// ================== TIMING / TUNING ==================
#define node_delay 200 //120
#define stop_timer 100 //90
#define u_turn_delay 350//350
#define u_turn_pause 5000  // ms to sit still on white before rotating
#define brake_time 50     // 50 - when speed 22 // 40 - when speed 20
#define turn_brake 80     // 80 - when speed 22 // 60 - when speed 20

float spl = 8;  // 22 // 20 19 18
float spr = 8;  // 22 // 20 19 18

// ================== STATE VARIABLES ==================
// Route requested by Raspberry Pi. Start with NO route instead of defaulting to left.
char side = 's';
bool route_received = false;
char flag = 's';

bool box_loaded = false;
bool returning = false;

#define servo_pin 5
Servo forklift;
int pos;
char cross = 's';
uint32_t m1, m2;

// ================== SENSOR VARIABLES ==================
int s[6];
int mid[6], maximum[6], minimum[6];
int base[6] = { 1, 2, 4, 8, 16, 32 };  // binary -> decimal conversion
int sensor;                            // fused decimal value of the 6 sensors
int sum = 0;                           // count of sensors that see the line

// ================== SETUP ==================
void setup() {
  // UART link to Raspberry Pi.
  // This keeps the same Serial port used by your original code (pins 0/1).
  // On an Arduino Mega, Serial1 (RX1/TX1) is preferable if you want USB Serial free for debugging.
  Serial.begin(9600);

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
}

// ================== MAIN LOOP ==================
// Press count:  1 = calibrate,  2 = run (li 
void loop(){
  int r = button_read();
  if (r == 1) cal();
  else if (r == 2) line_follow();
}

// ================== BUTTON (press counter) ==================
int button_read() {
  int cl = 0;
p:
  int t = 0;
  if (digitalRead(sw) == 0) {       // pressed (LOW)
    while (digitalRead(sw) == 0) {  // wait while held
      delay(1);
      t++;
    }
    if (t > 15) {  // debounce: real press
      t = 0;
      cl++;
      while (digitalRead(sw) == HIGH) {  // wait for next press or timeout
        delay(1);
        t++;
        if (t > 1000) return cl;  // 1s gap -> done counting
      }
      goto p;  // another press came in
    }
  }
  return cl;
}


// ================== RASPBERRY PI SERIAL COMMAND ==================
// Raspberry Pi sends: 'l' or 'r'.
// side stores the OUTBOUND destination direction.
// While returning, the stored outbound direction is kept unchanged so that
// get_turn_direction() can automatically use the opposite turn.

void serial_read_pi()
{
  while (Serial.available() > 0)
  {
    char command = Serial.read();

    // Ignore CR/LF and any other serial characters.
    if (command == 'L') command = 'l';
    if (command == 'R') command = 'r';

    if ((command == 'l' || command == 'r') && returning == false)
    {
      side = command;
      route_received = true;
    }
  }
}


void serial_delay(unsigned long t)
{
  unsigned long start = millis();

  while(millis() - start < t)
  {
    serial_read_pi();
    delay(1);
  }
}

// ================== SENSOR READ (fusion) ==================
void reading() {
  serial_read_pi();

  sensor = 0;  // refresh
  sum = 0;
  for (int i = 0; i < 6; i++) {
    s[i] = analogRead(i);                   // A0..A5 = 0..5 on the Mega
    (s[i] < mid[i]) ? s[i] = 0 : s[i] = 1;  // CNY70: on-line reads lower//changed
    sensor += s[i] * base[i];               // fuse the 6 bits into one number
    sum += s[i];                            // total sensors on the line
  }
}

// ================== MOTOR (BTS7960) ==================
// a = left speed, b = right speed. Sign = direction. Range clamped to +/-255.
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

void lift_box()
{
  // Fork UP: servo rotates clockwise
  forklift.write(0);
  delay(1000);
}


void drop_box()
{
  // Fork DOWN: servo rotates counter-clockwise
  forklift.write(180);
  delay(1000);
}


void reverse_before_return()
{
  // move backward slightly before 180 degree rotation
  motor(-10 * spl, -10 * spr);
  delay(300);
  motor(0,0);
  delay(200);
}


char get_turn_direction()
{
  // Do not invent a direction before the Pi has supplied a valid route.
  if (route_received == false)
    return 's';

  // Outbound: obey Raspberry Pi directly.
  if (returning == false)
    return side;

  // Return: retrace the route by taking the opposite turn at the junction.
  if (side == 'l') return 'r';
  if (side == 'r') return 'l';

  return 's';
}


char wait_for_turn_direction()
{
  char dir = get_turn_direction();

  // If the robot reaches a route-dependent junction before receiving l/r,
  // stop instead of accidentally treating 's' as a right turn.
  while (dir != 'l' && dir != 'r')
  {
    motor(0, 0);
    serial_read_pi();
    delay(1);
    dir = get_turn_direction();
  }

  return dir;
}

// ================== LINE FOLLOW (same fusion logic) ==================
void line_follow() {
  while (1) {  //infinite loop
a:
    reading();
    if (sum == 0) {
      if (flag != 's') {
        brake();
        (flag == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        while (s[2] == 0 && s[3] == 0) reading();
        (flag == 'r') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        serial_delay(turn_brake);
        flag = 's';
        cross = 's';
        pos = 0;  //when you are done turning robot, make the flag to its normal state so that robot wont turn on its own when it finds no line without detecting 90degree logic
      } else if (pos > -3 && pos < 3) {
        m2 = millis();
        while (sum == 0) {
          reading();
          if (millis() - m2 > u_turn_delay) {
            brake();
            motor(0, 0);

            // ================= WHITE STATION ACTION =================
            // First long white area = pickup, second = drop

            if (box_loaded == false)
            {
              lift_box();
              box_loaded = true;
            }

            else
            {
              drop_box();
              box_loaded = false;
              returning = true;

              // move backward before U-turn
              reverse_before_return();
            }

            // ======================================================

            serial_delay(u_turn_pause);  // stand still on white before turning around

            char turn_dir = wait_for_turn_direction();
            (turn_dir == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
            while (s[2] == 0 && s[3] == 0) reading();
            (turn_dir == 'r') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
            serial_delay(turn_brake);
            pos = 0;
            break;
          }
        }
      }
    }
   /* if (sum == 0) {
      
      // =========================================================
      // SCENARIO 1: T-JUNCTION OR CORNER TURN (No 5000ms pause)
      // =========================================================
      if (flag != 's') { 
        
        // Push the robot fully into the white zone before turning
        motor(10 * spl, 10 * spr); 
        delay(150); // <-- Adjust this (100 to 300) to control how deep it goes into the white part
        
        brake();
        (flag == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        while (s[2] == 0 && s[3] == 0) reading();
        (flag == 'r') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
        serial_delay(turn_brake);
        flag = 's';
        cross = 's';
        pos = 0; 
      } 
      
      // =========================================================
      // SCENARIO 2: DEAD END / STRAIGHT LINE TO WHITE (Pause 5000ms)
      // =========================================================
      else if (pos > -3 && pos < 3) { 
        m2 = millis();
        
        // Force the robot to drive perfectly straight while waiting.
        motor(10 * spl, 10 * spr);
        
        while (sum == 0) {
          reading();
          if (millis() - m2 > u_turn_delay) { // Wait 400ms
            brake();
            motor(0, 0);
            serial_delay(u_turn_pause);  // Sit perfectly still for 5000ms
            
            // Execute the 180 U-Turn
            (get_turn_direction() == 'l') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
            
            // ---> THE FIX: Blind spin to ignore electrical noise/shadows <---
            delay(250); 
            
            while (s[2] == 0 && s[3] == 0) reading();
            (get_turn_direction() == 'r') ? motor(-6 * spl, 6 * spr) : motor(6 * spl, -6 * spr);
            serial_delay(turn_brake);
            pos = 0;
            break;
          }
        }
      }
    }*/
    
    
    
    
    
     else if (sum == 1 || sum == 2) {  //only for straight line
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
          pos = 0;  //this logic is for those whose bot is wabbling during high speed run. this logic will forcefully return bot to its midpoint. don't give more than 30ms delay here!
        }           //pos*5
        motor(10 * spl, 10 * spr);
      }
      //right side portion
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
      //left side portion
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
      //i_detection();
      if (s[5] == 1 && s[0] == 0 && s[2] + s[3] > 0) {
        flag = 'l';
        while (s[5] == 1 && s[0] == 0) reading();
        if (s[0] == 0) {
          serial_delay(node_delay);
          reading();
          if (sum != 0)
            cross = wait_for_turn_direction();
        }
      }

      else if (s[0] == 1 && s[5] == 0 && s[2] + s[3] > 0) {
        flag = 'r';
        while (s[5] == 0 && s[0] == 1) reading();
        if (s[5] == 0) {
          serial_delay(node_delay);
          reading();
          if (sum != 0)
            cross = wait_for_turn_direction();
        }
      }
      m1 = millis();
    }

    else if (sum == 6) {
      flag = wait_for_turn_direction();
      m2 = millis();
      while (s[5] == 1 || s[0] == 1) {
        reading();
        if (millis() - m2 > stop_timer) {
          motor(0, 0);
          while (sum == 6) reading();
          goto a;
        }
      }
      serial_delay(node_delay);
      reading();
      if (sum != 0) cross = wait_for_turn_direction();
      m1 = millis();
    }
    if (millis() - m1 > 500) flag = 's';
  }
}


// ================== CALIBRATION ==================
// No button, so call this manually from setup() once if you ever need to
// re-calibrate (place robot so sensors sweep across the line while it spins).
void cal() {
  for (int i = 0; i < 6; i++) {  // was i<8 -> out of bounds, fixed
    maximum[i] = 0;
    minimum[i] = 1023;
  }
  motor(120, -120);  // spin in place
  for (int j = 0; j < 5000; j++) {
    for (int i = 0; i < 6; i++) {
      s[i] = analogRead(i);
      maximum[i] = max(maximum[i], s[i]);
      minimum[i] = min(minimum[i], s[i]);
    }
  }
  motor(0, 0);
  for (int i = 0; i < 6; i++) {  // was i<7 -> out of bounds, fixed
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