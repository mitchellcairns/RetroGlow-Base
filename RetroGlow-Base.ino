#include <FastLED.h>
#include <EEPROM.h>
#include "config.h"

// PIN setup
// ----------------
#define selPin    PCINT0
#define lPin      PCINT2
#define rPin      PCINT1
#define ledPin    4

// Save check variable to see if save data exists
#define SAVE_CHECK  35

// Led Stuff
// Max brightness to prevent power issues
#define LED_COUNT 9
#define MAX_BRIGHTNESS 200
#define MAX_SPEED 64
#define FADE_INCREMENT 2

// Use this boolean to tell the loop when it should update
// the LED colors (so it's not *always* sending color updates)
bool colorSet = false;

// Use this boolean to tell the MCU when to do a button logic check
bool buttonSet = false;

// Definitions for the different color modes
#define COLORMODES      4
#define COLORMODE_USER  0
#define COLORMODE_RGB   1
#define COLORMODE_PARTY 2
#define COLORMODE_SOLID 3
byte colorMode = 1;
// Set to false to initiate colorInit
bool colorModeSet = false;

// Definitions for the different edit modes
// Idle is when you ain't editin!
#define EDITMODES       4
#define EDIT_IDLE       0
#define EDIT_BRIGHTNESS 1
#define EDIT_COLORMODE  2
#define EDIT_CMSET      3
byte editMode = 0;

// Definitions for the sub-edit slots
// Multi-purpose and vary from mode to mode
#define SED_ZERO  0
#define SED_ONE   1
#define SED_TWO   2
byte seditMode = 0;
// Number used will change per mode.
// Adjust dynamically.
byte seditModes = 0;

// Calculate rainbowcolor VALUE based on saturation
byte scaledRCV(byte saturation)
{
  float tmp = (0.29411 * saturation) + 180;
  return (byte) tmp;
}

// Color related values
// rainbowColor is used whenever we need a chsv value (color in Hue, Saturation, Value/Brightness format)
byte rainbowTime = 16; // How much rainbow value increases each cycle. (Higher is faster, lower is slower).
CHSV rainbowColor = CHSV(0, 255, scaledRCV(255));
byte brightness = 100;

// Primary 'leds' variable which contains all the colors in an array we need to set each LED
CRGB leds[LED_COUNT] = {CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black};

// User preference object initialization
UserPreference upref;

// button check functions

// Button states for individual buttons
#define NO_PRESS    0
#define HELD        1
#define COMPLETED   2
// For select after another press was done
#define LIMBO       3

byte selState = 0;
byte lState = 0;
byte rState = 0;

// Global button state flag for taking actions in the main loop
// based on different button combos
#define NO_PRESS    0
#define SEL_ONLY    1
#define L_ONLY      2
#define R_ONLY      3
#define SEL_L       4
#define SEL_R       5
byte buttonState = 0;

// colorChange states
// This is used to help determine which color fade
// type is used and if we should loop
// Idle for no changing
#define COLOR_IDLE    0
// Change for a single change
#define COLOR_CHANGE  1
// Cycle for continuously changing
#define COLOR_CYCLE   2
byte colorState = COLOR_CHANGE;

int counter = 0;

// Function to update button state
byte updateButtonState(byte newValue, byte currentState)
{
    if (currentState == HELD and newValue == HIGH) return COMPLETED;
    else if (currentState == NO_PRESS and newValue == LOW) return HELD;
    else if (currentState == COMPLETED) return NO_PRESS;
    else if (currentState == HELD and newValue == LOW) return HELD;
    else if (currentState == LIMBO and newValue == HIGH) return NO_PRESS;

    return NO_PRESS;
}

// Variables for reading the pins
volatile byte tmpSel;
volatile byte tmpL;
volatile byte tmpR;

// Interrupt function
ISR(PCINT0_vect)
{ 
    // Since an interrupt was triggered
    // One of the pin values changed; let's figure out 
    // which one(s) it was!
    tmpSel = digitalRead(selPin);
    tmpL = digitalRead(lPin);
    tmpR = digitalRead(rPin);

    // Set the buttonSet flag to true so we can run a button logic check!
    buttonSet = true;
}

// Perform a button logic check if the flag is set by the interrupt
void buttonLogicCheck()
{
    // Reset button state
    buttonState = NO_PRESS;
  
    // Update all button states accordingly
    selState = updateButtonState(tmpSel, selState);
    lState = updateButtonState(tmpL, lState);
    rState = updateButtonState(tmpR, rState);

    // Check and update the button state byte flag
    if (selState == HELD or selState == LIMBO)
    {
        if (lState == COMPLETED)
        {
          lState = NO_PRESS;
          selState = LIMBO;
          buttonState = SEL_L;
        }
        else if (rState == COMPLETED)
        {
          rState = NO_PRESS;
          selState = LIMBO;
          buttonState = SEL_R;
        }
    }
    
    else if (selState == COMPLETED)
    {
      selState = NO_PRESS;
      buttonState = SEL_ONLY;
    }
    
    else if (lState == COMPLETED)
    {
      lState = NO_PRESS;
      buttonState = L_ONLY;
    }
    
    else if (rState == COMPLETED) 
    {
      rState = NO_PRESS;
      buttonState = R_ONLY;
    }

    buttonSet = false;
}

// LED Groups
// each item in the array indicates the last LED in the group.
// For example, if the second number is 1, we know there's only one
// LED in the group 0. If the second number is 4, we know it contains 1, 2, 3 and 4.
byte led_groups[GROUP_COUNT] = { 
  3,  // Dpad
  4,  // Brightness
  5,  // Select
  6,  // Start
  7,  // B
  8   // A
  };
byte buttonIndex = 0;

CRGB lastGroupColor[GROUP_COUNT] = {};
CRGB thisGroupColor[GROUP_COUNT] = {};
CRGB nextGroupColor[GROUP_COUNT] = {};
CRGB colorPreset[GROUP_COUNT] = {CRGB::Red, CRGB::Orange, CRGB::Yellow, CRGB::Green, CRGB::Blue, CRGB::Purple};

// Set color of one group. 
// group is the button group 2d array
// color is the color you want
// loadDefault is a bool for whether or not the group is set to load the preset default
void setGroupColor(byte group, CRGB color, bool loadDefault)
{   
    fill_solid(nextGroupColor, GROUP_COUNT, CHSV(0,0,0));
    byte lastFilled = 0;
  
    if (loadDefault == true)
    {
      nextGroupColor[group] = colorPreset[group];
    }
    else 
    {
      nextGroupColor[group] = color;
      colorPreset[group] = color;
    } 
}

void updateLEDs(void)
{
  for (byte i = 0; i < GROUP_COUNT; i++)
  {
    byte tmpStart = (!i) ? 0 : led_groups[i-1] + 1;
    
    for (byte s = tmpStart; s <= led_groups[i]; s++)
    {
      leds[s] = thisGroupColor[i];
    }
  }
}


void setup() {

  pinMode(selPin, INPUT);
  pinMode(lPin, INPUT);
  pinMode(rPin, INPUT);
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);

  

  if (digitalRead(selPin) == LOW && digitalRead(lPin) == LOW)
  {
    upref = {};
    EEPROM.put(0,upref);
    delay(100);
  }
  else
  {
    delay(20);
    EEPROM.get(0, upref);
    delay(20);
    // Try to load upref data
    if (upref.saved == SAVE_CHECK)
    {
      brightness = upref.brightness;
      colorMode = upref.colorMode;
      rainbowTime = upref.rainbowTime;
      rainbowColor.hue = upref.hue;
      rainbowColor.saturation = upref.saturation;
      rainbowColor.value = scaledRCV(rainbowColor.saturation);
      memcpy(colorPreset, upref.ledPreset, sizeof(colorPreset));
    }
  }

  // Initialize fastLED library. GRB is neo pixel 2020's typically but not always. Check your specifications.
  FastLED.addLeds<NEOPIXEL, ledPin>(leds, LED_COUNT).setCorrection(CRGB(255, 125, 255));
  FastLED.setBrightness(brightness);

  // BOOT ANIMATION :)

  FastLED.delay(600);

  CHSV animColor;
  animColor.saturation = 255;
  animColor.hue = 0;
  animColor.value = 255;
  byte rColor = 0;

  while(rColor < 10)
  {
    for(byte y = 9; y > 1; y--)
    {
      leds[y-1] = leds[y-2];
    }
    leds[0] = animColor;
    if (animColor.hue < 165)
    {
      animColor.hue += 8;
    }
    else
    {
      rColor++;
    }
    FastLED.show();
    FastLED.delay(50);
  }
  FastLED.delay(100);
  rColor = 0;
  byte purpCount = 0;
  byte tmpColor = animColor.hue;

  while(rColor < 20)
  {
    for(byte y = 9; y > 1; y--)
    {
      leds[y-1] = leds[y-2];
    }
    leds[0] = animColor;
    if (purpCount < 10)
    {
      animColor.hue += 8;
      purpCount++;
    }
    else
    {
      animColor.hue -=8;
      if (animColor.hue <= tmpColor) animColor.hue = tmpColor;
      rColor++;
    }
    FastLED.show();
    FastLED.delay(45);
  }

  if (colorMode == COLORMODE_PARTY || colorMode == COLORMODE_RGB) 
  {
    rainbowColor.hue = animColor.hue;
    rainbowColor.saturation = animColor.saturation;
  }

  fill_solid(leds, LED_COUNT, animColor);
  fill_solid(thisGroupColor, GROUP_COUNT, animColor);
  FastLED.show();
  FastLED.delay(200);
  colorInit();

  cli();
  // Set up interrupts
  GIMSK = 0b00100000;    // turns on pin change interrupts
  PCMSK |= (1 << selPin);
  PCMSK |= (1 << lPin);
  PCMSK |= (1 << rPin);
  sei(); 
}

// Function that performs a visual double flash to mark a change
void switchIndicator(CRGB col) {
  FastLED.setBrightness(30);
  for (byte i = 0; i < 2; i++)
  {
    fill_solid(leds, LED_COUNT, col);
    FastLED.show();
    delay(100);
    FastLED.clear();
    FastLED.show();
    delay(100);
  }
  FastLED.setBrightness(brightness);
}


// List out all the button function types here
#define SAT_CHANGE      0
#define HUE_CHANGE      1
#define BUTTON_CHANGE   2
#define SPEED_CHANGE    3

#define COLORMODE_CHANGE  4
#define EDITMODE_CHANGE   5
#define SEDITMODE_CHANGE  6

#define BRIGHTNESS_CHANGE 7
#define SAVE_SETTINGS     8

// List out modifier parameters here
static int SAT_INCREMENT = 8;
static int HUE_INCREMENT = 8;
static int BRIGHTNESS_INCREMENT = 8;
static int SPEED_INCREMENT = 8;

// Define function for value changing
// also handles overflows when it returns the value
byte adjustValue(int changing, int cap, int increment, bool loop)
{
  int adjusted = changing + increment;
  
  if (adjusted > cap)
  {
    if (loop == true) 
    {
      adjusted = adjusted - cap - 1;
      return (byte) adjusted;
    }
    else 
    {
      return cap;
    }
  }
  else if (adjusted < 0)
  {
    if (loop == true) 
    {
      adjusted = adjusted + cap + 1;
      return (byte) adjusted;
    }
    else 
    {
      return 0;
    }
  }
  else return (byte) adjusted;
  
}

// Define what each button function does and the param it takes in
void buttonFunction(byte functionType, int parameter)
{
  if (functionType == SAT_CHANGE)
  {
    rainbowColor.saturation = adjustValue(rainbowColor.saturation, 255, parameter, false);
    rainbowColor.value = scaledRCV(rainbowColor.saturation);
    colorInit();
  }
  else if (functionType == HUE_CHANGE)
  {
    rainbowColor.hue = adjustValue(rainbowColor.hue, 255, parameter, true);
    colorInit();
  }
  else if (functionType == COLORMODE_CHANGE)
  {
    colorMode = adjustValue(colorMode, COLORMODES-1, parameter, true);
    colorModeSet = false;
    colorInit();
  }
  else if (functionType == EDITMODE_CHANGE)
  {
    switchIndicator(CRGB::Green);
    editMode = adjustValue(editMode, EDITMODES-1, parameter, false);
    seditMode = SED_ZERO;
    colorModeSet = false;
    colorInit();
  }
  else if (functionType == BRIGHTNESS_CHANGE)
  {
    brightness = adjustValue(brightness, MAX_BRIGHTNESS, parameter, false);
    FastLED.setBrightness(brightness);
    FastLED.show();
  }
  else if (functionType == SEDITMODE_CHANGE)
  {
    switchIndicator(CRGB::Red);
    seditMode = adjustValue(seditMode, seditModes-1, parameter, true);
    colorInit();
  }
  else if (functionType == BUTTON_CHANGE)
  {
    buttonIndex = adjustValue(buttonIndex, GROUP_COUNT-1, parameter, true);
    colorInit();
  }
  else if (functionType == SAVE_SETTINGS)
  {
    if (colorMode != COLORMODE_RGB and colorMode != COLORMODE_USER and colorMode != COLORMODE_PARTY)
    {
      upref.hue = rainbowColor.hue;
    }

    if (colorMode != COLORMODE_USER)
    {
      upref.saturation = rainbowColor.saturation;
    }
    
    upref.brightness = brightness;
    upref.colorMode = colorMode;
    upref.rainbowTime = rainbowTime;
    upref.saved = SAVE_CHECK;
    memcpy(upref.ledPreset, colorPreset, sizeof(colorPreset));

    EEPROM.put(0,upref);
    delay(100);
    editMode = EDIT_IDLE;
    seditMode = SED_ZERO;
    
    switchIndicator(CRGB::Blue);
    colorInit();
    
  }
  else if (functionType == SPEED_CHANGE)
  {
    rainbowTime = adjustValue(rainbowTime, 64, parameter, false);
    if (rainbowTime < 1) rainbowTime = 1;
  }

}

// Use this to handle color changing etc
// We want every color fade to take the same
// amount of time. We can simply control the increment
// that is changed during rainbow modes instead for
// smoother control of output color/avoid weird flickering issues.
void colorTick()
{
    // Different behavior depending on the color state
    if (colorState == COLOR_IDLE) return;

    if (colorState != COLOR_IDLE && counter == 0)
    {
      memcpy(lastGroupColor, thisGroupColor, sizeof(lastGroupColor));
      colorInit();
    }
    
    if (colorState != COLOR_IDLE && counter < 256)
    {
      for (byte x = 0; x < GROUP_COUNT; x++)
      {
        thisGroupColor[x] = blend(lastGroupColor[x], nextGroupColor[x], (byte) counter);
        updateLEDs();
      }
      FastLED.delay(1);
      FastLED.show();
      counter += FADE_INCREMENT;
    }
    else
    {
      colorModeSet = true;
      memcpy(thisGroupColor, nextGroupColor, sizeof(thisGroupColor));
      updateLEDs();
      FastLED.show();
      counter = 0;
      
      if (colorState == COLOR_CHANGE)
      {
        colorState = COLOR_IDLE;
        return;
      }
    }
}

// Use to initialize colors when changing modes or booting
void colorInit()
{
  counter = 0;

  if (!colorModeSet and upref.saved == SAVE_CHECK)
  {
    rainbowColor = CHSV(upref.hue, upref.saturation, scaledRCV(upref.saturation));
    
  }
  else if (!colorModeSet)
  {
    rainbowColor = CHSV(0,255,scaledRCV(255));
  }
  
  if (colorMode == COLORMODE_USER)
  {
    if (!colorModeSet)
    {
      seditModes = 3;
      buttonIndex = 0;
      if (upref.saved == SAVE_CHECK)
      {
        memcpy(colorPreset, upref.ledPreset, sizeof(colorPreset));
      }
    }

    if (editMode != EDIT_CMSET)
    {
      memcpy(nextGroupColor, colorPreset, sizeof(colorPreset));
    }
    else
    {
      setGroupColor(buttonIndex, rainbowColor, seditMode == SED_ZERO);
    }

    colorState = COLOR_CHANGE;  
  }
  else if (colorMode == COLORMODE_RGB)
  {
    if (!colorModeSet)
    {
      seditModes = 2;
      rainbowColor.hue = 0;
    }
    else
    {
      rainbowColor.hue = adjustValue(rainbowColor.hue, 255, rainbowTime, true);
    }
    colorState = COLOR_CYCLE;
    fill_solid(nextGroupColor, GROUP_COUNT, rainbowColor);
    
  }
  else if (colorMode == COLORMODE_PARTY)
  {
    if (!colorModeSet)
    {
      seditModes = 2;
      fill_solid(nextGroupColor, GROUP_COUNT, rainbowColor);
      colorModeSet = true;
    }
    else
    {
      for (byte i = GROUP_COUNT; i > 1; i--)
      {
        nextGroupColor[i-1] = nextGroupColor[i-2];
      }
      rainbowColor.hue = adjustValue(rainbowColor.hue, 255, rainbowTime, true);
      nextGroupColor[0] = rainbowColor;
    }
    colorState = COLOR_CYCLE;
  }
  else if (colorMode == COLORMODE_SOLID)
  {
    if (!colorModeSet)
    {
      seditModes = 2;
    }

    fill_solid(nextGroupColor, GROUP_COUNT, rainbowColor);
    colorState = COLOR_CHANGE;
  }
}

// UI TREE
// HERES ALL THE USER INTERFACE AND BUTTON ACTION :)
void uiTree()
{

  // EDIT MODE IDLE
  
  if (editMode == EDIT_IDLE)
  {
    if (buttonState == SEL_R) buttonFunction(EDITMODE_CHANGE, 1);
  }
  else
  {

    // RETURN FUNCTION GLOBAL IF NOT IDLE    
    if (buttonState == SEL_L) buttonFunction(EDITMODE_CHANGE, -1);

    // EDIT BRIGHTNESS
    else if (editMode == EDIT_BRIGHTNESS)
    {
      if (buttonState == L_ONLY)
      {
        buttonFunction(BRIGHTNESS_CHANGE, -1*BRIGHTNESS_INCREMENT);
      }
      else if (buttonState == R_ONLY) 
      {
        buttonFunction(BRIGHTNESS_CHANGE, BRIGHTNESS_INCREMENT);
      }
      else if (buttonState == SEL_R) 
      {
        buttonFunction(EDITMODE_CHANGE, 1);
      }
    }

    // EDIT COLOR MODE
    else if (editMode == EDIT_COLORMODE)
    {
      if (buttonState == L_ONLY) buttonFunction(COLORMODE_CHANGE, -1);
      else if (buttonState == R_ONLY) buttonFunction(COLORMODE_CHANGE, 1);
      else if (buttonState == SEL_R) buttonFunction(EDITMODE_CHANGE, 1);
    }

    // EDIT COLORMODE SET
    else if (editMode == EDIT_CMSET)
    {

      // CMSET GLOBAL SELECT BUTTON CHANGE OPTION
      if (buttonState == SEL_ONLY) buttonFunction(SEDITMODE_CHANGE, 1);

      // CMSET GLOBAL SAVE FUNCTION
      if (buttonState == SEL_R) buttonFunction(SAVE_SETTINGS, 0);

      // CMSET USER PRESET EDIT MODE
      if (colorMode == COLORMODE_USER)
      {
        if (seditMode == SED_ZERO)
        {
          if (buttonState == R_ONLY) buttonFunction(BUTTON_CHANGE, 1);
          else if (buttonState == L_ONLY) buttonFunction(BUTTON_CHANGE, -1);
        }
        else if (seditMode == SED_ONE)
        {
          if (buttonState == R_ONLY) buttonFunction(HUE_CHANGE, HUE_INCREMENT);
          else if (buttonState == L_ONLY) buttonFunction(HUE_CHANGE, -HUE_INCREMENT);
        }
        else if (seditMode == SED_TWO)
        {
          if (buttonState == R_ONLY) buttonFunction(SAT_CHANGE, SAT_INCREMENT);
          else if (buttonState == L_ONLY) buttonFunction(SAT_CHANGE, -SAT_INCREMENT);
        }
        
      }

      // PARTY AND RGB COMMON OPTION FOR SED_ZERO
      else if ((colorMode == COLORMODE_PARTY or colorMode == COLORMODE_RGB) and seditMode == SED_ZERO)
      {
        if (buttonState == R_ONLY) buttonFunction(SPEED_CHANGE, SPEED_INCREMENT);
        else if (buttonState == L_ONLY) buttonFunction(SPEED_CHANGE, -SPEED_INCREMENT);
      }

      // Solid color SED_ZERO options
      else if (colorMode == COLORMODE_SOLID and seditMode == SED_ZERO)
      {
        if (buttonState == R_ONLY) buttonFunction(HUE_CHANGE, HUE_INCREMENT);
        else if (buttonState == L_ONLY) buttonFunction(HUE_CHANGE, -HUE_INCREMENT);
      }

      // Common options for party, rgb, and solid color modes on SED_ONE
      else if ((colorMode == COLORMODE_PARTY or colorMode == COLORMODE_RGB or colorMode == COLORMODE_SOLID) and seditMode == SED_ONE)
      {
        if (buttonState == R_ONLY) buttonFunction(SAT_CHANGE, SAT_INCREMENT);
        else if (buttonState == L_ONLY) buttonFunction(SAT_CHANGE, -SAT_INCREMENT);
      }

    }
  }

  buttonState = NO_PRESS;
}

// Main program loop
void loop() {

  // When the button flag gets set by an interrupt, run the button logic check.
  if (buttonSet) buttonLogicCheck();

  if (buttonState != NO_PRESS)
  {
    uiTree();
  }

  // If color is not set, set it
  if (colorState != COLOR_IDLE)
  {
    colorTick();
  }

}
