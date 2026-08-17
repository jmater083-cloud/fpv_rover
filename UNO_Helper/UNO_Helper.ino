/*
UNO Helper (Option A) - FPV Rover v2.5.0
Units: United States of America (Volts, Inches, Fahrenheit)
Contributing Authors:
  - James Mater
  - OpenAI Codex (model: GPT-5) - AI contributing author
System Overview:
  UNO receives drive/mode/speed commands from the ESP32-CAM over a software
  serial helper link, reports I2C sensor status back to the ESP32-CAM, drives
  the L298N motor controller, generates the Jazzy one-wire serial output,
  reads the HC-SR04 ultrasonic ring, and runs the Auto-mode obstacle-avoidance
  state machine with MPU6050 yaw/tilt support.

Library Credits (author, version, URL):
  - Arduino LLC / Arduino: Wire (bundled with Arduino AVR core)
    Version: bundled with the installed Arduino AVR core
    URL: https://github.com/arduino/ArduinoCore-avr
  - Arduino LLC / Arduino: SoftwareSerial (bundled with Arduino AVR core)
    Version: bundled with the installed Arduino AVR core
    URL: https://github.com/arduino/ArduinoCore-avr
  - Adafruit: Adafruit INA219
    Version: install in Arduino Library Manager; version printed there
    URL: https://github.com/adafruit/Adafruit_INA219
  - Adafruit: Adafruit MCP23X17
    Version: install in Arduino Library Manager; version printed there
    URL: https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library
  - Adafruit: Adafruit MPU6050
    Version: install in Arduino Library Manager; version printed there
    URL: https://github.com/adafruit/Adafruit_MPU6050
  - Adafruit: Adafruit Unified Sensor
    Version: install in Arduino Library Manager; version printed there
    URL: https://github.com/adafruit/Adafruit_Sensor

Upload settings (Elegoo UNO R3):
  Board: Arduino Uno
  Processor: ATmega328P
  Upload speed: 115200
  USB-serial: CH340 (VID 1A86 / PID 7523), typically COM7
  Note: Upload baud is unrelated to runtime UART speed.
  Note: COM port may change if the board is unplugged/reconnected.

Upload Checklist:
  1. Disconnect Jazzy or its level shifter from UNO D1 before uploading.
  2. Leave UNO D0 reserved and unwired during normal rover runtime use.
  3. Reconnect Jazzy to UNO D1 only after the helper sketch upload completes.

Boot Diagnostic:
  On every power-up/reset, the UNO prints a hardware self-test to the USB
  serial port at 115200 baud.  Open the serial monitor at 115200 BEFORE
  powering/resetting the UNO to capture it.  After the diagnostic, Serial
  switches to Jazzy mode (38400 8E1).

UART Protocol (ASCII lines):
  MODE:MAN | MODE:AUTO | MODE:TRAIL
  DRIVE:FWD | DRIVE:REV | DRIVE:LEFT | DRIVE:RIGHT | DRIVE:STOP
  SPEED:0..255
  STATUS
  WIFILED:CLIENT | WIFILED:AP | WIFILED:OFF

  UNO -> ESP32 reports:
  V12:x.x V | V5:x.x V
  INA12:OK | INA12:NOT
  INA5:OK | INA5:NOT
  MCP:OK | MCP:NOT
  I2C:0x40 0x44 0x27 0x68 ...
  DIST:F:xx,L:xx,R:xx   (cm, front/left/right)
  IMU:OK | IMU:NOT
  YAW:xxx.x
  AUTO:STATE

Pins (UNO):
  L298N ENA = D5 (PWM)
  L298N ENB = D6 (PWM)
  L298N IN1 = D2
  L298N IN2 = D3
  L298N IN3 = D7
  L298N IN4 = D4
  ESP32 helper RX = D10
  ESP32 helper TX = D11
  WiFi status LED = MCP23017 GPA0 (external blue LED, 330 ohm resistor to GND)
  Jazzy TX = D1 (hardware UART TX, 38400 8E1)
  Jazzy RX mate reserved = D0 (kept unused to preserve the UART pair)
  Motor mapping: OUT1/OUT2 = Left track, OUT3/OUT4 = Right track

  HC-SR04 ultrasonic ring (shared TRIG on D9):
    D9  = TRIG (shared, all sensors fire together)
    A0  = ECHO center-front (0 deg)
    A1  = ECHO left-front (45 deg)
    A2  = ECHO right-front (45 deg)
    A3  = ECHO left (90 deg)   [Phase 2]
    D13 = ECHO right (90 deg)  [Phase 2]

  HW-201 IR drop sensors (on MCP23017 #1):
    GPA1 = front, between LF-HC and CF-HC (down)
    GPA2 = front, between CF-HC and RF-HC (down)
    GPA3 = left side (down)
    GPA4 = right side (down)

  DHT11 = D12 (Phase 2)
  HW-484 mic = A6 (Phase 2)

I2C (UNO): SDA=A4, SCL=A5 (level-shifted to 3.3V)
  INA219 12V @ 0x40
  INA219 5V  @ 0x44
  MCP23017 @ 0x27
  MPU6050  @ 0x68 (IMU, yaw + tilt safety)

MCP23017 Pin Plan:
  GPA0: WiFi status LED
  GPA1..GPA4: HW-201 IR drop sensors
  GPA5..GPA7: HW-201 IR (more, Phase 2)
  GPB0..GPB2: DS1302 RTC (Phase 2)
  GPB3..GPB7: Spare / KY-022 (Phase 2)

ASCII Wiring Diagram:

  ESP32-CAM GPIO13 (TX) ----------------------> UNO D10 (helper RX)
  ESP32-CAM GPIO14 (RX) <--- level shifter --- UNO D11 (helper TX)
  MCP23017 GPA0 --- blue LED --- 330 ohm resistor ---> GND
  UNO D1 (Jazzy TX, 38400 8E1) --- level shifter ---> D51157 white data line
  UNO D0 (Jazzy RX mate) reserved and left unused
  ESP32-CAM GND -------------------------------------- UNO GND

  HC-SR04 ring:
    D9 (TRIG) ---> all 5 HC-SR04 TRIG pins (shared)
    A0 <--- CF ECHO
    A1 <--- LF ECHO
    A2 <--- RF ECHO
    A3 <--- L ECHO (Phase 2)
    D13 <--- R ECHO (Phase 2)

  MPU6050: VCC=3.3V, GND, SDA=A4, SCL=A5 (via level shifter)

Wiring table last updated: 2026-04-05
UART pinning (TTL UART, level shifted):
  ESP32-CAM GPIO13 (TX) -> UNO D10 (SoftwareSerial RX)
  UNO D11 (SoftwareSerial TX) -> level shifter -> ESP32-CAM GPIO14 (RX)
  GND is common between boards
Helper UART speed:
  38400 baud (ESP32-CAM UART1 to UNO SoftwareSerial)
Jazzy UART speed:
  38400 baud, 8E1, generated by UNO hardware UART on D1
Upload note:
  Keep Jazzy disconnected from UNO D1 while uploading the UNO sketch if the
  controller or level shifter interferes with the bootloader.
*/

#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_INA219.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Motor pins
const uint8_t ENA_PIN = 5;
const uint8_t ENB_PIN = 6;
const uint8_t IN1_PIN = 2;
const uint8_t IN2_PIN = 3;
const uint8_t IN3_PIN = 7;
const uint8_t IN4_PIN = 4;
const uint8_t WIFI_LED_PIN = 0;  // MCP23017 GPA0

// ESP32 helper link on spare UNO pins
const uint8_t HELPER_RX_PIN = 10;
const uint8_t HELPER_TX_PIN = 11;
const unsigned long HELPER_BAUD = 38400;
SoftwareSerial helperSerial(HELPER_RX_PIN, HELPER_TX_PIN);

// Jazzy output on UNO hardware UART TX
const unsigned long JAZZY_BAUD = 38400;
const uint8_t JAZZY_SPEED_RANGE = 14;
const unsigned long JAZZY_PACKET_INTERVAL_MS = 1000UL / 60UL;
const unsigned long JAZZY_WAKE_MS = 300;
const unsigned long JAZZY_IDLE_WAKE_MS = 5UL * 60UL * 1000UL;

// HC-SR04 ultrasonic ring
const uint8_t HC_TRIG_PIN = 9;          // shared trigger
const uint8_t HC_ECHO_CF = A0;          // center-front
const uint8_t HC_ECHO_LF = A1;          // left-front
const uint8_t HC_ECHO_RF = A2;          // right-front
const uint8_t HC_ECHO_L = A3;           // left (Phase 2)
const uint8_t HC_ECHO_R = 13;           // right (Phase 2)
const uint8_t HC_NUM_SENSORS = 3;       // Phase 1: CF, LF, RF
const unsigned long HC_MEASURE_INTERVAL_MS = 50;  // ~20 Hz per sensor (round-robin)
const unsigned int HC_MAX_CM = 400;     // max reliable range
const unsigned int HC_OBSTACLE_CM = 40; // auto-mode obstacle threshold
const unsigned int HC_TURN_CLEAR_CM = 60; // min clearance to turn toward

// HW-201 IR drop sensors (MCP23017 #1)
const uint8_t IR_DROP_PINS[4] = {1, 2, 3, 4};  // GPA1..GPA4

// Auto-mode state machine
enum AutoState { AUTO_FORWARD, AUTO_TURN_LEFT, AUTO_TURN_RIGHT, AUTO_REVERSE, AUTO_STOP };
AutoState auto_state = AUTO_STOP;
unsigned long auto_state_ms = 0;
const unsigned long AUTO_TURN_TIMEOUT_MS = 3000;
const unsigned long AUTO_REVERSE_MS = 600;
const float AUTO_TURN_DEG = 90.0f;
const unsigned long AUTO_MANUAL_OVERRIDE_MS = 500;  // joystick override window

// I2C devices
Adafruit_INA219 ina12(0x40);
Adafruit_INA219 ina5(0x44);
Adafruit_MCP23X17 mcp;
Adafruit_MPU6050 mpu;

bool ina12_ok = false;
bool ina5_ok = false;
bool mcp_ok = false;
bool mpu_ok = false;

uint8_t speed_limit = 200;
String mode = "MAN";
unsigned long last_status_ms = 0;
const unsigned long STATUS_INTERVAL_MS = 2000;
unsigned long last_drive_ms = 0;
const unsigned long DRIVE_TIMEOUT_MS = 300;
String last_drive_cmd = "STOP";
unsigned long last_jazzy_packet_ms = 0;
unsigned long jazzy_last_move_ms = 0;
int8_t jazzy_fore_aft = 0;
int8_t jazzy_left_right = 0;
uint8_t wifi_led_mode = 0;
bool wifi_led_on = false;
unsigned long last_wifi_led_toggle_ms = 0;

// Motor direction inversion (set true to flip direction)
const bool invert_motor_a = false;
const bool invert_motor_b = false;

// HC-SR04 state
unsigned int hc_dist[HC_NUM_SENSORS] = {0, 0, 0};
uint8_t hc_next_sensor = 0;
unsigned long last_hc_measure_ms = 0;
unsigned long last_dist_report_ms = 0;
const unsigned long DIST_REPORT_INTERVAL_MS = 500;

// MPU6050 state
float yaw_deg = 0.0f;
unsigned long last_mpu_ms = 0;
const unsigned long MPU_INTERVAL_MS = 20;  // 50 Hz
const float TILT_SAFE_DEG = 45.0f;
bool tilted = false;

void helperPrintln(const String &line) {
  helperSerial.println(line);
}

void sendStatusSnapshot() {
  scanI2C();
  helperPrintln(String("INA12:") + (ina12_ok ? "OK" : "NOT"));
  helperPrintln(String("INA5:") + (ina5_ok ? "OK" : "NOT"));
  helperPrintln(String("MCP:") + (mcp_ok ? "OK" : "NOT"));
  helperPrintln(String("IMU:") + (mpu_ok ? "OK" : "NOT"));
  if (!mcp_ok) {
    helperPrintln("WiFi LED: MCP missing, indicator disabled");
  }
}

void setWifiLedMode(uint8_t mode) {
  wifi_led_mode = mode;
  if (wifi_led_mode == 1) {
    wifi_led_on = true;
    if (mcp_ok) mcp.digitalWrite(WIFI_LED_PIN, HIGH);
  } else if (wifi_led_mode == 2) {
    wifi_led_on = true;
    last_wifi_led_toggle_ms = millis();
    if (mcp_ok) mcp.digitalWrite(WIFI_LED_PIN, HIGH);
  } else {
    wifi_led_on = false;
    if (mcp_ok) mcp.digitalWrite(WIFI_LED_PIN, LOW);
  }
}

void scanI2C() {
  helperSerial.print("I2C:");
  bool any = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      if (any) helperSerial.print(' ');
      helperSerial.print("0x");
      if (addr < 16) helperSerial.print('0');
      helperSerial.print(addr, HEX);
      // add address to array to show as a table on web ui
      any = true;
    }
  }
  if (!any) helperSerial.print("None");
  helperSerial.println();
}

void setJazzyDrive(const String &cmd) {
  int8_t f = 0;
  int8_t l = 0;
  if (cmd == "FWD") f = 100;
  else if (cmd == "REV") f = -100;
  else if (cmd == "LEFT") l = -100;
  else if (cmd == "RIGHT") l = 100;
  jazzy_fore_aft = f;
  jazzy_left_right = l;
  if (f != 0 || l != 0) {
    jazzy_last_move_ms = millis();
  }
}

void sendJazzyBreak() {
  Serial.end();
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);
  delay(JAZZY_WAKE_MS);
  Serial.begin(JAZZY_BAUD, SERIAL_8E1);
  delay(30);
}

void sendJazzyPacket(uint8_t speed, int8_t fore_aft, int8_t left_right) {
  uint8_t checksum = 0xFF - (uint8_t)(0x4A + 0x01 + speed +
                                      (uint8_t)fore_aft + (uint8_t)left_right);
  Serial.write(0x4A);
  Serial.write(0x01);
  Serial.write(speed);
  Serial.write((uint8_t)fore_aft);
  Serial.write((uint8_t)left_right);
  Serial.write(checksum);
}

void setMotor(int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed == 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  } else if (speed > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  }
}

void driveCmd(const String &cmd) {
  String eff = cmd;
  last_drive_cmd = eff;
  setJazzyDrive(eff);
  int spd = speed_limit;
  if (eff == "STOP") {
    analogWrite(ENA_PIN, 0);
    analogWrite(ENB_PIN, 0);
    setMotor(IN1_PIN, IN2_PIN, 0);
    setMotor(IN3_PIN, IN4_PIN, 0);
    return;
  }
  analogWrite(ENA_PIN, speed_limit);
  analogWrite(ENB_PIN, speed_limit);
  if (eff == "FWD") {
    setMotor(IN1_PIN, IN2_PIN, invert_motor_a ? -spd : spd);
    setMotor(IN3_PIN, IN4_PIN, invert_motor_b ? -spd : spd);
  } else if (eff == "REV") {
    setMotor(IN1_PIN, IN2_PIN, invert_motor_a ? spd : -spd);
    setMotor(IN3_PIN, IN4_PIN, invert_motor_b ? spd : -spd);
  } else if (eff == "LEFT") {
    int a = invert_motor_a ? spd : -spd;
    int b = invert_motor_b ? -spd : spd;
    setMotor(IN1_PIN, IN2_PIN, a);
    setMotor(IN3_PIN, IN4_PIN, b);
  } else if (eff == "RIGHT") {
    int a = invert_motor_a ? -spd : spd;
    int b = invert_motor_b ? spd : -spd;
    setMotor(IN1_PIN, IN2_PIN, a);
    setMotor(IN3_PIN, IN4_PIN, b);
  } else {
    analogWrite(ENA_PIN, 0);
    analogWrite(ENB_PIN, 0);
    setMotor(IN1_PIN, IN2_PIN, 0);
    setMotor(IN3_PIN, IN4_PIN, 0);
  }
}

// ===========================
// HC-SR04 driver (round-robin, non-blocking)
// ===========================
// Measures one sensor per call, rotating through the ring.
// Shared TRIG fires all sensors; we read only the selected ECHO.
unsigned int hcReadCm(uint8_t echoPin) {
  // Fire the shared trigger
  digitalWrite(HC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(HC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(HC_TRIG_PIN, LOW);

  // Measure the selected echo
  long duration = pulseIn(echoPin, HIGH, 30000);  // 30ms timeout (~5m max)
  if (duration == 0) return HC_MAX_CM;  // no echo = clear
  unsigned int cm = duration / 58;      // cm = us / 58
  if (cm > HC_MAX_CM) cm = HC_MAX_CM;
  return cm;
}

void hcUpdate() {
  if (millis() - last_hc_measure_ms < HC_MEASURE_INTERVAL_MS) return;
  last_hc_measure_ms = millis();

  const uint8_t echoPins[HC_NUM_SENSORS] = {HC_ECHO_CF, HC_ECHO_LF, HC_ECHO_RF};
  hc_dist[hc_next_sensor] = hcReadCm(echoPins[hc_next_sensor]);
  hc_next_sensor = (hc_next_sensor + 1) % HC_NUM_SENSORS;
}

void reportDistances() {
  if (millis() - last_dist_report_ms < DIST_REPORT_INTERVAL_MS) return;
  last_dist_report_ms = millis();
  // Order: F (center-front), L (left-front), R (right-front)
  helperPrintln(String("DIST:F:") + hc_dist[0] + ",L:" + hc_dist[1] + ",R:" + hc_dist[2]);
}

// ===========================
// MPU6050 (yaw + tilt safety)
// ===========================
void mpuUpdate() {
  if (!mpu_ok) return;
  if (millis() - last_mpu_ms < MPU_INTERVAL_MS) return;
  unsigned long now = millis();
  float dt = (now - last_mpu_ms) / 1000.0f;
  last_mpu_ms = now;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Integrate gyro Z for yaw (drifts over time, fine for short turns)
  yaw_deg += g.gyro.z * dt * (180.0f / PI);

  // Tilt safety from accelerometer
  float pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y +
                                              a.acceleration.z * a.acceleration.z)) * (180.0f / PI);
  float roll = atan2(a.acceleration.y, a.acceleration.z) * (180.0f / PI);
  tilted = (fabs(pitch) > TILT_SAFE_DEG || fabs(roll) > TILT_SAFE_DEG);
}

void resetYaw() {
  yaw_deg = 0.0f;
}

// ===========================
// Auto-mode state machine
// ===========================
void autoUpdate() {
  if (mode != "AUTO") return;

  // Tilt safety: stop if tipped
  if (tilted) {
    if (auto_state != AUTO_STOP) {
      auto_state = AUTO_STOP;
      auto_state_ms = millis();
      driveCmd("STOP");
      helperPrintln("AUTO:STOP (tilt)");
    }
    return;
  }

  unsigned int front = hc_dist[0];  // center-front
  unsigned int left = hc_dist[1];   // left-front
  unsigned int right = hc_dist[2];  // right-front

  switch (auto_state) {
    case AUTO_FORWARD:
      if (front < HC_OBSTACLE_CM) {
        // Obstacle ahead: stop, then turn toward the open side
        driveCmd("STOP");
        auto_state_ms = millis();
        if (left >= right) {
          auto_state = AUTO_TURN_LEFT;
          helperPrintln("AUTO:TURN_LEFT");
        } else {
          auto_state = AUTO_TURN_RIGHT;
          helperPrintln("AUTO:TURN_RIGHT");
        }
      } else {
        driveCmd("FWD");
      }
      break;

    case AUTO_TURN_LEFT:
      if (millis() - auto_state_ms > AUTO_TURN_TIMEOUT_MS) {
        auto_state = AUTO_FORWARD;
        auto_state_ms = millis();
        driveCmd("FWD");
        helperPrintln("AUTO:FORWARD");
      } else {
        driveCmd("LEFT");
      }
      break;

    case AUTO_TURN_RIGHT:
      if (millis() - auto_state_ms > AUTO_TURN_TIMEOUT_MS) {
        auto_state = AUTO_FORWARD;
        auto_state_ms = millis();
        driveCmd("FWD");
        helperPrintln("AUTO:FORWARD");
      } else {
        driveCmd("RIGHT");
      }
      break;

    case AUTO_REVERSE:
      if (millis() - auto_state_ms > AUTO_REVERSE_MS) {
        auto_state = AUTO_FORWARD;
        auto_state_ms = millis();
        driveCmd("FWD");
        helperPrintln("AUTO:FORWARD");
      } else {
        driveCmd("REV");
      }
      break;

    case AUTO_STOP:
    default:
      // Stay stopped until a manual override or mode change
      break;
  }
}

// Manual override in auto mode: joystick input takes priority briefly
unsigned long last_manual_override_ms = 0;
bool manual_override_active = false;

void handleDriveCommand(const String &cmd) {
  if (mode == "AUTO" && cmd != "STOP") {
    // Joystick override in auto mode
    manual_override_active = true;
    last_manual_override_ms = millis();
    driveCmd(cmd);
  } else if (mode == "AUTO" && cmd == "STOP") {
    // Joystick released: resume auto after a short window
    if (manual_override_active && (millis() - last_manual_override_ms) < AUTO_MANUAL_OVERRIDE_MS) {
      // Still within override window, keep stopped
      driveCmd("STOP");
    } else {
      manual_override_active = false;
      auto_state = AUTO_FORWARD;
      auto_state_ms = millis();
      driveCmd("FWD");
      helperPrintln("AUTO:FORWARD (resume)");
    }
  } else {
    driveCmd(cmd);
  }
}

// ===========================
// Boot diagnostic (runs once on every power-up / reset)
// ===========================
// Prints a hardware self-test to the USB serial port at 115200 baud.
// Open the serial monitor at 115200 BEFORE powering/resetting the UNO.
// After the diagnostic completes, Serial switches to Jazzy mode (38400 8E1).
void runBootDiagnostic() {
  Serial.println(F("========================================"));
  Serial.println(F(" FPV ROVER - BOOT DIAGNOSTIC"));
  Serial.println(F("========================================"));

  // --- I2C device scan (every possible address 0x01-0x7E) ---
  Serial.println(F("[I2C] Scanning all addresses (0x01-0x7E)..."));
  uint8_t foundAddrs[32];
  uint8_t foundCount = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (foundCount < 32) foundAddrs[foundCount++] = addr;
    }
  }
  if (foundCount == 0) {
    Serial.println(F("[I2C] NONE FOUND - check A4/A5 wiring and power"));
  } else {
    for (uint8_t i = 0; i < foundCount; i++) {
      uint8_t addr = foundAddrs[i];
      Serial.print(F("[I2C] 0x"));
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      Serial.print(F(" -> "));
      // Identify likely device by address range
      if (addr >= 0x40 && addr <= 0x47) {
        Serial.println(F("INA219 (A0/A1 selectable)"));
      } else if (addr >= 0x48 && addr <= 0x4B) {
        Serial.println(F("INA219 or ADS1115 (A0/A1 selectable)"));
      } else if (addr >= 0x4C && addr <= 0x4F) {
        Serial.println(F("INA219 (A0/A1 selectable)"));
      } else if (addr >= 0x20 && addr <= 0x27) {
        Serial.println(F("MCP23017 (A0/A1/A2 selectable)"));
      } else if (addr == 0x68) {
        Serial.println(F("MPU6050 (AD0=GND)"));
      } else if (addr == 0x69) {
        Serial.println(F("MPU6050 (AD0=VCC)"));
      } else if (addr == 0x3C || addr == 0x3D) {
        Serial.println(F("OLED display (0x3C/0x3D)"));
      } else if (addr == 0x76 || addr == 0x77) {
        Serial.println(F("BMP280/BME280 (0x76/0x77)"));
      } else if (addr >= 0x50 && addr <= 0x53) {
        Serial.println(F("EEPROM (24Cxx)"));
      } else {
        Serial.println(F("Unknown device"));
      }
    }
    Serial.println(F("[I2C] Expected: INA219@0x40, INA219@0x44, MCP23017@0x27, MPU6050@0x68"));
    Serial.println(F("[I2C] If a device shows at a different address, update the begin() calls in the sketch."));
  }

  // --- INA219 voltages ---
  if (ina12_ok) {
    Serial.print(F("[VOLT] V12: "));
    Serial.print(ina12.getBusVoltage_V(), 2);
    Serial.println(F(" V"));
  } else {
    Serial.println(F("[VOLT] V12: NOT DETECTED (0x40)"));
  }
  if (ina5_ok) {
    Serial.print(F("[VOLT] V5: "));
    Serial.print(ina5.getBusVoltage_V(), 2);
    Serial.println(F(" V"));
  } else {
    Serial.println(F("[VOLT] V5: NOT DETECTED (0x44)"));
  }

  // --- MCP23017 + HW-201 IR sensors ---
  if (mcp_ok) {
    const __FlashStringHelper* irNames[4] = {
      F("GPA1 (front-left)"), F("GPA2 (front-right)"),
      F("GPA3 (left)"), F("GPA4 (right)")
    };
    for (int i = 0; i < 4; i++) {
      bool state = mcp.digitalRead(IR_DROP_PINS[i]);
      Serial.print(F("[IR] "));
      Serial.print(irNames[i]);
      Serial.print(F(": "));
      Serial.println(state ? F("HIGH (clear)") : F("LOW (triggered!)"));
    }
  } else {
    Serial.println(F("[IR] MCP23017 NOT DETECTED - cannot read HW-201 sensors"));
  }

  // --- HC-SR04 ultrasonic ring (all 5) ---
  const uint8_t echoPins[5] = {HC_ECHO_CF, HC_ECHO_LF, HC_ECHO_RF, HC_ECHO_L, HC_ECHO_R};
  const __FlashStringHelper* echoNames[5] = {
    F("CF (A0)"), F("LF (A1)"), F("RF (A2)"), F("L (A3)"), F("R (D13)")
  };
  for (int i = 0; i < 5; i++) {
    unsigned int cm = hcReadCm(echoPins[i]);
    Serial.print(F("[SONIC] "));
    Serial.print(echoNames[i]);
    Serial.print(F(": "));
    Serial.print(cm);
    if (cm >= HC_MAX_CM) {
      Serial.println(F(" cm (no echo - check wiring or nothing in range)"));
    } else {
      Serial.println(F(" cm"));
    }
  }

  // --- Motor driver pins (should all be LOW at boot) ---
  const uint8_t motorPins[6] = {ENA_PIN, ENB_PIN, IN1_PIN, IN2_PIN, IN3_PIN, IN4_PIN};
  const __FlashStringHelper* motorNames[6] = {
    F("ENA (D5)"), F("ENB (D6)"), F("IN1 (D2)"), F("IN2 (D3)"), F("IN3 (D7)"), F("IN4 (D4)")
  };
  for (int i = 0; i < 6; i++) {
    Serial.print(F("[MOTOR] "));
    Serial.print(motorNames[i]);
    Serial.print(F(": "));
    Serial.println(digitalRead(motorPins[i]) ? F("HIGH") : F("LOW"));
  }

  // --- MPU6050 ---
  Serial.print(F("[IMU] MPU6050: "));
  Serial.println(mpu_ok ? F("OK") : F("NOT DETECTED (0x68)"));

  Serial.println(F("----------------------------------------"));
  Serial.println(F(" NOTE: Cover/uncover each HW-201 IR sensor"));
  Serial.println(F("       to verify GPA1-GPA4 trigger states."));
  Serial.println(F("========================================"));
  Serial.println(F(" BOOT DIAGNOSTIC COMPLETE"));
  Serial.println(F("========================================"));
}

void setup() {
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);
  helperSerial.begin(HELPER_BAUD);
  // NOTE: Serial (USB) is intentionally NOT started yet.  The boot
  // diagnostic below uses 115200 baud; Serial then switches to Jazzy
  // mode (38400 8E1) after the diagnostic completes.

  pinMode(ENA_PIN, OUTPUT);
  pinMode(ENB_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);
  analogWrite(ENA_PIN, 0);
  analogWrite(ENB_PIN, 0);
  driveCmd("STOP");

  // HC-SR04 pins (all 5 echo pins, including Phase 2 A3/D13)
  pinMode(HC_TRIG_PIN, OUTPUT);
  digitalWrite(HC_TRIG_PIN, LOW);
  pinMode(HC_ECHO_CF, INPUT);
  pinMode(HC_ECHO_LF, INPUT);
  pinMode(HC_ECHO_RF, INPUT);
  pinMode(HC_ECHO_L, INPUT);
  pinMode(HC_ECHO_R, INPUT);

  Wire.begin();
  ina12_ok = ina12.begin();
  ina5_ok = ina5.begin();
  mcp_ok = mcp.begin_I2C(0x27);
  if (mcp_ok) {
    mcp.pinMode(WIFI_LED_PIN, OUTPUT);
    mcp.digitalWrite(WIFI_LED_PIN, LOW);
    // HW-201 IR drop sensors on GPA1..GPA4
    for (int i = 0; i < 4; i++) {
      mcp.pinMode(IR_DROP_PINS[i], INPUT_PULLUP);
    }
  }

  // MPU6050 (graceful fallback if not present yet)
  mpu_ok = mpu.begin();
  if (mpu_ok) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    last_mpu_ms = millis();
  }

  // --- Boot diagnostic (USB serial at 115200) ---
  Serial.begin(115200);
  runBootDiagnostic();
  delay(2000);  // give the user time to read the diagnostic
  Serial.end();

  // --- Switch Serial to Jazzy mode (38400 8E1) ---
  Serial.begin(JAZZY_BAUD, SERIAL_8E1);

  sendStatusSnapshot();
  setWifiLedMode(0);
  helperPrintln("UNO helper ready");
  sendJazzyBreak();
}

void loop() {
  if (wifi_led_mode == 2 && (millis() - last_wifi_led_toggle_ms) >= 800) {
    last_wifi_led_toggle_ms = millis();
    wifi_led_on = !wifi_led_on;
    if (mcp_ok) mcp.digitalWrite(WIFI_LED_PIN, wifi_led_on ? HIGH : LOW);
  }

  if (millis() - last_jazzy_packet_ms >= JAZZY_PACKET_INTERVAL_MS) {
    last_jazzy_packet_ms = millis();
    if ((jazzy_fore_aft != 0 || jazzy_left_right != 0) &&
        (millis() - jazzy_last_move_ms > JAZZY_IDLE_WAKE_MS)) {
      sendJazzyBreak();
      jazzy_last_move_ms = millis();
    }
    sendJazzyPacket(JAZZY_SPEED_RANGE, jazzy_fore_aft, jazzy_left_right);
  }

  if (last_drive_cmd == "STOP") {
    // Keep forcing stop to overcome any transient line glitches.
    digitalWrite(IN1_PIN, LOW);
    digitalWrite(IN2_PIN, LOW);
    digitalWrite(IN3_PIN, LOW);
    digitalWrite(IN4_PIN, LOW);
  }
  if (last_drive_ms != 0 && (millis() - last_drive_ms) > DRIVE_TIMEOUT_MS) {
    driveCmd("STOP");
    last_drive_ms = 0;
  }
  if (millis() - last_status_ms >= STATUS_INTERVAL_MS) {
    last_status_ms = millis();
    if (ina12_ok) {
      float v12 = ina12.getBusVoltage_V();
      helperSerial.print("V12:");
      helperSerial.print(v12, 2);
      helperSerial.println(" V");
    } else {
      helperPrintln("V12:--");
    }
    if (ina5_ok) {
      float v5 = ina5.getBusVoltage_V();
      helperSerial.print("V5:");
      helperSerial.print(v5, 2);
      helperSerial.println(" V");
    } else {
      helperPrintln("V5:--");
    }
  }

  // Sensor updates
  hcUpdate();
  reportDistances();
  mpuUpdate();

  // Auto-mode state machine (only when in AUTO mode)
  if (mode == "AUTO") {
    autoUpdate();
  }

  if (helperSerial.available()) {
    String line = helperSerial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("MODE:")) {
      mode = line.substring(5);
      helperPrintln(String("Mode set: ") + mode);
      if (mode == "AUTO") {
        auto_state = AUTO_FORWARD;
        auto_state_ms = millis();
        manual_override_active = false;
        resetYaw();
        helperPrintln("AUTO:FORWARD (start)");
      } else if (mode == "MAN") {
        driveCmd("STOP");
      }
    } else if (line == "STATUS") {
      sendStatusSnapshot();
    } else if (line.startsWith("WIFILED:")) {
      String wifiMode = line.substring(8);
      if (wifiMode == "CLIENT") setWifiLedMode(1);
      else if (wifiMode == "AP") setWifiLedMode(2);
      else setWifiLedMode(0);
      helperPrintln(String("WiFi LED: ") + wifiMode);
    } else if (line.startsWith("DRIVE:")) {
      String cmd = line.substring(6);
      helperPrintln(String("Drive: ") + cmd);
      handleDriveCommand(cmd);
      last_drive_ms = millis();
    } else if (line.startsWith("SPEED:")) {
      int v = line.substring(6).toInt();
      v = constrain(v, 0, 255);
      speed_limit = (uint8_t)v;
      helperPrintln(String("Speed: ") + speed_limit);
      analogWrite(ENA_PIN, speed_limit);
      analogWrite(ENB_PIN, speed_limit);
    }
  }
}