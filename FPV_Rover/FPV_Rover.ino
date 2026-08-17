/*
================================================================================
FPV Rover v2.5.1
--------------------------------------------------------------------------------
Units: United States of America (Fahrenheit, Volts, etc.)
Documentation: This header documents wiring, libraries, and build notes.
Contributing Authors:
  - James Mater
  - OpenAI Codex (model: GPT-5) - AI contributing author
Base Source:
  - Espressif CameraWebServer example from Arduino-ESP32
System Overview:
  ESP32-CAM hosts the web UI + camera stream and sends drive/mode/speed commands
  to the UNO helper over uno UART. The UNO handles I2C sensors, motor control,
  autonomous sonar/cliff avoidance, GPS/home tracking, sound detection, and
  the Jazzy one-wire serial output.

Library Credits (author, version, URL):
  - Espressif Systems: Arduino-ESP32 core
    Version: printed at boot as "Arduino-ESP32 core vX.Y.Z"
    URL: https://github.com/espressif/arduino-esp32
  - Espressif Systems: esp_camera (bundled with Arduino-ESP32)
    Version: same as Arduino-ESP32 core
    URL: https://github.com/espressif/arduino-esp32/tree/master/libraries/esp32-camera
  - Espressif Systems: WiFi (bundled with Arduino-ESP32)
    Version: same as Arduino-ESP32 core
    URL: https://github.com/espressif/arduino-esp32

 Upload Checklist:
   1. Leave Jazzy disconnected from UNO D1 while uploading the UNO helper.
   2. Keep ESP32-CAM GPIO12 unused so ESP32 flashing remains reliable.
   3. After both uploads finish, reconnect Jazzy to UNO D1 and test the WiFi LED.

 ASCII Wiring Diagram (ESP32-CAM + UNO helper; 5V only on ESP32-CAM):

   5V PSU
     |
   ESP32-CAM 5V ---------------------- GND (common ground) ---- UNO GND
     |                                                    |
  GPIO13 (UART1 TX) -> UNO D10 (SoftwareSerial RX)         |
  GPIO14 (UART1 RX) <- UNO D11 (SoftwareSerial TX) -- level shift!
     |                                                    |
  GPIO4 (LED) -> Camera LED (onboard)
                                                           |
  UNO D1 (HW TX, 38400 8E1) -> level shifter -> D51157 white data line
  UNO D0 (HW RX) reserved with D1 as Jazzy UART mate; keep clear
  MCP23017 GPA0 -> Blue WiFi status LED -> 330 ohm resistor -> GND
  UNO D9 -> HC-SR04 TRIG (shared, all 5 sensors)
  UNO A0/A1/A2/A3/D13 <- HC-SR04 echo pins (CF, LF, RF, L, R)
  MCP23017 GPA1..GPA4 <- HW-201 IR drop sensor DO pins (FL, FR, LS, RS)

  NEO-6M GPS TX -> ESP32-CAM GPIO16 (optional)
  HW-484 sound OUT -> UNO A6

  UNO I2C (A4/A5) -> INA219/MCP23017
  Detailed wiring table: D:\projects\FPV_Rover\wiring_pin_map.ods
  Wiring table last updated: 2026-07-21
================================================================================
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HardwareSerial.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// Helper status (from UNO)
extern String comm_bat12;
extern String comm_bat5;
extern String comm_ina12;
extern String comm_ina5;
extern String comm_mcp;
extern String comm_i2c;
extern String comm_sonar;
extern String comm_cliff;
extern String comm_gps;
extern String comm_sound;

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "NID7417";
const char *password = "blackrabbits";
const char *ap_ssid = "ESP32-Rover";
const char *ap_password = "";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 12000;
const unsigned long HELPER_STATUS_REQUEST_INTERVAL_MS = 10000;

/*
================================================================================
PIN MAP (Selected)
--------------------------------------------------------------------------------
ESP32-CAM (AI Thinker) GPIOs
No I2C/SPI peripherals on ESP32-CAM in this build.
All I2C devices are handled by the UNO helper over its I2C bus.

Motor Driver (L298N)
  Controlled by UNO helper (pins listed in UNO_Helper.ino)
  Motor mapping: OUT1/OUT2 = Left track, OUT3/OUT4 = Right track

 Camera LED
  GPIO4

 WiFi status LED (external, driven by UNO helper through MCP23017)
   MCP23017 GPA0 drives a blue LED through a 330 ohm resistor to GND
   Solid ON = connected to client WiFi
   Slow blink = AP mode

 Autonomous sensors (handled by UNO helper):
   HC-SR04 sonar sensors:
     CF = center front, LF = left front, RF = right front, L = left, R = right
     UNO D9 drives a shared TRIG line that fires all 5 sensors simultaneously
     UNO A0/A1/A2/A3/D13 read CF/LF/RF/L/R echo lines
   HW-201 IR drop sensors:
     Downward-facing FL/FR/LS/RS modules feed MCP23017 GPA1..GPA4 digital inputs
     Any cliff input in AUTO mode stops the rover immediately
   NEO-6M GPS:
     GPS TX -> ESP32-CAM GPIO16 (optional). Power-up fix is saved as home.
     Return-home uses GPS course-over-ground only when moving; no compass is installed.
   HW-484 sound sensor:
     Analog output feeds UNO A6. One module detects sound but
     cannot locate direction; use 3 or 4 spaced modules for bearing later.

  Helper MCU (UNO) UART (for INA219/I2C helper and Jazzy proxy)
  ESP32-CAM UART1 TX = GPIO13  -> UNO D10 (SoftwareSerial RX)
  ESP32-CAM UART1 RX = GPIO14  <- UNO D11 (SoftwareSerial TX, level shift required)
  NOTE: GPIO13/14 are shared with SD card (future). If SD is enabled later,
        helper UART pins will need to move again.
UART pinning (uno UART, level shifted):
  ESP32-CAM GPIO13 (TX) -> UNO D10 (RX)
  UNO D11 (TX) -> level shifter -> ESP32-CAM GPIO14 (RX)
  GND is common between boards
 UART health:
   UART status card shows OK if data received in the last 5 seconds.
  UART speed:
   38400 baud (ESP32-CAM UART1 to uno SoftwareSerial)

 Internal temperature (ESP32):
   Uses temperatureRead() for a rough reading; displayed in Fahrenheit.
   This is not a calibrated ambient sensor. DHT11 will be added later for accuracy.

 D51157 (Jazzy) controller serial (generated by UNO helper)
   UNO D1 (TX) -> level shifter -> Jazzy controller serial input
   UNO D0 (RX) is reserved to keep the hardware UART pair together
   38400 baud, 8E1, continuous packets (from Jazzy sample in D:\projects\FPV_Rover\Jazzy)
   Use a level shifter (white data line idles ~3.6V)
   NOTE: Keep Jazzy disconnected from UNO D1 while uploading the UNO sketch if
         the controller or level shifter interferes with the bootloader.
 Drive enable:
   Output is gated by a safety toggle (default OFF).
 Drive routing:
   Commands are sent from ESP32-CAM to the UNO helper. The UNO then drives both
   the L298N and the D51157 serial output.

MCP23017 (on UNO I2C bus)
  Address: 0x27
  GPA0 use: WiFi status LED
  GPA1..GPA4 use: HW-201 IR drop sensors
  GPA5..GPA7 use: HW-201 IR (more, Phase 2)
  GPB0..GPB2 use: DS1302 RTC (Phase 2)
  GPB3..GPB7 use: Spare / KY-022 (Phase 2)
================================================================================
*/

void startCameraServer();
void setupLedFlash();
void initHelperUart();
void sendHelperCmd(const String& cmd);
void updateHelperWifiLed();
void requestHelperStatus();

static String helperLine;
String helper_last_line = "";
unsigned long helper_last_ms = 0;
static int helper_wifi_led_mode = -1;

static void handleHelperLine(const String &line) {
  int colon = line.indexOf(':');
  if (colon <= 0) return;
  String key = line.substring(0, colon);
  String val = line.substring(colon + 1);
  val.trim();
  if (key == "V12") comm_bat12 = val;
  else if (key == "V5") comm_bat5 = val;
  else if (key == "INA12") comm_ina12 = val;
  else if (key == "INA5") comm_ina5 = val;
  else if (key == "MCP") comm_mcp = val;
  else if (key == "I2C") comm_i2c = val;
  else if (key == "SONAR") comm_sonar = val;
  else if (key == "CLIFF") comm_cliff = val;
  else if (key == "GPS") comm_gps = val;
  else if (key == "SOUND") comm_sound = val;
}

void sendHelperCmd(const String& cmd) {
  Serial1.print(cmd);
  Serial1.print('\n');
}

void requestHelperStatus() {
  sendHelperCmd("STATUS");
}

void updateHelperWifiLed() {
  int mode = WiFi.isConnected() ? 1 : 2;
  if (mode != helper_wifi_led_mode) {
    helper_wifi_led_mode = mode;
    sendHelperCmd(String("WIFILED:") + (mode == 1 ? "CLIENT" : "AP"));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("UART helper link: 38400 baud");
#if defined(ESP_ARDUINO_VERSION_MAJOR)
  Serial.printf("Arduino-ESP32 core v%d.%d.%d\n",
                ESP_ARDUINO_VERSION_MAJOR,
                ESP_ARDUINO_VERSION_MINOR,
                ESP_ARDUINO_VERSION_PATCH);
#else
  Serial.println("Arduino-ESP32 core version: unknown");
#endif
  initHelperUart();
  delay(250);
  requestHelperStatus();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

  if (psramFound()) {
    Serial.printf("PSRAM: OK | Total: %u bytes | Free: %u bytes\n",
                  ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("PSRAM: FAIL");
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("WiFi connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected to ");
    Serial.println(ssid);
  } else {
    WiFi.disconnect(true);
    delay(200);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("AP started: ");
    Serial.println(ap_ssid);
  }

  updateHelperWifiLed();

  startCameraServer();

  IPAddress ip = WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP();
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(ip);
  Serial.println("' to connect");
}

void loop() {
  static unsigned long lastHelperStatusRequestMs = 0;
  static unsigned long lastWifiLedUpdateMs = 0;
  if (millis() - lastHelperStatusRequestMs >= HELPER_STATUS_REQUEST_INTERVAL_MS) {
    lastHelperStatusRequestMs = millis();
    requestHelperStatus();
  }
  if (millis() - lastWifiLedUpdateMs >= 1000) {
    lastWifiLedUpdateMs = millis();
    updateHelperWifiLed();
  }
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      helper_last_line = helperLine;
      helper_last_ms = millis();
      handleHelperLine(helperLine);
      helperLine = "";
    } else if (c != '\r') {
      if (helperLine.length() < 160) helperLine += c;
    }
  }
  delay(5);
}
