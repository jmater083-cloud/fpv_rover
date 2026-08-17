// Demonstration code to control a Pride Jazzy Select by remote
// control. 
//
// 2025-08-21 Brent A. Crosby
//
//---------------------------------------------------------------------------
//This is free and unencumbered software released into the public domain.
//
//Anyone is free to copy, modify, publish, use, compile, sell, or
//distribute this software, either in source code form or as a compiled
//binary, for any purpose, commercial or non-commercial, and by any
//means.
//
//In jurisdictions that recognize copyright laws, the author or authors
//of this software dedicate any and all copyright interest in the
//software to the public domain. We make this dedication for the benefit
//of the public at large and to the detriment of our heirs and
//successors. We intend this dedication to be an overt act of
//relinquishment in perpetuity of all present and future rights to this
//software under copyright law.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
//OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
//ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//OTHER DEALINGS IN THE SOFTWARE.
//
//For more information, please refer to <http://unlicense.org/>
//============================================================================
#include <Arduino.h>
//So we can read the servo PWM signals from the R/C receiver
#include <ServoInput.h>
// Used to set the pace of the mainloop
#include <TimerOne.h>

#define LED_PIN 13

//===========================================================================
volatile uint8_t
  time_to_execute_main_loop;
void timerIsr()
  {
  time_to_execute_main_loop=1;
  }
//===========================================================================
//These are the two interrupt pins that need to be connected to the
//receiver's PWM out signals.
#define SERVO_TURN_INT_2   (2)
#define SERVO_SPEED_INT_3  (3)
ServoInputPin<2> servo_p2_turn;
ServoInputPin<3> servo_p3_speed;
//===========================================================================
// I reverse engineered the packets by snooping on the serial line that
// goes from the hardware controller to the motor controller. Details
// are in . . . CFA10100\Serial Decode
void Send_Jazzy_Packet(uint8_t speed,
                       int8_t fore_aft,
                       int8_t left_right)
  {
  uint8_t
    checksum;

  //first packet
  checksum=0xFF-((uint8_t)0x4A+(uint8_t)0x01+(uint8_t)speed+(uint8_t)fore_aft+(uint8_t)left_right);
  Serial.write(0x4A);       //start of packet
  Serial.write(0x01);       // beep and power
  Serial.write(speed);      // speed: 0x00 to 0x0E
  Serial.write(fore_aft);   // fore/aft: -100 to + 100
  Serial.write(left_right); // left/right: -100 to + 100
  Serial.write(checksum);
/*
  // The original Jazzy hardware joystick controller sends a second
  // packet, but it appears that the second packet is not needed.
  delay(2);
  //second packet 
  // just using copied string of bytes for now
  Serial.write(0xFE);
  Serial.write(0x54);
  Serial.write(0x00);
  Serial.write(0xA0);
  Serial.write(0xA1);
  Serial.write(0x51);  // somewhat related to speed
  Serial.write(0x19);
*/
  }
//===========================================================================
//JAZZY
// The Jazzy needs a ~300ms break followed by a continuous stream of
// packets. I suspect that some number of the initial  packets need to be
// 0 speed, 0 turn, but I have not verified it.
//
// After some long time of only 0 speed, 0 turn packets, the motor
// controller will go to sleep again. It is something longer that
// 10 minutes.
//
// If the motor controller goes to sleep, you can send a break to
// wake it up again.
void Send_Jazzy_Break(void)
  {
  Serial.end();
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);  // Send BREAK
  delay(300);
  //Initialize serial for the Jazzy Select control
  //38400 baud, 8 bits even parity, 1 stop
  Serial.begin(38400,SERIAL_8E1);
  delay(30);
  }
//============================================================================
void setup()
  { 
  //Status LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN,1);

  // Set up a timer so main loop can execute at 60 Hz
  Timer1.initialize(16667); // 1/60 second
  Timer1.attachInterrupt(timerIsr);

  //Initialize serial for the Jazzy Select control
  //38400 baud, 8 bits even parity, 1 stop
  Serial.begin(38400,SERIAL_8E1);

  //Input pins for reading the R/C receiver's servo outputs
  pinMode(SERVO_TURN_INT_2, INPUT_PULLUP);
  pinMode(SERVO_SPEED_INT_3, INPUT_PULLUP);

  //Do the servo input library thing
	servo_p2_turn.attach();   // attaches the servo input interrupt
	servo_p3_speed.attach();  // attaches the servo input interrupt
  }
//============================================================================

int8_t
  x_turn=0;
int8_t
  y_speed=0;
// 0 = slow, 14=fast
int8_t
  speed_range=14;

// The Jazzy's motor controller will fall asleep after some time. Something
// greater than 10 minutes. If we have not moved in 5 minutes, preemptively
// send a break before a movement command to force it awake.
//
// We will count 60Hz mainloops:
//  5 mimutes * 60 seconds * 60 loops/second = 18000 loops
//
// If it is more than 5 minutes since a non-zero packet is sent, then
// We will send a break before the next non-zero packet is sent.
uint32_t  // ~2.3 years before overflow
  jazzy_time_with_no_movement=0;
//----------------------------------------------------------------------------
void loop()
  {
  // Send a 300ms break to signal "wake up" the wheel controller. You must send
  // packets continously thereafter
  Send_Jazzy_Break();

  //Timeout the servo readings after 1/2 second
  uint8_t
    servo_reading_timeout;
  servo_reading_timeout=30;
  uint16_t
    servo_p2_turn_uS;
  servo_p2_turn_uS=0;
  uint16_t
    servo_p3_speed_uS;
  servo_p3_speed_uS=0;

  //We want to flash the red LED if the
  //receiver is giving us good data.
  //Solid red if no servo data
  uint8_t
    LED_Blink_Count;
  LED_Blink_Count=0;

  while(1)
    {
    // Wait for the 60Hz ISR to hit.
    while(0 == time_to_execute_main_loop)
      {
      ; 
      }
    time_to_execute_main_loop=0;
    LED_Blink_Count++;

    //See if new servo readings are available
    if((0 != servo_p2_turn.available()) &&
       (0 != servo_p3_speed.available()))
      {
      // New set of readings is available, reset 1/2 second timeout
      servo_reading_timeout=30;
      //Save new readings
      servo_p2_turn_uS=servo_p2_turn.getPulse();
      servo_p3_speed_uS=servo_p3_speed.getPulse();
      }
    else
      {
      //No new reading. Check to see if we have timed out.
      if(0 < servo_reading_timeout)
        {
        // not timed out yet -- but getting closer.
        servo_reading_timeout--;
        }
      else
        {
        //We have not had a new reding in 1/2 second.
        //Set the varaibles to an invalid reading.
        servo_p2_turn_uS=0;
        servo_p3_speed_uS=0;
        }
      }

    //See if there are valid servo readings
    if((995 < servo_p2_turn_uS ) && (servo_p2_turn_uS  < 2005 ) &&
       (995 < servo_p3_speed_uS) && (servo_p3_speed_uS < 2005))
      {
      //valid servo signal from the receiver
      //clip so math is clean
      if(servo_p2_turn_uS < 1000)
        servo_p2_turn_uS=1000;
      if(2000 < servo_p2_turn_uS)
        servo_p2_turn_uS=2000;
      if(servo_p3_speed_uS < 1000)
        servo_p3_speed_uS=1000;
      if(2000 < servo_p3_speed_uS)
        servo_p3_speed_uS=2000;

      //Convert from 1000 to 1500 to 2000 land to -100 to 0 to +100 land
      int16_t
        signed_temp;
      //from 1000 to 1500 to 2000 land to -500 to 0 to +500 land
      signed_temp=servo_p2_turn_uS-1500;
      //from -500 to 0 to +500 land to -100 to 0 to +500 land
      signed_temp/=5;

      x_turn=signed_temp;

      //from 1000 to 1500 to 2000 land to -500 to 0 to +500 land
      signed_temp=servo_p3_speed_uS-1500;
      //from -500 to 0 to +500 land to -100 to 0 to +500 land
      signed_temp/=5;
      y_speed=signed_temp;
      }

    // Keep track of if we might need to wake Jazzy out
    // of sleep mode.
    if((13 < y_speed) ||
       (13 < x_turn ))
      {
      //We have been asked to move.
      //If it has been more than 5 minutes, send a
      //break first to wake the motor controller up.
      if(18000 <= jazzy_time_with_no_movement)
        {
        Send_Jazzy_Break(); 
        }
      //Keep track that we are moving.
      jazzy_time_with_no_movement=0;
      }
    else
      {
      //Not moving
      jazzy_time_with_no_movement++;
      }

    Send_Jazzy_Packet(speed_range,  //speed
                      y_speed,  //fore_aft
                      x_turn); //left_right)

    // LED blinks if normal.
    // Solid red if invalid servo input.
    if((0 == servo_p2_turn_uS) ||
       (0 == servo_p3_speed_uS))
      {
      //Error, no PWM signal from receiver
      //LED stays on
      digitalWrite(LED_PIN,1);
      }
    else
      {
      //Normal heartbeat blinky
      if(LED_Blink_Count & 0x08)
        {
        //LED blink on
        digitalWrite(LED_PIN,1);
        }
      else
        {
        //LED blink off
        digitalWrite(LED_PIN,0);
        }
      }

    } // while(1)

  }
//============================================================================
