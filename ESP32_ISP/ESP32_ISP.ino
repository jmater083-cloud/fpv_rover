/*
ESP32-CAM as ArduinoISP programmer
==================================
Flashes an ATmega328P (UNO) through its ICSP header, bypassing a broken
onboard CH340 USB chip.

Wiring (ESP32-CAM -> UNO ICSP header):
  ESP32 GPIO2  -> ICSP pin 4 (MOSI / D11)
  ESP32 GPIO4  -> ICSP pin 1 (MISO / D12)
  ESP32 GPIO15 -> ICSP pin 3 (SCK / D13)
  ESP32 GPIO16 -> ICSP pin 5 (RESET)
  ESP32 5V     -> ICSP pin 2 (VCC)
  ESP32 GND    -> ICSP pin 6 (GND)

IMPORTANT:
  - Disconnect the UNO from USB while programming via ICSP.
  - The UNO must be powered (5V from ESP32 is fine for programming).
  - This uses bit-banged SPI (SoftwareSPI) for reliability.

Usage:
  1. Upload this sketch to the ESP32-CAM (COM5).
  2. Wire the 6 connections above.
  3. From the PC, run:
     arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32 \
       --programmer arduinoasisp "UNO_Helper"
     (or use avrdude directly with the ESP32 as the programmer)
*/

#include <Arduino.h>

// --- ISP pins on ESP32-CAM ---
#define ISP_MOSI 2
#define ISP_MISO 4
#define ISP_SCK  15
#define ISP_RESET 16

// --- AVR ISP protocol constants ---
#define AVR_ISP_ENTER_PROGRAMMING 0xAC
#define AVR_ISP_LEAVE_PROGRAMMING 0x51
#define AVR_ISP_READ_SIGNATURE    0x30
#define AVR_ISP_READ_FUSE         0x50
#define AVR_ISP_WRITE_FUSE        0xAC
#define AVR_ISP_READ_LOCK         0x58
#define AVR_ISP_WRITE_LOCK        0xAC
#define AVR_ISP_READ_FLASH        0x20
#define AVR_ISP_WRITE_FLASH       0x40
#define AVR_ISP_READ_EEPROM       0xA0
#define AVR_ISP_WRITE_EEPROM      0xC0
#define AVR_ISP_CHIP_ERASE        0xAC

// --- Software SPI bit-bang ---
void ispPinSetup() {
  pinMode(ISP_MOSI, OUTPUT);
  pinMode(ISP_SCK, OUTPUT);
  pinMode(ISP_RESET, OUTPUT);
  pinMode(ISP_MISO, INPUT_PULLUP);
  digitalWrite(ISP_MOSI, LOW);
  digitalWrite(ISP_SCK, LOW);
  digitalWrite(ISP_RESET, HIGH);  // target not in reset
}

void ispClockPulse() {
  digitalWrite(ISP_SCK, HIGH);
  delayMicroseconds(5);
  digitalWrite(ISP_SCK, LOW);
  delayMicroseconds(5);
}

uint8_t ispTransfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(ISP_MOSI, (out >> i) & 1);
    ispClockPulse();
    if (digitalRead(ISP_MISO)) in |= (1 << i);
  }
  return in;
}

// --- AVR ISP commands ---
void ispEnterProgramming() {
  digitalWrite(ISP_RESET, LOW);  // enter programming mode
  delay(50);
  ispTransfer(AVR_ISP_ENTER_PROGRAMMING);
  ispTransfer(0x53);
  ispTransfer(0x00);
  ispTransfer(0x00);
  delay(50);
}

void ispLeaveProgramming() {
  ispTransfer(AVR_ISP_LEAVE_PROGRAMMING);
  ispTransfer(0x00);
  ispTransfer(0x00);
  ispTransfer(0x00);
  digitalWrite(ISP_RESET, HIGH);  // exit programming mode
  delay(50);
}

uint8_t ispReadSignature(uint8_t index) {
  ispTransfer(AVR_ISP_READ_SIGNATURE);
  ispTransfer(0x00);
  ispTransfer(index);
  ispTransfer(0x00);
  return ispTransfer(0x00);
}

uint8_t ispReadFuse(uint8_t fuse) {
  ispTransfer(AVR_ISP_READ_FUSE);
  ispTransfer(0x00);
  ispTransfer(fuse);
  ispTransfer(0x00);
  return ispTransfer(0x00);
}

void ispChipErase() {
  ispTransfer(AVR_ISP_CHIP_ERASE);
  ispTransfer(0x80);
  ispTransfer(0x00);
  ispTransfer(0x00);
  delay(100);
}

// --- Serial protocol for host (avrdude-style) ---
// The host sends commands as bytes; we respond with results.
// This is a minimal implementation that supports:
//   'S' - read signature (3 bytes)
//   'F' - read fuses (3 bytes: low, high, ext)
//   'E' - chip erase
//   'P' - enter programming
//   'L' - leave programming
//   'R' - read flash byte (addr hi, addr lo) -> returns byte
//   'W' - write flash byte (addr hi, addr lo, data)
//   'r' - read eeprom byte (addr) -> returns byte
//   'w' - write eeprom byte (addr, data)

void setup() {
  Serial.begin(115200);
  ispPinSetup();
  Serial.println("ESP32-ISP ready");
}

void loop() {
  if (Serial.available() >= 1) {
    char cmd = Serial.read();

    switch (cmd) {
      case 'P':  // enter programming
        ispEnterProgramming();
        Serial.println("OK");
        break;

      case 'L':  // leave programming
        ispLeaveProgramming();
        Serial.println("OK");
        break;

      case 'S': {  // read signature
        uint8_t sig[3];
        sig[0] = ispReadSignature(0);
        sig[1] = ispReadSignature(1);
        sig[2] = ispReadSignature(2);
        Serial.write(sig, 3);
        break;
      }

      case 'F': {  // read fuses
        uint8_t fuses[3];
        fuses[0] = ispReadFuse(0x00);  // low
        fuses[1] = ispReadFuse(0x08);  // high
        fuses[2] = ispReadFuse(0x04);  // extended
        Serial.write(fuses, 3);
        break;
      }

      case 'E':  // chip erase
        ispChipErase();
        Serial.println("OK");
        break;

      case 'R': {  // read flash byte
        while (Serial.available() < 2) { delay(1); }
        uint8_t hi = Serial.read();
        uint8_t lo = Serial.read();
        uint16_t addr = (hi << 8) | lo;
        ispTransfer(AVR_ISP_READ_FLASH);
        ispTransfer(hi);
        ispTransfer(lo);
        ispTransfer(0x00);
        uint8_t data = ispTransfer(0x00);
        Serial.write(data);
        break;
      }

      case 'W': {  // write flash byte
        while (Serial.available() < 3) { delay(1); }
        uint8_t hi = Serial.read();
        uint8_t lo = Serial.read();
        uint8_t data = Serial.read();
        uint16_t addr = (hi << 8) | lo;
        ispTransfer(AVR_ISP_WRITE_FLASH);
        ispTransfer(hi);
        ispTransfer(lo);
        ispTransfer(data);
        ispTransfer(0x00);
        delay(5);  // flash write time
        Serial.println("OK");
        break;
      }

      case 'r': {  // read eeprom byte
        while (Serial.available() < 1) { delay(1); }
        uint8_t addr = Serial.read();
        ispTransfer(AVR_ISP_READ_EEPROM);
        ispTransfer(0x00);
        ispTransfer(addr);
        ispTransfer(0x00);
        uint8_t data = ispTransfer(0x00);
        Serial.write(data);
        break;
      }

      case 'w': {  // write eeprom byte
        while (Serial.available() < 2) { delay(1); }
        uint8_t addr = Serial.read();
        uint8_t data = Serial.read();
        ispTransfer(AVR_ISP_WRITE_EEPROM);
        ispTransfer(0x00);
        ispTransfer(addr);
        ispTransfer(data);
        ispTransfer(0x00);
        delay(5);  // eeprom write time
        Serial.println("OK");
        break;
      }

      default:
        // Unknown command - ignore
        break;
    }
  }
}