/*
 UNO Helper (Option A) - FPV Rover v2.5.1
 Units: United States of America (Volts, Inches, Fahrenheit)
 Contributing Authors:
  - James Mater
  - OpenAI Codex (model: GPT-5) - AI contributing author

 System Overview:
  UNO receives drive/mode/speed commands from the ESP32-CAM over a software
  serial helper link, reports I2C sensor status back to the ESP32-CAM, drives
  the L298N motor controller, generates the Jazzy one-wire serial output,
  reads the HC-SR04 ultrasonic ring, and runs the Auto-mode obstacle-avoidance
  state machine with MPU6050 yaw/tilt support and fail-safe watchdogs.
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
const uint8_t WIFI_LED_PIN = 0; // MCP23017 GPA0

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
const uint8_t HC_TRIG_PIN = 9; // shared trigger
const uint8_t HC_ECHO_CF = A0; // center-front
const uint8_t HC_ECHO_LF = A1; // left-front
const uint8_t HC_ECHO_RF = A2; // right-front
const uint8_t HC_ECHO_L  = A3; // left (Phase 2)
const uint8_t HC_ECHO_R  = 13; // right (Phase 2)
const uint8_t HC_NUM_SENSORS = 3; // Phase 1: CF, LF, RF
const unsigned long HC_MEASURE_INTERVAL_MS = 50; // ~20 Hz round-robin
const unsigned int HC_MAX_CM = 400;
const unsigned int HC_OBSTACLE_CM = 40;
const unsigned int HC_TURN_CLEAR_CM = 60;

// HW-201 IR drop sensors (MCP23017 #1)
const uint8_t IR_DROP_PINS[4] = {1, 2, 3, 4}; // GPA1..GPA4

// Auto-mode state machine
enum AutoState { AUTO_FORWARD, AUTO_TURN_LEFT, AUTO_TURN_RIGHT, AUTO_REVERSE, AUTO_STOP };
AutoState auto_state = AUTO_STOP;
unsigned long auto_state_ms = 0;
const unsigned long AUTO_TURN_TIMEOUT_MS = 3000;
const unsigned long AUTO_REVERSE_MS = 600;
const float AUTO_TURN_DEG = 90.0f;
const unsigned long AUTO_MANUAL_OVERRIDE_MS = 500;

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
const unsigned long DRIVE_TIMEOUT_MS = 400; // Watchdog failsafe timeout
String last_drive_cmd = "STOP";
unsigned long last_jazzy_packet_ms = 0;
unsigned long jazzy_last_move_ms = 0;
int8_t jazzy_fore_aft = 0;
int8_t jazzy_left_right = 0;
uint8_t wifi_led_mode = 0;
bool wifi_led_on = false;
unsigned long last_wifi_led_toggle_ms = 0;

// Motor direction inversion
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
const unsigned long MPU_INTERVAL_MS = 20; // 50 Hz
const float TILT_SAFE_DEG = 45.0f;
bool tilted = false;

// Override state
unsigned long last_manual_override_ms = 0;
bool manual_override_active = false;

// Helper Serial String buffer
String helperRxBuffer = "";

void helperPrintln(const String &line) {
  helperSerial.println(line);
}

void scanI2C() {
  helperSerial.print(F("I2C:"));
  bool any = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      if (any) helperSerial.print(' ');
      helperSerial.print(F("0x"));
      if (addr < 16) helperSerial.print('0');
      helperSerial.print(addr, HEX);
      any = true;
    }
  }
  if (!any) helperSerial.print(F("None"));
  helperSerial.println();
}

void sendStatusSnapshot() {
  scanI2C();
  helperPrintln(String(F("INA12:")) + (ina12_ok ? "OK" : "NOT"));
  helperPrintln(String(F("INA5:")) + (ina5_ok ? "OK" : "NOT"));
  helperPrintln(String(F("MCP:")) + (mcp_ok ? "OK" : "NOT"));
  helperPrintln(String(F("IMU:")) + (mpu_ok ? "OK" : "NOT"));
  if (ina12_ok) {
    helperPrintln(String(F("V12:")) + String(ina12.getBusVoltage_V(), 2) + " V");
  }
  if (ina5_ok) {
    helperPrintln(String(F("V5:")) + String(ina5.getBusVoltage_V(), 2) + " V");
  }
}

void setWifiLedMode(uint8_t m) {
  wifi_led_mode = m;
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

void sendJazzyPacket(uint8_t spd, int8_t fore_aft, int8_t left_right) {
  uint8_t checksum = 0xFF - (uint8_t)(0x4A + 0x01 + spd + (uint8_t)fore_aft + (uint8_t)left_right);
  Serial.write(0x4A);
  Serial.write(0x01);
  Serial.write(spd);
  Serial.write((uint8_t)fore_aft);
  Serial.write((uint8_t)left_right);
  Serial.write(checksum);
}

void setMotor(int in1, int in2, int spd) {
  spd = constrain(spd, -255, 255);
  if (spd == 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  } else if (spd > 0) {
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

unsigned int hcReadCm(uint8_t echoPin) {
  digitalWrite(HC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(HC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(HC_TRIG_PIN, LOW);

  long duration = pulseIn(echoPin, HIGH, 25000); // 25ms timeout (~4.3m max)
  if (duration == 0) return HC_MAX_CM;
  unsigned int cm = duration / 58;
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
  helperPrintln(String(F("DIST:F:")) + hc_dist[0] + F(",L:") + hc_dist[1] + F(",R:") + hc_dist[2]);
}

void mpuUpdate() {
  if (!mpu_ok) return;
  if (millis() - last_mpu_ms < MPU_INTERVAL_MS) return;
  unsigned long now = millis();
  float dt = (now - last_mpu_ms) / 1000.0f;
  last_mpu_ms = now;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  yaw_deg += g.gyro.z * dt * (180.0f / PI);

  float pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * (180.0f / PI);
  float roll = atan2(a.acceleration.y, a.acceleration.z) * (180.0f / PI);
  tilted = (fabs(pitch) > TILT_SAFE_DEG || fabs(roll) > TILT_SAFE_DEG);
}

void resetYaw() {
  yaw_deg = 0.0f;
}

void autoUpdate() {
  if (mode != "AUTO") return;

  if (tilted) {
    if (auto_state != AUTO_STOP) {
      auto_state = AUTO_STOP;
      auto_state_ms = millis();
      driveCmd("STOP");
      helperPrintln(F("AUTO:STOP (tilt)"));
    }
    return;
  }

  unsigned int front = hc_dist[0];
  unsigned int left  = hc_dist[1];
  unsigned int right = hc_dist[2];

  switch (auto_state) {
    case AUTO_FORWARD:
      if (front < HC_OBSTACLE_CM) {
        driveCmd("STOP");
        auto_state_ms = millis();
        if (left >= right) {
          auto_state = AUTO_TURN_LEFT;
          helperPrintln(F("AUTO:TURN_LEFT"));
        } else {
          auto_state = AUTO_TURN_RIGHT;
          helperPrintln(F("AUTO:TURN_RIGHT"));
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
        helperPrintln(F("AUTO:FORWARD"));
      } else {
        driveCmd("LEFT");
      }
      break;

    case AUTO_TURN_RIGHT:
      if (millis() - auto_state_ms > AUTO_TURN_TIMEOUT_MS) {
        auto_state = AUTO_FORWARD;
        auto_state_ms = millis();
        driveCmd("FWD");
        helperPrintln(F("AUTO:FORWARD"));
      } else {
        driveCmd("RIGHT");
      }
      break;

    case AUTO_REVERSE:
      if (millis() - auto_state_ms > AUTO_REVERSE_MS) {
        auto_state = AUTO_FORWARD;
        auto_state_ms = millis();
        driveCmd("FWD");
        helperPrintln(F("AUTO:FORWARD"));
      } else {
        driveCmd("REV");
      }
      break;

    case AUTO_STOP:
    default:
      break;
  }
}

void handleDriveCommand(const String &cmd) {
  last_drive_ms = millis();
  if (mode == "AUTO" && cmd != "STOP") {
    manual_override_active = true;
    last_manual_override_ms = millis();
    driveCmd(cmd);
  } else if (mode == "AUTO" && cmd == "STOP") {
    if (manual_override_active && (millis() - last_manual_override_ms) < AUTO_MANUAL_OVERRIDE_MS) {
      driveCmd("STOP");
    } else {
      manual_override_active = false;
      auto_state = AUTO_FORWARD;
      auto_state_ms = millis();
      driveCmd("FWD");
      helperPrintln(F("AUTO:FORWARD (resume)"));
    }
  } else {
    driveCmd(cmd);
  }
}

void parseHelperLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("DRIVE:")) {
    handleDriveCommand(line.substring(6));
  } else if (line.startsWith("SPEED:")) {
    speed_limit = constrain(line.substring(6).toInt(), 0, 255);
  } else if (line.startsWith("MODE:")) {
    mode = line.substring(5);
    if (mode == "AUTO") {
      auto_state = AUTO_FORWARD;
      auto_state_ms = millis();
    } else {
      driveCmd("STOP");
    }
  } else if (line.startsWith("WIFILED:")) {
    String st = line.substring(8);
    if (st == "CLIENT") setWifiLedMode(1);
    else if (st == "AP") setWifiLedMode(2);
    else setWifiLedMode(0);
  } else if (line == "STATUS") {
    sendStatusSnapshot();
  }
}

void setup() {
  Serial.begin(115200);
  helperSerial.begin(HELPER_BAUD);

  pinMode(ENA_PIN, OUTPUT);
  pinMode(ENB_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);
  pinMode(HC_TRIG_PIN, OUTPUT);

  driveCmd("STOP");

  Wire.begin();
  ina12_ok = ina12.begin();
  ina5_ok = ina5.begin();
  mcp_ok = mcp.begin_I2C(0x27);
  mpu_ok = mpu.begin(0x68);

  if (mcp_ok) {
    mcp.pinMode(WIFI_LED_PIN, OUTPUT);
    for (int i = 0; i < 4; i++) {
      mcp.pinMode(IR_DROP_PINS[i], INPUT_PULLUP);
    }
  }

  Serial.println(F("[BOOT] UNO Helper Initialized"));
  Serial.end();
  Serial.begin(JAZZY_BAUD, SERIAL_8E1);
  last_drive_ms = millis();
}

void loop() {
  // 1. Process commands from ESP32-CAM
  while (helperSerial.available()) {
    char c = (char)helperSerial.read();
    if (c == '\n') {
      parseHelperLine(helperRxBuffer);
      helperRxBuffer = "";
    } else if (c != '\r') {
      if (helperRxBuffer.length() < 64) {
        helperRxBuffer += c;
      }
    }
  }

  // 2. Watchdog: Auto-halt motors if manual drive packets cease
  if (mode == "MAN" && last_drive_cmd != "STOP") {
    if (millis() - last_drive_ms > DRIVE_TIMEOUT_MS) {
      driveCmd("STOP");
    }
  }

  // 3. Sensor & Auto updates
  hcUpdate();
  mpuUpdate();
  autoUpdate();
  reportDistances();

  // 4. WiFi LED blinking in AP mode
  if (wifi_led_mode == 2 && mcp_ok) {
    if (millis() - last_wifi_led_toggle_ms >= 500) {
      last_wifi_led_toggle_ms = millis();
      wifi_led_on = !wifi_led_on;
      mcp.digitalWrite(WIFI_LED_PIN, wifi_led_on ? HIGH : LOW);
    }
  }

  // 5. Periodic status telemetry
  if (millis() - last_status_ms >= STATUS_INTERVAL_MS) {
    last_status_ms = millis();
    if (ina12_ok) helperPrintln(String(F("V12:")) + String(ina12.getBusVoltage_V(), 2) + " V");
    if (ina5_ok) helperPrintln(String(F("V5:")) + String(ina5.getBusVoltage_V(), 2) + " V");
  }
}
