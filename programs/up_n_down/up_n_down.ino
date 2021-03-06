/*
Copyright (c) 2013, "Whitney Battestilli" <whitney@battestilli.net>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <hexbright.h>
#include <Wire.h>
#include <EEPROM.h>

#define EEPROM_LOCKED 0
#define EEPROM_NIGHTLIGHT_BRIGHTNESS 1

// Modes
#define MAX_MODE 5

#define MODE_OFF 0
#define MODE_LEVEL 1
#define MODE_BLINK 2
#define MODE_NIGHTLIGHT 3
#define MODE_SOS 4
#define MODE_LOCKED 5

 // Defaults
static const int glow_mode_time = 3000;
static const int click = 350; // maximum time for a "short" click
static const int nightlight_timeout = 10000; // timeout before nightlight powers down after any movement
static const unsigned char nightlight_sensitivity = 20; // measured in 100's of a G.

// State
static unsigned long treg1=0; 

static int bitreg=0;
const unsigned BLOCK_TURNING_OFF=0;
const unsigned GLOW_MODE=1;
const unsigned GLOW_MODE_SET=2;
const unsigned QUICKSTROBE=3;

static char mode = MODE_OFF;
static char new_mode = MODE_OFF;  

static word nightlight_brightness;

const word blink_freq_map[] = {70, 650, 10000}; // in ms
static word blink_frequency; // in ms;

static byte locked;
hexbright hb;

int adjustLED() {
  if(hb.button_pressed() && hb.button_pressed_time()>click) {
    double d = hb.difference_from_down();
    if(d>=0 && d<=1.0) {
      int i = (int)(d * 2000.0);
      i = i>1000 ? 1000 : i;
      i = i<=0 ? 1 : i;
      hb.set_light(CURRENT_LEVEL, i, 0);
      BIT_SET(bitreg,BLOCK_TURNING_OFF);
      return i;
    }
  }
  return -1;
}

byte updateEEPROM(word location, byte value) {
  byte c = EEPROM.read(location);
  Serial.print("Read "); Serial.print(c); Serial.print(" from EEPROM location: "); Serial.println(location); 
  if(c!=value) {
    Serial.print("Writing new value: "); Serial.println(value);
    EEPROM.write(location, value);
  }
  return value;
}

void setup() {
  // We just powered on!  That means either we got plugged
  // into USB, or the user is pressing the power button.
  hb = hexbright();
  hb.init_hardware();

  Serial.println("Reading defaults from EEPROM");
  locked = EEPROM.read(EEPROM_LOCKED);
  Serial.print("Locked: "); Serial.println(locked);
  nightlight_brightness = EEPROM.read(EEPROM_NIGHTLIGHT_BRIGHTNESS)*4;
  nightlight_brightness = nightlight_brightness==0 ? 1000 : nightlight_brightness;
  Serial.print("Nightlight Brightness: "); Serial.println(nightlight_brightness);  
  
  Serial.println("Powered up!");

  hb.config_click_count(click);
} 

void loop() {
  const unsigned long time = millis();
  int i;

  hb.update();
  
#ifdef PRINTING_NUMBER:
  if(!hb.printing_number()) 
#endif
  {
    // Charging
    hb.print_charge(GLED);

    // Low battery
    if(mode != MODE_OFF && hb.low_voltage_state())
      if(hb.get_led_state(RLED)==LED_OFF) 
	hb.set_led(RLED,50,1000);
  }

  new_mode = hb.click_count();
  if(new_mode>=MODE_OFF) {
    Serial.print("Clicks recieved: "); Serial.println((int)new_mode);
    if(mode!=MODE_OFF) {
      Serial.println("Forcing MODE_OFF");
      new_mode=MODE_OFF;
    } 
  }

  // Check for mode and do in-mode activities
  switch(mode) {
  case MODE_OFF:
    // glow mode
    if(BIT_CHECK(bitreg,GLOW_MODE))
      hb.set_led(GLED, 100);
    
    // holding the button
    if(hb.button_pressed() && !locked) {
	double d = hb.difference_from_down();
	if(BIT_CHECK(bitreg,QUICKSTROBE) || (hb.button_pressed_time() > click && d > 0.10 )) {
	  BIT_SET(bitreg,QUICKSTROBE);
	  if(treg1+blink_freq_map[0] < time) { 
	    treg1 = time; 
	    hb.set_light(MAX_LEVEL, 0, 20); 
	  }
	}
	if(hb.button_pressed_time() >= glow_mode_time && d <= 0.1 && 
	   !BIT_CHECK(bitreg,GLOW_MODE_SET) && !BIT_CHECK(bitreg,QUICKSTROBE) ) {
	  BIT_TOGGLE(bitreg,GLOW_MODE);
	  BIT_SET(bitreg,GLOW_MODE_SET);
	  if(BIT_CHECK(bitreg,GLOW_MODE))
	    Serial.println("Glow mode");
	  else
	    Serial.println("Glow mode off");	
	} 
    }
    if(hb.button_just_released()) {
      BIT_CLEAR(bitreg,GLOW_MODE_SET);
      BIT_CLEAR(bitreg,QUICKSTROBE);
    }
    break;
  case MODE_LEVEL:
    adjustLED();
    break;
  case MODE_NIGHTLIGHT: {
    if(!hb.low_voltage_state())
      hb.set_led(RLED, 100, 0);
    if(hb.moved(nightlight_sensitivity)) {
      //Serial.println("Nightlight Moved");
      treg1 = time;
      hb.set_light(CURRENT_LEVEL, nightlight_brightness, 1000);
    } else if(time > treg1 + nightlight_timeout) {
      hb.set_light(CURRENT_LEVEL, 0, 1000);
   }
    int i = adjustLED();
    if(i>0) {
      Serial.print("Nightlight Brightness: "); Serial.println(i);
      nightlight_brightness = i;
    }
    if(hb.button_just_released()) {
      Serial.print("Nightlight Brightness Saved: "); Serial.println(nightlight_brightness);
      updateEEPROM(EEPROM_NIGHTLIGHT_BRIGHTNESS, nightlight_brightness/4);
    }
    break; }
  case MODE_BLINK:
    if(hb.button_pressed()) {
      if( hb.button_pressed_time()> click) {
	double d = hb.difference_from_down();
	if(d>=0 && d<=0.99) {
	  if(d>=0.25) {
	    d = d>0.5 ? 0.5 : d;
	    blink_frequency = blink_freq_map[0] + (word)((blink_freq_map[1] - blink_freq_map[0]) * 4 * (0.5-d));
	  } else {
	    blink_frequency = blink_freq_map[1] + (word)((blink_freq_map[2] - blink_freq_map[1]) * 4 * (0.25-d));
	  }
	  Serial.print("Blink Freq: "); Serial.println(blink_frequency);
	  BIT_SET(bitreg,BLOCK_TURNING_OFF);
	}
      }
    }
    if(treg1+blink_frequency < time) { 
      treg1 = time;
      hb.set_light(MAX_LEVEL, 0, 20); 
    }
    break;
  case MODE_SOS:
    if(!hb.light_change_remaining())
    { // we're not currently doing anything, start the next step
      static int current_character = 0;
      static char symbols_remaining = 0;
      static byte pattern = 0;
      const char message[] = "SOS";
      const word morse[] = {	0x0300, // S ...
				0x0307, // O ---
				0x0300, // S ...
      };
      const int millisPerBeat = 150;
      
      if(current_character>=sizeof(message))
      { // we've hit the end of message, turn off.
        //mode = MODE_OFF;
        // reset the current_character, so if we're connected to USB, next printing will still work.
        current_character = 0;
        // return now to skip the following code.
        break;
      }
            
      if(symbols_remaining <= 0) // we're done printing our last character, get the next!
      {
	// Extract the symbols and length
	pattern = morse[current_character] & 0x00FF;
	symbols_remaining = morse[current_character] >> 8;
	// we count space (between dots/dashes) as a symbol to be printed;
	symbols_remaining *= 2;
        current_character++;
      }
      
      if (symbols_remaining<=0)
      { // character was unrecognized, treat it as a space
        // 7 beats between words, but 3 have already passed
        // at the end of the last character
        hb.set_light(0,0, millisPerBeat * 4);
      }
      else if (symbols_remaining==1) 
      { // last symbol in character, long pause
        hb.set_light(0, 0, millisPerBeat * 3);
      }
      else if(symbols_remaining%2==1) 
      { // even symbol, print space!
        hb.set_light(0,0, millisPerBeat);
      }
      else if (pattern & 1)
      { // dash, 3 beats
        hb.set_light(MAX_LEVEL, MAX_LEVEL, millisPerBeat * 3);
        pattern >>= 1;
      }
      else 
      { // dot, by elimination
        hb.set_light(MAX_LEVEL, MAX_LEVEL, millisPerBeat);
        pattern >>= 1;
      }
      symbols_remaining--;
    }
    break;
  }  

  // if we're locked, we prevent all other modes except MODE_LOCKED
  if(locked && new_mode!=MODE_LOCKED)
    new_mode=MODE_OFF;

  // Do the actual mode change
  if(new_mode>=MODE_OFF && new_mode!=mode) {
    double d;
    // we're changing mode
    Serial.print("New Mode: "); Serial.print((int)new_mode); Serial.print(" Mode: "); Serial.println((int)mode);
    switch(new_mode) {
    case MODE_OFF:
      Serial.println("Mode = off");
      hb.set_light(CURRENT_LEVEL, 0, NOW);
      if(!BIT_CHECK(bitreg,GLOW_MODE))
	hb.set_light(CURRENT_LEVEL, OFF_LEVEL, NOW);
      break;
    case MODE_LEVEL:
      Serial.println("Mode = level");
      d = hb.difference_from_down();
      i = MAX_LEVEL;
      if(d <= 0.40) {
	if(d <= 0.10)
	  i = 1;
	else
	  i = MAX_LOW_LEVEL;
      }
      hb.set_light(CURRENT_LEVEL, i, NOW); 
      break;
    case MODE_NIGHTLIGHT:
      Serial.println("Mode = nightlight");
      Serial.print("Nightlight Brightness: "); Serial.println(nightlight_brightness);  
#ifdef PRINTING_NUMBER:
      if(!hb.printing_number())
#endif
	hb.set_led(RLED, 1000, 0);
      break;
    case MODE_BLINK:
      Serial.println("Mode = blink");
      d = hb.difference_from_down();
      blink_frequency = blink_freq_map[0];
      if(d <= 0.40) {
	if(d <= 0.10)
	  blink_frequency = blink_freq_map[2];
	else
	  blink_frequency = blink_freq_map[1];
      }
      hb.set_light(MAX_LEVEL, 0, 20);
      break;
    case MODE_LOCKED:
      locked=!locked;
      updateEEPROM(EEPROM_LOCKED,locked);
      new_mode=MODE_OFF;
      break;
    }
    mode=new_mode;
  }
}
  
