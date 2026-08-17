/*
================================================================================
FPV Rover v2.5.2
Units: United States of America (Fahrenheit, Volts, etc.)
Contributing Authors:
 - James Mater
 - OpenAI Codex (model: GPT-5) - AI contributing author

System Overview:
 ESP32-CAM hosts the web UI + camera stream and sends drive/mode/speed commands
 to the UNO helper over UART1. The UNO handles I2C sensors, motor control,
 autonomous sonar/cliff avoidance, and Jazzy one-wire serial output.
================================================================================
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HardwareSerial.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Camera pin & board definitions
#include "board_config.h"

// Helper status (from UNO)
String comm_bat12 = "--";
String comm_bat5 = "--";
String comm_ina12 = "--";
String comm_ina5 = "--";
String comm_mcp = "--";
String comm_i2c = "--";
String comm_sonar = "--";
String comm_cliff = "--";
String comm_gps = "--";
String comm_sound = "--";

// WiFi credentials
const char *ssid = "NID7417";
const char *password = "blackrabbits";
const char *ap_ssid = "ESP32-Rover";
const char *ap_password = "";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 12000;
const unsigned long HELPER_STATUS_REQUEST_INTERVAL_MS = 5000;

void startCameraServer();
void setupLedFlash();
void initHelperUart();
void sendHelperCmd(const String& cmd);
void updateHelperWifiLed();
void requestHelperStatus();

static String helperLine = "";
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
  else if (key == "DIST") comm_sonar = val;
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
  // Disable brownout detector for stable WiFi startup
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println("\n[INIT] FPV Rover Starting...");

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
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    Serial.printf("[PSRAM] OK | Total: %u bytes\n", ESP.getPsramSize());
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("[PSRAM] Not Found (Limited resolution)");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("[WIFI] Connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    WiFi.disconnect(true);
    delay(100);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("[WIFI] AP Started. IP: ");
    Serial.println(WiFi.softAPIP());
  }

  updateHelperWifiLed();
  startCameraServer();
  Serial.println("[HTTP] Camera Server active");
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
      if (helperLine.length() < 128) helperLine += c;
    }
  }

  delay(5);
}
