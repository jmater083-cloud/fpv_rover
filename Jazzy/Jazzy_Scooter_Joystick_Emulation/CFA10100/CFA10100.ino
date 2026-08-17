
  // I have marked all the modifications to the original
  // Crystalfontz supplied code with:
  //
  //JAZZY
  //
  // I left all the demos in from the original Crystalfontz
  // demo code. They are all turned off in CFA10100_defines.h
  // except for the BMP_DEMO and TOUCH_DEMO. The unmodified
  // BMP_DEMO code is used to put the background graphic up.
  // The TOUCH_DEMO code is used as a framework to do the
  // UI ineraction and send the commands to the chair.
  //
  // We talk to the chair controller through the serial port,
  // so we cannot have debug on. Set DEBUG_LEVEL to DEBUG_NONE
  // in CFA10100_defines.h
  //
  // You probably do want to turn on debug when setting
  // PROGRAM_FLASH_FROM_USD to program the background 
  // image . . . CFA10100\uSD_Files\splash.a8z from the
  // uSD card to the flash on the CFA10100 EVE board.


//===========================================================================
//
// Crystalfontz Seeeduino / Arduino Simple Demonstration Program
// for FTDI / BridgeTek EVE graphic accelerators.
//
// Code written for Seeeduino v4.2 set to 3.3v (important!)
// Seeeduino v4.2 is an Arduino Uno clone that can run at 3.3v.
//
//---------------------------------------------------------------------------
//
// This is a simplified / refactored version of the code in FTDI's AN_275:
//
//  http://brtchip.com/wp-content/uploads/Support/Documentation/Application_Notes/ICs/EVE/AN_275_FT800_Example_with_Arduino.pdf
//
// I have added support for the BridgeTek BT817 EVE series.
//
// In the spirit of AN_275:
//
//   An “abstraction layer” concept was explicitly avoided in this
//   example. Rather, direct use of the Arduino libraries demonstrates
//   the simplicity of sending and receiving data through the FT800
//   while producing a graphic output.
//
// My main goal here is to be transparent about what is really happening
// from the high to lowest levels, without obfuscation, while still
// at least giving a nod to good programming practices.
//
// Plus, you probably don't have RAM and flash for all those fancy
// programming layers.
//
// The FTDI write offset (FWo) into the FT813's circular write write buffer
// is passed into and back from functions (FWol = FWo local) rather than being
// a global. Keeping track of the write offset avoids having to read that
// information from the FT813 before every SPI transaction.
//
// A nod to Rudolph R and company over at
//   https://www.mikrocontroller.net/topic/395608
//   https://github.com/RudolphRiedel/FT800-FT813
// for deep insight and lots of help in increasing our understanding
// of the fiddly bits of the EVE hardware and software architecture.
//
// 2020-08-05 Brent A. Crosby / Crystalfontz America, Inc.
// https://www.crystalfontz.com/products/eve-accelerated-tft-displays.php
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
// Adapted from:
// FTDIChip AN_275 FT800 with Arduino - Version 1.0
//
// Copyright (c) Future Technology Devices International
//
// THIS SOFTWARE IS PROVIDED BY FUTURE TECHNOLOGY DEVICES INTERNATIONAL
// LIMITED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL FUTURE TECHNOLOGY
// DEVICES INTERNATIONAL LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES LOSS OF USE,
// DATA, OR PROFITS OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// This code is provided as an example only and is not guaranteed by
// FTDI/BridgeTek. FTDI/BridgeTek accept no responsibility for any issues
// resulting from its use. By using this code, the developer of the final
// application incorporating any parts of this sample project agrees to take
// full responsible for ensuring its safe and correct operation and for any
// consequences resulting from its use.
//===========================================================================
#include <Arduino.h>
#include <SPI.h>
#include <stdarg.h>
// Definitions for our circuit board and display.
#include "CFA10100_defines.h"

#if BUILD_SD
#include <SD.h>
#endif

// The very simple EVE library files
#include "EVE_base.h"
#include "EVE_draw.h"

// Our demonstrations of various EVE functions
#include "demos.h"

// Seeeduino pinouts: 
// https://wiki.seeedstudio.com/Seeeduino_v4.2/

//===========================================================================
//JAZZY
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
// The Jazzy needs a ~300ms break followed by several a continuous
// stream of packets. I suspect that some number of the initial
// packets need to be 0 speed, 0 turn, but I have not verified it.
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
  }
//===========================================================================
void setup()
  {

#if (DEBUG_LEVEL == DEBUG_NONE)
//JAZZY
  //Initialize serial for the Jazzy Select control
  //38400 baud, 8 bits even parity, 1 stop
  Serial.begin(38400,SERIAL_8E1);
#endif

#if (DEBUG_LEVEL != DEBUG_NONE)
  // Initialize UART for debugging messages
  Serial.begin(115200);
#endif // (DEBUG_LEVEL != DEBUG_NONE)
  DBG_STAT("Begin\n");

  //Initialize GPIO port states
  // Set CS# high to start - SPI inactive
  SET_EVE_CS_NOT;
  // Set PD# high to start
  SET_EVE_PD_NOT;
  SET_SD_CS_NOT;

  //Initialize port directions
  // EVE interrupt output (not used in this example)
  pinMode(EVE_INT, INPUT_PULLUP);
  // EVE Power Down (reset) input
  pinMode(EVE_PD_NOT, OUTPUT);
  // EVE SPI bus CS# input
  pinMode(EVE_CS_NOT, OUTPUT);
  // USD card CS
  pinMode(SD_CS, OUTPUT);
  // Optional pin used for LED or oscilloscope debugging.
  pinMode(DEBUG_LED, OUTPUT);

  // Initialize SPI
  SPI.begin();
  //Bump the clock to 8MHz. Appears to be the maximum.
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  DBG_GEEK("SPI initialzed to: 8MHz\n");

#if BUILD_SD
  // The prototype hardware appears to functon fine at 8MHz which
  // also appears to be the max that the ATmega328P can do.
  if (!SD.begin(8000000,SD_CS))
    {
    DBG_STAT("uSD card failed to initialize, or not present\n");
    //Reset the SPI clock to fast. SD card library does not clean up well.
    //Bump the clock to 8MHz. Appears to be the maximum.
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    }
  else
    {
    DBG_STAT("uSD card initialized.\n");
    }
#endif

  //See if we can find the FTDI/BridgeTek EVE processor
  if(0 != EVE_Initialize())
    {
    DBG_STAT("Failed to initialize %s8%02X. Stopping.\n",EVE_DEVICE<0x14?"FT":"BT",EVE_DEVICE);
    while(1);
    }
  else
    {
    DBG_STAT("%s8%02X initialized.\n",EVE_DEVICE<0x14?"FT":"BT",EVE_DEVICE);
    }

  } //  setup()
//===========================================================================
//JAZZY
int8_t
  x_turn=0;
int8_t
  y_speed=0;
int8_t
  speed_range=10;

//---------------------------------------------------------------------------
void loop()
  {
  DBG_GEEK("Loop initialization.\n");

  //Get the current write pointer from the EVE
  uint16_t
    FWo;
  FWo = EVE_REG_Read_16(EVE_REG_CMD_WRITE);
  DBG_GEEK("Initial Offset Read: 0x%04X = %u\n",FWo ,FWo);

  //Keep track of the RAM_G memory allocation
  uint32_t
    RAM_G_Unused_Start;
  RAM_G_Unused_Start=0;
  DBG_GEEK("Initial RAM_G: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);

  // We need to keep track of the bitmap handles and where they are used.
  //
  // By default, bitmap handles 16 to 31 are used for built-in font and 15
  // is used as scratch bitmap handle by co-processor engine commands
  // CMD_GRADIENT, CMD_BUTTON and CMD_KEYS.
  //
  // For whatever reason, I am going to allocate handles from 14 to 0.
  uint8_t
    next_bitmap_handle_available;
  next_bitmap_handle_available=14;

  DBG_GEEK("EVE_Initialize_Flash() . . . ");
  FWo=EVE_Initialize_Flash(FWo);
  DBG_GEEK("done.\n");

  uint8_t
    flash_status;
  flash_status = EVE_REG_Read_8(EVE_REG_FLASH_STATUS);
  DBG_GEEK_Decode_Flash_Status(flash_status);

#if (0 != PROGRAM_FLASH_FROM_USD)
  //Keep track of the current write pointer into flash.
  uint32_t
    Flash_Sector;
  Flash_Sector=0;

  //Load the BLOB & write our image data to the flash from
  //the uSD card. This only needs to be executed once. It
  //uses RAM_G as scratch temporary memory, but does not
  //allocate any RAM_G.
  FWo= Initialize_Flash_From_uSD(FWo,
                                 RAM_G_Unused_Start,
                                 &Flash_Sector);
#else  // (0 != PROGRAM_FLASH_FROM_USD)
  DBG_GEEK("Not programming flash.\n");
#endif // (0 != PROGRAM_FLASH_FROM_USD)

#if (0 != BOUNCE_DEMO)
  // JAZZY this is used after PROGRAM_FLASH_FROM_USD
  DBG_STAT("Initialize_Bounce_Demo() . . .");
  Initialize_Bounce_Demo();
  DBG_STAT(" done.\n");
#endif // (0 != BOUNCE_DEMO)

#if (0 != LOGO_DEMO)
  DBG_STAT("Initialize_Logo_Demo() . . .");
  FWo=Initialize_Logo_Demo(FWo,&RAM_G_Unused_Start,next_bitmap_handle_available);
  //Keep track that we used a bitmap handle
  next_bitmap_handle_available--;
    
  DBG_STAT("  done.\n");
  DBG_GEEK("RAM_G after logo: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
#endif // (0 != LOGO_DEMO)

#if (0 != BMP_DEMO)
  DBG_STAT("Initialize_Bitmap_Demo() . . .");
  FWo=Initialize_Bitmap_Demo(FWo,next_bitmap_handle_available);
  //Keep track that we used a bitmap handle
  next_bitmap_handle_available--;
  DBG_STAT("  done.\n");
  DBG_GEEK("RAM_G after bitmap: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
#endif //(0 != BMP_DEMO)

#if (0 != SOUND_DEMO)
  DBG_STAT("Initialize_Sound_Demo() . . .");
  FWo=Initialize_Sound_Demo(FWo,&RAM_G_Unused_Start);
  DBG_STAT("  done.\n");
  DBG_GEEK("RAM_G after sound: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
#endif // (0 != SOUND_DEMO)

#if (0 != MARBLE_DEMO)
  DBG_STAT("Initialize_Marble_Demo() . . .");
  FWo=Initialize_Marble_Demo(FWo,&RAM_G_Unused_Start,next_bitmap_handle_available);
  //Keep track that we used a bitmap handle
  next_bitmap_handle_available--;
  DBG_STAT("  done.\n");
  DBG_GEEK("RAM_G after marble: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
#endif //MARBLE_DEMO

#if (0 != TOUCH_DEMO)
  //Bitmask of valid points in the array
  uint8_t
    points_touched_mask;
#if (EVE_TOUCH_TYPE == EVE_TOUCH_RESISTIVE)
  DBG_GEEK("Resistive touch, single point.\n");
  int16_t
    x_points[1];
  int16_t
    y_points[1];
#endif // (EVE_TOUCH_TYPE == EVE_TOUCH_RESISTIVE)

#if (EVE_TOUCH_TYPE == EVE_TOUCH_CAPACITIVE)
  DBG_GEEK("Capacitive touch, multiple points.\n");
  int16_t
    x_points[5];
  int16_t
    y_points[5];
#endif // (EVE_TOUCH_TYPE == EVE_TOUCH_CAPACITIVE)
#endif // (0 != TOUCH_DEMO)

#if (0 != VIDEO_DEMO)
  FWo=Initialize_Video_Demo(FWo,
                            &RAM_G_Unused_Start,
                            next_bitmap_handle_available);
  //Keep track that we used a bitmap handle
  next_bitmap_handle_available--;
  DBG_GEEK("RAM_G after video: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
#endif // (0 != VIDEO_DEMO)


  DBG_STAT("Initialization complete, entering main loop.\n");

//JAZZY
  //Send a 300ms break to signal "wake up". You must send packets
  //continously thereafter
  Send_Jazzy_Break();
  //Do not send a break for at least another 30 seconds
  uint16_t
    jazzy_break_blanking_time;
  jazzy_break_blanking_time=60*30;

  while(1)
    {
    //JAZZY
    Send_Jazzy_Packet(speed_range,  //speed
                      y_speed,  //fore_aft
                      x_turn); //left_right)

    //JAZZY
    //Keep track of when it is valid to send another break (wake up) signal
    //to the Jazzy.
    if(0 != jazzy_break_blanking_time)
      {
      jazzy_break_blanking_time--;
      }

    //========== FRAME SYNCHRONIZING ==========
    // Wait for graphics processor to complete executing the current command
    // list. This happens when EVE_REG_CMD_READ matches EVE_REG_CMD_WRITE, indicating
    // that all commands have been executed.  We have a local copy of
    // EVE_REG_CMD_WRITE in FWo.
    //
    // This appears to only occur on frame completion, which is nice since it
    // allows us to step the animation along at a reasonable rate.
    //
    // If possible, I have tweaked the timing on the Crystalfontz displays
    // to all have ~60Hz frame rate.
    FWo=Wait_for_EVE_Execution_Complete(FWo);

#if TOUCH_DEMO
    //Read the touch screen.
    points_touched_mask=Read_Touch(x_points,y_points);
#endif // TOUCH_DEMO

#if SOUND_DEMO
    //See if we should play a sound. The sound will synchronize
    //with the the start of the logo rotation.
    //If the previous sound is still playing it will wait until the
    //next time we call it.
    Start_Sound_Demo_Playing();
#endif //SOUND_DEMO

    //========== START THE DISPLAY LIST ==========
    // Start the display list
    FWo=EVE_Cmd_Dat_0(FWo,
                      (EVE_ENC_CMD_DLSTART));
  
    // Set the default clear color to black
    FWo=EVE_Cmd_Dat_0(FWo,
                      EVE_ENC_CLEAR_COLOR_RGB(0,0,0));
    // Clear the screen - this and the previous prevent artifacts between lists
    FWo=EVE_Cmd_Dat_0(FWo,
                      EVE_ENC_CLEAR(1 /*CLR_COL*/,1 /*CLR_STN*/,1 /*CLR_TAG*/));
    //========== ADD GRAPHIC ITEMS TO THE DISPLAY LIST ==========
    //Fill background with white
    FWo=EVE_Filled_Rectangle(FWo,
                             0,0,LCD_WIDTH-1,LCD_HEIGHT-1);
                            
#if (0 != BMP_DEMO)
    FWo=Add_Bitmap_To_Display_List(FWo);
#endif // BMP_DEMO


    //Put a dot to indicate speed range
    FWo=EVE_Cmd_Dat_0(FWo,
                      EVE_ENC_COLOR_A(0xFF));
    FWo=EVE_Cmd_Dat_0(FWo,
                      EVE_ENC_COLOR_RGB(0x00,0xFF,0x00));
    // Draw the touch dot -- a 40px point (filled circle)
    FWo=EVE_Point(FWo,
                  242*16,
                  (56+(26*(14-speed_range)))*16,
                  10*15);



#if (0 != TOUCH_DEMO)
    //See if we are touched at all.
    if(0 != points_touched_mask)
      {
      //Loop through the possible touch points
      uint8_t
        mask;
      mask=0x01;
      // This is a multi-touch display. We have three regions (wake,
      // speed range, joystick). Only use the first touch within
      // a given region.
      uint8_t
        wake_box_touched;
      uint8_t
        range_box_touched;
      uint8_t
        joystick_box_touched;
      wake_box_touched=0;
      range_box_touched=0;
      joystick_box_touched=0;
      for(uint8_t i=0;i<5;i++)
        {
        if(0 != (points_touched_mask&mask))
          {
          // This code loops through all the points touched, putting a
          // live dot on the display, to indicate the touch position,
          // color coded based on the touch number.
          static uint32_t colors[5]=
            {
            EVE_ENC_COLOR_RGB(0x00,0x00,0xFF),  // 1 => blue
            EVE_ENC_COLOR_RGB(0x00,0xFF,0x00),  // 2 => green
            EVE_ENC_COLOR_RGB(0xFF,0x00,0x00),  // 3 => red
            EVE_ENC_COLOR_RGB(0xFF,0x00,0xFF),  // 4 => magenta
            EVE_ENC_COLOR_RGB(0xFF,0xFF,0x00)   // 5 => yellow
            };
          // Set the drawing color
          FWo=EVE_Cmd_Dat_0(FWo,
                            colors[i]);
          // Make it solid
          FWo=EVE_Cmd_Dat_0(FWo,
                            EVE_ENC_COLOR_A(0xFF));
          // Draw the touch dot -- a 40px point (filled circle)
          FWo=EVE_Point(FWo,
                        x_points[i]*16,
                        y_points[i]*16,
                        40*16);
   
          //JAZZY
          //If the touch is in the wake button box, send a break to wake it up again
          if((0 == wake_box_touched) &&
             ( 66 < x_points[i]) && (x_points[i] < 166) &&
             (357 < y_points[i]) && (y_points[i] < 423))
            {
            // Send a 300ms break to signal "wake up". You must send packets
            // continously thereafter - which we do at the top of the
            // frame synchronization loop.
            if(0 == jazzy_break_blanking_time)
              {
              Send_Jazzy_Break();
              //Do not send another break for at least 30s
              jazzy_break_blanking_time=30*60;
              }

            //Do not react to any more touches in this region.
            wake_box_touched=1;
            }

          //JAZZY
          //If the touch is in the speed range box, do that thing
          if((0 == range_box_touched) &&
             (218 < x_points[i]) && (x_points[i] < 313) &&
             ( 42 < y_points[i]) && (y_points[i] < 437))
            {
            //There is a touch in the speed range box.
            //Calculate the new range and set it.
            speed_range= ((420-y_points[i])*14)/(420-53);
            //Do not react to any more touches in this region.
            range_box_touched=1;
            }

          //JAZZY
          //If the touch is in the joystick  box, do that thing
          if((0 == joystick_box_touched) &&
             (348 < x_points[i]) && (x_points[i] < 773) &&
             ( 28 < y_points[i]) && (y_points[i] < 452))
            {
            //clip the location to a valid range
            uint16_t
              x_location;
            x_location=x_points[i];
            if(x_location < 360)
              x_location=360;
            if(760 < x_location)
              x_location=760;
            uint16_t
              y_location;
            y_location=y_points[i];
            if(y_location < 40)
              y_location=40;
            if(440 < y_location)
              y_location=440;
            //calculate speed ticks based on the clipped location
            //location is +- 200 from center speed is +- 100
            int16_t
              signed_16_temp;

            //Speed is Y
            //Touch is from +40 (forward) 240 (stop) +440 (reverse)
            //offset to  0 (forward) 200 (stop) +400 (reverse)
            signed_16_temp=y_location-40;
            //scale to 0 (forward) 100 (stop) +200 (reverse)
            signed_16_temp=signed_16_temp/2;
            //offset to 100 (forward) 0 (stop) -100 (reverse)
            signed_16_temp=100-signed_16_temp;
            //Now we know that it will fit in the int8_t needed in the packet
            y_speed=(int8_t)signed_16_temp;

            //Turn is X
            //Touch is from +360 (left) 560 (straight) +760 (right)
            //offset to  0 (left) 200 (straight) +400 (right)
            signed_16_temp=x_location-360;
            //scale to 0 (left) 100 (straight) +200 (right)
            signed_16_temp=signed_16_temp/2;
            //offset to 100 (left) 0 (straight) -100 (right)
            signed_16_temp=100-signed_16_temp;
            //Now we know that it will fit in the int8_t needed in the packet
            x_turn=(int8_t)signed_16_temp;
          
            //Print the speed and turn values to the screen
            FWo=EVE_Cmd_Dat_0(FWo,
                              EVE_ENC_COLOR_RGB(0x00,0x00,0xFF));
            FWo=EVE_PrintF(FWo,
                          10,
                          440,
                          25,         //Font
                          0, //Options (default left)
                          "S=%4d, T=%4d",
                          y_speed,
                          x_turn);
            //Do not react to any more touches in this region.
            joystick_box_touched=1;                            
            }
          }

        mask<<=1;
        }  //for(uint8_t i=0;i<5;i++)
      }  //if(0 != points_touched_mask)

    else
      {
      //not touched, stop the motors
      y_speed=0;
      x_turn=0;

      FWo=EVE_Cmd_Dat_0(FWo,
                        EVE_ENC_COLOR_RGB(0xFF,0x00,0x00));
      // Make it solid
      FWo=EVE_Cmd_Dat_0(FWo,
                        EVE_ENC_COLOR_A(0xFF));
      // Draw the touch dot -- a 60px point (filled circle)
      FWo=EVE_Point(FWo,
                    560*16,
                    240*16,
                    30*16);

      //Print the speed and turn values to the screen
      FWo=EVE_PrintF(FWo,
                    10,
                    440,
                    25,         //Font
                    0, //Options (default left)
                    "S=%4d, T=%4d",
                    y_speed,
                    x_turn);
      }




#endif // (0 != TOUCH_DEMO)

#if (0 != MARBLE_DEMO)
#if (0 != TOUCH_DEMO)
    //Only show the bouncing marble if no there is no touch
    if(0 == points_touched_mask)
      {
#endif //(0 != TOUCH_DEMO)
      FWo=Add_Marble_To_Display_List(FWo);
#if (0 != TOUCH_DEMO)
      }
#endif //(0 != TOUCH_DEMO)
#endif //(0 != MARBLE_DEMO)

#if (0 != BOUNCE_DEMO)
    //========== BOUNCY BALL $ RUBBER BAND ==========
    FWo=Add_Bounce_To_Display_List( FWo);
#endif //BOUNCE_DEMO

#if (0 != VIDEO_DEMO)
#if (0 != TOUCH_DEMO)
    FWo=Add_Video_To_Display_List(FWo,points_touched_mask,x_points,y_points);
#else // (0 != TOUCH_DEMO)
    FWo=Add_Video_To_Display_List(FWo);
#endif // (0 != TOUCH_DEMO)
#endif // (0 != VIDEO_DEMO)


#if (0 != LOGO_DEMO)
    FWo=Add_Logo_To_Display_List(FWo);
#endif // (0 != LOGO_DEMO)

#if (0 != REMOTE_BACKLIGHT_DEBUG)
    int
      byte_read;
    if(-1 != (byte_read=Serial.read()))
      {
      DBG_GEEK("Serial Data Read: %3d 0x%02X",byte_read,byte_read);
      if(byte_read<=128)
        {
        EVE_REG_Write_8(EVE_REG_PWM_DUTY,byte_read);
        DBG_GEEK(", backlight set.");
        }
      DBG_GEEK("\n");
      }
#endif // (0 != REMOTE_BACKLIGHT_DEBUG)

#if (0 != VIDEO_DEMO)
    //Move the video to the next 30Hz frame 
    FWo=Update_Video_Frame(FWo);
#endif // (0 != VIDEO_DEMO)

    //========== FINSH AND SHOW THE DISPLAY LIST ==========
    // Instruct the graphics processor to show the list
    FWo=EVE_Cmd_Dat_0(FWo, EVE_ENC_DISPLAY());
    // Make this list active
    FWo=EVE_Cmd_Dat_0(FWo, EVE_ENC_CMD_SWAP);
    // Update the ring buffer pointer so the graphics processor starts executing
    EVE_REG_Write_16(EVE_REG_CMD_WRITE, (FWo));



#if (0 != BOUNCE_DEMO)
    //========== MOVE THE BALL AND CYCLE COLOR AND TRANSPARENCY ==========
    Bounce_Ball();
#endif //(0 != BOUNCE_DEMO)

#if (0 != MARBLE_DEMO)
  //========== BOUNCE THE MARBLE AROUND ==========
  Move_Marble();
#endif //(0 != MARBLE_DEMO)
    }  // while(1)
  } // loop()
//===========================================================================
